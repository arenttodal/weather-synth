#pragma once
// Header bar, icon buttons and the five macro tracks. Real components with real text:
// nothing here is painted into the scene artwork.
#include "Environment.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace atmos::gui
{
struct Theme
{
    static inline const juce::Colour ui { 0xff172b40 }, band { 0xff13233a }, track { 0xff0c1626 }, line { 0xff2a3f5a }, text { 0xffeee6d3 },
        textDim { 0xffa9b3bf }, focus { 0xfff0ae4c }, live { 0xfff0a040 }, warn { 0xffe0756b };
    static juce::Font font (float h, bool bold = false);
    static juce::Font mono (float h);
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override { return Theme::font (15); }
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// A square icon button drawn as a path (no glyph fonts), with an accessible title
class IconButton : public juce::Button
{
public:
    enum class Icon { refresh, place, keep, days, settings };
    IconButton (Icon, const juce::String& title, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};

// One macro: a bounded vertical track, a distinctive handle, the weather anchor (0) and
// the current value, with its name and a readout underneath.
class MacroTrack : public juce::Component, private juce::KeyListener
{
public:
    enum class Handle { cube, wedge, pentagon };
    MacroTrack (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label, Handle, juce::Colour, const juce::String& tooltip);
    ~MacroTrack() override { slider.removeKeyListener (this); }
    void resized() override;
    void paint (juce::Graphics&) override;
    void setUiScale (float s);
    juce::Slider slider;
    Handle handle;
    juce::Colour colour;

private:
    juce::Label name, value;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    float scale = 1;
    void updateValue();
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
};

class HeaderBar : public juce::Component
{
public:
    HeaderBar();
    void setStatus (const juce::String& place, const juce::String& temp, const juce::String& time, const juce::String& condition, Source, bool offline, const juce::String& detail);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setUiScale (float s);
    IconButton refresh { IconButton::Icon::refresh, "Refresh sky", "Read today's sky again (the sound changes to today's Day)" };
    IconButton place { IconButton::Icon::place, "Place", "Choose where your sky comes from" };
    IconButton keep { IconButton::Icon::keep, "Keep this Day", "Save this Day, with its sound, to your kept Days" };
    IconButton days { IconButton::Icon::days, "Kept Days", "Open a Day you kept" };
    IconButton settings { IconButton::Icon::settings, "Display settings", "Animation and accessibility settings" };

private:
    juce::String placeText, tempText, timeText, conditionText, detailText;
    Source source = Source::loading;
    bool offline = false;
    float scale = 1;
    juce::Rectangle<float> statusArea;
};
} // namespace atmos::gui
