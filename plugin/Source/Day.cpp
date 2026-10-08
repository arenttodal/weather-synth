#include "Day.h"
#include <cmath>

namespace atmos
{
juce::String Day::localDate() const
{
    return juce::String (atmos::localDate (observedAt, lon, utcOffset, utcOffsetKnown));
}

Climate Day::climate() const
{
    Climate c;
    c.temp = temp;
    c.humidity = humidity;
    c.precip = precip;
    c.wind = wind;
    c.clouds = clouds;
    c.pressure = pressure;
    c.sun = sunElevation (lat, lon, observedAt);
    c.moon = moonPhase (observedAt);
    c.seed = daySeed (localDate().toStdString(), lat, lon);
    return c;
}

juce::String Day::conditionText() const
{
    if (precip < 0.02)
    {
        if (humidity >= 97) return "Fog";
        return clouds > 0.6 ? "Overcast" : clouds > 0.25 ? "Partly cloudy" : "Clear";
    }
    if (temp < 1) return precip < 0.25 ? "Light snow" : precip < 0.6 ? "Snow" : "Heavy snow";
    if (precip < 0.25) return "Drizzle";
    if (precip < 0.55) return "Rain";
    if (precip < 0.8) return "Downpour";
    return "Storm";
}

juce::String Day::autoTitle() const
{
    juce::String place = placeName.isNotEmpty() ? placeName : juce::String (lat, 2) + ", " + juce::String (lon, 2);
    return localDate() + ATMOS_U8 (" · ") + place + ATMOS_U8 (" · ") + conditionText() + " " + juce::String (juce::roundToInt (temp)) + ATMOS_U8 (" °C · ")
           + juce::String (moonPhaseName (moonPhase (observedAt)));
}

juce::var Day::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("place", placeName);
    o->setProperty ("country", country);
    o->setProperty ("lat", lat);
    o->setProperty ("lon", lon);
    o->setProperty ("observedAt", (juce::int64) observedAt);
    o->setProperty ("utcOffset", utcOffset);
    o->setProperty ("utcOffsetKnown", utcOffsetKnown);
    o->setProperty ("source", source);
    o->setProperty ("condition", condition);
    o->setProperty ("temp", temp);
    o->setProperty ("humidity", humidity);
    o->setProperty ("precip", precip);
    o->setProperty ("wind", wind);
    o->setProperty ("clouds", clouds);
    o->setProperty ("pressure", pressure);
    o->setProperty ("mappingVersion", mappingVersion);
    return juce::var (o);
}

Day Day::fromVar (const juce::var& v)
{
    Day d;
    if (! v.isObject()) return d;
    auto num = [&] (const char* k, double def) { return v.hasProperty (k) ? (double) v[k] : def; };
    d.id = v["id"].toString();
    d.name = v["name"].toString();
    d.placeName = v["place"].toString();
    d.country = v["country"].toString();
    d.lat = juce::jlimit (-90.0, 90.0, num ("lat", d.lat));
    d.lon = juce::jlimit (-180.0, 180.0, num ("lon", d.lon));
    d.observedAt = (int64_t) (juce::int64) v["observedAt"];
    d.utcOffset = (int) num ("utcOffset", 0);
    d.utcOffsetKnown = (bool) v["utcOffsetKnown"];
    d.source = v.hasProperty ("source") ? v["source"].toString() : juce::String ("estimate");
    d.condition = v["condition"].toString();
    d.temp = juce::jlimit (-90.0, 60.0, num ("temp", d.temp));
    d.humidity = juce::jlimit (0.0, 100.0, num ("humidity", d.humidity));
    d.precip = juce::jlimit (0.0, 1.0, num ("precip", d.precip));
    d.wind = juce::jlimit (0.0, 120.0, num ("wind", d.wind));
    d.clouds = juce::jlimit (0.0, 1.0, num ("clouds", d.clouds));
    d.pressure = juce::jlimit (850.0, 1090.0, num ("pressure", d.pressure));
    d.mappingVersion = (int) num ("mappingVersion", kMappingVersion);
    return d;
}

Day estimateDay (double lat, double lon, const juce::String& placeName, int64_t when)
{
    const auto w = simulateWeather (lat, lon, when);
    Day d;
    d.lat = lat;
    d.lon = lon;
    d.placeName = placeName;
    d.observedAt = when;
    d.source = "estimate";
    d.temp = std::round (w.temp * 10) / 10;
    d.humidity = std::round (w.humidity);
    d.precip = w.precip;
    d.wind = std::round (w.wind * 10) / 10;
    d.clouds = std::round (w.clouds * 100) / 100;
    d.pressure = std::round (w.pressure);
    d.condition = w.condition;
    return d;
}
} // namespace atmos
