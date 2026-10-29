#include "Params.h"

namespace bm
{
namespace
{
    juce::StringArray toArray (const char* const* names, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i)
            a.add (names[i]);
        return a;
    }

    juce::ParameterID id (const juce::String& s) { return juce::ParameterID { s, 1 }; }

    std::unique_ptr<juce::AudioParameterFloat> bandGain (const char* pidName, const juce::String& name)
    {
        juce::NormalisableRange<float> r (-40.0f, 6.0f, 0.01f);
        r.setSkewForCentre (-6.0f);
        return std::make_unique<juce::AudioParameterFloat> (
            id (pidName), name, r, 0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) {
                    return v <= -39.9f ? juce::String ("-inf") : juce::String (v, 1);
                }));
    }

    std::unique_ptr<juce::AudioParameterFloat> unit (const char* pidName, const juce::String& name,
                                                     float lo, float hi, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id (pidName), name, juce::NormalisableRange<float> (lo, hi, 0.001f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (v, 2); }));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Isolate
    layout.add (bandGain (pid::isoLow,  "Iso Low"));
    layout.add (bandGain (pid::isoMid,  "Iso Mid"));
    layout.add (bandGain (pid::isoHigh, "Iso High"));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::killLow),  "Kill Low",  false));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::killMid),  "Kill Mid",  false));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::killHigh), "Kill High", false));

    // Build
    layout.add (unit (pid::buildAmt, "Build Amount", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::buildFilter), "Build Filter", true));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::buildNoise),  "Build Noise",  true));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::buildVerb),   "Build Reverb", true));
    {
        StringArray bars;
        for (auto b : riseBars)
            bars.add (String (b) + (b == 1 ? " Bar" : " Bars"));
        layout.add (std::make_unique<AudioParameterChoice> (id (pid::riseLen), "Rise Length", bars, 2));
    }
    layout.add (std::make_unique<AudioParameterBool> (id (pid::rise), "Rise Trigger", false));

    // Beat FX
    layout.add (std::make_unique<AudioParameterChoice> (id (pid::fxType), "FX Type", toArray (fxTypeNames, 4), fxStop));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::triplet), "Triplet", false));
    layout.add (unit (pid::fxDepth,    "FX Depth",    0.0f, 1.0f, 1.0f));
    layout.add (unit (pid::fxFeedback, "FX Feedback", 0.0f, 0.98f, 0.55f));
    layout.add (unit (pid::fxTone,     "FX Tone",    -1.0f, 1.0f, 0.25f));
    layout.add (std::make_unique<AudioParameterChoice> (id (pid::fxLength), "FX Length", toArray (lenNames, numLenModes), lenHold));
    for (int i = 0; i < numPads; ++i)
        layout.add (std::make_unique<AudioParameterBool> (id (pid::pad (i)), String ("Pad ") + padNames[i], false));

    // Brake / Spin
    layout.add (std::make_unique<AudioParameterBool> (id (pid::brake), "Brake", false));
    layout.add (std::make_unique<AudioParameterBool> (id (pid::spin),  "Spin",  false));
    layout.add (std::make_unique<AudioParameterChoice> (id (pid::brakeTime), "Brake Time", toArray (brakeTimeNames, numBrakeTimes), 2));
    layout.add (std::make_unique<AudioParameterChoice> (id (pid::brakeLen),  "Brake Length", toArray (lenNames, numLenModes), lenToBar));

    // Global
    layout.add (std::make_unique<AudioParameterChoice> (id (pid::quant), "Quantize", toArray (quantNames, 4), qBeat));
    layout.add (std::make_unique<AudioParameterFloat> (id (pid::outGain), "Output",
                                                       NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

} // namespace bm
