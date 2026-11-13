#include "Ui.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kPri (0xffffffff), kSec (0xffb8b8b8), kDim (0xff9f9f9f);

using gzui::uiFont;

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

namespace gzui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, (size_t) BinaryData::BarlowRegular_ttfSize);
    static const juce::Typeface::Ptr semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, (size_t) BinaryData::BarlowSemiBold_ttfSize);
    return bold ? semi : regular;
}
} // namespace gzui

//==============================================================================
namespace gzui
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

} // namespace gzui
