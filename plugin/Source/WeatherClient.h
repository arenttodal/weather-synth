#pragma once
#include "Day.h"
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace atmos
{
// Talks to the Atmospheric relay (server/ in this repo). All network work runs
// on a background thread; callbacks arrive on the message thread, and never
// after this object is destroyed.
class WeatherClient
{
public:
    struct Result
    {
        bool ok = false;
        Day day;
        juce::String error;
    };
    struct Place
    {
        juce::String name, country;
        double lat = 0, lon = 0;
    };
    using Callback = std::function<void (Result)>;

    WeatherClient();
    ~WeatherClient();

    // The caller's own sky, placed by IP on the server
    void fetchHere (Callback);
    // A specific place (home override, globe pin)
    void fetchAt (double lat, double lon, Callback);
    void geocode (const juce::String& query, std::function<void (juce::Array<Place>, juce::String)>);

    static Result parseSky (const juce::var& json, int64_t nowUnix);

private:
    void run (std::function<void()> job);
    static juce::var getJson (const juce::String& pathAndQuery, int& status, juce::String& error);

    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive;
};
} // namespace atmos
