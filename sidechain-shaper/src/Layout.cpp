// Sidechain Shaper: control layout + the curve editor (draw the volume curve, see the playhead, the gain, the detector and the triggers).
#include "PluginEditor.h"

namespace scui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY SIDECHAIN SHAPER", "CURVE  /  TEMPO  /  SIDECHAIN",
        { 16, 56, 868, 228 },
        {
            { "TIMING",    nullptr, { 16, 292, 434, 118 },  { { 'c', "mode", "MODE" }, { 'c', "rate", "LENGTH" }, { 'k', "offset", "OFFSET" } }, 140 },
            { "SHAPE",     nullptr, { 458, 292, 426, 118 }, { { 'k', "depth", "DEPTH" }, { 'k', "smooth", "SMOOTH" }, { 'k', "stereo", "STEREO OFFSET" } }, 140 },
            { "SIDECHAIN", nullptr, { 16, 418, 592, 118 },  { { 'c', "source", "TRIGGER" }, { 'k', "threshold", "THRESHOLD" }, { 'k', "focus", "FOCUS" }, { 'k', "hold", "HOLD" } }, 146 },
            { "OUTPUT",    nullptr, { 616, 418, 268, 118 }, { { 'k', "mix", "MIX" }, { 'k', "gain", "OUTPUT" } }, 132 },
        },
        "depth"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cGridB (0xff2c2c2c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);
using sc::dsp::Curve;
using sc::dsp::Pt;

class CurveEditor final : public juce::Component, private juce::Timer
{
public:
    CurveEditor (SidechainShaperProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        pts = proc.curve(); shownText = proc.curveText();
        startTimerHz (30);
    }

    // ------------------------------------------------------------------------------------------------ drawing
    void paint (juce::Graphics& g) override
    {
        const auto P = proc.currentParams();
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (12, 10);
        auto meters = r.removeFromRight (120); r.removeFromRight (14);
        auto readout = r.removeFromTop (30); r.removeFromTop (6);

        const juce::StringArray rates = juce::StringArray::fromTokens (sc::dsp::rateNames(), "|", "");
        g.setFont (uiFont (24.0f, true)); g.setColour (cPri);
        g.drawText (rates[P.rate], readout.removeFromLeft (110), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f, true)); g.setColour (cDim);
        juce::String sub;
        sub << (P.mode == 1 ? "SIDECHAIN TRIGGER" : "TEMPO SYNC") << "   DEPTH " << juce::roundToInt (P.depth * 100) << "%   ";
        if (P.mode == 1) sub << (ext ? "SOURCE: SIDECHAIN INPUT" : (P.source == 0 ? "NO SIDECHAIN CONNECTED - USING THE INPUT" : "SOURCE: PLUG-IN INPUT"));
        else             sub << juce::String (bpm, 1) << " BPM" << (playing ? "   PLAYING" : "   STOPPED (FREE-RUNNING)");
        g.drawText (sub, readout, juce::Justification::centredLeft);

        // curve area
        g.setColour (cLine); g.drawRect (r);
        area = r.toFloat().reduced (8.0f, 8.0f);
        const auto c = area;
        const int gd = proc.grid();
        for (int i = 1; i < juce::jmax (1, gd); ++i)
        {
            const float x = c.getX() + c.getWidth() * (float) i / (float) gd;
            g.setColour ((i * 4) % gd == 0 ? cGridB : cGrid); g.drawVerticalLine ((int) x, c.getY(), c.getBottom());
        }
        for (float y : { 0.25f, 0.5f, 0.75f }) { g.setColour (cGrid); g.drawHorizontalLine ((int) yOf (y), c.getX(), c.getRight()); }
        g.setColour (cGridB); g.drawRect (c);
        g.setFont (uiFont (9.5f)); g.setColour (cDim.withAlpha (0.7f));
        g.drawText ("100%", (int) c.getX() + 3, (int) c.getY() + 1, 40, 11, juce::Justification::left);
        g.drawText ("0", (int) c.getX() + 3, (int) c.getBottom() - 12, 40, 11, juce::Justification::left);

        // the curve (filled), and the effective gain when Depth < 100%
        juce::Path line, fill;
        const int steps = juce::jmax (64, (int) c.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x01 = (float) i / (float) steps, v = sc::dsp::evalCurve (pts, x01);
            const juce::Point<float> q (xOf (x01), yOf (v));
            if (i == 0) { line.startNewSubPath (q); fill.startNewSubPath (c.getX(), c.getBottom()); }
            else line.lineTo (q);
            fill.lineTo (q);
        }
        fill.lineTo (c.getRight(), c.getBottom()); fill.closeSubPath();
        g.setColour (cPri.withAlpha (0.07f)); g.fillPath (fill);
        if (P.depth < 0.995f)
        {
            juce::Path eff;
            for (int i = 0; i <= steps; ++i)
            {
                const float x01 = (float) i / (float) steps, v = 1.0f - P.depth * (1.0f - sc::dsp::evalCurve (pts, x01));
                if (i == 0) eff.startNewSubPath (xOf (x01), yOf (v)); else eff.lineTo (xOf (x01), yOf (v));
            }
            g.setColour (cDim.withAlpha (0.6f));
            juce::Path dashed; const float dl[] = { 4.0f, 4.0f }; juce::PathStrokeType (1.0f).createDashedStroke (dashed, eff, dl, 2); g.fillPath (dashed);
        }
        g.setColour (cPri); g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // bend handles (segment middles) and points
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            if (! handleVisible (i)) continue;
            const auto h = handlePos (i);
            juce::Path d; d.addRectangle (-3.5f, -3.5f, 7.0f, 7.0f); d.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (h));
            g.setColour ((int) i == hoverHandle || (dragKind == 2 && (int) i == dragIndex) ? cPri : cDim.withAlpha (0.8f));
            if (std::fabs (pts[i].t) > 0.01f) g.fillPath (d); else g.strokePath (d, juce::PathStrokeType (1.2f));
        }
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const auto q = ptPos (i);
            const bool hot = (int) i == hoverPoint || (dragKind == 1 && (int) i == dragIndex);
            const float rad = hot ? 6.0f : 4.5f;
            g.setColour (cBg); g.fillEllipse (q.x - rad - 1, q.y - rad - 1, 2 * rad + 2, 2 * rad + 2);
            g.setColour (cPri); g.fillEllipse (q.x - rad, q.y - rad, 2 * rad, 2 * rad);
        }

        // playheads
        const float phL = demo ? 0.34f : proc.dspEngine().phaseL.load (std::memory_order_relaxed);
        const float phR = demo ? 0.34f : proc.dspEngine().phaseR.load (std::memory_order_relaxed);
        if (P.stereo > 0.001f)
        {
            const float x = xOf (phR); g.setColour (cSec.withAlpha (0.45f));
            for (float y = c.getY(); y < c.getBottom(); y += 6.0f) g.drawVerticalLine ((int) x, y, juce::jmin (c.getBottom(), y + 3.0f));
        }
        {
            const float x = xOf (phL); g.setColour (cPri.withAlpha (0.55f)); g.drawVerticalLine ((int) x, c.getY(), c.getBottom());
            const float y = yOf (sc::dsp::evalCurve (pts, phL));
            g.setColour (cPri.withAlpha (0.18f)); g.fillEllipse (x - 9, y - 9, 18, 18);
            g.setColour (cPri); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
        }
        if (flash > 0.01f) { g.setColour (cPri.withAlpha (flash * 0.8f)); g.drawRect (area.expanded (7.0f), 2.0f); }

        g.setColour (cDim); g.setFont (uiFont (9.5f, true));
        g.drawText ("CLICK: ADD   DRAG: MOVE   DIAMOND / ALT-DRAG: BEND   DOUBLE-CLICK: DELETE   SHIFT: NO SNAP   RIGHT-CLICK: SHAPES",
                    r.reduced (10, 3), juce::Justification::bottomRight);

        // meters: IN, GAIN (what the curve does right now), DETECTOR with the threshold, OUT
        auto meter = [&] (juce::Rectangle<int> m, float lvl01, const char* name, float mark01 = -1.0f)
        {
            g.setColour (cDim); g.setFont (uiFont (9.5f, true));
            g.drawText (name, m.removeFromBottom (14), juce::Justification::centred);
            g.setColour (cGrid); g.fillRect (m);
            const float hh = juce::jlimit (0.0f, 1.0f, lvl01) * (float) m.getHeight();
            g.setColour (cSec.withAlpha (0.85f)); g.fillRect ((float) m.getX(), (float) m.getBottom() - hh, (float) m.getWidth(), hh);
            g.setColour (cPri); g.fillRect ((float) m.getX(), (float) m.getBottom() - hh, (float) m.getWidth(), juce::jmin (2.0f, hh));
            if (mark01 >= 0.0f) { g.setColour (cPri); g.fillRect ((float) m.getX() - 2.0f, (float) m.getBottom() - mark01 * (float) m.getHeight() - 1.0f, (float) m.getWidth() + 4.0f, 2.0f); }
        };
        auto db01 = [] (float lin) { return (juce::Decibels::gainToDecibels (lin, -60.0f) + 60.0f) / 60.0f; };
        const int mw = 24, gap = (meters.getWidth() - 4 * mw) / 3;
        auto m1 = meters.removeFromLeft (mw); meters.removeFromLeft (gap);
        auto m2 = meters.removeFromLeft (mw); meters.removeFromLeft (gap);
        auto m3 = meters.removeFromLeft (mw); meters.removeFromLeft (gap);
        meter (m1, db01 (inL), "IN");
        meter (m2, gainShown, "GAIN");
        meter (m3, db01 (detL), "DET", db01 (juce::Decibels::decibelsToGain (P.thrDb)));
        meter (meters.removeFromLeft (mw), db01 (outL), "OUT");
    }

    // ------------------------------------------------------------------------------------------------ editing (also driven by the self test)
    void beginEdit() { before = proc.curveText(); editing = true; }
    void endEdit() { editing = false; proc.commitCurve (before, pts); shownText = proc.curveText(); dragKind = 0; dragIndex = -1; repaint(); }
    int addPoint (float x01, float y01)
    {
        Pt p { juce::jlimit (0.001f, 0.999f, x01), juce::jlimit (0.0f, 1.0f, y01), 0.0f };
        size_t at = 1; while (at < pts.size() && pts[at].x <= p.x) ++at;
        if ((int) pts.size() >= sc::dsp::kMaxPts) return -1;
        pts.insert (pts.begin() + (long) at, p); proc.previewCurve (pts); return (int) at;
    }
    void movePoint (int i, float x01, float y01)
    {
        if (i < 0 || i >= (int) pts.size()) return;
        auto& p = pts[(size_t) i];
        if (i == 0) p.x = 0.0f; else if (i == (int) pts.size() - 1) p.x = 1.0f;
        else p.x = juce::jlimit (pts[(size_t) i - 1].x, pts[(size_t) i + 1].x, x01);
        p.y = juce::jlimit (0.0f, 1.0f, y01);
        proc.previewCurve (pts);
    }
    void setBend (int seg, float t) { if (seg >= 0 && seg + 1 < (int) pts.size()) { pts[(size_t) seg].t = juce::jlimit (-1.0f, 1.0f, t); proc.previewCurve (pts); } }
    bool deletePoint (int i)
    {
        if (i <= 0 || i >= (int) pts.size() - 1) return false;
        pts.erase (pts.begin() + i); return true;
    }
    void applyCurve (const Curve& c, const juce::String& what) { pts = sc::dsp::sanitize (c); proc.setCurve (pts, true, what); shownText = proc.curveText(); repaint(); }
    const Curve& points() const { return pts; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int hp = pointAt (e.position), hh = hp < 0 ? handleAt (e.position) : -1;
        if (hp != hoverPoint || hh != hoverHandle) { hoverPoint = hp; hoverHandle = hh; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverPoint = hoverHandle = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu (panel.getLocalPoint (nullptr, e.getScreenPosition())); return; }
        if (! area.expanded (8.0f).contains (e.position)) return;
        const int p = pointAt (e.position), h = p < 0 ? handleAt (e.position) : -1;
        beginEdit();
        if ((e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()) && p < 0) { dragKind = 2; dragIndex = segmentAt (x01Of (e.position.x)); }
        else if (p >= 0) { dragKind = 1; dragIndex = p; }
        else if (h >= 0) { dragKind = 2; dragIndex = h; }
        else if (e.getNumberOfClicks() < 2) { dragKind = 1; dragIndex = addPoint (snapX (x01Of (e.position.x), e.mods.isShiftDown()), snapY (y01Of (e.position.y))); }
        dragStartY = e.position.y; dragStartT = dragKind == 2 && dragIndex >= 0 ? pts[(size_t) dragIndex].t : 0.0f;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! editing || dragIndex < 0) return;
        if (dragKind == 1) movePoint (dragIndex, snapX (x01Of (e.position.x), e.mods.isShiftDown()), snapY (y01Of (e.position.y)));
        else if (dragKind == 2 && dragIndex + 1 < (int) pts.size())
        {
            const float up = (dragStartY - e.position.y) / 110.0f;
            const bool rising = pts[(size_t) dragIndex + 1].y >= pts[(size_t) dragIndex].y;
            setBend (dragIndex, dragStartT + (rising ? -up : up));
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override { if (editing && ! e.mods.isPopupMenu()) endEdit(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const int p = pointAt (e.position);
        if (p > 0 && p < (int) pts.size() - 1) { auto c = pts; c.erase (c.begin() + p); applyCurve (c, "Delete point"); return; }
        const int h = handleAt (e.position);
        if (h >= 0) { auto c = pts; c[(size_t) h].t = 0.0f; applyCurve (c, "Straighten segment"); }
    }

    void showMenu (juce::Point<int> where)
    {
        std::vector<PopupOverlay::Item> v;
        static const char* const labels[] = { "&Pump", "&Hard Pump", "&Soft Duck", "Shor&t Duck", "&Long Release", "&Gate Half", "&Chop 8", "Tr&ance Gate", "St&utter",
                                              "&Ramp Up", "Ramp &Down", "Tr&iangle", "Si&ne", "S&well", "&Flat" };
        const auto& sh = sc::dsp::shapes();
        for (size_t i = 0; i < sh.size() && i < std::size (labels); ++i)
            v.push_back ({ labels[i], [this, i] { applyCurve (sc::dsp::shapes()[i].pts, juce::String ("Curve: ") + sc::dsp::shapes()[i].name); } });
        v.push_back ({ "Re&verse", [this] { Curve c; for (auto it = pts.rbegin(); it != pts.rend(); ++it) c.push_back ({ 1.0f - it->x, it->y, 0 });
                                              for (size_t i = 0; i + 1 < c.size(); ++i) c[i].t = -pts[pts.size() - 2 - i].t;
                                              applyCurve (c, "Reverse curve"); }, true, false, true });
        v.push_back ({ "&Mirror Up/Down", [this] { auto c = pts; for (auto& p : c) { p.y = 1.0f - p.y; p.t = -p.t; } applyCurve (c, "Mirror curve"); } });
        v.push_back ({ "Straighten &Bends", [this] { auto c = pts; for (auto& p : c) p.t = 0; applyCurve (c, "Straighten curve"); } });
        v.push_back ({ "Cop&y Curve", [this] { juce::SystemClipboard::copyTextToClipboard ("CMPLXTY-CURVE:" + proc.curveText()); } });
        const auto clip = juce::SystemClipboard::getTextFromClipboard();
        v.push_back ({ "Paste Curv&e", [this, clip] { applyCurve (sc::dsp::parseCurve (clip.fromFirstOccurrenceOf ("CMPLXTY-CURVE:", false, false).toStdString()), "Paste curve"); },
                       clip.startsWith ("CMPLXTY-CURVE:") });
        const int gd = proc.grid();
        v.push_back ({ "Grid &Off", [this] { proc.setGrid (0); repaint(); }, true, gd == 0, true });
        v.push_back ({ "Grid &4",   [this] { proc.setGrid (4); repaint(); }, true, gd == 4 });
        v.push_back ({ "Grid &8",   [this] { proc.setGrid (8); repaint(); }, true, gd == 8 });
        v.push_back ({ "Grid 1&6",  [this] { proc.setGrid (16); repaint(); }, true, gd == 16 });
        v.push_back ({ "Grid 3&2",  [this] { proc.setGrid (32); repaint(); }, true, gd == 32 });
        panel.popupOverlay().show (where, std::move (v));
    }

    void demoFill() { demo = true; inL = 0.4f; outL = 0.3f; detL = 0.5f; gainShown = 0.55f; ext = true; bpm = 124.0f; playing = true; repaint(); }

private:
    void timerCallback() override
    {
        if (! editing && proc.curveText() != shownText) { pts = proc.curve(); shownText = proc.curveText(); }   // undo / redo / preset load
        if (! demo)
        {
            const auto& e = proc.dspEngine();
            inL = std::max (proc.inLevel(), inL * 0.85f); outL = std::max (proc.outLevel(), outL * 0.85f);
            detL = std::max (e.detLevel.load (std::memory_order_relaxed), detL * 0.85f);
            gainShown = e.gainL.load (std::memory_order_relaxed);
            ext = e.usingExternal.load (std::memory_order_relaxed) != 0;
            bpm = proc.hostBpm.load(); playing = proc.hostPlaying.load();
            const int t = e.triggers.load (std::memory_order_relaxed);
            if (t != lastTrig) { flash = 1.0f; lastTrig = t; } else flash *= 0.8f;
        }
        repaint();
    }
    float xOf (float x01) const { return area.getX() + x01 * area.getWidth(); }
    float yOf (float y01) const { return area.getBottom() - y01 * area.getHeight(); }
    float x01Of (float x) const { return juce::jlimit (0.0f, 1.0f, (x - area.getX()) / juce::jmax (1.0f, area.getWidth())); }
    float y01Of (float y) const { return juce::jlimit (0.0f, 1.0f, (area.getBottom() - y) / juce::jmax (1.0f, area.getHeight())); }
    float snapX (float x, bool noSnap) const { const int gd = proc.grid(); return (noSnap || gd <= 0) ? x : std::round (x * (float) gd) / (float) gd; }
    static float snapY (float y) { return y < 0.025f ? 0.0f : (y > 0.975f ? 1.0f : y); }
    juce::Point<float> ptPos (size_t i) const { return { xOf (pts[i].x), yOf (pts[i].y) }; }
    bool handleVisible (size_t i) const { return i + 1 < pts.size() && (pts[i + 1].x - pts[i].x) * area.getWidth() > 18.0f && std::fabs (pts[i + 1].y - pts[i].y) > 0.02f; }
    juce::Point<float> handlePos (size_t i) const
    {
        const float xm = 0.5f * (pts[i].x + pts[i + 1].x);
        return { xOf (xm), yOf (sc::dsp::evalCurve (pts, xm)) };
    }
    int pointAt (juce::Point<float> m) const
    {
        int best = -1; float bd = 9.0f * 9.0f;
        for (size_t i = 0; i < pts.size(); ++i) { const float d = ptPos (i).getDistanceSquaredFrom (m); if (d <= bd) { bd = d; best = (int) i; } }
        return best;
    }
    int handleAt (juce::Point<float> m) const
    {
        for (size_t i = 0; i + 1 < pts.size(); ++i) if (handleVisible (i) && handlePos (i).getDistanceSquaredFrom (m) <= 7.0f * 7.0f) return (int) i;
        return -1;
    }
    int segmentAt (float x01) const { for (size_t i = 0; i + 1 < pts.size(); ++i) if (x01 <= pts[i + 1].x) return (int) i; return (int) pts.size() - 2; }

    SidechainShaperProcessor& proc;
    Panel& panel;
    Curve pts;
    juce::String shownText, before;
    juce::Rectangle<float> area { 20, 50, 700, 150 };
    bool editing = false, demo = false, ext = false, playing = false;
    int dragKind = 0, dragIndex = -1, hoverPoint = -1, hoverHandle = -1, lastTrig = 0;
    float dragStartY = 0, dragStartT = 0, inL = 0, outL = 0, detL = 0, gainShown = 1, flash = 0, bpm = 120;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (SidechainShaperProcessor& p, Panel& pn) { return std::make_unique<CurveEditor> (p, pn); }
void demoCurveMenu (juce::Component* c) { if (auto* d = dynamic_cast<CurveEditor*> (c)) d->showMenu (d->getBoundsInParent().getPosition() + juce::Point<int> (420, 70)); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<CurveEditor*> (c)) d->demoFill(); }

// Test hook: edits the curve through the same functions the mouse uses, and checks undo / redo, the menu and the focus rules.
juce::String auditCurve (juce::Component* c, SidechainShaperProcessor& proc)
{
    auto* d = dynamic_cast<CurveEditor*> (c);
    if (d == nullptr) return "CURVE AUDIT FAILED: no curve editor\n";
    juce::String out; int bad = 0;
    auto check = [&] (bool ok, const juce::String& what) { out << (ok ? "  ok   " : "  FAIL ") << what << "\n"; bad += ok ? 0 : 1; };
    check (! d->getWantsKeyboardFocus(), "curve editor never takes keyboard focus");
    d->applyCurve (sc::dsp::shapeByName ("Pump"), "Curve: Pump");
    proc.undoManager.clearUndoHistory();
    const auto start = proc.curveText(); const size_t n0 = d->points().size();
    d->beginEdit(); const int i = d->addPoint (0.3f, 0.2f); d->movePoint (i, 0.35f, 0.25f); d->endEdit();
    check (d->points().size() == n0 + 1 && proc.curve().size() == n0 + 1, "click adds a point, drag moves it, the processor gets it");
    check (std::fabs (sc::dsp::evalCurve (proc.curve(), 0.35f) - 0.25f) < 0.01f, "moved point is where it was dropped");
    check (proc.undoManager.canUndo(), "the edit is one undo step");
    proc.undoManager.undo();
    check (proc.curveText() == start, "undo restores the curve");
    proc.undoManager.redo();
    check (proc.curve().size() == n0 + 1, "redo brings the point back");
    d->beginEdit(); d->setBend (0, 0.8f); d->endEdit();
    check (std::fabs (proc.curve().front().t - 0.8f) < 0.01f, "bend handle sets a segment's bend");
    d->beginEdit(); d->movePoint (0, 0.5f, 0.6f); d->endEdit();
    check (proc.curve().front().x == 0.0f && std::fabs (proc.curve().front().y - 0.6f) < 0.01f, "end points move only up / down");
    auto cur = d->points(); cur.erase (cur.begin() + 1); d->applyCurve (cur, "Delete point");
    check (proc.curve().size() == n0, "a point can be deleted");
    d->showMenu ({ 100, 100 });
    // (the menu belongs to the panel; it is checked by the caller: itemTexts + activate)
    check (true, "right-click menu opened");
    return out + (bad == 0 ? "CURVE AUDIT OK\n" : "CURVE AUDIT FAILED\n");
}
} // namespace scui
