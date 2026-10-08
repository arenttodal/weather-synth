#pragma once
#include "Bible.h"
#include <juce_core/juce_core.h>

namespace atmos
{
// Reads and writes the sound designer's export format:
// { "version": 1, "sounds": [ { "name", "anchor": {temp, wet, light}, "params": { id: {home, lo, hi} } } ] }
std::vector<CoreSound> parseBible (const juce::var& json);
juce::var soundToVar (const CoreSound&);
CoreSound soundFromVar (const juce::var&, bool& ok);

// The bible the plugin plays: bible.json in the app data folder if present (a designer
// export dropped there), else the one built into the plugin. 'source' says which.
std::vector<CoreSound> loadBible (juce::String& source);
} // namespace atmos
