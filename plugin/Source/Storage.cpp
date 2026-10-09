#include "Storage.h"

#ifndef ATMOS_RELAY_URL
 #define ATMOS_RELAY_URL ""
#endif

namespace atmos
{
namespace
{
    juce::File& overrideFolder()
    {
        static juce::File f;
        return f;
    }
    juce::CriticalSection& fileLock()
    {
        static juce::CriticalSection cs;
        return cs;
    }
} // namespace

void Storage::setFolderOverride (const juce::File& f) { overrideFolder() = f; }

juce::File Storage::folder()
{
    if (overrideFolder() != juce::File()) return overrideFolder();
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile ("Application Support");
#endif
    return base.getChildFile ("Atmospheric");
}

juce::var Storage::readJson (const juce::File& f)
{
    const juce::ScopedLock sl (fileLock());
    if (! f.existsAsFile()) return {};
    return juce::JSON::parse (f.loadFileAsString());
}

bool Storage::writeJson (const juce::File& f, const juce::var& v)
{
    const juce::ScopedLock sl (fileLock());
    f.getParentDirectory().createDirectory();
    // Write to a temp file then swap, so a crash never leaves half a file
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (v, false))) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

juce::var Storage::settings()
{
    auto v = readJson (folder().getChildFile ("settings.json"));
    return v.isObject() ? v : juce::var (new juce::DynamicObject());
}

void Storage::writeSettings (const juce::var& v) { writeJson (folder().getChildFile ("settings.json"), v); }

juce::String Storage::relayUrl()
{
    const auto s = settings()["relayUrl"].toString().trim();
    juce::String url = s.isNotEmpty() ? s : juce::String (ATMOS_RELAY_URL).trim();
    while (url.endsWithChar ('/'))
        url = url.dropLastCharacters (1);
    return url;
}

void Storage::setRelayUrl (const juce::String& u)
{
    auto s = settings();
    s.getDynamicObject()->setProperty ("relayUrl", u);
    writeSettings (s);
}

Storage::Home Storage::home()
{
    Home h;
    const auto v = settings()["home"];
    if (v.isObject() && v.hasProperty ("lat") && v.hasProperty ("lon"))
    {
        h.name = v["name"].toString();
        h.lat = juce::jlimit (-90.0, 90.0, (double) v["lat"]);
        h.lon = juce::jlimit (-180.0, 180.0, (double) v["lon"]);
        h.set = true;
    }
    return h;
}

void Storage::setHome (const Home& h)
{
    auto s = settings();
    if (! h.set)
        s.getDynamicObject()->removeProperty ("home");
    else
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", h.name);
        o->setProperty ("lat", h.lat);
        o->setProperty ("lon", h.lon);
        s.getDynamicObject()->setProperty ("home", juce::var (o));
    }
    writeSettings (s);
}

Day Storage::lastSky() { return Day::fromVar (settings()["lastSky"]); }

void Storage::setLastSky (const Day& d)
{
    auto s = settings();
    s.getDynamicObject()->setProperty ("lastSky", d.toVar());
    writeSettings (s);
}

juce::Array<Day> Storage::loadDays()
{
    juce::Array<Day> out;
    const auto v = readJson (folder().getChildFile ("days.json"));
    if (auto* arr = v["days"].getArray())
        for (auto& d : *arr)
        {
            auto day = Day::fromVar (d);
            if (day.isValid() && day.id.isNotEmpty()) out.add (day);
        }
    std::sort (out.begin(), out.end(), [] (const Day& a, const Day& b) { return a.observedAt > b.observedAt; });
    return out;
}

bool Storage::writeDays (const juce::Array<Day>& days)
{
    juce::Array<juce::var> arr;
    for (auto& d : days)
        arr.add (d.toVar());
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("days", arr);
    return writeJson (folder().getChildFile ("days.json"), juce::var (o));
}

bool Storage::saveDay (Day day)
{
    const juce::ScopedLock sl (fileLock());
    if (day.id.isEmpty()) day.id = juce::Uuid().toDashedString();
    auto days = loadDays();
    for (int i = days.size(); --i >= 0;)
        if (days[i].id == day.id) days.remove (i);
    days.add (day);
    return writeDays (days);
}

bool Storage::deleteDay (const juce::String& id)
{
    const juce::ScopedLock sl (fileLock());
    auto days = loadDays();
    const int before = days.size();
    for (int i = days.size(); --i >= 0;)
        if (days[i].id == id) days.remove (i);
    return days.size() != before && writeDays (days);
}
Storage::VisualPrefs Storage::visualPrefs()
{
    auto v = settings()["visual"];
    VisualPrefs p;
    if (v.isObject())
    {
        const auto a = v["animation"].toString();
        if (a == "full" || a == "economy" || a == "still") p.animation = a;
        p.reduceFlashes = (bool) v["reduceFlashes"];
        p.reduceMotion = (bool) v["reduceMotion"];
    }
    return p;
}

void Storage::setVisualPrefs (const VisualPrefs& p)
{
    auto s = settings();
    auto* o = new juce::DynamicObject();
    o->setProperty ("animation", p.animation);
    o->setProperty ("reduceFlashes", p.reduceFlashes);
    o->setProperty ("reduceMotion", p.reduceMotion);
    s.getDynamicObject()->setProperty ("visual", juce::var (o));
    writeSettings (s);
}
} // namespace atmos
