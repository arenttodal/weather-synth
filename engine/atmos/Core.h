#pragma once
// Atmospheric's sound engine: plain C++20, no JUCE, so the plugin and the browser
// (WebAssembly) run identical code. Effects come from OSP (engine/osp).
#include "Params.h"
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
// An analogue-style polysynth voice (two band-limited oscillators, sub, noise, transistor
// ladder or state-variable filter with its own envelope, Prophet-style poly-mod), followed
// by OSP's studio: Kaleidoscope, Movement, Echo and Space.
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
    // Exponential (RC-style) ADSR, as analogue envelope generators curve
    struct Env
    {
        enum Stage { idle, att, dec, rel } stage = idle;
        double level = 0, aC = 0.01, dC = 0.001, s = 1, rC = 0.001;
        void set (double sr, double A, double D, double S, double R, double scale);
        void on() { stage = att; }
        void off() { if (stage != idle) stage = rel; }
        bool active() const { return stage != idle; }
        double next() noexcept;
    };

    struct Voice
    {
        int note = -1;
        float vel = 0;
        bool keyDown = false, held = false;
        uint64_t age = 0;
        double phA = 0, phB = 0, phSub = 0;
        double pitch = 60; // gliding note number
        double stackCents = 0, pan = 0;
        // Analogue character: fixed per voice card, scaled by 'slop'
        double cardCents = 0, cardCutoff = 0, cardEnv = 1;
        double drift = 0, driftTarget = 0;
        int driftCount = 0;
        Env amp, fenv;
        // Filter state (ladder stages or SVF)
        double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        float noiseLp = 0;
        uint32_t rng = 1;
    };

    void controlTick() noexcept;
    void updateResonances() noexcept;
    void renderVoices (float* L, float* R, int n) noexcept;
    void startVoice (Voice& v, int note, float vel, double stackCents, double pan) noexcept;

    double sr = 48000;
    Patch target, cur; // cur = smoothed copy of target for continuous parameters
    bool first = true;

    std::array<Voice, kVoices> voices;
    uint64_t ageCounter = 0;
    bool sustainDown = false;
    double bendSemis = 0;
    int countdown = 0;
    double lastNote = -1;
    double lfoPh = 0, lfoValue = 0, lfoHeld = 0;
    uint32_t lfoRng = 0x2545f491u;

    // Bus
    osp::ShelfFilter loShelf[2], hiShelf[2];
    float lastTilt = 1000.f;
    float crushHold[2] {};
    double crushPhase = 0;
    float dcX[2] {}, dcY[2] {};
    osp::ReimaginedStage kaleido;
    osp::MovementBus movement;
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
