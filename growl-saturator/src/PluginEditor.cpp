#include "PluginEditor.h"
#include "BinaryData.h"
#include "HostKeys.h"

namespace
{
const juce::Colour kBg (0xff0c0c0c), kPanel (0xff181818), kPri (0xffffffff), kSec (0xffb8b8b8), kDim (0xff9f9f9f), kTrack (0xff2d2d2d);
constexpr int kFooterH = 22;

using gwui::uiFont;

float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// Draws "Re&set" as Re<u>s</u>et. Returns the shortcut letter (lower case) or 0.
juce::juce_wchar drawMenuText (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, const juce::Font& font)
{
    const int amp = text.indexOfChar ('&');
    g.setFont (font);
    if (amp < 0 || amp >= text.length() - 1) { g.drawText (text, r, juce::Justification::centredLeft, false); return 0; }
    const auto pre = text.substring (0, amp), key = text.substring (amp + 1, amp + 2), post = text.substring (amp + 2);
    const float wPre = textWidth (font, pre), wKey = textWidth (font, key);
    const float x = (float) r.getX(), cy = (float) r.getCentreY();
    g.drawText (pre, juce::Rectangle<float> (x, (float) r.getY(), wPre + 2, (float) r.getHeight()), juce::Justification::centredLeft, false);
    g.drawText (key, juce::Rectangle<float> (x + wPre, (float) r.getY(), wKey + 2, (float) r.getHeight()), juce::Justification::centredLeft, false);
    const float uy = cy + font.getHeight() * 0.5f - 1.0f;
    g.fillRect (x + wPre, uy, wKey, 1.0f);
    g.drawText (post, juce::Rectangle<float> (x + wPre + wKey, (float) r.getY(), (float) r.getWidth() - wPre - wKey, (float) r.getHeight()), juce::Justification::centredLeft, false);
    return juce::CharacterFunctions::toLowerCase (key[0]);
}
juce::juce_wchar shortcutOf (const juce::String& text)
{
    const int amp = text.indexOfChar ('&');
    return (amp >= 0 && amp < text.length() - 1) ? juce::CharacterFunctions::toLowerCase (text[amp + 1]) : 0;
}
juce::String stripAmp (const juce::String& t) { return t.replace ("&", ""); }
} // namespace

namespace gwui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, (size_t) BinaryData::BarlowRegular_ttfSize);
    static const juce::Typeface::Ptr semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, (size_t) BinaryData::BarlowSemiBold_ttfSize);
    return bold ? semi : regular;
}
} // namespace gwui

//==============================================================================
namespace gwui
{
PopupOverlay::PopupOverlay() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); setVisible (false); setInterceptsMouseClicks (true, true); }

void PopupOverlay::show (juce::Point<int> where, std::vector<Item> newItems)
{
    items = std::move (newItems);
    const auto f = uiFont (13.0f);
    float w = 150.0f;
    for (auto& it : items) w = juce::jmax (w, textWidth (f, stripAmp (it.text.upToFirstOccurrenceOf ("\t", false, false))) + textWidth (uiFont (11.0f), it.text.fromFirstOccurrenceOf ("\t", false, false)) + 52.0f);
    const int h = (int) items.size() * 24 + 8 + (int) std::count_if (items.begin(), items.end(), [] (const Item& i) { return i.separatorAbove; }) * 6;
    panel = { where.x, where.y, (int) w, h };
    panel.setPosition (juce::jlimit (4, juce::jmax (4, getWidth() - panel.getWidth() - 4), panel.getX()),
                       juce::jlimit (4, juce::jmax (4, getHeight() - panel.getHeight() - 4), panel.getY()));
    hover = -1; open = true;
    setVisible (true); toFront (false);
    hostkeys::remember (*this);                          // which host window has the keyboard right now
    setWantsKeyboardFocus (true); grabKeyboardFocus();   // only while the menu is open, so its shortcut letters work
    repaint();
}

void PopupOverlay::dismiss()
{
    const bool wasOpen = open;
    open = false; setWantsKeyboardFocus (false); giveAwayKeyboardFocus(); setVisible (false);
    if (wasOpen) hostkeys::release (*this);   // native focus back to the host window (Space = play / pause keeps working)
}

juce::Rectangle<int> PopupOverlay::rowBounds (int i) const
{
    int y = panel.getY() + 4;
    for (int k = 0; k <= i; ++k)
    {
        if (items[(size_t) k].separatorAbove) y += 6;
        if (k == i) return { panel.getX() + 2, y, panel.getWidth() - 4, 24 };
        y += 24;
    }
    return {};
}
int PopupOverlay::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) items.size(); ++i) if (rowBounds (i).contains (p)) return i;
    return -1;
}

void PopupOverlay::paint (juce::Graphics& g)
{
    if (! open) return;
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (panel.translated (2, 3).toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff202020)); g.fillRoundedRectangle (panel.toFloat(), 6.0f);
    g.setColour (kPri.withAlpha (0.7f)); g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
    const auto f = uiFont (13.0f);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto& it = items[(size_t) i]; auto r = rowBounds (i);
        if (it.separatorAbove) { g.setColour (kDim.withAlpha (0.35f)); g.fillRect (panel.getX() + 8, r.getY() - 4, panel.getWidth() - 16, 1); }
        if (i == hover && it.enabled) { g.setColour (kSec.darker (0.5f)); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
        g.setColour ((it.enabled ? juce::Colours::white : kDim.withAlpha (0.6f)));
        if (it.ticked)
        {
            juce::Path tick; auto t = juce::Rectangle<float> ((float) r.getX() + 6, (float) r.getCentreY() - 5, 10, 10);
            tick.startNewSubPath (t.getX(), t.getCentreY()); tick.lineTo (t.getX() + 3.5f, t.getBottom() - 1); tick.lineTo (t.getRight(), t.getY() + 1);
            g.setColour (kPri); g.strokePath (tick, juce::PathStrokeType (2.0f)); g.setColour (juce::Colours::white);
        }
        const auto label = it.text.upToFirstOccurrenceOf ("\t", false, false), hint = it.text.fromFirstOccurrenceOf ("\t", false, false);
        drawMenuText (g, r.withTrimmedLeft (24), label, f);
        if (hint.isNotEmpty()) { g.setColour (kDim); g.setFont (uiFont (11.0f)); g.drawText (hint, r.withTrimmedRight (8), juce::Justification::centredRight, false); }
    }
}

void PopupOverlay::mouseMove (const juce::MouseEvent& e) { const int h = itemAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
void PopupOverlay::mouseDown (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (i >= 0 && items[(size_t) i].enabled) trigger (i); else dismiss();
}
void PopupOverlay::trigger (int i)
{
    auto fn = items[(size_t) i].action;
    dismiss();
    if (fn) fn();
}
bool PopupOverlay::keyPressed (const juce::KeyPress& k)
{
    if (! open) return false;
    if (k.isKeyCode (juce::KeyPress::escapeKey)) { dismiss(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
    const int n = (int) items.size();
    if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::upKey))
    {
        const int d = k.isKeyCode (juce::KeyPress::downKey) ? 1 : -1;
        int i = hover;
        for (int tries = 0; tries < n; ++tries) { i = (i + d + n) % n; if (items[(size_t) i].enabled) break; }
        hover = i; repaint(); return true;
    }
    if (k.isKeyCode (juce::KeyPress::returnKey) && hover >= 0 && items[(size_t) hover].enabled) { trigger (hover); return true; }
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c != 0)
        for (int i = 0; i < n; ++i)
            if (items[(size_t) i].enabled && shortcutOf (items[(size_t) i].text) == c) { trigger (i); return true; }
    return false; // not ours (Space, transport keys, ...): let it travel on to the host
}

//------------------------------------------------------------------------------
UpdateBanner::UpdateBanner()
{
    for (auto* b : { &update, &later, &skip }) addAndMakeVisible (b);
    update.onClick = [this] { if (onUpdate) onUpdate(); };
    later.onClick = [this] { if (onLater) onLater(); };
    skip.onClick = [this] { if (onSkip) onSkip(); };
    setVisible (false);
}
void UpdateBanner::show (const gw::upd::Info& i)
{
    title = "v" + i.version + " AVAILABLE";
    setVisible (true); toFront (false); repaint();
}
void UpdateBanner::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff202020)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kSec.withAlpha (0.85f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (kSec); g.fillEllipse (10.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (juce::Colours::white); g.setFont (uiFont (12.0f, true));
    g.drawText (title, 22, 0, 110, getHeight(), juce::Justification::centredLeft, false);
}
void UpdateBanner::resized()
{
    int x = getWidth() - 6; const int y = 4, h = getHeight() - 8;
    skip.setBounds (x - 118, y, 118, h); x -= 118 + 4;
    later.setBounds (x - 52, y, 52, h); x -= 52 + 4;
    update.setBounds (x - 60, y, 60, h);
}
} // namespace gwui

//==============================================================================
GrowlEditor::Look::Look()
{
    setDefaultSansSerifTypeface (gwui::uiTypeface (false));
    setColour (juce::Slider::rotarySliderFillColourId, kPri);
    setColour (juce::Slider::rotarySliderOutlineColourId, kTrack);
    setColour (juce::Slider::thumbColourId, kSec);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kSec.darker (0.3f));
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kPri.withAlpha (0.55f));
    setColour (juce::ComboBox::arrowColourId, kPri);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kSec.darker (0.5f));
    setColour (juce::TextButton::buttonColourId, kTrack);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void GrowlEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float ring = juce::jmax (3.0f, d * 0.075f), rr = r - ring * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    // knob body
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272727), c.x, c.y - r, juce::Colour (0xff111111), c.x, c.y + r, false));
    g.fillEllipse (c.x - rr + ring, c.y - rr + ring, (rr - ring) * 2.0f, (rr - ring) * 2.0f);
    // track
    juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
    g.setColour (kTrack); g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc (bipolar controls grow from the middle)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float from = a0 + zero * (a1 - a0);
    if (std::fabs (ang - from) > 0.01f)
    {
        juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0.0f, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (kPri.withAlpha (0.25f)); g.strokePath (val, juce::PathStrokeType (ring + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kPri); g.strokePath (val, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // pointer
    const float pr1 = (rr - ring) * 0.35f, pr2 = (rr - ring) * 0.88f;
    juce::Path p; p.startNewSubPath (c.x + std::sin (ang) * pr1, c.y - std::cos (ang) * pr1); p.lineTo (c.x + std::sin (ang) * pr2, c.y - std::cos (ang) * pr2);
    g.setColour (kSec); g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void GrowlEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kPri.withAlpha (0.20f) : kTrack); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? kPri : kDim.withAlpha (highlighted ? 0.9f : 0.6f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const float d = r.getHeight() - 6.0f;
    const float cx = on ? r.getRight() - 3.0f - d : r.getX() + 3.0f;
    g.setColour (on ? kPri : kDim); g.fillRoundedRectangle (cx, r.getY() + 3.0f, d, d, 2.0f);
    g.setFont (uiFont (9.0f, true)); g.setColour (on ? kPri : kDim);
    g.drawText (on ? "ON" : "OFF", r.withTrimmedLeft (on ? 5.0f : d + 6.0f).withTrimmedRight (on ? d + 6.0f : 3.0f), juce::Justification::centred, false);
}

void GrowlEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.brighter (down ? 0.4f : highlighted ? 0.2f : 0.0f)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (kPri.withAlpha (0.55f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

//==============================================================================
namespace gwui
{
VowelView::VowelView (GrowlProcessor& p) : proc (p) { startTimerHz (30); setOpaque (false); }

float VowelView::raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void VowelView::timerCallback()
{
    hostkeys::remember (*this);   // keep track of which host window has the keyboard, so a closed value box / menu can hand it back
    const float now[6] = { proc.vowelDisplay.load(), raw ("shift"), raw ("focus"), raw ("crossover"), raw ("formantAmount"), raw ("formantOn") };
    bool ch = false;
    for (int i = 0; i < 6; ++i) if (std::fabs (now[i] - cached[i]) > 1.0e-4f) { cached[i] = now[i]; ch = true; }
    if (ch) repaint();
}

void VowelView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0f0f0f)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kPri.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const auto plot = r.reduced (8.0f, 6.0f).withTrimmedBottom (12.0f);
    const float f0 = 40.0f, f1 = 10000.0f;
    auto xOf = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / f0) / std::log (f1 / f0); };
    // grid
    g.setFont (uiFont (9.0f)); 
    for (float gf : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = xOf (gf);
        g.setColour (kDim.withAlpha (0.18f)); g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (kDim.withAlpha (0.7f));
        g.drawText (gf >= 1000.0f ? juce::String ((int) (gf / 1000.0f)) + "k" : juce::String ((int) gf), juce::Rectangle<float> (x - 18.0f, plot.getBottom() + 1.0f, 36.0f, 11.0f), juce::Justification::centred, false);
    }
    // clean low band
    const float xc = juce::jlimit (plot.getX(), plot.getRight(), xOf (juce::jmax (f0, cached[3])));
    g.setColour (kSec.withAlpha (0.10f)); g.fillRect (plot.getX(), plot.getY(), xc - plot.getX(), plot.getHeight());
    g.setColour (kSec.withAlpha (0.8f)); g.drawVerticalLine ((int) xc, plot.getY(), plot.getBottom());
    g.setFont (uiFont (9.0f, true)); g.setColour (kSec.withAlpha (0.9f));
    g.drawText ("CLEAN", juce::Rectangle<float> (plot.getX() + 2, plot.getY() + 1, juce::jmax (30.0f, xc - plot.getX() - 4), 11.0f), juce::Justification::centredLeft, false);

    // formant response (sum of the three band-pass resonances)
    float F[3], Q[3];
    gw::detail::vowelFormants (cached[0], cached[1], cached[2], 48000.0f, F, Q);
    const int n = (int) plot.getWidth();
    juce::Path line;
    const float amt = cached[5] > 0.5f ? cached[4] : 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        const float f = f0 * std::pow (f1 / f0, (float) i / (float) n);
        float y = 0.0f;
        for (int b = 0; b < 3; ++b) { const float t = Q[b] * (f / F[b] - F[b] / f); y += gw::detail::kFormantWeight[b] / std::sqrt (1.0f + t * t); }
        y = y * amt + (1.0f - amt) * 0.15f;
        const float py = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, y / 1.15f);
        if (i == 0) line.startNewSubPath (plot.getX(), py); else line.lineTo (plot.getX() + (float) i, py);
    }
    auto fill = line; fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
    const float al = dimmed ? 0.35f : 1.0f;
    g.setGradientFill (juce::ColourGradient (kPri.withAlpha (0.45f * al), 0, plot.getY(), kPri.withAlpha (0.02f), 0, plot.getBottom(), false)); g.fillPath (fill);
    g.setColour (kPri.withAlpha (0.25f * al)); g.strokePath (line, juce::PathStrokeType (5.0f));
    g.setColour (kPri.withAlpha (al)); g.strokePath (line, juce::PathStrokeType (1.8f));
    // formant centre markers
    for (int b = 0; b < 3; ++b)
    {
        const float x = xOf (F[b]);
        g.setColour (kSec.withAlpha (al)); g.fillEllipse (x - 3.0f, plot.getY() + 14.0f + (float) b * 0, 6.0f, 6.0f);
    }
    // vowel name
    g.setFont (uiFont (22.0f, true)); g.setColour (juce::Colours::white.withAlpha (0.9f * al));
    g.drawText (proc.apvts.getParameter ("vowel")->getText (cached[0], 0), r.getRight() - 96.0f, r.getY() + 8.0f, 88.0f, 26.0f, juce::Justification::centredRight, false);
}
} // namespace gwui

//==============================================================================
void GrowlEditor::addKnob (KnobBox& k, const char* id, const char* name)
{
    k.slider.setLookAndFeel (&look);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 86, 18);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setLookAndFeel (&look);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    auto* prm = proc.apvts.getParameter (id);
    // double-click resets to the parameter's default (host-visible change, one undo-able gesture)
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    // value text with unit, e.g. "100 Hz" / "-6.0 dB" / "OH>AH"
    k.slider.textFromValueFunction = [prm] (double v)
    {
        auto t = prm->getText (prm->convertTo0to1 ((float) v), 0);
        const auto u = prm->getLabel();
        return u.isEmpty() ? t : t + " " + u;
    };
    k.slider.updateText();
    paramOf.push_back ({ &k.slider, id });
    k.slider.onContextMenu = [this] (gwui::Knob& kn, juce::Point<int> screenPos) { showParamMenu (kn, getLocalPoint (nullptr, screenPos)); };
}

void GrowlEditor::addCombo (Combo& c, const char* id, const char* name)
{
    c.box.setLookAndFeel (&look);
    c.box.addItemList (proc.apvts.getParameter (id)->getAllValueStrings(), 1);
    addAndMakeVisible (c.box);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setLookAndFeel (&look);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c.box);
    paramOf.push_back ({ &c.box, id }); c.box.addMouseListener (this, true);
}

void GrowlEditor::addTog (Tog& t, const char* id, const char* name)
{
    t.button.setLookAndFeel (&look);
    addAndMakeVisible (t.button);
    t.label.setText (name, juce::dontSendNotification);
    t.label.setJustificationType (juce::Justification::centredRight);
    t.label.setLookAndFeel (&look);
    addAndMakeVisible (t.label);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t.button);
    paramOf.push_back ({ &t.button, id }); t.button.addMouseListener (this, false);
}

void GrowlEditor::applySectionState (Section& s, bool on)
{
    s.on = on;
    s.power.setToggleState (on, juce::dontSendNotification);
    for (auto* c : s.members) c->setAlpha (on ? 1.0f : 0.4f);
    if (&s == &formSec) vowelView.setDimmed (! on);
    repaint();
}

void GrowlEditor::addSection (Section& s, const char* paramId)
{
    s.power.setLookAndFeel (&look);
    s.power.setClickingTogglesState (false);
    addAndMakeVisible (s.power);
    auto* prm = proc.apvts.getParameter (paramId);
    s.att = std::make_unique<juce::ParameterAttachment> (*prm, [this, &s] (float v) { applySectionState (s, v > 0.5f); }, nullptr);
    s.power.onClick = [&s] { s.att->setValueAsCompleteGesture (s.on ? 0.0f : 1.0f); };
    paramOf.push_back ({ &s.power, paramId }); s.power.addMouseListener (this, false);
    s.att->sendInitialUpdate();
}

GrowlEditor::GrowlEditor (GrowlProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob (inGain, "inGain", "INPUT");   addKnob (xover, "crossover", "CROSSOVER"); addKnob (subLevel, "subLevel", "SUB LEVEL");
    addKnob (drive, "drive", "DRIVE");     addKnob (bias, "bias", "BIAS");
    addKnob (vowel, "vowel", "VOWEL");     addKnob (shift, "shift", "SHIFT");  addKnob (focus, "focus", "FOCUS"); addKnob (fAmt, "formantAmount", "AMOUNT");
    addKnob (lfoDepth, "lfoDepth", "LFO DEPTH");
    addKnob (postCut, "postCut", "CUTOFF"); addKnob (postRes, "postRes", "RESONANCE");
    addKnob (mix, "mix", "MIX");           addKnob (outGain, "outGain", "OUTPUT");
    addCombo (satType, "satType", "TYPE");  addCombo (os, "oversampling", "OVERSAMPLING");
    addCombo (lfoRate, "lfoRate", "LFO RATE"); addCombo (lfoShape, "lfoShape", "LFO SHAPE");
    addCombo (postType, "postType", "FILTER");
    addTog (subMono, "subMono", "SUB MONO"); addTog (limiter, "limiter", "SAFETY LIMITER");
    addAndMakeVisible (vowelView);

    driveSec.members = { &drive.slider, &drive.label, &bias.slider, &bias.label, &satType.box, &satType.label, &os.box, &os.label };
    formSec.members  = { &vowel.slider, &vowel.label, &shift.slider, &shift.label, &focus.slider, &focus.label, &fAmt.slider, &fAmt.label,
                         &lfoRate.box, &lfoRate.label, &lfoShape.box, &lfoShape.label, &lfoDepth.slider, &lfoDepth.label };
    postSec.members  = { &postType.box, &postType.label, &postCut.slider, &postCut.label, &postRes.slider, &postRes.label };
    addSection (driveSec, "driveOn"); addSection (formSec, "formantOn"); addSection (postSec, "postOn");

    addChildComponent (banner);
    banner.setLookAndFeel (&look);
    for (auto* b : { &banner.update, &banner.later, &banner.skip }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    banner.onUpdate = [this]
    {
        const auto page = gw::upd::pageForUrl (bannerInfo.url);
        if (gw::upd::isTrustedUrl (page)) juce::URL (page).launchInDefaultBrowser();
        banner.setVisible (false); presetBar.setVisible (true); footerNote = {}; repaint();
    };
    banner.onLater = [this] { banner.setVisible (false); presetBar.setVisible (true); footerNote = {}; repaint(); };
    banner.onSkip = [this]
    {
        auto s = gw::upd::Settings::load(); s.skipVersion = bannerInfo.version; s.save();
        banner.setVisible (false); presetBar.setVisible (true); footerNote = {}; repaint();
    };
    addChildComponent (popup);
    // preset bar (top right; the update banner takes its place while shown).  Mouse only: no part of it ever takes keyboard focus.
    addAndMakeVisible (presetBar); addChildComponent (presetMenu);
    presetBar.setLookAndFeel (&look);
    presetBar.font = presetMenu.font = [] (float h) { return uiFont (h); };
    presetBar.bg = kPanel; presetBar.line = kSec.withAlpha (0.35f); presetBar.dim = kDim;
    presetMenu.bg = juce::Colour (0xff202020); presetMenu.line = kSec.withAlpha (0.7f); presetMenu.hi = kTrack; presetMenu.tick = kPri; presetMenu.dim = kDim;
    presetBar.showMenu = [this] (std::vector<pk::MenuItem> items, juce::Rectangle<int> a) { popup.dismiss(); presetMenu.show (a + presetBar.getPosition(), std::move (items)); };
    presetBar.afterDialog = [this] { gwui::hostkeys::releaseLater (*this); };
    presetMenu.onDismiss = [this] { gwui::hostkeys::release (*this); };

    updatesOn = gw::upd::Settings::load().enabled;
    setWantsKeyboardFocus (false);            // never hold keyboard focus: Space & co. must keep driving the host transport
    setMouseClickGrabsKeyboardFocus (false);
    // belt and braces: no child (incl. the sliders' value labels) may keep keyboard focus - only the popup while open, and text editors
    std::function<void (juce::Component&)> noFocus = [&] (juce::Component& comp)
    {
        if (&comp != &popup) { comp.setWantsKeyboardFocus (false); comp.setMouseClickGrabsKeyboardFocus (false); }
        for (auto* ch : comp.getChildren()) noFocus (*ch);
    };
    noFocus (*this);
    popup.setWantsKeyboardFocus (false);
    setSize (840, 540);
    // update check: after the editor is up, on a background thread, at most once per 24 h, silent on failure
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GrowlEditor> (this)] { if (sp != nullptr) sp->startUpdateCheck(); });
}

GrowlEditor::~GrowlEditor()
{
    checker.cancelAndWait();
    for (auto* k : { &inGain, &xover, &subLevel, &drive, &bias, &vowel, &shift, &focus, &fAmt, &lfoDepth, &postCut, &postRes, &mix, &outGain })
    { k->slider.setLookAndFeel (nullptr); k->label.setLookAndFeel (nullptr); }
    for (auto* c : { &satType, &os, &lfoRate, &lfoShape, &postType }) { c->box.setLookAndFeel (nullptr); c->label.setLookAndFeel (nullptr); }
    for (auto* t : { &subMono, &limiter }) { t->button.setLookAndFeel (nullptr); t->label.setLookAndFeel (nullptr); }
    for (auto* s : { &driveSec, &formSec, &postSec }) s->power.setLookAndFeel (nullptr);
    banner.setLookAndFeel (nullptr);
    presetBar.setLookAndFeel (nullptr);
}

//==============================================================================
void GrowlEditor::startUpdateCheck (bool force)
{
    if (! force && ! gw::upd::Settings::load().enabled) return;
    checker.start ([this, force] (const gw::upd::Result& r)
    {
        if (r.ok && gw::upd::shouldNotify (r, gw::upd::Settings::load(), force)) showBanner (r.info);
        else if (force) { footerNote = r.ok ? juce::String ("Growl ") + GW_VERSION_STRING + " is up to date" : juce::String ("Could not reach the update server"); repaint(); }
    }, force);
}

void GrowlEditor::showBanner (const gw::upd::Info& i)
{
    bannerInfo = i;
    banner.show (i); presetBar.setVisible (false); presetMenu.dismiss();
    footerNote = i.notes.isNotEmpty() ? "What's new: " + i.notes : juce::String();
    resized(); repaint();
}

juce::Rectangle<int> GrowlEditor::updateLinkBounds() const { return footerArea.withTrimmedLeft (footerArea.getWidth() - 190).withTrimmedRight (14).reduced (0, 2); }

void GrowlEditor::showUpdateMenu()
{
    const bool on = gw::upd::Settings::load().enabled;
    std::vector<gwui::PopupOverlay::Item> v;
    v.push_back ({ "Check for &updates automatically", [this, on]
                   { auto s = gw::upd::Settings::load(); s.enabled = ! on; s.save(); updatesOn = s.enabled;
                     footerNote = s.enabled ? "Update check on (once a day, nothing is sent about you)" : "Update check off: the plugin never contacts the network"; repaint(); }, true, on });
    v.push_back ({ "Check &now", [this] { footerNote = "Checking..."; repaint(); startUpdateCheck (true); }, true, false });
    auto b = updateLinkBounds();
    popup.show ({ b.getRight() - 230, b.getY() - 70 }, std::move (v));
}

void GrowlEditor::resetAll()
{
    for (auto* prm : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
}

juce::String GrowlEditor::paramIdOf (juce::Component* c) const
{
    for (; c != nullptr && c != this; c = c->getParentComponent())
        for (auto& pr : paramOf) if (pr.first == c) return pr.second;
    return {};
}

// Right-click menu: our own items (with underlined shortcut letters) plus the host's own parameter menu when the host
// offers one (VST3 IComponentHandler3 -> automation lane, MIDI learn, ...). Without a host menu there is an "Automate" item
// that touches the parameter so the host registers it as the last-touched / automatable one.
void GrowlEditor::showParamMenu (juce::Component& source, juce::Point<int> pos)
{
    const auto id = paramIdOf (&source);
    auto* prm = id.isNotEmpty() ? proc.apvts.getParameter (id) : nullptr;
    juce::Component::SafePointer<gwui::Knob> knob (dynamic_cast<gwui::Knob*> (&source));
    std::vector<gwui::PopupOverlay::Item> v;
    if (prm != nullptr)
    {
        v.push_back ({ knob != nullptr ? "&Reset to default\tdouble-click" : "&Reset to default", [prm]
                       { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); } });
        if (knob != nullptr)
        {
            v.push_back ({ "&Type in value...", [knob] { if (knob != nullptr) knob->showTextBox(); } });
            v.push_back ({ "&Copy value", [knob] { if (knob != nullptr) juce::SystemClipboard::copyTextToClipboard (knob->getTextFromValue (knob->getValue())); }, true, false, true });
            const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
            const bool pasteOk = clip.isNotEmpty() && clip.length() < 24 && clip.containsAnyOf ("0123456789");
            v.push_back ({ "&Paste value", [knob, clip] { if (knob != nullptr) knob->setValue (knob->getValueFromText (clip), juce::sendNotificationSync); }, pasteOk });
        }
        std::shared_ptr<juce::HostProvidedContextMenu> host;
        if (auto* hc = getHostContext())
            if (auto m = hc->getContextMenuForParameter (prm)) host = std::move (m);
        if (host != nullptr)
        {
            // flatten the host's menu (submenus get a "Name > " prefix); each entry runs the host's own action
            bool first = true;
            std::function<void (const juce::PopupMenu&, const juce::String&)> add = [&] (const juce::PopupMenu& m, const juce::String& prefix)
            {
                juce::PopupMenu::MenuItemIterator it (m);
                while (it.next())
                {
                    auto& item = it.getItem();
                    if (item.isSeparator) { first = true; continue; }
                    if (item.subMenu != nullptr) { add (*item.subMenu, prefix + item.text.replace ("&", "") + " > "); continue; }
                    if (item.text.isEmpty() || v.size() > 14) continue;
                    auto act = item.action;
                    v.push_back ({ prefix + item.text.replace ("&", ""), [host, act] { if (act) act(); }, item.isEnabled && (bool) act, item.isTicked, first });
                    first = false;
                }
            };
            add (host->getEquivalentPopupMenu(), {});
        }
        else
        {
            v.push_back ({ "&Automate", [this, prm]
                           { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getValue()); prm->endChangeGesture();
                             footerNote = "Selected for automation - now use your host's automation command for this parameter"; repaint(); }, true, false, true });
        }
    }
    v.push_back ({ "&Init all controls", [this] { resetAll(); }, true, false, true });
    popup.show (pos, std::move (v));
}

void GrowlEditor::demoKnobMenu() { showParamMenu (postCut.slider, postCut.slider.getBounds().getCentre()); }

void GrowlEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* c = e.eventComponent; c != nullptr && c != this && paramIdOf (c).isNotEmpty())
        { showParamMenu (*c, getLocalPoint (nullptr, e.getScreenPosition())); return; }
    }
    else if (updateLinkBounds().contains (e.getPosition())) showUpdateMenu();
}

// Walks every component: nothing except the open popup / a text editor may want keyboard focus; unhandled keys must not be consumed.
juce::String GrowlEditor::auditKeyboard()
{
    juce::String out; int n = 0, bad = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        ++n;
        if (c.getWantsKeyboardFocus() && &c != &popup && dynamic_cast<juce::TextEditor*> (&c) == nullptr)
        { ++bad; out << "  wants focus: " << c.getName() << " (" << typeid (c).name() << ")\n"; }
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    walk (*this);
    out << "  " << n << " components, " << bad << " ask for keyboard focus (expected 0)\n";
    const bool closedWants = popup.getWantsKeyboardFocus();
    out << "  popup closed: wantsFocus=" << (int) closedWants << " (expected 0)\n";
    demoKnobMenu();
    const bool openWants = popup.getWantsKeyboardFocus();
    const bool spaceUsed = popup.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    const bool zUsed     = popup.keyPressed (juce::KeyPress ('z', juce::ModifierKeys(), 'z'));
    const bool rUsed     = popup.isOpen() && popup.keyPressed (juce::KeyPress ('r', juce::ModifierKeys(), 'r'));    // R = Reset, closes the menu
    out << "  menu open: wantsFocus=" << (int) openWants << " (expected 1)  Space consumed=" << (int) spaceUsed << " (expected 0)  unknown letter consumed=" << (int) zUsed
        << " (expected 0)  'R' shortcut consumed+closed=" << (int) (rUsed && ! popup.isOpen()) << " (expected 1)\n";
    demoKnobMenu(); popup.dismiss();
    const bool afterSpace = popup.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    out << "  after dismiss: open=" << (int) popup.isOpen() << " wantsFocus=" << (int) popup.getWantsKeyboardFocus() << " Space consumed=" << (int) afterSpace
        << "  editor wantsFocus=" << (int) getWantsKeyboardFocus() << " editor keyPressed(Space)=" << (int) keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) << " (all expected 0)\n";
    const bool ok = bad == 0 && ! closedWants && openWants && ! spaceUsed && ! zUsed && rUsed && ! popup.getWantsKeyboardFocus() && ! afterSpace && ! getWantsKeyboardFocus();
    out << (ok ? "KEYBOARD AUDIT OK\n" : "KEYBOARD AUDIT FAILED\n");
    return out;
}

//==============================================================================
void GrowlEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    // faint scan lines
    g.setColour (juce::Colours::white.withAlpha (0.015f));
    for (int y = 0; y < getHeight(); y += 4) g.fillRect (0, y, getWidth(), 1);
    g.setColour (kPri);
    g.setFont (uiFont (24.0f, true));
    g.drawText ("CMPLXTY // GROWL", 16, 7, 330, 32, juce::Justification::centredLeft);
    g.setColour (kSec);
    g.fillRect (16, 42, 150, 2);
    g.setColour (kPri); g.fillRect (166, 42, 24, 2);
    auto panel = [&] (juce::Rectangle<int> r, const char* t, bool on)
    {
        g.setColour (kPanel);          g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kPri.withAlpha (on ? 0.35f : 0.15f)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (kSec.withAlpha (on ? 0.9f : 0.4f)); g.fillRect (r.getX() + 1, r.getY() + 8, 2, 12);
        g.setColour (on ? kDim : kDim.withAlpha (0.5f)); g.setFont (uiFont (12.0f, true));
        g.drawText (t, r.getX() + 10, r.getY() + 4, 150, 16, juce::Justification::centredLeft);
    };
    panel (inArea, "INPUT // SPLIT", true); panel (driveArea, "DRIVE", driveSec.on); panel (formArea, "FORMANT // VOWEL", formSec.on);
    panel (postArea, "POST FILTER", postSec.on); panel (outArea, "OUT", true);
    // LFO sub-block heading
    g.setColour (formSec.on ? kDim : kDim.withAlpha (0.5f)); g.setFont (uiFont (10.5f, true));
    g.drawText ("TEMPO-SYNCED VOWEL LFO", lfoArea.getX(), formArea.getY() + 5, lfoArea.getWidth(), 14, juce::Justification::centredLeft);
    g.setColour (kPri.withAlpha (0.18f)); g.fillRect (lfoArea.getX() - 6, formArea.getY() + 24, 1, formArea.getHeight() - 32);

    // footer: version, notes, update switch
    g.setFont (uiFont (11.0f));
    g.setColour (kDim.withAlpha (0.8f));
    g.drawText (juce::String ("v") + GW_VERSION_STRING + "  -  free, GPLv3  -  cmplxty.music", footerArea.getX() + 16, footerArea.getY(), 230, footerArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (footerNote.isNotEmpty() ? kSec.withAlpha (0.9f) : kDim);
    g.drawFittedText (footerNote, footerArea.getX() + 250, footerArea.getY(), footerArea.getWidth() - 250 - 210, footerArea.getHeight(), juce::Justification::centredLeft, 1);
    auto ul = updateLinkBounds();
    g.setColour (updatesOn ? kPri.withAlpha (0.85f) : kDim);
    g.drawText (updatesOn ? "UPDATE CHECK: ON" : "UPDATE CHECK: OFF", ul, juce::Justification::centredRight);
}

void GrowlEditor::resized()
{
    auto full = getLocalBounds();
    footerArea = full.removeFromBottom (22);
    auto area = full.reduced (12, 0).withTrimmedTop (50).withTrimmedBottom (6);
    auto rowA = area.removeFromTop (142); area.removeFromTop (8);
    auto rowB = area.removeFromTop (170); area.removeFromTop (8);
    auto rowC = area;

    inArea = rowA.removeFromLeft (390); rowA.removeFromLeft (8); driveArea = rowA;
    formArea = rowB;
    postArea = rowC.removeFromLeft (470); rowC.removeFromLeft (8); outArea = rowC;

    auto knobAt = [] (KnobBox& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16)); k.slider.setBounds (r);
    };
    auto comboAt = [] (Combo& c, juce::Rectangle<int> r)
    {
        c.label.setBounds (r.removeFromTop (15)); c.box.setBounds (r.removeFromTop (26));
    };
    auto body = [] (juce::Rectangle<int> a) { return a.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4); };

    { // input / split
        auto r = body (inArea); const int w = r.getWidth() / 3;
        knobAt (inGain, r.removeFromLeft (w)); knobAt (xover, r.removeFromLeft (w)); knobAt (subLevel, r);
        subMono.button.setBounds (inArea.getRight() - 50, inArea.getY() + 4, 42, 16);
        subMono.label.setBounds (inArea.getRight() - 50 - 84, inArea.getY() + 4, 80, 16);
    }
    { // drive
        auto r = body (driveArea);
        auto combos = r.removeFromRight (150).reduced (4, 8);
        const int w = r.getWidth() / 2;
        knobAt (drive, r.removeFromLeft (w)); knobAt (bias, r);
        comboAt (satType, combos.removeFromTop (44)); combos.removeFromTop (8); comboAt (os, combos.removeFromTop (44));
    }
    { // formant
        auto r = body (formArea);
        vowelView.setBounds (r.removeFromLeft (258).reduced (0, 2).withTrimmedBottom (2));
        r.removeFromLeft (6);
        lfoArea = r.removeFromRight (200);
        const int w = r.getWidth() / 4;
        knobAt (vowel, r.removeFromLeft (w)); knobAt (shift, r.removeFromLeft (w)); knobAt (focus, r.removeFromLeft (w)); knobAt (fAmt, r);
        auto l = lfoArea.withTrimmedTop (0).withTrimmedLeft (0);
        auto kn = l.removeFromRight (92);
        auto cb = l.reduced (0, 14).withTrimmedRight (6); cb.setY (cb.getY() + 8);
        comboAt (lfoRate, cb.removeFromTop (44)); cb.removeFromTop (8); comboAt (lfoShape, cb.removeFromTop (44));
        knobAt (lfoDepth, kn.withTrimmedTop (0));
    }
    { // post
        auto r = body (postArea);
        auto cb = r.removeFromLeft (120).reduced (4, 12);
        comboAt (postType, cb.removeFromTop (44));
        const int w = r.getWidth() / 2;
        knobAt (postCut, r.removeFromLeft (w)); knobAt (postRes, r);
    }
    { // out
        auto r = body (outArea);
        auto tg = r.removeFromRight (96).reduced (4, 0);
        const int w = r.getWidth() / 2;
        knobAt (mix, r.removeFromLeft (w)); knobAt (outGain, r);
        limiter.label.setBounds (tg.getX() - 4, tg.getCentreY() - 26, tg.getWidth() + 8, 16); limiter.label.setJustificationType (juce::Justification::centred);
        limiter.button.setBounds (tg.getCentreX() - 21, tg.getCentreY() - 6, 42, 18);
    }
    for (auto [sec, a] : { std::pair<Section*, juce::Rectangle<int>> { &driveSec, driveArea }, { &formSec, formArea }, { &postSec, postArea } })
        sec->power.setBounds (a.getRight() - 50, a.getY() + 4, 42, 16);

    banner.setBounds (getWidth() - 12 - 392, 9, 392, 28);
    presetBar.setBounds (getWidth() - 12 - 330, 10, 330, 26);
    presetMenu.setBounds (getLocalBounds());
    popup.setBounds (getLocalBounds());
}

// Test hook: drives the right-click path end to end - with and without a host-provided parameter menu.
namespace
{
struct FakeHostMenu final : juce::HostProvidedContextMenu
{
    std::shared_ptr<int> hits;
    explicit FakeHostMenu (std::shared_ptr<int> h) : hits (std::move (h)) {}
    juce::PopupMenu getEquivalentPopupMenu() const override
    {
        juce::PopupMenu m, sub; auto h = hits;
        m.addItem (juce::PopupMenu::Item ("Show automation lane for parameter").setAction ([h] { ++*h; }));
        sub.addItem (juce::PopupMenu::Item ("Create automation clip").setAction ([h] { *h += 10; }));
        m.addSeparator(); m.addSubMenu ("Host", sub);
        return m;
    }
    void showNativeMenu (juce::Point<int>) const override {}
};
struct FakeHost final : juce::AudioProcessorEditorHostContext
{
    std::shared_ptr<int> hits = std::make_shared<int> (0);
    int asked = 0;
    std::unique_ptr<juce::HostProvidedContextMenu> getContextMenuForParameter (const juce::AudioProcessorParameter*) const override
    { const_cast<FakeHost*> (this)->asked++; return std::make_unique<FakeHostMenu> (hits); }
};
struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};
}

juce::String GrowlEditor::auditMenus()
{
    juce::String out; bool ok = true;
    auto check = [&] (bool cond, const juce::String& what) { out << (cond ? "  ok   " : "  FAIL ") << what << "\n"; ok = ok && cond; };
    auto rightClick = [this] (juce::Component& c)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, juce::Point<float> (4.0f, 4.0f), juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, now, juce::Point<float> (4.0f, 4.0f), now, 1, false);
        c.mouseDown (e);                         // the component's own handler (Knob menu hook / Btn ignores right-click) ...
        for (auto* p = &c; p != nullptr && p != this; p = p->getParentComponent()) {}
        mouseDown (e);                            // ... and the editor's MouseListener path used for combos / switches
        c.mouseUp (e);
    };
    auto* pw = proc.apvts.getParameter ("postOn");
    GestureCounter gc;

    // 1) no host menu: custom menu, with an Automate item
    popup.dismiss();
    rightClick (postCut.slider);
    auto t = popup.itemTexts();
    out << "  knob menu (no host): " << t.joinIntoString (" | ") << "\n";
    check (popup.isOpen() && t.size() >= 5, "right-click on a knob opens the menu");
    check (t.contains ("&Reset to default\tdouble-click") && t.joinIntoString ("|").contains ("&Automate"), "menu has Reset and Automate with shortcut letters");
    auto* cutP = proc.apvts.getParameter ("postCut"); cutP->addListener (&gc);
    check (popup.keyPressed (juce::KeyPress ('a', juce::ModifierKeys(), 'a')) && ! popup.isOpen() && gc.begins == 1 && gc.ends == 1, "pressing A runs Automate: touches the parameter (begin/end gesture), closes menu");
    cutP->removeListener (&gc);

    // 2) switch right-click: menu opens, state unchanged, host item present
    const float before = pw->getValue();
    rightClick (postSec.power);
    check (popup.isOpen() && pw->getValue() == before, "right-click on a section switch opens the menu and does NOT toggle it");
    t = popup.itemTexts(); out << "  switch menu: " << t.joinIntoString (" | ") << "\n";
    popup.dismiss();
    rightClick (satType.box);
    check (popup.isOpen() && ! popup.itemTexts().contains ("&Type in value..."), "right-click on a combo opens the (no 'type in') menu");
    popup.dismiss();
    rightClick (subMono.button);
    check (popup.isOpen(), "right-click on a plain toggle opens the menu");
    popup.dismiss();

    // 3) with a host-provided menu
    FakeHost host; setHostContext (&host);
    rightClick (postCut.slider);
    t = popup.itemTexts(); out << "  knob menu (host): " << t.joinIntoString (" | ") << "\n";
    check (host.asked >= 1, "asked host context for the parameter menu");
    check (t.joinIntoString ("|").contains ("Show automation lane") && t.joinIntoString ("|").contains ("Host > Create automation clip") && ! t.joinIntoString ("|").contains ("&Automate"),
           "host items shown (submenu flattened), custom Automate replaced");
    check (popup.activate ("Show automation lane") && *host.hits == 1, "clicking a host item runs the host's own action");
    rightClick (postCut.slider); popup.activate ("Create automation clip");
    check (*host.hits == 11, "submenu host item runs too");
    rightClick (postSec.power);
    check (popup.isOpen() && popup.itemTexts().joinIntoString ("|").contains ("Show automation lane"), "host menu also offered on section switch");
    popup.dismiss();
    setHostContext (nullptr);
    out << (ok ? "MENU AUDIT OK\n" : "MENU AUDIT FAILED\n");
    return out;
}
