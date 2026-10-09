#include "Controls.h"
#include "SceneArt.h"

namespace atmos::gui
{
EmbeddedFonts::EmbeddedFonts()
{
    int size = 0;
    if (const char* d = SceneArt::getNamedResource ("IBMPlexMonoMedium_ttf", size))
        medium = juce::Typeface::createSystemTypefaceFor (d, (size_t) size);
    if (const char* d = SceneArt::getNamedResource ("IBMPlexMonoSemiBold_ttf", size))
        semibold = juce::Typeface::createSystemTypefaceFor (d, (size_t) size);
}

namespace
{
    juce::Typeface::Ptr typeface (bool bold)
    {
        juce::SharedResourcePointer<EmbeddedFonts> fonts;
        return bold ? fonts->semibold : fonts->medium;
    }

    float textWidth (const juce::Font& f, const juce::String& t)
    {
#if JUCE_MAJOR_VERSION >= 8
        return juce::GlyphArrangement::getStringWidth (f, t);
#else
        return f.getStringWidthFloat (t);
#endif
    }

    juce::Path polygon (juce::Point<float> c, float r, int n, float rotation)
    {
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float a = rotation + juce::MathConstants<float>::twoPi * i / n;
            const juce::Point<float> v { c.x + r * std::cos (a), c.y + r * std::sin (a) };
            if (i == 0) p.startNewSubPath (v);
            else p.lineTo (v);
        }
        p.closeSubPath();
        return p;
    }
} // namespace

juce::Font Theme::font (float h, bool bold)
{
    if (! bold) return mono (h);
    if (auto t = typeface (true))
    {
#if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (t).withHeight (h));
#else
        return juce::Font (t).withHeight (h);
#endif
    }
    return mono (h).boldened();
}

juce::Font Theme::mono (float h)
{
    if (auto t = typeface (false))
    {
#if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (t).withHeight (h));
#else
        return juce::Font (t).withHeight (h);
#endif
    }
#if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
#else
    return juce::Font (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain);
#endif
}

// ---------------------------------------------------------------- LookAndFeel
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::ui);
    setColour (juce::TextButton::buttonColourId, Theme::band);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, Theme::text);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::TextEditor::backgroundColourId, Theme::track);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::focus);
    setColour (juce::CaretComponent::caretColourId, Theme::focus);
    setColour (juce::PopupMenu::backgroundColourId, Theme::band);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::focus.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::text);
    setColour (juce::PopupMenu::headerTextColourId, Theme::textDim);
    setColour (juce::TooltipWindow::backgroundColourId, Theme::track);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, Theme::line);
    setDefaultSansSerifTypeface (typeface (false));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto* track = dynamic_cast<MacroTrack*> (s.getParentComponent());
    const float sc = juce::jmax (0.6f, w / 54.0f);
    const float cx = x + w * 0.5f;
    const float tw = 12 * sc;
    const juce::Rectangle<float> rail (cx - tw / 2, (float) y, tw, (float) h);
    g.setColour (Theme::track);
    g.fillRoundedRectangle (rail, tw / 2);
    g.setColour (Theme::line);
    g.drawRoundedRectangle (rail, tw / 2, 1);

    // Weather anchor: the value 0 ("nature"), drawn as a notch across the rail
    const float anchorY = (float) s.getPositionOfValue (0.0);
    g.setColour (Theme::textDim);
    g.fillRect (cx - tw, anchorY - 1, tw * 2, 2.0f);
    // Current lean away from nature
    const auto col = track != nullptr ? track->colour : Theme::focus;
    g.setColour (col.withAlpha (0.55f));
    g.fillRect (cx - tw / 2 + 3, juce::jmin (anchorY, pos), tw - 6, std::abs (pos - anchorY));

    // Handle: cube, wedge or pentagon, faceted like the scene
    const float r = 16 * sc;
    const juce::Point<float> c { cx, pos };
    const auto light = col.brighter (0.25f), dark = col.darker (0.35f);
    const auto shape = track != nullptr ? track->handle : MacroTrack::Handle::cube;
    if (shape == MacroTrack::Handle::cube)
    {
        juce::Path front, top, side;
        const float s2 = r * 0.82f, d = r * 0.32f;
        front.addRectangle (c.x - s2, c.y - s2 + d / 2, 2 * s2 - d, 2 * s2 - d);
        top.startNewSubPath (c.x - s2, c.y - s2 + d / 2);
        top.lineTo (c.x - s2 + d, c.y - s2 - d / 2);
        top.lineTo (c.x + s2, c.y - s2 - d / 2);
        top.lineTo (c.x + s2 - d, c.y - s2 + d / 2);
        top.closeSubPath();
        side.startNewSubPath (c.x + s2 - d, c.y - s2 + d / 2);
        side.lineTo (c.x + s2, c.y - s2 - d / 2);
        side.lineTo (c.x + s2, c.y + s2 - d * 1.5f);
        side.lineTo (c.x + s2 - d, c.y + s2 - d / 2);
        side.closeSubPath();
        g.setColour (col);
        g.fillPath (front);
        g.setColour (light);
        g.fillPath (top);
        g.setColour (dark);
        g.fillPath (side);
    }
    else if (shape == MacroTrack::Handle::wedge)
    {
        juce::Path upper, lower;
        upper.addTriangle (c.x - r * 0.8f, c.y - r * 0.85f, c.x + r, c.y, c.x - r * 0.8f, c.y);
        lower.addTriangle (c.x - r * 0.8f, c.y, c.x + r, c.y, c.x - r * 0.8f, c.y + r * 0.85f);
        g.setColour (light);
        g.fillPath (upper);
        g.setColour (dark);
        g.fillPath (lower);
    }
    else
    {
        auto p = polygon (c, r, 5, -juce::MathConstants<float>::halfPi);
        g.setColour (col);
        g.fillPath (p);
        juce::Path facet;
        facet.addTriangle (c.x, c.y, c.x - r * 0.95f, c.y - r * 0.31f, c.x, c.y - r);
        g.setColour (light);
        g.fillPath (facet);
        juce::Path shade;
        shade.addTriangle (c.x, c.y, c.x + r * 0.59f, c.y + r * 0.81f, c.x - r * 0.59f, c.y + r * 0.81f);
        g.setColour (dark);
        g.fillPath (shade);
    }
    if (s.hasKeyboardFocus (true))
    {
        g.setColour (Theme::focus);
        g.drawEllipse (c.x - r * 1.35f, c.y - r * 1.35f, r * 2.7f, r * 2.7f, 2.0f);
    }
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Theme::focus.withAlpha (0.25f) : over ? Theme::line : Theme::band);
    g.fillRoundedRectangle (r, 4);
    g.setColour (b.hasKeyboardFocus (true) ? Theme::focus : Theme::line);
    g.drawRoundedRectangle (r, 4, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return Theme::font (juce::jmin (15.0f, h * 0.5f)); }

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (Theme::track);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4);
    g.setColour (Theme::line);
    g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 4, 1);
    juce::AttributedString s;
    s.append (text, Theme::font (13.5f), Theme::text);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (s, (float) w - 16);
    tl.draw (g, { 8, 6, (float) w - 16, (float) h - 12 });
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> p, juce::Rectangle<int> area)
{
    juce::AttributedString s;
    s.append (tip, Theme::font (13.5f), Theme::text);
    s.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (s, 300);
    const int w = (int) std::ceil (juce::jmin (300.0f, tl.getWidth())) + 18, h = (int) std::ceil (tl.getHeight()) + 14;
    return juce::Rectangle<int> (p.x - w / 2, p.y + 18, w, h).constrainedWithin (area);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (Theme::mono (13));
    return l;
}

// ---------------------------------------------------------------- IconButton
IconButton::IconButton (Icon i, const juce::String& title, const juce::String& tip) : juce::Button (title), icon (i)
{
    setTitle (title);
    setTooltip (tip);
    setWantsKeyboardFocus (true);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1);
    if (over || down || hasKeyboardFocus (true))
    {
        g.setColour ((down ? Theme::focus.withAlpha (0.3f) : Theme::line.withAlpha (0.7f)));
        g.fillRoundedRectangle (b, 4);
    }
    if (hasKeyboardFocus (true))
    {
        g.setColour (Theme::focus);
        g.drawRoundedRectangle (b, 4, 1.5f);
    }
    const float s = b.getWidth();
    const auto c = b.getCentre();
    const float r = s * 0.28f;
    g.setColour (isEnabled() ? Theme::text : Theme::textDim.withAlpha (0.5f));
    const juce::PathStrokeType stroke (juce::jmax (1.4f, s * 0.07f), juce::PathStrokeType::mitered, juce::PathStrokeType::square);
    juce::Path p;
    switch (icon)
    {
        case Icon::refresh:
        {
            p.addCentredArc (c.x, c.y, r, r, 0, 0.5f, 5.2f, true);
            g.strokePath (p, stroke);
            const float a = 0.5f;
            const juce::Point<float> tip { c.x + r * std::sin (a), c.y - r * std::cos (a) };
            juce::Path head;
            head.addTriangle (tip.x - s * 0.12f, tip.y - s * 0.1f, tip.x + s * 0.1f, tip.y + s * 0.02f, tip.x - s * 0.06f, tip.y + s * 0.13f);
            g.fillPath (head);
            break;
        }
        case Icon::place:
        {
            p.startNewSubPath (c.x, c.y + r * 1.25f);
            p.lineTo (c.x - r * 0.85f, c.y - r * 0.1f);
            p.addCentredArc (c.x, c.y - r * 0.35f, r * 0.85f, r * 0.85f, 0, -1.75f, 1.75f);
            p.closeSubPath();
            g.strokePath (p, stroke);
            g.fillEllipse (c.x - r * 0.25f, c.y - r * 0.6f, r * 0.5f, r * 0.5f);
            break;
        }
        case Icon::keep:
        {
            p.startNewSubPath (c.x - r * 0.75f, c.y - r * 1.05f);
            p.lineTo (c.x + r * 0.75f, c.y - r * 1.05f);
            p.lineTo (c.x + r * 0.75f, c.y + r * 1.1f);
            p.lineTo (c.x, c.y + r * 0.45f);
            p.lineTo (c.x - r * 0.75f, c.y + r * 1.1f);
            p.closeSubPath();
            g.strokePath (p, stroke);
            break;
        }
        case Icon::days:
        {
            for (int i = -1; i <= 1; ++i)
            {
                g.fillRect (c.x - r, c.y + i * r * 0.7f - s * 0.035f, s * 0.08f, s * 0.07f);
                g.fillRect (c.x - r * 0.55f, c.y + i * r * 0.7f - s * 0.035f, r * 1.55f, s * 0.07f);
            }
            break;
        }
        case Icon::settings:
        {
            g.strokePath (polygon (c, r * 0.95f, 6, 0.0f), stroke);
            g.fillEllipse (c.x - r * 0.32f, c.y - r * 0.32f, r * 0.64f, r * 0.64f);
            break;
        }
    }
}

// ---------------------------------------------------------------- MacroTrack
MacroTrack::MacroTrack (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& label, Handle h, juce::Colour c, const juce::String& tip)
    : handle (h), colour (c), param (state.getParameter (id)), attachment (state, id, slider)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setDoubleClickReturnValue (true, 0.0); // double-click: back to the weather's anchor
    slider.setVelocityModeParameters (0.6, 1, 0.0, false, juce::ModifierKeys::shiftModifier); // shift-drag: fine
    slider.setWantsKeyboardFocus (true);
    slider.setTitle (label);
    slider.setDescription (tip);
    slider.setTooltip (tip + "\n\nDouble-click: back to nature. Shift-drag: fine. Arrows: nudge. Double-click the value to type it.");
    slider.textFromValueFunction = [] (double v) {
        if (std::abs (v) < 0.005) return juce::String ("nature");
        return (v > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (juce::roundToInt (std::abs (v) * 100)) + "%";
    };
    slider.valueFromTextFunction = [] (const juce::String& t) {
        if (t.trim().equalsIgnoreCase ("nature")) return 0.0;
        auto s = t.replace (juce::CharPointer_UTF8 ("\xe2\x88\x92"), "-").retainCharacters ("+-0123456789.");
        return juce::jlimit (-1.0, 1.0, s.getDoubleValue() / 100.0);
    };
    slider.onValueChange = [this] {
        updateValue();
        repaint();
    };
    addAndMakeVisible (slider);

    name.setText (label.toUpperCase(), juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setColour (juce::Label::textColourId, Theme::text);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
    value.setJustificationType (juce::Justification::centred);
    value.setColour (juce::Label::textColourId, Theme::textDim);
    value.setEditable (false, true, false); // double-click to type a value
    value.setTooltip ("Double-click to type a value, e.g. +25 or -40");
    value.setTitle (label + " value");
    value.onTextChange = [this] {
        setValueFromText (value.getText());
        updateValue();
    };
    addAndMakeVisible (value);
    updateValue();

    slider.addKeyListener (this);
}

// Keyboard: up/down (or right/left) nudge, shift for fine steps, Home or 0 back to nature.
// setValue goes through the attachment, which wraps each change in a host gesture.
bool MacroTrack::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    const double step = k.getModifiers().isShiftDown() ? 0.002 : 0.02;
    const int code = k.getKeyCode();
    double v = slider.getValue();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) v += step;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) v -= step;
    else if (code == juce::KeyPress::pageUpKey) v += 0.1;
    else if (code == juce::KeyPress::pageDownKey) v -= 0.1;
    else if (code == juce::KeyPress::homeKey || k.getTextCharacter() == '0') v = 0;
    else return false;
    setValueWithGesture (v);
    return true;
}

void MacroTrack::setValueWithGesture (double v)
{
    v = juce::jlimit (-1.0, 1.0, v);
    if (param != nullptr) param->beginChangeGesture();
    slider.setValue (v, juce::sendNotificationSync);
    if (param != nullptr) param->endChangeGesture();
}

void MacroTrack::updateValue() { value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); }

void MacroTrack::setUiScale (float s)
{
    scale = s;
    name.setFont (Theme::font (15.5f * s, true).withExtraKerningFactor (0.08f));
    value.setFont (Theme::mono (15 * s)); // 12 px at the minimum size
    resized();
}

void MacroTrack::resized()
{
    auto r = getLocalBounds();
    const int labelH = juce::roundToInt (22 * scale), valueH = juce::roundToInt (18 * scale);
    value.setBounds (r.removeFromBottom (valueH + juce::roundToInt (8 * scale)).withTrimmedBottom (juce::roundToInt (8 * scale)));
    name.setBounds (r.removeFromBottom (labelH));
    r.removeFromBottom (juce::roundToInt (8 * scale));
    slider.setBounds (r.withSizeKeepingCentre (juce::roundToInt (54 * scale), r.getHeight()));
}

void MacroTrack::paint (juce::Graphics&) {}

// ---------------------------------------------------------------- HeaderBar
HeaderBar::HeaderBar()
{
    for (auto* b : { &refresh, &place, &keep, &days, &settings })
        addAndMakeVisible (b);
    setTitle ("Atmospheric header");
}

void HeaderBar::setUiScale (float s)
{
    scale = s;
    resized();
    repaint();
}

void HeaderBar::setStatus (const juce::String& p, const juce::String& t, const juce::String& tm, const juce::String& cond, Source s, bool off, const juce::String& detail)
{
    if (p == placeText && t == tempText && tm == timeText && cond == conditionText && s == source && off == offline && detail == detailText) return;
    placeText = p;
    tempText = t;
    timeText = tm;
    conditionText = cond;
    source = s;
    offline = off;
    detailText = detail;
    // The status reads as text for screen readers as well
    setDescription (placeText + ", " + tempText + ", " + timeText + ", " + conditionText + ", " + (offline ? "offline, " : "") + sourceLabel (source)
                    + (detail.isNotEmpty() ? ". " + detail : juce::String()));
    repaint();
}

void HeaderBar::resized()
{
    const int bs = juce::roundToInt (30 * scale), gap = juce::roundToInt (4 * scale);
    int x = juce::roundToInt (236 * scale);
    const int y = (getHeight() - bs) / 2;
    for (auto* b : { &refresh, &place, &keep, &days, &settings })
    {
        b->setBounds (x, y, bs, bs);
        x += bs + gap;
    }
}

void HeaderBar::paint (juce::Graphics& g)
{
    const float s = scale;
    g.fillAll (Theme::ui);
    g.setColour (Theme::text);
    g.setFont (Theme::font (24 * s, true).withExtraKerningFactor (0.06f));
    g.drawText ("ATMOSPHERIC", juce::Rectangle<float> (22 * s, 0, 220 * s, (float) getHeight()), juce::Justification::centredLeft);

    // Right side: place · temperature · time · condition · [shape] SOURCE
    juce::String label = sourceLabel (source);
    if (offline && source != Source::estimated) label = "OFFLINE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " " + label;
    if (offline && source == Source::estimated) label = "OFFLINE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " ESTIMATED";
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    juce::String info;
    for (auto& part : { placeText, tempText, timeText, conditionText })
        if (part.isNotEmpty()) info << (info.isEmpty() ? juce::String() : dot) << part;
    const auto f = Theme::mono (15.0f * s); // 12 px at the minimum size
    const float right = getWidth() - 20 * s;
    g.setFont (f);
    const float labelW = textWidth (f, label) + 2;
    const float infoW = textWidth (f, info + dot) + 2;
    const float iconR = 5.5f * s, gap = 8 * s;
    float x = right - labelW;
    g.setColour (Theme::text);
    g.drawText (label, juce::Rectangle<float> (x, 0, labelW, (float) getHeight()), juce::Justification::centredLeft);
    // Source indicator: shape and colour both carry the meaning
    const juce::Point<float> c { x - gap - iconR, getHeight() / 2.0f };
    switch (source)
    {
        case Source::live: g.setColour (Theme::live); g.fillEllipse (c.x - iconR, c.y - iconR, 2 * iconR, 2 * iconR); break;
        case Source::stale: g.setColour (Theme::textDim); g.drawEllipse (c.x - iconR, c.y - iconR, 2 * iconR, 2 * iconR, 1.6f * s); break;
        case Source::savedDay: g.setColour (Theme::text); g.fillRect (c.x - iconR * 0.85f, c.y - iconR * 0.85f, iconR * 1.7f, iconR * 1.7f); break;
        case Source::estimated:
        {
            juce::Path t;
            t.addTriangle (c.x, c.y - iconR, c.x + iconR, c.y + iconR * 0.8f, c.x - iconR, c.y + iconR * 0.8f);
            g.setColour (Theme::textDim);
            g.strokePath (t, juce::PathStrokeType (1.6f * s));
            break;
        }
        case Source::preview:
        {
            juce::Path d;
            d.addQuadrilateral (c.x, c.y - iconR, c.x + iconR, c.y, c.x, c.y + iconR, c.x - iconR, c.y);
            g.setColour (Theme::focus);
            g.fillPath (d);
            break;
        }
        case Source::fixture:
        {
            juce::Path d;
            d.addQuadrilateral (c.x, c.y - iconR, c.x + iconR, c.y, c.x, c.y + iconR, c.x - iconR, c.y);
            g.setColour (Theme::textDim);
            g.strokePath (d, juce::PathStrokeType (1.6f * s));
            break;
        }
        case Source::loading:
            g.setColour (Theme::textDim);
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (c.x + i * iconR * 0.9f - 1.5f * s, c.y - 1.5f * s, 3 * s, 3 * s);
            break;
    }
    if (offline)
    {
        g.setColour (Theme::warn);
        g.drawLine (c.x - iconR * 1.3f, c.y + iconR * 1.3f, c.x + iconR * 1.3f, c.y - iconR * 1.3f, 1.6f * s);
    }
    x = c.x - iconR - gap - infoW;
    const float minX = (236 + 5 * 34 + 12) * s;
    g.setColour (Theme::text);
    g.drawFittedText (info + dot, juce::Rectangle<float> (juce::jmax (minX, x), 0, juce::jmin (infoW, c.x - iconR - gap - minX), (float) getHeight()).toNearestInt(),
                      juce::Justification::centredRight, 1, 0.85f);
    statusArea = { minX, 0, right - minX, (float) getHeight() };
}
} // namespace atmos::gui
