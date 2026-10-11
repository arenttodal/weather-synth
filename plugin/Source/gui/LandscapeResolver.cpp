#include "LandscapeResolver.h"
#include "../Globe.h"
#include <cmath>

namespace atmos::gui
{
namespace
{
    struct Curated
    {
        const char* name;
        double lat, lon;
        Landscape l;
    };
    // Hand-picked, approximate families for well-known places; extend with care
    constexpr Curated curated[] = {
        { "Mumbai", 19.08, 72.88, Landscape::city },          { "Delhi", 28.61, 77.21, Landscape::city },
        { "Tokyo", 35.68, 139.69, Landscape::city },          { "Shanghai", 31.23, 121.47, Landscape::city },
        { "Beijing", 39.90, 116.40, Landscape::city },        { "Singapore", 1.35, 103.82, Landscape::city },
        { "New York", 40.71, -74.01, Landscape::city },       { "Los Angeles", 34.05, -118.24, Landscape::city },
        { "London", 51.51, -0.13, Landscape::city },          { "Paris", 48.86, 2.35, Landscape::city },
        { "Oslo", 59.91, 10.75, Landscape::city },            { "Sao Paulo", -23.55, -46.63, Landscape::city },
        { "Cairo", 30.04, 31.24, Landscape::city },           { "Lagos", 6.52, 3.38, Landscape::city },
        { "Sydney", -33.87, 151.21, Landscape::coast },       { "Cape Town", -33.92, 18.42, Landscape::coast },
        { "Rio de Janeiro", -22.91, -43.20, Landscape::coast }, { "Trondheim", 63.43, 10.39, Landscape::coast },
        { "Bergen", 60.39, 5.32, Landscape::coast },          { "Reykjavik", 64.15, -21.94, Landscape::coast },
        { "Honolulu", 21.31, -157.86, Landscape::coast },     { "Tromso", 69.65, 18.96, Landscape::coast },
        { "Barcelona", 41.39, 2.17, Landscape::mediterranean }, { "Athens", 37.98, 23.73, Landscape::mediterranean },
        { "Rome", 41.90, 12.50, Landscape::mediterranean },   { "Marseille", 43.30, 5.37, Landscape::mediterranean },
        { "Lisbon", 38.72, -9.14, Landscape::mediterranean }, { "Dubai", 25.20, 55.27, Landscape::desert },
        { "Riyadh", 24.71, 46.68, Landscape::desert },        { "Phoenix", 33.45, -112.07, Landscape::desert },
        { "Marrakesh", 31.63, -8.00, Landscape::desert },     { "Zermatt", 46.02, 7.75, Landscape::alpine },
        { "Innsbruck", 47.27, 11.39, Landscape::alpine },     { "Chamonix", 45.92, 6.87, Landscape::alpine },
        { "Andalsnes", 62.57, 7.69, Landscape::alpine },      { "Nairobi", -1.29, 36.82, Landscape::savanna },
        { "Serengeti", -2.33, 34.83, Landscape::savanna },    { "Manaus", -3.12, -60.02, Landscape::tropical },
        { "Longyearbyen", 78.22, 15.65, Landscape::tundra },  { "Nuuk", 64.18, -51.72, Landscape::tundra },
        { "Fairbanks", 64.84, -147.72, Landscape::boreal },   { "Yellowknife", 62.45, -114.37, Landscape::boreal },
        { "Wichita", 37.69, -97.34, Landscape::grassland },   { "Ulaanbaatar", 47.89, 106.91, Landscape::grassland },
        { "New Orleans", 29.95, -90.07, Landscape::wetland }, { "Dhaka", 23.81, 90.41, Landscape::wetland },
    };

    double km (double lat1, double lon1, double lat2, double lon2)
    {
        const double d = juce::MathConstants<double>::pi / 180.0;
        const double a = std::sin ((lat2 - lat1) * d / 2), b = std::sin ((lon2 - lon1) * d / 2);
        const double h = a * a + std::cos (lat1 * d) * std::cos (lat2 * d) * b * b;
        return 2 * 6371.0 * std::asin (std::sqrt (juce::jmin (1.0, h)));
    }
} // namespace

LandscapeResolution resolveLandscape (double lat, double lon)
{
    LandscapeResolution r;
    for (auto& c : curated)
        if (km (lat, lon, c.lat, c.lon) < 40)
        {
            r.profile = c.l;
            r.confidence = 0.8f;
            r.source = "curated";
            return r;
        }
    // Open water only when there is no land within about half a degree in any direction
    bool land = false;
    for (int i = -1; i <= 1 && ! land; ++i)
        for (int j = -1; j <= 1 && ! land; ++j)
            land = atmos::Globe::isLandAt (juce::jlimit (-89.9, 89.9, lat + 0.5 * i), lon + 0.5 * j / juce::jmax (0.2, std::cos (lat * juce::MathConstants<double>::pi / 180)));
    if (! land)
    {
        r.profile = Landscape::openWater;
        r.confidence = 0.7f;
        r.source = "ocean";
    }
    return r;
}
} // namespace atmos::gui
