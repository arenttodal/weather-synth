#pragma once
// The living island: a fixed-camera, layered 2.5D scene.
//
// Draw order: sky and horizon tint; stars, sun and moon; distant lightning and tornado;
// rear clouds; sea, wave marks and shore foam; island art (blended lighting anchors with
// surface accents); wind vane and anemometer sprites; lamps and musical indicators;
// precipitation; fog and dust veils. Controls and text are separate components, never
// drawn into the scene.
//
// Cost control: the background (sky + sun/moon + sea) and the island are cached at the
// display's physical resolution and rebuilt only when their inputs change (at most 4 Hz
// during a transition). Each frame draws the cached layers plus a bounded set of moving
// shapes. The timer runs only while the editor is showing, at 30 / 15 / 0 fps.
#include "Environment.h"
#include "SceneAssets.h"
#include "SceneModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace atmos::gui
{
class SceneView : public juce::Component, private juce::Timer
{
public:
    enum class Quality { full, economy, still };
    struct Prefs
    {
        Quality quality = Quality::full;
        bool reduceFlashes = false;
        bool reduceMotion = false;
    };
    // Optional musical feedback, polled once per frame (never pushed from the audio thread)
    struct Musical
    {
        float tone = 0, motion = 0, space = 0, activity = 0;
    };

    SceneView();
    ~SceneView() override;

    // A new accepted snapshot. 'immediate' skips the transition (restores, first open).
    void setSnapshot (const Snapshot&, bool immediate);
    void setTimeOverride (const TimeOfDay*); // fixtures only
    void setPrefs (const Prefs&);
    const Prefs& prefs() const { return prefs_; }
    std::function<Musical()> musicalSource;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { updateTimer(); }
    void parentHierarchyChanged() override { updateTimer(); }

    // Measurement hooks
    const Environment& shownEnvironment() const { return shown; }
    int frames() const { return frameCount; }
    double updateP95Ms() const;
    double paintP95Ms() const;
    int activeParticles() const { return activeCount; }
    static constexpr int maxParticles = 400;

private:
    void timerCallback() override;
    void updateTimer();
    void step (double dt);
    void retarget();
    juce::AffineTransform toComponent() const;

    void buildBackground (float physScale);
    void buildIsland (float physScale);
    void drawLightningAndTornado (juce::Graphics&);
    void drawClouds (juce::Graphics&);
    void drawSea (juce::Graphics&);
    void drawMechanisms (juce::Graphics&);
    void drawLamps (juce::Graphics&);
    void drawPrecipitation (juce::Graphics&);
    void drawVeils (juce::Graphics&);

    juce::SharedResourcePointer<SceneAssets> assets;
    Prefs prefs_;
    Snapshot snap;
    const TimeOfDay* timeOverride = nullptr;
    Environment from, target, shown;
    double transT = 1, transSeconds = 8;
    double clock = 0; // monotonic animation time, seconds

    // Cached layers
    juce::Image bgCache, islandCache;
    juce::String bgKey, islandKey;
    double lastBgBuild = -10, lastIslandBuild = -10;
    float cachedScale = 0;

    // Clouds: faceted templates built from the visual seed
    struct CloudShape
    {
        juce::Path tones[3]; // light, mid, shadow facets in a 1 x 1 box
    };
    std::vector<CloudShape> cloudShapes;
    struct Cloud
    {
        float x, y, w, h;
        int shape;
        float speed;
    };
    std::vector<Cloud> clouds;

    struct Wave
    {
        float x, y, len, phase;
    };
    std::vector<Wave> waves;

    struct Particle
    {
        float x, y, vx, vy, size, groundY, age;
        uint8_t kind; // 0 rain, 1 drizzle, 2 snow, 3 sleet pellet, 4 hail, 5 dust speck, 6 bounce
    };
    std::array<Particle, maxParticles> parts {};
    int activeCount = 0;
    struct Splash
    {
        float x, y, age;
    };
    std::array<Splash, 48> splashes {};
    int nextSplash = 0;

    juce::Random rng { 1 };
    double vaneAngle = 0, anemoPhase = 0;
    double nextBolt = 6, boltAge = 99;
    juce::Path bolt;
    float boltX = 0;

    int frameCount = 0;
    std::vector<float> updateMs, paintMs;
    double lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SceneView)
};
} // namespace atmos::gui
