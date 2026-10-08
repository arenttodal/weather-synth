#pragma once
// The sound bible: a handful of core sounds, each a home patch with boundaries,
// and the rules that place today's sound inside one of them.
//
// value = home + u * (hi - home)  when u > 0
//       = home + u * (home - lo)  when u < 0      (log parameters interpolate in log space)
// u comes from the climate (fixed weather rules below), the daily seed and the macros,
// and is clamped to [-1, 1], so nothing ever leaves the box the designer approved.
#include "ClimateMapper.h"
#include "Params.h"
#include <string>
#include <vector>

namespace atmos
{
struct CoreSound
{
    std::string name;
    Patch home, lo, hi;
    double anchorTemp = 12;  // °C this sound belongs to
    double anchorWet = 0.4;  // 0 dry .. 1 drenched (same scale as the 'wet' force)
    double anchorLight = 0.5; // 0 night .. 1 bright day
};

// How far each parameter leans, -1..1, before macros
void natureLean (const Climate& c, double precip, double intensity, double out[kNumParams]);

// Which macro moves which parameters (also shown in the designer)
void macroLean (const Macros& m, double out[kNumParams]);

// Nearest core sound for this climate; the seed breaks near-ties differently each day
int chooseCoreSound (const std::vector<CoreSound>& bible, const Climate& c, double precip);

// The finished patch: core sound, nature, lottery and macros
Patch resolvePatch (const CoreSound& s, const Climate& c, double precip, const Macros& m);
// Same, from plain data (no allocation: safe on the audio thread)
Patch resolvePatch (const Patch& home, const Patch& lo, const Patch& hi, const Climate& c, double precip, const Macros& m);

// Place a lean inside a core sound's boundaries
double placeInBounds (int param, double home, double lo, double hi, double u);
} // namespace atmos
