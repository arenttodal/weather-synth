#pragma once
#include "ClimateMapper.h"
#include "Wavetable.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

namespace atmos
{
// The one sound. Signal chain mirrors lab.html:
// voices -> drive -> bit reduction -> 24 dB low-pass -> shelves -> chorus
//        -> vibrato -> tremolo -> feedback delay (+ rain/wind bed) -> reverb -> trim -> safety limiter
class Engine
{
public:
    static constexpr int kVoices = 12;

    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();

    // Audio thread: new target parameters (smoothed internally)
    void setTarget (const Params& p) noexcept { target = p; }

    // Message thread: install the waveform for a new climate frame position.
    // Returns a retired bank (or null) so it is freed here, never on the audio thread.
    std::shared_ptr<const WavetableBank> installFrame (double framePosition);
    double currentFrame() const noexcept { return framePos.load(); }

    // Audio thread
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept;
    void allNotesOff() noexcept;
    int activeVoiceCount() const noexcept;

private:
    struct Voice
    {
        int note = -1;
        float velocity = 0;
        bool held = false; // key down (or sustained by pedal)
        bool keyDown = false;
        uint64_t age = 0;
        double phA = 0, phB = 0, phSub = 0, phShim = 0;
        juce::ADSR amp, shimEnv;
        bool active() const { return amp.isActive() || shimEnv.isActive(); }
    };

    void handleMidi (const juce::MidiMessage& m) noexcept;
    void noteOn (int note, float vel) noexcept;
    void noteOff (int note) noexcept;
    void updateChunkParams() noexcept;
    void renderVoices (float* L, float* R, int n) noexcept;
    void processFx (float* L, float* R, int n) noexcept;

    double sr = 44100;
    int maxBlockSize = 512;
    Params target {}, cur {};
    bool first = true;

    std::array<Voice, kVoices> voices {};
    uint64_t ageCounter = 0;
    bool sustainPedal = false;
    double pitchBendSemis = 0;

    // Waveform banks: written on the message thread, picked up atomically
    std::shared_ptr<const WavetableBank> bankMain, bankPending, bankShimmer;
    juce::SpinLock bankLock;
    bool pendingFresh = false; // guarded by bankLock
    std::atomic<double> framePos { -1.0 };

    // Noise bed
    float pink[7] {};
    juce::Random rng { 0x41544d4f };
    juce::dsp::StateVariableTPTFilter<float> noiseBp;
    float noiseGate = 0, gustPhase = 0;

    // FX
    juce::dsp::StateVariableTPTFilter<float> lp1, lp2;
    juce::dsp::IIR::Filter<float> loShelf[2], hiShelf[2];
    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> vibLine { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echo { 96000 * 2 };
    juce::Reverb reverb;
    float limiterGain = 1.0f, limiterRelease = 0.9998f;
    double vibPhase = 0, tremPhase = 0;
    float lastShelfTilt = 1000.f;
    int lastBits = 16;
    float crushSteps = 32768.f;

    juce::AudioBuffer<float> scratch;
};
} // namespace atmos
