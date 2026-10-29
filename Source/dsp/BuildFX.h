#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "DspUtil.h"
#include "Trigger.h"

namespace bm
{
//==============================================================================
/**
    Build-up section: one amount (0..1) drives a resonant high-pass sweep,
    a noise riser and a reverb wash. The amount comes from the knob or from
    the automatic RISE, which runs over 1..16 bars starting on a bar line and
    snaps back to zero exactly on the downbeat at the end (the drop).
*/
class BuildFX
{
public:
    enum class RiseState { idle = 0, pending, active };

    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        sendL.assign ((size_t) maxBlock, 0.0f);
        sendR.assign ((size_t) maxBlock, 0.0f);
        amt.assign ((size_t) maxBlock, 0.0f);

        reverb.setSampleRate (sampleRate);
        juce::Reverb::Parameters p;
        p.roomSize = 0.9f;
        p.damping = 0.35f;
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);

        smoothK = 1.0f - std::exp (-1.0f / (0.004f * sr));
        verbK    = 1.0f - std::exp (-1.0f / (0.030f * sr));
        verbRelK = 1.0f - std::exp (-1.0f / (0.010f * sr));
        filterMix.setTime (0.012f, sr);
        cutK = 1.0f - std::exp (-1.0f / (0.25f * sr));
        for (auto* f : { &sendHp[0], &sendHp[1] })
            f->set (300.0f, 0.7f, sr);
        reset();
    }

    void reset()
    {
        A = 0.0f;
        verbGain = 0.0f;
        filterMix.snap (0.0f);
        cutA = 0.0f;
        reverb.reset();
        verbIdle = true;
        for (auto& f : hp)    f.reset();
        for (auto& f : noiseF) f.reset();
        for (auto& f : sendHp) f.reset();
        state = RiseState::idle;
    }

    void setParams (float manualAmount, bool filterOn, bool noiseOn, bool verbOn, int riseLenIdx)
    {
        manual = manualAmount;
        useFilter = filterOn;
        useNoise = noiseOn;
        useVerb = verbOn;
        lenIdx = std::clamp (riseLenIdx, 0, numRiseLens - 1);
    }

    /** RISE button: start (on the next bar), or - while rising - drop on the next beat. */
    void toggleRise (double ppq, const Grid& g)
    {
        if (state == RiseState::idle)
        {
            riseLen = riseBars[lenIdx] * g.barLen;
            if (g.quant == qOff)
            {
                riseStart = ppq;
            }
            else
            {
                const double prev = g.prevBar (ppq);
                riseStart = (ppq - prev <= g.tolerance()) ? prev : prev + g.barLen;
            }
            riseEnd = riseStart + riseLen;
            state = RiseState::pending;
        }
        else if (state == RiseState::pending)
        {
            state = RiseState::idle;
        }
        else
        {
            if (g.quant == qOff)
            {
                riseEnd = ppq;
            }
            else
            {
                const double prev = std::floor (ppq);
                riseEnd = std::min (riseEnd, (ppq - prev <= g.tolerance()) ? ppq : prev + 1.0);
            }
        }
    }

    void shift (double delta) { riseStart += delta; riseEnd += delta; }

    RiseState getState() const { return state; }
    float getAmount() const    { return A; }
    float riseProgress (double ppq) const
    {
        if (state != RiseState::active || riseLen <= 0.0) return 0.0f;
        return (float) std::clamp ((ppq - riseStart) / riseLen, 0.0, 1.0);
    }

    void process (float* L, float* R, int n, const SegmentClock& c)
    {
        jassert (n <= (int) amt.size());

        // ---------------------------------------------------- pass 1: amount, filter, noise, send
        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq0 + i * c.beatsPerSample;

            if (state == RiseState::pending && ppq >= riseStart)
                state = RiseState::active;

            float autoA = 0.0f;
            if (state == RiseState::active)
            {
                if (ppq >= riseEnd)
                    state = RiseState::idle;
                else
                    autoA = (float) std::pow (std::clamp ((ppq - riseStart) / riseLen, 0.0, 1.0), 1.6);
            }

            const float target = std::max (manual, autoA);
            A += (target - A) * smoothK;
            if (A < 1.0e-5f && target <= 0.0f) A = 0.0f;
            amt[(size_t) i] = A;

            const float xl = L[i], xr = R[i];

            if (useVerb)
            {
                sendHp[0].process (xl);
                sendHp[1].process (xr);
                sendL[(size_t) i] = sendHp[0].hp * A;
                sendR[(size_t) i] = sendHp[1].hp * A;
            }

            float yl = xl, yr = xr;

            // The cutoff follows the amount upwards, but falls slowly. When the amount drops fast
            // (the drop, or the knob slammed back) the filtered signal is crossfaded to dry instead
            // of sweeping the resonant filter down, which would throw a bass thump.
            cutA = A > cutA ? A : cutA + (A - cutA) * cutK;
            filterMix.setTarget (useFilter && cutA > 0.0005f ? 1.0f : 0.0f);
            const float fm = filterMix.next() * (cutA > 0.0f ? std::min (1.0f, A / cutA) : 0.0f);
            if (filterMix.value > 0.0f)
            {
                if ((coefCounter++ & 15) == 0)
                {
                    const float f = 20.0f * std::pow (120.0f, cutA);
                    const float q = 0.707f + 1.6f * cutA * cutA;
                    hp[0].set (f, q, sr);
                    hp[1].set (f, q, sr);
                    resComp = 1.0f / (1.0f + 0.35f * (q - 0.707f));
                }
                hp[0].process (xl);
                hp[1].process (xr);
                yl = xl + (hp[0].hp * resComp - xl) * fm;
                yr = xr + (hp[1].hp * resComp - xr) * fm;
                filterRunning = true;
            }
            else if (filterRunning)
            {
                hp[0].reset(); hp[1].reset();
                filterRunning = false;
                coefCounter = 0;
            }

            if (useNoise && A > 0.0f)
            {
                if ((noiseCounter++ & 15) == 0)
                {
                    const float f = 300.0f * std::pow (40.0f, A);
                    noiseF[0].set (f, 0.9f, sr);
                    noiseF[1].set (f, 0.9f, sr);
                }
                const float amp = 0.30f * A * A * std::sqrt (A);
                noiseF[0].process (noise[0].next());
                noiseF[1].process (noise[1].next());
                yl += noiseF[0].bp * amp;
                yr += noiseF[1].bp * amp;
            }

            L[i] = yl;
            R[i] = yr;
        }

        // ---------------------------------------------------- pass 2: reverb wash
        const bool needVerb = useVerb && (A > 0.0f || verbGain > 1.0e-4f || manual > 0.0f || state != RiseState::idle);
        if (! needVerb)
        {
            if (! verbIdle) { reverb.reset(); verbIdle = true; }
            verbGain = 0.0f;
            return;
        }
        verbIdle = false;

        reverb.processStereo (sendL.data(), sendR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float a = amt[(size_t) i];
            const float target = std::min (1.0f, a * 1.4f) * 0.28f;
            verbGain += (target - verbGain) * (target > verbGain ? verbK : verbRelK);   // fast release = clean drop
            L[i] += sendL[(size_t) i] * verbGain;
            R[i] += sendR[(size_t) i] * verbGain;
        }
    }

private:
    float sr = 44100.0f;
    float manual = 0.0f;
    bool useFilter = true, useNoise = true, useVerb = true;
    int lenIdx = 2;

    RiseState state = RiseState::idle;
    double riseStart = 0.0, riseEnd = 0.0, riseLen = 16.0;

    float A = 0.0f, smoothK = 0.01f;
    SVF hp[2], noiseF[2], sendHp[2];
    Noise noise[2] { Noise (0x9e3779b9u), Noise (0x7f4a7c15u) };
    int coefCounter = 0, noiseCounter = 0;
    bool filterRunning = false;
    Ramp filterMix;
    float cutA = 0.0f, cutK = 0.001f, resComp = 1.0f;

    juce::Reverb reverb;
    std::vector<float> sendL, sendR, amt;
    float verbGain = 0.0f, verbK = 0.01f, verbRelK = 0.01f;
    bool verbIdle = true;
};

} // namespace bm
