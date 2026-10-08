#pragma once
// Atmospheric's sound engine: plain C++20, no JUCE, so the plugin and the browser
// (WebAssembly) run identical code. Effects come from OSP (engine/osp).
#include "Params.h"
#include "engine/CharacterFilter.h"
#include "engine/EchoDelay.h"
#include "engine/MovementBus.h"
#include "engine/ReimaginedStage.h"
#include "engine/ShelfFilter.h"
#include "engine/SpaceReverb.h"
#include <array>
#include <cstdint>
#include <vector>

namespace atmos
{
// Eight harmonic frames, 16 partials each, ordered dark/sparse -> bright/dense
constexpr int kFrames = 8;
const char* frameName (double position);

class Core
{
public:
    static constexpr int kVoices = 12;
    static constexpr int kControl = 32; // control-rate interval in samples

    Core();
    void prepare (double sampleRate, int maxBlock);
    void reset();

    // Not thread-safe by itself: call from the audio thread (the plugin hands patches over)
    void setPatch (const Patch& p);
    void setParam (int index, double value);
    const Patch& patch() const { return target; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();
    void setSustain (bool down);
    void setPitchBend (double semitones) { bendSemis = semitones; }

    // Renders n samples (any n) into L and R, replacing their contents
    void process (float* L, float* R, int n) noexcept;

    int activeVoices() const noexcept;
    double sampleRate() const noexcept { return sr; }

private:
    struct Adsr
    {
        enum Stage { idle, att, dec, sus, rel } stage = idle;
        double level = 0, a = 0.01, d = 0.1, s = 1, r = 0.1, sr = 48000;
        void set (double A, double D, double S, double R) { a = A; d = D; s = S; r = R; }
        void on() { stage = att; }
        void off() { if (stage != idle) stage = rel; }
        bool active() const { return stage != idle; }
        float next() noexcept;
    };

    struct Voice
    {
        int note = -1;
        float vel = 0;
        bool keyDown = false, held = false;
        uint64_t age = 0;
        double ph[8] {}; // oscillator phases (layers / saw voices)
        double phSub = 0, phShim = 0, phMod[2] {}, fbPrev[2] {};
        double vibPh = 0, driftPos = 0, driftTarget = 0;
        int driftCount = 0;
        Adsr amp;
        double fenv = 0; // filter envelope 1 -> 0
        osp::CharacterFilter filter;
        std::vector<float> ks; // Karplus-Strong line
        int ksLen = 0, ksPos = 0;
        float ksLp = 0;
        uint32_t rng = 1;
    };

    void controlTick() noexcept;
    void updateResonances() noexcept;
    float frameSample (int octave, double frame, double phase) const noexcept;
    int octaveFor (double hz) const noexcept;
    void renderVoices (float* L, float* R, int n) noexcept;
    void startPluck (Voice& v, double hz) noexcept;

    double sr = 48000;
    Patch target, cur; // cur = smoothed copy of target for continuous parameters
    bool first = true;

    std::array<Voice, kVoices> voices;
    uint64_t ageCounter = 0;
    bool sustainDown = false;
    double bendSemis = 0;
    int countdown = 0;

    // Wavetables: [frame][octave][2048 + 1]
    static constexpr int kTable = 2048, kOctaves = 11;
    std::vector<float> tables;

    // Bus
    osp::ShelfFilter loShelf[2], hiShelf[2];
    float lastTilt = 1000.f;
    float crushHold[2] {};
    double crushPhase = 0;
    osp::ReimaginedStage kaleido;
    osp::MovementBus movement;
    int lastMovMode = -1;
    osp::EchoDelay echo;
    bool echoIdle = true;
    double echoLevel = 0;
    osp::SpaceReverb reverbs[2];
    int activeReverb = 0, reverbFade = 0, reverbFadeLength = 1;
    osp::SpaceReverb::Settings appliedSpace;
    double spaceLevel = 0;
    bool spaceIdle = true;
    double levelCoef = 0.001;
    float pink[7] {};
    uint32_t bedRng = 0x41544d4f;
    float bedLp1 = 0, bedLp2 = 0, bedGate = 0, gustPh = 0;
    float limGain = 1.0f, limRelease = 0.9998f;
    bool resonancesDirty = true;
};
} // namespace atmos
