#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace bm;

//==============================================================================
BrecherProcessor::BrecherProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NOVA_BRECHER", createParameterLayout())
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    pIso[0] = raw (pid::isoLow);   pIso[1] = raw (pid::isoMid);   pIso[2] = raw (pid::isoHigh);
    pKill[0] = raw (pid::killLow); pKill[1] = raw (pid::killMid); pKill[2] = raw (pid::killHigh);
    pBuildAmt = raw (pid::buildAmt);
    pBuildFilter = raw (pid::buildFilter);
    pBuildNoise = raw (pid::buildNoise);
    pBuildVerb = raw (pid::buildVerb);
    pRiseLen = raw (pid::riseLen);
    pRise = raw (pid::rise);
    pFxType = raw (pid::fxType);
    pTriplet = raw (pid::triplet);
    pDepth = raw (pid::fxDepth);
    pFeedback = raw (pid::fxFeedback);
    pTone = raw (pid::fxTone);
    pFxLen = raw (pid::fxLength);
    for (int i = 0; i < numPads; ++i)
        pPad[i] = raw (pid::pad (i));
    pBrake = raw (pid::brake);
    pSpin = raw (pid::spin);
    pBrakeTime = raw (pid::brakeTime);
    pBrakeLen = raw (pid::brakeLen);
    pQuant = raw (pid::quant);
    pOut = raw (pid::outGain);
}

bool BrecherProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BrecherProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    isolator.prepare (sampleRate, maxChunk);
    beatFx.prepare (sampleRate);
    buildFx.prepare (sampleRate, maxChunk);
    brakeFx.prepare (sampleRate);

    monoScratch.assign ((size_t) std::max (samplesPerBlock, maxChunk) * 2, 0.0f);
    outGainSm.reset (sampleRate, 0.02);
    outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));

    clockValid = false;
    midiLatchMask = midiBrakeLatch = 0;
    midiKill = 0;
    needsResync = true;
}

//==============================================================================
void BrecherProcessor::updateClock (int numSamples)
{
    bool playing = false;
    std::optional<double> hostPpq, lastBar;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = std::clamp (*b, 40.0, 300.0);
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0)
                    grid.barLen = ts->numerator * 4.0 / ts->denominator;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition())                   hostPpq = *p;
            if (auto lb = pos->getPpqPositionOfLastBarStart())    lastBar = *lb;
        }
    }

    spb = sr * 60.0 / bpm;
    bps = 1.0 / spb;

    if (playing && hostPpq.has_value())
    {
        const double p = *hostPpq;
        if (clockValid && std::abs (p - expectedPpq) > std::max (0.01, 4.0 * bps))
        {
            const double delta = p - expectedPpq;   // loop or relocation: keep pending events relative
            beatFx.trigger.shift (delta);
            brakeFx.trigger.shift (delta);
            buildFx.shift (delta);
        }
        ppqBlock = p;

        if (lastBar.has_value())
        {
            const double o = std::fmod (*lastBar, grid.barLen);
            grid.barOrigin = o < 0.0 ? o + grid.barLen : o;
        }
    }
    else
    {
        ppqBlock = expectedPpq;   // transport stopped: free-running clock at host tempo
    }

    hostPlaying = playing;
    clockValid = true;
    grid.quant = (int) pQuant->load();
    grid.blockBeats = numSamples * bps;
    expectedPpq = ppqBlock + numSamples * bps;
}

//==============================================================================
void BrecherProcessor::handleParamEvents (double ppq)
{
    const int fxMode = (int) pFxLen->load();
    const int brMode = (int) pBrakeLen->load();

    // a mode switch must never fire a pad by itself
    const bool fxModeChanged = fxMode != beatFx.trigger.getMode();
    const bool brModeChanged = brMode != brakeFx.trigger.getMode();
    beatFx.trigger.setMode (fxMode, ppq);
    brakeFx.trigger.setMode (brMode, ppq);
    if (fxModeChanged || brModeChanged)
        midiLatchMask = midiBrakeLatch = 0;

    const bool resync = needsResync.exchange (false);

    // Pad parameters mirror "pressed".
    //   HOLD / LATCH: pressed = effect requested (LATCH pads are toggled by the editor)
    //   timed modes:  a rising edge fires one shot
    for (int i = 0; i < numPads; ++i)
    {
        const bool v = pPad[i]->load() > 0.5f;
        if (v == prevPad[i]) continue;
        prevPad[i] = v;
        if (resync || fxModeChanged) continue;

        if (v)                       beatFx.trigger.press (i, ppq);
        else if (! isTimed (fxMode)) beatFx.trigger.release (i, ppq);
    }

    auto brakeEdge = [&] (std::atomic<float>* p, bool& prev, int idx)
    {
        const bool v = p->load() > 0.5f;
        if (v == prev) return;
        prev = v;
        if (resync || brModeChanged) return;

        if (v)                       brakeFx.trigger.press (idx, ppq);
        else if (! isTimed (brMode)) brakeFx.trigger.release (idx, ppq);
    };
    brakeEdge (pBrake, prevBrake, 0);
    brakeEdge (pSpin,  prevSpin,  1);

    const bool r = pRise->load() > 0.5f;
    if (r != prevRise)
    {
        prevRise = r;
        if (r && ! resync)
            buildFx.toggleRise (ppq, grid);
    }
}

void BrecherProcessor::handleMidi (const juce::MidiMessage& m, double ppq)
{
    const int fxMode = beatFx.trigger.getMode();
    const int brMode = brakeFx.trigger.getMode();

    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        beatFx.trigger.releaseAll (ppq);
        brakeFx.trigger.releaseAll (ppq);
        midiLatchMask = midiBrakeLatch = 0;
        midiKill = 0;
        return;
    }

    if (! (m.isNoteOn() || m.isNoteOff()))
        return;

    const int n = m.getNoteNumber();
    const bool on = m.isNoteOn();

    // generic pad handling for both trigger groups
    auto padEvent = [&] (Trigger& trig, int mode, uint32_t& latch, int idx, int numInGroup)
    {
        if (isTimed (mode))
        {
            if (on) trig.press (idx, ppq);
        }
        else if (mode == lenLatch)
        {
            if (! on) return;
            const uint32_t bit = 1u << idx;
            if (latch & bit)
            {
                trig.release (idx, ppq);
                latch = 0;
            }
            else
            {
                for (int j = 0; j < numInGroup; ++j)
                    if (latch & (1u << j)) trig.release (j, ppq);
                latch = bit;
                trig.press (idx, ppq);
            }
        }
        else
        {
            if (on) trig.press (idx, ppq);
            else    trig.release (idx, ppq);
        }
    };

    if (n >= note::firstPad && n < note::firstPad + numPads)
        padEvent (beatFx.trigger, fxMode, midiLatchMask, n - note::firstPad, numPads);
    else if (n == note::brake)
        padEvent (brakeFx.trigger, brMode, midiBrakeLatch, 0, 2);
    else if (n == note::spin)
        padEvent (brakeFx.trigger, brMode, midiBrakeLatch, 1, 2);
    else if (n == note::rise && on)
        buildFx.toggleRise (ppq, grid);
    else if (n >= note::killLow && n <= note::killHigh)
    {
        const int bit = 1 << (n - note::killLow);
        midiKill = on ? (midiKill | bit) : (midiKill & ~bit);
    }
}

//==============================================================================
void BrecherProcessor::render (float* L, float* R, int start, int len)
{
    while (len > 0)
    {
        const int n = std::min (len, maxChunk);
        SegmentClock c { ppqBlock + start * bps, bps, spb };

        isolator.process (L + start, R + start, n);
        beatFx.process   (L + start, R + start, n, c);
        buildFx.process  (L + start, R + start, n, c);
        brakeFx.process  (L + start, R + start, n, c);

        start += n;
        len -= n;
    }
}

void BrecherProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = nullptr;
    if (numCh > 1)
    {
        R = buffer.getWritePointer (1);
    }
    else
    {
        if ((int) monoScratch.size() < n)
            monoScratch.resize ((size_t) n);   // only if the host breaks its block size promise
        std::copy (L, L + n, monoScratch.begin());
        R = monoScratch.data();
    }

    updateClock (n);

    // ------------------------------------------------ parameters
    const int killMidi = midiKill;
    isolator.setGains (pIso[0]->load(), pIso[1]->load(), pIso[2]->load(),
                       pKill[0]->load() > 0.5f || (killMidi & 1),
                       pKill[1]->load() > 0.5f || (killMidi & 2),
                       pKill[2]->load() > 0.5f || (killMidi & 4));

    beatFx.setParams ((int) pFxType->load(), pTriplet->load() > 0.5f,
                      pDepth->load(), pFeedback->load(), pTone->load());

    buildFx.setParams (pBuildAmt->load(), pBuildFilter->load() > 0.5f, pBuildNoise->load() > 0.5f,
                       pBuildVerb->load() > 0.5f, (int) pRiseLen->load());

    brakeFx.setTimeBeats (brakeTimeBeats[std::clamp ((int) pBrakeTime->load(), 0, numBrakeTimes - 1)]);

    handleParamEvents (ppqBlock);

    // ------------------------------------------------ audio, split at MIDI events
    int pos = 0;
    for (const auto meta : midi)
    {
        const int s = std::clamp (meta.samplePosition, 0, n);
        if (s > pos)
        {
            render (L, R, pos, s - pos);
            pos = s;
        }
        handleMidi (meta.getMessage(), ppqBlock + pos * bps);

        if (midiKill != killMidi)   // kill notes act immediately
            isolator.setGains (pIso[0]->load(), pIso[1]->load(), pIso[2]->load(),
                               pKill[0]->load() > 0.5f || (midiKill & 1),
                               pKill[1]->load() > 0.5f || (midiKill & 2),
                               pKill[2]->load() > 0.5f || (midiKill & 4));
    }
    if (pos < n)
        render (L, R, pos, n - pos);

    // ------------------------------------------------ output gain
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGainSm.getNextValue();
        L[i] *= g;
        R[i] *= g;
        peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));
    }

    // ------------------------------------------------ UI
    const double ppqEnd = ppqBlock + n * bps;
    ui.ppq = ppqEnd;
    ui.bpm = bpm;
    ui.barLen = grid.barLen;
    ui.barOrigin = grid.barOrigin;
    ui.playing = hostPlaying;

    ui.fxState = (int) beatFx.trigger.getState();
    ui.fxIdx = beatFx.trigger.index();
    ui.fxType = beatFx.getActiveType();
    ui.fxProgress = beatFx.trigger.progress (ppqEnd);

    ui.brakeState = (int) brakeFx.trigger.getState();
    ui.brakeIdx = brakeFx.trigger.index();
    ui.brakeRate = brakeFx.getRate();
    ui.brakeProgress = brakeFx.trigger.progress (ppqEnd);

    ui.riseState = (int) buildFx.getState();
    ui.riseProgress = buildFx.riseProgress (ppqEnd);
    ui.buildAmount = buildFx.getAmount();
    ui.midiKillMask = midiKill;
    ui.outLevel = std::max (peak, ui.outLevel.load() * 0.9f);
}

//==============================================================================
void BrecherProcessor::resetTriggerParams()
{
    // pads and trigger buttons never come back "pressed" from a saved session
    auto clear = [this] (const juce::String& id) {
        if (auto* p = apvts.getParameter (id))
            if (p->getValue() > 0.5f)
                p->setValueNotifyingHost (0.0f);
    };
    for (int i = 0; i < numPads; ++i)
        clear (pid::pad (i));
    clear (pid::brake);
    clear (pid::spin);
    clear (pid::rise);
    needsResync = true;
}

void BrecherProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BrecherProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    resetTriggerParams();
}

juce::AudioProcessorEditor* BrecherProcessor::createEditor()
{
    return new BrecherEditor (*this);
}

//==============================================================================
#if ! BM_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrecherProcessor();
}
#endif
