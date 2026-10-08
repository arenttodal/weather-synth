#pragma once
#include "ClimateMapper.h"
#include <cstdint>
#include <string>

namespace atmos
{
// Solar elevation in degrees (NOAA low-precision algorithm, ~0.5° accuracy)
double sunElevation (double latDeg, double lonDeg, int64_t unixSeconds);

// Lunar phase 0..1 (0 new, 0.5 full), mean synodic month from the 2000-01-06 new moon
double moonPhase (int64_t unixSeconds);

std::string moonPhaseName (double phase);

// "YYYY-MM-DD" of the place's local date. utcOffsetSeconds from the weather
// provider when known, otherwise estimated from longitude.
std::string localDate (int64_t unixSeconds, double lonDeg, int utcOffsetSeconds, bool offsetKnown);

// The lottery seed: same for everyone in a ~11 km cell on a given local date
uint32_t daySeed (const std::string& localDateYmd, double latDeg, double lonDeg);

// Deterministic stand-in weather from latitude, season and hour. Used when
// the relay can't be reached and for globe previews while offline.
struct SimWeather
{
    double temp, humidity, precip, wind, clouds, pressure;
    std::string condition;
};
SimWeather simulateWeather (double latDeg, double lonDeg, int64_t unixSeconds);
} // namespace atmos
