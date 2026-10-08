#pragma once
// The single parameter schema shared by the plugin, the WebAssembly build and the
// sound designer. Index order is the wire format: append new parameters at the end.
#include <cstddef>

namespace atmos
{
enum class Scale { linear, log, choice };

struct ParamInfo
{
    const char* id;
    const char* name;
    const char* group;
    double min, max, def;
    Scale scale;
    const char* choices; // "|"-separated for Scale::choice
};

enum P : int
{
    // SOURCE
    srcType, srcFrame, fmRatio, fmIndex, fmFeedback, sawVoices, sawDetune, pluckDamp, pluckBright,
    srcSpread, srcSub, srcShimmer, srcBreath,
    // VOICE
    attack, decay, sustain, release, vibDepth, vibRate, glide,
    // DRIVE
    drive, crushBits, crushRate,
    // CHARACTER (OSP CharacterFilter)
    filtType, cutoff, resonance, filtDrive, filtEnv, filtEnvDecay, tilt,
    // KALEIDOSCOPE (OSP ReimaginedStage)
    kalAmount, kalFocus, kalSpread,
    // MOVEMENT (OSP MovementBus)
    movMode, movAmount, movA, movB, movC,
    // ECHO (OSP EchoDelay, a send)
    echoType, echoSend, echoTime, echoFeedback, echoTone, echoAge, echoStereo,
    // SPACE (OSP SpaceReverb, a send)
    spaceType, spaceSend, spaceDecay, spaceSize, spaceDamping, spaceMod, spacePreDelay,
    // WEATHER BED
    bedLevel, bedColour,
    // OUTPUT
    outGain,
    kNumParams
};

inline const ParamInfo& paramInfo (int i)
{
    static const ParamInfo table[kNumParams] = {
        { "srcType", "Source", "Source", 0, 3, 0, Scale::choice, "Wavetable|FM|Supersaw|Pluck" },
        { "srcFrame", "Wavetable", "Source", 0, 1, 0.35, Scale::linear, nullptr },
        { "fmRatio", "FM ratio", "Source", 0.5, 8, 2, Scale::log, nullptr },
        { "fmIndex", "FM index", "Source", 0, 10, 2.5, Scale::linear, nullptr },
        { "fmFeedback", "FM feedback", "Source", 0, 1, 0, Scale::linear, nullptr },
        { "sawVoices", "Saw voices", "Source", 1, 7, 5, Scale::linear, nullptr },
        { "sawDetune", "Saw detune", "Source", 0, 60, 18, Scale::linear, nullptr },
        { "pluckDamp", "Pluck damping", "Source", 0, 1, 0.4, Scale::linear, nullptr },
        { "pluckBright", "Pluck brightness", "Source", 0, 1, 0.6, Scale::linear, nullptr },
        { "srcSpread", "Layer spread", "Source", 0, 40, 6, Scale::linear, nullptr },
        { "srcSub", "Sub octave", "Source", 0, 1, 0.2, Scale::linear, nullptr },
        { "srcShimmer", "Octave shimmer", "Source", 0, 1, 0, Scale::linear, nullptr },
        { "srcBreath", "Breath noise", "Source", 0, 1, 0, Scale::linear, nullptr },

        { "attack", "Attack", "Voice", 0.002, 4, 0.08, Scale::log, nullptr },
        { "decay", "Decay", "Voice", 0.05, 6, 0.8, Scale::log, nullptr },
        { "sustain", "Sustain", "Voice", 0, 1, 0.75, Scale::linear, nullptr },
        { "release", "Release", "Voice", 0.05, 12, 1.2, Scale::log, nullptr },
        { "vibDepth", "Vibrato", "Voice", 0, 1, 0.05, Scale::linear, nullptr },
        { "vibRate", "Vibrato rate", "Voice", 0.1, 10, 4.5, Scale::log, nullptr },
        { "glide", "Drift", "Voice", 0, 1, 0.15, Scale::linear, nullptr },

        { "drive", "Drive", "Drive", 0, 1, 0, Scale::linear, nullptr },
        { "crushBits", "Bits", "Drive", 4, 16, 16, Scale::linear, nullptr },
        { "crushRate", "Sample-rate crush", "Drive", 0, 1, 0, Scale::linear, nullptr },

        { "filtType", "Filter", "Character", 0, 4, 0, Scale::choice, "LP24|LP12|HP12|BP12|Tilt" },
        { "cutoff", "Cutoff", "Character", 40, 18000, 2400, Scale::log, nullptr },
        { "resonance", "Resonance", "Character", 0, 0.9, 0.12, Scale::linear, nullptr },
        { "filtDrive", "Filter drive", "Character", 0, 1, 0.1, Scale::linear, nullptr },
        { "filtEnv", "Filter envelope", "Character", -1, 1, 0.15, Scale::linear, nullptr },
        { "filtEnvDecay", "Filter env decay", "Character", 0.02, 4, 0.6, Scale::log, nullptr },
        { "tilt", "Brightness", "Character", -12, 6, 0, Scale::linear, nullptr },

        { "kalAmount", "Kaleidoscope", "Kaleidoscope", 0, 1, 0, Scale::linear, nullptr },
        { "kalFocus", "Focus", "Kaleidoscope", 0, 1, 0.5, Scale::linear, nullptr },
        { "kalSpread", "Spread", "Kaleidoscope", 0, 1, 0.5, Scale::linear, nullptr },

        { "movMode", "Movement", "Movement", 0, 4, 2, Scale::choice, "Off|Tape|Chorus|Pulse|Shaper" },
        { "movAmount", "Movement amount", "Movement", 0, 1, 0.3, Scale::linear, nullptr },
        { "movA", "Rate / wow / pattern", "Movement", 0, 1, 0.45, Scale::linear, nullptr },
        { "movB", "Width / flutter / shape", "Movement", 0, 1, 0.6, Scale::linear, nullptr },
        { "movC", "Stereo / wear / smooth", "Movement", 0, 1, 0.6, Scale::linear, nullptr },

        { "echoType", "Echo", "Echo", 0, 1, 0, Scale::choice, "Tape|BBD" },
        { "echoSend", "Echo send", "Echo", 0, 1, 0.1, Scale::linear, nullptr },
        { "echoTime", "Echo time", "Echo", 40, 1200, 375, Scale::log, nullptr },
        { "echoFeedback", "Feedback", "Echo", 0, 1, 0.4, Scale::linear, nullptr },
        { "echoTone", "Echo tone", "Echo", 0, 1, 0.5, Scale::linear, nullptr },
        { "echoAge", "Echo age", "Echo", 0, 1, 0.35, Scale::linear, nullptr },
        { "echoStereo", "Echo stereo", "Echo", 0, 2, 1, Scale::choice, "Mono|Ping-pong|Wide" },

        { "spaceType", "Space", "Space", 0, 3, 2, Scale::choice, "Room|Hall|Plate|Spring" },
        { "spaceSend", "Space send", "Space", 0, 1, 0.3, Scale::linear, nullptr },
        { "spaceDecay", "Decay", "Space", 0.2, 12, 2.5, Scale::log, nullptr },
        { "spaceSize", "Size", "Space", 0, 1, 0.5, Scale::linear, nullptr },
        { "spaceDamping", "Damping", "Space", 0, 1, 0.4, Scale::linear, nullptr },
        { "spaceMod", "Modulation", "Space", 0, 1, 0.4, Scale::linear, nullptr },
        { "spacePreDelay", "Pre-delay", "Space", 0, 250, 10, Scale::linear, nullptr },

        { "bedLevel", "Weather bed", "Bed", 0, 1, 0, Scale::linear, nullptr },
        { "bedColour", "Bed colour", "Bed", 200, 8000, 1800, Scale::log, nullptr },

        { "outGain", "Output", "Output", -24, 6, 0, Scale::linear, nullptr },
    };
    return table[i];
}

// A complete set of parameter values
struct Patch
{
    double v[kNumParams];
    Patch()
    {
        for (int i = 0; i < kNumParams; ++i)
            v[i] = paramInfo (i).def;
    }
    double& operator[] (int i) { return v[i]; }
    double operator[] (int i) const { return v[i]; }
};
} // namespace atmos
