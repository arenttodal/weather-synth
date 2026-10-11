#pragma once
// The distant regional backdrop below the balloon: one or two low-contrast bands of flat,
// faceted silhouettes, drawn into the scene's cached background (so it costs nothing per
// frame). Stylised region types, never maps, skylines or landmarks.
#include "Environment.h"
#include <juce_graphics/juce_graphics.h>

namespace atmos::gui
{
// Fills everything below the horizon 'hz' (scene coordinates, W x H) and may rise above it
// (hills, peaks, roofs). Colours come from the sky so every time of day and weather fits;
// settled snow ('accent') whitens the ground, and lit windows follow the lamp level.
void drawLandscape (juce::Graphics&, Landscape, float W, float H, float hz, const SkyColours&, const Environment&);
} // namespace atmos::gui
