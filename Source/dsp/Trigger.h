#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../Params.h"

namespace bm
{
//==============================================================================
/** Musical grid shared by all sections. Updated once per host block. */
struct Grid
{
    int    quant      = qBeat;
    double barLen     = 4.0;   // beats per bar
    double barOrigin  = 0.0;   // ppq of any bar line, reduced modulo barLen
    double blockBeats = 0.0;   // length of the current host block in beats

    double gridLen() const
    {
        switch (quant)
        {
            case qOff:     return 0.0;
            case qQuarter: return 0.25;
            case qBeat:    return 1.0;
            default:       return barLen;
        }
    }

    double origin() const { return quant == qBar ? barOrigin : 0.0; }

    /** How late a press may come and still snap to the grid line that just passed. */
    double tolerance() const
    {
        const double g = gridLen();
        return std::max (std::min (g, 1.0) * 0.0625, blockBeats + 1.0e-6);
    }

    static double floorTo (double ppq, double g, double org)
    {
        return org + std::floor ((ppq - org) / g + 1.0e-9) * g;
    }

    double prevBar (double ppq) const  { return floorTo (ppq, barLen, barOrigin); }
    double nextBarAfter (double ppq) const { return prevBar (ppq + 1.0e-6) + barLen; }
};

/** Timing of one processed segment. */
struct SegmentClock
{
    double ppq0           = 0.0;   // ppq at the first sample of the segment
    double beatsPerSample = 0.0;
    double samplesPerBeat = 22050.0;
};

//==============================================================================
/**
    Quantised on/off logic for a group of pads.

    Hold / Latch: the effect runs while at least one pad is held. Start and end
                  snap to the quantise grid.
    Timed modes:  one press starts a shot of fixed length (1 beat ... 4 bars,
                  or "to bar" = until the next bar line). Pressing another pad
                  during a shot only changes the beat value, pressing the same
                  pad again stops it on the next grid line.
*/
class Trigger
{
public:
    enum class State { idle, pending, active };

    struct Event
    {
        bool started     = false;
        bool stopped     = false;
        int  lateSamples = 0;   // how far after the grid line the start happened
    };

    explicit Trigger (const Grid& g) : grid (g) {}

    void reset()
    {
        state = State::idle;
        heldMask = 0;
        hasEnd = endAfterStart = false;
    }

    void setMode (int newMode, double ppq)
    {
        if (newMode == mode)
            return;

        const bool bothTimed = isTimed (mode) && isTimed (newMode);
        mode = newMode;
        heldMask = 0;

        if (state == State::pending && ! bothTimed)
            state = State::idle;
        else if (state == State::active && ! bothTimed)
            scheduleEnd (ppq);
    }

    void press (int idx, double ppq)
    {
        if (isTimed (mode))
        {
            if (state == State::idle)
            {
                current = idx;
                scheduleStart (ppq, true);
            }
            else if (idx == current)
            {
                if (state == State::pending) state = State::idle;
                else                         scheduleEnd (ppq);
            }
            else
            {
                current = idx;   // switch beat value, keep the timing
            }
            return;
        }

        heldMask |= (1u << idx);
        order[idx] = ++counter;
        current = idx;
        updateRequest (ppq);
    }

    void release (int idx, double ppq)
    {
        if (isTimed (mode))
            return;

        heldMask &= ~(1u << idx);

        if (heldMask != 0)
        {
            uint64_t best = 0;
            for (int i = 0; i < 32; ++i)
                if ((heldMask & (1u << i)) && order[i] >= best) { best = order[i]; current = i; }
        }

        updateRequest (ppq);
    }

    void releaseAll (double ppq)
    {
        heldMask = 0;
        if (! isTimed (mode))
            updateRequest (ppq);
    }

    Event tick (double ppq, double samplesPerBeat)
    {
        Event ev;

        if (state == State::pending && ppq >= startPpq - 1.0e-9)
        {
            state = State::active;
            ev.started = true;
            ev.lateSamples = std::max (0, (int) std::lround ((ppq - gridStart) * samplesPerBeat));

            if (timedShot)
            {
                hasEnd = true;
                endPpq = gridStart + timedLength (gridStart);
            }
            else if (endAfterStart)
            {
                hasEnd = true;
                endPpq = gridStart + std::max (grid.gridLen(), 0.0625);
            }
        }

        if (state == State::active && hasEnd && ppq >= endPpq - 1.0e-9)
        {
            state = State::idle;
            hasEnd = false;
            ev.stopped = true;
        }

        return ev;
    }

    /** Host transport jumped (loop, relocation): keep everything relative. */
    void shift (double delta)
    {
        startPpq += delta;
        endPpq += delta;
        gridStart += delta;
    }

    State  getState() const     { return state; }
    bool   isActive() const     { return state == State::active; }
    int    index() const        { return current; }
    double getGridStart() const { return gridStart; }
    int    getMode() const      { return mode; }

    /** 0..1 progress of a shot with a known end, or -1. */
    float progress (double ppq) const
    {
        if (state != State::active || ! hasEnd || endPpq <= gridStart)
            return -1.0f;
        return (float) std::clamp ((ppq - gridStart) / (endPpq - gridStart), 0.0, 1.0);
    }

private:
    void updateRequest (double ppq)
    {
        if (heldMask != 0)
        {
            if (state == State::idle)                 scheduleStart (ppq, false);
            else if (state == State::active && hasEnd) hasEnd = false;   // re-pressed before the end
            else if (state == State::pending)          endAfterStart = false;
        }
        else
        {
            if (state == State::pending)     endAfterStart = true;   // quick tap: run one grid step
            else if (state == State::active) scheduleEnd (ppq);
        }
    }

    void scheduleStart (double ppq, bool timed)
    {
        timedShot = timed;
        endAfterStart = false;
        hasEnd = false;
        state = State::pending;

        const double g = grid.gridLen();
        if (g <= 0.0)
        {
            startPpq = gridStart = ppq;
            return;
        }

        const double prev = Grid::floorTo (ppq, g, grid.origin());
        if (ppq - prev <= grid.tolerance())
        {
            startPpq = ppq;     // a hair late: start now, aligned to the line that just passed
            gridStart = prev;
        }
        else
        {
            startPpq = gridStart = prev + g;
        }
    }

    void scheduleEnd (double ppq)
    {
        const double g = grid.gridLen();
        double e = ppq;

        if (g > 0.0)
        {
            const double prev = Grid::floorTo (ppq, g, grid.origin());
            e = (ppq - prev <= grid.tolerance()) ? ppq : prev + g;
            e = std::max (e, gridStart + g);
        }

        endPpq = hasEnd ? std::min (endPpq, e) : e;
        hasEnd = true;
    }

    double timedLength (double from) const
    {
        switch (mode)
        {
            case lenBeat1: return 1.0;
            case lenBeat2: return 2.0;
            case lenToBar: return grid.nextBarAfter (from) - from;
            case lenBar1:  return grid.barLen;
            case lenBar2:  return grid.barLen * 2.0;
            case lenBar4:  return grid.barLen * 4.0;
            default:       return 1.0;
        }
    }

    const Grid& grid;
    State    state = State::idle;
    int      mode = lenHold;
    int      current = 0;
    uint32_t heldMask = 0;
    uint64_t order[32] {};
    uint64_t counter = 0;
    bool     timedShot = false, endAfterStart = false, hasEnd = false;
    double   startPpq = 0.0, endPpq = 0.0, gridStart = 0.0;
};

} // namespace bm
