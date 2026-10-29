#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DspUtil.h"
#include "Trigger.h"

namespace bm
{
//==============================================================================
/**
    The beat-synced pad section.

    ECHO  dry keeps playing, tempo-synced echo with feedback.
    STOP  "delay stop": the track cuts out on the grid line, the last beat value
          of audio before the cut repeats with feedback (the classic power break).
    ROLL  beat repeat / loop roll, grid-aligned, value can be switched while rolling.
    GATE  rhythmic chopper.
*/
class BeatFX
{
public:
    explicit BeatFX (const Grid& g) : trigger (g) {}

    Trigger trigger;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        size = (int) std::ceil (sampleRate * 6.5);
        hist.setSize (2, size);
        echo.setSize (2, size);
        roll.setSize (2, size);

        fadeLen   = std::max (8, (int) (0.003 * sampleRate));
        tapXfLen  = std::max (8, (int) (0.010 * sampleRate));
        rollXfLen = std::max (8, (int) (0.004 * sampleRate));

        stopDry.setTime (0.003f, sr);
        feedEnv.setTime (0.003f, sr);
        rollMix.setTime (0.003f, sr);
        gateGain.setTime (0.0012f, sr);
        reset();
    }

    void reset()
    {
        hist.clear(); echo.clear(); roll.clear();
        w = 0;
        curD = oldD = (int) (sr * 0.25f);
        tapXf = 0;
        capPos = capRemain = capLen = 0;
        rollOn = false; rollK = 0;
        gateOn = false;
        stopDry.snap (1.0f);
        feedEnv.snap (0.0f);
        rollMix.snap (0.0f);
        gateGain.snap (1.0f);
        tone[0].reset(); tone[1].reset();
        trigger.reset();
    }

    void setParams (int newType, bool tripletOn, float newDepth, float newFeedback, float newTone)
    {
        type = newType;
        tripletMul = tripletOn ? (2.0 / 3.0) : 1.0;
        depth = newDepth;
        feedback = std::min (newFeedback, 0.98f);

        toneMode = 0;
        if (newTone > 0.02f)
        {
            toneMode = 1;
            const float f = 20.0f * std::pow (100.0f, newTone);
            tone[0].set (f, 0.6f, sr); tone[1].set (f, 0.6f, sr);
        }
        else if (newTone < -0.02f)
        {
            toneMode = -1;
            const float f = 20000.0f * std::pow (0.04f, -newTone);
            tone[0].set (f, 0.6f, sr); tone[1].set (f, 0.6f, sr);
        }
    }

    double currentBeats() const { return padBeats[trigger.index()] * tripletMul; }
    int    getActiveType() const { return activeType; }

    void process (float* L, float* R, int n, const SegmentClock& c)
    {
        const double spb = c.samplesPerBeat;
        float* h[2] = { hist.getWritePointer (0), hist.getWritePointer (1) };
        float* e[2] = { echo.getWritePointer (0), echo.getWritePointer (1) };
        float* r[2] = { roll.getWritePointer (0), roll.getWritePointer (1) };

        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq0 + i * c.beatsPerSample;
            const auto ev = trigger.tick (ppq, spb);

            const double beats = currentBeats();
            const int D = std::clamp ((int) std::lround (beats * spb), 8, size - 8);

            if (ev.started) onStart (ev.lateSamples, D, h, e, r);
            if (ev.stopped) onStop();

            const float x[2] = { L[i], R[i] };
            h[0][w] = x[0];
            h[1][w] = x[1];

            // ---------------------------------------------------- echo engine (also renders tails)
            if (D != curD)
            {
                oldD = curD;
                curD = D;
                tapXf = tapXfLen;
            }

            float injGain;
            if (capRemain > 0)
            {
                injGain = std::min ({ 1.0f, (float) capPos / (float) fadeLen, (float) capRemain / (float) fadeLen });
                ++capPos;
                --capRemain;
            }
            else
            {
                injGain = feedEnv.next();
            }

            const int rCur = wrap (w - curD);
            const int rOld = wrap (w - oldD);
            const float xfA = tapXf > 0 ? (float) tapXf / (float) tapXfLen : 0.0f;
            if (tapXf > 0) --tapXf;

            float wet[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                float tap = e[ch][rCur];
                if (xfA > 0.0f)
                    tap = tap * (1.0f - xfA) + e[ch][rOld] * xfA;

                float fb = tap;
                if (toneMode != 0)
                {
                    tone[ch].process (tap);
                    fb = toneMode > 0 ? tone[ch].hp : tone[ch].lp;
                }

                const float v = softClip (h[ch][rCur] * injGain + fb * feedback);
                e[ch][w] = v;
                wet[ch] = v;
            }

            // ---------------------------------------------------- gate
            float gTarget = 1.0f;
            if (gateOn)
            {
                const double phase = std::fmod (ppq - gateGrid, beats) / beats;
                gTarget = phase < 0.5 ? 1.0f : 1.0f - depth;
            }
            gateGain.setTarget (gTarget);
            const float gg = gateGain.next();

            // ---------------------------------------------------- dry + echo
            // in ECHO the dry keeps playing, so the echo sits 3 dB under it
            const float dg = stopDry.next() * gg;
            const float wg = depth * (activeType == fxEcho ? 0.7f : 1.0f);
            float out[2] = { x[0] * dg + wet[0] * wg,
                             x[1] * dg + wet[1] * wg };

            // ---------------------------------------------------- roll
            if (rollOn)
            {
                if (rollK < size)
                {
                    r[0][rollK] = x[0];
                    r[1][rollK] = x[1];
                }
                const int recorded = std::min (rollK + 1, size);
                const int k = recorded - 1;

                const double rel = std::max (0.0, ppq - rollGrid);

                // read the loop for a given length, with a crossfade over the loop seam
                auto readRoll = [&] (double lenBeats, float* y)
                {
                    const int pi = std::clamp ((int) (std::fmod (rel, lenBeats) * spb), 0, k);
                    const int Dsi = (int) (lenBeats * spb);
                    const int F = std::max (1, std::min (rollXfLen, Dsi / 4));
                    y[0] = r[0][pi];
                    y[1] = r[1][pi];
                    if (rel >= lenBeats && pi < F && Dsi + pi < recorded)
                    {
                        const float a = ((float) pi + 0.5f) / (float) F;
                        y[0] = y[0] * a + r[0][Dsi + pi] * (1.0f - a);
                        y[1] = y[1] * a + r[1][Dsi + pi] * (1.0f - a);
                    }
                };

                if (std::abs (beats - rollBeats) > 1.0e-9)
                {
                    if (rollBeats > 0.0)   // value switched while rolling: blend old into new
                    {
                        rollPrevBeats = rollBeats;
                        rollSwXf = rollXfLen;
                    }
                    rollBeats = beats;
                }

                float y[2];
                readRoll (beats, y);
                if (rollSwXf > 0)
                {
                    float yo[2];
                    readRoll (rollPrevBeats, yo);
                    const float a = (float) rollSwXf / (float) rollXfLen;
                    y[0] = y[0] * (1.0f - a) + yo[0] * a;
                    y[1] = y[1] * (1.0f - a) + yo[1] * a;
                    --rollSwXf;
                }

                if (rollK < size) ++rollK;

                const float m = rollMix.next() * depth;
                out[0] += (y[0] - out[0]) * m;
                out[1] += (y[1] - out[1]) * m;

                if (rollMix.target <= 0.0f && rollMix.value <= 0.0f)
                    rollOn = false;
            }

            L[i] = out[0];
            R[i] = out[1];
            w = wrap (w + 1);
        }
    }

private:
    int wrap (int idx) const { idx %= size; return idx < 0 ? idx + size : idx; }

    void onStart (int late, int D, float* h[2], float* e[2], float* r[2])
    {
        activeType = type;
        late = std::clamp (late, 0, std::min (D - 1, size / 4));

        switch (activeType)
        {
            case fxStop:
            {
                stopDry.setTarget (0.0f);
                curD = oldD = D;
                tapXf = 0;
                capLen = D;
                capPos = 0;
                capRemain = D;

                // started a little after the grid line: write the part we missed,
                // so the loop stays locked to the grid
                for (int j = late; j > 0; --j)
                {
                    const int p = wrap (w - j);
                    const float g = std::min ({ 1.0f, (float) capPos / (float) fadeLen, (float) capRemain / (float) fadeLen });
                    for (int ch = 0; ch < 2; ++ch)
                        e[ch][p] = softClip (e[ch][p] + h[ch][wrap (p - D)] * g);
                    ++capPos;
                    --capRemain;
                }
                break;
            }

            case fxEcho:
                feedEnv.setTarget (1.0f);
                break;

            case fxRoll:
            {
                rollOn = true;
                rollGrid = trigger.getGridStart();
                rollK = 0;
                rollBeats = -1.0;
                rollSwXf = 0;
                for (int j = late; j > 0; --j)
                {
                    const int p = wrap (w - j);
                    r[0][rollK] = h[0][p];
                    r[1][rollK] = h[1][p];
                    ++rollK;
                }
                rollMix.setTarget (1.0f);
                break;
            }

            case fxGate:
                gateOn = true;
                gateGrid = trigger.getGridStart();
                break;

            default: break;
        }
    }

    void onStop()
    {
        switch (activeType)
        {
            case fxStop: stopDry.setTarget (1.0f); break;     // capture finishes on its own, tail rings out
            case fxEcho: feedEnv.setTarget (0.0f); break;
            case fxRoll: rollMix.setTarget (0.0f); break;
            case fxGate: gateOn = false; break;
            default: break;
        }
    }

    float sr = 44100.0f;
    int size = 1;
    juce::AudioBuffer<float> hist, echo, roll;
    int w = 0;

    // params
    int type = fxStop, activeType = fxStop;
    double tripletMul = 1.0;
    float depth = 1.0f, feedback = 0.55f;
    int toneMode = 0;
    SVF tone[2];

    // echo / stop
    int curD = 1, oldD = 1, tapXf = 0, tapXfLen = 1, fadeLen = 1;
    int capPos = 0, capRemain = 0, capLen = 0;
    Ramp stopDry, feedEnv;

    // roll
    bool rollOn = false;
    int rollK = 0, rollXfLen = 1, rollSwXf = 0;
    double rollGrid = 0.0, rollBeats = -1.0, rollPrevBeats = 1.0;
    Ramp rollMix;

    // gate
    bool gateOn = false;
    double gateGrid = 0.0;
    Ramp gateGain;
};

} // namespace bm
