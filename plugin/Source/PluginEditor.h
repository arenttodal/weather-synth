#pragma once
#include "Globe.h"
#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/SceneView.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace atmos
{
struct Palette
{
    static inline const juce::Colour bg { 0xff172b40 }, panel { 0xff13233a }, line { 0xff2a3f5a }, ink { 0xffeee6d3 }, ink2 { 0xffa9b3bf },
        ink3 { 0xff7a8794 }, accent { 0xfff0ae4c };
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

    // Benchmark and fixture hooks (AtmosGuiBench, AtmosTests): "fixture" or "fixture@time"
    void applyBenchOptions (const juce::String& fixture, const juce::String& quality);
    int benchFrames() const { return scene.frames(); }
    double benchUpdateP95Ms() const { return scene.updateP95Ms(); }
    double benchPaintP95Ms() const { return scene.paintP95Ms(); }
    atmos::gui::SceneView& sceneView() { return scene; }
    juce::OwnedArray<atmos::gui::MacroTrack>& macroTracks() { return tracks; }
    atmos::gui::HeaderBar& headerBar() { return header; }

    static constexpr int logicalWidth = 1024, logicalHeight = 682, headerH = 44, sceneH = 460;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void applySnapshot (const atmos::gui::Snapshot&, bool immediate);
    void showDaysMenu();
    void showKeepPanel();
    void showPlacePanel();
    void showSettingsMenu();
    void applyVisualPrefs();
    void layoutGlobe();
    void toast (const juce::String&);

    AtmosProcessor& proc;
    atmos::gui::LookAndFeel lnf;
    atmos::gui::HeaderBar header;
    atmos::gui::SceneView scene;
    juce::OwnedArray<atmos::gui::MacroTrack> tracks;
    juce::Label toastLabel;
    juce::uint32 toastUntil = 0;
    atmos::InlinePanel panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    juce::String sceneKey;
    bool fixtureMode = false;
    juce::String fixtureClock;
    int devFixture = -1, devTime = -1;

    // Admin globe (Cmd/Ctrl+Shift+G, Cmd/Ctrl+W)
    atmos::Backdrop globeLayer;
    atmos::Globe globe;
    juce::Label globeTitle, globeInfo;
    juce::TextButton globeKeep { "Keep this Day" }, globeBack { "Back to my sky" }, globeClose { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosEditor)
};
