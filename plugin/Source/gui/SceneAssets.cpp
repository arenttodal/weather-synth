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
    if (auto* poly = manifest["shore"]["island"].getArray())
        for (auto& p : *poly)
            shorePoly.add (pt (p, vpW, vpH));
    if (auto* rocks = manifest["shore"]["rocks"].getArray())
        for (auto& r : *rocks)
        {
            juce::Array<juce::Point<float>> poly;
            if (auto* pts = r.getArray())
                for (auto& p : *pts)
                    poly.add (pt (p, vpW, vpH));
            rockPolys.add (poly);
        }
    if (auto* f = manifest["mechanisms"]["vane"]["frames"].getArray()) vaneDefs = *f;
    if (auto* f = manifest["mechanisms"]["anemometer"]["frames"].getArray()) anemoDefs = *f;
    vaneN = vaneDefs.size();
    anemoN = anemoDefs.size();
    anemoStep = (float) (double) manifest["mechanisms"]["anemometer"].getProperty ("degreesPerFrame", 20.0);
    vaneSprites.resize ((size_t) vaneN);
    anemoSprites.resize ((size_t) anemoN);
    ok = ! art.isEmpty() && manifest["lighting"].getDynamicObject() != nullptr;
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

bool SceneAssets::isLand (juce::Point<float> p)
{
    if (land.empty())
    {
        land.assign ((size_t) (landW * landH), 0);
        // Any lighting anchor has the same alpha; use whichever is decoded (or decode noon)
        auto img = lights.empty() ? lighting ("noon") : lights.begin()->second;
        if (img.isValid())
        {
            const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < landH; ++y)
                for (int x = 0; x < landW; ++x)
                {
                    const int px = (x * img.getWidth()) / landW, py = (y * img.getHeight()) / landH;
                    land[(size_t) (y * landW + x)] = bd.getPixelColour (px, py).getAlpha() > 128 ? 1 : 0;
                }
        }
    }
    if (! art.contains (p)) return false;
    const int x = juce::jlimit (0, landW - 1, (int) ((p.x - art.getX()) / art.getWidth() * landW));
    const int y = juce::jlimit (0, landH - 1, (int) ((p.y - art.getY()) / art.getHeight() * landH));
    return land[(size_t) (y * landW + x)] != 0;
}

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
    return n;
}
} // namespace atmos::gui
