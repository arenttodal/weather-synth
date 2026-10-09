#include "PluginEditor.h"
#include "Storage.h"
#include "gui/Platform.h"
#include "gui/SceneModel.h"

using namespace atmos;
namespace G = atmos::gui;

namespace
{
juce::String tempLabel (double c) { return juce::String (juce::roundToInt (c)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "C"; }

G::FeedStatus feed (AtmosProcessor::Status s)
{
    switch (s)
    {
        case AtmosProcessor::Status::dealing: return G::FeedStatus::dealing;
        case AtmosProcessor::Status::live: return G::FeedStatus::live;
        case AtmosProcessor::Status::estimate: return G::FeedStatus::estimate;
        case AtmosProcessor::Status::restored: return G::FeedStatus::restored;
        case AtmosProcessor::Status::preview: return G::FeedStatus::preview;
        case AtmosProcessor::Status::previewLoading: return G::FeedStatus::previewLoading;
    }
    return G::FeedStatus::estimate;
}
} // namespace

// ---------------------------------------------------------------- InlinePanel
InlinePanel::InlinePanel()
{
    addAndMakeVisible (field);
    addAndMakeVisible (primary);
    addAndMakeVisible (cancel);
    addChildComponent (extra);
    addAndMakeVisible (message);
    cancel.setButtonText ("Cancel");
    message.setColour (juce::Label::textColourId, Palette::ink2);
    message.setFont (G::Theme::mono (13));
    field.setFont (G::Theme::mono (15));
    field.setIndents (8, 6);
}

void InlinePanel::open (const juce::String& t, const juce::String& placeholder, const juce::String& primaryText)
{
    title = t;
    field.setText ({}, false);
    field.setTextToShowWhenEmpty (placeholder, Palette::ink3);
    field.setTitle (t);
    primary.setButtonText (primaryText);
    message.setText ({}, juce::dontSendNotification);
    results.clear();
    extra.setVisible (false);
    setVisible (true);
    toFront (true);
    resized();
    field.grabKeyboardFocus();
}

void InlinePanel::setResults (const juce::StringArray& labels, std::function<void (int)> onPick)
{
    results.clear();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = results.add (new juce::TextButton (labels[i]));
        b->onClick = [onPick, i] { onPick (i); };
        addAndMakeVisible (b);
    }
    resized();
}

void InlinePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto box = getLocalBounds().withSizeKeepingCentre (460, 124 + results.size() * 34).toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (box, 6);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (box, 6, 1);
    g.setColour (Palette::ink);
    g.setFont (G::Theme::font (16, true));
    g.drawText (title, box.reduced (18, 14).removeFromTop (22).toNearestInt(), juce::Justification::left);
}

void InlinePanel::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (460, 124 + results.size() * 34).reduced (18, 14);
    box.removeFromTop (28);
    auto row = box.removeFromTop (34);
    cancel.setBounds (row.removeFromRight (84));
    row.removeFromRight (6);
    primary.setBounds (row.removeFromRight (92));
    row.removeFromRight (8);
    field.setBounds (row);
    box.removeFromTop (6);
    for (auto* b : results)
    {
        b->setBounds (box.removeFromTop (30));
        box.removeFromTop (4);
    }
    auto bottom = box.removeFromTop (24);
    if (extra.isVisible()) extra.setBounds (bottom.removeFromRight (200));
    message.setBounds (bottom);
}

// ---------------------------------------------------------------- Editor
AtmosEditor::AtmosEditor (AtmosProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    setTitle ("Weather Synth");

    addAndMakeVisible (header);
    addAndMakeVisible (scene);
    header.refresh.onClick = [this] { proc.dealToday(); };
    header.place.onClick = [this] { showPlacePanel(); };
    header.keep.onClick = [this] { showKeepPanel(); };
    header.days.onClick = [this] { showDaysMenu(); };
    header.settings.onClick = [this] { showSettingsMenu(); };

    // The five real macros. Labels and tooltips describe what each does to the sound.
    struct M
    {
        const char *id, *label;
        G::MacroTrack::Handle shape;
        juce::uint32 colour;
        const char* tip;
    };
    const M macros[] = {
        { "tone", "Tone", G::MacroTrack::Handle::cube, 0xffd8623e, "Tone: darker or brighter than the weather set it (filter cutoff, brightness, filter envelope)." },
        { "bloom", "Bloom", G::MacroTrack::Handle::wedge, 0xffe9dcc0, "Bloom: from plucked to swelling (attack, release and sustain of the sound and its filter)." },
        { "motion", "Motion", G::MacroTrack::Handle::pentagon, 0xff7fb57a, "Motion: how much the sound moves (chorus/tape/pulse movement, vibrato, PWM, filter LFO)." },
        { "space", "Space", G::MacroTrack::Handle::cube, 0xff7fa3cc, "Space: drier or wetter (reverb send and decay, echo send)." },
        { "intensity", "Intensity", G::MacroTrack::Handle::pentagon, 0xffeab04e, "Intensity: how hard today's weather pulls the sound, from tamed (core sound) to exaggerated extremes." },
    };
    for (auto& m : macros)
    {
        auto* t = tracks.add (new G::MacroTrack (proc.apvts, m.id, m.label, m.shape, juce::Colour (m.colour),
                                                 juce::String (m.tip) + " Stays inside the sound's designed limits; the weather shown above never changes."));
        addAndMakeVisible (t);
    }

    scene.musicalSource = [this] {
        G::SceneView::Musical m;
        m.tone = proc.apvts.getRawParameterValue ("tone")->load();
        m.motion = proc.apvts.getRawParameterValue ("motion")->load();
        m.space = proc.apvts.getRawParameterValue ("space")->load();
        m.activity = proc.getActivity();
        return m;
    };

    toastLabel.setJustificationType (juce::Justification::centred);
    toastLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xe0101c2c));
    toastLabel.setColour (juce::Label::textColourId, G::Theme::text);
    addChildComponent (toastLabel);

    addChildComponent (panel);
    panel.cancel.onClick = [this] { panel.setVisible (false); };

    // Admin globe
    addChildComponent (globeLayer);
    globeLayer.addAndMakeVisible (globe);
    for (auto* c : std::initializer_list<juce::Component*> { &globeTitle, &globeInfo, &globeKeep, &globeBack, &globeClose })
        globeLayer.addAndMakeVisible (c);
    globeTitle.setText ("Anywhere, right now", juce::dontSendNotification);
    globeTitle.setFont (G::Theme::font (18, true));
    globeInfo.setJustificationType (juce::Justification::topLeft);
    globeInfo.setFont (G::Theme::mono (13));
    globeInfo.setColour (juce::Label::textColourId, Palette::ink2);
    globe.onPin = [this] (double lat, double lon) { proc.previewAt (lat, lon); };
    globeKeep.onClick = [this] { showKeepPanel(); };
    globeBack.onClick = [this] {
        proc.endPreview();
        globe.setPin (0, 0, false);
    };
    globeClose.onClick = [this] { toggleGlobe(); };

    tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    applyVisualPrefs();

    // Fixed 3:2 aspect; the layout is recomputed (not stretched) at every size
    setResizable (true, true);
    setResizeLimits (820, 546, 2048, 1364);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) logicalWidth / logicalHeight);
    setSize (logicalWidth, logicalHeight);

    proc.addChangeListener (this);
    refresh();
    startTimer (15 * 1000); // keeps the stale/live label honest; no data is fetched here
}

AtmosEditor::~AtmosEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
    scene.musicalSource = nullptr;
    tooltips.reset();
    setLookAndFeel (nullptr);
}

void AtmosEditor::paint (juce::Graphics& g)
{
    g.fillAll (G::Theme::ui);
    const float s = getWidth() / (float) logicalWidth;
    const float bandTop = (headerH + sceneH) * s;
    g.setColour (G::Theme::band);
    g.fillRect (0.0f, bandTop, (float) getWidth(), getHeight() - bandTop);
    g.setColour (G::Theme::line);
    g.fillRect (0.0f, bandTop, (float) getWidth(), 1.0f);
}

void AtmosEditor::resized()
{
    const float s = getWidth() / (float) logicalWidth;
    auto R = [s] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt(); };
    header.setBounds (R (0, 0, logicalWidth, headerH));
    header.setUiScale (s);
    scene.setBounds (R (0, headerH, logicalWidth, sceneH));
    const float bandTop = headerH + sceneH, bandH = (float) logicalHeight - bandTop;
    for (int i = 0; i < tracks.size(); ++i)
    {
        const float cx = logicalWidth / 2.0f + (i - 2) * 166.0f;
        auto r = R (cx - 75, bandTop + 10, 150, bandH - 10);
        tracks[i]->setBounds (r.withBottom (getHeight())); // anchored to the real bottom edge (no rounding overflow)
        tracks[i]->setUiScale (s);
    }
    toastLabel.setFont (G::Theme::mono (14 * s));
    toastLabel.setBounds (R (logicalWidth / 2 - 260, headerH + sceneH - 44, 520, 30));
    panel.setBounds (getLocalBounds());
    globeLayer.setBounds (getLocalBounds());
    layoutGlobe();
}

void AtmosEditor::layoutGlobe()
{
    auto r = globeLayer.getLocalBounds().reduced (16);
    auto side = r.removeFromRight (250);
    globe.setBounds (r);
    globeTitle.setBounds (side.removeFromTop (30));
    globeClose.setBounds (side.removeFromBottom (32));
    side.removeFromBottom (8);
    globeBack.setBounds (side.removeFromBottom (32));
    side.removeFromBottom (8);
    globeKeep.setBounds (side.removeFromBottom (32));
    side.removeFromBottom (8);
    globeInfo.setBounds (side);
}

void AtmosEditor::toggleGlobe()
{
    const bool show = ! globeLayer.isVisible();
    if (show)
    {
        const auto d = proc.currentDay();
        globe.centreOn (d.lat, d.lon);
        juce::Array<juce::Point<float>> marks;
        for (auto& k : Storage::loadDays())
            marks.add ({ (float) k.lon, (float) k.lat });
        globe.setMarks (marks);
    }
    globeLayer.setVisible (show);
    if (show) globeLayer.toFront (false);
    refresh();
    grabKeyboardFocus();
}

bool AtmosEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode());
    if (mods.isCommandDown() && ((mods.isShiftDown() && code == 'g') || code == 'w'))
    {
        toggleGlobe();
        return true;
    }
    // Developer fixtures: only in builds started with ATMOS_DEV=1, never in the public UI
    if (mods.isCommandDown() && mods.isAltDown() && mods.isShiftDown() && (code == 'f' || code == 't')
        && juce::SystemStats::getEnvironmentVariable ("ATMOS_DEV", {}) == "1")
    {
        if (code == 'f') devFixture = (devFixture + 1) % (int) G::fixtures().size();
        else devTime = (devTime + 1) % (int) G::timesOfDay().size();
        const juce::String name = G::fixtures()[(size_t) juce::jmax (0, devFixture)].name;
        applyBenchOptions (devTime >= 0 ? name + "@" + G::timesOfDay()[(size_t) devTime].name : name, {});
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        if (panel.isVisible()) panel.setVisible (false);
        else if (globeLayer.isVisible()) toggleGlobe();
        return true;
    }
    return false;
}

void AtmosEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void AtmosEditor::timerCallback()
{
    refresh();
    if (toastUntil != 0 && juce::Time::getMillisecondCounter() > toastUntil)
    {
        toastLabel.setVisible (false);
        toastUntil = 0;
        startTimer (15 * 1000);
    }
}

void AtmosEditor::toast (const juce::String& text)
{
    toastLabel.setText (text, juce::dontSendNotification);
    toastLabel.setVisible (true);
    toastLabel.toFront (false);
    toastUntil = juce::Time::getMillisecondCounter() + 3500;
    startTimer (500);
}

void AtmosEditor::applySnapshot (const G::Snapshot& s, bool immediate)
{
    header.setStatus (G::placeLabel (s), tempLabel (s.tempC), fixtureClock.isNotEmpty() ? fixtureClock : G::timeLabel (s), G::conditionLabel (s), s.source, s.offline,
                      fixtureMode ? juce::String ("Developer fixture: not live data") : proc.statusMessage());
    scene.setSnapshot (s, immediate);
}

void AtmosEditor::refresh()
{
    if (fixtureMode) return;
    const auto d = proc.currentDay();
    const auto st = proc.status();
    const auto snap = G::makeSnapshot (d, feed (st), proc.dayIsCurrent(), juce::Time::currentTimeMillis() / 1000);
    // A new accepted Day changes the scene; restores and saved Days appear at once
    const juce::String key = juce::String (d.observedAt) + "|" + juce::String (d.lat, 4) + "|" + juce::String (d.lon, 4) + "|" + d.source + "|" + juce::String ((int) st);
    if (key != sceneKey)
    {
        const bool first = sceneKey.isEmpty();
        sceneKey = key;
        applySnapshot (snap, first || st == AtmosProcessor::Status::restored);
    }
    else
        header.setStatus (G::placeLabel (snap), tempLabel (snap.tempC), G::timeLabel (snap), G::conditionLabel (snap), snap.source, snap.offline, proc.statusMessage());

    header.keep.setEnabled (true);
    header.refresh.setTooltip (d.source == "live" && proc.dayIsCurrent() && ! proc.isPreviewing()
                                   ? "Read today's sky again (only the sky changes; it is still today's Day)"
                                   : "Hear today: replace this Day with today's sky and sound");

    if (globeLayer.isVisible())
    {
        juce::String info;
        if (proc.isPreviewing())
            info << d.autoTitle() << "\n\n"
                 << juce::String (d.lat, 3) << ", " << juce::String (d.lon, 3) << "\n"
                 << d.conditionText() << ", " << juce::String (d.temp, 1) << juce::String (juce::CharPointer_UTF8 (" \xc2\xb0" "C\n"))
                 << juce::String (juce::roundToInt (d.humidity)) << "% humidity, wind " << juce::String (d.wind, 1) << " m/s\n"
                 << "Sound " << proc.currentSound().name << "\n\n"
                 << proc.statusMessage();
        else
            info << "Drag to spin, scroll to zoom.\nClick anywhere to hear that place's sky right now.\n\n"
                 << "White dots are your kept Days.\n\nEsc closes.";
        globeInfo.setText (info, juce::dontSendNotification);
        globeKeep.setEnabled (proc.isPreviewing());
        globeBack.setEnabled (proc.isPreviewing());
    }
}

void AtmosEditor::applyBenchOptions (const juce::String& fixture, const juce::String& quality)
{
    if (quality.isNotEmpty())
    {
        auto p = scene.prefs();
        p.quality = quality == "still" ? G::SceneView::Quality::still : quality == "economy" ? G::SceneView::Quality::economy : G::SceneView::Quality::full;
        scene.setPrefs (p);
    }
    if (fixture.isEmpty()) return;
    if (fixture == "live") // back to the processor's own Day
    {
        fixtureMode = false;
        fixtureClock.clear();
        scene.setTimeOverride (nullptr);
        sceneKey.clear();
        refresh();
        return;
    }
    const auto name = fixture.upToFirstOccurrenceOf ("@", false, false);
    const auto time = fixture.fromFirstOccurrenceOf ("@", false, false);
    if (auto* f = G::findFixture (name))
    {
        fixtureMode = true;
        const auto* t = G::findTime (time.isNotEmpty() ? time : juce::String ("noon"));
        scene.setTimeOverride (t);
        fixtureClock = t != nullptr ? juce::String (t->clock) : juce::String();
        auto s = f->snapshot;
        s.place = std::string ("Fixture: ") + f->title;
        applySnapshot (s, true);
    }
}

void AtmosEditor::applyVisualPrefs()
{
    const auto v = Storage::visualPrefs();
    G::SceneView::Prefs p;
    p.quality = v.animation == "still" ? G::SceneView::Quality::still : v.animation == "economy" ? G::SceneView::Quality::economy : G::SceneView::Quality::full;
    p.reduceFlashes = v.reduceFlashes;
    p.reduceMotion = v.reduceMotion || G::osPrefersReducedMotion(); // the system setting always wins
    scene.setPrefs (p);
}

void AtmosEditor::showSettingsMenu()
{
    const auto v = Storage::visualPrefs();
    juce::PopupMenu m;
    m.addSectionHeader ("Animation");
    m.addItem (1, "Full (24 fps)", true, v.animation == "full");
    m.addItem (2, "Economy (12 fps)", true, v.animation == "economy");
    m.addItem (3, "Still picture", true, v.animation == "still");
    m.addSeparator();
    const bool osReduced = G::osPrefersReducedMotion();
    m.addItem (4, osReduced ? "Reduce motion (on in system settings)" : "Reduce motion", ! osReduced, v.reduceMotion || osReduced);
    m.addItem (5, "No lightning flashes", true, v.reduceFlashes);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.settings), [this] (int r) {
        if (r <= 0) return;
        auto p = Storage::visualPrefs();
        if (r == 1) p.animation = "full";
        if (r == 2) p.animation = "economy";
        if (r == 3) p.animation = "still";
        if (r == 4) p.reduceMotion = ! p.reduceMotion;
        if (r == 5) p.reduceFlashes = ! p.reduceFlashes;
        Storage::setVisualPrefs (p);
        applyVisualPrefs();
    });
}

void AtmosEditor::showKeepPanel()
{
    const auto suggestion = proc.currentDay().autoTitle();
    panel.open ("Keep this Day", "Name it (optional)", "Keep");
    panel.message.setText (suggestion, juce::dontSendNotification);
    panel.primary.onClick = [this] {
        if (proc.keepDay (panel.field.getText()))
        {
            panel.setVisible (false);
            const auto name = panel.field.getText().trim();
            toast ("Kept: " + (name.isNotEmpty() ? name : proc.currentDay().autoTitle()));
            if (globeLayer.isVisible())
            {
                juce::Array<juce::Point<float>> marks;
                for (auto& k : Storage::loadDays())
                    marks.add ({ (float) k.lon, (float) k.lat });
                globe.setMarks (marks);
            }
        }
        else
            panel.message.setText ("Couldn't save to " + Storage::folder().getFullPathName(), juce::dontSendNotification);
    };
    panel.field.onReturnKey = [this] { panel.primary.triggerClick(); };
}

void AtmosEditor::showDaysMenu()
{
    const auto days = Storage::loadDays();
    juce::PopupMenu menu, del;
    menu.addSectionHeader ("Kept Days");
    if (days.isEmpty()) menu.addItem (-1, "Nothing kept yet. Use Keep this Day.", false);
    for (int i = 0; i < days.size(); ++i)
    {
        const auto label = days[i].name.isNotEmpty() ? days[i].name + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\x94  ")) + days[i].autoTitle() : days[i].autoTitle();
        menu.addItem (i + 1, label);
        del.addItem (1000 + i, label);
    }
    if (! days.isEmpty())
    {
        menu.addSeparator();
        menu.addSubMenu ("Delete a Day", del);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header.days), [this, days] (int r) {
        if (r >= 1 && r <= days.size()) proc.loadDay (days[r - 1]);
        else if (r >= 1000 && r < 1000 + days.size()) Storage::deleteDay (days[r - 1000].id);
    });
}

void AtmosEditor::showPlacePanel()
{
    panel.open ("Where is your sky?", "Search a city, e.g. Bergen", "Search");
    const auto home = Storage::home();
    panel.message.setText (home.set ? "Fixed to " + home.name : "Following your internet connection's location", juce::dontSendNotification);
    panel.extra.setButtonText ("Use my connection");
    panel.extra.setVisible (home.set);
    panel.extra.onClick = [this] {
        Storage::setHome ({});
        panel.setVisible (false);
        proc.dealToday();
    };
    panel.resized();
    panel.primary.onClick = [this] {
        const auto q = panel.field.getText().trim();
        if (q.length() < 2) return;
        panel.message.setText (juce::String (juce::CharPointer_UTF8 ("Searching\xe2\x80\xa6")), juce::dontSendNotification);
        juce::Component::SafePointer<AtmosEditor> safe (this);
        proc.weather.geocode (q, [safe] (juce::Array<WeatherClient::Place> places, juce::String err) {
            if (safe == nullptr) return;
            auto& pnl = safe->panel;
            if (places.isEmpty())
            {
                pnl.message.setText (err.isNotEmpty() ? err : "No match. Try adding the country, e.g. Paris, FR", juce::dontSendNotification);
                pnl.setResults ({}, {});
                return;
            }
            juce::StringArray labels;
            for (auto& p : places)
                labels.add (p.name + ", " + p.country + "   (" + juce::String (p.lat, 2) + ", " + juce::String (p.lon, 2) + ")");
            pnl.message.setText ("Pick one", juce::dontSendNotification);
            pnl.setResults (labels, [safe, places] (int i) {
                if (safe == nullptr) return;
                Storage::Home h;
                h.name = places[i].name;
                h.lat = places[i].lat;
                h.lon = places[i].lon;
                h.set = true;
                Storage::setHome (h);
                safe->panel.setVisible (false);
                safe->proc.dealToday();
            });
        });
    };
    panel.field.onReturnKey = [this] { panel.primary.triggerClick(); };
}
