#include "ClimateMapper.h"
#include <algorithm>
#include <cmath>

namespace atmos
{
namespace
{
    double clamp (double v, double a, double b) { return std::min (b, std::max (a, v)); }
    double norm (double v, double lo, double hi) { return clamp ((v - lo) / (hi - lo), 0.0, 1.0); }
    // JavaScript's Math.round (half rounds toward +infinity)
    double jsRound (double v) { return std::floor (v + 0.5); }
} // namespace

double SeedRand::next()
{
    a = a + 0x6d2b79f5u;
    uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}

Forces forces (const Climate& c, double intensity)
{
    Forces f {};
    f.fullness = 1.0 - std::abs (c.moon - 0.5) * 2.0;
    const double day = norm (c.sun, -12, 40);
    const double moonlight = (1.0 - day) * f.fullness * 0.35 * (1.0 - c.clouds * 0.7);
    const double wetRaw = 0.55 * c.precip + 0.45 * norm (c.humidity, 40, 100);
    f.energy = norm (c.temp, -25, 40);
    f.heat = std::min (1.25, norm (c.temp, 32, 45) * intensity);
    f.cold = std::min (1.25, norm (-c.temp, 10, 30) * intensity);
    f.wet = clamp (wetRaw, 0, 1);
    f.drench = std::min (1.25, norm (wetRaw, 0.7, 1) * intensity);
    f.humid = norm (c.humidity, 30, 100);
    f.motion = norm (c.wind, 0, 20);
    f.gale = std::min (1.25, norm (c.wind, 14, 28) * intensity);
    f.light = clamp (day * (1.0 - 0.5 * c.clouds) + moonlight, 0, 1);
    f.dark = 1.0 - day;
    f.low = norm (1013.0 - c.pressure, 0, 45);
    return f;
}

Params natureParams (const Climate& c, double intensity)
{
    const Forces f = forces (c, intensity);
    SeedRand r (c.seed != 0 ? c.seed : 1u);
    const double seedFrame = (r.next() - 0.5) * 0.24;
    static const double delayMults[] = { 0.75, 1.0, 1.5 };
    const double seedDelay = delayMults[(int) std::floor (r.next() * 3.0)];
    const double seedChorus = 0.8 + r.next() * 0.4;

    Params P {};
    P.frame = clamp (0.08 + 0.84 * f.energy + seedFrame, 0, 1);
    P.spread = 3 + 10 * f.humid + 18 * f.heat;
    P.sub = clamp (0.1 + 0.35 * f.low + 0.2 * (1 - f.fullness) * f.dark, 0, 0.7);
    P.shimmer = clamp (0.4 * f.fullness * f.dark + 0.3 * f.cold, 0, 0.6);
    P.attack = clamp ((0.005 + 1.4 * std::pow (1 - f.energy, 2.0)) * (0.7 + 0.6 * f.wet) * (1 - 0.9 * f.heat), 0.003, 3);
    P.decay = 0.3 + 1.5 * f.wet;
    P.sustain = clamp (0.72 + 0.2 * f.wet - 0.3 * f.heat - 0.15 * f.gale, 0.2, 1);
    P.release = clamp ((0.25 + 3.2 * f.wet + 1.2 * f.cold) * (1 - 0.6 * f.heat), 0.08, 8);
    P.drive = clamp (0.3 * norm (c.temp, 18, 32) + 0.7 * f.heat, 0, 1);
    P.bits = f.heat > 0.01 ? clamp (jsRound (16 - 12 * std::pow (f.heat, 1.4)), 4, 16) : 16;
    const double bright = 0.5 * f.light + 0.3 * f.energy + 0.2 * (1 - 0.5 * f.wet);
    P.cutoff = 200 * std::pow (2.0, bright * 5.5);
    P.q = 0.8 + 5 * f.gale + 2 * f.cold;
    P.tilt = -10 + 13 * f.light - 2 * c.clouds;
    P.chorusDepth = 0.2 + 0.6 * f.humid;
    P.chorusWet = 0.12 + 0.45 * norm (c.humidity, 50, 100);
    P.chorusRate = (0.3 + 3 * f.energy) * seedChorus;
    P.vibDepth = clamp (0.02 + 0.22 * f.motion + 0.2 * f.gale, 0, 0.6);
    P.vibRate = 1.5 + 5 * f.energy + 3 * f.motion;
    P.tremDepth = clamp (0.45 * f.gale + 0.85 * f.heat, 0, 1);
    P.tremRate = 2 + 10 * f.heat + 4 * f.gale + 3 * f.energy;
    P.delayTime = clamp ((0.25 + 0.3 * f.humid - 0.12 * f.heat) * seedDelay, 0.06, 0.9);
    P.delayFb = clamp (0.12 + 0.45 * f.wet + 0.2 * f.drench, 0, 0.85);
    P.delayWet = clamp (0.04 + 0.35 * c.precip + 0.25 * f.drench, 0, 0.7);
    P.verbDecay = clamp (1.2 + 7 * f.wet + 3 * f.cold + 6 * f.drench, 0.6, 18);
    P.verbWet = clamp (0.12 + 0.42 * f.wet + 0.3 * f.drench, 0, 0.92);
    P.noise = clamp (0.5 * c.precip + 0.35 * f.gale, 0, 0.8);
    const bool snow = c.temp < 1;
    P.noiseFreq = (500 + 4000 * c.precip) * (snow ? 0.3 : 1) + 1500 * f.gale;
    P.noise *= snow ? 0.6 : 1;
    P.trim = clamp (1.01 * P.drive - 3.7 * P.sub - 1.21 * P.verbWet - 2.45 * P.delayFb - 2.78 * P.noise
                        + 2.25 * P.chorusWet - 1.91 * P.tremDepth + 0.17 * (std::log2 (P.cutoff) - 10),
                    -9, 9);
    return P;
}

Params resolveLeash (const Climate& c, const Macros& m)
{
    const double intensity = m.intensity >= 0 ? 1 + 0.6 * m.intensity : 1 + m.intensity;
    Params P = natureParams (c, intensity);
    const Forces f = forces (c, intensity);

    P.cutoff = clamp (P.cutoff * std::pow (2.0, 3.0 * m.tone), 60, 16000);
    P.attack = clamp (P.attack * std::pow (2.0, 3.0 * m.bloom), 0.002, 4);
    P.release = clamp (P.release * std::pow (2.0, 2.5 * m.bloom), 0.05, 12);
    P.sustain = clamp (P.sustain + 0.25 * m.bloom, 0.1, 1);

    // Climate sets the floor of the space: a drenched day never goes fully dry
    P.verbWet = clamp (P.verbWet + 0.55 * m.space, 0.45 * f.drench, 0.95);
    P.delayWet = clamp (P.delayWet + 0.35 * m.space, 0.3 * f.drench, 0.75);
    P.verbDecay = clamp (P.verbDecay * std::pow (2.0, m.space), 0.5, 20);

    const double ms = std::pow (2.0, 1.6 * m.motion);
    P.vibDepth = clamp (P.vibDepth * ms, 0, 0.8);
    P.chorusDepth = clamp (P.chorusDepth * ms, 0, 1);
    P.tremDepth = clamp (P.tremDepth * ms + (m.motion > 0 ? 0.15 * m.motion : 0), 0, 1);
    return P;
}
} // namespace atmos
