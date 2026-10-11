#pragma once
// The scene's shipped artwork (assets/weather_scene, compiled into the plugin as SceneArt).
//
// One instance is shared by every open editor in the process (juce::SharedResourcePointer):
// opened by the first editor, freed when the last one closes. Images are decoded on the
// message thread on first use, never in the audio callback; lighting anchors are kept in a
// small most-recently-used cache so only the anchors in use (plus one neighbour) stay decoded.
#include <juce_graphics/juce_graphics.h>
#include <map>

namespace atmos::gui
{
struct SpriteFrame
{
    juce::Image image;
    juce::Rectangle<float> box; // logical scene coordinates (1024 x 460 viewport)
};

class SceneAssets
{
public:
    SceneAssets();

    bool valid() const { return ok; }
    // Logical scene viewport the art was rendered for, and where the craft art sits in it
    float viewportWidth() const { return vpW; }
    float viewportHeight() const { return vpH; }
    float horizon() const { return horizonY; } // 0..1 from the top
    juce::Rectangle<float> artBox() const { return art; }
    juce::Point<float> marker (const juce::String& name) const; // logical coordinates

    juce::Image lighting (const juce::String& anchor); // decoded on demand (cached)
    juce::Image accentMask();
    const SpriteFrame& vane (int frame);
    const SpriteFrame& anemometer (int frame);
    int vaneFrames() const { return vaneN; }
    int anemometerFrames() const { return anemoN; }
    float anemometerDegreesPerFrame() const { return anemoStep; }

    // The hero environment (hero.json, design/blender/hero_env.py): per lighting anchor, the sky
    // as colours down the scene, the city and the horizon bands as layers, and the hero's clouds,
    // each decoded on demand into a small cache
    bool hasHero() const { return hero.isObject(); }
    std::vector<std::pair<float, juce::Colour>> skyRows (const juce::String& anchor) const; // (logical y, colour)
    juce::Rectangle<float> heroBox (const juce::String& layer) const;                       // scene coordinates
    juce::Image heroLayer (const juce::String& layer, const juce::String& anchor);
    int heroCloudCount() const { return heroClouds.size(); }
    juce::Rectangle<float> heroCloudBox (int k) const;
    juce::Image heroCloud (int k, const juce::String& anchor);

    size_t decodedBytes() const;

private:
    juce::Image load (const juce::String& file) const;
    const SpriteFrame& sprite (std::vector<SpriteFrame>&, const juce::Array<juce::var>&, int frame);
    juce::var manifest, hero;
    juce::Array<juce::var> heroClouds;
    std::map<juce::String, juce::Image> extra; // hero layers and clouds, keyed by file
    std::vector<juce::String> extraLru;
    juce::Image cachedExtra (const juce::String& file, size_t keep);
    bool ok = false;
    float vpW = 1024, vpH = 460, horizonY = 0.555f, scale = 2;
    juce::Rectangle<float> art;
    std::map<juce::String, juce::Point<float>> markers;
    std::map<juce::String, juce::Image> lights;
    std::vector<juce::String> lru;
    juce::Image accent;
    std::vector<SpriteFrame> vaneSprites, anemoSprites;
    juce::Array<juce::var> vaneDefs, anemoDefs;
    int vaneN = 0, anemoN = 0;
    float anemoStep = 20;
};
} // namespace atmos::gui
