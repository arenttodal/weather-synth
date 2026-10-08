#include "BibleFile.h"
#include "BinaryData.h"
#include "Storage.h"

namespace atmos
{
CoreSound soundFromVar (const juce::var& v, bool& ok)
{
    CoreSound s;
    ok = false;
    if (! v.isObject()) return s;
    s.name = v["name"].toString().toStdString();
    const auto a = v["anchor"];
    if (a.isObject())
    {
        s.anchorTemp = juce::jlimit (-60.0, 60.0, (double) a.getProperty ("temp", 12.0));
        s.anchorWet = juce::jlimit (0.0, 1.0, (double) a.getProperty ("wet", 0.4));
        s.anchorLight = juce::jlimit (0.0, 1.0, (double) a.getProperty ("light", 0.5));
    }
    const auto params = v["params"];
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& info = paramInfo (i);
        const auto q = params[juce::Identifier (info.id)];
        const double home = juce::jlimit (info.min, info.max, q.isObject() ? (double) q.getProperty ("home", info.def) : info.def);
        s.home[i] = home;
        s.lo[i] = juce::jlimit (info.min, home, q.isObject() ? (double) q.getProperty ("lo", home) : home);
        s.hi[i] = juce::jlimit (home, info.max, q.isObject() ? (double) q.getProperty ("hi", home) : home);
    }
    ok = params.isObject();
    return s;
}

juce::var soundToVar (const CoreSound& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", juce::String (s.name));
    auto* a = new juce::DynamicObject();
    a->setProperty ("temp", s.anchorTemp);
    a->setProperty ("wet", s.anchorWet);
    a->setProperty ("light", s.anchorLight);
    o->setProperty ("anchor", juce::var (a));
    auto* p = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        auto* q = new juce::DynamicObject();
        q->setProperty ("home", s.home[i]);
        q->setProperty ("lo", s.lo[i]);
        q->setProperty ("hi", s.hi[i]);
        p->setProperty (paramInfo (i).id, juce::var (q));
    }
    o->setProperty ("params", juce::var (p));
    return juce::var (o);
}

std::vector<CoreSound> parseBible (const juce::var& json)
{
    std::vector<CoreSound> out;
    if (auto* arr = json["sounds"].getArray())
        for (auto& v : *arr)
        {
            bool ok;
            auto s = soundFromVar (v, ok);
            if (ok) out.push_back (std::move (s));
        }
    return out;
}

std::vector<CoreSound> loadBible (juce::String& source)
{
    const auto user = Storage::folder().getChildFile ("bible.json");
    if (user.existsAsFile())
    {
        auto b = parseBible (juce::JSON::parse (user.loadFileAsString()));
        if (! b.empty())
        {
            source = "your bible.json";
            return b;
        }
    }
    source = "built in";
    return parseBible (juce::JSON::parse (juce::String::fromUTF8 (BinaryData::bible_json, BinaryData::bible_jsonSize)));
}
} // namespace atmos
