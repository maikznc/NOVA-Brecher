#include "PluginEditor.h"

using namespace bm;
using namespace bm::ui;
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

namespace
{
    // section frames (base coordinates)
    const juce::Rectangle<float> isoArea   { 16.0f, 84.0f, 236.0f, 236.0f };
    const juce::Rectangle<float> buildArea { 16.0f, 332.0f, 236.0f, 252.0f };
    const juce::Rectangle<float> beatArea  { 264.0f, 84.0f, 472.0f, 500.0f };
    const juce::Rectangle<float> brakeArea { 748.0f, 84.0f, 236.0f, 500.0f };

    constexpr int isoX = 16, isoY = 84, buildX = 16, buildY = 332, beatX = 264, beatY = 84, brakeX = 748, brakeY = 84;

    int isoCentre (int i) { return isoX + 39 + i * 79; }

    const char* const padSubs[numPads] = { "BUZZ", "32ND", "16TH", "8TH", "DOTTED 8TH", "1 BEAT", "2 BEATS", "1 BAR" };

    juce::StringArray arr (const char* const* names, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i) a.add (names[i]);
        return a;
    }
}

//==============================================================================
MainPanel::MainPanel (BrecherProcessor& p)
    : proc (p), apvts (p.apvts)
{
    setLookAndFeel (&lnf);
    setOpaque (true);

    auto param = [this] (const juce::String& id) -> juce::RangedAudioParameter& { return *apvts.getParameter (id); };

    // ------------------------------------------------ isolate
    const char* isoIds[3]  = { pid::isoLow, pid::isoMid, pid::isoHigh };
    const char* killIds[3] = { pid::killLow, pid::killMid, pid::killHigh };
    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (isoKnob[i]);
        isoAtt[i] = std::make_unique<SliderAtt> (apvts, isoIds[i], isoKnob[i]);
        isoKnob[i].setDoubleClickReturnValue (true, 0.0);
        kill[i] = std::make_unique<ParamToggle> (param (killIds[i]), "KILL", col::iso);
        addAndMakeVisible (*kill[i]);
    }

    // ------------------------------------------------ build
    addAndMakeVisible (buildKnob);
    buildAtt = std::make_unique<SliderAtt> (apvts, pid::buildAmt, buildKnob);
    buildKnob.setGhost (-1.0f);   // reserves room for the outer "rise" ring
    buildKnob.setDoubleClickReturnValue (true, 0.0);

    buildFilter = std::make_unique<ParamToggle> (param (pid::buildFilter), "FILTER", col::build);
    buildNoise  = std::make_unique<ParamToggle> (param (pid::buildNoise),  "NOISE",  col::build);
    buildVerb   = std::make_unique<ParamToggle> (param (pid::buildVerb),   "VERB",   col::build);
    for (auto* t : { buildFilter.get(), buildNoise.get(), buildVerb.get() })
        addAndMakeVisible (*t);

    riseLen = std::make_unique<SegmentSelector> (param (pid::riseLen), juce::StringArray { "1", "2", "4", "8", "16" }, col::build);
    addAndMakeVisible (*riseLen);

    bindPad (riseBinding, pid::rise);
    addAndMakeVisible (riseButton);
    riseButton.onDown = [this] {
        riseBinding.attachment->beginGesture();
        riseBinding.attachment->setValueAsPartOfGesture (1.0f);
        riseBinding.holding = true;
    };
    riseButton.onUp = [this] { padUp (riseBinding); };

    // ------------------------------------------------ beat fx
    fxType = std::make_unique<SegmentSelector> (param (pid::fxType), arr (fxTypeNames, 4), col::beat);
    fxLength = std::make_unique<SegmentSelector> (param (pid::fxLength), arr (lenNames, numLenModes), col::beat);
    addAndMakeVisible (*fxType);
    addAndMakeVisible (*fxLength);

    for (int i = 0; i < numPads; ++i)
    {
        pads[i] = std::make_unique<Pad> (padNames[i], padSubs[i], col::beat);
        addAndMakeVisible (*pads[i]);
        bindPad (padBindings[i], pid::pad (i));
        padGroup.push_back (&padBindings[i]);
        pads[i]->onDown = [this, i] { padDown (padGroup, i, (int) apvts.getRawParameterValue (pid::fxLength)->load()); };
        pads[i]->onUp   = [this, i] { padUp (padBindings[i]); };
    }

    for (auto* k : { &depthKnob, &feedbackKnob, &toneKnob })
        addAndMakeVisible (*k);
    depthAtt    = std::make_unique<SliderAtt> (apvts, pid::fxDepth, depthKnob);
    feedbackAtt = std::make_unique<SliderAtt> (apvts, pid::fxFeedback, feedbackKnob);
    toneAtt     = std::make_unique<SliderAtt> (apvts, pid::fxTone, toneKnob);
    depthKnob.setDoubleClickReturnValue (true, 1.0);
    feedbackKnob.setDoubleClickReturnValue (true, 0.55);
    toneKnob.setDoubleClickReturnValue (true, 0.25);

    triplet = std::make_unique<ParamToggle> (param (pid::triplet), "TRIPLET", col::beat);
    addAndMakeVisible (*triplet);

    // ------------------------------------------------ brake
    addAndMakeVisible (platter);
    platter.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (brakeButton);
    addAndMakeVisible (spinButton);
    bindPad (brakeBindings[0], pid::brake);
    bindPad (brakeBindings[1], pid::spin);
    brakeGroup = { &brakeBindings[0], &brakeBindings[1] };
    brakeButton.onDown = [this] { padDown (brakeGroup, 0, (int) apvts.getRawParameterValue (pid::brakeLen)->load()); };
    brakeButton.onUp   = [this] { padUp (brakeBindings[0]); };
    spinButton.onDown  = [this] { padDown (brakeGroup, 1, (int) apvts.getRawParameterValue (pid::brakeLen)->load()); };
    spinButton.onUp    = [this] { padUp (brakeBindings[1]); };

    brakeTime = std::make_unique<SegmentSelector> (param (pid::brakeTime), arr (brakeTimeNames, numBrakeTimes), col::brake);
    brakeLen  = std::make_unique<SegmentSelector> (param (pid::brakeLen), arr (lenNames, numLenModes), col::brake, 2);
    addAndMakeVisible (*brakeTime);
    addAndMakeVisible (*brakeLen);

    // ------------------------------------------------ global
    quant = std::make_unique<SegmentSelector> (param (pid::quant), arr (quantNames, 4), col::beat);
    addAndMakeVisible (*quant);
    addAndMakeVisible (outKnob);
    outAtt = std::make_unique<SliderAtt> (apvts, pid::outGain, outKnob);
    outKnob.setDoubleClickReturnValue (true, 0.0);

    setSize (baseW, baseH);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

MainPanel::~MainPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainPanel::bindPad (PadBinding& b, const juce::String& paramId)
{
    auto* p = apvts.getParameter (paramId);
    b.attachment = std::make_unique<juce::ParameterAttachment> (*p, [&b] (float v) { b.value = v; });
    b.attachment->sendInitialUpdate();
}

void MainPanel::padDown (std::vector<PadBinding*>& group, int idx, int lenMode)
{
    auto& b = *group[(size_t) idx];

    if (lenMode == lenLatch)
    {
        // one latched pad per group: switching pads moves the latch
        for (size_t j = 0; j < group.size(); ++j)
            if ((int) j != idx && group[j]->value > 0.5f)
                group[j]->attachment->setValueAsCompleteGesture (0.0f);
        b.attachment->setValueAsCompleteGesture (b.value > 0.5f ? 0.0f : 1.0f);
        return;
    }

    // HOLD and timed modes: the parameter follows the finger
    b.attachment->beginGesture();
    b.attachment->setValueAsPartOfGesture (1.0f);
    b.holding = true;
}

void MainPanel::padUp (PadBinding& b)
{
    if (! b.holding)
        return;
    b.attachment->setValueAsPartOfGesture (0.0f);
    b.attachment->endGesture();
    b.holding = false;
}

void MainPanel::clearLatched (std::vector<PadBinding*>& group)
{
    for (auto* b : group)
        if (! b->holding && b->value > 0.5f)
            b->attachment->setValueAsCompleteGesture (0.0f);
}

//==============================================================================
void MainPanel::resized()
{
    for (int i = 0; i < 3; ++i)
    {
        const int cx = isoCentre (i);
        isoKnob[i].setBounds (cx - 35, isoY + 52, 70, 70);
        kill[i]->setBounds (cx - 31, isoY + 152, 62, 28);
    }

    buildKnob.setBounds (buildX + 16, buildY + 50, 98, 98);
    buildFilter->setBounds (buildX + 130, buildY + 54, 90, 27);
    buildNoise->setBounds  (buildX + 130, buildY + 88, 90, 27);
    buildVerb->setBounds   (buildX + 130, buildY + 122, 90, 27);
    riseLen->setBounds (buildX + 58, buildY + 166, 162, 23);
    riseButton.setBounds (buildX + 9, buildY + 190, 218, 46);

    fxType->setBounds (beatX + 16, beatY + 52, 440, 30);
    for (int i = 0; i < numPads; ++i)
        pads[i]->setBounds (beatX + 10 + (i % 4) * 113, beatY + 88 + (i / 4) * 94, 113, 94);

    depthKnob.setBounds    (beatX + 24,  beatY + 284, 66, 66);
    feedbackKnob.setBounds (beatX + 120, beatY + 284, 66, 66);
    toneKnob.setBounds     (beatX + 216, beatY + 284, 66, 66);
    triplet->setBounds (beatX + 318, beatY + 290, 138, 30);
    fxLength->setBounds (beatX + 16, beatY + 398, 440, 30);

    platter.setBounds (brakeX + 28, brakeY + 48, 180, 180);
    brakeButton.setBounds (brakeX + 9,   brakeY + 232, 110, 66);
    spinButton.setBounds  (brakeX + 117, brakeY + 232, 110, 66);
    brakeTime->setBounds (brakeX + 16, brakeY + 320, 204, 26);
    brakeLen->setBounds  (brakeX + 16, brakeY + 372, 204, 54);

    quant->setBounds (770, 30, 156, 26);
    outKnob.setBounds (940, 10, 46, 46);
}

//==============================================================================
void MainPanel::paint (juce::Graphics& g)
{
    // chassis
    g.setGradientFill (juce::ColourGradient (col::chassisHi, 0, 0, col::chassis, 0, (float) baseH, false));
    g.fillAll();

    juce::Random rnd (42);
    for (int y = 0; y < baseH; y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.006f + rnd.nextFloat() * 0.008f));
        g.drawHorizontalLine (y, 0.0f, (float) baseW);
    }

    drawHeader (g);

    drawSection (g, isoArea,   col::iso,   "ISOLATE");
    drawSection (g, buildArea, col::build, "BUILD");
    drawSection (g, beatArea,  col::beat,  "BEAT FX");
    drawSection (g, brakeArea, col::brake, "BRAKE");

    // ------------------------------------------------ isolate captions
    const char* bands[3] = { "LOW", "MID", "HIGH" };
    for (int i = 0; i < 3; ++i)
        drawCaption (g, bands[i], { isoCentre (i) - 40, isoY + 126, 80, 14 });
    drawCaption (g, "250 HZ  |  2.5 KHZ", { isoX, isoY + 200, 236, 14 }, juce::Justification::centred, col::textFaint);

    // ------------------------------------------------ build captions
    drawCaption (g, "AMOUNT", { buildX + 16, buildY + 148, 98, 14 });
    drawCaption (g, "BARS", { buildX + 16, buildY + 166, 42, 23 }, juce::Justification::centredLeft);

    // ------------------------------------------------ beat captions
    drawCaption (g, "DEPTH",    { beatX + 17,  beatY + 352, 80, 14 });
    drawCaption (g, "FEEDBACK", { beatX + 113, beatY + 352, 80, 14 });
    drawCaption (g, "TONE",     { beatX + 209, beatY + 352, 80, 14 });
    drawCaption (g, "LENGTH", { beatX + 18, beatY + 381, 120, 14 }, juce::Justification::centredLeft);
    drawCaption (g, "LP", { beatX + 216, beatY + 336, 20, 12 }, juce::Justification::centredLeft, col::textFaint);
    drawCaption (g, "HP", { beatX + 262, beatY + 336, 20, 12 }, juce::Justification::centredRight, col::textFaint);

    // delay time readout
    {
        auto lcd = juce::Rectangle<float> ((float) beatX + 318, (float) beatY + 328, 138, 34);
        g.setColour (col::inset);
        g.fillRoundedRectangle (lcd, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (lcd.reduced (0.5f), 6.0f, 1.0f);

        const int idx = juce::jlimit (0, numPads - 1, proc.ui.fxIdx.load());
        const bool trip = apvts.getRawParameterValue (pid::triplet)->load() > 0.5f;
        const double ms = padBeats[idx] * (trip ? 2.0 / 3.0 : 1.0) * 60000.0 / std::max (1.0, shownBpm);
        g.setColour (col::beat);
        g.setFont (monoFont (17.0f));
        g.drawText (juce::String (ms, ms < 100.0 ? 1 : 0), lcd.reduced (12, 0).toNearestInt(), juce::Justification::centredLeft);
        drawCaption (g, "MS", lcd.reduced (12, 0).toNearestInt(), juce::Justification::centredRight);
    }

    // ------------------------------------------------ brake captions
    drawCaption (g, "TIME",   { brakeX + 18, brakeY + 304, 120, 14 }, juce::Justification::centredLeft);
    drawCaption (g, "LENGTH", { brakeX + 18, brakeY + 356, 120, 14 }, juce::Justification::centredLeft);

    // ------------------------------------------------ status strips
    drawProgressStrip (g, { (float) beatX + 16, (float) beatY + 442, 440, 32 }, col::beat, fxProgress, fxStatus);
    drawProgressStrip (g, { (float) brakeX + 16, (float) brakeY + 442, 204, 32 }, col::brake, brakeProgress, brakeStatus);
}

void MainPanel::paintOverChildren (juce::Graphics&) {}

void MainPanel::drawHeader (juce::Graphics& g)
{
    // logo
    g.setColour (col::beat);
    g.setFont (font (11.0f, true, 0.62f));
    g.drawText ("NOVA", 26, 12, 120, 14, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.setFont (font (30.0f, true, 0.06f));
    g.drawText ("BRECHER", 23, 24, 260, 32, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (9.0f, true, 0.34f));
    g.drawText ("POWER BREAK EFFECTOR", 26, 54, 260, 12, juce::Justification::centredLeft);

    // beat display
    auto box = juce::Rectangle<float> (352, 12, 278, 48);
    g.setColour (col::inset);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    const double barLen = std::max (1.0, proc.ui.barLen.load());
    const double origin = proc.ui.barOrigin.load();
    const double rel = shownPpq - origin;
    const int beatInBar = (int) std::floor (std::fmod (std::fmod (rel, barLen) + barLen, barLen));
    const int barInPhrase = (int) (((long long) std::floor (rel / barLen) % 4 + 4) % 4);
    const float frac = (float) (rel - std::floor (rel));
    const float brightness = shownPlaying ? 1.0f : 0.55f;

    drawCaption (g, "BEAT", { 362, 18, 40, 14 }, juce::Justification::centredLeft);
    drawCaption (g, "BAR", { 362, 38, 40, 14 }, juce::Justification::centredLeft);

    for (int i = 0; i < 4; ++i)
    {
        auto led = juce::Rectangle<float> (404.0f + i * 56.0f, 20.0f, 50.0f, 11.0f);
        const bool on = (beatInBar % 4) == i;
        if (on)
        {
            const float lvl = brightness * (1.0f - 0.55f * frac);
            const auto c = i == 0 ? col::beat.brighter (0.5f) : col::beat;
            drawGlow (g, led, 3.0f, c, lvl * 0.8f, 6.0f);
            g.setColour (c.withAlpha (0.35f + 0.65f * lvl));
        }
        else
        {
            g.setColour (juce::Colour (0xff23252a));
        }
        g.fillRoundedRectangle (led, 3.0f);

        auto barLed = juce::Rectangle<float> (404.0f + i * 56.0f, 41.0f, 50.0f, 6.0f);
        g.setColour (barInPhrase == i ? col::text.withAlpha (0.25f + 0.6f * brightness) : juce::Colour (0xff1e2024));
        g.fillRoundedRectangle (barLed, 2.0f);
    }

    // tempo
    auto lcd = juce::Rectangle<float> (644, 12, 112, 48);
    g.setColour (col::inset);
    g.fillRoundedRectangle (lcd, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (lcd.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::beat.withAlpha (shownPlaying ? 1.0f : 0.6f));
    g.setFont (monoFont (23.0f));
    g.drawText (juce::String (shownBpm, 1), lcd.withTrimmedBottom (12).toNearestInt(), juce::Justification::centred);
    drawCaption (g, shownPlaying ? "BPM  SYNC" : "BPM  FREE", lcd.removeFromBottom (16).toNearestInt(), juce::Justification::centred);

    drawCaption (g, "QUANTIZE", { 772, 12, 150, 14 }, juce::Justification::centredLeft);
    drawCaption (g, "OUT", { 940, 56, 46, 12 });
}

void MainPanel::drawProgressStrip (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent,
                                   float progress, const juce::String& text)
{
    g.setColour (col::inset);
    g.fillRoundedRectangle (r, 7.0f);

    if (progress >= 0.0f)
    {
        auto fill = r.reduced (3.0f);
        fill.setWidth (fill.getWidth() * progress);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.15f), fill.getX(), 0, accent.withAlpha (0.55f), fill.getRight(), 0, false));
        g.fillRoundedRectangle (fill, 5.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

    g.setColour (text.startsWith ("READY") ? col::textDim : accent.brighter (0.3f));
    g.setFont (font (11.0f, true, 0.14f));
    g.drawText (text, r.reduced (12.0f, 0).toNearestInt(), juce::Justification::centredLeft);
}

//==============================================================================
void MainPanel::timerCallback()
{
    auto& u = proc.ui;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.2, (now - lastTime) * 0.001);
    lastTime = now;
    const float blink = 0.5f + 0.5f * std::sin ((float) now * 0.0125f);

    const int fxMode = (int) apvts.getRawParameterValue (pid::fxLength)->load();
    const int brMode = (int) apvts.getRawParameterValue (pid::brakeLen)->load();
    if (fxMode != lastFxMode) { if (lastFxMode == lenLatch) clearLatched (padGroup); lastFxMode = fxMode; }
    if (brMode != lastBrakeMode) { if (lastBrakeMode == lenLatch) clearLatched (brakeGroup); lastBrakeMode = brMode; }

    // ------------------------------------------------ beat pads
    fxState = u.fxState.load();
    const int idx = u.fxIdx.load();
    const bool trip = apvts.getRawParameterValue (pid::triplet)->load() > 0.5f;
    const float prog = u.fxProgress.load();

    for (int i = 0; i < numPads; ++i)
    {
        float lit = 0.0f;
        if (i == idx && fxState == 2)      lit = 1.0f;
        else if (i == idx && fxState == 1) lit = 0.2f + 0.5f * blink;
        else if (fxMode == lenLatch && padBindings[i].value > 0.5f) lit = 0.3f;
        pads[i]->setState (lit, i == idx, (i == idx && fxState == 2) ? prog : -1.0f, trip);
    }

    // ------------------------------------------------ brake buttons
    brakeState = u.brakeState.load();
    const int bIdx = u.brakeIdx.load();
    auto brakeLit = [&] (int i) {
        if (i == bIdx && brakeState == 2) return 1.0f;
        if (i == bIdx && brakeState == 1) return 0.2f + 0.5f * blink;
        return 0.0f;
    };
    brakeButton.setState (brakeLit (0), bIdx == 0, -1.0f);
    spinButton.setState  (brakeLit (1), bIdx == 1, -1.0f);
    platter.advance (dt, u.brakeRate.load(), brakeState == 2, (float) u.bpm.load());

    // ------------------------------------------------ rise
    const int riseState = u.riseState.load();
    const float riseProg = u.riseProgress.load();
    if (riseState == 2)      { riseButton.setLabel ("DROP"); riseButton.setState (0.25f + 0.75f * riseProg, true, riseProg); }
    else if (riseState == 1) { riseButton.setLabel ("ARMED"); riseButton.setState (0.2f + 0.5f * blink, true, -1.0f); }
    else                     { riseButton.setLabel ("RISE"); riseButton.setState (0.0f, false, -1.0f); }

    const float manual = apvts.getRawParameterValue (pid::buildAmt)->load();
    const float eff = u.buildAmount.load();
    buildKnob.setGhost (eff > manual + 0.01f ? eff : -1.0f);

    // ------------------------------------------------ kills held via MIDI
    const int mk = u.midiKillMask.load();
    for (int i = 0; i < 3; ++i)
        kill[i]->setExtraLit ((mk >> i) & 1);

    // ------------------------------------------------ header + status
    shownPpq = u.ppq.load();
    shownBpm = u.bpm.load();
    shownPlaying = u.playing.load();
    repaint (340, 0, 430, 72);

    const int type = juce::jlimit (0, 3, fxState == 0 ? (int) apvts.getRawParameterValue (pid::fxType)->load() : u.fxType.load());
    const juce::String valueName = juce::String (padNames[juce::jlimit (0, numPads - 1, idx)]) + (trip ? "T" : "");
    const int q = (int) apvts.getRawParameterValue (pid::quant)->load();
    const juce::String nextLine = q == qBar ? "NEXT BAR" : q == qBeat ? "NEXT BEAT" : "NEXT 1/4";

    juce::String s;
    if (fxState == 2)      s = juce::String (fxTypeNames[type]) + "  " + valueName + "  |  " + lenNames[fxMode];
    else if (fxState == 1) s = "ARMED  >  " + nextLine;
    else                   s = "READY  |  " + juce::String (fxTypeNames[type]) + "  " + valueName;
    const float newFxProg = fxState == 2 ? (prog >= 0.0f ? prog : 1.0f) : -1.0f;
    if (s != fxStatus || std::abs (newFxProg - fxProgress) > 0.002f)
    {
        fxStatus = s;
        fxProgress = newFxProg;
        repaint (beatX, beatY + 320, 472, 160);
    }

    const float bProg = u.brakeProgress.load();
    juce::String b;
    if (brakeState == 2)      b = juce::String (bIdx == 1 ? "SPIN" : "BRAKE") + "  |  " + lenNames[brMode];
    else if (brakeState == 1) b = "ARMED  >  " + nextLine;
    else                      b = "READY";
    const float newBrProg = brakeState == 2 ? (bProg >= 0.0f ? bProg : 1.0f) : -1.0f;
    if (b != brakeStatus || std::abs (newBrProg - brakeProgress) > 0.002f)
    {
        brakeStatus = b;
        brakeProgress = newBrProg;
        repaint (brakeX, brakeY + 436, 236, 44);
    }
}

//==============================================================================
BrecherEditor::BrecherEditor (BrecherProcessor& p)
    : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits (700, 420, 1600, 960);
    getConstrainer()->setFixedAspectRatio ((double) MainPanel::baseW / (double) MainPanel::baseH);
    setSize (MainPanel::baseW, MainPanel::baseH);
}

void BrecherEditor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::baseW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, MainPanel::baseW, MainPanel::baseH);
}
