#include "WeatherClient.h"
#include "Storage.h"

#ifndef ATMOS_VERSION_STRING
 #define ATMOS_VERSION_STRING "dev"
#endif

namespace atmos
{
WeatherClient::WeatherClient() : alive (std::make_shared<std::atomic<bool>> (true)) {}

WeatherClient::~WeatherClient()
{
    alive->store (false);
    pool.removeAllJobs (true, 8000);
}

void WeatherClient::run (std::function<void()> job) { pool.addJob (std::move (job)); }

juce::var WeatherClient::getJson (const juce::String& pathAndQuery, int& status, juce::String& error)
{
    status = 0;
    const auto base = Storage::relayUrl();
    if (base.isEmpty())
    {
        error = "No weather server set up yet";
        return {};
    }
    const juce::URL url (base + pathAndQuery);
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (7000)
                                             .withNumRedirectsToFollow (3)
                                             .withStatusCode (&status)
                                             .withExtraHeaders ("User-Agent: Atmospheric/" ATMOS_VERSION_STRING "\r\nAccept: application/json"));
    if (stream == nullptr)
    {
        error = "Couldn't reach the weather server";
        return {};
    }
    const auto body = stream->readEntireStreamAsString();
    auto json = juce::JSON::parse (body);
    if (status != 200)
    {
        const auto msg = json["message"].toString();
        error = msg.isNotEmpty() ? msg : "Weather server answered " + juce::String (status);
        return {};
    }
    if (! json.isObject()) error = "Weather server sent something unreadable";
    return json;
}

WeatherClient::Result WeatherClient::parseSky (const juce::var& j, int64_t now)
{
    Result r;
    if (! j.isObject() || ! j.hasProperty ("temp"))
    {
        r.error = "Weather reading was incomplete";
        return r;
    }
    Day d;
    d.source = "live";
    d.temp = (double) j["temp"];
    d.humidity = (double) j["humidity"];
    d.precip = juce::jlimit (0.0, 1.0, (double) j["precip"]);
    d.wind = (double) j["wind"];
    d.clouds = juce::jlimit (0.0, 1.0, (double) j["clouds"]);
    d.pressure = j.hasProperty ("pressure") ? (double) j["pressure"] : 1013.0;
    d.condition = j["condition"].toString();
    // Sun and moon use the moment we heard the sky, not when the station measured it
    d.observedAt = now;
    d.utcOffset = (int) j["timezone"];
    d.utcOffsetKnown = j.hasProperty ("timezone");
    const auto place = j["place"];
    d.placeName = place["name"].toString();
    d.country = place["country"].toString();
    d.lat = (double) place["lat"];
    d.lon = (double) place["lon"];
    auto v = d.toVar();
    for (auto* k : { "conditionIds", "windDeg", "gust", "visibility", "rain1h", "snow1h" })
        if (j.hasProperty (k) && ! j[k].isVoid()) v.getDynamicObject()->setProperty (k, j[k]);
    r.day = Day::fromVar (v); // clamps and validates everything
    r.ok = true;
    return r;
}

void WeatherClient::fetchHere (Callback cb)
{
    auto flag = alive;
    run ([flag, cb] {
        int status;
        juce::String err;
        const auto json = getJson ("/v1/sky", status, err);
        auto res = err.isEmpty() ? parseSky (json, juce::Time::currentTimeMillis() / 1000) : Result { false, {}, err };
        juce::MessageManager::callAsync ([flag, cb, res] {
            if (flag->load()) cb (res);
        });
    });
}

void WeatherClient::fetchAt (double lat, double lon, Callback cb)
{
    auto flag = alive;
    run ([flag, cb, lat, lon] {
        int status;
        juce::String err;
        const auto json = getJson ("/v1/sky?lat=" + juce::String (lat, 4) + "&lon=" + juce::String (lon, 4), status, err);
        auto res = err.isEmpty() ? parseSky (json, juce::Time::currentTimeMillis() / 1000) : Result { false, {}, err };
        if (res.ok)
        {
            // Keep the exact pin; the server rounds to its cache cell
            res.day.lat = lat;
            res.day.lon = lon;
        }
        juce::MessageManager::callAsync ([flag, cb, res] {
            if (flag->load()) cb (res);
        });
    });
}

void WeatherClient::geocode (const juce::String& q, std::function<void (juce::Array<Place>, juce::String)> cb)
{
    auto flag = alive;
    run ([flag, cb, q] {
        int status;
        juce::String err;
        const auto json = getJson ("/v1/geocode?q=" + juce::URL::addEscapeChars (q, true), status, err);
        juce::Array<Place> places;
        if (auto* arr = json["results"].getArray())
            for (auto& p : *arr)
                places.add ({ p["name"].toString(), p["country"].toString(), (double) p["lat"], (double) p["lon"] });
        juce::MessageManager::callAsync ([flag, cb, places, err] {
            if (flag->load()) cb (places, err);
        });
    });
}
} // namespace atmos
