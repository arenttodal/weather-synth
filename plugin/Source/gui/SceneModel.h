#pragma once
// Snapshot adapter (accepted Day -> scene snapshot) and the developer fixtures.
#include "../Day.h"
#include "Environment.h"
#include <juce_core/juce_core.h>

namespace atmos::gui
{
enum class FeedStatus { dealing, live, estimate, restored, preview, previewLoading };

// Translate the processor's accepted Day into the scene's snapshot. 'now' is wall time, used
// only to decide whether a live reading has gone stale; the sky itself uses the Day's own
// observation time so the picture always matches the sound.
Snapshot makeSnapshot (const Day&, FeedStatus, bool dayIsCurrent, int64_t now);

// Header line pieces
juce::String placeLabel (const Snapshot&);
juce::String timeLabel (const Snapshot&); // local time of the observation, "18:42"
juce::String conditionLabel (const Snapshot&);

// ---- Fixtures: every visual state without the network (tests, benchmark, dev builds only)
struct Fixture
{
    const char* name;  // "rain", "fog", ...
    const char* title; // "Rain / showers"
    Snapshot snapshot;
};
const std::vector<Fixture>& fixtures();
const Fixture* findFixture (const juce::String& name);

// Lighting anchors and polar cases for the visual matrix: overrides sun, moon and time of day
struct TimeOfDay
{
    const char* name;
    double sunAlt;
    bool evening;
    double moonAlt, moonAz, moonPhase;
};
const std::vector<TimeOfDay>& timesOfDay();
const TimeOfDay* findTime (const juce::String& name);
void applyTime (Environment&, const TimeOfDay&);
} // namespace atmos::gui
