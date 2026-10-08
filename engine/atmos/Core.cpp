#include "Core.h"
#include "engine/Shaping.h"
#include <algorithm>
#include <cmath>

namespace atmos
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    struct FrameDef
    {
        const char* name;
        float p[16];
    };
    const FrameDef kFrameDefs[kFrames] = {
        { "Glass", { 1, 0, 0.5f, 0, 0, 0, 0.35f, 0, 0, 0, 0.25f, 0, 0, 0.15f, 0, 0 } },
        { "Breath", { 1, 0.25f, 0.08f, 0.03f, 0.01f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Hollow", { 1, 0, 0.6f, 0, 0.12f, 0, 0.3f, 0, 0.05f, 0, 0.12f, 0, 0, 0, 0.05f, 0 } },
        { "Reed", { 1, 0, 0.33f, 0, 0.2f, 0, 0.14f, 0, 0.11f, 0, 0.09f, 0, 0.07f, 0, 0.06f, 0 } },
        { "Organ", { 1, 0.7f, 0, 0.5f, 0, 0, 0, 0.3f, 0, 0, 0, 0, 0, 0, 0, 0.15f } },
        { "String", { 1, 1 / 2.f, 1 / 3.f, 1 / 4.f, 1 / 5.f, 1 / 6.f, 1 / 7.f, 1 / 8.f, 1 / 9.f, 1 / 10.f, 1 / 11.f, 1 / 12.f, 1 / 13.f, 1 / 14.f, 1 / 15.f, 1 / 16.f } },
        { "Choir", { 0.5f, 0.6f, 1, 0.8f, 0.35f, 0.15f, 0.25f, 0.4f, 0.3f, 0.12f, 0.05f, 0.03f, 0.02f, 0.01f, 0, 0 } },
        { "Brass", { 0.6f, 0.8f, 1, 0.9f, 0.7f, 0.5f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f, 0.01f } },
    };

    inline float rnd (uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((s >> 8) & 0xffffff) / 8388608.0f - 1.0f;
    }
    // Tone.js Distortion curve, as in the lab
    inline float distort (float x, float k)
    {
        x = std::clamp (x, -1.0f, 1.0f);
        constexpr float deg = 3.14159265f / 180.0f;
        return (3.0f + k) * x * 20.0f * deg / (3.14159265f + k * std::abs (x));
    }
} // namespace

const char* frameName (double pos)
{
    const int i = (int) std::lround (std::clamp (pos, 0.0, 1.0) * (kFrames - 1));
    return kFrameDefs[i].name;
}

float Core::Adsr::next() noexcept
{
    switch (stage)
    {
        case att:
            level += 1.0 / std::max (1.0, a * sr);
            if (level >= 1.0) { level = 1.0; stage = dec; }
            break;
        case dec:
            level -= (1.0 - s) / std::max (1.0, d * sr);
            if (level <= s) { level = s; stage = sus; }
            break;
        case sus: level = s; break;
        case rel:
            level -= std::max (level, 0.001) / std::max (1.0, r * sr) * 1.0;
            level -= 1.0 / std::max (1.0, r * sr) * 0.05; // finish cleanly
            if (level <= 0) { level = 0; stage = idle; }
            break;
        case idle: level = 0; break;
    }
    return (float) level;
}

Core::Core()
{
    // Build band-limited tables once: frame x octave
    tables.assign ((size_t) kFrames * kOctaves * (kTable + 1), 0.0f);
}

void Core::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (int f = 0; f < kFrames; ++f)
    {
        float scale = 1.0f;
        for (int o = 0; o < kOctaves; ++o)
        {
            float* tb = &tables[((size_t) f * kOctaves + o) * (kTable + 1)];
            const double maxF = 27.5 * std::pow (2.0, o + 1);
            const int maxH = std::clamp ((int) (0.45 * sr / maxF), 1, 16);
            for (int n = 0; n < kTable; ++n)
            {
                double s = 0;
                for (int h = 1; h <= maxH; ++h)
                    if (kFrameDefs[f].p[h - 1] != 0.0f) s += kFrameDefs[f].p[h - 1] * std::sin (kTwoPi * h * n / kTable);
                tb[n] = (float) s;
            }
            if (o == 0)
            {
                float peak = 0;
                for (int n = 0; n < kTable; ++n)
                    peak = std::max (peak, std::abs (tb[n]));
                scale = peak > 0 ? 1.0f / peak : 1.0f;
            }
            for (int n = 0; n < kTable; ++n)
                tb[n] *= scale;
            tb[kTable] = tb[0];
        }
    }
    for (auto& v : voices)
    {
        v.amp.sr = sr;
        v.filter.prepare (sr);
        v.ks.assign ((size_t) (sr / 15.0) + 4, 0.0f);
    }
    kaleido.prepare (sr, 0x41544d4fu);
    movement.prepare (sr, 0x5eedu);
    echo.prepare (sr);
    for (auto& r : reverbs)
        r.prepare (sr);
    reverbFadeLength = (int) (0.25 * sr);
    levelCoef = 1.0 - std::exp (-1.0 / (0.05 * sr));
    limRelease = (float) std::exp (-1.0 / (0.1 * sr));
    reset();
}

void Core::reset()
{
    for (auto& v : voices)
    {
        v.amp.stage = Adsr::idle;
        v.amp.level = 0;
        v.note = -1;
        v.keyDown = v.held = false;
        v.filter.reset();
    }
    for (int c = 0; c < 2; ++c)
    {
        loShelf[c].reset();
        hiShelf[c].reset();
    }
    kaleido.reset();
    movement.reset();
    echo.reset();
    for (auto& r : reverbs)
        r.reset();
    echoLevel = spaceLevel = 0;
    echoIdle = spaceIdle = true;
    reverbFade = 0;
    lastMovMode = -1;
    lastTilt = 1000.f;
    first = true;
    countdown = 0;
    limGain = 1.0f;
    bedGate = 0;
}

void Core::setPatch (const Patch& p)
{
    target = p;
    for (int i = 0; i < kNumParams; ++i)
        target.v[i] = std::clamp (target.v[i], paramInfo (i).min, paramInfo (i).max);
}

void Core::setParam (int i, double value)
{
    if (i >= 0 && i < kNumParams) target.v[i] = std::clamp (value, paramInfo (i).min, paramInfo (i).max);
}

int Core::activeVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        n += v.amp.active() ? 1 : 0;
    return n;
}

void Core::startPluck (Voice& v, double hz) noexcept
{
    v.ksLen = std::clamp ((int) std::lround (sr / std::max (hz, 16.0)), 2, (int) v.ks.size() - 1);
    v.ksPos = 0;
    v.ksLp = 0;
    const float bright = (float) cur[pluckBright];
    float lp = 0;
    for (int i = 0; i < v.ksLen; ++i)
    {
        const float w = rnd (v.rng);
        lp += (w - lp) * (0.1f + 0.9f * bright); // darker excitation when not bright
        v.ks[(size_t) i] = lp;
    }
}

void Core::noteOn (int note, float vel)
{
    Voice* pick = nullptr;
    for (auto& v : voices)
        if (! v.amp.active() && (pick == nullptr || v.age < pick->age)) pick = &v;
    if (pick == nullptr)
        for (auto& v : voices)
            if (! v.held && (pick == nullptr || v.age < pick->age)) pick = &v;
    if (pick == nullptr)
        for (auto& v : voices)
            if (pick == nullptr || v.age < pick->age) pick = &v;
    auto& v = *pick;
    v.note = note;
    v.vel = vel;
    v.keyDown = v.held = true;
    v.age = ++ageCounter;
    v.rng = 0x9e3779b9u ^ (uint32_t) (note * 7919 + ageCounter * 104729);
    for (auto& p : v.ph)
        p = (rnd (v.rng) + 1.0f) * 0.5f; // free-running phases avoid a flanged attack on unison
    v.phSub = v.phShim = 0;
    v.phMod[0] = v.phMod[1] = 0;
    v.fbPrev[0] = v.fbPrev[1] = 0;
    v.fenv = 1.0;
    v.amp.level = 0;
    v.amp.on();
    v.filter.reset();
    if ((int) std::lround (target[srcType]) == 3) startPluck (v, 440.0 * std::pow (2.0, (note - 69) / 12.0));
    resonancesDirty = true;
    movement.noteStarted();
}

void Core::noteOff (int note)
{
    for (auto& v : voices)
        if (v.note == note && v.keyDown)
        {
            v.keyDown = false;
            if (! sustainDown)
            {
                v.held = false;
                v.amp.off();
            }
        }
    resonancesDirty = true;
}

void Core::setSustain (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.held && ! v.keyDown)
            {
                v.held = false;
                v.amp.off();
            }
}

void Core::allNotesOff()
{
    for (auto& v : voices)
    {
        v.keyDown = v.held = false;
        v.amp.off();
    }
    sustainDown = false;
}

void Core::updateResonances() noexcept
{
    // Kaleidoscope's sympathetic bank rings at the held notes (and an octave below the lowest)
    double hz[osp::ReimaginedStage::resonators];
    int n = 0;
    int lowest = 200;
    for (auto& v : voices)
        if (v.held && v.note >= 0 && n < osp::ReimaginedStage::resonators)
        {
            hz[n++] = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
            lowest = std::min (lowest, v.note);
        }
    if (n > 0 && n < osp::ReimaginedStage::resonators) hz[n++] = 440.0 * std::pow (2.0, (lowest - 81) / 12.0);
    if (n > 0) kaleido.setResonances (hz, n); // keep the last chord ringing after release
    resonancesDirty = false;
}

int Core::octaveFor (double hz) const noexcept { return std::clamp ((int) std::floor (std::log2 (std::max (hz, 1.0) / 27.5)), 0, kOctaves - 1); }

float Core::frameSample (int o, double frame, double phase) const noexcept
{
    const double x = std::clamp (frame, 0.0, 1.0) * (kFrames - 1);
    const int f = std::min (kFrames - 2, (int) x);
    const float t = (float) (x - f);
    const double idx = phase * kTable;
    const int i = (int) idx;
    const float fr = (float) (idx - i);
    const float* a = &tables[((size_t) f * kOctaves + o) * (kTable + 1)];
    const float* b = &tables[((size_t) (f + 1) * kOctaves + o) * (kTable + 1)];
    const float va = a[i] + fr * (a[i + 1] - a[i]);
    const float vb = b[i] + fr * (b[i + 1] - b[i]);
    return va + t * (vb - va);
}

void Core::controlTick() noexcept
{
    const double a = first ? 1.0 : 1.0 - std::exp (-kControl / (0.06 * sr));
    for (int i = 0; i < kNumParams; ++i)
    {
        if (first || paramInfo (i).scale == Scale::choice) cur.v[i] = target.v[i];
        else if (paramInfo (i).scale == Scale::log) cur.v[i] = std::exp (std::log (cur.v[i]) + (std::log (target.v[i]) - std::log (cur.v[i])) * a);
        else cur.v[i] += (target.v[i] - cur.v[i]) * a;
    }
    first = false;

    const double rel = cur[release];
    for (auto& v : voices)
        v.amp.set (cur[attack], cur[decay], cur[sustain], rel);

    // Tilt
    if (std::abs ((float) cur[tilt] - lastTilt) > 0.05f)
    {
        lastTilt = (float) cur[tilt];
        for (int c = 0; c < 2; ++c)
        {
            hiShelf[c].setup (osp::ShelfFilter::Type::high, sr, 2500.0, lastTilt);
            loShelf[c].setup (osp::ShelfFilter::Type::low, sr, 220.0, -0.35 * lastTilt);
        }
    }

    // Kaleidoscope
    if (resonancesDirty) updateResonances();
    kaleido.setAmount (cur[kalAmount]);
    kaleido.setShape (cur[kalFocus], cur[kalSpread]);
    kaleido.update();

    // Movement: map the three generic knobs onto the mode's own settings
    osp::Shaping sh;
    const int mode = (int) std::lround (cur[movMode]);
    const double A = cur[movA], B = cur[movB], C = cur[movC];
    switch (mode)
    {
        case 1: sh.movementMode = osp::MovementMode::tape; sh.tapeWow = A; sh.tapeFlutter = B; sh.tapeWear = C; break;
        case 2: sh.movementMode = osp::MovementMode::chorus; sh.chorusRate = A; sh.chorusWidth = B; sh.chorusStereo = C; break;
        case 3: sh.movementMode = osp::MovementMode::pulse; sh.pulseRate = A; sh.pulseShape = B; sh.pulseStereo = C; break;
        case 4:
            sh.movementMode = osp::MovementMode::shaper;
            sh.shaper.pattern = std::clamp ((int) std::lround (A * 11), 0, 11);
            sh.shaper.rate = (osp::ShaperRate) std::clamp ((int) std::lround (B * 5), 0, 5);
            sh.shaper.smooth = C;
            break;
        default: sh.movementMode = osp::MovementMode::drift; break;
    }
    movement.setTargets (sh, mode == 0 ? 0.0 : cur[movAmount]);
    bool anyHeld = false;
    for (auto& v : voices)
        anyHeld |= v.amp.active();
    movement.setVoicesActive (anyHeld);

    // Echo
    osp::EchoDelay::Settings es;
    es.type = (int) std::lround (cur[echoType]) == 1 ? osp::EchoType::bbd : osp::EchoType::tape;
    es.sync = false;
    es.timeMs = cur[echoTime];
    es.feedback = cur[echoFeedback];
    es.tone = cur[echoTone];
    es.age = cur[echoAge];
    es.stereo = (osp::EchoStereo) std::clamp ((int) std::lround (cur[echoStereo]), 0, 2);
    echo.setSettings (es);

    // Space: type/size changes crossfade to a freshly configured reverb (as OSP's PostProcessor)
    osp::SpaceReverb::Settings ss;
    ss.type = (osp::SpaceType) std::clamp ((int) std::lround (cur[spaceType]), 0, 3);
    double dLo = 0.2, dHi = 12.0;
    osp::shaping::decayRange (ss.type, dLo, dHi);
    ss.decaySeconds = std::clamp (cur[spaceDecay], dLo, dHi);
    ss.preDelayMs = cur[spacePreDelay];
    ss.size = std::round (cur[spaceSize] * 20.0) / 20.0; // settle drags into steps
    ss.damping = cur[spaceDamping];
    ss.modulation = cur[spaceMod];
    ss.width = 1.0;
    ss.lowCutHz = 100.0;
    ss.highCutHz = 12000.0;
    if (ss.structurallyDifferent (appliedSpace) && reverbFade == 0)
    {
        appliedSpace = ss;
        activeReverb = 1 - activeReverb;
        reverbs[activeReverb].configure (ss);
        reverbs[activeReverb].reset();
        reverbFade = reverbFadeLength;
    }
    else if (! ss.structurallyDifferent (appliedSpace))
    {
        appliedSpace = ss;
        reverbs[activeReverb].tune (ss);
    }

    // Per-voice filter: CHARACTER with its envelope
    const auto ft = (osp::FilterType) std::clamp ((int) std::lround (cur[filtType]), 0, 4);
    const double envDecayCoef = std::exp (-kControl / (std::max (0.01, cur[filtEnvDecay]) * sr));
    for (auto& v : voices)
    {
        if (! v.amp.active()) continue;
        const double keyTrack = std::pow (2.0, (v.note - 60) / 24.0); // half key tracking
        const double hz = std::clamp (cur[cutoff] * keyTrack * std::pow (2.0, 5.0 * cur[filtEnv] * v.fenv * (0.5 + 0.5 * v.vel)), 20.0, 0.45 * sr);
        v.filter.setParameters (ft, hz, cur[resonance], cur[filtDrive], ft == osp::FilterType::tilt ? cur[tilt] : 0.0);
        v.fenv *= envDecayCoef;
    }
}

void Core::renderVoices (float* L, float* R, int n) noexcept
{
    const int type = (int) std::lround (cur[srcType]);
    const double spreadUp = std::pow (2.0, cur[srcSpread] / 2400.0);
    const double subG = cur[srcSub] * 0.45, shimG = cur[srcShimmer] * 0.22, breathG = cur[srcBreath] * 0.35;
    const double frame = cur[srcFrame];
    const int sawN = std::clamp ((int) std::lround (cur[sawVoices]), 1, 7);
    const double vibD = cur[vibDepth] * 0.5; // semitones peak
    const double vibInc = cur[vibRate] / sr;
    const double driftCents = cur[glide] * 18.0;
    const double ratio = cur[fmRatio], index = cur[fmIndex], fb = cur[fmFeedback];
    const float ksDamp = (float) (0.996 - 0.03 * cur[pluckDamp]);

    for (auto& v : voices)
    {
        if (! v.amp.active()) continue;
        // Slow random pitch drift (a new target every ~0.4 s)
        if (--v.driftCount <= 0)
        {
            v.driftCount = (int) (0.4 * sr / kControl);
            v.driftTarget = rnd (v.rng) * driftCents;
        }
        v.driftPos += (v.driftTarget - v.driftPos) * 0.02;
        const double baseSemis = v.note - 69 + bendSemis + v.driftPos / 100.0;
        for (int i = 0; i < n; ++i)
        {
            const double vib = vibD * std::sin (kTwoPi * v.vibPh);
            v.vibPh += vibInc;
            v.vibPh -= std::floor (v.vibPh);
            const double hz = 440.0 * std::pow (2.0, (baseSemis + vib) / 12.0);
            const float env = v.amp.next() * v.vel;
            float l = 0, r = 0;
            switch (type)
            {
                case 0: // wavetable, two detuned layers panned apart
                {
                    const double fa = hz * spreadUp, fb2 = hz / spreadUp;
                    const float a = frameSample (octaveFor (fa), frame, v.ph[0]);
                    const float b = frameSample (octaveFor (fb2), frame, v.ph[1]);
                    v.ph[0] += fa / sr; v.ph[0] -= std::floor (v.ph[0]);
                    v.ph[1] += fb2 / sr; v.ph[1] -= std::floor (v.ph[1]);
                    l = 0.7f * a + 0.3f * b;
                    r = 0.3f * a + 0.7f * b;
                    break;
                }
                case 1: // FM: modulator (with feedback) into carrier, two layers
                {
                    for (int k = 0; k < 2; ++k)
                    {
                        const double f = k == 0 ? hz * spreadUp : hz / spreadUp;
                        const double m = std::sin (kTwoPi * v.phMod[k] + fb * 2.5 * v.fbPrev[k]);
                        v.fbPrev[k] = m;
                        const float c = (float) std::sin (kTwoPi * v.ph[k] + index * m * (0.4 + 0.6 * v.fenv));
                        v.phMod[k] += f * ratio / sr; v.phMod[k] -= std::floor (v.phMod[k]);
                        v.ph[k] += f / sr; v.ph[k] -= std::floor (v.ph[k]);
                        (k == 0 ? l : r) += c * 0.85f;
                        (k == 0 ? r : l) += c * 0.15f;
                    }
                    break;
                }
                case 2: // supersaw from the band-limited String frame
                {
                    const double det = cur[sawDetune];
                    for (int k = 0; k < sawN; ++k)
                    {
                        const double off = sawN == 1 ? 0.0 : (k / (double) (sawN - 1) - 0.5) * 2.0;
                        const double f = hz * std::pow (2.0, off * det / 1200.0);
                        const float s = frameSample (octaveFor (f), 5.0 / (kFrames - 1), v.ph[k]);
                        v.ph[k] += f / sr; v.ph[k] -= std::floor (v.ph[k]);
                        const float pan = (float) (0.5 + 0.45 * off);
                        l += s * (1.0f - pan);
                        r += s * pan;
                    }
                    const float g = 1.4f / std::sqrt ((float) sawN);
                    l *= g;
                    r *= g;
                    break;
                }
                default: // pluck (Karplus-Strong with a one-pole damper)
                {
                    if (v.ksLen > 1)
                    {
                        const int p = v.ksPos;
                        const int q = (p + 1) % v.ksLen;
                        const float y = v.ks[(size_t) p];
                        v.ksLp += ((y + v.ks[(size_t) q]) * 0.5f - v.ksLp) * (0.35f + 0.6f * (float) cur[pluckBright]);
                        v.ks[(size_t) p] = v.ksLp * ksDamp;
                        v.ksPos = q;
                        l = r = y * 5.0f;
                    }
                    break;
                }
            }
            // Sub octave, octave shimmer and breath
            const float sub = (float) (std::sin (kTwoPi * v.phSub) * subG);
            v.phSub += 0.5 * hz / sr; v.phSub -= std::floor (v.phSub);
            const float shim = frameSample (octaveFor (hz * 2), 0.0, v.phShim) * (float) shimG;
            v.phShim += 2.0 * hz / sr; v.phShim -= std::floor (v.phShim);
            const float br = rnd (v.rng) * (float) breathG * (0.4f + 0.6f * (float) v.fenv);
            l = (l + sub + shim + br) * env * 0.32f;
            r = (r + sub + shim + br) * env * 0.32f;
            v.filter.process (l, r);
            L[i] += l;
            R[i] += r;
        }
        if (! v.amp.active()) v.note = -1;
    }
}

void Core::process (float* L, float* R, int n) noexcept
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    int pos = 0;
    while (pos < n)
    {
        if (countdown <= 0)
        {
            controlTick();
            countdown = kControl;
        }
        const int len = std::min (n - pos, countdown);
        float* l = L + pos;
        float* r = R + pos;
        renderVoices (l, r, len);

        const float k = (float) (cur[drive] * 90.0);
        const double dw = std::clamp (cur[drive] * 1.3, 0.0, 1.0);
        const float dDry = (float) std::cos (dw * 1.5707963), dWet = (float) std::sin (dw * 1.5707963);
        const int bits = (int) std::lround (cur[crushBits]);
        const float steps = (float) std::pow (2.0, bits - 1);
        const double cw = bits < 16 ? std::clamp ((16.0 - bits) / 8.0, 0.0, 1.0) : 0.0;
        const float cDry = (float) std::cos (cw * 1.5707963), cWet = (float) std::sin (cw * 1.5707963);
        const double holdInc = 1.0 - 0.94 * std::pow (cur[crushRate], 0.5); // 1 = every sample
        bool anyHeld = false;
        for (auto& v : voices)
            anyHeld |= v.held;
        const float gateA = (float) (1.0 - std::exp (-1.0 / (sr * (anyHeld ? 0.05 + cur[attack] / 3 : std::max (0.05, cur[release] / 3)))));
        const float bedG = (float) (cur[bedLevel] * 0.3);
        const float bedA = (float) (1.0 - std::exp (-kTwoPi * cur[bedColour] / sr));
        const float outG = (float) std::pow (10.0, cur[outGain] / 20.0);
        const double echoTarget = std::pow (cur[echoSend], 1.4);
        const double spaceTarget = 1.25 * std::pow (cur[spaceSend], 1.2);

        for (int i = 0; i < len; ++i)
        {
            float x[2] = { l[i], r[i] };
            // Sample-rate crush: hold samples
            crushPhase += holdInc;
            const bool take = crushPhase >= 1.0;
            if (take) crushPhase -= 1.0;
            for (int c = 0; c < 2; ++c)
            {
                float s = x[c];
                if (dw > 0.0005) s = s * dDry + distort (s, k) * dWet;
                if (holdInc < 0.999)
                {
                    if (take) crushHold[c] = s;
                    s = crushHold[c];
                }
                if (cw > 0.0005) s = s * cDry + (std::round (std::clamp (s, -1.0f, 1.0f) * steps) / steps) * cWet;
                s = loShelf[c].process (s);
                s = hiShelf[c].process (s);
                x[c] = s;
            }
            kaleido.process (x[0], x[1]);
            movement.process (x[0], x[1]);

            // Weather bed: pink noise, low-passed, gated by playing, with slow gusts
            const float w = rnd (bedRng);
            pink[0] = 0.99886f * pink[0] + w * 0.0555179f;
            pink[1] = 0.99332f * pink[1] + w * 0.0750759f;
            pink[2] = 0.96900f * pink[2] + w * 0.1538520f;
            pink[3] = 0.86650f * pink[3] + w * 0.3104856f;
            pink[4] = 0.55000f * pink[4] + w * 0.5329522f;
            pink[5] = -0.7616f * pink[5] - w * 0.0168980f;
            float pn = (pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362f) * 0.11f;
            pink[6] = w * 0.115926f;
            bedLp1 += (pn - bedLp1) * bedA;
            bedLp2 += (bedLp1 - bedLp2) * bedA;
            bedGate += ((anyHeld ? 1.0f : 0.0f) - bedGate) * gateA;
            gustPh += (float) (0.17 / sr);
            if (gustPh > 1) gustPh -= 1;
            const float bed = (pn - bedLp2 * 0.6f) * bedGate * bedG * (0.65f + 0.35f * std::sin (6.2831853f * gustPh));
            const float sl = x[0] + bed, sr2 = x[1] + bed;

            // Echo send
            echoLevel += (echoTarget - echoLevel) * levelCoef;
            float el = 0, er = 0;
            if (echoLevel > 1.0e-5)
            {
                if (echoIdle)
                {
                    echoIdle = false;
                    echo.reset();
                }
                echo.process (sl, sr2, el, er);
            }
            else
                echoIdle = true;

            // Space send
            spaceLevel += (spaceTarget - spaceLevel) * levelCoef;
            float ol = x[0], orr = x[1];
            if (spaceLevel > 1.0e-5 || reverbFade > 0)
            {
                if (spaceIdle)
                {
                    spaceIdle = false;
                    for (auto& rv : reverbs)
                        rv.reset();
                }
                float wl, wr;
                reverbs[activeReverb].process (sl, sr2, wl, wr);
                if (reverbFade > 0)
                {
                    float pl, pr;
                    reverbs[1 - activeReverb].process (sl, sr2, pl, pr);
                    const float f = (float) reverbFade / (float) reverbFadeLength;
                    wl += f * (pl - wl);
                    wr += f * (pr - wr);
                    --reverbFade;
                }
                const float wet = (float) spaceLevel, dry = (float) (1.0 - 0.2 * std::min (1.0, spaceLevel));
                ol = dry * ol + wet * wl;
                orr = dry * orr + wet * wr;
            }
            else
                spaceIdle = true;
            ol += (float) echoLevel * el + bed * 0.5f;
            orr += (float) echoLevel * er + bed * 0.5f;

            // Output and safety limiter (-1 dBFS)
            ol *= outG;
            orr *= outG;
            constexpr float ceiling = 0.891f;
            const float peak = std::max (std::abs (ol), std::abs (orr));
            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            limGain = need < limGain ? need : need - (need - limGain) * limRelease;
            l[i] = std::clamp (ol * limGain, -ceiling, ceiling);
            r[i] = std::clamp (orr * limGain, -ceiling, ceiling);
        }
        pos += len;
        countdown -= len;
    }
}
} // namespace atmos
