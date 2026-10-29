#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff080d0a), kPanel (0xff111a15), kPri (0xffa6ff00), kSec (0xffff3d6e), kDim (0xff8ea597), kTrack (0xff233128);
constexpr int kFooterH = 22;

using dfui::uiFont;

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

namespace dfui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, (size_t) BinaryData::BarlowRegular_ttfSize);
    static const juce::Typeface::Ptr semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, (size_t) BinaryData::BarlowSemiBold_ttfSize);
    return bold ? semi : regular;
}
} // namespace dfui

//==============================================================================
namespace dfui
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
    g.setColour (juce::Colour (0xff17231c)); g.fillRoundedRectangle (panel.toFloat(), 6.0f);
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
void UpdateBanner::show (const df::upd::Info& i)
{
    title = "v" + i.version + " AVAILABLE";
    setVisible (true); toFront (false); repaint();
}
void UpdateBanner::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff17231c)); g.fillRoundedRectangle (r, 6.0f);
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
} // namespace dfui

//==============================================================================
using DriftPanel = dfui::DriftPanel;

DriftPanel::Look::Look()
{
    setDefaultSansSerifTypeface (dfui::uiTypeface (false));
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

void DriftPanel::Look::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool down)
{
    g.setColour (kPri.withAlpha (down ? 1.0f : over ? 0.9f : 0.5f));
    for (int i = 1; i <= 3; ++i)
    {
        const float o = (float) i * 4.0f;
        g.drawLine ((float) w - o, (float) h - 1.0f, (float) w - 1.0f, (float) h - o, 1.4f);
    }
}

void DriftPanel::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float ring = juce::jmax (3.0f, d * 0.075f), rr = r - ring * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    // knob body
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2b23), c.x, c.y - r, juce::Colour (0xff0c130f), c.x, c.y + r, false));
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

void DriftPanel::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
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

void DriftPanel::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.brighter (down ? 0.4f : highlighted ? 0.2f : 0.0f)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (kPri.withAlpha (0.55f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

//==============================================================================
namespace dfui
{
namespace
{
constexpr float kFMin = 20.0f, kFMax = 20000.0f, kDbBottom = -96.0f;
void effectiveCrossovers (float lo, float hi, float& l, float& h) { l = lo; h = (std::max) (hi, lo * 1.25f); }
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
juce::String noteName (int n) { return n < 0 ? juce::String ("--") : juce::String (kNoteNames[n % 12]) + juce::String (n / 12 - 1); }
}

SpectrumView::SpectrumView (DriftProcessor& p) : proc (p)
{
    setOpaque (false); setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
    order = 12; an.init (order);
    inDb.assign ((size_t) an.bins(), -120.0f); outDb = inShown = outShown = inDb; samples.assign ((size_t) an.size(), 0.0f);
    startTimerHz (30);
}

float SpectrumView::raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
juce::Rectangle<float> SpectrumView::plotArea() const { return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (14.0f); }
float SpectrumView::xOfHz (float hz) const { const auto r = plotArea(); return r.getX() + r.getWidth() * std::log (juce::jlimit (kFMin, kFMax, hz) / kFMin) / std::log (kFMax / kFMin); }
float SpectrumView::hzOfX (float x) const { const auto r = plotArea(); return kFMin * std::pow (kFMax / kFMin, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }

void SpectrumView::timerCallback()
{
    auto& eng = proc.getEngine();
    eng.setDisplayActive (true);                       // the audio thread only feeds the taps while a display is watching
    const bool e = raw ("eco") > 0.5f;
    if (e != eco) { eco = e; startTimerHz (eco ? 12 : 30); }
    const int ord = eco ? 10 : 12;
    if (ord != order)
    {
        order = ord; an.init (order);
        inDb.assign ((size_t) an.bins(), -120.0f); outDb = inShown = outShown = inDb; samples.assign ((size_t) an.size(), 0.0f);
    }
    const float fall = eco ? 6.0f : 2.5f;
    auto update = [&] (const df::SampleTap& tap, std::vector<float>& db, std::vector<float>& shown)
    {
        if (tap.latest (samples.data(), an.size())) an.analyse (samples.data(), db.data());
        for (size_t i = 0; i < shown.size(); ++i) shown[i] = (std::max) (db[i], shown[i] - fall);
    };
    update (eng.inTap(), inDb, inShown);
    update (eng.outTap(), outDb, outShown);
    const char* ids[] = { "xoLow", "xoHigh", "lowBand", "highBand", "soloLow", "soloMid", "soloHigh", "coarse", "fine", "shiftOn", "mix" };
    for (int i = 0; i < 11; ++i) cache[i] = raw (ids[i]);
    repaint();
}

int SpectrumView::handleAt (float x) const
{
    float l, h; effectiveCrossovers (cache[0], cache[1], l, h);
    const float dl = std::fabs (x - xOfHz (l)), dh = std::fabs (x - xOfHz (h));
    if ((std::min) (dl, dh) > 9.0f) return -1;
    return dl <= dh ? 0 : 1;
}

void SpectrumView::setCrossover (int which, float hz)
{
    float l, h; effectiveCrossovers (raw ("xoLow"), raw ("xoHigh"), l, h);
    auto* prm = proc.apvts.getParameter (which == 0 ? "xoLow" : "xoHigh");
    if (which == 0) hz = juce::jlimit (40.0f, (std::min) (8000.0f, h / 1.25f), hz);
    else            hz = juce::jlimit ((std::max) (80.0f, l * 1.25f), 16000.0f, hz);
    prm->setValueNotifyingHost (prm->convertTo0to1 (hz));
}

void SpectrumView::mouseMove (const juce::MouseEvent& e) { const int h = handleAt (e.position.x); if (h != hot) { hot = h; repaint(); } }
void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position.x);
    if (e.mods.isPopupMenu()) { if (h >= 0 && onMenu) onMenu (h == 0 ? "xoLow" : "xoHigh", e.getScreenPosition()); return; }
    drag = h;
    if (drag >= 0) proc.apvts.getParameter (drag == 0 ? "xoLow" : "xoHigh")->beginChangeGesture();
}
void SpectrumView::mouseDrag (const juce::MouseEvent& e) { if (drag >= 0 && ! e.mods.isPopupMenu()) setCrossover (drag, hzOfX (e.position.x)); }
void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (drag >= 0) proc.apvts.getParameter (drag == 0 ? "xoLow" : "xoHigh")->endChangeGesture();
    drag = -1;
}
void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position.x);
    if (h < 0) return;
    auto* prm = proc.apvts.getParameter (h == 0 ? "xoLow" : "xoHigh");
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0a110d)); g.fillRoundedRectangle (r, 6.0f);
    const auto plot = plotArea();
    auto yOf = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kDbBottom) / -kDbBottom); };

    // grid
    g.setFont (uiFont (9.0f));
    for (float db : { -24.0f, -48.0f, -72.0f })
    {
        g.setColour (kDim.withAlpha (0.14f)); g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (kDim.withAlpha (0.55f)); g.drawText (juce::String ((int) db), (int) plot.getX() + 2, (int) yOf (db) - 11, 26, 10, juce::Justification::centredLeft, false);
    }
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOfHz (f);
        g.setColour (kDim.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.2f : 0.1f)); g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (kDim.withAlpha (0.75f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 18.0f, plot.getBottom() + 2.0f, 36.0f, 11.0f), juce::Justification::centred, false);
    }

    // bands
    float xoL, xoH; effectiveCrossovers (cache[0], cache[1], xoL, xoH);
    const bool lowOn = cache[2] > 0.5f, highOn = cache[3] > 0.5f, shiftOn = cache[9] > 0.5f;
    const bool sLow = cache[4] > 0.5f && lowOn, sMid = cache[5] > 0.5f, sHigh = cache[6] > 0.5f && highOn, anySolo = sLow || sMid || sHigh;
    const float xl = xOfHz (xoL), xh = xOfHz (xoH), x0 = plot.getX(), x1 = plot.getRight();
    const float sFrom = lowOn ? xl : x0, sTo = highOn ? xh : x1;
    g.setColour ((shiftOn ? kPri : kDim).withAlpha (shiftOn ? 0.075f : 0.05f)); g.fillRect (sFrom, plot.getY(), sTo - sFrom, plot.getHeight());
    auto hatch = [&] (float a, float b)
    {
        g.saveState(); g.reduceClipRegion (juce::Rectangle<float> (a, plot.getY(), b - a, plot.getHeight()).toNearestInt());
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (a, plot.getY(), b - a, plot.getHeight());
        g.setColour (kDim.withAlpha (0.12f));
        for (float x = a - plot.getHeight(); x < b; x += 9.0f) g.drawLine (x, plot.getBottom(), x + plot.getHeight(), plot.getY(), 1.0f);
        g.restoreState();
    };
    if (! lowOn)  hatch (x0, xl);
    if (! highOn) hatch (xh, x1);
    if (anySolo)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        if (! sLow)  g.fillRect (x0, plot.getY(), xl - x0, plot.getHeight());
        if (! sMid)  g.fillRect (xl, plot.getY(), xh - xl, plot.getHeight());
        if (! sHigh) g.fillRect (xh, plot.getY(), x1 - xh, plot.getHeight());
    }

    // curves: one value per pixel column (max over the bins that fall into it, interpolated where a column is narrower than a bin)
    const double sr = proc.getEngine().sampleRate();
    const float binW = (float) (sr / (double) an.size());
    const int nb = an.bins();
    const float step = eco ? 3.0f : 1.0f;
    auto build = [&] (const std::vector<float>& db, juce::Path& line, juce::Path& fill)
    {
        bool first = true; float lastX = x0;
        for (float x = x0; x <= x1 + 0.01f; x += step)
        {
            const float fa = hzOfX (x) / binW, fb = hzOfX (x + step) / binW;
            float v;
            if (fb - fa < 1.0f) { const float c = 0.5f * (fa + fb); const int i = juce::jlimit (0, nb - 2, (int) c); const float t = juce::jlimit (0.0f, 1.0f, c - (float) i); v = db[(size_t) i] * (1 - t) + db[(size_t) i + 1] * t; }
            else { v = -200.0f; for (int i = juce::jlimit (0, nb - 1, (int) fa); i <= juce::jlimit (0, nb - 1, (int) fb); ++i) v = (std::max) (v, db[(size_t) i]); }
            const float y = yOf (v);
            if (first) { line.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); fill.lineTo (x, y); first = false; } else { line.lineTo (x, y); fill.lineTo (x, y); }
            lastX = x;
        }
        fill.lineTo (lastX, plot.getBottom()); fill.closeSubPath();
    };
    g.saveState(); g.reduceClipRegion (plot.expanded (0, 2).toNearestInt());
    { juce::Path l, f; build (inShown, l, f);
      g.setColour (kDim.withAlpha (0.13f)); g.fillPath (f); g.setColour (kDim.withAlpha (0.8f)); g.strokePath (l, juce::PathStrokeType (1.1f)); }
    { juce::Path l, f; build (outShown, l, f);
      if (eco) { g.setColour (kPri.withAlpha (0.16f)); g.fillPath (f); }
      else { g.setGradientFill (juce::ColourGradient (kPri.withAlpha (0.38f), 0, plot.getY(), kPri.withAlpha (0.02f), 0, plot.getBottom(), false)); g.fillPath (f);
             g.setColour (kPri.withAlpha (0.22f)); g.strokePath (l, juce::PathStrokeType (4.5f)); }
      g.setColour (kPri); g.strokePath (l, juce::PathStrokeType (eco ? 1.3f : 1.7f)); }
    g.restoreState();

    // band captions
    const float total = cache[7] + cache[8];
    g.setFont (uiFont (11.0f, true));
    auto caption = [&] (float x, const juce::String& t, juce::Colour c) { g.setColour (c); g.drawText (t, juce::Rectangle<float> (x + 6.0f, plot.getY() + 3.0f, 230.0f, 14.0f), juce::Justification::centredLeft, false); };
    caption (x0, lowOn ? "LOW  - dry" : "LOW  - removed", kDim.withAlpha (0.85f));
    caption (xl, shiftOn ? "MID  - shifted " + juce::String (total >= 0 ? "+" : "") + juce::String (total, 1) + " Hz" : "MID  - shift off", (shiftOn ? kPri : kDim).withAlpha (0.95f));
    if (xh + 120.0f < x1 || highOn) caption (xh, highOn ? "HIGH - dry" : "HIGH - removed", kDim.withAlpha (0.85f));

    // crossover handles
    for (int i = 0; i < 2; ++i)
    {
        const float x = i == 0 ? xl : xh;
        const bool active = hot == i || drag == i;
        g.setColour (kSec.withAlpha (active ? 1.0f : 0.8f)); g.fillRect (x - 0.75f, plot.getY(), 1.5f, plot.getHeight());
        const auto grip = juce::Rectangle<float> (x - 6.0f, plot.getCentreY() - 15.0f, 12.0f, 30.0f);
        g.setColour (juce::Colour (0xff1a0f13)); g.fillRoundedRectangle (grip, 3.0f);
        g.setColour (kSec.withAlpha (active ? 1.0f : 0.9f)); g.drawRoundedRectangle (grip, 3.0f, active ? 1.8f : 1.2f);
        for (int k = -1; k <= 1; ++k) g.fillRect (x - 3.0f, grip.getCentreY() + (float) k * 5.0f - 0.5f, 6.0f, 1.0f);
        const float hz = i == 0 ? xoL : xoH;
        const juce::String t = hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        g.setFont (uiFont (10.5f, true)); g.setColour (kSec);
        const bool left = x + 70.0f > x1;
        const float tw = textWidth (uiFont (10.5f, true), t) + 8.0f;
        const juce::Rectangle<float> chip (left ? x - 9.0f - tw : x + 9.0f, grip.getCentreY() - 8.0f, tw, 16.0f);
        g.setColour (juce::Colour (0xe60a110d)); g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (kSec.withAlpha (0.6f)); g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        g.setColour (kSec); g.drawText (t, chip, juce::Justification::centred, false);
    }

    // legend
    g.setFont (uiFont (10.0f, true));
    float lx = r.getRight() - 14.0f;
    auto legend = [&] (const juce::String& t, juce::Colour c) { const float w = textWidth (uiFont (10.0f, true), t) + 16.0f; lx -= w; g.setColour (c); g.fillEllipse (lx, plot.getY() + 7.0f, 6.0f, 6.0f); g.drawText (t, juce::Rectangle<float> (lx + 9.0f, plot.getY() + 3.0f, w, 14.0f), juce::Justification::centredLeft, false); };
    legend ("OUTPUT", kPri); legend ("INPUT", kDim);
    if (eco) legend ("ECO", kSec);

    g.setColour (kPri.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}
} // namespace dfui

//==============================================================================
void DriftPanel::addKnob (KnobBox& k, const char* id, const char* name)
{
    k.slider.setLookAndFeel (&look);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 86, 18);   // read-only: a click on the number must not open an editor (typing is in the right-click menu)
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setLookAndFeel (&look);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    auto* prm = proc.apvts.getParameter (id);
    // double-click resets to the parameter's default (host-visible change, one undo-able gesture)
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    // value text with unit, e.g. "+300 Hz" / "50 %"; typing goes through the parameter's own parser ("+300", "-1.5k", "50")
    k.slider.textFromValueFunction = [prm] (double v)
    {
        auto t = prm->getText (prm->convertTo0to1 ((float) v), 0);
        const auto u = prm->getLabel();
        return u.isEmpty() ? t : t + " " + u;
    };
    k.slider.valueFromTextFunction = [prm] (const juce::String& s) { return (double) prm->convertFrom0to1 (prm->getValueForText (s)); };
    k.slider.updateText();
    paramOf.push_back ({ &k.slider, id });
    k.slider.onContextMenu = [this, id = juce::String (id)] (dfui::Knob& kn, juce::Point<int> screenPos) { showParamMenu (id, &kn, getLocalPoint (nullptr, screenPos)); };
}

void DriftPanel::addCombo (Combo& c, const char* id, const char* name)
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

void DriftPanel::addTog (Tog& t, const char* id, const char* name)
{
    t.button.setLookAndFeel (&look);
    addAndMakeVisible (t.button);
    t.label.setText (name, juce::dontSendNotification);
    t.label.setJustificationType (juce::Justification::centredLeft);
    t.label.setLookAndFeel (&look);
    addAndMakeVisible (t.label);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t.button);
    paramOf.push_back ({ &t.button, id }); t.button.addMouseListener (this, false);
}

void DriftPanel::applySectionState (Section& s, bool on)
{
    s.on = on;
    s.power.setToggleState (on, juce::dontSendNotification);
    for (auto* c : s.members) c->setAlpha (on ? 1.0f : 0.4f);
    repaint();
}

void DriftPanel::addSection (Section& s, const char* paramId, const char* name)
{
    s.power.setLookAndFeel (&look);
    s.power.setClickingTogglesState (false);
    addAndMakeVisible (s.power);
    s.label.setText (name, juce::dontSendNotification);
    s.label.setJustificationType (juce::Justification::centredRight);
    s.label.setLookAndFeel (&look);
    addAndMakeVisible (s.label);
    auto* prm = proc.apvts.getParameter (paramId);
    s.att = std::make_unique<juce::ParameterAttachment> (*prm, [this, &s] (float v) { applySectionState (s, v > 0.5f); }, &proc.undoManager);
    s.power.onClick = [&s] { s.att->setValueAsCompleteGesture (s.on ? 0.0f : 1.0f); };
    paramOf.push_back ({ &s.power, paramId }); s.power.addMouseListener (this, false);
    s.att->sendInitialUpdate();
}

DriftPanel::DriftPanel (DriftProcessor& p, juce::AudioProcessorEditor& o) : proc (p), owner (o)
{
    addAndMakeVisible (spec);
    addKnob (coarse, "coarse", "COARSE SHIFT"); addKnob (fine, "fine", "FINE SHIFT");
    addKnob (spread, "spread", "L / R SPREAD");
    addKnob (mix, "mix", "MIX"); addKnob (feedback, "feedback", "FEEDBACK");
    addKnob (keyAmt, "keyAmt", "HZ PER SEMITONE");
    addKnob (xoLow, "xoLow", "CROSSOVER"); addKnob (xoHigh, "xoHigh", "CROSSOVER");
    addCombo (slope, "slope", "CROSSOVER SLOPE");
    addTog (lowBand, "lowBand", "BAND"); addTog (highBand, "highBand", "BAND");
    addTog (soloLow, "soloLow", "SOLO"); addTog (soloMid, "soloMid", "SOLO"); addTog (soloHigh, "soloHigh", "SOLO");
    addTog (eco, "eco", "ECO DISPLAY");

    shiftSec.members = { &coarse.slider, &coarse.label, &fine.slider, &fine.label };
    wideSec.members  = { &spread.slider, &spread.label };
    keySec.members   = { &keyAmt.slider, &keyAmt.label };
    addSection (shiftSec, "shiftOn", ""); addSection (wideSec, "wide", "WIDE"); addSection (keySec, "keyOn", "");

    spec.onMenu = [this] (const juce::String& id, juce::Point<int> screenPos) { showParamMenu (id, nullptr, getLocalPoint (nullptr, screenPos)); };

    for (auto* b : { &undoBtn, &redoBtn })
    {
        b->setLookAndFeel (&look); addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
    }
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    redoBtn.onClick = [this] { proc.undoManager.redo(); };

    addChildComponent (banner);
    banner.setLookAndFeel (&look);
    for (auto* b : { &banner.update, &banner.later, &banner.skip }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    banner.onUpdate = [this]
    {
        const auto page = df::upd::pageForUrl (bannerInfo.url);
        if (df::upd::isTrustedUrl (page)) juce::URL (page).launchInDefaultBrowser();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    banner.onLater = [this] { banner.setVisible (false); footerNote = {}; repaint(); };
    banner.onSkip = [this]
    {
        auto s = df::upd::Settings::load(); s.skipVersion = bannerInfo.version; s.save();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    addChildComponent (popup);

    updatesOn = df::upd::Settings::load().enabled;
    setWantsKeyboardFocus (false);            // never hold keyboard focus: Space & co. must keep driving the host transport
    setMouseClickGrabsKeyboardFocus (false);
    // belt and braces: no child (incl. the sliders' value labels) may keep keyboard focus - only the popup while open, and text editors
    hostkeys::noKeyboardFocus (*this, &popup);
    popup.setWantsKeyboardFocus (false);
    proc.getEngine().setDisplayActive (true);
    startTimerHz (10);                        // remembers the host's focused window; safety net, see timerCallback
    setSize (DriftProcessor::kBaseW, DriftProcessor::kBaseH);
    // update check: after the editor is up, on a background thread, at most once per 24 h, silent on failure
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<DriftPanel> (this)] { if (sp != nullptr) sp->startUpdateCheck(); });
}

DriftPanel::~DriftPanel()
{
    stopTimer();
    checker.cancelAndWait();
    proc.getEngine().setDisplayActive (false);
    for (auto* k : knobs()) { k->slider.setLookAndFeel (nullptr); k->label.setLookAndFeel (nullptr); }
    slope.box.setLookAndFeel (nullptr); slope.label.setLookAndFeel (nullptr);
    for (auto* t : { &lowBand, &highBand, &soloLow, &soloMid, &soloHigh, &eco }) { t->button.setLookAndFeel (nullptr); t->label.setLookAndFeel (nullptr); }
    for (auto* s : { &shiftSec, &wideSec, &keySec }) { s->power.setLookAndFeel (nullptr); s->label.setLookAndFeel (nullptr); }
    undoBtn.setLookAndFeel (nullptr); redoBtn.setLookAndFeel (nullptr);
    banner.setLookAndFeel (nullptr);
}

// Only acts when the plug-in window really holds the native keyboard focus and nothing needs it (no popup, no value being typed, no combo menu):
// then the focus goes back to the host. Normally it never fires, because nothing in the editor asks for focus.
void DriftPanel::timerCallback()
{
    hostkeys::remember (*this);
   #if DF_FOCUS_NET
    if (! popup.isOpen()) hostkeys::release (*this);
   #endif
    const int n = proc.heldNote.load (std::memory_order_relaxed);
    if (n != shownNote) { shownNote = n; repaint (keyArea); }
    undoBtn.setAlpha (proc.undoManager.canUndo() ? 1.0f : 0.4f);
    redoBtn.setAlpha (proc.undoManager.canRedo() ? 1.0f : 0.4f);
}

//==============================================================================
void DriftPanel::startUpdateCheck (bool force)
{
    if (! force && ! df::upd::Settings::load().enabled) return;
    checker.start ([this, force] (const df::upd::Result& r)
    {
        if (r.ok && df::upd::shouldNotify (r, df::upd::Settings::load(), force)) showBanner (r.info);
        else if (force) { footerNote = r.ok ? juce::String ("Drift ") + DF_VERSION_STRING + " is up to date" : juce::String ("Could not reach the update server"); repaint(); }
    }, force);
}

void DriftPanel::showBanner (const df::upd::Info& i)
{
    bannerInfo = i;
    banner.show (i);
    footerNote = i.notes.isNotEmpty() ? "What's new: " + i.notes : juce::String();
    resized(); repaint();
}

juce::Rectangle<int> DriftPanel::updateLinkBounds() const { return footerArea.withTrimmedLeft (footerArea.getWidth() - 190).withTrimmedRight (14).reduced (0, 2); }

void DriftPanel::showUpdateMenu()
{
    const bool on = df::upd::Settings::load().enabled;
    std::vector<dfui::PopupOverlay::Item> v;
    v.push_back ({ "Check for &updates automatically", [this, on]
                   { auto s = df::upd::Settings::load(); s.enabled = ! on; s.save(); updatesOn = s.enabled;
                     footerNote = s.enabled ? "Update check on (once a day, nothing is sent about you)" : "Update check off: the plugin never contacts the network"; repaint(); }, true, on });
    v.push_back ({ "Check &now", [this] { footerNote = "Checking..."; repaint(); startUpdateCheck (true); }, true, false });
    auto b = updateLinkBounds();
    popup.show ({ b.getRight() - 230, b.getY() - 70 }, std::move (v));
}

void DriftPanel::resetAll()
{
    for (auto* prm : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
}

juce::String DriftPanel::paramIdOf (juce::Component* c) const
{
    for (; c != nullptr && c != this; c = c->getParentComponent())
        for (auto& pr : paramOf) if (pr.first == c) return pr.second;
    return {};
}

// Right-click menu: our own items (with underlined shortcut letters) plus the host's own parameter menu when the host
// offers one (VST3 IComponentHandler3 -> automation lane, MIDI learn, ...). Without a host menu there is an "Automate" item
// that touches the parameter so the host registers it as the last-touched / automatable one.
void DriftPanel::showParamMenu (const juce::String& id, dfui::Knob* knobPtr, juce::Point<int> pos)
{
    auto* prm = id.isNotEmpty() ? proc.apvts.getParameter (id) : nullptr;
    juce::Component::SafePointer<dfui::Knob> knob (knobPtr);
    std::vector<dfui::PopupOverlay::Item> v;
    if (prm != nullptr)
    {
        const bool dbl = knob != nullptr || id == "xoLow" || id == "xoHigh";
        v.push_back ({ dbl ? "&Reset to default\tdouble-click" : "&Reset to default", [prm]
                       { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); } });
        if (knob != nullptr)
        {
            v.push_back ({ "&Type in value...", [knob] { if (knob != nullptr) knob->typeValue(); } });
            v.push_back ({ "&Copy value", [knob] { if (knob != nullptr) juce::SystemClipboard::copyTextToClipboard (knob->getTextFromValue (knob->getValue())); }, true, false, true });
            const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
            const bool pasteOk = clip.isNotEmpty() && clip.length() < 24 && clip.containsAnyOf ("0123456789");
            v.push_back ({ "&Paste value", [knob, clip, prm] { if (knob != nullptr) { prm->beginChangeGesture(); knob->setValue (knob->getValueFromText (clip), juce::sendNotificationSync); prm->endChangeGesture(); } }, pasteOk });
        }
        std::shared_ptr<juce::HostProvidedContextMenu> host;
        if (auto* hc = owner.getHostContext())
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

void DriftPanel::demoKnobMenu() { showParamMenu ("mix", &mix.slider, mix.slider.getBounds().getCentre()); }

void DriftPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* c = e.eventComponent; c != nullptr && c != this)
            if (const auto id = paramIdOf (c); id.isNotEmpty())
            { showParamMenu (id, dynamic_cast<dfui::Knob*> (c), getLocalPoint (nullptr, e.getScreenPosition())); return; }
    }
    else if (updateLinkBounds().contains (e.getPosition())) showUpdateMenu();
}

// Walks every component: nothing except the open popup / a text editor may want keyboard focus; unhandled keys must not be consumed.
juce::String DriftPanel::auditKeyboard()
{
    juce::String out; int n = 0, bad = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        ++n;
        if (c.getWantsKeyboardFocus() && &c != &popup && dynamic_cast<juce::TextEditor*> (&c) == nullptr)
        { ++bad; out << "  wants focus: " << c.getName() << " (" << typeid (c).name() << ")\n"; }
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    // the editor above us must not want it either (and neither may its resize corner)
    std::function<void (juce::Component&)> walkUp = [&] (juce::Component& c)
    {
        if (&c != this && c.getWantsKeyboardFocus()) { ++bad; out << "  editor-level component wants focus: " << typeid (c).name() << "\n"; }
        for (auto* ch : c.getChildren()) if (ch != this) walkUp (*ch);
    };
    // Slider turns focus back on for its value box on look-and-feel / enablement changes: provoke both and make sure the knobs fix it up again
    for (auto* ks : knobs()) { ks->slider.setEnabled (false); ks->slider.setEnabled (true); ks->slider.sendLookAndFeelChange(); }
    // negative control: a stock juce::Slider really does ask for keyboard focus (so the walk below could fail)
    {
        juce::Slider stock (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        stock.setBounds (0, 0, 80, 80); int stockWants = 0;
        for (auto* c : stock.getChildren()) stockWants += c->getWantsKeyboardFocus() ? 1 : 0;
        out << "  negative control: stock slider's value box wants focus=" << stockWants << " (expected 1)\n";
        if (stockWants != 1) ++bad;
    }
    walk (*this);
    walkUp (owner);
    if (owner.getWantsKeyboardFocus()) { ++bad; out << "  editor wants focus\n"; }
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
        << "  panel wantsFocus=" << (int) getWantsKeyboardFocus() << " panel keyPressed(Space)=" << (int) keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) << " (all expected 0)\n";
    const bool ok = bad == 0 && ! closedWants && openWants && ! spaceUsed && ! zUsed && rUsed && ! popup.getWantsKeyboardFocus() && ! afterSpace && ! getWantsKeyboardFocus();
    out << (ok ? "KEYBOARD AUDIT OK\n" : "KEYBOARD AUDIT FAILED\n");
    return out;
}

//==============================================================================
void DriftPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    // faint scan lines
    g.setColour (juce::Colours::white.withAlpha (0.015f));
    for (int y = 0; y < getHeight(); y += 4) g.fillRect (0, y, getWidth(), 1);
    g.setColour (kPri);
    g.setFont (uiFont (24.0f, true));
    g.drawText ("CMPLXTY // DRIFT", 16, 7, 330, 32, juce::Justification::centredLeft);
    g.setColour (kSec);
    g.fillRect (16, 42, 150, 2);
    g.setColour (kPri); g.fillRect (166, 42, 24, 2);
    g.setColour (kDim); g.setFont (uiFont (10.5f, true));
    g.drawText ("FREQUENCY SHIFTER", 200, 36, 140, 12, juce::Justification::centredLeft);

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& t, bool on)
    {
        g.setColour (kPanel);          g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kPri.withAlpha (on ? 0.35f : 0.15f)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (kSec.withAlpha (on ? 0.9f : 0.4f)); g.fillRect (r.getX() + 1, r.getY() + 8, 2, 12);
        g.setColour (on ? kDim : kDim.withAlpha (0.5f)); g.setFont (uiFont (12.0f, true));
        g.drawText (t, r.getX() + 10, r.getY() + 4, 190, 16, juce::Justification::centredLeft);
    };
    panel (bandArea[0], "LOW BAND", lowBand.button.getToggleState());
    panel (bandArea[1], "MID BAND  -  THE SHIFTED ONE", true);
    panel (bandArea[2], "HIGH BAND", highBand.button.getToggleState());
    panel (shiftArea, "SHIFT", shiftSec.on); panel (stereoArea, "STEREO", wideSec.on); panel (outArea, "FEEDBACK  //  MIX", true); panel (keyArea, "KEYTRACK", keySec.on);

    // keytrack read-out: held MIDI note and the offset it adds
    {
        auto r = keyArea.withTrimmedTop (keyArea.getHeight() - 24).reduced (10, 2);
        const int note = shownNote;
        g.setFont (uiFont (11.0f, true)); g.setColour (keySec.on ? kDim : kDim.withAlpha (0.5f));
        g.drawText ("NOTE", r.removeFromLeft (40), juce::Justification::centredLeft);
        g.setColour (note >= 0 && keySec.on ? kSec : kDim.withAlpha (0.7f)); g.setFont (uiFont (14.0f, true));
        g.drawText (noteName (note), r.removeFromLeft (50), juce::Justification::centredLeft);
        if (note >= 0 && keySec.on)
        {
            const float off = proc.apvts.getRawParameterValue ("keyAmt")->load() * (float) (juce::jlimit (0, 96, note) - 60);
            g.setFont (uiFont (11.0f)); g.setColour (kPri); g.drawText ((off >= 0 ? "+" : "") + juce::String (off, 1) + " Hz", r, juce::Justification::centredRight);
        }
    }

    // footer: version, notes, update switch
    g.setFont (uiFont (11.0f));
    g.setColour (kDim.withAlpha (0.8f));
    g.drawText (juce::String ("v") + DF_VERSION_STRING + "  -  free, GPLv3  -  cmplxty.music", footerArea.getX() + 16, footerArea.getY(), 230, footerArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (footerNote.isNotEmpty() ? kSec.withAlpha (0.9f) : kDim);
    g.drawFittedText (footerNote, footerArea.getX() + 250, footerArea.getY(), footerArea.getWidth() - 250 - 210, footerArea.getHeight(), juce::Justification::centredLeft, 1);
    auto ul = updateLinkBounds();
    g.setColour (updatesOn ? kPri.withAlpha (0.85f) : kDim);
    g.drawText (updatesOn ? "UPDATE CHECK: ON" : "UPDATE CHECK: OFF", ul, juce::Justification::centredRight);
}

void DriftPanel::resized()
{
    const int W = getWidth();
    footerArea = { 0, getHeight() - 24, W, 24 };
    spec.setBounds (12, 52, W - 24, 210);

    const int bw[3] = { 282, 296, 282 };
    { int x = 12; for (int i = 0; i < 3; ++i) { bandArea[i] = { x, 268, bw[i], 120 }; x += bw[i] + 8; } }
    shiftArea = { 12, 394, 330, 168 }; stereoArea = { 350, 394, 160, 168 }; outArea = { 518, 394, 200, 168 }; keyArea = { 726, 394, 162, 168 };

    auto knobAt = [] (KnobBox& k, juce::Rectangle<int> r, int labelH = 16) { k.label.setBounds (r.removeFromTop (labelH)); k.slider.setBounds (r); };
    auto body = [] (juce::Rectangle<int> a) { return a.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4); };
    auto togRow = [] (Tog& t, int x, int y, int labelW) { t.label.setBounds (x, y, labelW, 18); t.button.setBounds (x + labelW, y, 42, 18); };

    { auto r = body (bandArea[0]); knobAt (xoLow, r.removeFromLeft (112), 14);  const int x = r.getX() + 14;
      togRow (lowBand, x, r.getY() + 14, 56); togRow (soloLow, x, r.getY() + 44, 56); }
    { auto r = body (bandArea[2]); knobAt (xoHigh, r.removeFromLeft (112), 14); const int x = r.getX() + 14;
      togRow (highBand, x, r.getY() + 14, 56); togRow (soloHigh, x, r.getY() + 44, 56); }
    { auto r = body (bandArea[1]);
      auto left = r.removeFromLeft (140).reduced (2, 12);
      slope.label.setBounds (left.removeFromTop (15)); slope.box.setBounds (left.removeFromTop (26));
      const int x = r.getX() + 22;
      togRow (soloMid, x, r.getY() + 14, 84); togRow (eco, x, r.getY() + 44, 84); }

    { auto r = body (shiftArea); const int w = r.getWidth() / 2; knobAt (coarse, r.removeFromLeft (w)); knobAt (fine, r); }
    { auto r = body (stereoArea); knobAt (spread, r); }
    { auto r = body (outArea); const int w = r.getWidth() / 2; knobAt (feedback, r.removeFromLeft (w)); knobAt (mix, r); }
    { auto r = body (keyArea).withTrimmedBottom (22); knobAt (keyAmt, r); }

    for (auto [sec, a] : { std::pair<Section*, juce::Rectangle<int>> { &shiftSec, shiftArea }, { &wideSec, stereoArea }, { &keySec, keyArea } })
    {
        sec->power.setBounds (a.getRight() - 50, a.getY() + 4, 42, 16);
        sec->label.setBounds (a.getRight() - 50 - 58, a.getY() + 4, 54, 16);
    }
    undoBtn.setBounds (352, 12, 56, 24); redoBtn.setBounds (412, 12, 56, 24);
    banner.setBounds (W - 12 - 392, 9, 392, 28);
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


juce::String DriftPanel::auditMenus()
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
        mouseDown (e);                            // ... and the panel's MouseListener path used for combos / switches
        c.mouseUp (e);
    };
    auto* pw = proc.apvts.getParameter ("shiftOn");
    GestureCounter gc;

    // 1) no host menu: custom menu, with an Automate item
    popup.dismiss();
    rightClick (mix.slider);
    auto t = popup.itemTexts();
    out << "  knob menu (no host): " << t.joinIntoString (" | ") << "\n";
    check (popup.isOpen() && t.size() >= 5, "right-click on a knob opens the menu");
    check (t.contains ("&Reset to default\tdouble-click") && t.joinIntoString ("|").contains ("&Automate"), "menu has Reset and Automate with shortcut letters");
    auto* mixP = proc.apvts.getParameter ("mix"); mixP->addListener (&gc);
    check (popup.keyPressed (juce::KeyPress ('a', juce::ModifierKeys(), 'a')) && ! popup.isOpen() && gc.begins == 1 && gc.ends == 1, "pressing A runs Automate: touches the parameter (begin/end gesture), closes menu");
    mixP->removeListener (&gc);

    // 2) switch right-click: menu opens, state unchanged
    const float before = pw->getValue();
    rightClick (shiftSec.power);
    check (popup.isOpen() && pw->getValue() == before, "right-click on a section switch opens the menu and does NOT toggle it");
    t = popup.itemTexts(); out << "  switch menu: " << t.joinIntoString (" | ") << "\n";
    popup.dismiss();
    rightClick (slope.box);
    check (popup.isOpen() && ! popup.itemTexts().contains ("&Type in value..."), "right-click on a combo opens the (no 'type in') menu");
    popup.dismiss();
    auto* soloP = proc.apvts.getParameter ("soloMid"); const float soloBefore = soloP->getValue();
    rightClick (soloMid.button);
    check (popup.isOpen() && soloP->getValue() == soloBefore, "right-click on a plain toggle opens the menu without flipping it");
    popup.dismiss();
    spec.onMenu ("xoLow", { 10, 10 });
    check (popup.isOpen() && popup.itemTexts().joinIntoString ("|").contains ("&Reset to default"), "right-click on a crossover handle opens the menu");
    popup.dismiss();

    // 3) with a host-provided menu
    FakeHost host; owner.setHostContext (&host);
    rightClick (mix.slider);
    t = popup.itemTexts(); out << "  knob menu (host): " << t.joinIntoString (" | ") << "\n";
    check (host.asked >= 1, "asked host context for the parameter menu");
    check (t.joinIntoString ("|").contains ("Show automation lane") && t.joinIntoString ("|").contains ("Host > Create automation clip") && ! t.joinIntoString ("|").contains ("&Automate"),
           "host items shown (submenu flattened), custom Automate replaced");
    check (popup.activate ("Show automation lane") && *host.hits == 1, "clicking a host item runs the host's own action");
    rightClick (mix.slider); popup.activate ("Create automation clip");
    check (*host.hits == 11, "submenu host item runs too");
    rightClick (shiftSec.power);
    check (popup.isOpen() && popup.itemTexts().joinIntoString ("|").contains ("Show automation lane"), "host menu also offered on section switch");
    popup.dismiss();
    owner.setHostContext (nullptr);

    // 4) typed values go through the parameter's parser (signed values, "k" suffix)
    coarse.slider.setValue (coarse.slider.getValueFromText ("+1.5k"), juce::sendNotificationSync);
    check (std::fabs (proc.apvts.getRawParameterValue ("coarse")->load() - 1500.0f) < 1.0f, "typing \"+1.5k\" into Coarse Shift gives +1500 Hz");
    coarse.slider.setValue (coarse.slider.getValueFromText ("-250"), juce::sendNotificationSync);
    check (std::fabs (proc.apvts.getRawParameterValue ("coarse")->load() + 250.0f) < 1.0f, "typing \"-250\" gives -250 Hz");
    coarse.slider.setValue (0.0, juce::sendNotificationSync);
    out << (ok ? "MENU AUDIT OK\n" : "MENU AUDIT FAILED\n");
    return out;
}

//==============================================================================
DriftEditor::DriftEditor (DriftProcessor& p) : AudioProcessorEditor (&p), proc (p), panel (p, *this)
{
    setLookAndFeel (&panel.look);
    addAndMakeVisible (panel);
    constrainer.setFixedAspectRatio ((double) DriftProcessor::kBaseW / (double) DriftProcessor::kBaseH);
    constrainer.setMinimumSize (540, 354);
    constrainer.setMaximumSize (2160, 1416);
    setConstrainer (&constrainer);
    setResizable (true, true);                 // free resizing (host window frame or the corner handle), fixed aspect ratio
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    dfui::hostkeys::noKeyboardFocus (*this, &panel.popupOverlay());   // includes the resize corner
    addMouseListener (this, true);
    const int w = p.uiWidth.load(), h = p.uiHeight.load();
    if (w >= 540 && h >= 354) setSize (w, h); else setSize (DriftProcessor::kBaseW, DriftProcessor::kBaseH);
}

DriftEditor::~DriftEditor() { removeMouseListener (this); setLookAndFeel (nullptr); }

void DriftEditor::applySavedSize()
{
    const int w = proc.uiWidth.load(), h = proc.uiHeight.load();
    if (w >= 540 && h >= 354 && (w != getWidth() || h != getHeight())) setSize (w, h);
}

void DriftEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) DriftProcessor::kBaseW, (float) getHeight() / (float) DriftProcessor::kBaseH));
    panel.setBounds (0, 0, DriftProcessor::kBaseW, DriftProcessor::kBaseH);
    proc.uiWidth.store (getWidth()); proc.uiHeight.store (getHeight());
}
