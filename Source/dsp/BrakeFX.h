#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DspUtil.h"
#include "Trigger.h"

namespace bm
{
//==============================================================================
/**
    Turntable section at the end of the chain (it stops everything, tails included).

    BRAKE  vinyl stop: playback speed falls from 1 to 0 over the brake time.
    SPIN   backspin: the record is thrown backwards and spins down.

    After the effect has run out it stays silent until the shot ends
    (e.g. "TO BAR": back in on the next downbeat).
*/
class BrakeFX
{
public:
    enum Kind { none = -1, kindBrake = 0, kindSpin = 1 };

    explicit BrakeFX (const Grid& g) : trigger (g) {}

    Trigger trigger;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        size = (int) std::ceil (sampleRate * 8.0);
        hist.setSize (2, size);
        mix.setTime (0.0025f, sr);
        reset();
    }

    void reset()
    {
        hist.clear();
        wAbs = 0;
        kind = none;
        mix.snap (0.0f);
        rate = 1.0f;
        trigger.reset();
    }

    void setTimeBeats (double beats) { timeBeats = beats; }
    float getRate() const { return kind == none ? 1.0f : rate; }

    void process (float* L, float* R, int n, const SegmentClock& c)
    {
        float* h[2] = { hist.getWritePointer (0), hist.getWritePointer (1) };

        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq0 + i * c.beatsPerSample;
            const auto ev = trigger.tick (ppq, c.samplesPerBeat);

            const int wi = (int) (wAbs % size);
            h[0][wi] = L[i];
            h[1][wi] = R[i];

            if (ev.started)
            {
                kind = trigger.index() == 1 ? kindSpin : kindBrake;
                t = 0;
                len = std::max (64.0, timeBeats * c.samplesPerBeat);
                readPos = (double) wAbs;
                mix.setTarget (1.0f);
            }
            if (ev.stopped)
                mix.setTarget (0.0f);

            if (kind != none)
            {
                float gain = 1.0f;
                const double u = (double) t / len;

                if (kind == kindBrake)
                {
                    rate = (float) std::max (0.0, 1.0 - u);
                    gain = (float) std::clamp ((1.0 - u) * 6.0, 0.0, 1.0);
                }
                else
                {
                    constexpr double throwSpeed = 3.0;
                    const double grab = std::min (0.08 * len, 0.05 * sr);
                    if (t < grab)
                    {
                        const double s = t / grab;
                        rate = (float) (1.0 + (-throwSpeed - 1.0) * (0.5 - 0.5 * std::cos (3.14159265 * s)));
                    }
                    else
                    {
                        const double u2 = std::min (1.0, (t - grab) / std::max (1.0, len - grab));
                        rate = (float) (-throwSpeed * (1.0 - u2) * (1.0 - u2));
                        gain = (float) std::clamp ((1.0 - u2) * 5.0, 0.0, 1.0);
                    }
                }

                float y[2] = { 0.0f, 0.0f };
                if (gain > 0.0f)
                {
                    const double ip = std::floor (readPos);
                    const float frac = (float) (readPos - ip);
                    const int64_t base = (int64_t) ip;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        auto s = [&] (int64_t k) { k = std::min (k, wAbs); return h[ch][(int) (((k % size) + size) % size)]; };
                        y[ch] = hermite (s (base - 1), s (base), s (base + 1), s (base + 2), frac) * gain;
                    }
                }

                readPos = std::clamp (readPos + rate, (double) (wAbs - size + 8), (double) wAbs + 1.0);
                ++t;

                const float m = mix.next();
                L[i] += (y[0] - L[i]) * m;
                R[i] += (y[1] - R[i]) * m;

                if (mix.target <= 0.0f && mix.value <= 0.0f)
                    kind = none;
            }

            ++wAbs;
        }
    }

private:
    float sr = 44100.0f;
    int size = 1;
    juce::AudioBuffer<float> hist;
    int64_t wAbs = 0;

    int kind = none;
    int64_t t = 0;
    double len = 1.0, readPos = 0.0, timeBeats = 1.0;
    float rate = 1.0f;
    Ramp mix;
};

} // namespace bm
