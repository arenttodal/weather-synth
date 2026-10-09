#include "SceneView.h"
#include <cmath>

namespace atmos::gui
{
namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;
    juce::Colour rgb (const float* c, float a = 1.0f) { return juce::Colour::fromFloatRGBA (c[0], c[1], c[2], a); }
    float lum (const float* c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
    double wrap180 (double d) { return std::fmod (std::fmod (d + 180.0, 360.0) + 360.0, 360.0) - 180.0; }
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    float q (double v, double steps = 32) { return (float) (std::round (v * steps) / steps); }

    // The scene looks towards the equator, so the sun arcs across the sky from left to right
    double viewBearing (double lat) { return lat >= 0 ? 180.0 : 0.0; }

    juce::Colour veilColour (const Environment& e, const SkyColours& c)
    {
        auto col = rgb (c.horizon).interpolatedWith (juce::Colours::white, 0.35f);
        const float warm = (float) juce::jlimit (0.0, 1.0, e.haze * 0.6 + e.smoke);
        col = col.interpolatedWith (juce::Colour (0xffa69a86), warm * 0.6f);
        col = col.interpolatedWith (juce::Colour (0xffc9925a), (float) juce::jlimit (0.0, 1.0, e.dust * 1.2));
        col = col.interpolatedWith (juce::Colour (0xff8a8a88), (float) juce::jlimit (0.0, 1.0, e.ash));
        return col.withMultipliedBrightness (juce::jmax (0.35f, lum (c.ambient)));
    }

    double percentile (std::vector<float> v, double p)
    {
        if (v.empty()) return 0;
        std::sort (v.begin(), v.end());
        return v[(size_t) std::min<double> (v.size() - 1, p * v.size())];
    }
} // namespace

SceneView::SceneView()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false); // decoration: nothing here pretends to be a control
    setAccessible (false);
    lastTick = nowSeconds();
    for (auto& p : parts)
        p.age = -1; // spawn anywhere on first use (still pictures show weather too)
}

SceneView::~SceneView() { stopTimer(); }

void SceneView::setPrefs (const Prefs& p)
{
    prefs_ = p;
    transSeconds = p.reduceMotion ? 0.8 : 8.0;
    if (p.quality == Quality::still) transT = 1, shown = target;
    updateTimer();
    repaint();
}

void SceneView::setTimeOverride (const TimeOfDay* t)
{
    timeOverride = t;
    retarget();
    transT = 1;
    shown = target;
    repaint();
}

void SceneView::setSnapshot (const Snapshot& s, bool immediate)
{
    const bool seedChanged = s.visualSeed != snap.visualSeed || cloudShapes.empty();
    snap = s;
    from = shown;
    retarget();
    if (seedChanged)
    {
        // Deterministic layout from the snapshot's visual seed
        rng.setSeed ((juce::int64) s.visualSeed);
        cloudShapes.clear();
        for (int k = 0; k < 5; ++k)
        {
            // A bumpy top made from a few overlapping round lobes, a nearly flat base, and
            // three rows of facets between them: light on top, mid in the middle, shade below.
            CloudShape c;
            struct Lobe
            {
                float x, y, r;
            };
            std::vector<Lobe> lobes;
            const int nl = 3 + rng.nextInt (3);
            for (int i = 0; i < nl; ++i)
            {
                const float x = 0.18f + 0.64f * (nl == 1 ? 0.5f : i / (float) (nl - 1)) + (rng.nextFloat() - 0.5f) * 0.08f;
                const float r = 0.16f + 0.2f * rng.nextFloat() * (1.0f - std::abs (x - 0.5f));
                lobes.push_back ({ x, 0.82f, r });
            }
            const int cols = 11;
            std::vector<juce::Point<float>> top, mid, bot;
            for (int i = 0; i <= cols; ++i)
            {
                const float x = 0.02f + 0.96f * i / cols;
                float y = 1.0f;
                for (auto& l : lobes)
                {
                    const float dx = (x - l.x) / l.r;
                    if (std::abs (dx) < 1) y = juce::jmin (y, l.y - l.r * 2.2f * std::sqrt (1 - dx * dx));
                }
                y = juce::jlimit (0.0f, 0.97f, y + (rng.nextFloat() - 0.5f) * 0.05f);
                if (i == 0 || i == cols) y = 0.93f;
                top.push_back ({ x, y });
                mid.push_back ({ x + (rng.nextFloat() - 0.5f) * 0.03f, juce::jmin (0.95f, y + (1.0f - y) * (0.42f + 0.15f * rng.nextFloat())) });
                bot.push_back ({ x, 0.97f + 0.03f * rng.nextFloat() });
            }
            for (int i = 0; i < cols; ++i)
            {
                const int t0 = rng.nextFloat() < 0.8f ? 0 : 1;
                c.tones[t0].addTriangle (top[(size_t) i], top[(size_t) i + 1], mid[(size_t) i + 1]);
                c.tones[rng.nextFloat() < 0.6f ? 0 : 1].addTriangle (top[(size_t) i], mid[(size_t) i + 1], mid[(size_t) i]);
                c.tones[rng.nextFloat() < 0.7f ? 1 : 2].addTriangle (mid[(size_t) i], mid[(size_t) i + 1], bot[(size_t) i + 1]);
                c.tones[2].addTriangle (mid[(size_t) i], bot[(size_t) i + 1], bot[(size_t) i]);
            }
            cloudShapes.push_back (std::move (c));
        }
        waves.clear();
        const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
        for (int i = 0; i < 64; ++i)
        {
            const float t = std::pow (rng.nextFloat(), 0.8f);
            waves.push_back ({ rng.nextFloat() * W, hz + 5 + t * (H - hz - 8), 6 + 22 * t, rng.nextFloat() * 6.28f });
        }
    }
    // New weather: re-pick every particle (kind and position) so nothing from the previous
    // state lingers, and still pictures are recomputed
    for (auto& p : parts)
        p.age = -1;
    activeCount = 0;
    // Cloud layout follows cover: a few broad shapes, more under an overcast sky
    const int count = juce::jlimit (0, 9, (int) std::round (target.cloud * 6 + (target.overcast > 0.5 ? 3 : 0)));
    if ((int) clouds.size() != count)
    {
        juce::Random r ((juce::int64) snap.visualSeed + 99);
        const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
        clouds.clear();
        for (int i = 0; i < count; ++i)
        {
            const float w = (130 + 130 * r.nextFloat()) * (float) (1 + 0.5 * target.overcast);
            const float y = 0.05f * H + r.nextFloat() * (hz * 0.62f - 0.05f * H);
            clouds.push_back ({ (i + r.nextFloat() * 0.6f) / juce::jmax (1, count) * (W + w) - w / 2, y, w, w * 0.4f, r.nextInt (5), 0.6f + 0.6f * r.nextFloat() });
        }
    }
    if (immediate || prefs_.quality == Quality::still)
    {
        transT = 1;
        shown = target;
    }
    else
        transT = 0;
    updateTimer();
    repaint();
}

void SceneView::retarget()
{
    target = computeEnvironment (snap);
    if (timeOverride != nullptr) applyTime (target, *timeOverride);
}

void SceneView::updateTimer()
{
    const bool run = isShowing() && prefs_.quality != Quality::still;
    const int hz = prefs_.quality == Quality::economy ? 12 : 24;
    if (run && (! isTimerRunning() || getTimerInterval() != 1000 / hz))
    {
        lastTick = nowSeconds();
        startTimerHz (hz);
    }
    else if (! run && isTimerRunning())
        stopTimer();
}

void SceneView::timerCallback()
{
    const double now = nowSeconds();
    // Clamp long gaps (sleep, a stalled host) instead of simulating them
    const double dt = juce::jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    const double t0 = nowSeconds();
    step (dt);
    updateMs.push_back ((float) ((nowSeconds() - t0) * 1000.0));
    if (updateMs.size() > 512) updateMs.erase (updateMs.begin(), updateMs.begin() + 256);
    repaint();
}

bool SceneView::particlesFinite() const
{
    for (int i = 0; i < activeCount; ++i)
    {
        const auto& p = parts[(size_t) i];
        if (! std::isfinite (p.x) || ! std::isfinite (p.y) || p.x < -200 || p.x > 1300 || p.y < -200 || p.y > 700) return false;
    }
    return true;
}

double SceneView::updateP95Ms() const { return percentile (updateMs, 0.95); }
double SceneView::paintP95Ms() const { return percentile (paintMs, 0.95); }

void SceneView::step (double dt)
{
    clock += dt;
    if (transT < 1)
    {
        transT = juce::jmin (1.0, transT + dt / transSeconds);
        const double e = transT * transT * (3 - 2 * transT);
        shown = blend (from, target, e);
    }
    const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
    const double motion = prefs_.reduceMotion ? 0.3 : 1.0;
    const auto& e = shown;
    // Wind across the view: +1 blowing to the right, -1 to the left; unknown direction drifts gently right
    const double vb = viewBearing (snap.lat);
    const double across = e.windFromDeg >= 0 ? std::cos ((e.windFromDeg + 90 - vb) * pi / 180) : 0.35;

    for (auto& c : clouds)
    {
        c.x += (float) (across * (3 + e.windMps * 1.4) * c.speed * motion * dt);
        if (c.x > W + c.w * 0.6f) c.x = -c.w * 1.1f;
        if (c.x < -c.w * 1.2f) c.x = W + c.w * 0.5f;
    }

    // Vane turns towards the wind (pointing into it), with a little gust wobble
    if (e.windFromDeg >= 0)
    {
        double targetAng = 90 - e.windFromDeg + vb - 180;
        targetAng += std::sin (clock * 2.3) * std::min (12.0, (e.gustMps - e.windMps) * 1.5) * motion;
        vaneAngle += wrap180 (targetAng - vaneAngle) * juce::jmin (1.0, dt * 1.5);
    }
    anemoPhase = std::fmod (anemoPhase + dt * juce::jmin (900.0, 40 + e.windMps * 55 + (e.gustMps - e.windMps) * 20 * (0.5 + 0.5 * std::sin (clock * 1.7))) * motion, 360.0);

    // Precipitation pool
    const int cap = prefs_.quality == Quality::economy ? 160 : (prefs_.reduceMotion ? 140 : 300);
    const double load = std::max ({ e.rain, e.drizzle * 0.7, e.snow * 0.8, e.sleet, e.freezing, e.hail * 0.7, e.dust * 0.4 });
    activeCount = juce::jlimit (0, maxParticles, (int) std::round (cap * std::pow (load, 0.8)));
    const double total = e.rain + e.drizzle + e.snow + e.sleet + e.freezing + e.hail + e.dust * 0.4 + 1e-9;
    auto pickKind = [&] (float r) -> uint8_t {
        double acc = 0;
        const double weights[] = { e.rain + e.freezing, e.drizzle, e.snow + e.sleet * 0.5, e.sleet * 0.5, e.hail, e.dust * 0.4 };
        for (int k = 0; k < 6; ++k)
        {
            acc += weights[k] / total;
            if (r <= acc) return (uint8_t) k;
        }
        return 0;
    };
    auto spawn = [&] (Particle& p, bool anywhere) {
        p.kind = pickKind (rng.nextFloat());
        p.x = -60 + rng.nextFloat() * (W + 120);
        p.y = anywhere ? rng.nextFloat() * H : -10 - rng.nextFloat() * 40;
        p.groundY = hz + 4 + rng.nextFloat() * (H - hz - 4);
        p.age = 0;
        const float wx = (float) (across * e.windMps);
        switch (p.kind)
        {
            case 0: p.vy = 460 + rng.nextFloat() * 120; p.vx = juce::jlimit (-320.0f, 320.0f, wx * 16); p.size = 14 + 12 * rng.nextFloat(); break;
            case 1: p.vy = 260 + rng.nextFloat() * 60; p.vx = juce::jlimit (-200.0f, 200.0f, wx * 12); p.size = 6 + 4 * rng.nextFloat(); break;
            case 2: p.vy = 38 + rng.nextFloat() * 34; p.vx = wx * 5; p.size = 2.2f + 2.4f * rng.nextFloat(); break;
            case 3: p.vy = 240 + rng.nextFloat() * 60; p.vx = wx * 10; p.size = 1.8f + rng.nextFloat(); break;
            case 4: p.vy = 380 + rng.nextFloat() * 90; p.vx = wx * 8; p.size = 2.0f + 1.6f * rng.nextFloat(); break;
            case 5: p.vy = 6 + rng.nextFloat() * 10; p.vx = (float) (across * (20 + e.windMps * 6)); p.size = 2.6f + 2.8f * rng.nextFloat(); p.y = rng.nextFloat() * H; break;
            default: break;
        }
    };
    for (int i = 0; i < activeCount; ++i)
    {
        auto& p = parts[(size_t) i];
        if (p.age < 0)
        {
            spawn (p, true);
            continue;
        }
        p.age += (float) dt;
        const float sway = p.kind == 2 ? (float) std::sin (clock * 1.3 + i) * 12 : 0;
        p.x += (p.vx + sway) * (float) (dt * motion);
        p.y += p.vy * (float) (dt * (prefs_.reduceMotion ? 0.5 : 1.0));
        if (p.kind == 6) // hail bounce: up, then gone
        {
            p.vy += 900 * (float) dt;
            if (p.age > 0.35f) spawn (p, false);
            continue;
        }
        const bool landed = p.kind != 5 && p.y >= p.groundY;
        if (landed)
        {
            const bool sea = ! assets->isLand ({ p.x, p.y });
            if ((p.kind == 0 || p.kind == 4 || p.kind == 3) && sea && rng.nextFloat() < (p.kind == 0 ? 0.25f : 0.5f))
            {
                splashes[(size_t) nextSplash] = { p.x, p.y, 0 };
                nextSplash = (nextSplash + 1) % (int) splashes.size();
            }
            if (p.kind == 4 && rng.nextFloat() < 0.4f)
            {
                p.kind = 6;
                p.vy = -160;
                p.age = 0;
                continue;
            }
            spawn (p, false);
        }
        else if (p.x < -80 || p.x > W + 80 || p.y > H + 20)
            spawn (p, false);
    }
    for (auto& s : splashes)
        s.age += (float) dt;

    // Lightning: occasional, small and in the distance; no full-screen flashes
    boltAge += dt;
    if (e.thunder > 0.5 && ! prefs_.reduceMotion)
    {
        nextBolt -= dt;
        if (nextBolt <= 0)
        {
            nextBolt = 6 + rng.nextFloat() * 9;
            boltAge = 0;
            boltX = rng.nextBool() ? W * (0.06f + 0.18f * rng.nextFloat()) : W * (0.76f + 0.18f * rng.nextFloat());
            bolt.clear();
            float x = boltX, y = H * 0.2f;
            bolt.startNewSubPath (x, y);
            while (y < hz - 6)
            {
                x += (rng.nextFloat() - 0.5f) * 18;
                y += 10 + rng.nextFloat() * 14;
                bolt.lineTo (x, juce::jmin (y, hz - 4));
            }
        }
    }
    ++frameCount;
}

juce::AffineTransform SceneView::toComponent() const
{
    const float s = (float) getWidth() / assets->viewportWidth();
    return juce::AffineTransform::scale (s);
}

void SceneView::resized()
{
    bgKey.clear();
    islandKey.clear();
}

// ---------------------------------------------------------------- cached layers
void SceneView::buildBackground (float ps)
{
    const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
    const auto& e = shown;
    const auto c = skyColours (e);
    juce::Image img (juce::Image::RGB, (int) std::ceil (W * ps), (int) std::ceil (H * ps), false); // opaque: blits without blending
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (ps));

    g.setGradientFill (juce::ColourGradient (rgb (c.zenith), 0, 0, rgb (c.horizon), 0, hz, false));
    g.fillRect (0.0f, 0.0f, W, hz + 1);

    // Stars (deterministic), dimmed by cloud and moonlight
    if (e.stars > 0.02)
    {
        juce::Random r ((juce::int64) snap.visualSeed + 7);
        for (int i = 0; i < 46; ++i)
        {
            const float x = r.nextFloat() * W, y = r.nextFloat() * hz * 0.82f, a = (float) (e.stars * (0.35 + 0.65 * r.nextFloat()));
            g.setColour (juce::Colours::white.withAlpha (a));
            g.fillRect (x, y, 1.6f, 1.6f);
        }
    }

    const double vb = viewBearing (snap.lat);
    auto skyXY = [&] (double az, double alt) {
        const float x = (float) juce::jlimit (0.06 * W, 0.94 * W, W * (0.5 + wrap180 (az - vb) / 220.0));
        const float y = (float) (hz - juce::jlimit (-10.0, 62.0, alt) / 62.0 * (hz - 30));
        return juce::Point<float> { x, y };
    };
    const float veil = (float) (1 - 0.9 * e.overcast) * (float) (0.4 + 0.6 * e.visibility);

    // Sun: a simple faceted disc with a soft ring; larger and redder near the horizon
    if (e.sunAlt > -2 && veil > 0.03f)
    {
        const auto p = skyXY (e.sunAz, e.sunAlt);
        const float low = (float) juce::jlimit (0.0, 1.0, 1 - e.sunAlt / 20);
        const float r = 15 + 5 * low;
        const auto col = juce::Colour (0xffffd86b).interpolatedWith (juce::Colour (0xffff9a52), low);
        g.setColour (col.withAlpha (0.18f * veil));
        g.fillEllipse (p.x - r * 2, p.y - r * 2, r * 4, r * 4);
        juce::Path disc;
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) (i * pi / 4 + pi / 8);
            const juce::Point<float> v { p.x + r * std::cos (a), p.y + r * std::sin (a) };
            if (i == 0) disc.startNewSubPath (v);
            else disc.lineTo (v);
        }
        disc.closeSubPath();
        g.setColour (col.withAlpha (veil));
        g.fillPath (disc);
    }
    // Moon: the lit fraction from the date, visible only above the horizon
    if (e.moonAlt > 0 && veil > 0.03f)
    {
        const auto p = skyXY (e.moonAz, e.moonAlt);
        const float r = 15;
        const bool waxing = e.moonPhase < 0.5;
        const bool litRight = (snap.lat >= 0) == waxing;
        const float f = (float) juce::jlimit (0.0, 1.0, e.moonFraction);
        juce::Path m;
        m.addCentredArc (p.x, p.y, r, r, 0, litRight ? 0.0f : (float) pi, litRight ? (float) pi : (float) (2 * pi), true);
        const float rx = r * std::abs (1 - 2 * f);
        // Terminator: bulges into the lit side for a crescent, away from it for a gibbous moon
        const bool sameSide = f < 0.5f;
        const float start = litRight == sameSide ? (float) pi : 0.0f;
        m.addCentredArc (p.x, p.y, juce::jmax (0.01f, rx), r, 0, start, start + (float) pi * (litRight == sameSide ? -1.0f : 1.0f));
        m.closeSubPath();
        g.setColour (juce::Colour (0xffeee2bd).withAlpha (0.12f * veil * (float) (1 - e.daylight)));
        g.fillEllipse (p.x - r * 1.8f, p.y - r * 1.8f, r * 3.6f, r * 3.6f);
        g.setColour (juce::Colour (0xffeee2bd).withAlpha (veil * (float) (0.55 + 0.45 * (1 - e.daylight))));
        g.fillPath (m);
    }

    // Sea: flat colour deepening towards the viewer, a lighter line at the horizon
    g.setGradientFill (juce::ColourGradient (rgb (c.sea), 0, hz, rgb (c.seaDeep), 0, H, false));
    g.fillRect (0.0f, hz, W, H - hz);
    g.setColour (rgb (c.horizon).withAlpha (0.25f));
    g.fillRect (0.0f, hz, W, 1.5f);

    // Overcast: a flat diffuse deck across the upper sky (static, so it lives in the cache)
    if (e.overcast > 0.3)
    {
        const float l = juce::jmax (0.18f, lum (c.ambient));
        const auto deck = juce::Colour (0xffdfe3e7).interpolatedWith (rgb (c.horizon), 0.3f).withMultipliedBrightness (l * 0.86f);
        g.setGradientFill (juce::ColourGradient (deck.withAlpha ((float) (0.55 * e.overcast)), 0, 0, deck.withAlpha (0.0f), 0, hz * 0.9f, false));
        g.fillRect (0.0f, 0.0f, W, hz);
    }
    // Visibility veil: a clamped wash plus soft bands thickest at the horizon
    const float amount = (float) juce::jlimit (0.0, 1.0, 1 - e.visibility);
    if (amount > 0.02f)
    {
        const auto col = veilColour (e, c);
        g.setColour (col.withAlpha (juce::jmin (0.42f, amount * 0.55f + (float) (0.2 * e.dust + 0.12 * (e.haze + e.smoke)))));
        g.fillRect (0.0f, 0.0f, W, H);
        for (int k = 0; k < 2; ++k)
        {
            const float y = hz - 30 + k * 60;
            const float a = juce::jmin (0.35f, amount * (0.45f - k * 0.12f));
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.0f), 0, y - 40, col.withAlpha (a), 0, y, false));
            g.fillRect (0.0f, y - 40, W, 40.0f);
            g.setGradientFill (juce::ColourGradient (col.withAlpha (a), 0, y, col.withAlpha (0.0f), 0, y + 40, false));
            g.fillRect (0.0f, y, W, 40.0f);
        }
    }
    bgCache = img;
}

void SceneView::buildIsland (float ps)
{
    const auto box = assets->artBox();
    juce::Image img (juce::Image::ARGB, (int) std::ceil (box.getWidth() * ps), (int) std::ceil (box.getHeight() * ps), true);
    juce::Graphics g (img);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto dest = img.getBounds().toFloat();
    double acc = 0;
    for (auto& w : anchorWeights (shown))
    {
        if (w.weight < 0.004) continue;
        auto src = assets->lighting (w.name);
        if (! src.isValid()) continue;
        acc += w.weight;
        // Weighted average of aligned renders: draw each over the previous with its share
        g.setOpacity ((float) (w.weight / acc));
        g.drawImage (src, dest, juce::RectanglePlacement::stretchToFit);
    }
    g.setOpacity (1);
    const auto c = skyColours (shown);
    if (shown.accent > 0.01)
    {
        auto mask = assets->accentMask();
        if (mask.isValid())
        {
            const auto snow = juce::Colour (0xfff4f7fa).interpolatedWith (juce::Colour (0xffcfeaf7), (float) shown.accentIce);
            const float light = 0.45f + 0.55f * juce::jmax (lum (c.ambient), (float) shown.daylight);
            g.setColour (snow.withMultipliedBrightness (light).withAlpha ((float) juce::jlimit (0.0, 1.0, shown.accent)));
            g.drawImage (mask, dest, juce::RectanglePlacement::stretchToFit, true);
        }
    }
    // Storms darken the island a little beyond what the overcast render already does
    const float dark = (float) juce::jlimit (0.0, 0.4, 0.22 * std::max ({ shown.rain, shown.freezing, shown.hail }) + 0.15 * shown.thunder);
    // Storms darken the island, fog and dust veil it (clamped: the cabinet must stay readable)
    const float amount = (float) juce::jlimit (0.0, 1.0, 1 - shown.visibility);
    if (dark > 0.01f || amount > 0.02f)
    {
        const auto copy = img.createCopy();
        if (dark > 0.01f)
        {
            g.setColour (juce::Colour (0xff101620).withAlpha (dark));
            g.drawImage (copy, dest, juce::RectanglePlacement::stretchToFit, true);
        }
        if (amount > 0.02f)
        {
            g.setColour (veilColour (shown, c).withAlpha (juce::jmin (0.4f, amount * 0.5f + (float) (0.15 * shown.dust + 0.1 * (shown.haze + shown.smoke)))));
            g.drawImage (copy, dest, juce::RectanglePlacement::stretchToFit, true);
        }
    }
    islandCache = img;
}

// ---------------------------------------------------------------- per-frame layers
void SceneView::drawLightningAndTornado (juce::Graphics& g)
{
    const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
    if (boltAge < 0.22 && ! bolt.isEmpty())
    {
        const float a = (float) (1 - boltAge / 0.22);
        if (! prefs_.reduceFlashes)
        {
            // Restrained local glow around the strike, not a screen flash
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f * a), boltX, hz * 0.5f, juce::Colours::transparentWhite, boltX + 140, hz * 0.5f, true));
            g.fillRect (boltX - 140, 0.0f, 280.0f, hz);
        }
        g.setColour (juce::Colour (0xfffff2b0).withAlpha ((prefs_.reduceFlashes ? 0.5f : 0.95f) * a));
        g.strokePath (bolt, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered));
    }
    if (shown.tornado > 0.05)
    {
        const float x = W * 0.86f, top = H * 0.16f, sway = (float) std::sin (clock * 0.8) * 6;
        juce::Path f;
        f.startNewSubPath (x - 34, top);
        f.lineTo (x + 34, top);
        f.lineTo (x + 6 + sway, hz - 2);
        f.lineTo (x - 2 + sway, hz - 2);
        f.closeSubPath();
        g.setColour (juce::Colour (0xff3a3f48).withAlpha ((float) (0.75 * shown.tornado * (0.4 + 0.6 * shown.visibility))));
        g.fillPath (f);
    }
}

void SceneView::drawClouds (juce::Graphics& g)
{
    if (clouds.empty() || shown.cloud < 0.02) return;
    const auto c = skyColours (shown);
    const float l = juce::jmax (0.18f, lum (c.ambient));
    const float storm = (float) juce::jlimit (0.0, 1.0, 0.5 * shown.thunder + 0.4 * std::max ({ shown.rain, shown.hail, shown.freezing }));
    const auto base = juce::Colour (0xffeef1f4).interpolatedWith (rgb (c.horizon), 0.25f).withMultipliedBrightness (l).interpolatedWith (juce::Colour (0xff4a5260), storm * 0.65f);
    const juce::Colour tones[3] = { base, base.withMultipliedBrightness (0.86f), base.withMultipliedBrightness (0.7f) };
    const float alpha = (float) juce::jlimit (0.0, 1.0, (0.35 + shown.cloud) * (0.45 + 0.55 * shown.visibility));
    // One fill per facet tone for all clouds together (far fewer edge tables per frame)
    juce::Path merged[3];
    for (auto& cl : clouds)
    {
        const auto& shape = cloudShapes[(size_t) cl.shape % cloudShapes.size()];
        const auto t = juce::AffineTransform::scale (cl.w, cl.h).translated (cl.x, cl.y);
        for (int k = 0; k < 3; ++k)
            merged[k].addPath (shape.tones[k], t);
    }
    for (int k = 0; k < 3; ++k)
    {
        g.setColour (tones[k].withAlpha (alpha));
        g.fillPath (merged[k]);
    }
}

void SceneView::drawSea (juce::Graphics& g)
{
    const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
    const auto& e = shown;
    const auto c = skyColours (e);
    const double vb = viewBearing (snap.lat);
    const double across = e.windFromDeg >= 0 ? std::cos ((e.windFromDeg + 90 - vb) * pi / 180) : 0.35;
    const double motion = prefs_.quality == Quality::still ? 0.0 : (prefs_.reduceMotion ? 0.3 : 1.0);
    const int n = juce::jlimit (0, (int) waves.size(), (int) (18 + e.waves * 46));
    juce::Path calm, caps;
    const float strength = (float) (0.7 + 0.9 * e.waves);
    for (int i = 0; i < n; ++i)
    {
        const auto& w = waves[(size_t) i];
        const float depth = (w.y - hz) / (H - hz);
        const float drift = (float) (across * (2 + e.windMps * 0.8) * (0.3 + depth) * clock * motion);
        float x = std::fmod (w.x + drift, W + 40.0f);
        if (x < -20) x += W + 40;
        const float bob = (float) (std::sin (clock * 1.4 * motion + w.phase) * 1.2);
        const float len = w.len * strength, h = (1.2f + 2.4f * depth) * strength;
        const bool cap = e.waves > 0.5 && (i % 3 == 0);
        auto& p = cap ? caps : calm;
        p.addTriangle (x - len / 2, w.y + bob, x + len * 0.15f, w.y + bob - (cap ? h * 2.2f : h), x + len / 2, w.y + bob);
    }
    const auto seaLight = rgb (c.sea).interpolatedWith (juce::Colours::white, 0.35f);
    g.setColour (seaLight.withAlpha (0.55f));
    g.fillPath (calm);
    g.setColour (juce::Colours::white.withAlpha ((float) (0.45 + 0.4 * e.waves) * juce::jmax (0.35f, lum (c.ambient))));
    g.fillPath (caps);

    // Foam around the island and rocks (drawn under the island, so only the shore shows)
    auto ring = [&] (const juce::Array<juce::Point<float>>& poly, float grow) {
        if (poly.size() < 3) return;
        juce::Path p;
        juce::Point<float> c0;
        for (auto& v : poly)
            c0 += v;
        c0 /= (float) poly.size();
        for (int i = 0; i < poly.size(); ++i)
        {
            const auto v = poly[i] + (poly[i] - c0) * grow;
            if (i == 0) p.startNewSubPath (v);
            else p.lineTo (v);
        }
        p.closeSubPath();
        const float pulse = (float) (0.85 + 0.15 * std::sin (clock * 0.9 * motion));
        g.setColour (seaLight.withAlpha ((float) (0.35 + 0.35 * e.waves) * pulse));
        g.strokePath (p, juce::PathStrokeType ((float) (2.5 + 3.5 * e.waves), juce::PathStrokeType::curved));
    };
    ring (assets->shore(), 0.02f);
    for (auto& r : assets->rockShores())
        ring (r, 0.08f);

    // Splashes where rain meets open water
    juce::Path sp;
    for (auto& s : splashes)
        if (s.age < 0.25f)
        {
            const float r = 1.5f + s.age * 14;
            sp.addTriangle (s.x - r, s.y, s.x, s.y - r * 0.8f, s.x + r, s.y);
        }
    g.setColour (juce::Colours::white.withAlpha (0.5f * juce::jmax (0.4f, lum (c.ambient))));
    g.fillPath (sp);
}

void SceneView::drawMechanisms (juce::Graphics& g)
{
    const auto c = skyColours (shown);
    const float dark = juce::jlimit (0.0f, 0.75f, 1.0f - lum (c.ambient) * 1.05f);
    auto draw = [&] (const SpriteFrame& s) {
        if (! s.image.isValid()) return;
        // Clip to the sprite: the alpha-mask tint otherwise costs a pass over the whole clip area
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (s.box.expanded (1).getSmallestIntegerContainer());
        g.setColour (juce::Colours::black); // opaque: drawImage takes the colour's alpha as opacity
        g.drawImage (s.image, s.box, juce::RectanglePlacement::stretchToFit);
        if (dark > 0.02f)
        {
            g.setColour (juce::Colour (0xff121a2c).withAlpha (dark));
            g.drawImage (s.image, s.box, juce::RectanglePlacement::stretchToFit, true);
        }
    };
    if (assets->anemometerFrames() > 0)
        draw (assets->anemometer ((int) (anemoPhase / assets->anemometerDegreesPerFrame())));
    if (assets->vaneFrames() > 0)
    {
        const double a = std::fmod (vaneAngle + 3600, 360.0);
        draw (assets->vane ((int) std::lround (a / (360.0 / assets->vaneFrames()))));
    }
}

void SceneView::drawLamps (juce::Graphics& g)
{
    const auto& e = shown;
    auto glow = [&] (juce::Point<float> p, float r, juce::Colour col, float a) {
        if (a < 0.01f) return;
        g.setGradientFill (juce::ColourGradient (col.withAlpha (a), p.x, p.y, col.withAlpha (0.0f), p.x + r, p.y, true));
        g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
    };
    const auto amber = juce::Colour (0xfff0ae4c);
    const double motion = prefs_.quality == Quality::still ? 0.0 : 1.0;
    glow (assets->marker ("MarkerDoorLight"), 16, amber, (float) (0.15 + 0.45 * e.lamps));
    const float beaconPulse = (float) (0.85 + 0.15 * std::sin (clock * 2.2 * motion));
    glow (assets->marker ("MarkerBeacon"), 18, amber, (float) (0.2 + 0.4 * e.lamps) * beaconPulse);

    // Musical feedback: one restrained response per macro, fading when nothing plays
    Musical m;
    if (musicalSource) m = musicalSource();
    const float act = juce::jlimit (0.0f, 1.0f, m.activity);
    glow (assets->marker ("MarkerFaceLamp"), 9, amber, 0.12f + 0.45f * (m.tone + 1) / 2 * (0.4f + 0.6f * act));
    const float rate = 0.6f + 1.6f * (m.motion + 1);
    const float blink = (float) (0.5 + 0.5 * std::sin (clock * rate * 2 * pi * motion));
    glow (assets->marker ("MarkerFaceFader"), 7, juce::Colour (0xff8ee07c), act * (0.15f + 0.45f * blink));
    const auto tip = assets->marker ("MarkerAntennaTip");
    const float halo = (m.space + 1) / 2 * act;
    if (halo > 0.02f)
    {
        g.setColour (juce::Colour (0xffeee6d3).withAlpha (0.35f * halo));
        const float r = 6 + 10 * halo + (float) std::fmod (clock * 6 * motion, 8.0);
        g.drawEllipse (tip.x - r, tip.y - r, 2 * r, 2 * r, 1.2f);
    }
}

void SceneView::drawPrecipitation (juce::Graphics& g)
{
    if (activeCount == 0)
    {
        if (prefs_.quality == Quality::still && (shown.rain + shown.drizzle + shown.snow + shown.sleet + shown.freezing + shown.hail + shown.dust) > 0.01)
            step (0.0); // still mode: place a frozen, deterministic set of particles
        if (activeCount == 0) return;
    }
    const auto c = skyColours (shown);
    const float l = juce::jmax (0.35f, lum (c.ambient));
    juce::Path streaks, drizzle;
    juce::RectangleList<float> flakes, pellets;
    for (int i = 0; i < activeCount; ++i)
    {
        const auto& p = parts[(size_t) i];
        if (p.age < 0) continue;
        const float inv = 1.0f / juce::jmax (1.0f, std::hypot (p.vx, p.vy));
        switch (p.kind)
        {
            case 0: streaks.addLineSegment ({ p.x, p.y, p.x - p.vx * inv * p.size, p.y - p.vy * inv * p.size }, 1.6f); break;
            case 1: drizzle.addLineSegment ({ p.x, p.y, p.x - p.vx * inv * p.size, p.y - p.vy * inv * p.size }, 1.1f); break;
            case 2: flakes.add (p.x, p.y, p.size, p.size); break;
            case 3: case 4: case 6: pellets.add (p.x, p.y, p.size, p.size); break;
            case 5: pellets.add (p.x, p.y, p.size, p.size * 0.8f); break;
            default: break;
        }
    }
    const auto rain = juce::Colour (0xffe4eef6).interpolatedWith (juce::Colour (0xffd8f4ff), (float) juce::jmin (1.0, shown.freezing * 2));
    const float lr = juce::jmax (0.62f, l);
    g.setColour (rain.withMultipliedBrightness (lr).withAlpha (0.7f));
    g.fillPath (streaks);
    g.setColour (rain.withMultipliedBrightness (lr).withAlpha (0.55f));
    g.fillPath (drizzle);
    g.setColour (juce::Colours::white.withMultipliedBrightness (juce::jmax (0.55f, l)).withAlpha (0.9f));
    g.fillRectList (flakes);
    const auto pelletCol = shown.dust > 0.2 ? juce::Colour (0xffb98a52) : juce::Colour (0xfff2f6f8);
    g.setColour (pelletCol.withMultipliedBrightness (juce::jmax (0.5f, l)).withAlpha (0.85f));
    g.fillRectList (pellets);
}

void SceneView::drawVeils (juce::Graphics& g)
{
    // The static wash and bands are cached; in front, one band drifts slowly over the horizon
    const float W = assets->viewportWidth(), H = assets->viewportHeight(), hz = H * assets->horizon();
    const auto& e = shown;
    const float amount = (float) juce::jlimit (0.0, 1.0, 1 - e.visibility);
    if (amount < 0.05f) return;
    const auto col = veilColour (e, skyColours (e));
    const double motion = prefs_.quality == Quality::still ? 0.0 : (prefs_.reduceMotion ? 0.3 : 1.0);
    const float y = hz + 18 + (float) std::sin (clock * 0.12 * motion) * 10;
    const float a = juce::jmin (0.3f, amount * 0.35f);
    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.0f), 0, y - 30, col.withAlpha (a), 0, y, false));
    g.fillRect (0.0f, y - 30, W, 30.0f);
    g.setGradientFill (juce::ColourGradient (col.withAlpha (a), 0, y, col.withAlpha (0.0f), 0, y + 30, false));
    g.fillRect (0.0f, y, W, 30.0f);
}

void SceneView::paint (juce::Graphics& g)
{
    const double t0 = nowSeconds();
    g.fillAll (juce::Colour (0xff172b40));
    if (! assets->valid()) return;
    const float s = (float) getWidth() / assets->viewportWidth();
    const float ps = s * g.getInternalContext().getPhysicalPixelScaleFactor();
    const double since = clock;

    // Rebuild cached layers only when their inputs change (throttled during transitions)
    const auto c = skyColours (shown);
    const juce::String bk = juce::String (ps, 3) + "|" + juce::String (q (c.zenith[0], 64)) + juce::String (q (c.zenith[2], 64)) + juce::String (q (c.horizon[0], 64))
                            + juce::String (q (c.sea[1], 64)) + "|" + juce::String (q (shown.sunAlt, 2)) + juce::String (q (shown.sunAz, 1)) + juce::String (q (shown.moonAlt, 2))
                            + juce::String (q (shown.overcast)) + juce::String (q (shown.stars)) + juce::String (q (shown.visibility)) + juce::String (q (shown.dust))
                            + juce::String (q (shown.haze + shown.smoke)) + juce::String (q (shown.ash));
    juce::String ik = juce::String (ps, 3) + "|";
    for (auto& w : anchorWeights (shown))
        ik << w.name << q (w.weight) << ",";
    ik << q (shown.accent) << q (shown.accentIce) << q (shown.rain) << q (shown.thunder) << q (shown.visibility) << q (shown.dust) << q (shown.haze + shown.smoke) << q (shown.ash);
    const bool settled = transT >= 1;
    if (bk != bgKey && (settled || since - lastBgBuild > 0.25 || ! bgCache.isValid()))
    {
        buildBackground (ps);
        bgKey = bk;
        lastBgBuild = since;
    }
    if (ik != islandKey && (settled || since - lastIslandBuild > 0.25 || ! islandCache.isValid()))
    {
        buildIsland (ps);
        islandKey = ik;
        lastIslandBuild = since;
    }

    static const bool profile = juce::SystemStats::getEnvironmentVariable ("ATMOS_PROFILE", {}) == "1";
    static double acc[10] {};
    static int nprof = 0;
    double tp = nowSeconds();
    auto lap = [&] (int i) {
        if (! profile) return;
        const double n = nowSeconds();
        acc[i] += n - tp;
        tp = n;
    };
    g.reduceClipRegion (getLocalBounds());
    g.addTransform (juce::AffineTransform::scale (s));
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImageTransformed (bgCache, juce::AffineTransform::scale (1.0f / ps));
    lap (0);
    drawLightningAndTornado (g);
    drawClouds (g);
    lap (1);
    drawSea (g);
    lap (2);
    const auto box = assets->artBox();
    g.setOpacity (1.0f); // drawImage uses the current colour's alpha
    g.drawImageTransformed (islandCache, juce::AffineTransform::scale (1.0f / ps).translated (box.getX(), box.getY()));
    lap (3);
    drawMechanisms (g);
    lap (4);
    drawLamps (g);
    lap (5);
    drawPrecipitation (g);
    lap (6);
    drawVeils (g);
    lap (7);
    if (profile && ++nprof % 150 == 0)
    {
        std::fprintf (stderr, "ps=%.4f s=%.4f per frame ms: bg %.2f clouds %.2f sea %.2f island %.2f mech %.2f lamps %.2f precip %.2f veils %.2f\n", ps, s, acc[0] * 1000 / nprof,
                      acc[1] * 1000 / nprof, acc[2] * 1000 / nprof, acc[3] * 1000 / nprof, acc[4] * 1000 / nprof, acc[5] * 1000 / nprof, acc[6] * 1000 / nprof, acc[7] * 1000 / nprof);
    }
    paintMs.push_back ((float) ((nowSeconds() - t0) * 1000.0));
    if (paintMs.size() > 512) paintMs.erase (paintMs.begin(), paintMs.begin() + 256);
}
} // namespace atmos::gui
