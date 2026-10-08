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
    // OSCILLATORS (two analogue-style oscillators, sub, noise; Prophet-5 / Memorymoog lineage)
    oscAWave, oscAPw, oscBWave, oscBInterval, oscBDetune, oscSync, mixA, mixB, mixSub, mixNoise,
    slop, voiceMode, stackDetune, portamento,
    // FILTER (transistor ladder or state-variable, with its own ADSR)
    filtType, cutoff, resonance, filtDrive, keyTrack, filtEnv, filtVel, fAttack, fDecay, fSustain, fRelease,
    // AMP
    attack, decay, sustain, release, ampVel,
    // MODULATION (LFO and Prophet-style poly-mod)
    lfoRate, lfoShape, vibDepth, lfoPwm, lfoFilter, pmEnvA, pmOscB,
    // COLOUR (bus saturation, tilt, and the heat's breakup)
    drive, tilt, crushBits, crushRate,
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
        { "oscAWave", "Osc A wave", "Oscillators", 0, 1, 0, Scale::linear, nullptr },
        { "oscAPw", "Osc A pulse width", "Oscillators", 0.05, 0.5, 0.4, Scale::linear, nullptr },
        { "oscBWave", "Osc B wave", "Oscillators", 0, 2, 1, Scale::linear, nullptr },
        { "oscBInterval", "Osc B interval", "Oscillators", 0, 4, 1, Scale::choice, "-1 oct|Unison|+5th|+1 oct|+2 oct" },
        { "oscBDetune", "Osc B detune", "Oscillators", 0, 40, 7, Scale::linear, nullptr },
        { "oscSync", "Sync A to B", "Oscillators", 0, 1, 0, Scale::choice, "Off|On" },
        { "mixA", "Osc A level", "Oscillators", 0, 1, 0.8, Scale::linear, nullptr },
        { "mixB", "Osc B level", "Oscillators", 0, 1, 0.6, Scale::linear, nullptr },
        { "mixSub", "Sub level", "Oscillators", 0, 1, 0.15, Scale::linear, nullptr },
        { "mixNoise", "Noise level", "Oscillators", 0, 1, 0, Scale::linear, nullptr },
        { "slop", "Analogue drift", "Oscillators", 0, 1, 0.3, Scale::linear, nullptr },
        { "voiceMode", "Voices", "Oscillators", 0, 2, 0, Scale::choice, "Poly|Stack 2|Stack 4" },
        { "stackDetune", "Stack detune", "Oscillators", 0, 40, 10, Scale::linear, nullptr },
        { "portamento", "Glide", "Oscillators", 0, 1, 0, Scale::linear, nullptr },

        { "filtType", "Filter", "Filter", 0, 3, 0, Scale::choice, "Moog ladder|Prophet ladder|SEM lowpass|SEM bandpass" },
        { "cutoff", "Cutoff", "Filter", 30, 16000, 1800, Scale::log, nullptr },
        { "resonance", "Resonance", "Filter", 0, 1, 0.2, Scale::linear, nullptr },
        { "filtDrive", "Filter drive", "Filter", 0, 1, 0.25, Scale::linear, nullptr },
        { "keyTrack", "Key tracking", "Filter", 0, 1, 0.5, Scale::linear, nullptr },
        { "filtEnv", "Envelope amount", "Filter", -1, 1, 0.4, Scale::linear, nullptr },
        { "filtVel", "Velocity to filter", "Filter", 0, 1, 0.3, Scale::linear, nullptr },
        { "fAttack", "Filter attack", "Filter", 0.001, 8, 0.01, Scale::log, nullptr },
        { "fDecay", "Filter decay", "Filter", 0.02, 10, 0.6, Scale::log, nullptr },
        { "fSustain", "Filter sustain", "Filter", 0, 1, 0.3, Scale::linear, nullptr },
        { "fRelease", "Filter release", "Filter", 0.02, 12, 0.8, Scale::log, nullptr },

        { "attack", "Attack", "Amp", 0.001, 8, 0.01, Scale::log, nullptr },
        { "decay", "Decay", "Amp", 0.02, 10, 1, Scale::log, nullptr },
        { "sustain", "Sustain", "Amp", 0, 1, 0.8, Scale::linear, nullptr },
        { "release", "Release", "Amp", 0.02, 12, 0.6, Scale::log, nullptr },
        { "ampVel", "Velocity to level", "Amp", 0, 1, 0.4, Scale::linear, nullptr },

        { "lfoRate", "LFO rate", "Modulation", 0.05, 20, 4.5, Scale::log, nullptr },
        { "lfoShape", "LFO shape", "Modulation", 0, 3, 0, Scale::choice, "Triangle|Saw|Square|Random" },
        { "vibDepth", "LFO to pitch", "Modulation", 0, 1, 0.03, Scale::linear, nullptr },
        { "lfoPwm", "LFO to pulse width", "Modulation", 0, 1, 0, Scale::linear, nullptr },
        { "lfoFilter", "LFO to filter", "Modulation", 0, 1, 0, Scale::linear, nullptr },
        { "pmEnvA", "Poly-mod env > A pitch", "Modulation", -1, 1, 0, Scale::linear, nullptr },
        { "pmOscB", "Poly-mod B > cutoff", "Modulation", 0, 1, 0, Scale::linear, nullptr },

        { "drive", "Saturation", "Colour", 0, 1, 0.15, Scale::linear, nullptr },
        { "tilt", "Brightness", "Colour", -12, 6, 0, Scale::linear, nullptr },
        { "crushBits", "Bits", "Colour", 4, 16, 16, Scale::linear, nullptr },
        { "crushRate", "Sample-rate crush", "Colour", 0, 1, 0, Scale::linear, nullptr },

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
