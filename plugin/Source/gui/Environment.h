#pragma once
// From an accepted weather snapshot to what the scene should look like.
//
// Pure data and arithmetic, no drawing and no JUCE GUI classes, so it is unit-tested on
// its own and runs only on the message thread. It never writes to the processor: the
// scene follows the sound's snapshot, never the other way round.
#include <cstdint>
#include <string>
#include <vector>

namespace atmos::gui
{
// What the header says about where the data came from
enum class Source
{
    live,      // a fresh accepted reading
    stale,     // a live reading that has aged (older than an hour, or from an earlier date)
    savedDay,  // a kept Day or a project's Day
    estimated, // the defined offline fallback (simulated sky for the last known place)
    preview,   // the admin globe
    loading    // waiting for the first reading
};
const char* sourceLabel (Source);

// The scene's view of one accepted Day. Unknown numbers are negative, never zero.
struct Snapshot
{
    int schemaVersion = 1;
    uint32_t generation = 0;
    Source source = Source::loading;
    bool offline = false; // refresh unavailable; can accompany any source
    std::string place;
    double lat = 0, lon = 0;
    int64_t observedAt = 0; // unix seconds: the sky this sound was dealt from
    int utcOffset = 0;
    bool offsetKnown = false;
    double tempC = 10, humidity = 70;
    std::vector<int> conditionIds; // OpenWeather codes, most significant first
    std::string conditionMain;     // OpenWeather's category, used only when no codes are known
    double cloud = 0.5;            // 0..1 cover
    double precip = 0;             // the engine's 0..1 intensity
    double rainMm = -1, snowMm = -1, windMps = 0, gustMps = -1, windFromDeg = -1, visibilityM = -1;
    uint32_t visualSeed = 1;
    // Fixture-only states the live provider cannot report
    bool fixtureHail = false, fixtureFrost = false;
};

// Structured reading of the condition codes
struct Conditions
{
    enum class Precip { none, drizzle, rain, snow, sleet, freezingRain, hail };
    Precip precip = Precip::none;
    double rate = 0; // 0..1
    bool thunder = false, squall = false, tornado = false;
    double mist = 0, fog = 0, haze = 0, smoke = 0, dust = 0, ash = 0; // 0..1 veils
    double cloud = 0;
    bool frost = false;
    bool unknownCode = false;
};
Conditions classify (const Snapshot&);

struct Environment
{
    // Sky (degrees; azimuth is a compass bearing)
    double sunAlt = 30, sunAz = 180, moonAlt = -10, moonAz = 0, moonFraction = 0.5, moonPhase = 0.25, moonAngle = 0;
    double daylight = 1; // 0 night .. 1 day, from the sun's altitude only
    bool evening = false;
    // Weather (all 0..1 unless stated)
    double cloud = 0, overcast = 0;
    double drizzle = 0, rain = 0, snow = 0, sleet = 0, hail = 0, freezing = 0;
    double thunder = 0, squall = 0, tornado = 0;
    double mist = 0, haze = 0, smoke = 0, dust = 0, ash = 0;
    double visibility = 1; // 1 clear .. 0 a few hundred metres
    double windMps = 0, gustMps = 0, windFromDeg = -1; // -1: direction unknown
    double waves = 0;
    double accent = 0, accentIce = 0; // surface snow/frost/ice
    double lamps = 0, stars = 0;
};
Environment computeEnvironment (const Snapshot&);
Environment blend (const Environment& a, const Environment& b, double t);

// Which lighting renders to mix, by the sun's altitude and the time of day
struct AnchorWeight
{
    const char* name;
    double weight;
};
std::vector<AnchorWeight> anchorWeights (const Environment&);

// Sky colours for the runtime gradient (sRGB 0..1)
struct SkyColours
{
    float zenith[3], horizon[3], sea[3], seaDeep[3], ambient[3];
};
SkyColours skyColours (const Environment&);
} // namespace atmos::gui
