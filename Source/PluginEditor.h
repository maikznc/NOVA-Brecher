#pragma once

#include "PluginProcessor.h"
#include "gui/Widgets.h"

//==============================================================================
/** The whole front panel, laid out at a fixed 1000 x 600 and scaled by the editor. */
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int baseW = 1000, baseH = 600;

    explicit MainPanel (BrecherProcessor&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct PadBinding
    {
        std::unique_ptr<juce::ParameterAttachment> attachment;
        float value = 0.0f;
        bool holding = false;
    };

    void bindPad (PadBinding&, const juce::String& paramId);
    void padDown (std::vector<PadBinding*>& group, int idx, int lenMode);
    void padUp (PadBinding& b);
    void clearLatched (std::vector<PadBinding*>& group);
    void drawHeader (juce::Graphics&);
    void drawProgressStrip (juce::Graphics&, juce::Rectangle<float>, juce::Colour, float progress, const juce::String& text);

    BrecherProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    bm::ui::LookAndFeel lnf;

    // isolate
    bm::ui::Knob isoKnob[3] { bm::ui::Knob (bm::ui::col::iso), bm::ui::Knob (bm::ui::col::iso), bm::ui::Knob (bm::ui::col::iso) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> isoAtt[3];
    std::unique_ptr<bm::ui::ParamToggle> kill[3];

    // build
    bm::ui::Knob buildKnob { bm::ui::col::build };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> buildAtt;
    std::unique_ptr<bm::ui::ParamToggle> buildFilter, buildNoise, buildVerb;
    std::unique_ptr<bm::ui::SegmentSelector> riseLen;
    bm::ui::Pad riseButton { "RISE", "", bm::ui::col::build, 16.0f };
    PadBinding riseBinding;

    // beat fx
    std::unique_ptr<bm::ui::SegmentSelector> fxType, fxLength;
    std::unique_ptr<bm::ui::Pad> pads[bm::numPads];
    PadBinding padBindings[bm::numPads];
    std::vector<PadBinding*> padGroup;
    bm::ui::Knob depthKnob { bm::ui::col::beat }, feedbackKnob { bm::ui::col::beat }, toneKnob { bm::ui::col::beat, true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAtt, feedbackAtt, toneAtt;
    std::unique_ptr<bm::ui::ParamToggle> triplet;

    // brake
    bm::ui::Platter platter;
    bm::ui::Pad brakeButton { "BRAKE", "VINYL STOP", bm::ui::col::brake, 15.0f };
    bm::ui::Pad spinButton  { "SPIN", "BACKSPIN", bm::ui::col::brake, 15.0f };
    PadBinding brakeBindings[2];
    std::vector<PadBinding*> brakeGroup;
    std::unique_ptr<bm::ui::SegmentSelector> brakeTime, brakeLen;

    // global
    std::unique_ptr<bm::ui::SegmentSelector> quant;
    bm::ui::Knob outKnob { bm::ui::col::text };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAtt;

    // display state
    double lastTime = 0.0;
    int lastFxMode = -1, lastBrakeMode = -1;
    float fxProgress = -1.0f, brakeProgress = -1.0f;
    int fxState = 0, brakeState = 0;
    double shownPpq = 0.0, shownBpm = 128.0;
    bool shownPlaying = false;
    juce::String fxStatus, brakeStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

//==============================================================================
class BrecherEditor : public juce::AudioProcessorEditor
{
public:
    explicit BrecherEditor (BrecherProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (bm::ui::col::chassis); }

private:
    MainPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrecherEditor)
};
