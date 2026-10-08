#include "Bible.h"
#include <algorithm>
#include <cmath>

namespace atmos
{
namespace
{
    double clamp1 (double x) { return std::clamp (x, -1.0, 1.0); }
    uint32_t mix (uint32_t a, uint32_t b)
    {
        uint32_t h = a ^ (b * 0x9e3779b9u);
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }
    double unit (uint32_t h) { return (h & 0xffffff) / 16777216.0; } // 0..1
} // namespace

void natureLean (const Climate& c, double precip, double intensity, double u[kNumParams])
{
    const Forces f = forces (c, 1.0);
    const double E = f.energy - 0.5, H = f.heat, K = f.cold, W = f.wet - 0.5, D = f.drench, Hu = f.humid - 0.5;
    const double M = f.motion - 0.3, G = f.gale, Li = f.light - 0.5, Lo = f.low, Fu = f.fullness, Dk = f.dark;
    const double Pr = precip;
    for (int i = 0; i < kNumParams; ++i)
        u[i] = 0.0;

    // Oscillators: heat and humidity loosen the tuning; rain and wind add noise; low pressure adds weight
    u[oscAPw] = -0.8 * H + 0.5 * M;
    u[oscBDetune] = 1.2 * Hu + 1.0 * H;
    u[mixSub] = 1.5 * Lo + 0.6 * (1 - Fu) * Dk - 0.3;
    u[mixNoise] = 1.4 * (Pr - 0.2) + 0.8 * G + 0.4 * W;
    u[slop] = 1.6 * H + 0.8 * Hu - 0.6 * K;
    u[stackDetune] = 1.0 * H + 0.8 * Hu;
    u[portamento] = 0.8 * Hu + 0.4 * M;

    // Filter: light opens it, energy snaps the envelope, heat drives it, wind makes it ring
    u[cutoff] = 1.2 * Li + 0.8 * E - 0.6 * W;
    u[resonance] = 1.0 * G + 0.6 * K;
    u[filtDrive] = 1.8 * H + 0.6 * E;
    u[filtEnv] = 0.8 * E + 0.6 * H;
    u[fAttack] = -1.2 * E + 0.8 * K;
    u[fDecay] = 1.0 * W - 0.8 * H;
    u[fSustain] = 0.4 * W;
    u[fRelease] = 1.0 * W + 0.6 * K;

    // Amp: heat is fast and short, cold and wet are slow and long
    u[attack] = -1.6 * E + 0.8 * K - 1.0 * H;
    u[decay] = 1.0 * W;
    u[sustain] = -0.8 * H - 0.6 * G + 0.4 * W;
    u[release] = 1.4 * W + 0.8 * K - 0.8 * H;

    // Modulation: wind and motion wobble; heat makes the poly-mod growl
    u[lfoRate] = 1.2 * E + 0.8 * M + 0.8 * H;
    u[vibDepth] = 1.4 * M + 0.8 * G;
    u[lfoPwm] = 1.0 * M + 0.6 * Hu;
    u[lfoFilter] = 1.0 * G + 0.6 * M;
    u[pmEnvA] = 1.0 * H;
    u[pmOscB] = 1.4 * H + 0.6 * G;

    // Colour: 40 °C saturates and breaks apart
    u[drive] = 2.0 * H + 0.6 * E;
    u[crushBits] = -2.0 * H;
    u[crushRate] = 2.0 * H;
    u[tilt] = 1.6 * Li;

    u[kalAmount] = 1.2 * Hu + 0.8 * D + 0.6 * Fu * Dk;
    u[kalFocus] = 0.8 * (Fu - 0.5);
    u[kalSpread] = 1.0 * M;

    u[movAmount] = 1.4 * M + 1.0 * G + 0.8 * H;
    u[movA] = 1.2 * E + 0.8 * G;
    u[movB] = 0.8 * M;

    u[echoSend] = 1.6 * (Pr - 0.2) + 0.8 * D;
    u[echoTime] = 0.8 * Hu - 0.6 * H;
    u[echoFeedback] = 1.2 * W + 0.6 * D;
    u[echoTone] = 0.8 * Li;
    u[echoAge] = 1.0 * H + 0.6 * Hu;

    u[spaceSend] = 1.4 * W + 1.0 * D + 0.5 * K;
    u[spaceDecay] = 1.2 * W + 0.8 * K + 0.8 * D;
    u[spaceSize] = 0.8 * Hu + 0.6 * K;
    u[spaceDamping] = -0.8 * Li + 0.6 * Hu;
    u[spaceMod] = 1.0 * M;
    u[spacePreDelay] = -0.8 * Hu;

    u[bedLevel] = 1.6 * (Pr - 0.1) + 1.0 * G;
    u[bedColour] = 1.0 * (Pr - 0.4) - (c.temp < 1 ? 0.8 : 0.0);

    // Intensity scales the weather's pull (0 = tamed .. 1.6 = exaggerated)
    for (int i = 0; i < kNumParams; ++i)
        u[i] *= intensity;
}

void macroLean (const Macros& m, double u[kNumParams])
{
    for (int i = 0; i < kNumParams; ++i)
        u[i] = 0.0;
    u[cutoff] = 1.5 * m.tone;
    u[tilt] = 0.8 * m.tone;
    u[filtEnv] = 0.5 * m.tone;
    u[attack] = 1.5 * m.bloom;
    u[release] = 1.5 * m.bloom;
    u[sustain] = 0.8 * m.bloom;
    u[fAttack] = 1.2 * m.bloom;
    u[fRelease] = 1.0 * m.bloom;
    u[spaceSend] = 1.5 * m.space;
    u[echoSend] = 1.0 * m.space;
    u[spaceDecay] = 1.0 * m.space;
    u[movAmount] = 1.5 * m.motion;
    u[vibDepth] = 1.2 * m.motion;
    u[lfoPwm] = 1.0 * m.motion;
    u[lfoFilter] = 0.8 * m.motion;
    u[kalSpread] = 0.8 * m.motion;
}

double placeInBounds (int p, double home, double lo, double hi, double u)
{
    const auto& info = paramInfo (p);
    if (info.scale == Scale::choice) return home; // a core sound's types are its identity
    lo = std::min (lo, home);
    hi = std::max (hi, home);
    u = clamp1 (u);
    if (info.scale == Scale::log && lo > 0 && home > 0)
    {
        const double lh = std::log (home);
        return std::exp (u >= 0 ? lh + u * (std::log (hi) - lh) : lh + u * (lh - std::log (lo)));
    }
    return u >= 0 ? home + u * (hi - home) : home + u * (home - lo);
}

int chooseCoreSound (const std::vector<CoreSound>& bible, const Climate& c, double)
{
    if (bible.empty()) return -1;
    const Forces f = forces (c, 1.0);
    int best = 0;
    double bestD = 1e18;
    for (size_t i = 0; i < bible.size(); ++i)
    {
        const auto& s = bible[i];
        const double dt = (c.temp - s.anchorTemp) / 12.0, dw = (f.wet - s.anchorWet) / 0.35, dl = (f.light - s.anchorLight) / 0.8;
        double d = dt * dt + dw * dw + dl * dl;
        // The lottery: today's seed shifts each sound's pull by up to ±25 %
        d *= 0.75 + 0.5 * unit (mix (c.seed, (uint32_t) i + 1));
        if (d < bestD)
        {
            bestD = d;
            best = (int) i;
        }
    }
    return best;
}

Patch resolvePatch (const CoreSound& s, const Climate& c, double precip, const Macros& m) { return resolvePatch (s.home, s.lo, s.hi, c, precip, m); }

Patch resolvePatch (const Patch& home, const Patch& lo, const Patch& hi, const Climate& c, double precip, const Macros& m)
{
    const double intensity = m.intensity >= 0 ? 1 + 0.6 * m.intensity : 1 + m.intensity;
    double un[kNumParams], um[kNumParams];
    natureLean (c, precip, intensity, un);
    macroLean (m, um);
    Patch out = home;
    for (int p = 0; p < kNumParams; ++p)
    {
        // Small daily variation inside the box, so the same core sound is never quite the same twice
        const double lottery = (unit (mix (c.seed ^ 0xa5a5a5a5u, (uint32_t) p * 31 + 7)) - 0.5) * 0.3;
        out[p] = placeInBounds (p, home[p], lo[p], hi[p], un[p] + um[p] + lottery);
    }
    return out;
}
} // namespace atmos
