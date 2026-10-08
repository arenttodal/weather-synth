#include "Core.h"
#include "engine/Shaping.h"
#include <algorithm>
#include <cmath>

namespace atmos
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kPi = 3.141592653589793;

    inline float rnd (uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((s >> 8) & 0xffffff) / 8388608.0f - 1.0f;
    }
    // Polynomial band-limited step: removes most aliasing from saw, pulse and sub edges
    inline double blep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
    inline double saw (double ph, double dt) noexcept { return 2.0 * ph - 1.0 - blep (ph, dt); }
    inline double pulse (double ph, double dt, double pw) noexcept
    {
        double y = ph < pw ? 1.0 : -1.0;
        y += blep (ph, dt);
        double t2 = ph + 1.0 - pw;
        t2 -= std::floor (t2);
        y -= blep (t2, dt);
        return y - (2.0 * pw - 1.0); // remove DC so width changes don't thump
    }
    inline double tri (double ph) noexcept { return 1.0 - 4.0 * std::abs (ph - 0.5); }
    inline double softClip (double x) noexcept { return x / std::sqrt (1.0 + x * x); }
    constexpr double kIntervals[5] = { -12, 0, 7, 12, 24 };
} // namespace

void Core::Env::set (double sr, double A, double D, double S, double R, double scale)
{
    // Attack charges towards 1.3 and stops at 1 (the curve of an analogue envelope);
    // decay and release fall exponentially, reaching about -26 dB at the set time.
    aC = 1.0 - std::exp (-1.466 / std::max (1.0, A * scale * sr));
    dC = 1.0 - std::exp (-3.0 / std::max (1.0, D * scale * sr));
    rC = 1.0 - std::exp (-3.0 / std::max (1.0, R * scale * sr));
    s = S;
}

double Core::Env::next() noexcept
{
    switch (stage)
    {
        case att:
            level += (1.3 - level) * aC;
            if (level >= 1.0)
            {
                level = 1.0;
                stage = dec;
            }
            break;
        case dec: level += (s - level) * dC; break;
        case rel:
            level -= level * rC;
            if (level < 1.0e-4)
            {
                level = 0;
                stage = idle;
            }
            break;
        case idle: level = 0; break;
    }
    return level;
}

Core::Core() = default;

void Core::prepare (double sampleRate, int)
{
    sr = sampleRate;
    // Each voice card is slightly different, as in a real polysynth
    uint32_t card = 0x50524f50u;
    for (auto& v : voices)
    {
        v.cardCents = rnd (card);
        v.cardCutoff = rnd (card);
        v.cardEnv = rnd (card);
        v.rng = card ^ 0x9e3779b9u;
        v.phA = (rnd (card) + 1.0f) * 0.5f;
        v.phB = (rnd (card) + 1.0f) * 0.5f;
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
        v.amp.stage = v.fenv.stage = Env::idle;
        v.amp.level = v.fenv.level = 0;
        v.note = -1;
        v.keyDown = v.held = false;
        v.s1 = v.s2 = v.s3 = v.s4 = 0;
        v.noiseLp = 0;
    }
    for (int c = 0; c < 2; ++c)
    {
        loShelf[c].reset();
        hiShelf[c].reset();
        dcX[c] = dcY[c] = 0;
    }
    kaleido.reset();
    movement.reset();
    echo.reset();
    for (auto& r : reverbs)
        r.reset();
    echoLevel = spaceLevel = 0;
    echoIdle = spaceIdle = true;
    reverbFade = 0;
    lastTilt = 1000.f;
    first = true;
    countdown = 0;
    limGain = 1.0f;
    bedGate = 0;
    lastNote = -1;
    lfoPh = lfoValue = lfoHeld = 0;
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

void Core::startVoice (Voice& v, int note, float vel, double stackCents, double pan) noexcept
{
    const bool glide = target[portamento] > 0.001 && lastNote >= 0;
    if (! glide || ! v.amp.active()) v.pitch = glide ? lastNote : note;
    if (! glide) v.pitch = note;
    v.note = note;
    v.vel = vel;
    v.keyDown = v.held = true;
    v.age = ++ageCounter;
    v.stackCents = stackCents;
    v.pan = pan;
    // Oscillators run free and the envelopes restart from where they are, as on the hardware
    v.amp.on();
    v.fenv.on();
}

void Core::noteOn (int note, float vel)
{
    const int mode = (int) std::lround (target[voiceMode]);
    const int stack = mode == 2 ? 4 : mode == 1 ? 2 : 1;
    for (int k = 0; k < stack; ++k)
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
        const double off = stack == 1 ? 0.0 : (k / (double) (stack - 1) - 0.5) * 2.0;
        const int idx = (int) (pick - voices.data());
        const double pan = stack == 1 ? ((idx % 2) ? 0.18 : -0.18) * (0.5 + 0.5 * target[slop]) : off * 0.75;
        startVoice (*pick, note, vel, off * target[stackDetune], pan);
    }
    lastNote = note;
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
                v.fenv.off();
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
                v.fenv.off();
            }
}

void Core::allNotesOff()
{
    for (auto& v : voices)
    {
        v.keyDown = v.held = false;
        v.amp.off();
        v.fenv.off();
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
            bool dup = false;
            for (int i = 0; i < n; ++i)
                dup |= std::abs (hz[i] - 440.0 * std::pow (2.0, (v.note - 69) / 12.0)) < 0.01;
            if (dup) continue;
            hz[n++] = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
            lowest = std::min (lowest, v.note);
        }
    if (n > 0 && n < osp::ReimaginedStage::resonators) hz[n++] = 440.0 * std::pow (2.0, (lowest - 81) / 12.0);
    if (n > 0) kaleido.setResonances (hz, n); // keep the last chord ringing after release
    resonancesDirty = false;
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

    const double sl = cur[slop];
    for (auto& v : voices)
    {
        const double scale = 1.0 + 0.15 * sl * v.cardEnv;
        v.amp.set (sr, cur[attack], cur[decay], cur[sustain], cur[release], scale);
        v.fenv.set (sr, cur[fAttack], cur[fDecay], cur[fSustain], cur[fRelease], scale);
        // Slow wandering pitch (a new target every ~0.5 s)
        if (--v.driftCount <= 0)
        {
            v.driftCount = (int) ((0.3 + 0.4 * (rnd (v.rng) + 1.0f)) * sr / kControl);
            v.driftTarget = rnd (v.rng) * 6.0 * sl;
        }
        v.drift += (v.driftTarget - v.drift) * 0.01;
    }

    // Global LFO
    lfoPh += cur[lfoRate] * kControl / sr;
    if (lfoPh >= 1.0)
    {
        lfoPh -= std::floor (lfoPh);
        lfoHeld = rnd (lfoRng);
    }
    switch ((int) std::lround (cur[lfoShape]))
    {
        case 1: lfoValue = 2.0 * lfoPh - 1.0; break;
        case 2: lfoValue = lfoPh < 0.5 ? 1.0 : -1.0; break;
        case 3: lfoValue += (lfoHeld - lfoValue) * 0.3; break;
        default: lfoValue = 1.0 - 4.0 * std::abs (lfoPh - 0.5); break;
    }

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

}

void Core::renderVoices (float* L, float* R, int n) noexcept
{
    const double sl = cur[slop];
    const double aw = cur[oscAWave], bw = cur[oscBWave];
    const double pw = std::clamp (cur[oscAPw] + cur[lfoPwm] * 0.42 * lfoValue, 0.04, 0.96);
    const double interval = kIntervals[std::clamp ((int) std::lround (cur[oscBInterval]), 0, 4)] + cur[oscBDetune] / 100.0;
    const bool sync = cur[oscSync] > 0.5;
    const double gA = cur[mixA], gB = cur[mixB], gSub = cur[mixSub] * 0.9, gNoise = cur[mixNoise] * 1.3;
    const double vib = cur[vibDepth] * lfoValue; // semitones
    const double pmA = cur[pmEnvA] * 24.0, pmB = cur[pmOscB] * 3.0;
    const double glideT = 0.004 + 0.9 * cur[portamento] * cur[portamento];
    const double glideC = cur[portamento] > 0.001 ? 1.0 - std::exp (-1.0 / (glideT * sr)) : 1.0;
    const int ft = std::clamp ((int) std::lround (cur[filtType]), 0, 3);
    const double res = cur[resonance];
    const double kLad = 4.1 * res;
    const double kSvf = 2.0 - 1.97 * res;
    const double inGain = 0.6 + 3.2 * cur[filtDrive] * cur[filtDrive] + 0.6 * cur[filtDrive];
    const double lfoOct = cur[lfoFilter] * 2.0 * lfoValue;
    const double envAmt = cur[filtEnv] * 7.0;
    const double nyq = 0.45 * sr;
    const int mode = (int) std::lround (cur[voiceMode]);
    const double stackGain = mode == 2 ? 0.5 : mode == 1 ? 0.72 : 1.0;
    const double outTrim = 0.42 / (1.0 + 0.9 * cur[filtDrive]); // a driven filter is denser, not louder

    for (auto& v : voices)
    {
        if (! v.amp.active()) continue;
        const double velF = 1.0 - cur[filtVel] + cur[filtVel] * v.vel;
        const double velA = (1.0 - cur[ampVel] + cur[ampVel] * v.vel) * stackGain;
        const double cents = (v.cardCents * 7.0 * sl + v.drift + v.stackCents) / 100.0;
        const double cutOff = cur[cutoff] * std::pow (2.0, v.cardCutoff * 0.35 * sl + lfoOct);
        const double gl = std::sqrt (1.0 - v.pan), gr = std::sqrt (1.0 + v.pan);
        for (int i = 0; i < n; ++i)
        {
            v.pitch += (v.note - v.pitch) * glideC;
            const double fe = v.fenv.next();
            const double base = v.pitch - 69.0 + bendSemis + cents + vib;
            const double hzB = 440.0 * std::exp2 ((base + interval) / 12.0);
            const double hzA = 440.0 * std::exp2 ((base + pmA * fe) / 12.0);
            const double dtA = std::min (hzA / sr, 0.45), dtB = std::min (hzB / sr, 0.45);

            // Oscillator B (also the sync master)
            double b;
            if (bw < 1.0) b = (1.0 - bw) * tri (v.phB) + bw * saw (v.phB, dtB);
            else b = (2.0 - bw) * saw (v.phB, dtB) + (bw - 1.0) * pulse (v.phB, dtB, 0.5);
            v.phB += dtB;
            bool wrapped = false;
            if (v.phB >= 1.0)
            {
                v.phB -= 1.0;
                wrapped = true;
            }

            // Oscillator A, saw blended into pulse
            const double a = (1.0 - aw) * saw (v.phA, dtA) + aw * pulse (v.phA, dtA, pw);
            const double sub = pulse (v.phSub, dtA * 0.5, 0.5);
            v.phA += dtA;
            v.phSub += dtA * 0.5;
            if (sync && wrapped) v.phA = v.phB / dtB * dtA; // hard sync: restart A where B wrapped
            if (v.phA >= 1.0) v.phA -= std::floor (v.phA);
            if (v.phSub >= 1.0) v.phSub -= 1.0;

            const float w = rnd (v.rng);
            v.noiseLp += (w - v.noiseLp) * 0.35f;
            double x = (gA * a + gB * b + gSub * sub + gNoise * v.noiseLp) * 0.5 * inGain;

            // Filter: cutoff from key, envelope, LFO and audio-rate osc B (poly-mod)
            const double oct = cur[keyTrack] * (v.pitch - 60.0) / 12.0 + envAmt * fe * velF + pmB * b;
            const double fc = std::clamp (cutOff * std::exp2 (oct), 16.0, nyq);
            const double g = std::tan (kPi * fc / sr);
            double y;
            if (ft <= 1)
            {
                // Transistor ladder (zero-delay feedback), saturating at its input
                const double G = g / (1.0 + g), inv = 1.0 / (1.0 + g);
                const double sig = G * G * G * v.s1 * inv + G * G * v.s2 * inv + G * v.s3 * inv + v.s4 * inv;
                const double G4 = G * G * G * G;
                double u;
                if (ft == 0) u = std::tanh ((x - kLad * sig) / (1.0 + kLad * G4)); // Moog: thick, loses bass as it resonates
                else u = softClip ((x * (1.0 + 0.6 * kLad) - kLad * sig) / (1.0 + kLad * G4)); // Prophet: cleaner, bass kept
                double t = (u - v.s1) * G;
                double y1 = t + v.s1;
                v.s1 = y1 + t;
                t = (y1 - v.s2) * G;
                double y2 = t + v.s2;
                v.s2 = y2 + t;
                t = (y2 - v.s3) * G;
                double y3 = t + v.s3;
                v.s3 = y3 + t;
                t = (y3 - v.s4) * G;
                y = t + v.s4;
                v.s4 = y + t;
                if (ft == 0) y *= 1.0 + 0.45 * kLad;
            }
            else
            {
                // State-variable 2-pole (Oberheim SEM), gentler and more open
                const double xi = std::tanh (x);
                const double a1 = 1.0 / (1.0 + g * (g + kSvf)), a2 = g * a1, a3 = g * a2;
                const double v3 = xi - v.s2;
                const double v1 = a1 * v.s1 + a2 * v3;
                const double v2 = v.s2 + a2 * v.s1 + a3 * v3;
                v.s1 = 2.0 * v1 - v.s1;
                v.s2 = 2.0 * v2 - v.s2;
                y = ft == 2 ? v2 : v1 * (2.8 - 1.4 * res);
            }
            const double out = y * v.amp.next() * velA * outTrim;
            L[i] += (float) (out * gl);
            R[i] += (float) (out * gr);
        }
        if (! v.amp.active())
        {
            v.note = -1;
            v.s1 = v.s2 = v.s3 = v.s4 = 0;
        }
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

        // Saturation: an asymmetric tube/tape curve (even harmonics), normalised so moderate levels stay put
        const double dv = cur[drive];
        const float k = (float) (1.0 + dv * dv * 22.0 + dv * 3.0);
        const float bias = (float) (0.2 * dv);
        const float tb = std::tanh (k * bias);
        const float norm = (float) (0.5 / std::tanh (k * 0.5) / (1.0 + 1.1 * dv * dv)); // louder edge, not louder overall
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
                if (dv > 0.0005)
                {
                    s = (std::tanh (k * (s + bias)) - tb) * norm;
                    // DC blocker for the asymmetry
                    const float yv = s - dcX[c] + 0.9995f * dcY[c];
                    dcX[c] = s;
                    dcY[c] = yv;
                    s = yv;
                }
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
