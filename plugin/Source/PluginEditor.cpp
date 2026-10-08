#include "PluginEditor.h"
#include "Storage.h"
#include "Core.h"

using namespace atmos;

namespace
{
juce::Font font (float h, bool bold = false)
{
#if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font (h, bold ? juce::Font::bold : juce::Font::plain);
#endif
}

juce::String fmtS (double s) { return s < 1 ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, 2) + " s"; }
juce::String fmtHz (double f) { return f < 1000 ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000, 1) + " kHz"; }
juce::String pct (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }
juce::String choiceName (int p, double v)
{
    const auto& info = atmos::paramInfo (p);
    juce::StringArray names;
    names.addTokens (info.choices != nullptr ? info.choices : "", "|", "");
    const int i = juce::jlimit (0, juce::jmax (0, names.size() - 1), juce::roundToInt (v - info.min));
    return names[i];
}

juce::String beaufort (double w)
{
    static const double lim[] = { 0.5, 1.6, 3.4, 5.5, 8, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7 };
    static const char* names[] = { "Calm", "Light air", "Light breeze", "Gentle breeze", "Moderate breeze", "Fresh breeze", "Strong breeze",
                                   "Near gale", "Gale", "Strong gale", "Storm", "Violent storm", "Hurricane" };
    int n = 12;
    for (int i = 0; i < 12; ++i)
        if (w < lim[i])
        {
            n = i;
            break;
        }
    return juce::String (names[n]);
}

juce::String prettyDate (const juce::String& ymd)
{
    const int y = ymd.substring (0, 4).getIntValue(), m = ymd.substring (5, 7).getIntValue(), d = ymd.substring (8, 10).getIntValue();
    if (y < 1970 || m < 1 || m > 12) return ymd;
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    return juce::String (d) + " " + months[m - 1] + " " + juce::String (y);
}
} // namespace

// ---------------------------------------------------------------- LookAndFeel
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::bg);
    setColour (juce::TextButton::buttonColourId, Palette::panel);
    setColour (juce::TextButton::textColourOffId, Palette::ink);
    setColour (juce::TextButton::textColourOnId, Palette::ink);
    setColour (juce::Label::textColourId, Palette::ink);
    setColour (juce::TextEditor::backgroundColourId, Palette::bg);
    setColour (juce::TextEditor::textColourId, Palette::ink);
    setColour (juce::TextEditor::outlineColourId, Palette::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
    setColour (juce::CaretComponent::caretColourId, Palette::accent);
    setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
    setColour (juce::PopupMenu::textColourId, Palette::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::ink);
    setColour (juce::PopupMenu::headerTextColourId, Palette::ink3);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2;
    const auto c = bounds.getCentre();
    const float mid = (start + end) / 2, ang = start + pos * (end - start);

    g.setColour (Palette::panel);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 4, r - 4, 0, start, end, true);
    g.setColour (Palette::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar arc from nature's position (12 o'clock)
    if (std::abs (ang - mid) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - 4, r - 4, 0, juce::jmin (mid, ang), juce::jmax (mid, ang), true);
        g.setColour (Palette::accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Nature tick
    g.setColour (Palette::ink3);
    g.fillRect (c.x - 1, c.y - r - 1, 2.0f, 6.0f);

    juce::Path needle;
    needle.addRoundedRectangle (-1.5f, -(r - 10), 3.0f, r * 0.45f, 1.5f);
    g.setColour (Palette::ink);
    g.fillPath (needle, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Palette::accent.withAlpha (0.25f) : over ? Palette::panel.brighter (0.08f) : Palette::panel);
    g.fillRoundedRectangle (r, 4);
    g.setColour (b.getToggleState() ? Palette::accent : over ? Palette::ink3 : Palette::line);
    g.drawRoundedRectangle (r, 4, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (14.0f, h * 0.5f)); }

// ---------------------------------------------------------------- DayCard
DayCard::DayCard()
{
    for (int i = 0; i < 90; ++i)
        drops.push_back ({ rng.nextFloat(), rng.nextFloat(), 0.6f + rng.nextFloat() * 0.8f });
    startTimerHz (30);
}

void DayCard::setDay (const Day& d, const juce::String& waveform)
{
    day = d;
    climate = d.climate();
    wave = waveform;
    repaint();
}

void DayCard::timerCallback()
{
    if (day.precip < 0.02 && ! (climate.sun < -6 && day.clouds < 0.5)) return;
    const bool snow = day.temp < 1;
    for (auto& d : drops)
    {
        d.y += (snow ? 0.002f : 0.02f) * d.v * (0.5f + (float) day.precip);
        d.x += snow ? 0.0012f * std::sin (d.y * 12 + d.v * 7) : 0.002f * (float) juce::jmin (day.wind, 20.0) / 10;
        if (d.y > 1)
        {
            d.y -= 1;
            d.x = rng.nextFloat();
        }
        if (d.x > 1) d.x -= 1;
        if (d.x < 0) d.x += 1;
    }
    repaint();
}

void DayCard::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const double sun = climate.sun;
    const float dayness = (float) juce::jlimit (0.0, 1.0, (sun + 6) / 30);
    const float dusk = (float) juce::jlimit (0.0, 1.0, 1 - std::abs (sun - 2) / 10);
    juce::Colour top = juce::Colour (0xff0b1220).interpolatedWith (juce::Colour (0xff3a78b8), dayness);
    juce::Colour bot = juce::Colour (0xff18223a).interpolatedWith (juce::Colour (0xff9cc6e8), dayness);
    bot = bot.interpolatedWith (juce::Colour (0xffc46a4a), dusk * 0.7f);
    const float grey = (float) juce::jlimit (0.0, 1.0, day.clouds * 0.6 + day.precip * 0.4);
    top = top.interpolatedWith (juce::Colour (0xff4a5560).withMultipliedBrightness (0.3f + 0.7f * dayness), grey * 0.7f);
    bot = bot.interpolatedWith (juce::Colour (0xff6d7884).withMultipliedBrightness (0.3f + 0.7f * dayness), grey * 0.7f);

    juce::Path shape;
    shape.addRoundedRectangle (b, 8);
    g.saveState();
    g.reduceClipRegion (shape);
    g.setGradientFill (juce::ColourGradient (top, 0, 0, bot, 0, b.getHeight(), false));
    g.fillAll();

    // Sun or moon
    const float r = 26;
    const juce::Point<float> sky (b.getRight() - 58, 58);
    if (sun > -3)
    {
        g.setColour (juce::Colour (0xfff6d58a).withAlpha (0.25f * (1 - grey * 0.8f)));
        g.fillEllipse (sky.x - r * 1.8f, sky.y - r * 1.8f, r * 3.6f, r * 3.6f);
        g.setColour (juce::Colour (0xfff6d58a).withAlpha (1 - grey * 0.7f));
        g.fillEllipse (sky.x - r, sky.y - r, 2 * r, 2 * r);
    }
    else
    {
        const double p = climate.moon;
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillEllipse (sky.x - r, sky.y - r, 2 * r, 2 * r);
        juce::Path lit;
        const bool waxing = p < 0.5;
        const double k = std::cos (6.283185307179586 * p);
        for (int i = 0; i <= 40; ++i)
        {
            const double t = -1.5707963 + 3.14159265 * i / 40;
            const float x = (float) (r * std::cos (t)) * (waxing ? 1 : -1), y = (float) (r * std::sin (t));
            i == 0 ? lit.startNewSubPath (sky.x + x, sky.y + y) : lit.lineTo (sky.x + x, sky.y + y);
        }
        for (int i = 40; i >= 0; --i)
        {
            const double t = -1.5707963 + 3.14159265 * i / 40;
            const float x = (float) (r * k * std::cos (t)) * (waxing ? 1 : -1), y = (float) (r * std::sin (t));
            lit.lineTo (sky.x + x, sky.y + y);
        }
        lit.closeSubPath();
        g.setColour (juce::Colour (0xffe9edf2).withAlpha (1 - (float) day.clouds * 0.6f));
        g.fillPath (lit);
    }

    // Weather particles
    if (day.precip >= 0.02)
    {
        const bool snow = day.temp < 1;
        const int n = juce::jlimit (12, 90, (int) (day.precip * 90));
        const float slant = (float) juce::jmin (day.wind, 20.0) * 0.6f;
        for (int i = 0; i < n; ++i)
        {
            const auto& d = drops[(size_t) i];
            const float x = d.x * b.getWidth(), y = d.y * b.getHeight();
            if (snow)
            {
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.fillEllipse (x, y, 2.5f * d.v, 2.5f * d.v);
            }
            else
            {
                g.setColour (juce::Colour (0xffb7cdf0).withAlpha (0.45f));
                g.drawLine (x, y, x - slant * 0.4f, y + 10 * d.v, 1.0f);
            }
        }
    }
    else if (sun < -6 && day.clouds < 0.5)
    {
        for (int i = 0; i < 40; ++i)
        {
            const auto& d = drops[(size_t) i];
            const float tw = 0.4f + 0.4f * std::sin (d.y * 40 + d.v * 9);
            g.setColour (juce::Colours::white.withAlpha (tw * (1 - (float) day.clouds)));
            g.fillEllipse (d.x * b.getWidth(), d.v / 1.4f * b.getHeight() * 0.55f, 1.6f, 1.6f);
        }
    }
    g.restoreState();

    // Text
    auto area = b.reduced (20, 18).toNearestInt();
    auto shadowed = [&g] (const juce::String& s, juce::Font f, juce::Rectangle<int> box, juce::Colour c, juce::Justification j) {
        g.setFont (f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawText (s, box.translated (0, 1), j, true);
        g.setColour (c);
        g.drawText (s, box, j, true);
    };
    const juce::String place = day.placeName.isNotEmpty() ? day.placeName + (day.country.isNotEmpty() ? ", " + day.country : juce::String())
                                                          : juce::String (day.lat, 2) + ", " + juce::String (day.lon, 2);
    shadowed (place, font (16, true), area.removeFromTop (22), Palette::ink, juce::Justification::left);
    shadowed (prettyDate (day.localDate()) + (day.name.isNotEmpty() ? ATMOS_U8 ("  ·  ") + day.name : juce::String()), font (13), area.removeFromTop (18),
              Palette::ink.withAlpha (0.8f), juce::Justification::left);
    area.removeFromTop (34);
    shadowed (juce::String (juce::roundToInt (day.temp)) + juce::String::fromUTF8 ("\xc2\xb0"), font (78, true), area.removeFromTop (84), Palette::ink,
              juce::Justification::left);
    shadowed (day.conditionText(), font (20), area.removeFromTop (28), Palette::ink, juce::Justification::left);

    auto facts = area.removeFromBottom (64);
    const juce::String l1 = juce::String (juce::roundToInt (day.humidity)) + ATMOS_U8 ("% humidity  ·  ") + beaufort (day.wind) + " " + juce::String (day.wind, 1) + " m/s";
    const juce::String l2 = juce::String (juce::roundToInt (day.pressure)) + ATMOS_U8 (" hPa  ·  ") + juce::String (moonPhaseName (climate.moon)) + ATMOS_U8 ("  ·  sun ")
                            + juce::String (juce::roundToInt (sun)) + juce::String::fromUTF8 ("\xc2\xb0");
    shadowed (l1, font (12.5f), facts.removeFromTop (18), Palette::ink.withAlpha (0.85f), juce::Justification::left);
    shadowed (l2, font (12.5f), facts.removeFromTop (18), Palette::ink.withAlpha (0.85f), juce::Justification::left);
    facts.removeFromTop (6);
    shadowed ("Waveform: " + wave + (day.source == "live" ? "" : "   (" + day.source + ")"), font (12.5f, true), facts.removeFromTop (18),
              Palette::accent.brighter (0.2f), juce::Justification::left);
}

void DayCard::mouseUp (const juce::MouseEvent&)
{
    const auto now = juce::Time::getMillisecondCounter();
    clickCount = (now - lastClick < 450) ? clickCount + 1 : 1;
    lastClick = now;
    if (clickCount >= 3)
    {
        clickCount = 0;
        if (onTripleClick) onTripleClick();
    }
}

// ---------------------------------------------------------------- MacroKnob
MacroKnob::MacroKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label)
    : attachment (s, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setTooltip ("Drag to move away from nature. Double-click to return.");
    name.setText (label, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (font (13, true));
    name.setColour (juce::Label::textColourId, Palette::ink2);
    readout.setJustificationType (juce::Justification::centredTop);
    readout.setFont (font (12));
    readout.setColour (juce::Label::textColourId, Palette::ink);
    addAndMakeVisible (slider);
    addAndMakeVisible (name);
    addAndMakeVisible (readout);
}

void MacroKnob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (20));
    readout.setBounds (r.removeFromBottom (34));
    slider.setBounds (r.reduced (2));
}

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
    message.setFont (font (12.5f));
    field.setFont (font (15));
    field.setIndents (8, 6);
}

void InlinePanel::open (const juce::String& t, const juce::String& placeholder, const juce::String& primaryText)
{
    title = t;
    field.setText ({}, false);
    field.setTextToShowWhenEmpty (placeholder, Palette::ink3);
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
    const auto box = getLocalBounds().withSizeKeepingCentre (440, 120 + results.size() * 34).toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (box, 8);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (box, 8, 1);
    g.setColour (Palette::ink);
    g.setFont (font (16, true));
    g.drawText (title, box.reduced (18, 14).removeFromTop (22).toNearestInt(), juce::Justification::left);
}

void InlinePanel::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (440, 120 + results.size() * 34).reduced (18, 14);
    box.removeFromTop (28);
    auto row = box.removeFromTop (32);
    cancel.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    primary.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    field.setBounds (row);
    box.removeFromTop (6);
    for (auto* b : results)
    {
        b->setBounds (box.removeFromTop (30));
        box.removeFromTop (4);
    }
    auto bottom = box.removeFromTop (22);
    if (extra.isVisible()) extra.setBounds (bottom.removeFromRight (190));
    message.setBounds (bottom);
}

// ---------------------------------------------------------------- Editor
AtmosEditor::AtmosEditor (AtmosProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    title.setText ("ATMOSPHERIC", juce::dontSendNotification);
    title.setFont (font (20, true));
    title.setColour (juce::Label::textColourId, Palette::ink);
    status.setJustificationType (juce::Justification::centredRight);
    status.setFont (font (12));
    status.setColour (juce::Label::textColourId, Palette::ink3);
    natureLine.setJustificationType (juce::Justification::topLeft);
    natureLine.setFont (font (12.5f));
    natureLine.setColour (juce::Label::textColourId, Palette::ink2);
    addAndMakeVisible (title);
    addAndMakeVisible (status);
    addAndMakeVisible (natureLine);
    addAndMakeVisible (card);
    card.onTripleClick = [this] { toggleGlobe(); };

    const char* ids[] = { "tone", "bloom", "space", "motion", "intensity" };
    const char* labels[] = { "Tone", "Bloom", "Space", "Motion", "Intensity" };
    for (int i = 0; i < 5; ++i)
    {
        auto* k = knobs.add (new MacroKnob (proc.apvts, ids[i], labels[i]));
        k->slider.onValueChange = [this] { refresh(); };
        addAndMakeVisible (k);
    }

    for (auto* b : { &keepBtn, &daysBtn, &placeBtn, &todayBtn })
        addAndMakeVisible (b);
    keepBtn.onClick = [this] { showKeepPanel(); };
    daysBtn.onClick = [this] { showDaysMenu(); };
    placeBtn.onClick = [this] { showPlacePanel(); };
    todayBtn.onClick = [this] { proc.dealToday(); };

    addChildComponent (panel);
    panel.cancel.onClick = [this] { panel.setVisible (false); };

    // Globe layer
    addChildComponent (globeLayer);
    globeLayer.addAndMakeVisible (globe);
    for (auto* c : std::initializer_list<juce::Component*> { &globeTitle, &globeInfo, &globeKeep, &globeBack, &globeClose })
        globeLayer.addAndMakeVisible (c);
    globeTitle.setText ("Anywhere, right now", juce::dontSendNotification);
    globeTitle.setFont (font (18, true));
    globeInfo.setJustificationType (juce::Justification::topLeft);
    globeInfo.setFont (font (13));
    globeInfo.setColour (juce::Label::textColourId, Palette::ink2);
    globe.onPin = [this] (double lat, double lon) { proc.previewAt (lat, lon); };
    globeKeep.onClick = [this] { showKeepPanel(); };
    globeBack.onClick = [this] {
        proc.endPreview();
        globe.setPin (0, 0, false);
    };
    globeClose.onClick = [this] { toggleGlobe(); };

    setSize (820, 500);
    proc.addChangeListener (this);
    refresh();
    startTimer (30 * 1000);
}

AtmosEditor::~AtmosEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AtmosEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);
    g.setColour (Palette::line);
    g.drawHorizontalLine (44, 16.0f, (float) getWidth() - 16);
}

void AtmosEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    auto top = r.removeFromTop (44);
    title.setBounds (top.removeFromLeft (220));
    status.setBounds (top);
    r.removeFromTop (12);

    auto bottom = r.removeFromBottom (56).withTrimmedBottom (16);
    keepBtn.setBounds (bottom.removeFromLeft (130));
    bottom.removeFromLeft (8);
    daysBtn.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    placeBtn.setBounds (bottom.removeFromLeft (90));
    todayBtn.setBounds (bottom.removeFromRight (130));

    r.removeFromBottom (12);
    card.setBounds (r.removeFromLeft (320));
    r.removeFromLeft (20);
    auto knobRow = r.removeFromTop (190);
    const int kw = knobRow.getWidth() / 5;
    for (auto* k : knobs)
        k->setBounds (knobRow.removeFromLeft (kw).reduced (4, 0));
    r.removeFromTop (16);
    natureLine.setBounds (r);

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
        const auto& d = proc.currentDay();
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
    if (k == juce::KeyPress::escapeKey)
    {
        if (panel.isVisible()) panel.setVisible (false);
        else if (globeLayer.isVisible()) toggleGlobe();
        return true;
    }
    return false;
}

void AtmosEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }
void AtmosEditor::timerCallback() { refresh(); }

void AtmosEditor::refresh()
{
    const auto& d = proc.currentDay();
    const auto P = proc.currentPatch();
    const auto& snd = proc.currentSound();
    const juce::String voice = juce::String (snd.name) + ATMOS_U8 (" · ") + choiceName (srcType, P[srcType])
                               + (P[srcType] < 0.5 ? ATMOS_U8 (" · ") + juce::String (frameName (P[srcFrame])) : juce::String());
    card.setDay (d, voice);
    status.setText (proc.statusMessage(), juce::dontSendNotification);

    knobs[0]->setReadout (fmtHz (P[cutoff]) + "\n" + (P[tilt] >= 0 ? "+" : "") + juce::String (P[tilt], 1) + " dB");
    knobs[1]->setReadout ("in " + fmtS (P[attack]) + "\nout " + fmtS (P[release]));
    knobs[2]->setReadout (choiceName (spaceType, P[spaceType]) + " " + pct (P[spaceSend]) + "\n" + juce::String (P[spaceDecay], 1) + " s tail");
    knobs[3]->setReadout (choiceName (movMode, P[movMode]) + " " + pct (P[movAmount]) + "\nvibrato " + pct (P[vibDepth]));
    const float iv = (float) knobs[4]->slider.getValue();
    knobs[4]->setReadout (juce::String::fromUTF8 ("\xc3\x97 ") + juce::String (iv >= 0 ? 1 + 0.6 * iv : 1 + iv, 2) + "\nextremes");

    juce::StringArray lines;
    lines.add ("Drive " + pct (P[drive]) + (P[crushBits] < 15.5 ? ATMOS_U8 ("  ·  ") + juce::String (juce::roundToInt (P[crushBits])) + "-bit" : juce::String())
               + ATMOS_U8 ("  ·  ") + choiceName (filtType, P[filtType]) + " filter, resonance " + pct (P[resonance] / 0.9));
    lines.add ("Kaleidoscope " + pct (P[kalAmount]) + ATMOS_U8 ("  ·  ") + choiceName (echoType, P[echoType]) + " echo " + pct (P[echoSend]) + " at "
               + fmtS (P[echoTime] / 1000) + ", feedback " + pct (P[echoFeedback]));
    lines.add ("Sub " + pct (P[srcSub]) + ATMOS_U8 ("  ·  moon glow ") + pct (P[srcShimmer]) + ATMOS_U8 ("  ·  rain/wind bed ") + pct (P[bedLevel]));
    natureLine.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);

    const bool liveToday = d.source == "live" && proc.dayIsCurrent() && ! proc.isPreviewing();
    todayBtn.setButtonText (liveToday ? "Refresh sky" : "Hear today");
    todayBtn.setToggleState (! liveToday && proc.status() != AtmosProcessor::Status::dealing, juce::dontSendNotification);

    if (globeLayer.isVisible())
    {
        juce::String info;
        if (proc.isPreviewing())
            info << d.autoTitle() << "\n\n"
                 << juce::String (d.lat, 3) << ", " << juce::String (d.lon, 3) << "\n"
                 << d.conditionText() << ", " << juce::String (d.temp, 1) << ATMOS_U8 (" °C\n")
                 << juce::String (juce::roundToInt (d.humidity)) << "% humidity, wind " << juce::String (d.wind, 1) << " m/s\n"
                 << "Sound " << voice << "\n\n"
                 << proc.statusMessage();
        else
            info << "Drag to spin, scroll to zoom.\nClick anywhere to hear that place's sky right now.\n\n"
                 << "White dots are your kept Days.\n\nEsc closes.";
        globeInfo.setText (info, juce::dontSendNotification);
        globeKeep.setEnabled (proc.isPreviewing());
        globeBack.setEnabled (proc.isPreviewing());
    }
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
            status.setText ("Kept: " + (panel.field.getText().trim().isNotEmpty() ? panel.field.getText().trim() : proc.currentDay().autoTitle()),
                            juce::dontSendNotification);
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
        const auto label = days[i].name.isNotEmpty() ? days[i].name + ATMOS_U8 ("  —  ") + days[i].autoTitle() : days[i].autoTitle();
        menu.addItem (i + 1, label);
        del.addItem (1000 + i, label);
    }
    if (! days.isEmpty())
    {
        menu.addSeparator();
        menu.addSubMenu ("Delete a Day", del);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&daysBtn), [this, days] (int r) {
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
        panel.message.setText (ATMOS_U8 ("Searching…"), juce::dontSendNotification);
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
