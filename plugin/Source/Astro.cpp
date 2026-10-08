#include "Astro.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace atmos
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    double rad (double d) { return d * kPi / 180.0; }
    double deg (double r) { return r * 180.0 / kPi; }

    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm)
    void civilFromDays (int64_t z, int& y, unsigned& m, unsigned& d)
    {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned) (z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = (int) (yoe + era * 400);
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp < 10 ? mp + 3 : mp - 9;
        y += (m <= 2);
    }

    int64_t floorDiv (int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

    uint32_t fnv1a (const std::string& s)
    {
        uint32_t h = 2166136261u;
        for (const char c : s)
        {
            h ^= (uint32_t) (unsigned char) c;
            h *= 16777619u;
        }
        return h;
    }

    double hash01 (const std::string& s) { return (fnv1a (s) & 0xffffff) / 16777216.0; }
} // namespace

double sunElevation (double latDeg, double lonDeg, int64_t unixSeconds)
{
    const double jd = unixSeconds / 86400.0 + 2440587.5;
    const double n = jd - 2451545.0;
    const double L = std::fmod (280.460 + 0.9856474 * n, 360.0);
    const double g = rad (std::fmod (357.528 + 0.9856003 * n, 360.0));
    const double lambda = rad (L + 1.915 * std::sin (g) + 0.020 * std::sin (2 * g));
    const double eps = rad (23.439 - 0.0000004 * n);
    const double decl = std::asin (std::sin (eps) * std::sin (lambda));
    const double ra = std::atan2 (std::cos (eps) * std::sin (lambda), std::cos (lambda));
    // Greenwich mean sidereal time -> local hour angle
    const double gmst = std::fmod (280.46061837 + 360.98564736629 * n, 360.0);
    const double ha = rad (gmst + lonDeg) - ra;
    const double lat = rad (latDeg);
    const double el = std::asin (std::sin (lat) * std::sin (decl) + std::cos (lat) * std::cos (decl) * std::cos (ha));
    return deg (el);
}

double moonPhase (int64_t unixSeconds)
{
    const double synodic = 29.530588853 * 86400.0;
    const double ref = 947182440.0; // 2000-01-06 18:14 UTC new moon
    double p = std::fmod ((double) unixSeconds - ref, synodic) / synodic;
    if (p < 0) p += 1.0;
    return p;
}

std::string moonPhaseName (double p)
{
    if (p < 0.03 || p > 0.97) return "New moon";
    if (p < 0.22) return "Waxing crescent";
    if (p < 0.28) return "First quarter";
    if (p < 0.47) return "Waxing gibbous";
    if (p < 0.53) return "Full moon";
    if (p < 0.72) return "Waning gibbous";
    if (p < 0.78) return "Last quarter";
    return "Waning crescent";
}

std::string localDate (int64_t unixSeconds, double lonDeg, int utcOffsetSeconds, bool offsetKnown)
{
    const int64_t offset = offsetKnown ? utcOffsetSeconds : (int64_t) std::llround (lonDeg / 15.0) * 3600;
    int y;
    unsigned m, d;
    civilFromDays (floorDiv (unixSeconds + offset, 86400), y, m, d);
    char buf[16];
    std::snprintf (buf, sizeof buf, "%04d-%02u-%02u", y, m, d);
    return buf;
}

uint32_t daySeed (const std::string& ymd, double latDeg, double lonDeg)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, "%s|%.1f,%.1f", ymd.c_str(), std::round (latDeg * 10) / 10, std::round (lonDeg * 10) / 10);
    const uint32_t h = fnv1a (buf);
    return h != 0 ? h : 1u;
}

SimWeather simulateWeather (double latDeg, double lonDeg, int64_t t)
{
    SimWeather w {};
    const double absLat = std::abs (latDeg);
    const double c = std::cos (rad (absLat));
    // Day of year and northern/southern season
    const double doy = std::fmod (t / 86400.0 + 365.25 * 30, 365.25); // offset keeps it positive
    double season = std::cos (2 * kPi * (doy - 200) / 365.25); // +1 mid-July
    if (latDeg < 0) season = -season;
    const double mean = 30 * std::pow (c, 1.2) - 18 * (1 - c);
    const double amp = 15 * std::sin (rad (absLat));
    const double elev = sunElevation (latDeg, lonDeg, t);
    const double diurnal = std::clamp (elev / 60.0, -0.4, 1.0) * 6.0 - 1.0;

    // Weather "luck" changes every 6 hours, per ~1° cell, deterministically
    char key[64];
    std::snprintf (key, sizeof key, "%lld|%.0f,%.0f", (long long) floorDiv (t, 21600), latDeg, lonDeg);
    const std::string k (key);
    const double luck1 = hash01 (k + "a"), luck2 = hash01 (k + "b"), luck3 = hash01 (k + "c");

    const double desert = std::exp (-std::pow ((absLat - 25) / 8, 2)); // subtropical dry belt
    const double tropic = std::exp (-std::pow (absLat / 12, 2));
    w.temp = mean + amp * season + diurnal + (luck1 - 0.5) * 6;
    const double wetChance = 0.12 + 0.4 * tropic + 0.2 * (1 - desert) * (absLat > 40 ? 1 : 0.5) - 0.1 * desert;
    w.precip = luck2 < wetChance ? std::round ((0.15 + 0.85 * luck3) * 100) / 100 : 0;
    w.humidity = std::clamp (72 - 50 * desert + 18 * tropic + (w.precip > 0 ? 15 : 0) + (luck3 - 0.5) * 20, 5.0, 100.0);
    w.clouds = w.precip > 0 ? 0.8 + 0.2 * luck1 : std::clamp (luck3 * (1 - 0.7 * desert), 0.0, 1.0);
    w.wind = std::max (0.0, 2 + 7 * std::sin (rad (absLat)) * luck2 + (absLat > 45 && luck1 > 0.85 ? 10 : 0));
    w.pressure = 1013 + (luck1 - 0.5) * 30 - (w.precip > 0.6 ? 12 : 0);
    w.condition = w.precip > 0 ? (w.temp < 1 ? "Snow" : w.precip > 0.7 ? "Thunderstorm" : w.precip > 0.25 ? "Rain" : "Drizzle")
                               : (w.clouds > 0.6 ? "Clouds" : "Clear");
    return w;
}
} // namespace atmos
