#pragma once
#include "Globe.h"
#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace atmos
{
struct Palette
{
    static inline const juce::Colour bg { 0xff0e141b }, panel { 0xff151d27 }, line { 0xff2a3644 }, ink { 0xffdfe6ee }, ink2 { 0xff9fb0c2 },
        ink3 { 0xff6c7c8d }, accent { 0xffd9a54a };
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
};

// The sky card: today's conditions, drawn as weather
class DayCard : public juce::Component, private juce::Timer
{
public:
    DayCard();
    void setDay (const Day&, const juce::String& waveform);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onTripleClick;

private:
    void timerCallback() override;
    Day day;
    Climate climate;
    juce::String wave;
    struct Drop
    {
        float x, y, v;
    };
    std::vector<Drop> drops;
    juce::Random rng;
    int clickCount = 0;
    juce::uint32 lastClick = 0;
};

class MacroKnob : public juce::Component
{
public:
    MacroKnob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label);
    void setReadout (const juce::String& s) { readout.setText (s, juce::dontSendNotification); }
    void resized() override;
    juce::Slider slider;

private:
    juce::Label name, readout;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

struct Backdrop : juce::Component
{
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xf20b1016)); }
};

// Small overlay with a title, a text field and actions
class InlinePanel : public juce::Component
{
public:
    InlinePanel();
    void paint (juce::Graphics&) override;
    void resized() override;
    void open (const juce::String& title, const juce::String& placeholder, const juce::String& primaryText);
    juce::TextEditor field;
    juce::TextButton primary, cancel, extra;
    juce::OwnedArray<juce::TextButton> results;
    juce::Label message;
    juce::String title;
    void setResults (const juce::StringArray& labels, std::function<void (int)> onPick);
};
} // namespace atmos

class AtmosEditor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
{
public:
    explicit AtmosEditor (AtmosProcessor&);
    ~AtmosEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void toggleGlobe();
    bool globeVisible() const { return globeLayer.isVisible(); }

    // Benchmark and fixture hooks (AtmosGuiBench, AtmosTests)
    void applyBenchOptions (const juce::String& /*fixture*/, const juce::String& /*quality*/) {}
    int benchFrames() const { return 0; }
    double benchUpdateP95Ms() const { return 0; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void showDaysMenu();
    void showKeepPanel();
    void showPlacePanel();
    void layoutGlobe();

    AtmosProcessor& proc;
    atmos::LookAndFeel lnf;
    atmos::DayCard card;
    juce::OwnedArray<atmos::MacroKnob> knobs;
    juce::Label title, status, natureLine;
    juce::TextButton keepBtn { "Keep this Day" }, daysBtn { "Days" }, placeBtn { "Place" }, todayBtn { "Hear today" };
    atmos::InlinePanel panel;

    // Admin globe (Cmd/Ctrl+Shift+G, Cmd/Ctrl+W, or triple-click the sky card)
    atmos::Backdrop globeLayer;
    atmos::Globe globe;
    juce::Label globeTitle, globeInfo;
    juce::TextButton globeKeep { "Keep this Day" }, globeBack { "Back to my sky" }, globeClose { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosEditor)
};
