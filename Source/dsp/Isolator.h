#pragma once

#include <juce_dsp/juce_dsp.h>

namespace bm
{
//==============================================================================
/** 3-band isolator with Linkwitz-Riley crossovers (phase-coherent sum). */
class Isolator
{
public:
    static constexpr float lowXover  = 250.0f;
    static constexpr float highXover = 2500.0f;

    void prepare (double sampleRate, int maxBlock)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
        for (auto* f : { &lr1, &lr2, &ap })
            f->prepare (spec);

        lr1.setCutoffFrequency (lowXover);
        lr2.setCutoffFrequency (highXover);
        ap.setCutoffFrequency (highXover);
        ap.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

        for (auto& g : gains)
        {
            g.reset (sampleRate, 0.015);
            g.setCurrentAndTargetValue (1.0f);
        }
        mix.reset (sampleRate, 0.02);
        mix.setCurrentAndTargetValue (0.0f);
        reset();
    }

    void reset()
    {
        lr1.reset(); lr2.reset(); ap.reset();
        for (auto& g : gains)
            g.setCurrentAndTargetValue (g.getTargetValue());
    }

    /** dB values (<= -39.9 means -inf), kill flags override. */
    void setGains (float lowDb, float midDb, float highDb, bool killLow, bool killMid, bool killHigh)
    {
        auto toGain = [] (float db, bool kill) {
            return (kill || db <= -39.9f) ? 0.0f : juce::Decibels::decibelsToGain (db);
        };
        gains[0].setTargetValue (toGain (lowDb,  killLow));
        gains[1].setTargetValue (toGain (midDb,  killMid));
        gains[2].setTargetValue (toGain (highDb, killHigh));

        // all bands at 0 dB: fade the crossover out completely, the signal stays untouched
        bool neutral = true;
        for (auto& g : gains)
            neutral &= std::abs (g.getTargetValue() - 1.0f) < 1.0e-6f && ! g.isSmoothing();
        mix.setTargetValue (neutral ? 0.0f : 1.0f);
    }

    void process (float* L, float* R, int n)
    {
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const float gl = gains[0].getNextValue();
            const float gm = gains[1].getNextValue();
            const float gh = gains[2].getNextValue();
            const float m = mix.getNextValue();

            for (int c = 0; c < 2; ++c)
            {
                // filters always run so their state is warm when the isolator fades in
                const float x = ch[c][i];
                float low, rest, mid, high;
                lr1.processSample (c, x, low, rest);
                lr2.processSample (c, rest, mid, high);
                low = ap.processSample (c, low);
                const float y = low * gl + mid * gm + high * gh;
                ch[c][i] = m > 0.0f ? x + (y - x) * m : x;
            }
        }
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> lr1, lr2, ap;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gains[3];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix;
};

} // namespace bm
