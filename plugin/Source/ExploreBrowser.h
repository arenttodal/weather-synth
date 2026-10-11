#pragma once
// Explore Locations: search or pin a place, read its weather, hear what it would sound like,
// then deliberately load it.
//
// Two domains, never mixed: the committed instrument (the processor's Day, sound and scene,
// shown in the editor's header) and this browser's candidate. Searching, pinning and
// previewing only ever change the candidate; Load is the one action that changes the
// instrument, and it commits exactly the Day that was shown and auditioned.
#include "Audition.h"
#include "Globe.h"
#include "gui/Controls.h"
#include "gui/Environment.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ExploreBrowser : public juce::Component, private juce::Timer
{
public:
    explicit ExploreBrowser (AtmosProcessor&);
    ~ExploreBrowser() override;

    void open();  // shows the panel; the instrument carries on as it is
    void close(); // Cancel: drops the candidate only (and stops its audition)
    std::function<void()> onClosed;
    std::function<const atmos::gui::Landscape*()> landscapeOverride; // the user's scene style, for the thumbnail
    void setUiScale (float s);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // ---- the candidate (public for tests)
    enum class State { none, resolving, ready, failed };
    struct Candidate
    {
        int generation = 0;
        double lat = 0, lon = 0;
        juce::String label; // a place name, or empty for a bare pin (the reply names what is near)
        State state = State::none;
        atmos::Day day;     // prepared exactly as Load will apply it (weather + core sound)
        bool live = false;  // false: the defined offline estimate
        juce::String error;
        juce::Image thumbnail;
        std::shared_ptr<const atmos::AuditionRenderer::Clip> clip;
        juce::String clipKey; // the patch and rate the clip was rendered for
    };
    const Candidate& candidate() const { return cand; }
    void select (double lat, double lon, const juce::String& label, bool centreGlobe);
    void search (const juce::String& query);
    void preview();
    void stopPreview();
    bool load(); // false when there is no ready candidate
    bool previewPreparing() const { return preparing; }
    static bool parseCoordinates (const juce::String&, double& lat, double& lon);
    atmos::Globe& globeView() { return globe; }

private:
    void timerCallback() override;
    void updateCard();
    void showResults (const juce::Array<atmos::WeatherClient::Place>&, const juce::String& message);
    juce::String clipKeyFor (const atmos::Patch&) const;
    juce::Image renderThumbnail() const;

    AtmosProcessor& proc;
    atmos::AuditionRenderer renderer;
    Candidate cand;
    int searchGeneration = 0, auditionToken = 0;
    bool preparing = false, playAfterRender = false;
    juce::String pendingQuery;
    double typedAt = 0;
    float scale = 1;

    juce::Label title, hint, status, place, coords, conditions, localTime, source, soundName, phrase, current;
    juce::TextEditor field;
    juce::OwnedArray<juce::TextButton> results;
    juce::Label resultsMessage;
    atmos::Globe globe;
    juce::TextButton closeButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) }, previewButton { "Preview sound" }, stopButton { "Stop" },
        cancelButton { "Cancel" }, loadButton { "Load location" }, connectionButton { "Use my connection's location" };
    juce::Rectangle<int> thumbArea, card, globeArea;
    std::vector<atmos::WeatherClient::Place> places;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExploreBrowser)
};
