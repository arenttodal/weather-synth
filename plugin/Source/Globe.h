#pragma once
#include "Day.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace atmos
{
// Orthographic globe: drag to spin, scroll to zoom, click to drop a pin.
class Globe : public juce::Component
{
public:
    Globe();
    std::function<void (double lat, double lon)> onPin;

    void setPin (double lat, double lon, bool hasPin);
    // The currently loaded place: a different shape from the pin (not only a colour)
    void setCurrent (double lat, double lon, bool has);
    struct Palette
    {
        juce::Colour ocean { 0xff12202e }, land { 0xff2f4a3c }, coast { 0xff6f9a7d }, grid { 0x22ffffff }, pin { 0xffd9a54a };
        bool shaded = false; // soft light from the upper left, for the Explore browser
    };
    void setPalette (const Palette& p) { pal = p; repaint(); }
    std::function<void()> onDragged; // the view moved (no selection)

    // Natural Earth 110 m land outlines (public domain), shared by every globe. Coarse:
    // coasts are only good to tens of kilometres.
    static bool isLandAt (double lat, double lon);
    void setMarks (juce::Array<juce::Point<float>> lonLat) { marks = std::move (lonLat); repaint(); }
    void centreOn (double lat, double lon);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Projection helpers, public for tests
    bool project (double lat, double lon, juce::Point<float>& out) const; // false when on the far side
    bool unproject (juce::Point<float> screen, double& lat, double& lon) const;

private:
    struct Ring
    {
        std::vector<juce::Point<float>> lonLat;
    };
    const std::vector<Ring>& land;
    Palette pal;
    double curLat = 0, curLon = 0;
    bool hasCurrent = false;
    static const std::vector<Ring>& landRings();
    double lat0 = 30, lon0 = 10, zoom = 1.0;
    double pinLat = 0, pinLon = 0;
    bool hasPin = false;
    juce::Point<float> dragStart;
    double dragLat0 = 0, dragLon0 = 0;
    bool dragged = false;
    juce::Array<juce::Point<float>> marks;

    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radius() const { return (float) (juce::jmin (getWidth(), getHeight()) * 0.45 * zoom); }
};
} // namespace atmos
