// Shared drawing helpers, fonts and look for the Quartz UI (monochrome: greys, black, white).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "BinaryData.h"

namespace ui
{
using juce::Colour; using juce::Graphics; using juce::Rectangle; using juce::Path; using juce::Justification;

inline constexpr float kPi = 3.14159265358979f;
inline float rad (float deg) { return deg * kPi / 180.0f; }
inline Colour grey (float v) { const auto c = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v)); return Colour::fromRGB (c, c, c); }
inline juce::Point<float> P (float cx, float cy, float r, float deg) { const float a = rad (deg - 90.0f); return { cx + r * std::cos (a), cy + r * std::sin (a) }; }
inline void arcPath (Path& p, float cx, float cy, float r, float a0, float a1) { p.addCentredArc (cx, cy, r, r, 0.0f, rad (a0), rad (a1), true); }

struct Fonts
{
    static juce::Typeface::Ptr make (const void* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); }
    static juce::Typeface::Ptr cinzel() { static auto t = make (BinaryData::CinzelSemiBold_ttf, BinaryData::CinzelSemiBold_ttfSize); return t; }
    static juce::Typeface::Ptr inter()  { static auto t = make (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize); return t; }
    static juce::Typeface::Ptr mono()   { static auto t = make (BinaryData::GeistMonoRegular_ttf, BinaryData::GeistMonoRegular_ttfSize); return t; }
    static juce::Font make (juce::Typeface::Ptr t, float h, float spacingPx)
    {
        // CSS font-size is the em size; JUCE's "point height" is the matching measure.
        juce::Font f { juce::FontOptions (t).withPointHeight (h) };
        if (spacingPx != 0.0f) f.setExtraKerningFactor (spacingPx / f.getHeight());
        return f;
    }
    static juce::Font title (float h, float spacingPx) { return make (cinzel(), h, spacingPx); }
    static juce::Font ui (float h, float spacingPx = 0.0f) { return make (inter(), h, spacingPx); }
    static juce::Font monoF (float h) { return make (mono(), h, 0.0f); }
};

inline void text (Graphics& g, const juce::String& s, Rectangle<float> r, const juce::Font& f, Colour c, Justification j = Justification::centred)
{
    g.setColour (c); g.setFont (f); g.drawText (s, r, j, false);
}
inline float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0); return ga.getBoundingBox (0, -1, true).getWidth();
}

// diamond (rotated square) of side s, centred
inline void diamond (Graphics& g, float cx, float cy, float side, Colour fill, Colour stroke, float sw = 1.0f, bool filled = true)
{
    const float d = side * 0.7071f;
    Path p; p.startNewSubPath (cx, cy - d); p.lineTo (cx + d, cy); p.lineTo (cx, cy + d); p.lineTo (cx - d, cy); p.closeSubPath();
    if (filled) { g.setColour (fill); g.fillPath (p); }
    if (sw > 0) { g.setColour (stroke); g.strokePath (p, juce::PathStrokeType (sw)); }
}

// power icon, 12x12 box with top-left (x,y)
inline void powerIcon (Graphics& g, float x, float y, Colour c)
{
    Path p; p.addCentredArc (x + 6.0f, y + 6.2f, 4.3f, 4.3f, 0.0f, 0.62f, 2.0f * kPi - 0.62f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (x + 6.0f, y + 0.9f, x + 6.0f, y + 5.4f, 1.4f);
}

// panel with the two cut corners (top-left and bottom-right)
inline Path cutPanelPath (Rectangle<float> r, float cut)
{
    Path p; p.startNewSubPath (r.getX() + cut, r.getY()); p.lineTo (r.getRight(), r.getY()); p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.lineTo (r.getX(), r.getY() + cut); p.closeSubPath(); return p;
}
inline void cutPanel (Graphics& g, Rectangle<float> r, float cut, Colour top, Colour bottom, Colour border, bool fullOutline = false)
{
    auto p = cutPanelPath (r, cut);
    g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bottom, r.getBottomRight(), false));
    g.fillPath (p);
    g.setColour (border);
    if (fullOutline) { g.strokePath (p, juce::PathStrokeType (1.0f)); return; }
    const auto b = r.reduced (0.5f);
    Path a; a.startNewSubPath (b.getX(), b.getY() + cut); a.lineTo (b.getX(), b.getBottom()); a.lineTo (b.getRight() - cut, b.getBottom());
    Path c; c.startNewSubPath (b.getX() + cut, b.getY()); c.lineTo (b.getRight(), b.getY()); c.lineTo (b.getRight(), b.getBottom() - cut);
    g.strokePath (a, juce::PathStrokeType (1.0f)); g.strokePath (c, juce::PathStrokeType (1.0f));
}

// faceted knob cap + arcs. (x,y,s) square; v normalised, zero = normalised bipolar origin or <0 for unipolar
inline void drawFacetKnob (Graphics& g, float x, float y, float s, float v, float zeroNorm, float alpha)
{
    const float c = s * 0.5f, cx = x + c, cy = y + c, rt = c - 6.0f;
    const float a0 = -135.0f, a1 = 135.0f, av = a0 + (a1 - a0) * v;
    g.saveState();
    g.setOpacity (alpha);
    {
        Path t; arcPath (t, cx, cy, rt, a0, a1);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.strokePath (t, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float origin = zeroNorm >= 0.0f ? a0 + (a1 - a0) * zeroNorm : a0;
        const float lo = std::min (av, origin), hi = std::max (av, origin);
        if (hi - lo > 0.5f)
        {
            Path a; arcPath (a, cx, cy, rt, lo, hi);
            g.setColour (juce::Colour (0xfff2f2f2));
            g.strokePath (a, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    const float R = rt - 8.0f, rin = R * 0.5f, rot = av;
    for (int i = 0; i < 8; ++i)
    {
        const float A1 = rot + i * 45.0f - 22.5f, A2 = A1 + 45.0f, Am = A1 + 22.5f;
        const auto p1 = P (cx, cy, R, A1), p2 = P (cx, cy, R, A2), q1 = P (cx, cy, rin, A1 + 22.5f - 11.0f), q2 = P (cx, cy, rin, A2 + 22.5f - 11.0f);
        const float light = std::cos (rad (Am + 45.0f));
        const float lv = 60.0f + (light * 0.5f + 0.5f) * 150.0f;
        Path poly; poly.startNewSubPath (p1); poly.lineTo (p2); poly.lineTo (q2); poly.lineTo (q1); poly.closeSubPath();
        g.setColour (grey (lv * 0.55f)); g.fillPath (poly);
        g.setColour (juce::Colour (0xff0b0b0b)); g.strokePath (poly, juce::PathStrokeType (0.8f));
    }
    {
        Path t;
        for (int i = 0; i < 8; ++i) { const auto q = P (cx, cy, rin, rot + i * 45.0f + 22.5f - 11.0f); if (i == 0) t.startNewSubPath (q); else t.lineTo (q); }
        t.closeSubPath();
        g.setColour (juce::Colour (0xff161616)); g.fillPath (t);
        g.setColour (juce::Colour (0xff6a6a6a)); g.strokePath (t, juce::PathStrokeType (0.8f));
    }
    const auto l1 = P (cx, cy, R - 1.0f, av), l2 = P (cx, cy, rin + 2.0f, av);
    g.setColour (juce::Colours::white);
    g.drawLine (juce::Line<float> (l1, l2), 2.4f);   // rounded caps below
    g.fillEllipse (l1.x - 1.2f, l1.y - 1.2f, 2.4f, 2.4f); g.fillEllipse (l2.x - 1.2f, l2.y - 1.2f, 2.4f, 2.4f);
    g.restoreState();
}

struct Lcg
{
    uint32_t s; explicit Lcg (uint32_t seed) : s (seed) {}
    float operator()() { s = s * 1664525u + 1013904223u; return (float) ((double) s / 4294967296.0); }
};

// brilliant-cut gem lit by INTENSITY and spectrum. Port of gemI() in the design.
inline void drawGem (Graphics& g, float cx, float cy, float R, int seed, const float* spec, int nspec, float I)
{
    Lcg rnd ((uint32_t) seed), rk ((uint32_t) (seed * 31 + 5)), r2 ((uint32_t) (seed * 13 + 1));
    const int N = 16;
    const float rings[4] = { R, R * 0.80f, R * 0.58f, R * 0.36f };
    auto pt = [&] (int ri, int i, float off) { return P (cx, cy, rings[ri], (i + off) * 360.0f / N); };
    auto bright = [] (float ang) { return std::cos (rad (ang + 50.0f)) * 0.5f + 0.5f; };
    auto mod = [&] (int i) { return spec[(i * 3) % nspec]; };
    auto f = [&] (float base, int i)
    {
        const float v = base + (rnd() - 0.5f) * 38.0f + (mod (i) - 0.5f) * 60.0f;
        const float r = rk(); const float dark = 13.0f + r * 9.0f;
        const float lit = juce::jlimit (0.0f, 1.0f, (I - r * 0.7f) / 0.3f);
        return grey (dark + lit * (std::max (20.0f, v * (0.7f + 0.3f * I)) - dark));
    };
    const Colour sw = grey (48.0f - 38.0f * I);
    auto poly3 = [&] (juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, Colour fill)
    {
        Path p; p.addTriangle (a, b, c);
        g.setColour (fill); g.fillPath (p);
        g.setColour (sw); g.strokePath (p, juce::PathStrokeType (1.0f));
    };
    for (int i = 0; i < N; ++i)
    {
        const float a = i * 360.0f / N + 360.0f / N / 2.0f;
        poly3 (pt (0, i, 0), pt (0, i + 1, 0), pt (1, i, 0.5f), f (40 + bright (a) * 140, i));
        poly3 (pt (1, i, 0.5f), pt (1, i + 1, 0.5f), pt (0, i + 1, 0), f (70 + bright (a + 11) * 150, i + 3));
        poly3 (pt (1, i, 0.5f), pt (2, i + 1, 0), pt (2, i, 0), f (30 + bright (a - 8) * 170, i + 5));
        poly3 (pt (1, i, 0.5f), pt (1, i + 1, 0.5f), pt (2, i + 1, 0), f (55 + bright (a + 20) * 160, i + 9));
        poly3 (pt (2, i, 0), pt (2, i + 1, 0), pt (3, i, 0.5f), f (50 + bright (a + 5) * 180, i + 2));
    }
    Path tbl;
    for (int i = 0; i < N; ++i) { const auto q = pt (3, i, 0.5f); if (i == 0) tbl.startNewSubPath (q); else tbl.lineTo (q); }
    tbl.closeSubPath();
    g.setColour (grey (14.0f + 16.0f * I)); g.fillPath (tbl);
    g.setColour (grey (90.0f + 110.0f * I)); g.strokePath (tbl, juce::PathStrokeType (1.2f));
    {
        Path gl; gl.addTriangle (P (cx, cy, rings[3] * 0.7f, -60), P (cx, cy, rings[3] * 0.55f, 10), P (cx, cy, rings[3] * 0.7f, 60));
        g.setColour (juce::Colours::white.withAlpha (0.03f + 0.2f * I)); g.fillPath (gl);
    }
    const int nl = juce::roundToInt (I * I * 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f + 0.3f * I));
    for (int k = 0; k < nl; ++k)
    {
        const int i = (int) std::floor (r2() * N), j = (int) std::floor (r2() * N);
        g.drawLine (juce::Line<float> (pt (3, i, 0.5f), pt (1, j, 0.5f)), 0.8f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f * I * I), cx, cy, juce::Colours::white.withAlpha (0.0f), cx + R * 0.62f, cy, true));
    g.fillEllipse (cx - R * 0.62f, cy - R * 0.62f, R * 1.24f, R * 1.24f);
    g.setColour (grey (80.0f + 120.0f * I));
    g.drawEllipse (cx - R, cy - R, R * 2, R * 2, 1.0f + I);
}

// monochrome LookAndFeel (menus, text editor, resize corner)
class QuartzLookAndFeel : public juce::LookAndFeel_V4
{
public:
    QuartzLookAndFeel()
    {
        setDefaultSansSerifTypeface (Fonts::inter());
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0c0c0c));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xffcfcfcf));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffe8e8e8));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xff777777));
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff050505));
        setColour (juce::TextEditor::textColourId, juce::Colours::white);
        setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff8a8a8a));
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::white);
        setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff6a6a6a));
        setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff555555));
    }
    juce::Font getPopupMenuFont() override { return Fonts::ui (13.0f); }

    // "&Copy value": the letter after '&' is the keyboard shortcut and is drawn underlined.
    static juce::String stripMnemonic (const juce::String& t) { return t.replace ("&", "", false); }
    static int mnemonicIndex (const juce::String& t) { const int i = t.indexOfChar ('&'); return (i >= 0 && i + 1 < t.length()) ? i : -1; }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize (stripMnemonic (text), isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
    }
    void drawPopupMenuItem (Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                            bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColourToUse) override
    {
        const int mi = mnemonicIndex (text);
        if (mi < 0 || isSeparator)
        {
            juce::LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text, shortcutKeyText, icon, textColourToUse);
            return;
        }
        const auto plain = stripMnemonic (text);
        // draw everything except the label with the stock code (empty label), then the label and its underline here
        juce::LookAndFeel_V4::drawPopupMenuItem (g, area, false, isActive, isHighlighted, isTicked, hasSubMenu, juce::String(), shortcutKeyText, icon, textColourToUse);
        auto r = area.reduced (1);
        r.reduce (juce::jmin (5, area.getWidth() / 20), 0);
        auto font = getPopupMenuFont();
        const float maxFontHeight = (float) r.getHeight() / 1.3f;
        if (font.getHeight() > maxFontHeight) font.setHeight (maxFontHeight);
        r.removeFromLeft (juce::roundToInt (maxFontHeight));
        if (icon != nullptr) r.removeFromLeft (juce::roundToInt (maxFontHeight * 0.5f));
        const auto col = (isHighlighted && isActive) ? findColour (juce::PopupMenu::highlightedTextColourId)
                         : (textColourToUse == nullptr ? findColour (juce::PopupMenu::textColourId) : *textColourToUse).withMultipliedAlpha (isActive ? 1.0f : 0.5f);
        g.setColour (col); g.setFont (font);
        g.drawText (plain, r, juce::Justification::centredLeft, true);
        const float x0 = (float) r.getX() + font.getStringWidthFloat (plain.substring (0, mi));
        const float cw = font.getStringWidthFloat (plain.substring (mi, mi + 1));
        const float base = (float) r.getCentreY() - font.getHeight() * 0.5f + font.getAscent();
        g.fillRect (x0, base + 1.5f, cw, 1.0f);
    }
    void drawCornerResizer (Graphics& g, int w, int h, bool over, bool down) override
    {
        g.setColour (juce::Colour (down ? 0xffffffff : over ? 0xffcccccc : 0xff777777));
        for (int i = 1; i <= 3; ++i) g.drawLine ((float) w - 3.0f * (float) i * 2.2f, (float) h - 1.0f, (float) w - 1.0f, (float) h - 3.0f * (float) i * 2.2f, 1.0f);
    }
    void drawPopupMenuBackground (Graphics& g, int w, int h) override
    {
        g.fillAll (juce::Colour (0xff0c0c0c)); g.setColour (juce::Colour (0xff888888)); g.drawRect (0, 0, w, h, 1);
    }
};
} // namespace ui
