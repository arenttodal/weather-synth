#include "Engine.h"
#include <cmath>

namespace atmos
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr int kChunk = 32;

    // Equal-power dry/wet, as Tone.js CrossFade does
    inline float dryGain (double w) { return (float) std::cos (juce::jlimit (0.0, 1.0, w) * juce::MathConstants<double>::halfPi); }
    inline float wetGain (double w) { return (float) std::sin (juce::jlimit (0.0, 1.0, w) * juce::MathConstants<double>::halfPi); }

    // Tone.Distortion's curve
    inline float distort (float x, float k)
    {
        x = juce::jlimit (-1.0f, 1.0f, x);
        constexpr float deg = juce::MathConstants<float>::pi / 180.0f;
        return (3.0f + k) * x * 20.0f * deg / (juce::MathConstants<float>::pi + k * std::abs (x));
    }

    inline void smooth (double& v, double target, double a) { v += (target - v) * a; }
} // namespace

Engine::Engine()
{
    bankShimmer = std::make_shared<WavetableBank> (frames()[0].partials, sr);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = juce::jmax (maxBlock, kChunk);
    scratch.setSize (2, kChunk);

    bankShimmer = std::make_shared<WavetableBank> (frames()[0].partials, sr);
    installFrame (framePos.load() < 0 ? 0.3 : framePos.load());

    const juce::dsp::ProcessSpec stereo { sr, (juce::uint32) maxBlockSize, 2 };
    const juce::dsp::ProcessSpec mono { sr, (juce::uint32) maxBlockSize, 1 };
    for (auto* f : { &lp1, &lp2 })
    {
        f->prepare (stereo);
        f->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }
    noiseBp.prepare (mono);
    noiseBp.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    noiseBp.setResonance (0.7f);
    for (int ch = 0; ch < 2; ++ch)
    {
        loShelf[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 220.0, 0.7071, 1.0f);
        hiShelf[ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0, 0.7071, 1.0f);
        loShelf[ch].prepare (mono);
        hiShelf[ch].prepare (mono);
    }
    chorus.prepare (stereo);
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (0.0f);
    vibLine.prepare (stereo);
    vibLine.setMaximumDelayInSamples ((int) (0.01 * sr) + 8);
    echo.prepare (stereo);
    echo.setMaximumDelayInSamples ((int) (1.0 * sr) + 8);
    reverb.setSampleRate (sr);
    limiterRelease = (float) std::exp (-1.0 / (0.1 * sr));

    for (auto& v : voices)
    {
        v.amp.setSampleRate (sr);
        v.shimEnv.setSampleRate (sr);
    }
    reset();
}

void Engine::reset()
{
    for (auto& v : voices)
    {
        v.amp.reset();
        v.shimEnv.reset();
        v.note = -1;
        v.held = v.keyDown = false;
    }
    lp1.reset();
    lp2.reset();
    noiseBp.reset();
    for (int ch = 0; ch < 2; ++ch)
    {
        loShelf[ch].reset();
        hiShelf[ch].reset();
    }
    chorus.reset();
    vibLine.reset();
    echo.reset();
    reverb.reset();
    limiterGain = 1.0f;
    noiseGate = 0;
    first = true;
    lastShelfTilt = 1000.f;
}

std::shared_ptr<const WavetableBank> Engine::installFrame (double pos)
{
    auto fresh = std::make_shared<const WavetableBank> (framePartials (pos), sr);
    std::shared_ptr<const WavetableBank> old;
    {
        const juce::SpinLock::ScopedLockType sl (bankLock);
        old = std::move (bankPending);
        bankPending = std::move (fresh);
        pendingFresh = true;
    }
    framePos.store (pos);
    // 'old' is either null or a pending bank the audio thread never picked up;
    // banks the audio thread retired come back through the same slot later.
    return old;
}

void Engine::allNotesOff() noexcept
{
    for (auto& v : voices)
    {
        v.keyDown = v.held = false;
        v.amp.noteOff();
        v.shimEnv.noteOff();
    }
    sustainPedal = false;
}

int Engine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        n += v.active() ? 1 : 0;
    return n;
}

void Engine::noteOn (int note, float vel) noexcept
{
    Voice* pick = nullptr;
    for (auto& v : voices)
        if (! v.active() && (pick == nullptr || v.age < pick->age))
            pick = &v;
    if (pick == nullptr) // steal: prefer the oldest released voice, then the oldest
    {
        for (auto& v : voices)
            if (! v.held && (pick == nullptr || v.age < pick->age))
                pick = &v;
        if (pick == nullptr)
            for (auto& v : voices)
                if (pick == nullptr || v.age < pick->age)
                    pick = &v;
    }
    auto& v = *pick;
    v.note = note;
    v.velocity = vel;
    v.held = v.keyDown = true;
    v.age = ++ageCounter;
    v.phA = v.phB = v.phSub = v.phShim = 0;
    v.amp.reset();
    v.shimEnv.reset();
    v.amp.noteOn();
    v.shimEnv.noteOn();
}

void Engine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.note == note && v.keyDown)
        {
            v.keyDown = false;
            if (! sustainPedal)
            {
                v.held = false;
                v.amp.noteOff();
                v.shimEnv.noteOff();
            }
        }
}

void Engine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())
        sustainPedal = true;
    else if (m.isSustainPedalOff())
    {
        sustainPedal = false;
        for (auto& v : voices)
            if (v.held && ! v.keyDown)
            {
                v.held = false;
                v.amp.noteOff();
                v.shimEnv.noteOff();
            }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff();
    else if (m.isPitchWheel())
        pitchBendSemis = (m.getPitchWheelValue() - 8192) / 8192.0 * 2.0;
}

void Engine::updateChunkParams() noexcept
{
    // Pick up a freshly built waveform without blocking
    {
        const juce::SpinLock::ScopedTryLockType tl (bankLock);
        if (tl.isLocked() && pendingFresh)
        {
            std::swap (bankMain, bankPending); // the old bank waits in the pending slot to be freed off-thread
            pendingFresh = false;
        }
    }

    const double a = first ? 1.0 : 1.0 - std::exp (-kChunk / (sr * 0.25));
    if (first)
    {
        cur = target;
        first = false;
    }
    else
    {
        auto* c = reinterpret_cast<double*> (&cur);
        auto* t = reinterpret_cast<const double*> (&target);
        for (size_t i = 0; i < sizeof (Params) / sizeof (double); ++i)
            smooth (c[i], t[i], a);
        cur.bits = target.bits;
    }

    const juce::ADSR::Parameters env { (float) cur.attack, (float) cur.decay, (float) cur.sustain, (float) cur.release };
    const juce::ADSR::Parameters shim { (float) (cur.attack * 1.5 + 0.05), (float) cur.decay, (float) cur.sustain, (float) cur.release };
    for (auto& v : voices)
    {
        v.amp.setParameters (env);
        v.shimEnv.setParameters (shim);
    }

    const float cutoff = (float) juce::jlimit (30.0, sr * 0.45, cur.cutoff);
    const float res = (float) std::pow (10.0, cur.q / 20.0);
    lp1.setCutoffFrequency (cutoff);
    lp2.setCutoffFrequency (cutoff);
    lp1.setResonance (res);
    lp2.setResonance (res);

    if (std::abs ((float) cur.tilt - lastShelfTilt) > 0.05f)
    {
        lastShelfTilt = (float) cur.tilt;
        const auto lo = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sr, 220.0, 0.7071, juce::Decibels::decibelsToGain (-0.35f * lastShelfTilt));
        const auto hi = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 2500.0, 0.7071, juce::Decibels::decibelsToGain (lastShelfTilt));
        for (int ch = 0; ch < 2; ++ch)
        {
            *loShelf[ch].coefficients = lo;
            *hiShelf[ch].coefficients = hi;
        }
    }

    chorus.setRate ((float) juce::jlimit (0.05, 20.0, cur.chorusRate));
    chorus.setDepth ((float) juce::jlimit (0.0, 1.0, cur.chorusDepth));
    chorus.setMix ((float) juce::jlimit (0.0, 1.0, cur.chorusWet));

    noiseBp.setCutoffFrequency ((float) juce::jlimit (60.0, sr * 0.45, cur.noiseFreq));

    const int bits = (int) juce::jlimit (2.0, 16.0, cur.bits);
    if (bits != lastBits)
    {
        lastBits = bits;
        crushSteps = (float) std::pow (2.0, bits - 1);
    }

    juce::Reverb::Parameters rp;
    rp.roomSize = (float) juce::jlimit (0.3, 0.98, 0.3 + 0.038 * cur.verbDecay);
    rp.damping = 0.45f;
    rp.width = 1.0f;
    rp.dryLevel = dryGain (cur.verbWet) * 0.5f; // juce::Reverb scales dry by 2
    rp.wetLevel = wetGain (cur.verbWet) / 3.0f; // and wet by 3
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
}

void Engine::renderVoices (float* L, float* R, int n) noexcept
{
    const auto* main = bankMain.get();
    const auto* shimB = bankShimmer.get();
    if (main == nullptr) return;

    const double spreadUp = std::pow (2.0, cur.spread / 2400.0);
    const double subG = cur.sub * 0.398;   // lab: dB(sub) - 8
    const double shimG = cur.shimmer * 0.2; // lab: dB(shimmer) - 14
    const double bend = pitchBendSemis;

    for (auto& v : voices)
    {
        if (! v.active()) continue;
        const double f = 440.0 * std::pow (2.0, (v.note - 69 + bend) / 12.0);
        const double incA = f * spreadUp / sr, incB = f / spreadUp / sr, incSub = 0.5 * f / sr, incShim = 2.0 * f / sr;
        const int tA = main->tableFor (f * spreadUp), tB = main->tableFor (f / spreadUp), tS = shimB->tableFor (2.0 * f);
        const float vel = v.velocity;
        for (int i = 0; i < n; ++i)
        {
            const float e = v.amp.getNextSample() * vel;
            const float es = v.shimEnv.getNextSample() * vel * 0.7f;
            const float a = main->lookup (tA, v.phA) * 0.25f;
            const float b = main->lookup (tB, v.phB) * 0.25f;
            const float s = (float) (std::sin (kTwoPi * v.phSub) * subG);
            const float sh = (float) (shimB->lookup (tS, v.phShim) * shimG);
            const float mid = (s * e) + sh * es;
            L[i] += e * (0.7f * a + 0.3f * b) * 1.18f + mid;
            R[i] += e * (0.3f * a + 0.7f * b) * 1.18f + mid;
            v.phA += incA;
            v.phB += incB;
            v.phSub += incSub;
            v.phShim += incShim;
            v.phA -= std::floor (v.phA);
            v.phB -= std::floor (v.phB);
            v.phSub -= std::floor (v.phSub);
            v.phShim -= std::floor (v.phShim);
        }
        if (! v.active()) v.note = -1;
    }
}

void Engine::processFx (float* L, float* R, int n) noexcept
{
    const float k = (float) (cur.drive * 0.9 * 100.0);
    const double driveWet = juce::jlimit (0.0, 1.0, cur.drive * 1.3);
    const float dDry = dryGain (driveWet), dWet = wetGain (driveWet);
    const double crushWet = cur.bits < 16 ? juce::jlimit (0.0, 1.0, (16.0 - cur.bits) / 8.0) : 0.0;
    const float cDry = dryGain (crushWet), cWet = wetGain (crushWet);
    const float steps = crushSteps;

    bool anyHeld = false;
    for (auto& v : voices)
        anyHeld |= v.held;
    const double gateTau = anyHeld ? 0.05 + cur.attack / 3.0 : std::max (0.05, cur.release / 3.0);
    const float gateA = (float) (1.0 - std::exp (-1.0 / (sr * gateTau)));
    const float noiseLevel = (float) (cur.noise * 0.25);
    const float gustInc = (float) (0.2 / sr);

    const double vibInc = cur.vibRate / sr, tremInc = cur.tremRate / sr;
    const double vibDepth = cur.vibDepth, tremDepth = cur.tremDepth;
    const float eDry = dryGain (cur.delayWet), eWet = wetGain (cur.delayWet);
    const float fb = (float) cur.delayFb;
    const float delaySamples = (float) juce::jlimit (1.0, sr * 0.95, cur.delayTime * sr);
    echo.setDelay (delaySamples);

    float* ch[2] = { L, R };
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            float x = ch[c][i];
            if (driveWet > 0.0005) x = x * dDry + distort (x, k) * dWet;
            if (crushWet > 0.0005) x = x * cDry + (std::round (juce::jlimit (-1.0f, 1.0f, x) * steps) / steps) * cWet;
            x = lp1.processSample (c, x);
            x = lp2.processSample (c, x);
            x = loShelf[c].processSample (x);
            x = hiShelf[c].processSample (x);
            ch[c][i] = x;
        }
    }

    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
    chorus.process (juce::dsp::ProcessContextReplacing<float> (block));

    for (int i = 0; i < n; ++i)
    {
        // Vibrato: 2.5 ms ± depth, fully wet (Tone.Vibrato)
        const double vd = 0.0025 * (1.0 + vibDepth * std::sin (kTwoPi * vibPhase));
        vibPhase += vibInc;
        vibPhase -= std::floor (vibPhase);
        const float tremL = (float) (1.0 - tremDepth * (0.5 + 0.5 * std::sin (kTwoPi * tremPhase)));
        const float tremR = (float) (1.0 - tremDepth * (0.5 - 0.5 * std::sin (kTwoPi * tremPhase)));
        tremPhase += tremInc;
        tremPhase -= std::floor (tremPhase);

        // Rain / wind bed (Paul Kellet's pink noise -> band-pass -> gate -> gusts)
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        pink[0] = 0.99886f * pink[0] + w * 0.0555179f;
        pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
        pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
        pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
        pink[4] = 0.55000f * pink[4] + w * 0.5329522f;
        pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
        const float pn = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362f) * 0.11f;
        pink[6] = w * 0.115926f;
        noiseGate += ((anyHeld ? 1.0f : 0.0f) - noiseGate) * gateA;
        gustPhase += gustInc;
        gustPhase -= std::floor (gustPhase);
        const float gust = 0.65f + 0.35f * std::sin ((float) kTwoPi * gustPhase);
        const float bed = noiseBp.processSample (0, pn) * noiseGate * gust * noiseLevel;

        const float trem[2] = { tremL, tremR };
        for (int c = 0; c < 2; ++c)
        {
            vibLine.pushSample (c, ch[c][i]);
            float x = vibLine.popSample (c, (float) (vd * sr)) * trem[c] + bed;
            const float d = echo.popSample (c);
            echo.pushSample (c, x + d * fb);
            ch[c][i] = x * eDry + d * eWet;
        }
    }

    reverb.processStereo (L, R, n);

    // Output level, then a safety limiter: instant attack, 100 ms release, -1 dBFS ceiling.
    // Normal playing stays well below it; it only catches extreme stacks.
    const float outGain = 0.4f * juce::Decibels::decibelsToGain ((float) cur.trim);
    constexpr float ceiling = 0.891f;
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i] * outGain, r = R[i] * outGain;
        const float peak = std::max (std::abs (l), std::abs (r));
        const float needed = peak > ceiling ? ceiling / peak : 1.0f;
        limiterGain = needed < limiterGain ? needed : needed - (needed - limiterGain) * limiterRelease;
        L[i] = juce::jlimit (-ceiling, ceiling, l * limiterGain);
        R[i] = juce::jlimit (-ceiling, ceiling, r * limiterGain);
    }
}

void Engine::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept
{
    // Works in chunks of at most kChunk samples, so any host block size is fine
    const int total = out.getNumSamples();
    const int outCh = out.getNumChannels();
    float* L = scratch.getWritePointer (0);
    float* R = scratch.getWritePointer (1);

    auto it = midi.cbegin();
    int pos = 0;
    while (pos < total)
    {
        while (it != midi.cend() && (*it).samplePosition <= pos)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        int end = juce::jmin (total, pos + kChunk);
        if (it != midi.cend() && (*it).samplePosition < end) end = juce::jmax (pos + 1, (*it).samplePosition);
        const int n = end - pos;
        juce::FloatVectorOperations::clear (L, n);
        juce::FloatVectorOperations::clear (R, n);
        updateChunkParams();
        renderVoices (L, R, n);
        processFx (L, R, n);
        if (outCh >= 2)
        {
            out.copyFrom (0, pos, L, n);
            out.copyFrom (1, pos, R, n);
            for (int c = 2; c < outCh; ++c)
                out.clear (c, pos, n);
        }
        else if (outCh == 1)
        {
            out.copyFrom (0, pos, L, n, 0.5f);
            out.addFrom (0, pos, R, n, 0.5f);
        }
        pos = end;
    }
    // Events at or past the block end (hosts shouldn't send them, but some do)
    for (; it != midi.cend(); ++it)
        handleMidi ((*it).getMessage());
}
} // namespace atmos
