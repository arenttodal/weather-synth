#include "Globe.h"
#include "BinaryData.h"
#include <cmath>

namespace atmos
{
namespace
{
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
} // namespace

const std::vector<Globe::Ring>& Globe::landRings()
{
    // land110m.bin: int32 rings, then per ring int32 n + n * (int16 lon*100, int16 lat*100)
    static const std::vector<Ring> rings = [] {
        std::vector<Ring> out;
        juce::MemoryInputStream in (BinaryData::land110m_bin, (size_t) BinaryData::land110m_binSize, false);
        const int count = in.readInt();
        for (int r = 0; r < count && ! in.isExhausted(); ++r)
        {
            const int n = in.readInt();
            if (n <= 0 || n > 100000) break;
            Ring ring;
            ring.lonLat.reserve ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const float lon = in.readShort() / 100.0f;
                const float lat = in.readShort() / 100.0f;
                ring.lonLat.push_back ({ lon, lat });
            }
            out.push_back (std::move (ring));
        }
        return out;
    }();
    return rings;
}

bool Globe::isLandAt (double lat, double lon)
{
    // Even-odd ray casts in the lon/lat plane. Rings that cross the dateline are unwrapped to
    // continuous longitudes and tested at lon and lon +/- 360; a ring that circles the pole
    // (Antarctica) keeps its seam and is tested as stored.
    struct Prepared
    {
        std::vector<juce::Point<double>> pts;
        bool polar = false;
    };
    static const std::vector<Prepared> rings = [] {
        std::vector<Prepared> out;
        for (const auto& r : landRings())
        {
            Prepared p;
            for (const auto& v : r.lonLat)
            {
                double x = v.x;
                if (! p.pts.empty())
                {
                    while (x - p.pts.back().x > 180) x -= 360;
                    while (x - p.pts.back().x < -180) x += 360;
                }
                p.pts.push_back ({ x, (double) v.y });
            }
            p.polar = ! p.pts.empty() && std::abs (p.pts.back().x - p.pts.front().x) > 180;
            if (p.polar)
            {
                p.pts.clear();
                for (const auto& v : r.lonLat)
                    p.pts.push_back ({ (double) v.x, (double) v.y });
            }
            out.push_back (std::move (p));
        }
        return out;
    }();
    auto odd = [] (const std::vector<juce::Point<double>>& v, double y, double x, bool skipSeam) {
        bool inside = false;
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++)
        {
            if (skipSeam && std::abs (v[i].x - v[j].x) > 180) continue;
            if ((v[i].y > y) != (v[j].y > y) && x < (v[j].x - v[i].x) * (y - v[i].y) / (v[j].y - v[i].y) + v[i].x) inside = ! inside;
        }
        return inside;
    };
    lon = std::fmod (std::fmod (lon + 180.0, 360.0) + 360.0, 360.0) - 180.0;
    for (const auto& r : rings)
    {
        if (r.polar)
        {
            if (odd (r.pts, lat, lon, true)) return true;
        }
        else if (odd (r.pts, lat, lon, false) || odd (r.pts, lat, lon + 360, false) || odd (r.pts, lat, lon - 360, false))
            return true;
    }
    return false;
}

Globe::Globe() : land (landRings())
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void Globe::setCurrent (double lat, double lon, bool has)
{
    curLat = lat;
    curLon = lon;
    hasCurrent = has;
    repaint();
}

void Globe::centreOn (double lat, double lon)
{
    lat0 = juce::jlimit (-85.0, 85.0, lat);
    lon0 = lon;
    repaint();
}

void Globe::setPin (double lat, double lon, bool has)
{
    pinLat = lat;
    pinLon = lon;
    hasPin = has;
    repaint();
}

bool Globe::project (double lat, double lon, juce::Point<float>& out) const
{
    const double p = lat * kDeg, l = (lon - lon0) * kDeg, p0 = lat0 * kDeg;
    const double cosc = std::sin (p0) * std::sin (p) + std::cos (p0) * std::cos (p) * std::cos (l);
    double x = std::cos (p) * std::sin (l);
    double y = std::cos (p0) * std::sin (p) - std::sin (p0) * std::cos (p) * std::cos (l);
    if (cosc < 0)
    {
        // Far side: push onto the horizon so filled outlines clip cleanly
        const double len = std::sqrt (x * x + y * y);
        if (len > 1e-9)
        {
            x /= len;
            y /= len;
        }
    }
    const auto c = centre();
    const float R = radius();
    out = { c.x + (float) (R * x), c.y - (float) (R * y) };
    return cosc >= 0;
}

bool Globe::unproject (juce::Point<float> s, double& lat, double& lon) const
{
    const auto c = centre();
    const double R = radius();
    const double x = (s.x - c.x) / R, y = -(s.y - c.y) / R;
    const double rho = std::sqrt (x * x + y * y);
    if (rho > 1.0) return false;
    const double p0 = lat0 * kDeg;
    if (rho < 1e-9)
    {
        lat = lat0;
        lon = lon0;
        return true;
    }
    const double cc = std::asin (rho);
    lat = std::asin (std::cos (cc) * std::sin (p0) + y * std::sin (cc) * std::cos (p0) / rho) / kDeg;
    lon = lon0 + std::atan2 (x * std::sin (cc), rho * std::cos (cc) * std::cos (p0) - y * std::sin (cc) * std::sin (p0)) / kDeg;
    lon = std::fmod (lon + 540.0, 360.0) - 180.0;
    return true;
}

void Globe::paint (juce::Graphics& g)
{
    const auto c = centre();
    const float R = radius();
    const juce::Rectangle<float> disc (c.x - R, c.y - R, 2 * R, 2 * R);

    g.setColour (pal.ocean.brighter (0.15f).withAlpha (0.35f));
    g.fillEllipse (disc.expanded (6)); // atmosphere glow
    g.setColour (pal.ocean);
    g.fillEllipse (disc);

    g.saveState();
    juce::Path clip;
    clip.addEllipse (disc);
    g.reduceClipRegion (clip);

    // Graticule every 30°
    g.setColour (pal.grid);
    for (int lat = -60; lat <= 60; lat += 30)
    {
        juce::Path p;
        bool started = false;
        for (int lon = -180; lon <= 180; lon += 4)
        {
            juce::Point<float> pt;
            const bool vis = project (lat, lon, pt);
            if (vis && ! started) p.startNewSubPath (pt), started = true;
            else if (vis) p.lineTo (pt);
            else started = false;
        }
        g.strokePath (p, juce::PathStrokeType (0.8f));
    }
    for (int lon = -180; lon < 180; lon += 30)
    {
        juce::Path p;
        bool started = false;
        for (int lat = -88; lat <= 88; lat += 4)
        {
            juce::Point<float> pt;
            const bool vis = project (lat, lon, pt);
            if (vis && ! started) p.startNewSubPath (pt), started = true;
            else if (vis) p.lineTo (pt);
            else started = false;
        }
        g.strokePath (p, juce::PathStrokeType (0.8f));
    }

    // Land
    for (const auto& ring : land)
    {
        juce::Path fill, coast;
        bool anyVisible = false, coastStarted = false;
        for (size_t i = 0; i < ring.lonLat.size(); ++i)
        {
            juce::Point<float> pt;
            const bool vis = project (ring.lonLat[i].y, ring.lonLat[i].x, pt);
            anyVisible |= vis;
            if (i == 0) fill.startNewSubPath (pt);
            else fill.lineTo (pt);
            if (vis && ! coastStarted) coast.startNewSubPath (pt), coastStarted = true;
            else if (vis) coast.lineTo (pt);
            else coastStarted = false;
        }
        if (! anyVisible) continue;
        fill.closeSubPath();
        g.setColour (pal.land);
        g.fillPath (fill);
        g.setColour (pal.coast);
        g.strokePath (coast, juce::PathStrokeType (0.9f));
    }

    // Kept Days
    for (auto m : marks)
    {
        juce::Point<float> pt;
        if (project (m.y, m.x, pt))
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5, 5);
        }
    }
    if (pal.shaded)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), c.x - R * 0.5f, c.y - R * 0.55f, juce::Colours::black.withAlpha (0.28f), c.x + R * 0.75f, c.y + R * 0.8f, true));
        g.fillEllipse (disc);
    }
    g.restoreState();

    // Keep the rim crisp
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (disc, 1.0f);

    if (hasCurrent)
    {
        juce::Point<float> pt;
        if (project (curLat, curLon, pt))
        {
            juce::Path d; // a white diamond: the place that is loaded now
            d.addQuadrilateral (pt.x, pt.y - 6, pt.x + 6, pt.y, pt.x, pt.y + 6, pt.x - 6, pt.y);
            g.setColour (juce::Colours::white);
            g.fillPath (d);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.strokePath (d, juce::PathStrokeType (1.2f));
        }
    }
    if (hasPin)
    {
        juce::Point<float> pt;
        if (project (pinLat, pinLon, pt))
        {
            g.setColour (pal.pin.withAlpha (0.3f));
            g.fillEllipse (pt.x - 11, pt.y - 11, 22, 22);
            g.setColour (pal.pin);
            g.fillEllipse (pt.x - 5, pt.y - 5, 10, 10);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (pt.x - 5, pt.y - 5, 10, 10, 1.2f);
        }
    }
}

void Globe::mouseDown (const juce::MouseEvent& e)
{
    dragStart = e.position;
    dragLat0 = lat0;
    dragLon0 = lon0;
    dragged = false;
}

void Globe::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - dragStart;
    if (d.getDistanceFromOrigin() > 3) dragged = true;
    if (! dragged) return;
    const double degPerPx = 90.0 / (radius() + 1);
    lon0 = dragLon0 - d.x * degPerPx;
    lat0 = juce::jlimit (-85.0, 85.0, dragLat0 + d.y * degPerPx);
    repaint();
}

void Globe::mouseUp (const juce::MouseEvent& e)
{
    if (dragged) return;
    double lat, lon;
    if (unproject (e.position, lat, lon))
    {
        setPin (lat, lon, true);
        if (onPin) onPin (lat, lon);
    }
}

void Globe::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    zoom = juce::jlimit (0.8, 4.0, zoom * std::pow (1.15, w.deltaY * 4));
    repaint();
}
} // namespace atmos
