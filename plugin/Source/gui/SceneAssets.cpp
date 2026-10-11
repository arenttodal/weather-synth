#include "SceneAssets.h"
#include "SceneArt.h"

namespace atmos::gui
{
namespace
{
    juce::String resourceName (const juce::String& path)
    {
        // juce_add_binary_data names resources after the file name with '.' and '-' as '_'
        return path.fromLastOccurrenceOf ("/", false, false).replaceCharacters (".-", "__");
    }
    juce::Point<float> pt (const juce::var& v, float w, float h) { return { (float) (double) v[0] * w, (float) (double) v[1] * h }; }
} // namespace

SceneAssets::SceneAssets()
{
    int size = 0;
    if (const char* data = SceneArt::getNamedResource ("manifest_json", size))
        manifest = juce::JSON::parse (juce::String::fromUTF8 (data, size));
    if (! manifest.isObject()) return;
    const auto vp = manifest["viewport"];
    vpW = (float) (double) vp.getProperty ("width", 1024);
    vpH = (float) (double) vp.getProperty ("height", 460);
    horizonY = (float) (double) vp.getProperty ("horizonY", 0.555);
    scale = (float) (double) vp.getProperty ("scale", 2);
    if (auto* a = manifest["artBox"].getArray(); a != nullptr && a->size() == 4)
        art = { (float) (double) (*a)[0], (float) (double) (*a)[1], (float) (double) (*a)[2], (float) (double) (*a)[3] };
    if (auto* obj = manifest["markers"].getDynamicObject())
        for (auto& p : obj->getProperties())
            markers[p.name.toString()] = pt (p.value, vpW, vpH);
    if (auto* f = manifest["mechanisms"]["vane"]["frames"].getArray()) vaneDefs = *f;
    if (auto* f = manifest["mechanisms"]["anemometer"]["frames"].getArray()) anemoDefs = *f;
    vaneN = vaneDefs.size();
    anemoN = anemoDefs.size();
    anemoStep = (float) (double) manifest["mechanisms"]["anemometer"].getProperty ("degreesPerFrame", 20.0);
    vaneSprites.resize ((size_t) vaneN);
    anemoSprites.resize ((size_t) anemoN);
    ok = ! art.isEmpty() && manifest["lighting"].getDynamicObject() != nullptr;
    if (const char* data = SceneArt::getNamedResource ("hero_json", size))
        hero = juce::JSON::parse (juce::String::fromUTF8 (data, size));
    if (auto* c = hero["clouds"].getArray())
        heroClouds = *c;
}

juce::Image SceneAssets::cachedExtra (const juce::String& file, size_t keep)
{
    if (file.isEmpty()) return {};
    auto it = extra.find (file);
    if (it == extra.end())
    {
        auto img = load (file);
        if (! img.isValid()) return {};
        it = extra.emplace (file, img).first;
    }
    extraLru.erase (std::remove (extraLru.begin(), extraLru.end(), file), extraLru.end());
    extraLru.push_back (file);
    while (extraLru.size() > keep)
    {
        extra.erase (extraLru.front());
        extraLru.erase (extraLru.begin());
    }
    return it->second;
}

namespace
{
    juce::Rectangle<float> boxOf (const juce::var& v)
    {
        if (auto* b = v.getArray(); b != nullptr && b->size() == 4)
            return { (float) (double) (*b)[0], (float) (double) (*b)[1], (float) (double) (*b)[2], (float) (double) (*b)[3] };
        return {};
    }
} // namespace

std::vector<std::pair<float, juce::Colour>> SceneAssets::skyRows (const juce::String& anchor) const
{
    std::vector<std::pair<float, juce::Colour>> out;
    if (auto* rows = hero["sky"][juce::Identifier (anchor)].getArray())
        for (auto& r : *rows)
            if (auto* c = r[1].getArray(); c != nullptr && c->size() == 3)
                out.push_back ({ (float) (double) r[0], juce::Colour ((juce::uint8) (int) (*c)[0], (juce::uint8) (int) (*c)[1], (juce::uint8) (int) (*c)[2]) });
    return out;
}

juce::Rectangle<float> SceneAssets::heroBox (const juce::String& layer) const { return boxOf (hero["layers"][juce::Identifier (layer)]["box"]); }

juce::Image SceneAssets::heroLayer (const juce::String& layer, const juce::String& anchor)
{
    return cachedExtra (hero["layers"][juce::Identifier (layer)]["frames"][juce::Identifier (anchor)].toString(), 40);
}

juce::Rectangle<float> SceneAssets::heroCloudBox (int k) const { return k >= 0 && k < heroClouds.size() ? boxOf (heroClouds[k]["box"]) : juce::Rectangle<float> {}; }

juce::Image SceneAssets::heroCloud (int k, const juce::String& anchor)
{
    if (k < 0 || k >= heroClouds.size()) return {};
    // Five clouds x the anchors in a blend (two or three) stay decoded
    return cachedExtra (heroClouds[k]["frames"][juce::Identifier (anchor)].toString(), 40);
}

juce::Point<float> SceneAssets::marker (const juce::String& name) const
{
    auto it = markers.find (name);
    return it != markers.end() ? it->second : juce::Point<float> { vpW / 2, vpH / 2 };
}

juce::Image SceneAssets::load (const juce::String& file) const
{
    int size = 0;
    if (const char* data = SceneArt::getNamedResource (resourceName (file).toRawUTF8(), size))
        return juce::ImageFileFormat::loadFrom (data, (size_t) size);
    return {};
}

juce::Image SceneAssets::lighting (const juce::String& anchor)
{
    auto it = lights.find (anchor);
    if (it == lights.end())
    {
        const auto def = manifest["lighting"][juce::Identifier (anchor)];
        if (! def.isObject()) return {};
        auto img = load (def["file"].toString());
        if (! img.isValid()) return {};
        it = lights.emplace (anchor, img).first;
    }
    lru.erase (std::remove (lru.begin(), lru.end(), anchor), lru.end());
    lru.push_back (anchor);
    // Keep the three most recently used anchors decoded (two blending plus overcast)
    while (lru.size() > 3)
    {
        lights.erase (lru.front());
        lru.erase (lru.begin());
    }
    return it->second;
}

juce::Image SceneAssets::accentMask()
{
    if (! accent.isValid()) accent = load (manifest["masks"]["accent"]["file"].toString());
    return accent;
}

const SpriteFrame& SceneAssets::sprite (std::vector<SpriteFrame>& cache, const juce::Array<juce::var>& defs, int frame)
{
    static const SpriteFrame empty;
    if (defs.isEmpty()) return empty;
    frame = ((frame % defs.size()) + defs.size()) % defs.size();
    auto& s = cache[(size_t) frame];
    if (! s.image.isValid())
    {
        const auto d = defs[frame];
        s.image = load (d["file"].toString());
        if (auto* b = d["box"].getArray(); b != nullptr && b->size() == 4)
            s.box = { (float) (double) (*b)[0], (float) (double) (*b)[1], (float) (double) (*b)[2], (float) (double) (*b)[3] };
    }
    return s;
}

const SpriteFrame& SceneAssets::vane (int frame) { return sprite (vaneSprites, vaneDefs, frame); }
const SpriteFrame& SceneAssets::anemometer (int frame) { return sprite (anemoSprites, anemoDefs, frame); }

size_t SceneAssets::decodedBytes() const
{
    size_t n = 0;
    auto add = [&n] (const juce::Image& i) {
        if (i.isValid()) n += (size_t) i.getWidth() * (size_t) i.getHeight() * 4;
    };
    for (auto& l : lights)
        add (l.second);
    add (accent);
    for (auto& s : vaneSprites)
        add (s.image);
    for (auto& s : anemoSprites)
        add (s.image);
    for (auto& e : extra)
        add (e.second);
    return n;
}
} // namespace atmos::gui
