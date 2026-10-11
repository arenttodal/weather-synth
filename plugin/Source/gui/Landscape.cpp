#include "Landscape.h"
#include <cmath>
#include <vector>

namespace atmos::gui
{
namespace
{
    using Pt = juce::Point<float>;
    // Vertical unit: the layouts below were drawn for a 64-unit band under the horizon and
    // scale to the actual band (set at the start of each drawLandscape; message thread only)
    float u = 1;
    float lum (const float* c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

    // Every colour passes through here: hazed towards the horizon colour with distance,
    // dimmed with the ambient light, whitened by settled snow
    struct Tint
    {
        juce::Colour horizon;
        float light = 1, snow = 0;
        juce::Colour operator() (juce::Colour base, float distance) const
        {
            auto c = base.interpolatedWith (juce::Colour (0xffeef3f7), snow * 0.65f);
            return c.interpolatedWith (horizon, distance).withMultipliedBrightness (light);
        }
    };

    std::vector<Pt> ridgePoints (juce::Random& r, float W, float base, float amp, float step, float sharp)
    {
        std::vector<Pt> pts;
        for (float x = -step; x <= W + step; x += step * (0.7f + 0.6f * r.nextFloat()))
            pts.push_back ({ x, base - amp * std::pow (r.nextFloat(), sharp) });
        return pts;
    }

    // A faceted band: the silhouette in one colour, each segment's facet a shade lighter or
    // darker by its slope (light from the left), closed down to 'bottom'
    void band (juce::Graphics& g, const std::vector<Pt>& pts, float bottom, juce::Colour col, float facet = 0.12f)
    {
        juce::Path p, lit, shade;
        p.startNewSubPath (pts.front().x, bottom);
        for (auto& v : pts)
            p.lineTo (v);
        p.lineTo (pts.back().x, bottom);
        p.closeSubPath();
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const auto a = pts[i], b = pts[i + 1];
            const Pt foot { (a.x + b.x) / 2, juce::jmin (bottom, juce::jmax (a.y, b.y) + 0.6f * std::abs (a.y - b.y) + 5) };
            (b.y < a.y ? lit : shade).addTriangle (a, b, foot);
        }
        g.setColour (col);
        g.fillPath (p);
        g.setColour (col.brighter (facet));
        g.fillPath (lit);
        g.setColour (col.darker (facet));
        g.fillPath (shade);
    }

    void house (juce::Graphics& g, float x, float y, float w, float h, juce::Colour wall, juce::Colour roof, float lamps, const Tint&)
    {
        g.setColour (wall);
        g.fillRect (x, y - h, w, h);
        juce::Path r;
        r.addTriangle (x - 1.5f, y - h, x + w / 2, y - h - h * 0.75f, x + w + 1.5f, y - h);
        g.setColour (roof);
        g.fillPath (r);
        if (lamps > 0.35f)
        {
            g.setColour (juce::Colour (0xfff0ae4c).withAlpha (juce::jmin (1.0f, (lamps - 0.35f) * 2.2f)));
            g.fillRect (x + w * 0.35f, y - h * 0.62f, juce::jmax (1.5f, w * 0.28f), juce::jmax (1.5f, h * 0.3f));
        }
    }

    void neutral (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 13, 95, 1.4f), H, t (juce::Colour (0xff8a9aa4), 0.55f), 0.06f);
        band (g, ridgePoints (r, W, hz + 16.0f * u, 9, 70, 1.2f), H, t (juce::Colour (0xff7d9070), 0.25f), 0.08f);
        band (g, ridgePoints (r, W, hz + 38.0f * u, 6, 60, 1.0f), H, t (juce::Colour (0xff6d8262), 0.1f), 0.08f);
    }

    // An angular broadleaf tree: a short trunk under a faceted crown
    void tree (juce::Graphics& g, float x, float y, float h, juce::Colour crown, juce::Colour trunk)
    {
        g.setColour (trunk);
        g.fillRect (x - 0.8f, y - h * 0.35f, 1.6f, h * 0.35f);
        juce::Path c;
        c.addQuadrilateral (x - h * 0.32f, y - h * 0.38f, x + h * 0.3f, y - h * 0.4f, x + h * 0.18f, y - h, x - h * 0.2f, y - h * 0.95f);
        g.setColour (crown);
        g.fillPath (c);
        juce::Path lit;
        lit.addTriangle (x - h * 0.32f, y - h * 0.38f, x - h * 0.2f, y - h * 0.95f, x, y - h * 0.6f);
        g.setColour (crown.brighter (0.15f));
        g.fillPath (lit);
    }

    void conifers (juce::Graphics& g, juce::Random& r, float W, float y0, float y1, float hmin, float hmax, float gapMin, float gapMax, juce::Colour col)
    {
        juce::Path p, lit;
        for (float x = 3; x < W; x += gapMin + (gapMax - gapMin) * r.nextFloat())
        {
            const float y = y0 + (y1 - y0) * r.nextFloat(), h = hmin + (hmax - hmin) * r.nextFloat();
            p.addTriangle (x - h * 0.3f, y, x, y - h, x + h * 0.3f, y);
            lit.addTriangle (x - h * 0.3f, y, x, y - h, x - h * 0.05f, y);
        }
        g.setColour (col);
        g.fillPath (p);
        g.setColour (col.brighter (0.12f));
        g.fillPath (lit);
    }

    void temperate (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 30, 120, 0.9f), H, t (juce::Colour (0xff7f93a6), 0.55f), 0.07f);
        band (g, ridgePoints (r, W, hz + 18.0f * u, 16, 75, 1.1f), H, t (juce::Colour (0xff7a9a62), 0.22f), 0.1f);
        // A few field patches on the near slope
        const juce::Colour fields[] = { juce::Colour (0xff98ad66), juce::Colour (0xff86a05c), juce::Colour (0xffa8b070) };
        for (int i = 0; i < 6; ++i)
        {
            const float x = W * r.nextFloat(), y = hz + 30.0f * u + 8 * r.nextFloat(), w = 40 + 50 * r.nextFloat();
            juce::Path f;
            f.addQuadrilateral (x, y, x + w, y - 2, x + w + 6, y + 6, x + 4, y + 8);
            g.setColour (t (fields[r.nextInt (3)], 0.1f));
            g.fillPath (f);
        }
        band (g, ridgePoints (r, W, hz + 44.0f * u, 5, 55, 1.0f), H, t (juce::Colour (0xff688a55), 0.06f), 0.08f);
        const auto crown = t (juce::Colour (0xff4f7a45), 0.08f), trunk = t (juce::Colour (0xff4d4034), 0.05f);
        for (float x = 10; x < W; x += 40 + 90 * r.nextFloat())
            tree (g, x, hz + 46.0f * u + 10 * r.nextFloat(), 14 + 8 * r.nextFloat(), crown, trunk);
    }

    void boreal (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, const SkyColours& c)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 20, 110, 0.9f), H, t (juce::Colour (0xff7d8ea3), 0.5f), 0.07f);
        // A distant lake catching the sky
        const auto lake = juce::Colour::fromFloatRGBA (c.sea[0], c.sea[1], c.sea[2], 1);
        juce::Path l;
        l.addQuadrilateral (W * 0.3f, hz + 10.0f * u, W * 0.62f, hz + 9.0f * u, W * 0.66f, hz + 16.0f * u, W * 0.27f, hz + 17.0f * u);
        g.setColour (lake.interpolatedWith (juce::Colours::white, 0.12f));
        g.fillPath (l);
        conifers (g, r, W, hz + 10.0f * u, hz + 18.0f * u, 8, 13, 5, 12, t (juce::Colour (0xff3f6150), 0.3f));
        band (g, ridgePoints (r, W, hz + 26.0f * u, 6, 80, 1.0f), H, t (juce::Colour (0xff5d7a62), 0.12f), 0.08f);
        conifers (g, r, W, hz + 42.0f * u, hz + 58.0f * u, 14, 24, 10, 26, t (juce::Colour (0xff2f4d40), 0.04f));
    }

    void savanna (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 18, 130, 1.0f), H, t (juce::Colour (0xff9a8a7a), 0.5f), 0.07f);
        band (g, ridgePoints (r, W, hz + 16.0f * u, 4, 90, 1.0f), H, t (juce::Colour (0xffc9a35a), 0.2f), 0.06f);
        band (g, ridgePoints (r, W, hz + 40.0f * u, 3, 90, 1.0f), H, t (juce::Colour (0xffbf9450), 0.06f), 0.06f);
        // Sparse flat-topped trees
        const auto dark = t (juce::Colour (0xff4a4a30), 0.08f);
        for (float x = 30; x < W; x += 110 + 160 * r.nextFloat())
        {
            const float y = hz + 22.0f * u + 26 * r.nextFloat(), h = 12 + 10 * (y - hz) / 50;
            g.setColour (dark);
            g.fillRect (x - 0.9f, y - h * 0.8f, 1.8f, h * 0.8f);
            juce::Path top;
            top.addQuadrilateral (x - h * 0.9f, y - h * 0.8f, x + h * 0.9f, y - h * 0.8f, x + h * 0.6f, y - h * 1.05f, x - h * 0.6f, y - h * 1.05f);
            g.fillPath (top);
        }
    }

    void mediterranean (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, float lamps)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 22, 110, 0.9f), H, t (juce::Colour (0xff8d8a9a), 0.5f), 0.07f);
        band (g, ridgePoints (r, W, hz + 20.0f * u, 16, 80, 1.0f), H, t (juce::Colour (0xffb7774f), 0.2f), 0.12f);
        // A hill village of pale walls and terracotta roofs
        const auto wall = t (juce::Colour (0xffeadfc8), 0.18f), roof = t (juce::Colour (0xffb5603e), 0.18f);
        for (int i = 0; i < 5; ++i)
            house (g, W * (0.18f + 0.03f * i) + 3 * r.nextFloat(), hz + 18.0f * u + 3 * r.nextFloat(), 7, 5, wall, roof, lamps, t);
        band (g, ridgePoints (r, W, hz + 40.0f * u, 6, 60, 1.0f), H, t (juce::Colour (0xff9a8a52), 0.06f), 0.1f);
        // Cypresses and round olive-like trees
        const auto green = t (juce::Colour (0xff4f6a45), 0.08f), trunk = t (juce::Colour (0xff4d4034), 0.05f);
        juce::Path cy;
        for (float x = W * 0.5f; x < W * 0.8f; x += 9 + 10 * r.nextFloat())
        {
            const float y = hz + 34.0f * u + 4 * r.nextFloat(), h = 12 + 6 * r.nextFloat();
            cy.addQuadrilateral (x - 1.8f, y, x + 1.8f, y, x + 0.8f, y - h, x - 0.8f, y - h);
        }
        g.setColour (green.darker (0.1f));
        g.fillPath (cy);
        for (float x = 20; x < W; x += 70 + 120 * r.nextFloat())
            tree (g, x, hz + 50.0f * u + 8 * r.nextFloat(), 12 + 6 * r.nextFloat(), green, trunk);
    }

    void wetland (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, const SkyColours& c)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 8, 90, 1.0f), H, t (juce::Colour (0xff7d8c8a), 0.5f), 0.05f);
        conifers (g, r, W, hz + 4.0f * u, hz + 8.0f * u, 5, 8, 18, 60, t (juce::Colour (0xff55665a), 0.4f));
        g.setColour (t (juce::Colour (0xff76855a), 0.15f));
        g.fillRect (0.0f, hz + 8.0f * u, W, H - hz - 8);
        // Winding water ribbons
        const auto water = juce::Colour::fromFloatRGBA (c.sea[0], c.sea[1], c.sea[2], 1).interpolatedWith (juce::Colours::white, 0.15f);
        for (int k = 0; k < 3; ++k)
        {
            juce::Path ribbon;
            const float y = hz + 14.0f * u + k * 15.0f, w = 2.5f + k * 2.5f;
            ribbon.startNewSubPath (-10, y);
            for (float x = -10; x <= W + 10; x += 40)
                ribbon.lineTo (x, y + (float) std::sin (x * 0.012f + k * 2.1f) * (4 + 3 * k));
            g.setColour (water);
            g.strokePath (ribbon, juce::PathStrokeType (w, juce::PathStrokeType::beveled));
        }
        // Reeds in the foreground
        juce::Path reeds;
        for (float x = 2; x < W; x += 3 + 7 * r.nextFloat())
        {
            if (r.nextFloat() < 0.45f) continue;
            const float y = hz + 56.0f * u + 6 * r.nextFloat(), h = 8 + 10 * r.nextFloat(), lean = (r.nextFloat() - 0.4f) * 4;
            reeds.addTriangle (x - 0.9f, y, x + 0.9f, y, x + lean, y - h);
        }
        g.setColour (t (juce::Colour (0xff4f6440), 0.04f));
        g.fillPath (reeds);
    }

    void openWater (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const SkyColours& c)
    {
        const auto sea = juce::Colour::fromFloatRGBA (c.sea[0], c.sea[1], c.sea[2], 1), deep = juce::Colour::fromFloatRGBA (c.seaDeep[0], c.seaDeep[1], c.seaDeep[2], 1);
        g.setGradientFill (juce::ColourGradient (sea, 0, hz, deep, 0, H, false));
        g.fillRect (0.0f, hz, W, H - hz);
        g.setColour (juce::Colour::fromFloatRGBA (c.horizon[0], c.horizon[1], c.horizon[2], 0.35f));
        g.fillRect (0.0f, hz, W, 1.5f);
        // Static faceted wave marks, larger towards the viewer
        juce::Path marks;
        for (int i = 0; i < 70; ++i)
        {
            const float d = std::pow (r.nextFloat(), 0.8f), y = hz + 4.0f * u + d * (H - hz - 6), len = 5 + 18 * d, x = r.nextFloat() * W;
            marks.addTriangle (x - len / 2, y, x + len * 0.15f, y - (1 + 2 * d), x + len / 2, y);
        }
        g.setColour (sea.interpolatedWith (juce::Colours::white, 0.35f).withAlpha (0.5f));
        g.fillPath (marks);
    }

    void coast (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, const SkyColours& c, float lamps)
    {
        // Headlands at the sides, open water in the middle
        auto far = ridgePoints (r, W, hz + 2.0f * u, 26, 80, 0.9f);
        for (auto& p : far)
        {
            const float m = std::abs (p.x / W - 0.5f) * 2; // 0 middle .. 1 edges
            p.y = hz + 2.0f * u - (hz + 2.0f * u - p.y) * juce::jlimit (0.0f, 1.0f, (m - 0.25f) * 1.6f);
        }
        band (g, far, H, t (juce::Colour (0xff76889a), 0.5f), 0.07f);
        const auto sea = juce::Colour::fromFloatRGBA (c.sea[0], c.sea[1], c.sea[2], 1), deep = juce::Colour::fromFloatRGBA (c.seaDeep[0], c.seaDeep[1], c.seaDeep[2], 1);
        g.setGradientFill (juce::ColourGradient (sea, 0, hz + 3.0f * u, deep, 0, hz + 34.0f * u, false));
        g.fillRect (0.0f, hz + 3.0f * u, W, 34.0f);
        g.setColour (sea.interpolatedWith (juce::Colours::white, 0.4f).withAlpha (0.45f));
        for (int i = 0; i < 18; ++i)
            g.fillRect (r.nextFloat() * W, hz + 6.0f * u + r.nextFloat() * 26, 6 + 14 * r.nextFloat(), 1.2f);
        auto shore = ridgePoints (r, W, hz + 40.0f * u, 7, 60, 1.0f);
        band (g, shore, H, t (juce::Colour (0xff6f8a68), 0.12f), 0.08f);
        const auto wall = t (juce::Colour (0xffe0d5bf), 0.15f), roof = t (juce::Colour (0xff9c5a44), 0.15f);
        for (int i = 0; i < 5; ++i)
            house (g, W * (0.08f + 0.045f * i), hz + 46.0f * u + r.nextFloat() * 4, 7, 5, wall, roof, lamps, t);
        // A small lighthouse on the right headland
        const float lx = W * 0.9f, ly = hz + 4.0f * u;
        juce::Path tower;
        tower.addQuadrilateral (lx - 3, ly, lx + 3, ly, lx + 2, ly - 18, lx - 2, ly - 18);
        g.setColour (t (juce::Colour (0xffe9e3d6), 0.4f));
        g.fillPath (tower);
        g.setColour (t (juce::Colour (0xffb5513a), 0.4f));
        g.fillRect (lx - 2.6f, ly - 10, 5.2f, 3.0f);
        if (lamps > 0.3f)
        {
            g.setColour (juce::Colour (0xfff7d48a).withAlpha (juce::jmin (1.0f, lamps)));
            g.fillEllipse (lx - 2.5f, ly - 22, 5, 5);
        }
    }

    void city (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, float lamps)
    {
        auto blocks = [&] (float base, float minH, float maxH, float minW, float maxW, juce::Colour col, float windows) {
            juce::Path p;
            std::vector<juce::Rectangle<float>> rects;
            for (float x = -10; x < W + 10;)
            {
                const float w = minW + (maxW - minW) * r.nextFloat(), h = minH + (maxH - minH) * std::pow (r.nextFloat(), 1.6f);
                rects.push_back ({ x, base - h, w, h + (H - base) });
                x += w + (r.nextFloat() < 0.25f ? 3 + 8 * r.nextFloat() : 0);
            }
            for (auto& b : rects)
                p.addRectangle (b);
            g.setColour (col);
            g.fillPath (p);
            g.setColour (col.brighter (0.06f));
            for (auto& b : rects)
                if (b.getWidth() > 12) g.fillRect (b.getX(), b.getY(), b.getWidth() * 0.35f, b.getHeight());
            if (windows > 0.35f)
            {
                juce::RectangleList<float> lit;
                for (auto& b : rects)
                    for (float y = b.getY() + 4; y < base - 3; y += 6)
                        for (float x = b.getX() + 3; x < b.getRight() - 3; x += 5)
                            if (r.nextFloat() < 0.16f) lit.add (x, y, 2, 2);
                g.setColour (juce::Colour (0xfff0ae4c).withAlpha (juce::jmin (0.9f, (windows - 0.35f) * 2)));
                g.fillRectList (lit);
            }
        };
        blocks (hz + 4.0f * u, 6, 46, 10, 26, t (juce::Colour (0xff8a90a0), 0.55f), lamps * 0.7f);
        blocks (hz + 26.0f * u, 4, 30, 14, 34, t (juce::Colour (0xff5d6474), 0.2f), lamps);
        g.setColour (t (juce::Colour (0xff4a505c), 0.1f));
        g.fillRect (0.0f, hz + 48.0f * u, W, H - hz - 48);
    }

    void desert (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        g.setColour (t (juce::Colour (0xffd8b47c), 0.3f));
        g.fillRect (0.0f, hz, W, H - hz);
        band (g, ridgePoints (r, W, hz + 4.0f * u, 22, 140, 0.8f), H, t (juce::Colour (0xffd9b27a), 0.45f), 0.1f);
        band (g, ridgePoints (r, W, hz + 26.0f * u, 18, 120, 0.9f), H, t (juce::Colour (0xffcc9b5e), 0.15f), 0.14f);
        band (g, ridgePoints (r, W, hz + 48.0f * u, 8, 110, 1.0f), H, t (juce::Colour (0xffc28e52), 0.05f), 0.12f);
    }

    void alpine (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        // Jagged peaks: alternate high and low points, snow on the upper flanks
        std::vector<Pt> pts;
        bool up = r.nextBool();
        for (float x = -40; x <= W + 60; x += 55 + 50 * r.nextFloat(), up = ! up)
            pts.push_back ({ x, up ? hz - 28 - 46 * r.nextFloat() : hz - 2 - 14 * r.nextFloat() });
        const auto rock = t (juce::Colour (0xff7c86a0), 0.45f);
        band (g, pts, H, rock, 0.14f);
        const auto snowCol = t (juce::Colour (0xfff1f4f8), 0.3f);
        juce::Path caps;
        for (size_t i = 1; i + 1 < pts.size(); ++i)
        {
            const auto p = pts[i];
            if (p.y > hz - 30) continue;
            const float k = 0.42f;
            caps.addTriangle (p, p + (pts[i - 1] - p) * k, p + (pts[i + 1] - p) * k);
        }
        g.setColour (snowCol);
        g.fillPath (caps);
        band (g, ridgePoints (r, W, hz + 20.0f * u, 10, 70, 1.0f), H, t (juce::Colour (0xffb9c4d4), 0.2f), 0.08f);
        // A row of small pines
        juce::Path pines;
        for (float x = 4; x < W; x += 9 + 16 * r.nextFloat())
        {
            const float y = hz + 34.0f * u + 8 * r.nextFloat(), h = 9 + 7 * r.nextFloat();
            pines.addTriangle (x - h * 0.32f, y, x, y - h, x + h * 0.32f, y);
        }
        g.setColour (t (juce::Colour (0xff3f5a4e), 0.12f));
        g.fillPath (pines);
        band (g, ridgePoints (r, W, hz + 46.0f * u, 5, 60, 1.0f), H, t (juce::Colour (0xffd8e0ea), 0.06f), 0.06f);
    }

    void tropical (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, const SkyColours& c)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 18, 110, 1.0f), H, t (juce::Colour (0xff6f8f8a), 0.5f), 0.07f);
        auto canopy = [&] (float base, float rmin, float rmax, juce::Colour col) {
            juce::Path p, lit;
            for (float x = -10; x < W + 20;)
            {
                const float rr = rmin + (rmax - rmin) * r.nextFloat();
                juce::Path b;
                for (int k = 0; k <= 6; ++k)
                {
                    const float a = juce::MathConstants<float>::pi * (1 + k / 6.0f);
                    const Pt v { x + rr * std::cos (a), base + rr * 0.8f * std::sin (a) };
                    if (k == 0) b.startNewSubPath (v);
                    else b.lineTo (v);
                }
                b.lineTo (x + rr, H);
                b.lineTo (x - rr, H);
                b.closeSubPath();
                p.addPath (b);
                lit.addTriangle (x - rr, base, x - rr * 0.5f, base - rr * 0.69f, x, base - rr * 0.8f);
                x += rr * (1.1f + 0.4f * r.nextFloat());
            }
            g.setColour (col);
            g.fillPath (p);
            g.setColour (col.brighter (0.12f));
            g.fillPath (lit);
        };
        canopy (hz + 16.0f * u, 9, 16, t (juce::Colour (0xff4f7d58), 0.3f));
        // A river catching the sky
        const auto water = juce::Colour::fromFloatRGBA (c.sea[0], c.sea[1], c.sea[2], 1);
        juce::Path river;
        river.addQuadrilateral (W * 0.58f, hz + 18.0f * u, W * 0.64f, hz + 18.0f * u, W * 0.74f, hz + 34.0f * u, W * 0.62f, hz + 34.0f * u);
        g.setColour (water.interpolatedWith (juce::Colours::white, 0.15f));
        g.fillPath (river);
        canopy (hz + 40.0f * u, 10, 18, t (juce::Colour (0xff3d6a48), 0.08f));
        // Two palms
        for (float px : { W * 0.12f, W * 0.86f })
        {
            const float base = hz + 50.0f * u, top = hz + 14.0f * u;
            g.setColour (t (juce::Colour (0xff4a4036), 0.05f));
            juce::Path trunk;
            trunk.addQuadrilateral (px - 1.5f, base, px + 1.5f, base, px + 4.5f, top, px + 2.5f, top);
            g.fillPath (trunk);
            juce::Path fronds;
            for (int k = 0; k < 5; ++k)
            {
                const float a = -2.7f + k * 0.55f;
                fronds.addTriangle (px + 3.5f, top, px + 3.5f + 16 * std::cos (a), top + 16 * std::sin (a) + 4, px + 3.5f + 12 * std::cos (a + 0.25f), top + 12 * std::sin (a + 0.25f) + 6);
            }
            g.setColour (t (juce::Colour (0xff3e6b45), 0.05f));
            g.fillPath (fronds);
        }
    }

    void grassland (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t, float lamps)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 5, 40, 1.0f), H, t (juce::Colour (0xff6f8566), 0.45f), 0.06f);
        // Field strips, a little wider towards the viewer
        const juce::Colour fields[] = { juce::Colour (0xffb9b06a), juce::Colour (0xff8ea25e), juce::Colour (0xffc9b672), juce::Colour (0xff7f9658) };
        float y = hz + 5.0f * u;
        for (int row = 0; y < H; ++row)
        {
            const float h = 4 + row * 3.5f;
            for (float x = -10; x < W + 10;)
            {
                const float w = 60 + 160 * r.nextFloat();
                const float slant = (x - W / 2) * 0.05f;
                juce::Path f;
                f.addQuadrilateral (x, y, x + w, y, x + w + slant, y + h, x + slant, y + h);
                g.setColour (t (fields[r.nextInt (4)], juce::jmax (0.0f, 0.3f - row * 0.06f)));
                g.fillPath (f);
                x += w;
            }
            y += h;
        }
        // A barn, a silo and a windmill
        const float bx = W * 0.2f, by = hz + 16.0f * u;
        g.setColour (t (juce::Colour (0xffa4483a), 0.2f));
        g.fillRect (bx, by - 9, 16.0f, 9.0f);
        juce::Path roof;
        roof.addTriangle (bx - 2, by - 9, bx + 8, by - 16, bx + 18, by - 9);
        g.setColour (t (juce::Colour (0xff6e3b33), 0.2f));
        g.fillPath (roof);
        g.setColour (t (juce::Colour (0xffcfc9bb), 0.2f));
        g.fillRect (bx + 19, by - 18, 6.0f, 18.0f);
        if (lamps > 0.4f)
        {
            g.setColour (juce::Colour (0xfff0ae4c).withAlpha (juce::jmin (0.9f, lamps - 0.3f)));
            g.fillRect (bx + 6, by - 6, 3.0f, 3.0f);
        }
        const float wx = W * 0.84f, wy = hz + 22.0f * u;
        g.setColour (t (juce::Colour (0xff59606a), 0.2f));
        juce::Path tower;
        tower.addQuadrilateral (wx - 3, wy, wx + 3, wy, wx + 0.8f, wy - 30, wx - 0.8f, wy - 30);
        g.fillPath (tower);
        for (int k = 0; k < 3; ++k)
        {
            const float a = 0.6f + k * 2.094f;
            g.drawLine (wx, wy - 30, wx + 11 * std::cos (a), wy - 30 + 11 * std::sin (a), 1.6f);
        }
    }

    void tundra (juce::Graphics& g, juce::Random& r, float W, float H, float hz, const Tint& t)
    {
        band (g, ridgePoints (r, W, hz + 2.0f * u, 22, 70, 0.8f), H, t (juce::Colour (0xffaebfd6), 0.45f), 0.12f);
        band (g, ridgePoints (r, W, hz + 22.0f * u, 9, 90, 1.0f), H, t (juce::Colour (0xffd5e0ec), 0.2f), 0.08f);
        band (g, ridgePoints (r, W, hz + 42.0f * u, 4, 120, 1.0f), H, t (juce::Colour (0xffe6edf4), 0.06f), 0.05f);
    }
} // namespace

void drawLandscape (juce::Graphics& g, Landscape l, float W, float H, float hz, const SkyColours& c, const Environment& e)
{
    u = (H - hz) / 64.0f;
    Tint t;
    t.horizon = juce::Colour::fromFloatRGBA (c.horizon[0], c.horizon[1], c.horizon[2], 1);
    t.light = juce::jlimit (0.22f, 1.0f, 0.3f + 0.75f * lum (c.ambient));
    t.snow = (float) juce::jlimit (0.0, 1.0, e.accent * (1 - e.accentIce * 0.5));
    const float lamps = (float) e.lamps;
    juce::Random r (101 + (int) l * 977); // the same backdrop every time for a given style
    // Ground under everything first, so nothing of the sky shows below the horizon
    g.setColour (t (juce::Colour (0xff6c7d68), 0.3f));
    g.fillRect (0.0f, hz, W, H - hz);
    switch (l)
    {
        case Landscape::city: city (g, r, W, H, hz, t, lamps); break;
        case Landscape::temperate: temperate (g, r, W, H, hz, t); break;
        case Landscape::boreal: boreal (g, r, W, H, hz, t, c); break;
        case Landscape::tropical: tropical (g, r, W, H, hz, t, c); break;
        case Landscape::grassland: grassland (g, r, W, H, hz, t, lamps); break;
        case Landscape::savanna: savanna (g, r, W, H, hz, t); break;
        case Landscape::desert: desert (g, r, W, H, hz, t); break;
        case Landscape::mediterranean: mediterranean (g, r, W, H, hz, t, lamps); break;
        case Landscape::alpine: alpine (g, r, W, H, hz, t); break;
        case Landscape::tundra: tundra (g, r, W, H, hz, t); break;
        case Landscape::coast: coast (g, r, W, H, hz, t, c, lamps); break;
        case Landscape::wetland: wetland (g, r, W, H, hz, t, c); break;
        case Landscape::openWater: openWater (g, r, W, H, hz, c); break;
        case Landscape::neutral:
        default: neutral (g, r, W, H, hz, t); break;
    }
}
} // namespace atmos::gui
