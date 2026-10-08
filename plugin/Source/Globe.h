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
    std::vector<Ring> land;
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
