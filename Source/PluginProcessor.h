#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "dsp/Trigger.h"
#include "dsp/Isolator.h"
#include "dsp/BeatFX.h"
#include "dsp/BuildFX.h"
#include "dsp/BrakeFX.h"

//==============================================================================
/** Values the editor reads for its displays (written by the audio thread). */
struct UiState
{
    std::atomic<double> ppq { 0.0 }, bpm { 128.0 }, barLen { 4.0 }, barOrigin { 0.0 };
    std::atomic<bool>   playing { false };

    std::atomic<int>    fxState { 0 }, fxIdx { 0 }, fxType { 0 };
    std::atomic<float>  fxProgress { -1.0f };

    std::atomic<int>    brakeState { 0 }, brakeIdx { 0 };
    std::atomic<float>  brakeRate { 1.0f }, brakeProgress { -1.0f };

    std::atomic<int>    riseState { 0 };
    std::atomic<float>  riseProgress { 0.0f }, buildAmount { 0.0f };

    std::atomic<int>    midiKillMask { 0 };
    std::atomic<float>  outLevel { 0.0f };
};

//==============================================================================
class BrecherProcessor : public juce::AudioProcessor
{
public:
    BrecherProcessor();
    ~BrecherProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NOVA BRECHER"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    UiState ui;

private:
    juce::AudioProcessorEditor* createEditor() override;

    void updateClock (int numSamples);
    void handleParamEvents (double ppq);
    void handleMidi (const juce::MidiMessage& m, double ppq);
    void render (float* L, float* R, int start, int len);
    void resetTriggerParams();

    // DSP
    bm::Grid grid;
    bm::Isolator isolator;
    bm::BeatFX beatFx { grid };
    bm::BuildFX buildFx;
    bm::BrakeFX brakeFx { grid };

    static constexpr int maxChunk = 512;
    std::vector<float> monoScratch;
    juce::SmoothedValue<float> outGainSm;

    // clock
    double sr = 44100.0, bpm = 128.0, spb = 22050.0, bps = 1.0 / 22050.0;
    double ppqBlock = 0.0, expectedPpq = 0.0;
    bool clockValid = false, hostPlaying = false;

    // parameter edge detection
    std::atomic<bool> needsResync { true };
    bool prevPad[bm::numPads] {};
    bool prevBrake = false, prevSpin = false, prevRise = false;
    uint32_t midiLatchMask = 0, midiBrakeLatch = 0;
    int midiKill = 0;

    // raw parameter pointers
    std::atomic<float>* pIso[3] {};
    std::atomic<float>* pKill[3] {};
    std::atomic<float>* pBuildAmt = nullptr;
    std::atomic<float>* pBuildFilter = nullptr;
    std::atomic<float>* pBuildNoise = nullptr;
    std::atomic<float>* pBuildVerb = nullptr;
    std::atomic<float>* pRiseLen = nullptr;
    std::atomic<float>* pRise = nullptr;
    std::atomic<float>* pFxType = nullptr;
    std::atomic<float>* pTriplet = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pFxLen = nullptr;
    std::atomic<float>* pPad[bm::numPads] {};
    std::atomic<float>* pBrake = nullptr;
    std::atomic<float>* pSpin = nullptr;
    std::atomic<float>* pBrakeTime = nullptr;
    std::atomic<float>* pBrakeLen = nullptr;
    std::atomic<float>* pQuant = nullptr;
    std::atomic<float>* pOut = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrecherProcessor)
};
