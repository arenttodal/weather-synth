#include "Sky.h"
#include <cmath>

namespace atmos::sky
{
namespace
{
    constexpr double PI = 3.14159265358979323846;
    constexpr double rad = PI / 180.0;
    constexpr double e = rad * 23.4397; // obliquity of the Earth

    double toDays (int64_t t) { return t / 86400.0 - 0.5 + 2440588.0 - 2451545.0; }
    double rightAscension (double l, double b) { return std::atan2 (std::sin (l) * std::cos (e) - std::tan (b) * std::sin (e), std::cos (l)); }
    double declination (double l, double b) { return std::asin (std::sin (b) * std::cos (e) + std::cos (b) * std::sin (e) * std::sin (l)); }
    double azimuth (double H, double phi, double dec) { return std::atan2 (std::sin (H), std::cos (H) * std::sin (phi) - std::tan (dec) * std::cos (phi)); }
    double altitude (double H, double phi, double dec) { return std::asin (std::sin (phi) * std::sin (dec) + std::cos (phi) * std::cos (dec) * std::cos (H)); }
    double siderealTime (double d, double lw) { return rad * (280.16 + 360.9856235 * d) - lw; }

    struct Eq
    {
        double ra, dec, dist;
    };
    Eq sunCoords (double d)
    {
        const double M = rad * (357.5291 + 0.98560028 * d);
        const double C = rad * (1.9148 * std::sin (M) + 0.02 * std::sin (2 * M) + 0.0003 * std::sin (3 * M));
        const double L = M + C + rad * 102.9372 + PI;
        return { rightAscension (L, 0), declination (L, 0), 149598000.0 };
    }
    Eq moonCoords (double d)
    {
        const double L = rad * (218.316 + 13.176396 * d);
        const double M = rad * (134.963 + 13.064993 * d);
        const double F = rad * (93.272 + 13.229350 * d);
        const double l = L + rad * 6.289 * std::sin (M);
        const double b = rad * 5.128 * std::sin (F);
        return { rightAscension (l, b), declination (l, b), 385001.0 - 20905.0 * std::cos (M) };
    }
    // SunCalc measures azimuth from south, positive westward; convert to a compass bearing
    double compass (double az) { return std::fmod (az / rad + 180.0 + 360.0, 360.0); }
} // namespace

Position sun (double lat, double lon, int64_t t)
{
    const double lw = rad * -lon, phi = rad * lat, d = toDays (t);
    const Eq c = sunCoords (d);
    const double H = siderealTime (d, lw) - c.ra;
    return { altitude (H, phi, c.dec) / rad, compass (azimuth (H, phi, c.dec)) };
}

Position moon (double lat, double lon, int64_t t)
{
    const double lw = rad * -lon, phi = rad * lat, d = toDays (t);
    const Eq c = moonCoords (d);
    const double H = siderealTime (d, lw) - c.ra;
    double h = altitude (H, phi, c.dec);
    h += rad * 0.017 / std::tan (h + rad * 10.26 / (h / rad + 5.10)); // refraction
    return { h / rad, compass (azimuth (H, phi, c.dec)) };
}

MoonLight moonLight (int64_t t)
{
    const double d = toDays (t);
    const Eq s = sunCoords (d), m = moonCoords (d);
    const double phi = std::acos (std::sin (s.dec) * std::sin (m.dec) + std::cos (s.dec) * std::cos (m.dec) * std::cos (s.ra - m.ra));
    const double inc = std::atan2 (s.dist * std::sin (phi), m.dist - s.dist * std::cos (phi));
    const double angle = std::atan2 (std::cos (s.dec) * std::sin (s.ra - m.ra),
                                     std::sin (s.dec) * std::cos (m.dec) - std::cos (s.dec) * std::sin (m.dec) * std::cos (s.ra - m.ra));
    return { (1 + std::cos (inc)) / 2, 0.5 + 0.5 * inc * (angle < 0 ? -1 : 1) / PI, angle / rad };
}
} // namespace atmos::sky
