#pragma once
#include "Day.h"
#include <juce_core/juce_core.h>

namespace atmos
{
// Files live in the per-user app data folder:
//   macOS  ~/Library/Application Support/Atmospheric/
//   Win    %APPDATA%\Atmospheric\      Linux  ~/.config/Atmospheric/
// settings.json can be edited by hand (e.g. to point at a different relay).
class Storage
{
public:
    static juce::File folder();

    // Relay URL: settings.json "relayUrl" wins over the one built into the plugin
    static juce::String relayUrl();
    static void setRelayUrl (const juce::String&);

    // Optional fixed home place instead of IP location
    struct Home
    {
        juce::String name;
        double lat = 0, lon = 0;
        bool set = false;
    };
    static Home home();
    static void setHome (const Home&);

    // Last good live reading, for offline starts
    static Day lastSky();
    static void setLastSky (const Day&);

    // The almanac of kept Days, newest first
    static juce::Array<Day> loadDays();
    static bool saveDay (Day day); // assigns an id when missing; replaces same id
    static bool deleteDay (const juce::String& id);

    // Display preferences (global, never stored in projects): animation "full" / "economy" / "still"
    struct VisualPrefs
    {
        juce::String animation = "economy"; // new installs: 12 fps (full is 24)
        bool reduceFlashes = false, reduceMotion = false;
    };
    static VisualPrefs visualPrefs();
    static void setVisualPrefs (const VisualPrefs&);

    // Tests point storage at a temp folder
    static void setFolderOverride (const juce::File&);

private:
    static juce::var readJson (const juce::File&);
    static bool writeJson (const juce::File&, const juce::var&);
    static bool writeDays (const juce::Array<Day>&);
    static juce::var settings();
    static void writeSettings (const juce::var&);
};
} // namespace atmos
