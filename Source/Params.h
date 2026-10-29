#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bm
{
// ---------------------------------------------------------------- parameter IDs
namespace pid
{
    // Isolate
    inline constexpr const char* isoLow   = "isoLow";
    inline constexpr const char* isoMid   = "isoMid";
    inline constexpr const char* isoHigh  = "isoHigh";
    inline constexpr const char* killLow  = "killLow";
    inline constexpr const char* killMid  = "killMid";
    inline constexpr const char* killHigh = "killHigh";

    // Build
    inline constexpr const char* buildAmt    = "buildAmt";
    inline constexpr const char* buildFilter = "buildFilter";
    inline constexpr const char* buildNoise  = "buildNoise";
    inline constexpr const char* buildVerb   = "buildVerb";
    inline constexpr const char* riseLen     = "riseLen";
    inline constexpr const char* rise        = "rise";        // rising edge = start / drop

    // Beat FX
    inline constexpr const char* fxType     = "fxType";
    inline constexpr const char* triplet    = "triplet";
    inline constexpr const char* fxDepth    = "fxDepth";
    inline constexpr const char* fxFeedback = "fxFeedback";
    inline constexpr const char* fxTone     = "fxTone";
    inline constexpr const char* fxLength   = "fxLength";

    // Brake / Spin
    inline constexpr const char* brake     = "brake";
    inline constexpr const char* spin      = "spin";
    inline constexpr const char* brakeTime = "brakeTime";
    inline constexpr const char* brakeLen  = "brakeLen";

    // Global
    inline constexpr const char* quant   = "quant";
    inline constexpr const char* outGain = "outGain";

    inline juce::String pad (int i)   { return "pad" + juce::String (i); }
}

// ---------------------------------------------------------------- choices
inline constexpr int numPads = 8;

/** Beat values of the 8 pads, in beats (1/1 = one quarter note, 4/1 = one bar). */
inline constexpr double padBeats[numPads] = { 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 0.75, 1.0, 2.0, 4.0 };
inline const char* const padNames[numPads] = { "1/16", "1/8", "1/4", "1/2", "3/4", "1/1", "2/1", "4/1" };

enum FxType { fxEcho = 0, fxStop, fxRoll, fxGate };
inline const char* const fxTypeNames[] = { "ECHO", "STOP", "ROLL", "GATE" };

enum LenMode { lenHold = 0, lenLatch, lenBeat1, lenBeat2, lenToBar, lenBar1, lenBar2, lenBar4 };
inline const char* const lenNames[] = { "HOLD", "LATCH", "1 BEAT", "2 BEATS", "TO BAR", "1 BAR", "2 BARS", "4 BARS" };
inline constexpr int numLenModes = 8;
inline bool isTimed (int lenMode) { return lenMode >= lenBeat1; }

enum QuantMode { qOff = 0, qQuarter, qBeat, qBar };
inline const char* const quantNames[] = { "OFF", "1/4", "BEAT", "BAR" };

inline constexpr int numBrakeTimes = 5;
inline constexpr double brakeTimeBeats[numBrakeTimes] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
inline const char* const brakeTimeNames[numBrakeTimes] = { "1/4", "1/2", "1/1", "2/1", "4/1" };

inline constexpr int numRiseLens = 5;
inline constexpr int riseBars[numRiseLens] = { 1, 2, 4, 8, 16 };

// ---------------------------------------------------------------- MIDI note map (C1 = 36)
namespace note
{
    inline constexpr int firstPad = 36;  // C1 .. G1 -> pads 1/16 .. 4/1
    inline constexpr int brake    = 44;  // G#1
    inline constexpr int spin     = 45;  // A1
    inline constexpr int rise     = 46;  // A#1
    inline constexpr int killLow  = 48;  // C2   (held = kill)
    inline constexpr int killMid  = 49;  // C#2
    inline constexpr int killHigh = 50;  // D2
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace bm
