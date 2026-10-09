#pragma once
// Visual-only astronomy for the plugin's scene: where the sun and moon are in the sky and
// how much of the moon is lit. A port of SunCalc's formulas (Vladimir Agafonkin, BSD-2-Clause,
// https://github.com/mourner/suncalc), accurate to well under a degree, which is ample for a
// picture. The sound does not use these (it keeps Astro.h's elevation and mean phase), so
// changing them can never change how a Day sounds.
#include <cstdint>

namespace atmos::sky
{
struct Position
{
    double altitudeDeg; // above the horizon
    double azimuthDeg;  // compass bearing: 0 north, 90 east, 180 south, 270 west
};

Position sun (double latDeg, double lonDeg, int64_t unixSeconds);
Position moon (double latDeg, double lonDeg, int64_t unixSeconds);

struct MoonLight
{
    double fraction; // 0..1 of the disc lit
    double phase;    // 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter
    double angleDeg; // midpoint angle of the lit limb (SunCalc's 'angle')
};
MoonLight moonLight (int64_t unixSeconds);
} // namespace atmos::sky
