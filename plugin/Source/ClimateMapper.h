#pragma once
#include <cstdint>

// Climate -> synth parameter mapping. A line-for-line port of the mapping in
// lab.html (model B, "Leash"); tests/vectors.json, generated from that file,
// keeps the two in step. No JUCE or audio code here.
namespace atmos
{
// Bump when the mapping changes audibly; saved Days remember the version.
constexpr int kMappingVersion = 1;

struct Climate
{
    double temp = 10;       // °C
    double humidity = 70;   // %RH
    double precip = 0;      // 0..1 (none .. storm)
    double wind = 3;        // m/s
    double clouds = 0.5;    // 0..1
    double sun = 20;        // solar elevation, degrees
    double moon = 0.25;     // phase 0..1, 0.5 = full
    double pressure = 1013; // hPa
    uint32_t seed = 1;      // daily seed (date + place cell)
};

struct Forces
{
    double energy, heat, cold, wet, drench, humid, motion, gale, light, fullness, dark, low;
};

struct Params
{
    double frame, spread, sub, shimmer;
    double attack, decay, sustain, release;
    double drive, bits, cutoff, q, tilt;
    double chorusDepth, chorusWet, chorusRate;
    double vibDepth, vibRate, tremDepth, tremRate;
    double delayTime, delayFb, delayWet;
    double verbDecay, verbWet;
    double noise, noiseFreq, trim;
};

// Macro positions, -1..1, 0 = where nature put it
struct Macros
{
    double tone = 0, bloom = 0, space = 0, motion = 0, intensity = 0;
};

Forces forces (const Climate& c, double intensity = 1.0);
Params natureParams (const Climate& c, double intensity = 1.0);
Params resolveLeash (const Climate& c, const Macros& m);

// Same generator as the lab (mulberry32)
struct SeedRand
{
    explicit SeedRand (uint32_t s) : a (s) {}
    double next();
    uint32_t a;
};
} // namespace atmos
