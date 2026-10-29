// Offline test tool for NOVA BRECHER (not part of the plugin build).
//   NovaBrecherTests demo <out.wav>       renders a hard techno loop with power breaks
//   NovaBrecherTests clicks               runs each effect over a sine and reports discontinuities
//   NovaBrecherTests snapshot <out.png>   renders the editor to an image

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace bm;

namespace
{
constexpr double sr = 48000.0;
constexpr double bpm = 150.0;
constexpr int block = 512;
const double spb = sr * 60.0 / bpm;

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setTimeInSamples ((juce::int64) (ppq * spb));
        return p;
    }
};

void setParam (BrecherProcessor& p, const juce::String& id, float denorm)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (denorm));
}

//------------------------------------------------------------------------------ synthetic groove
struct Groove
{
    juce::Random rnd { 7 };
    float bpState[4] {};

    float kick (double t)
    {
        if (t < 0.0 || t > 0.35) return 0.0f;
        const double f0 = 48.0, f1 = 190.0;
        const double phase = 2.0 * juce::MathConstants<double>::pi * (f0 * t + (f1 - f0) * 0.018 * (1.0 - std::exp (-t / 0.018)));
        const double env = std::exp (-t / 0.16) * (t < 0.001 ? t / 0.001 : 1.0);
        const double click = std::exp (-t / 0.002) * 0.6;
        return (float) std::tanh (3.2 * (std::sin (phase) * env + click * (rnd.nextFloat() - 0.5)));
    }

    float noiseHit (double t, double decay)
    {
        if (t < 0.0 || t > decay * 6.0) return 0.0f;
        return (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t / decay);
    }

    float schranz (double t)
    {
        if (t < 0.0 || t > 0.12) return 0.0f;
        const double env = std::exp (-t / 0.035);
        const double s = std::sin (2 * juce::MathConstants<double>::pi * 410 * t + 2.4 * std::sin (2 * juce::MathConstants<double>::pi * 1290 * t));
        return (float) (std::tanh (5.0 * s) * env);
    }

    void render (juce::AudioBuffer<float>& out, double startPpq)
    {
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double ppq = startPpq + i / spb;
            const double beatT = (ppq - std::floor (ppq)) * 60.0 / bpm;
            const double six = ppq * 4.0;
            const int step = (int) std::floor (six) % 16;
            const double sixT = (six - std::floor (six)) * 15.0 / bpm;
            const double offT = (std::fmod (ppq + 0.5, 1.0)) * 60.0 / bpm;

            float k = kick (beatT) * 0.9f;

            // rumble: low sine on the off 16ths, ducked by the kick
            const double duck = std::min (1.0, beatT / 0.12);
            float rum = (float) (std::sin (2 * juce::MathConstants<double>::pi * 46.0 * ppq * 60.0 / bpm) * 0.28 * duck * duck);

            float hat = noiseHit (sixT, 0.012) * 0.16f + noiseHit (offT, 0.05) * 0.2f;
            // crude high-pass for hats
            bpState[0] = 0.75f * bpState[0] + 0.25f * hat;
            hat -= bpState[0];

            const int barBeat = (int) std::floor (ppq) % 4;
            const double clapT = (barBeat == 1 || barBeat == 3) ? beatT : -1.0;
            float clap = noiseHit (clapT, 0.03) * 0.35f;
            bpState[1] = 0.6f * bpState[1] + 0.4f * clap;
            clap = clap - bpState[1] * 0.5f;

            static const bool pattern[16] = { 0,0,1,0, 0,1,0,1, 0,0,1,0, 1,0,0,1 };
            float sch = pattern[step] ? schranz (sixT) * 0.33f : 0.0f;

            const float l = std::tanh (1.4f * (k + rum + hat * 0.9f + clap + sch * 0.8f)) * 0.8f;
            const float r = std::tanh (1.4f * (k + rum + hat * 1.1f + clap + sch * 1.1f)) * 0.8f;
            out.setSample (0, i, l);
            out.setSample (1, i, r);
        }
    }
};

//------------------------------------------------------------------------------ timeline
struct Event
{
    double ppq;
    std::function<void (BrecherProcessor&)> fn;
};

void tap (std::vector<Event>& ev, double ppq, const juce::String& id)
{
    ev.push_back ({ ppq, [id] (BrecherProcessor& p) { setParam (p, id, 1.0f); } });
    ev.push_back ({ ppq + 0.1, [id] (BrecherProcessor& p) { setParam (p, id, 0.0f); } });
}

void hold (std::vector<Event>& ev, double from, double to, const juce::String& id)
{
    ev.push_back ({ from, [id] (BrecherProcessor& p) { setParam (p, id, 1.0f); } });
    ev.push_back ({ to,   [id] (BrecherProcessor& p) { setParam (p, id, 0.0f); } });
}

void set (std::vector<Event>& ev, double ppq, const juce::String& id, float v)
{
    ev.push_back ({ ppq, [id, v] (BrecherProcessor& p) { setParam (p, id, v); } });
}

/** Runs the processor over `bars` bars of input from `source`, firing events at block starts. */
struct MidiEvent { double ppq; juce::MidiMessage msg; };

juce::AudioBuffer<float> run (BrecherProcessor& proc, std::vector<Event> events, int bars,
                              std::function<void (juce::AudioBuffer<float>&, double)> source,
                              std::vector<MidiEvent> midiEvents = {})
{
    std::sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.ppq < b.ppq; });

    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);

    const int total = (int) std::ceil (bars * 4.0 * spb);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    juce::MidiBuffer midi;
    size_t next = 0;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        ph.ppq = pos / spb;
        const double endPpq = (pos + n) / spb;

        while (next < events.size() && events[next].ppq < endPpq)
            events[next++].fn (proc);

        midi.clear();
        for (auto& me : midiEvents)
            if (me.ppq >= ph.ppq && me.ppq < endPpq)
                midi.addEvent (me.msg, (int) ((me.ppq - ph.ppq) * spb));

        juce::AudioBuffer<float> chunk (buf.getArrayOfWritePointers(), 2, n);
        source (chunk, ph.ppq);
        proc.processBlock (chunk, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, chunk, c, 0, n);
    }
    return out;
}

bool writeWav (const juce::AudioBuffer<float>& b, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    if (os == nullptr) return false;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

//------------------------------------------------------------------------------ demo
int demo (const juce::File& outFile)
{
    BrecherProcessor proc;
    Groove groove;
    std::vector<Event> ev;

    // 1) bar 4: classic power break - STOP 3/4 on beat 3, back on the one
    set (ev, 0.0, pid::fxLength, lenToBar);
    set (ev, 0.0, pid::fxType, fxStop);
    tap (ev, 13.98, pid::pad (4));

    // 2) bars 5-8: 4-bar RISE, last bar ROLL 1/4 -> 1/8 -> 1/16 -> 1/32
    set (ev, 15.5, pid::riseLen, 2);
    tap (ev, 15.98, pid::rise);
    set (ev, 27.0, pid::fxType, fxRoll);
    tap (ev, 27.98, pid::pad (2));
    tap (ev, 29.98, pid::pad (1));
    tap (ev, 30.98, pid::pad (0));

    // 3) bar 10: kill the low end, bar 11: GATE 1/8 for one bar, beat 3 of bar 12: BRAKE to the bar
    hold (ev, 35.98, 39.98, pid::killLow);
    set (ev, 39.0, pid::fxType, fxGate);
    set (ev, 39.0, pid::fxLength, lenBar1);
    tap (ev, 39.98, pid::pad (3));
    set (ev, 45.0, pid::brakeTime, 2);      // 1/1
    tap (ev, 45.98, pid::brake);

    // 4) bar 14: ECHO 3/4 held for two beats, bar 16 beat 3: SPIN to the bar
    set (ev, 51.0, pid::fxType, fxEcho);
    set (ev, 51.0, pid::fxLength, lenHold);
    hold (ev, 51.98, 53.98, pid::pad (4));
    set (ev, 61.0, pid::brakeTime, 1);      // 1/2
    tap (ev, 61.98, pid::spin);

    // 5) bar 18: STOP 1/2 with long feedback for 2 beats, then out
    set (ev, 67.0, pid::fxType, fxStop);
    set (ev, 67.0, pid::fxLength, lenBeat2);
    set (ev, 67.0, pid::fxFeedback, 0.8f);
    tap (ev, 69.98, pid::pad (3));

    auto out = run (proc, ev, 19, [&] (auto& b, double ppq) { groove.render (b, ppq); });

    int bad = 0;
    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const float v = out.getSample (c, i);
            if (! std::isfinite (v)) ++bad;
            peak = std::max (peak, std::abs (v));
        }
    std::printf ("demo: %d samples, peak %.3f (%.1f dBFS), non-finite %d\n",
                 out.getNumSamples(), peak, juce::Decibels::gainToDecibels (peak), bad);

    if (peak > 0.98f)
        out.applyGain (0.98f / peak);
    return writeWav (out, outFile) && bad == 0 ? 0 : 1;
}

//------------------------------------------------------------------------------ click test
struct ClickCase
{
    const char* name;
    std::function<void (std::vector<Event>&)> setup;
};

int clicks()
{
    auto sine = [] (juce::AudioBuffer<float>& b, double ppq)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (ppq * spb + i) / sr;
            const float v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    };

    std::vector<ClickCase> cases = {
        { "STOP 3/4 hold",  [] (auto& ev) { set (ev, 0, pid::fxType, fxStop); hold (ev, 3.98, 6.3, pid::pad (4)); } },
        { "STOP 1/16 bar",  [] (auto& ev) { set (ev, 0, pid::fxType, fxStop); set (ev, 0, pid::fxLength, lenBar1); tap (ev, 3.98, pid::pad (0)); } },
        { "ECHO 1/4 hold",  [] (auto& ev) { set (ev, 0, pid::fxType, fxEcho); hold (ev, 3.98, 6.3, pid::pad (2)); } },
        { "ROLL switch",    [] (auto& ev) { set (ev, 0, pid::fxType, fxRoll); set (ev, 0, pid::fxLength, lenBar1); tap (ev, 3.98, pid::pad (3)); tap (ev, 5.98, pid::pad (1)); tap (ev, 6.48, pid::pad (0)); } },
        { "ROLL late hit",  [] (auto& ev) { set (ev, 0, pid::fxType, fxRoll); hold (ev, 4.05, 6.05, pid::pad (2)); } },
        { "GATE 1/8",       [] (auto& ev) { set (ev, 0, pid::fxType, fxGate); hold (ev, 3.98, 6.0, pid::pad (1)); } },
        { "BRAKE to bar",   [] (auto& ev) { set (ev, 0, pid::brakeLen, lenToBar); tap (ev, 5.98, pid::brake); } },
        { "SPIN to bar",    [] (auto& ev) { set (ev, 0, pid::brakeLen, lenToBar); tap (ev, 5.98, pid::spin); } },
        { "KILL low",       [] (auto& ev) { hold (ev, 3.5, 5.5, pid::killLow); } },
        { "RISE 1 bar",     [] (auto& ev) { set (ev, 0, pid::riseLen, 0); set (ev, 0, pid::buildVerb, 0.0f); set (ev, 0, pid::buildNoise, 0.0f); tap (ev, 3.98, pid::rise); } },
    };

    int failures = 0;
    for (auto& c : cases)
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        c.setup (ev);
        auto out = run (proc, ev, 3, sine);

        // largest jump between neighbouring samples (a clean 110 Hz sine at 0.5 has ~0.007,
        // a 3x speed backspin ~0.022; a click is far above that)
        float worst = 0.0f;
        int worstAt = 0;
        bool finite = true;
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            const float v = out.getSample (0, i);
            finite &= std::isfinite (v);
            const float d = std::abs (v - out.getSample (0, i - 1));
            if (d > worst) { worst = d; worstAt = i; }
        }
        const bool ok = finite && worst < 0.06f;
        failures += ok ? 0 : 1;
        std::printf ("%-16s max step %.4f at beat %.3f  %s\n", c.name, worst, worstAt / spb, ok ? "ok" : "CLICK");
    }
    return failures;
}


//------------------------------------------------------------------------------ timing test
// The input is a slow ramp, so every output sample tells which input sample it came from.
int timing()
{
    auto ramp = [] (juce::AudioBuffer<float>& b, double ppq)
    {
        const int64_t start = (int64_t) std::llround (ppq * spb);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = (float) ((start + i) % 400000) / 400000.0f;
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
    };
    auto source = [] (float v) { return (int64_t) std::llround ((double) v * 400000.0); };

    int failures = 0;
    auto check = [&] (const char* name, bool ok, const juce::String& info)
    {
        failures += ok ? 0 : 1;
        std::printf ("%-28s %s  %s\n", name, ok ? "ok   " : "FAIL ", info.toRawUTF8());
    };

    // STOP 3/4, pressed a bit before beat 4, held to beat 7
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        set (ev, 0, pid::fxType, fxStop);
        set (ev, 0, pid::fxFeedback, 0.5f);
        set (ev, 0, pid::fxTone, 0.0f);
        hold (ev, 3.97, 7.0, pid::pad (4));
        auto out = run (proc, ev, 3, ramp);

        const int t0 = (int) std::llround (4.0 * spb), D = (int) std::llround (0.75 * spb);
        // first repeat: output == input from exactly D samples earlier
        int bad = 0;
        for (int n = t0 + 400; n < t0 + D - 400; n += 97)
            bad += std::abs (source (out.getSample (0, n)) - (n - D)) > 1;
        check ("STOP 3/4 first repeat", bad == 0, "start beat 4.000, offset " + juce::String (D) + " samples, errors " + juce::String (bad));

        // second repeat: the same chunk again, at half level (feedback 0.5)
        bad = 0;
        for (int n = t0 + D + 400; n < t0 + 2 * D - 400; n += 97)
        {
            const float expected = 0.5f * out.getSample (0, n - D);
            bad += std::abs (out.getSample (0, n) - expected) > 0.002f;
        }
        check ("STOP 3/4 second repeat", bad == 0, "period 0.75 beat, feedback 0.5, errors " + juce::String (bad));

        // just before the start the dry signal is untouched
        check ("STOP dry before grid", source (out.getSample (0, t0 - 10)) == t0 - 10, "");
    }

    // ROLL 1/4 timed one bar, started early
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        set (ev, 0, pid::fxType, fxRoll);
        set (ev, 0, pid::fxLength, lenBar1);
        tap (ev, 3.97, pid::pad (2));
        auto out = run (proc, ev, 3, ramp);

        const int t0 = (int) std::llround (4.0 * spb), D = (int) std::llround (0.25 * spb);
        int bad = 0;
        for (int n = t0 + 300; n < t0 + 4 * (int) spb - 300; n += 61)
        {
            const int pos = (n - t0) % D;
            if (pos < 300 || pos > D - 300) continue;     // skip seam crossfades
            bad += std::abs (source (out.getSample (0, n)) - (t0 + pos)) > 1;
        }
        check ("ROLL 1/4 loop", bad == 0, "loop locked to beat 4.000, errors " + juce::String (bad));

        const int tEnd = t0 + (int) std::llround (4.0 * spb);
        check ("ROLL ends after 1 bar", source (out.getSample (0, tEnd + 400)) == tEnd + 400, "back to dry at beat 8.000");
    }

    // ROLL hit slightly late (block after the beat): loop must still start on the beat
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        set (ev, 0, pid::fxType, fxRoll);
        hold (ev, 4.04, 6.0, pid::pad (2));
        auto out = run (proc, ev, 3, ramp);

        const int t0 = (int) std::llround (4.0 * spb), D = (int) std::llround (0.25 * spb);
        int bad = 0;
        for (int n = t0 + D + 300; n < t0 + 6 * D; n += 61)
        {
            const int pos = (n - t0) % D;
            if (pos < 300 || pos > D - 300) continue;
            bad += std::abs (source (out.getSample (0, n)) - (t0 + pos)) > 1;
        }
        check ("ROLL late hit snaps to grid", bad == 0, "pressed at beat 4.04, errors " + juce::String (bad));
    }

    // BRAKE "to bar": silent at the end of the bar, dry again exactly on the next downbeat
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        set (ev, 0, pid::brakeLen, lenToBar);
        set (ev, 0, pid::brakeTime, 2);
        tap (ev, 5.97, pid::brake);
        auto out = run (proc, ev, 3, ramp);

        const int bar = (int) std::llround (8.0 * spb);
        const float silent = std::abs (out.getSample (0, bar - 2000));
        const bool back = source (out.getSample (0, bar + 300)) == bar + 300;
        check ("BRAKE silent before bar", silent < 1.0e-4f, juce::String (silent, 6));
        check ("BRAKE back on downbeat", back, "beat 8.000");

        // halfway through the brake the playhead must lag behind (speed < 1)
        const int mid = (int) std::llround (6.5 * spb);
        const auto lag = mid - source (out.getSample (0, mid));
        check ("BRAKE slows down", lag > 1000, "lag at beat 6.5: " + juce::String ((int) lag) + " samples");
    }

    // MIDI: note C1+4 (pad 3/4) held from 3.97 to 6.0 in STOP/HOLD must behave like the GUI pad
    {
        BrecherProcessor proc;
        std::vector<Event> ev;
        set (ev, 0, pid::fxType, fxStop);
        set (ev, 0, pid::fxFeedback, 0.0f);   // no tail, so the dry signal can be checked exactly
        set (ev, 0, pid::fxTone, 0.0f);
        std::vector<MidiEvent> me { { 3.97, juce::MidiMessage::noteOn (1, note::firstPad + 4, (juce::uint8) 100) },
                                    { 6.00, juce::MidiMessage::noteOff (1, note::firstPad + 4) },
                                    { 8.97, juce::MidiMessage::noteOn (1, note::brake, (juce::uint8) 100) },
                                    { 9.10, juce::MidiMessage::noteOff (1, note::brake) } };
        set (ev, 0, pid::brakeLen, lenToBar);
        auto out = run (proc, ev, 4, ramp, me);

        const int t0 = (int) std::llround (4.0 * spb), D = (int) std::llround (0.75 * spb);
        int bad = 0;
        for (int n = t0 + 400; n < t0 + D - 400; n += 97)
            bad += std::abs (source (out.getSample (0, n)) - (n - D)) > 1;
        check ("MIDI note -> STOP 3/4", bad == 0, "errors " + juce::String (bad));

        const int back = (int) std::llround (6.0 * spb) + 600;   // released at 6.0 -> dry from beat 6
        check ("MIDI note off -> dry", std::abs (out.getSample (0, back) - (float) (back % 400000) / 400000.0f) < 0.02f, "");

        const int bar = (int) std::llround (12.0 * spb);
        check ("MIDI brake to bar", std::abs (out.getSample (0, bar - 2000)) < 1.0e-4f
                                      && source (out.getSample (0, bar + 300)) == bar + 300, "silent, back on beat 12");
    }

    return failures;
}


int dropDebug()
{
    for (int variant = 0; variant < 5; ++variant)
    {
        BrecherProcessor proc;
        Groove groove;
        std::vector<Event> ev;
        set (ev, 0.0, pid::riseLen, 1);   // 2 bars
        set (ev, 0.0, pid::buildFilter, variant == 1 || variant == 4 ? 1.0f : 0.0f);
        set (ev, 0.0, pid::buildNoise,  variant == 2 || variant == 4 ? 1.0f : 0.0f);
        set (ev, 0.0, pid::buildVerb,   variant == 3 || variant == 4 ? 1.0f : 0.0f);
        tap (ev, 7.98, pid::rise);
        auto out = run (proc, ev, 6, [&] (auto& b, double ppq) { groove.render (b, ppq); });
        float pk = 0, pkDry = 0; int at = 0;
        for (int i = (int) (15.5 * spb); i < (int) (17.0 * spb); ++i)
        {
            const float a = std::max (std::abs (out.getSample (0, i)), std::abs (out.getSample (1, i)));
            if (a > pk) { pk = a; at = i; }
        }
        for (int i = (int) (19.5 * spb); i < (int) (21.0 * spb); ++i)
            pkDry = std::max (pkDry, std::abs (out.getSample (0, i)));
        const char* names[] = { "none", "filter", "noise", "verb", "all" };
        std::printf ("%-7s drop peak %.3f at beat %.3f   (later bar %.3f)\n", names[variant], pk, at / spb, pkDry);
    }
    return 0;
}

//------------------------------------------------------------------------------ snapshot
int snapshot (const juce::File& outFile, bool action)
{
    BrecherProcessor proc;

    if (action)
    {
        setParam (proc, pid::buildAmt, 0.35f);
        setParam (proc, pid::triplet, 0.0f);
        setParam (proc, pid::killLow, 1.0f);
        setParam (proc, pid::fxLength, lenToBar);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorAndMakeActive());
    ed->setSize (1000, 600);

    proc.ui.ppq = action ? 14.3 : 2.2;
    proc.ui.bpm = 150.0;
    proc.ui.playing = true;
    if (action)
    {
        proc.ui.fxState = 2; proc.ui.fxIdx = 4; proc.ui.fxType = fxStop; proc.ui.fxProgress = 0.4f;
        proc.ui.riseState = 2; proc.ui.riseProgress = 0.62f; proc.ui.buildAmount = 0.55f;
        proc.ui.brakeRate = 0.45f; proc.ui.brakeState = 0;
    }
    else
    {
        proc.ui.fxIdx = 4;
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    outFile.deleteFile();
    juce::FileOutputStream os (outFile);
    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream (img, os);
    ed.reset();
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "clicks";
    const juce::File out = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]) : juce::File();

    if (mode == "demo")     return demo (out);
    if (mode == "clicks")   return clicks();
    if (mode == "timing")   return timing();
    if (mode == "drop")     return dropDebug();
    if (mode == "snapshot") return snapshot (out, argc > 3 && juce::String (argv[3]) == "action");
    return 2;
}
