#include "ExploreBrowser.h"
#include "BibleFile.h"
#include "Storage.h"
#include "gui/SceneModel.h"
#include "gui/SceneView.h"

namespace G = atmos::gui;
using G::Theme;

namespace
{
int64_t nowUnix() { return juce::Time::currentTimeMillis() / 1000; }
const juce::Colour kOrange (0xffe8743b);

juce::String degrees (double v, char pos, char neg)
{
    return juce::String (std::abs (v), 2) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0 ")) + juce::String::charToString (v >= 0 ? pos : neg);
}

juce::String ageText (int64_t observedAt)
{
    const auto mins = (nowUnix() - observedAt) / 60;
    if (observedAt <= 0) return {};
    if (mins < 2) return "just now";
    if (mins < 90) return juce::String (mins) + " min ago";
    return juce::String (mins / 60) + " h ago";
}
} // namespace

ExploreBrowser::ExploreBrowser (AtmosProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Explore locations");
    title.setText ("EXPLORE LOCATIONS", juce::dontSendNotification);
    hint.setText (juce::String (juce::CharPointer_UTF8 ("Drag to rotate \xc2\xb7 click to pin")), juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::centred);
    for (auto* l : { &title, &hint, &status, &place, &coords, &conditions, &localTime, &source, &soundName, &phrase, &current, &resultsMessage })
    {
        l->setColour (juce::Label::textColourId, Theme::text);
        addAndMakeVisible (l);
    }
    for (auto* l : { &hint, &localTime, &source, &phrase, &resultsMessage })
        l->setColour (juce::Label::textColourId, Theme::textDim);
    status.setColour (juce::Label::textColourId, kOrange);

    field.setTextToShowWhenEmpty ("Search city, region or coordinates (59.91, 10.75)", Theme::textDim);
    field.setTitle ("Search for a place");
    field.setColour (juce::TextEditor::backgroundColourId, Theme::track);
    field.onReturnKey = [this] { search (field.getText()); };
    field.onTextChange = [this] {
        pendingQuery = field.getText().trim();
        typedAt = juce::Time::getMillisecondCounterHiRes();
    };
    addAndMakeVisible (field);

    atmos::Globe::Palette pal;
    pal.ocean = juce::Colour (0xff2f6f78);
    pal.land = juce::Colour (0xffd8d6a8);
    pal.coast = juce::Colour (0xffa9b48a);
    pal.grid = juce::Colour (0x18ffffff);
    pal.pin = kOrange;
    pal.shaded = true;
    globe.setPalette (pal);
    globe.setTitle ("Globe: drag to rotate, click to choose a place");
    globe.onPin = [this] (double lat, double lon) { select (lat, lon, {}, false); }; // exact coordinates
    addAndMakeVisible (globe);

    closeButton.setTitle ("Close");
    closeButton.onClick = [this] { close(); };
    previewButton.setTitle ("Preview sound");
    previewButton.onClick = [this] { preview(); };
    stopButton.setTitle ("Stop preview");
    stopButton.onClick = [this] { stopPreview(); };
    cancelButton.onClick = [this] { close(); };
    loadButton.setTitle ("Load location");
    loadButton.onClick = [this] { load(); };
    connectionButton.setTooltip ("Follow your internet connection's location again, and read today's sky there");
    connectionButton.onClick = [this] {
        atmos::Storage::setHome ({});
        proc.stopAudition();
        proc.dealToday();
        close();
    };
    for (auto* b : { &closeButton, &previewButton, &stopButton, &cancelButton, &loadButton, &connectionButton })
        addAndMakeVisible (b);
    for (auto* b : { &previewButton, &loadButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kOrange);
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff1b1208));
    }
    setVisible (false);
}

ExploreBrowser::~ExploreBrowser()
{
    stopTimer();
    proc.stopAudition();
}

void ExploreBrowser::open()
{
    const auto d = proc.currentDay();
    current.setText ("Currently loaded: " + (d.placeName.isNotEmpty() ? d.placeName : juce::String (d.lat, 2) + ", " + juce::String (d.lon, 2)), juce::dontSendNotification);
    globe.setCurrent (d.lat, d.lon, true);
    if (cand.state == State::none) globe.centreOn (d.lat, d.lon);
    connectionButton.setVisible (atmos::Storage::home().set);
    updateCard();
    setVisible (true);
    toFront (true);
    field.grabKeyboardFocus();
    startTimerHz (10);
}

void ExploreBrowser::close()
{
    // Candidate state only: the instrument is whatever it is now (automation and the
    // instrument's own updates may have changed it meanwhile; nothing is restored)
    proc.stopAudition();
    renderer.cancel();
    ++cand.generation;
    ++searchGeneration;
    const int keepGen = cand.generation;
    cand = Candidate();
    cand.generation = keepGen;
    preparing = playAfterRender = false;
    globe.setPin (0, 0, false);
    results.clear();
    resultsMessage.setText ({}, juce::dontSendNotification);
    stopTimer();
    setVisible (false);
    if (onClosed) onClosed();
}

bool ExploreBrowser::parseCoordinates (const juce::String& text, double& lat, double& lon)
{
    // "59.91, 10.75", "59.91 10.75", "19.08 N 72.88 E", "33.9°S, 18.4°E"
    auto t = text.toUpperCase().replace (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), " ").replace (",", " ").trim();
    juce::StringArray parts;
    parts.addTokens (t, " ", {});
    parts.removeEmptyStrings();
    std::vector<double> nums;
    std::vector<char> hemis;
    for (auto& p : parts)
    {
        juce::String s = p;
        char h = 0;
        if (s.endsWithChar ('N') || s.endsWithChar ('S') || s.endsWithChar ('E') || s.endsWithChar ('W'))
        {
            h = (char) s.getLastCharacter();
            s = s.dropLastCharacters (1);
        }
        if (s.isEmpty() && h != 0 && ! hemis.empty())
        {
            hemis.back() = h;
            continue;
        }
        if (! s.containsOnly ("-+.0123456789") || s.isEmpty()) return false;
        nums.push_back (s.getDoubleValue());
        hemis.push_back (h);
    }
    if (nums.size() != 2) return false;
    lat = nums[0] * (hemis[0] == 'S' ? -1 : 1);
    lon = nums[1] * (hemis[1] == 'W' ? -1 : 1);
    if (hemis[0] == 'E' || hemis[0] == 'W' || hemis[1] == 'N' || hemis[1] == 'S') std::swap (lat, lon);
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

void ExploreBrowser::search (const juce::String& queryIn)
{
    const auto query = queryIn.trim();
    pendingQuery.clear();
    double lat, lon;
    if (parseCoordinates (query, lat, lon))
    {
        showResults ({}, {});
        select (lat, lon, {}, true);
        return;
    }
    if (query.length() < 2) return;
    const int gen = ++searchGeneration;
    resultsMessage.setText (juce::String (juce::CharPointer_UTF8 ("Searching\xe2\x80\xa6")), juce::dontSendNotification);
    juce::Component::SafePointer<ExploreBrowser> safe (this);
    proc.weather.geocode (query, [safe, gen] (juce::Array<atmos::WeatherClient::Place> found, juce::String err) {
        if (safe == nullptr || gen != safe->searchGeneration) return; // a newer search or a close
        safe->showResults (found, found.isEmpty() ? (err.isNotEmpty() ? err : "No match. Try adding the country, e.g. Paris, FR") : juce::String());
    });
}

void ExploreBrowser::showResults (const juce::Array<atmos::WeatherClient::Place>& found, const juce::String& message)
{
    results.clear();
    places.clear();
    for (int i = 0; i < juce::jmin (5, found.size()); ++i)
    {
        const auto& p = found.getReference (i);
        places.push_back (p);
        auto* b = results.add (new juce::TextButton (p.name + ", " + p.country + "   " + juce::String (p.lat, 2) + ", " + juce::String (p.lon, 2)));
        b->setTitle ("Choose " + p.name + ", " + p.country);
        b->setColour (juce::TextButton::buttonColourId, Theme::band);
        b->onClick = [this, i] {
            const auto chosen = places[(size_t) i];
            results.clear();
            resultsMessage.setText ({}, juce::dontSendNotification);
            select (chosen.lat, chosen.lon, chosen.name + (chosen.country.isNotEmpty() ? ", " + chosen.country : juce::String()), true);
        };
        addAndMakeVisible (b);
    }
    resultsMessage.setText (message, juce::dontSendNotification);
    resized();
    if (! results.isEmpty()) results[0]->grabKeyboardFocus();
}

void ExploreBrowser::select (double lat, double lon, const juce::String& label, bool centreGlobe)
{
    proc.stopAudition(); // a new place: the previous candidate's phrase stops
    renderer.cancel();
    const int gen = cand.generation + 1;
    cand = Candidate();
    cand.generation = gen;
    cand.lat = lat;
    cand.lon = lon;
    cand.label = label;
    cand.state = State::resolving;
    preparing = playAfterRender = false;
    globe.setPin (lat, lon, true);
    if (centreGlobe) globe.centreOn (lat, lon);
    updateCard();
    juce::Component::SafePointer<ExploreBrowser> safe (this);
    proc.weather.fetchAt (lat, lon, [safe, gen, lat, lon, label] (atmos::WeatherClient::Result r) {
        if (safe == nullptr) return;
        auto& c = safe->cand;
        if (gen != c.generation) return; // a later pin, a close, or a restore took over
        atmos::Day d = r.ok ? r.day : atmos::estimateDay (lat, lon, label, nowUnix());
        d.lat = lat; // the exact chosen point; a nearby city name is only a label
        d.lon = lon;
        if (label.isNotEmpty()) d.placeName = label;
        d.source = r.ok ? "live" : "estimate";
        c.day = safe->proc.prepareDay (d);
        c.live = r.ok;
        c.error = r.error;
        c.state = State::ready;
        c.thumbnail = safe->renderThumbnail();
        safe->updateCard();
    });
}

juce::Image ExploreBrowser::renderThumbnail() const
{
    // One still frame of the candidate's scene; no second animated interface
    // At the size it is shown (at least 320 px wide), so it stays crisp
    const int w = juce::jmax (320, thumbArea.getWidth()), h = (int) std::round (w * 460 / 1024.0);
    G::SceneView v;
    v.setSize (w, h);
    G::SceneView::Prefs p;
    p.quality = G::SceneView::Quality::still;
    v.setPrefs (p);
    if (landscapeOverride) v.setLandscapeOverride (landscapeOverride());
    v.setSnapshot (G::makeSnapshot (cand.day, cand.live ? G::FeedStatus::live : G::FeedStatus::estimate, true, nowUnix()), true);
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    v.paintEntireComponent (g, false);
    return img;
}

juce::String ExploreBrowser::clipKeyFor (const atmos::Patch& patch) const
{
    juce::String k (proc.hostSampleRate(), 1);
    for (int i = 0; i < atmos::kNumParams; ++i)
        k << "|" << juce::String (patch[i], 5);
    return k;
}

void ExploreBrowser::preview()
{
    if (cand.state != State::ready) return;
    const double sr = proc.hostSampleRate() > 0 ? proc.hostSampleRate() : 48000.0;
    const auto patch = proc.patchFor (cand.day); // the current macros, as Load would play it
    const auto key = clipKeyFor (patch);
    if (cand.clip != nullptr && cand.clipKey == key)
    {
        proc.playAudition (cand.clip);
        updateCard();
        return;
    }
    // Render (or re-render after the macros moved), then play
    preparing = playAfterRender = true;
    const int token = ++auditionToken;
    const int gen = cand.generation;
    juce::Component::SafePointer<ExploreBrowser> safe (this);
    renderer.render (patch, sr, token, [safe, gen, key] (std::shared_ptr<const atmos::AuditionRenderer::Clip> clip, int) {
        if (safe == nullptr || gen != safe->cand.generation) return;
        safe->preparing = false;
        safe->cand.clip = clip;
        safe->cand.clipKey = key;
        if (safe->playAfterRender) safe->proc.playAudition (clip);
        safe->playAfterRender = false;
        safe->updateCard();
    });
    updateCard();
}

void ExploreBrowser::stopPreview()
{
    playAfterRender = false;
    proc.stopAudition();
    updateCard();
}

bool ExploreBrowser::load()
{
    if (cand.state != State::ready) return false;
    // The exact Day that was shown and auditioned: no fresh request at the last moment
    const auto d = cand.day;
    const bool live = cand.live;
    proc.stopAudition();
    proc.commitLocation (d, live);
    close();
    return true;
}

void ExploreBrowser::timerCallback()
{
    proc.tidyAuditions();
    if (pendingQuery.length() >= 3 && juce::Time::getMillisecondCounterHiRes() - typedAt > 450) search (pendingQuery);
    // Invalidate a rendered phrase when the sound it was made for has changed (macros moved)
    if (cand.clip != nullptr && cand.state == State::ready && clipKeyFor (proc.patchFor (cand.day)) != cand.clipKey) cand.clip = nullptr;
    updateCard();
}

void ExploreBrowser::updateCard()
{
    const bool ready = cand.state == State::ready;
    status.setText (cand.state == State::none ? juce::String ("Search or click the globe") : juce::String (juce::CharPointer_UTF8 ("SELECTED \xc2\xb7 NOT LOADED")),
                    juce::dontSendNotification);
    const auto& d = cand.day;
    juce::String name = cand.label;
    if (name.isEmpty() && ready) name = d.placeName.isNotEmpty() ? "Near " + d.placeName + (d.country.isNotEmpty() ? ", " + d.country : juce::String()) : juce::String ("Open coordinates");
    if (name.isEmpty() && cand.state == State::resolving) name = "Pinned point";
    place.setText (name, juce::dontSendNotification);
    coords.setText (cand.state == State::none ? juce::String() : degrees (cand.lat, 'N', 'S') + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + degrees (cand.lon, 'E', 'W'),
                    juce::dontSendNotification);
    if (cand.state == State::resolving)
    {
        conditions.setText (juce::String (juce::CharPointer_UTF8 ("Reading the weather\xe2\x80\xa6")), juce::dontSendNotification);
        localTime.setText ({}, juce::dontSendNotification);
        source.setText ({}, juce::dontSendNotification);
        soundName.setText ({}, juce::dontSendNotification);
    }
    else if (ready)
    {
        conditions.setText (juce::String (juce::roundToInt (d.temp)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0" "C   ")) + d.conditionText() + "   " + juce::String (juce::roundToInt (d.humidity)) + "% RH",
                            juce::dontSendNotification);
        const auto snap = G::makeSnapshot (d, cand.live ? G::FeedStatus::live : G::FeedStatus::estimate, true, nowUnix());
        localTime.setText ("Local time " + G::timeLabel (snap), juce::dontSendNotification);
        source.setText (cand.live ? juce::String (juce::CharPointer_UTF8 ("LIVE \xc2\xb7 observed ")) + ageText (d.observedAt)
                                  : juce::String (juce::CharPointer_UTF8 ("ESTIMATED \xc2\xb7 offline")) + (cand.error.isNotEmpty() ? " (" + cand.error + ")" : juce::String()),
                        juce::dontSendNotification);
        bool ok = false;
        const auto s = atmos::soundFromVar (d.sound, ok);
        soundName.setText (ok ? "Sound: " + s.name : juce::String(), juce::dontSendNotification);
    }
    else
    {
        for (auto* l : { &conditions, &localTime, &source, &soundName })
            l->setText ({}, juce::dontSendNotification);
    }
    const bool playing = proc.auditionPlaying();
    previewButton.setEnabled (ready && ! preparing);
    stopButton.setEnabled (playing || preparing);
    loadButton.setEnabled (ready);
    juce::String ph = atmos::AuditionRenderer::phraseLabel;
    if (preparing) ph = juce::String (juce::CharPointer_UTF8 ("Preparing preview\xe2\x80\xa6"));
    else if (proc.auditionBlockedByOfflineRender()) ph = "The host is rendering offline: no preview";
    else if (playing) ph = "Playing " + juce::String (atmos::AuditionRenderer::phraseLabel);
    phrase.setText (ph, juce::dontSendNotification);
    repaint (thumbArea);
}

void ExploreBrowser::setUiScale (float s)
{
    scale = s;
    title.setFont (Theme::font (22 * s, true));
    status.setFont (Theme::mono (12 * s));
    place.setFont (Theme::font (20 * s, true));
    for (auto* l : { &coords, &conditions })
        l->setFont (Theme::mono (14 * s));
    for (auto* l : { &hint, &localTime, &source, &soundName, &phrase, &current, &resultsMessage })
        l->setFont (Theme::mono (12 * s));
    field.setFont (Theme::mono (14 * s));
    resized();
}

void ExploreBrowser::resized()
{
    const float s = scale;
    auto r = getLocalBounds().reduced ((int) (18 * s), (int) (12 * s));
    auto top = r.removeFromTop ((int) (34 * s));
    closeButton.setBounds (top.removeFromRight ((int) (34 * s)).reduced (2));
    title.setBounds (top);
    field.setBounds (r.removeFromTop ((int) (30 * s)));
    r.removeFromTop ((int) (8 * s));
    auto footer = r.removeFromBottom ((int) (34 * s));
    loadButton.setBounds (footer.removeFromRight ((int) (150 * s)).reduced (2));
    footer.removeFromRight ((int) (8 * s));
    cancelButton.setBounds (footer.removeFromRight ((int) (100 * s)).reduced (2));
    footer.removeFromRight ((int) (8 * s));
    if (connectionButton.isVisible()) connectionButton.setBounds (footer.removeFromRight ((int) (210 * s)).reduced (2));
    current.setBounds (footer);
    r.removeFromBottom ((int) (8 * s));

    globeArea = r.removeFromLeft (r.getWidth() * 44 / 100);
    auto ga = globeArea;
    hint.setBounds (ga.removeFromBottom ((int) (18 * s)));
    globe.setBounds (ga);
    // Search results drop over the globe
    auto rr = globeArea.withHeight ((int) (26 * s));
    resultsMessage.setBounds (rr);
    if (resultsMessage.getText().isNotEmpty()) rr.translate (0, rr.getHeight());
    for (auto* b : results)
    {
        b->setBounds (rr.reduced (0, 1));
        b->toFront (false);
        rr.translate (0, rr.getHeight());
    }

    r.removeFromLeft ((int) (16 * s));
    card = r;
    auto c = r;
    status.setBounds (c.removeFromTop ((int) (18 * s)));
    place.setBounds (c.removeFromTop ((int) (26 * s)));
    coords.setBounds (c.removeFromTop ((int) (18 * s)));
    conditions.setBounds (c.removeFromTop ((int) (20 * s)));
    localTime.setBounds (c.removeFromTop ((int) (16 * s)));
    source.setBounds (c.removeFromTop ((int) (16 * s)));
    soundName.setBounds (c.removeFromTop ((int) (16 * s)));
    c.removeFromTop ((int) (6 * s));
    auto buttons = c.removeFromBottom ((int) (48 * s));
    phrase.setBounds (buttons.removeFromBottom ((int) (16 * s)));
    previewButton.setBounds (buttons.removeFromLeft ((int) (160 * s)).reduced (0, 2));
    buttons.removeFromLeft ((int) (8 * s));
    stopButton.setBounds (buttons.removeFromLeft ((int) (64 * s)).reduced (0, 2));
    c.removeFromBottom ((int) (6 * s));
    // The thumbnail keeps the scene's aspect (1024 x 460)
    const int w = juce::jmin (c.getWidth(), (int) (c.getHeight() * 1024 / 460.0));
    thumbArea = c.withWidth (w).withHeight ((int) (w * 460 / 1024.0));
}

void ExploreBrowser::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xf2142334));
    g.fillRoundedRectangle (b, 10 * scale);
    g.setColour (Theme::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 10 * scale, 1.2f);
    g.drawVerticalLine (card.getX() - (int) (8 * scale), (float) card.getY(), (float) card.getBottom());
    if (cand.thumbnail.isValid() && cand.state == State::ready)
        g.drawImage (cand.thumbnail, thumbArea.toFloat(), juce::RectanglePlacement::stretchToFit);
    else if (! thumbArea.isEmpty())
    {
        g.setColour (Theme::track);
        g.fillRoundedRectangle (thumbArea.toFloat(), 4);
    }
}

bool ExploreBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}
