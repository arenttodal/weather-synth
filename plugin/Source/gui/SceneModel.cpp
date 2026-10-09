#include "SceneModel.h"
#include <cmath>

namespace atmos::gui
{
Snapshot makeSnapshot (const Day& d, FeedStatus st, bool dayIsCurrent, int64_t now)
{
    Snapshot s;
    s.place = d.placeName.isNotEmpty() ? d.placeName.toStdString() : (juce::String (d.lat, 2) + ", " + juce::String (d.lon, 2)).toStdString();
    s.lat = std::isfinite (d.lat) ? juce::jlimit (-90.0, 90.0, d.lat) : 0.0;
    s.lon = std::isfinite (d.lon) ? juce::jlimit (-180.0, 180.0, d.lon) : 0.0;
    s.observedAt = d.observedAt > 0 ? d.observedAt : now;
    s.utcOffset = d.utcOffset;
    s.offsetKnown = d.utcOffsetKnown;
    s.tempC = d.temp;
    s.humidity = d.humidity;
    for (int id : d.conditionIds)
        s.conditionIds.push_back (id);
    s.conditionMain = d.condition.toStdString();
    s.cloud = d.clouds;
    s.precip = d.precip;
    s.rainMm = d.rain1h;
    s.snowMm = d.snow1h;
    s.windMps = d.wind;
    s.gustMps = d.gust;
    s.windFromDeg = d.windDeg;
    s.visibilityM = d.visibility;
    s.visualSeed = (uint32_t) (std::llabs ((long long) (d.lat * 1000)) * 7919 + std::llabs ((long long) (d.lon * 1000)) * 104729 + (uint32_t) (d.observedAt / 86400));

    switch (st)
    {
        case FeedStatus::preview:
        case FeedStatus::previewLoading: s.source = Source::preview; break;
        case FeedStatus::restored: s.source = Source::savedDay; break;
        case FeedStatus::dealing: s.source = Source::loading; break;
        case FeedStatus::estimate:
            s.source = Source::estimated;
            s.offline = true; // the estimate is the offline fallback
            break;
        case FeedStatus::live:
            s.source = (! dayIsCurrent || now - s.observedAt > 3600) ? Source::stale : Source::live;
            break;
    }
    if (d.source == "estimate" && st != FeedStatus::restored && st != FeedStatus::dealing) s.source = Source::estimated;
    return s;
}

juce::String placeLabel (const Snapshot& s) { return juce::String::fromUTF8 (s.place.c_str()); }

juce::String timeLabel (const Snapshot& s)
{
    // The provider's UTC offset at observation time already includes daylight saving;
    // estimates without one use solar time from the longitude (marked "~").
    const int64_t off = s.offsetKnown ? s.utcOffset : (int64_t) std::lround (s.lon / 15.0) * 3600;
    const int64_t local = s.observedAt + off;
    const int64_t secs = ((local % 86400) + 86400) % 86400;
    return (s.offsetKnown ? juce::String() : juce::String ("~")) + juce::String::formatted ("%02d:%02d", (int) (secs / 3600), (int) (secs / 60 % 60));
}

juce::String conditionLabel (const Snapshot& s)
{
    const auto c = classify (s);
    using P = Conditions::Precip;
    juce::String t;
    switch (c.precip)
    {
        case P::drizzle: t = "Drizzle"; break;
        case P::rain: t = c.rate > 0.75 ? "Heavy rain" : c.rate > 0.45 ? "Rain" : "Light rain"; break;
        case P::snow: t = c.rate > 0.7 ? "Heavy snow" : "Snow"; break;
        case P::sleet: t = "Sleet"; break;
        case P::freezingRain: t = "Freezing rain"; break;
        case P::hail: t = "Hail"; break;
        case P::none: break;
    }
    if (c.thunder) t = t.isEmpty() ? juce::String ("Thunder") : "Thunder, " + t.toLowerCase();
    if (t.isEmpty())
    {
        if (c.tornado) t = "Tornado";
        else if (c.squall) t = "Squalls";
        else if (c.fog > 0) t = "Fog";
        else if (c.mist > 0) t = "Mist";
        else if (c.smoke > 0) t = "Smoke";
        else if (c.haze > 0) t = "Haze";
        else if (c.ash > 0) t = "Ash";
        else if (c.dust > 0) t = "Dust";
        else if (c.frost) t = "Frost";
        else t = c.cloud > 0.85 ? "Overcast" : c.cloud > 0.3 ? "Partly cloudy" : "Clear";
    }
    return t;
}

// ---------------------------------------------------------------- fixtures
namespace
{
    Snapshot base (double temp, std::vector<int> ids, double cloud, double precip, double wind, double windFrom = 250)
    {
        Snapshot s;
        s.source = Source::live;
        s.place = "Fixture";
        s.lat = 62.57;
        s.lon = 7.69;
        s.observedAt = 1791460800; // 2026-10-08 10:40 UTC, a daytime moment; times are overridden by applyTime
        s.utcOffset = 7200;
        s.offsetKnown = true;
        s.tempC = temp;
        s.conditionIds = std::move (ids);
        s.cloud = cloud;
        s.precip = precip;
        s.windMps = wind;
        s.windFromDeg = windFrom;
        s.visualSeed = 12345;
        return s;
    }
} // namespace

const std::vector<Fixture>& fixtures()
{
    static const std::vector<Fixture> f = [] {
        std::vector<Fixture> v;
        v.push_back ({ "clear", "Clear", base (18, { 800 }, 0.0, 0, 3) });
        v.push_back ({ "partly_cloudy", "Partly cloudy", base (15, { 802 }, 0.4, 0, 4) });
        v.push_back ({ "overcast", "Overcast", base (11, { 804 }, 1.0, 0, 5) });
        v.push_back ({ "drizzle", "Drizzle", base (10, { 301 }, 0.9, 0.2, 4) });
        v.push_back ({ "rain", "Rain / showers", base (9, { 521 }, 0.95, 0.55, 7) });
        v.push_back ({ "heavy_rain", "Heavy rain", base (7, { 502 }, 1.0, 0.9, 10) });
        v.push_back ({ "thunderstorm", "Thunderstorm", base (9, { 201 }, 1.0, 0.6, 9) });
        v.push_back ({ "wind", "Wind / squalls", base (6, { 771, 803 }, 0.7, 0, 19, 280) });
        auto snow = base (-4, { 601 }, 1.0, 0.5, 4);
        v.push_back ({ "snow", "Snow", snow });
        v.push_back ({ "sleet", "Sleet / mixed", base (1, { 616 }, 1.0, 0.5, 6) });
        v.push_back ({ "freezing_rain", "Freezing rain", base (-1, { 511 }, 1.0, 0.5, 4) });
        auto hail = base (2, { 211 }, 1.0, 0.0, 8);
        hail.fixtureHail = true;
        v.push_back ({ "hail", "Hail (fixture only)", hail });
        auto fog = base (6, { 741 }, 0.9, 0, 1.5);
        fog.visibilityM = 300;
        v.push_back ({ "fog", "Mist / fog", fog });
        auto haze = base (14, { 711 }, 0.4, 0, 3);
        haze.visibilityM = 3000;
        v.push_back ({ "haze", "Haze / smoke", haze });
        auto dust = base (18, { 761 }, 0.3, 0, 12, 120);
        dust.visibilityM = 2000;
        v.push_back ({ "dust", "Dust / sand / ash", dust });
        v.push_back ({ "tornado", "Tornado / extreme wind", base (9, { 781, 502 }, 1.0, 0.7, 26, 230) });
        auto frost = base (-10, { 800 }, 0.15, 0, 2);
        frost.fixtureFrost = true;
        v.push_back ({ "frost", "Frost (fixture only)", frost });
        auto saved = base (7, { 803 }, 0.6, 0, 5);
        saved.source = Source::savedDay;
        saved.offline = true;
        v.push_back ({ "offline_saved", "Offline / saved day", saved });
        return v;
    }();
    return f;
}

const Fixture* findFixture (const juce::String& name)
{
    for (auto& f : fixtures())
        if (name == f.name) return &f;
    return nullptr;
}

const std::vector<TimeOfDay>& timesOfDay()
{
    static const std::vector<TimeOfDay> t = {
        { "dawn", "06:24", -3, false, -20, 0, 0.1 },          { "morning", "08:17", 16, false, -30, 0, 0.1 },
        { "noon", "12:03", 45, false, -40, 0, 0.1 },          { "late_afternoon", "16:21", 18, true, -25, 0, 0.1 },
        { "sunset", "20:38", 1, true, 8, 120, 0.3 },          { "dusk", "22:11", -6, true, 18, 140, 0.32 },
        { "night", "01:46", -20, true, 30, 200, 0.38 },       { "polar_day", "00:12", 5, true, -15, 0, 0.1 },
        { "polar_night", "15:09", -8, false, 22, 170, 0.6 },
    };
    return t;
}

const TimeOfDay* findTime (const juce::String& name)
{
    for (auto& t : timesOfDay())
        if (name == t.name) return &t;
    return nullptr;
}

void applyTime (Environment& e, const TimeOfDay& t)
{
    e.sunAlt = t.sunAlt;
    e.evening = t.evening;
    e.sunAz = t.evening ? 240 : 120;
    if (juce::String (t.name).startsWith ("polar")) e.sunAz = t.sunAlt > 0 ? 200 : 180;
    e.moonAlt = t.moonAlt;
    e.moonAz = t.moonAz;
    e.moonPhase = t.moonPhase;
    e.moonFraction = (1 - std::cos (t.moonPhase * 2 * juce::MathConstants<double>::pi)) / 2;
    const double day = juce::jlimit (0.0, 1.0, (t.sunAlt + 9) / 16);
    e.daylight = day * day * (3 - 2 * day);
    e.lamps = juce::jlimit (0.0, 1.0, 1.0 - 0.8 * e.daylight + 0.25 * e.overcast * e.daylight + 0.2 * e.thunder);
    e.stars = juce::jlimit (0.0, 1.0, (1 - e.daylight) * (1 - e.cloud) * e.visibility);
}
} // namespace atmos::gui
