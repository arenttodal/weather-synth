#pragma once
#include "Astro.h"
#include "ClimateMapper.h"
#include <juce_core/juce_core.h>

// juce::String treats plain literals as ASCII; non-ASCII text must say it is UTF-8
#define ATMOS_U8(s) juce::String (juce::CharPointer_UTF8 (s))

namespace atmos
{
// A Day is a frozen moment of sky: the weather snapshot plus where and when.
// Projects and the almanac store Days, never raw synth parameters, so a Day
// always sounds the same and survives mapping improvements (mappingVersion).
struct Day
{
    juce::String id;          // unique, for the almanac
    juce::String name;        // optional user label
    juce::String placeName, country;
    double lat = 63.43, lon = 10.39;
    int64_t observedAt = 0;   // unix seconds
    int utcOffset = 0;        // seconds
    bool utcOffsetKnown = false;
    juce::String source = "estimate"; // "live", "estimate", "globe"
    juce::String condition = "Clouds";
    double temp = 6, humidity = 80, precip = 0, wind = 4, clouds = 0.7, pressure = 1010;
    int mappingVersion = kMappingVersion;

    bool isValid() const { return observedAt > 0; }
    juce::String localDate() const;        // YYYY-MM-DD at the place
    Climate climate() const;               // adds sun, moon and seed
    juce::String autoTitle() const;        // "2026-10-08 · Trondheim · Drizzle 6 °C · Waxing gibbous"
    juce::String conditionText() const;    // "Light snow", "Downpour", ...

    juce::var toVar() const;
    static Day fromVar (const juce::var& v);
};

// The Day a live sky reading or offline estimate produces for a place and moment
Day estimateDay (double lat, double lon, const juce::String& placeName, int64_t when);
} // namespace atmos
