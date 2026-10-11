#pragma once
// Which backdrop family a place gets. Geography only: never today's temperature, humidity or
// precipitation (a cold day in a city is not alpine). Honest by construction:
//   curated  - a hand-checked list of well-known places (about 40 km around each)
//   ocean    - open water where the Natural Earth 110 m land outline (public domain) has no
//              land anywhere within about half a degree, so coarse coasts are not misread
//   none     - everything else: neutral, confidence 0
// A user's scene style overrides the picture only, never coordinates or weather.
#include "Environment.h"

namespace atmos::gui
{
struct LandscapeResolution
{
    Landscape profile = Landscape::neutral;
    float confidence = 0;        // 0..1
    const char* source = "none"; // "curated", "ocean", "none"
    int version = 1;             // bump when the table or the rules change
};
LandscapeResolution resolveLandscape (double lat, double lon);
} // namespace atmos::gui
