// Silt: control layout (five pages) + the main display (sample overview with the grain cloud, slice markers, layer B strip, mod chips, keys)
// and the custom page components (mod matrix, step grid, FX rack).
#include "PluginEditor.h"
#include "SiltParams.h"

namespace sltui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY SILT", "SMART GRANULAR SYNTH",
        { 16, 56, 1148, 300 },
        {
            // page 0: GRAIN
            { "GRAIN", nullptr, { 16, 398, 610, 160 }, { { 'k', "pos", "POSITION" }, { 'k', "scan", "SCAN" }, { 'k', "density", "DENSITY" }, { 'k', "size", "SIZE" },
                                                       { 'k', "sizeRand", "SIZE RND" }, { 'k', "spray", "SPRAY" } }, 132, 0 },
            { "PITCH", nullptr, { 634, 398, 530, 160 }, { { 'k', "pitch", "PITCH" }, { 'k', "fine", "FINE" }, { 'k', "pjit", "PITCH JITTER" }, { 'k', "glide", "GLIDE" } }, 132, 0 },
            { "CHARACTER", nullptr, { 16, 564, 560, 160 }, { { 'k', "spread", "SPREAD" }, { 'k', "shape", "SHAPE" }, { 'k', "reverse", "REVERSE" }, { 'k', "magnet", "MAGNET" },
                                                           { 'k', "gcomp", "LEVEL COMP" } }, 132, 0 },
            { "AMP", nullptr, { 584, 564, 580, 160 }, { { 'k', "aA", "ATTACK" }, { 'k', "aD", "DECAY" }, { 'k', "aS", "SUSTAIN" }, { 'k', "aR", "RELEASE" },
                                                      { 'k', "velSens", "VELOCITY" }, { 'k', "level", "LEVEL" } }, 132, 0 },
            // page 1: TUNE + B
            { "TUNE", nullptr, { 16, 398, 560, 160 }, { { 'k', "cohere", "COHERE" }, { 'k', "root", "ROOT KEY" }, { 'c', "tune", "TUNE" }, { 'c', "scale", "SCALE LOCK" },
                                                      { 'c', "key", "SCALE KEY" } }, 160, 1 },
            { "VOICES", nullptr, { 584, 398, 580, 160 }, { { 'k', "voices", "VOICES" }, { 'k', "bend", "BEND RANGE" }, { 't', "mpe", "MPE" } }, 120, 1 },
            { "LAYER B", "bOn", { 16, 564, 560, 160 }, { { 'k', "morph", "MORPH" }, { 'k', "bOffset", "B OFFSET" }, { 'k', "bPitch", "B PITCH" }, { 'c', "morphMode", "MORPH MODE" },
                                                       { 't', "keyMatch", "KEY MATCH" } }, 160, 1 },
            { "GRAIN FILTER", "fOn", { 584, 564, 580, 160 }, { { 'k', "cutoff", "CUTOFF" }, { 'k', "res", "RESONANCE" }, { 'k', "fRand", "CUTOFF RND" }, { 'k', "fKey", "KEY TRACK" },
                                                             { 'c', "fType", "TYPE" } }, 140, 1 },
            // page 2: MOD
            { "LFO 1", nullptr, { 16, 398, 280, 160 }, { { 'k', "l1Rate", "RATE" }, { 'c', "l1Sync", "SYNC" }, { 'c', "l1Shape", "SHAPE" } }, 140, 2 },
            { "LFO 2", nullptr, { 304, 398, 280, 160 }, { { 'k', "l2Rate", "RATE" }, { 'c', "l2Sync", "SYNC" }, { 'c', "l2Shape", "SHAPE" } }, 140, 2 },
            { "MOD ENVELOPE", nullptr, { 592, 398, 572, 160 }, { { 'k', "e2A", "ATTACK" }, { 'k', "e2D", "DECAY" }, { 'k', "e2S", "SUSTAIN" }, { 'k', "e2R", "RELEASE" } }, 132, 2 },
            { "MACROS", nullptr, { 16, 564, 380, 160 }, { { 'k', "m1", "MACRO 1" }, { 'k', "m2", "MACRO 2" }, { 'k', "m3", "MACRO 3" }, { 'k', "m4", "MACRO 4" } }, 132, 2 },
            // page 3: SLICE + SEQ
            { "SLICES", nullptr, { 16, 398, 560, 160 }, { { 'k', "slNote", "FIRST KEY" }, { 'k', "sens", "TRANSIENT SENS" }, { 'c', "mode", "MODE" }, { 'c', "slPlay", "PLAY" },
                                                        { 'c', "slSrc", "SLICES" } }, 160, 3 },
            { "SEQUENCER", "seq", { 584, 398, 580, 160 }, { { 'k', "seqLen", "LENGTH" }, { 'k', "seqSwing", "SWING" }, { 'k', "seqGate", "GATE" }, { 'c', "seqRate", "RATE" },
                                                          { 'c', "seqTrig", "RUNS WHILE" } }, 150, 3 },
            // page 4: FX
            { "BEAT REPEAT", "rpOn", { 16, 444, 560, 136 }, { { 'k', "rpChance", "CHANCE" }, { 'k', "rpMix", "MIX" }, { 'c', "rpLen", "LENGTH" } }, 150, 4 },
            { "COMPRESSOR", "cpOn", { 584, 444, 580, 136 }, { { 'k', "cpThr", "THRESHOLD" }, { 'k', "cpRatio", "RATIO" }, { 'k', "cpGain", "MAKEUP" } }, 132, 4 },
            { "DELAY", "dlOn", { 16, 588, 560, 136 }, { { 'k', "dlFb", "FEEDBACK" }, { 'k', "dlMix", "MIX" }, { 'c', "dlTime", "TIME" } }, 150, 4 },
            { "REVERB", "rvOn", { 584, 588, 580, 136 }, { { 'k', "rvSize", "SIZE" }, { 'k', "rvDark", "DARK" }, { 'k', "rvMix", "MIX" } }, 132, 4 },
        },
        "density",
        { "GRAIN", "TUNE + LAYER B", "MOD", "SLICE + SEQ", "FX" },
        { 16, 364, 1148, 26 },
        { { 1, 2, { 404, 564, 760, 160 } }, { 2, 3, { 16, 564, 1148, 160 } }, { 3, 4, { 16, 398, 1148, 40 } } }
    };
    return s;
}

juce::String modSourceName (int src)
{
    static const juce::StringArray n = juce::StringArray::fromTokens (slt::modSrcChoices(), "|", "");
    return src > 0 && src < n.size() ? n[src] : juce::String ("-");
}

namespace
{
const juce::Colour cBg (0xff101010), cLane (0xff161616), cLine (0xff2d2d2d), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);

void noFocus (juce::Component& c) { c.setWantsKeyboardFocus (false); c.setMouseClickGrabsKeyboardFocus (false); }
float raw (SiltProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
void setParam (SiltProcessor& p, const juce::String& id, float realValue)
{
    if (auto* prm = p.apvts.getParameter (id))
    { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (realValue)); prm->endChangeGesture(); }
}
juce::String noteName (int n) { static const char* k[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String (k[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2); }

// draggable modulation source chip
struct ModChip final : juce::Component
{
    ModChip (Panel& p, int s) : panel (p), src (s) { noFocus (*this); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (dragging ? cPri.withAlpha (0.25f) : cLane); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (cPri.withAlpha (live ? 0.85f : 0.35f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (live ? cPri : cSec); g.setFont (uiFont (9.5f, true));
        g.drawText (modSourceName (src).toUpperCase(), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override { dragging = true; panel.modDragMove (src, e.getScreenPosition()); repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging) { panel.setNote ("Drag " + modSourceName (src) + " onto a knob to modulate it"); return; }
        dragging = false; panel.modDragEnd (src, e.getScreenPosition()); repaint();
    }
    Panel& panel; int src; bool dragging = false, live = false;
};

class SiltDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    enum Target { kNone = -1, kA = 0, kB = 1, kAnywhere = 20 };

    SiltDisplay (SiltProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        for (auto* b : { &loadA, &builtA, &clearA, &rootBtn, &loadB, &builtB, &clearB }) { addAndMakeVisible (*b); noFocus (*b); }
        loadA.onClick = [this] { choose (kA); };
        loadB.onClick = [this] { choose (kB); };
        builtA.onClick = [this] { builtInMenu (kA, builtA); };
        builtB.onClick = [this] { builtInMenu (kB, builtB); };
        clearA.onClick = [this] { proc.clearLayer (0); };
        clearB.onClick = [this] { proc.clearLayer (1); };
        rootBtn.onClick = [this] { proc.useDetectedRoot(); panel.setNote ("Tune set to Manual at the detected root: " + proc.rootDescription()); };
        addAndMakeVisible (keep); keep.setLookAndFeel (&panel.look);
        keepAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "keepSmp", keep);
        panel.registerParam (keep, "keepSmp");
        for (int s = 1; s <= 13; ++s) { auto& c = *chips.emplace_back (std::make_unique<ModChip> (panel, s)); addAndMakeVisible (c); }
        keys = std::make_unique<juce::MidiKeyboardComponent> (proc.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard);
        keys->setAvailableRange (24, 108); keys->setLowestVisibleKey (36); keys->setKeyWidth (14.0f);
        keys->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd8d8d8));
        keys->setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff141414));
        keys->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff404040));
        keys->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour (0xff707070));
        keys->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour (0x40ffffff));
        keys->setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keys->setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour (0xff202020));
        keys->setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, cPri);
        keys->setOctaveForMiddleC (3);    // same naming as the rest of Silt: MIDI 60 = C3
        addAndMakeVisible (*keys); noFocus (*keys);
        keys->setScrollButtonsVisible (true);
        for (auto* c : keys->getChildren()) noFocus (*c);
        startTimerHz (30);
    }
    ~SiltDisplay() override { keep.setLookAndFeel (nullptr); }

    void resized() override
    {
        auto top = getLocalBounds().reduced (10, 0).withTrimmedTop (6).removeFromTop (22);
        loadA.setBounds (top.removeFromLeft (64)); top.removeFromLeft (4);
        builtA.setBounds (top.removeFromLeft (86)); top.removeFromLeft (4);
        clearA.setBounds (top.removeFromLeft (54)); top.removeFromLeft (8);
        nameA = top.removeFromLeft (300);
        rootBtn.setBounds (top.removeFromLeft (128)); top.removeFromLeft (8);
        keep.setBounds (top.removeFromRight (42)); keepLabel = top.removeFromRight (108);
        rootText = top;
        waveA = { 10, 32, getWidth() - 20, 164 };
        auto bRow = juce::Rectangle<int> (10, 200, getWidth() - 20, 38);
        auto bBtns = bRow.removeFromLeft (64 + 4 + 86 + 4 + 54);
        loadB.setBounds (bBtns.removeFromLeft (64).withSizeKeepingCentre (64, 22)); bBtns.removeFromLeft (4);
        builtB.setBounds (bBtns.removeFromLeft (86).withSizeKeepingCentre (86, 22)); bBtns.removeFromLeft (4);
        clearB.setBounds (bBtns.removeFromLeft (54).withSizeKeepingCentre (54, 22));
        bRow.removeFromLeft (8); waveB = bRow;
        auto chipRow = juce::Rectangle<int> (10, 242, getWidth() - 20, 20);
        chipLabel = chipRow.removeFromLeft (112);
        for (auto& c : chips) { c->setBounds (chipRow.removeFromLeft (58)); chipRow.removeFromLeft (4); }
        status = chipRow;
        keys->setBounds (10, 266, getWidth() - 20, 30);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (hover != kNone ? 0.8f : 0.3f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, hover != kNone ? 2.0f : 1.0f);
        g.setFont (uiFont (12.0f, true)); g.setColour (cPri);
        g.drawText ("A  " + proc.sampleName (0).toUpperCase(), nameA, juce::Justification::centredLeft, true);
        g.setFont (uiFont (11.0f)); g.setColour (cSec);
        g.drawText ("ROOT " + proc.rootDescription(), rootText, juce::Justification::centredLeft, true);
        g.setColour (cDim); g.drawText ("KEEP MY SAMPLE", keepLabel, juce::Justification::centredRight);
        g.drawText ("MOD  drag to a knob:", chipLabel, juce::Justification::centredLeft);
        const auto& e = proc.engine();
        g.setColour (cDim);
        int nv = 0; for (auto& vp : e.voicePos) nv += vp.load (std::memory_order_relaxed) >= 0.0f ? 1 : 0;
        g.drawText (juce::String (nv) + (nv == 1 ? " voice" : " voices") + (proc.busy() ? "   ANALYSING..." : ""), status, juce::Justification::centredRight);

        paintA (g);
        paintB (g);
        if (note.isNotEmpty())   // bottom right, on its own backdrop, so it never sits on the slice key labels
        {
            g.setFont (uiFont (11.0f));
            const int tw = juce::jmin (waveA.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), note) + 12);
            const auto r = juce::Rectangle<int> (waveA.getRight() - 6 - tw, waveA.getBottom() - 19, tw, 15);
            g.setColour (juce::Colours::black.withAlpha (0.75f)); g.fillRect (r);
            g.setColour (cSec); g.drawText (note, r.reduced (6, 0), juce::Justification::centredRight, true);
        }
    }

    // ---- file drops: over the B strip -> layer B, anywhere else -> A (two files: A then B)
    bool isInterestedInFileDrag (const juce::StringArray& f) override
    {
        for (auto& s : f) if (juce::File (s).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { hover = targetAt ({ x, y }); repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { const int t = targetAt ({ x, y }); if (t != hover) { hover = t; repaint(); } }
    void fileDragExit (const juce::StringArray&) override { hover = kNone; repaint(); }
    void filesDropped (const juce::StringArray& f, int x, int y) override { hover = kNone; dropOn (targetAt ({ x, y }), f); hostkeys::releaseLater (*this); }

    void dropOn (int target, const juce::StringArray& files)
    {
        juce::StringArray ok;
        for (auto& s : files) if (juce::File (s).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) ok.add (s);
        if (ok.isEmpty()) { note = "Not an audio file (wav / aif / flac / ogg / mp3)"; repaint(); return; }
        juce::String err;
        if (target == kB) err = proc.loadFile (1, juce::File (ok[0]));
        else
        {
            err = proc.loadFile (0, juce::File (ok[0]));
            if (err.isEmpty() && target == kAnywhere && ok.size() > 1) err = proc.loadFile (1, juce::File (ok[1]));
        }
        note = err;
        repaint();
    }
    void demoHover (int t) { hover = t; repaint(); }

    // ---- mouse on the sample view
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMarker = -1; settingPos = settingOff = false;
        if (waveB.contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu()) { builtInMenu (kB, builtB); return; }
            settingOff = true; if (auto* p = proc.apvts.getParameter ("bOffset")) p->beginChangeGesture(); mouseDrag (e); return;
        }
        if (! waveA.contains (e.getPosition())) return;
        if (! proc.hasSample (0) && proc.builtInOf (0) < 0) { if (! e.mods.isPopupMenu()) choose (kA); return; }
        const bool sliceView = sliceMode();
        const int nearMark = sliceView ? markerNear (e.x) : -1;
        if (e.mods.isPopupMenu())
        {
            std::vector<PopupOverlay::Item> v;
            if (nearMark > 0) v.push_back ({ "&Delete this slice", [this, nearMark] { auto m = currentMarks(); m.erase (m.begin() + nearMark); proc.setManualSlices (m); } });
            if (sliceView) v.push_back ({ "&Back to transient slices", [this] { setParam (proc, "slSrc", (float) slt::kSlTransients); }, true, false, nearMark > 0 });
            v.push_back ({ "&Load sample...", [this] { choose (kA); }, true, false, ! v.empty() });
            for (int k = 0; k < slt::kNumBuiltIn; ++k) v.push_back ({ juce::String ("Built-in: ") + slt::builtInName (k), [this, k] { proc.loadBuiltIn (0, k); }, true, proc.builtInOf (0) == k, k == 0 });
            panel.popupOverlay().show (panel.getLocalPoint (nullptr, e.getScreenPosition()), std::move (v));
            return;
        }
        if (nearMark > 0) { dragMarker = nearMark; marks = currentMarks(); return; }
        settingPos = true; if (auto* p = proc.apvts.getParameter ("pos")) p->beginChangeGesture(); mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (settingOff) { if (auto* p = proc.apvts.getParameter ("bOffset")) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (e.x - waveB.getX()) / (float) waveB.getWidth())); return; }
        if (settingPos) { if (auto* p = proc.apvts.getParameter ("pos")) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (e.x - waveA.getX()) / (float) waveA.getWidth())); return; }
        if (dragMarker > 0 && dragMarker < (int) marks.size())
        {
            const int len = srcLen(); if (len <= 0) return;
            const int lo = marks[(size_t) dragMarker - 1] + 64, hi = dragMarker + 1 < (int) marks.size() ? marks[(size_t) dragMarker + 1] - 64 : len - 64;
            marks[(size_t) dragMarker] = juce::jlimit (lo, juce::jmax (lo, hi), xToSample (e.x));
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (settingPos) if (auto* p = proc.apvts.getParameter ("pos")) p->endChangeGesture();
        if (settingOff) if (auto* p = proc.apvts.getParameter ("bOffset")) p->endChangeGesture();
        if (dragMarker > 0) proc.setManualSlices (marks);
        settingPos = settingOff = false; dragMarker = -1;
        hostkeys::release (*this);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! waveA.contains (e.getPosition()) || ! sliceMode() || srcLen() <= 0) return;
        auto m = currentMarks(); m.push_back (xToSample (e.x));
        proc.setManualSlices (m);
        note = "Slice added (slices are now Manual)"; repaint();
    }

private:
    bool sliceMode() const { return juce::roundToInt (proc.apvts.getRawParameterValue ("mode")->load()) == slt::modeSlice; }
    int srcLen() const { auto s = proc.source (0); return s && s->audio ? s->audio->len() : 0; }
    int xToSample (int x) const { return juce::roundToInt (juce::jlimit (0.0, 1.0, (double) (x - waveA.getX()) / waveA.getWidth()) * srcLen()); }
    float sampleToX (int s) const { const int n = srcLen(); return (float) waveA.getX() + (n > 0 ? (float) s / (float) n : 0.0f) * (float) waveA.getWidth(); }
    std::vector<int> currentMarks() const { auto v = proc.slices(); if (v.empty()) v.push_back (0); return v; }
    int markerNear (int x) const
    {
        const auto v = currentMarks();
        for (int i = 1; i < (int) v.size(); ++i) if (std::abs (sampleToX (v[(size_t) i]) - (float) x) < 5.0f) return i;
        return -1;
    }
    int targetAt (juce::Point<int> p) const { return waveB.contains (p) ? kB : waveA.contains (p) ? kA : kAnywhere; }

    static void drawOverview (juce::Graphics& g, const slt::Source& s, juce::Rectangle<int> r, float alpha)
    {
        const float mid = (float) r.getCentreY(), h = (float) r.getHeight() * 0.46f;
        const float norm = s.peak > 1.0e-6f ? 1.0f / s.peak : 1.0f;
        juce::Path p;
        const int n = (int) s.overL.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) r.getX() + (float) i * (float) r.getWidth() / (float) n;
            const float a = std::max (s.overL[(size_t) i], s.overR[(size_t) i]) * norm * h;
            p.addRectangle (x, mid - a, std::max (1.0f, (float) r.getWidth() / (float) n), std::max (1.0f, 2.0f * a));
        }
        g.setColour (cSec.withAlpha (0.55f * alpha)); g.fillPath (p);
    }

    void paintA (juce::Graphics& g)
    {
        g.setColour (cLane); g.fillRoundedRectangle (waveA.toFloat(), 4.0f);
        g.setColour (hover == kA || hover == kAnywhere ? cPri : cLine); g.drawRoundedRectangle (waveA.toFloat(), 4.0f, 1.0f);
        auto s = proc.source (0);
        if (! s || ! s->audio)
        {
            g.setColour (cDim); g.setFont (uiFont (14.0f, true));
            g.drawText (proc.busy() ? "ANALYSING..." : "DROP A SAMPLE HERE  (or click to load, or pick a built-in)", waveA, juce::Justification::centred);
            return;
        }
        drawOverview (g, *s, waveA.reduced (2, 6), 1.0f);
        // slices
        if (sliceMode())
        {
            const auto v = dragMarker > 0 ? marks : currentMarks();
            const int first = juce::roundToInt (raw (proc, "slNote"));
            g.setFont (uiFont (9.5f, true));
            for (int i = 0; i < (int) v.size(); ++i)
            {
                const float x = sampleToX (v[(size_t) i]);
                g.setColour (cPri.withAlpha (i == dragMarker ? 1.0f : 0.6f)); g.fillRect (x, (float) waveA.getY() + 2.0f, i == dragMarker ? 2.0f : 1.0f, (float) waveA.getHeight() - 4.0f);
                const float nx = i + 1 < (int) v.size() ? sampleToX (v[(size_t) i + 1]) : (float) waveA.getRight();
                if (nx - x > 18.0f) { g.setColour (cSec); g.drawText (noteName (first + i), juce::Rectangle<float> (x + 2.0f, (float) waveA.getY() + 3.0f, nx - x - 3.0f, 11.0f), juce::Justification::centredLeft, false); }
            }
        }
        // position + spray band
        const float pos = raw (proc, "pos"), spray = raw (proc, "spray");
        const float px = (float) waveA.getX() + pos * (float) waveA.getWidth();
        if (! sliceMode())
        {
            g.setColour (cPri.withAlpha (0.08f)); g.fillRect (juce::Rectangle<float> (px - spray * 0.5f * (float) waveA.getWidth(), (float) waveA.getY() + 1.0f, spray * (float) waveA.getWidth(), (float) waveA.getHeight() - 2.0f).getIntersection (waveA.toFloat()));
            g.setColour (cPri); g.fillRect (px - 1.0f, (float) waveA.getY() + 1.0f, 2.0f, (float) waveA.getHeight() - 2.0f);
        }
        // grain cloud: newest taps, height = pitch, size = level, fading with age
        const auto& e = proc.engine();
        uint32_t now = 0;
        for (auto& t : e.taps) now = std::max (now, t.stamp.load (std::memory_order_relaxed));
        const float mid = (float) waveA.getCentreY();
        for (auto& t : e.taps)
        {
            const uint32_t st = t.stamp.load (std::memory_order_relaxed);
            if (st == 0 || t.layer.load (std::memory_order_relaxed) != 0) continue;
            const float age = (float) (now - st) / (float) juce::jmax (8000.0, proc.getSampleRate());
            if (age > 0.6f) continue;
            const float x = (float) waveA.getX() + juce::jlimit (0.0f, 1.0f, t.pos.load (std::memory_order_relaxed)) * (float) waveA.getWidth();
            const float y = juce::jlimit ((float) waveA.getY() + 4.0f, (float) waveA.getBottom() - 4.0f, mid - (t.pitch.load (std::memory_order_relaxed) - 60.0f) * 2.2f + t.pan.load (std::memory_order_relaxed) * 18.0f);
            const float d = 2.0f + 6.0f * std::sqrt (juce::jlimit (0.0f, 1.0f, t.amp.load (std::memory_order_relaxed)));
            g.setColour (cPri.withAlpha (0.85f * (1.0f - age / 0.6f))); g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
        // voice play heads
        for (auto& vp : e.voicePos)
        {
            const float v = vp.load (std::memory_order_relaxed);
            if (v < 0.0f) continue;
            const float x = (float) waveA.getX() + juce::jlimit (0.0f, 1.0f, v) * (float) waveA.getWidth();
            g.setColour (cSec.withAlpha (0.7f)); g.fillRect (x, (float) waveA.getBottom() - 12.0f, 1.5f, 10.0f);
        }
        g.setColour (cDim); g.setFont (uiFont (10.0f));
        g.drawText (juce::String (s->seconds(), 2) + " s" + (sliceMode() ? "   " + juce::String ((int) currentMarks().size()) + " slices - drag / double-click / right-click to edit" : juce::String()),
                    waveA.reduced (8, 3), juce::Justification::bottomLeft);
    }

    void paintB (juce::Graphics& g)
    {
        const bool on = raw (proc, "bOn") > 0.5f;
        g.setColour (cLane); g.fillRoundedRectangle (waveB.toFloat(), 4.0f);
        g.setColour (hover == kB ? cPri : cLine); g.drawRoundedRectangle (waveB.toFloat(), 4.0f, 1.0f);
        auto s = proc.source (1);
        if (s && s->audio)
        {
            drawOverview (g, *s, waveB.reduced (2, 3), on ? 1.0f : 0.35f);
            const float ox = (float) waveB.getX() + raw (proc, "bOffset") * (float) waveB.getWidth();
            g.setColour (cPri.withAlpha (on ? 1.0f : 0.4f)); g.fillRect (ox - 1.0f, (float) waveB.getY() + 1.0f, 2.0f, (float) waveB.getHeight() - 2.0f);
        }
        g.setColour (on ? cPri : cDim); g.setFont (uiFont (11.0f, true));
        g.drawText ("B  " + (s ? proc.sampleName (1).toUpperCase() : juce::String ("DROP A SECOND SAMPLE HERE")) + (on ? juce::String() : juce::String ("   (LAYER B OFF)")), waveB.reduced (8, 2), juce::Justification::topLeft, true);
    }

    void builtInMenu (int layer, juce::Component& anchor)
    {
        std::vector<PopupOverlay::Item> v;
        for (int k = 0; k < slt::kNumBuiltIn; ++k)
            v.push_back ({ juce::String (k + 1) + "  " + slt::builtInName (k), [this, layer, k] { proc.loadBuiltIn (layer, k); note.clear(); }, true, proc.builtInOf (layer) == k });
        panel.popupOverlay().show (panel.getLocalPoint (&anchor, juce::Point<int> (0, anchor.getHeight())), std::move (v));
    }

    void choose (int target)
    {
        chooser = std::make_unique<juce::FileChooser> (target == kB ? "Load a sample for layer B" : "Load a sample (any length up to 60 s)",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<SiltDisplay> safe (this);
        hostkeys::rememberForDialog (*this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, target] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                juce::Timer::callAfterDelay (60, [sp = juce::Component::SafePointer<juce::Component> (safe.getComponent())] { if (sp != nullptr) hostkeys::restoreAfterDialog (*sp); });
            if (safe == nullptr || fc.getResults().isEmpty()) return;
            safe->dropOn (target, juce::StringArray (fc.getResults()[0].getFullPathName()));
        });
    }

    void timerCallback() override
    {
        const auto msg = proc.lastMessage();
        if (msg != lastMsg) { lastMsg = msg; if (msg.isNotEmpty()) note = msg; }
        const bool hasA = proc.hasSample (0) || proc.builtInOf (0) >= 0, hasB = proc.hasSample (1) || proc.builtInOf (1) >= 0;
        clearA.setEnabled (hasA); clearB.setEnabled (hasB);
        rootBtn.setEnabled (proc.source (0) != nullptr);
        for (auto& c : chips)
        {
            bool used = false;
            for (int i = 1; i <= 8; ++i) used = used || juce::roundToInt (proc.apvts.getRawParameterValue ("ms" + juce::String (i))->load()) == c->src;
            if (used != c->live) { c->live = used; c->repaint(); }
        }
        repaint (waveA); repaint (waveB); repaint (status);
        if (proc.contentVersion() != lastVersion) { lastVersion = proc.contentVersion(); repaint(); }
    }

    SiltProcessor& proc;
    Panel& panel;
    juce::TextButton loadA { "LOAD..." }, builtA { "BUILT-IN" }, clearA { "CLEAR" }, rootBtn { "USE DETECTED ROOT" }, loadB { "LOAD B..." }, builtB { "BUILT-IN" }, clearB { "CLEAR" };
    Btn keep;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keepAtt;
    std::vector<std::unique_ptr<ModChip>> chips;
    std::unique_ptr<juce::MidiKeyboardComponent> keys;
    juce::Rectangle<int> nameA, rootText, keepLabel, waveA, waveB, chipLabel, status;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<int> marks;
    juce::String note, lastMsg;
    int lastVersion = -1, hover = kNone, dragMarker = -1;
    bool settingPos = false, settingOff = false;
};

// ---- MOD page: the 8-slot matrix (source / target / amount); drag-to-knob fills the next free slot
class ModMatrix final : public juce::Component
{
public:
    ModMatrix (SiltProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto& r = rows[(size_t) i];
            const auto n = juce::String (i + 1);
            for (auto* c : { &r.src, &r.dst }) { c->setLookAndFeel (&panel.look); addAndMakeVisible (*c); }
            r.src.addItemList (proc.apvts.getParameter ("ms" + n)->getAllValueStrings(), 1);
            r.dst.addItemList (proc.apvts.getParameter ("md" + n)->getAllValueStrings(), 1);
            r.sa = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "ms" + n, r.src);
            r.da = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "md" + n, r.dst);
            r.amt.setSliderStyle (juce::Slider::LinearBar);
            r.amt.setLookAndFeel (&panel.look);
            r.amt.setColour (juce::Slider::trackColourId, juce::Colour (0xff5a5a5a));
            addAndMakeVisible (r.amt);
            r.aa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "ma" + n, r.amt);
            auto* prm = proc.apvts.getParameter ("ma" + n);
            r.amt.textFromValueFunction = [prm] (double v) { return prm->getText (prm->convertTo0to1 ((float) v), 0) + " " + prm->getLabel(); };
            r.amt.valueFromTextFunction = [prm] (const juce::String& s) { return (double) prm->convertFrom0to1 (prm->getValueForText (s)); };
            r.amt.setDoubleClickReturnValue (true, 0.0);
            r.amt.updateText();
            r.amt.onContextMenu = [this, id = "ma" + n] (Knob& k, juce::Point<int> sp) { panel.showParamMenu (id, &k, panel.getLocalPoint (nullptr, sp)); };
            panel.registerParam (r.src, "ms" + n); panel.registerParam (r.dst, "md" + n);
        }
    }
    ~ModMatrix() override { for (auto& r : rows) { r.src.setLookAndFeel (nullptr); r.dst.setLookAndFeel (nullptr); r.amt.setLookAndFeel (nullptr); } }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff181818)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.35f)); g.drawRoundedRectangle (getLocalBounds().toFloat(), 6.0f, 1.0f);
        g.setColour (cSec.withAlpha (0.9f)); g.fillRect (1, 8, 2, 12);
        g.setColour (cDim); g.setFont (uiFont (12.0f, true));
        g.drawText ("MOD MATRIX  (or drag a MOD chip onto any knob)", 10, 4, getWidth() - 20, 16, juce::Justification::centredLeft);
        g.setFont (uiFont (10.5f, true));
        for (int i = 0; i < 8; ++i) g.drawText (juce::String (i + 1), slotLabel[(size_t) i], juce::Justification::centred);
    }
    void resized() override
    {
        const int colW = (getWidth() - 24) / 2;
        for (int i = 0; i < 8; ++i)
        {
            auto& r = rows[(size_t) i];
            juce::Rectangle<int> b (10 + (i / 4) * (colW + 4), 26 + (i % 4) * 32, colW, 28);
            slotLabel[(size_t) i] = b.removeFromLeft (18);
            r.src.setBounds (b.removeFromLeft (112).reduced (2, 2));
            r.dst.setBounds (b.removeFromLeft (124).reduced (2, 2));
            r.amt.setBounds (b.reduced (2, 3));
        }
    }
private:
    struct Row { NoFocusCombo src, dst; Knob amt; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sa, da; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aa; };
    SiltProcessor& proc; Panel& panel;
    std::array<Row, 8> rows;
    std::array<juce::Rectangle<int>, 8> slotLabel;
};

// ---- SLICE + SEQ page: 16 steps, each picks a slice (drag up / down, click toggles the step, the playing step lights up)
struct StepCell final : juce::Component
{
    StepCell (SiltProcessor& p, int i) : proc (p), idx (i), id ("st" + juce::String (i + 1)) { noFocus (*this); }
    int value() const { return juce::roundToInt (proc.apvts.getRawParameterValue (id)->load()); }
    void set (int v) { setParam (proc, id, (float) juce::jlimit (0, slt::kMaxSlices, v)); }
    void paint (juce::Graphics& g) override
    {
        const int v = value(); const bool now = proc.engine().seqStepNow.load() == idx && raw (proc, "seq") > 0.5f;
        const int len = juce::roundToInt (raw (proc, "seqLen"));
        const bool inLen = idx < len;
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (now ? cPri.withAlpha (0.3f) : juce::Colour (0xff161616)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (cPri.withAlpha (inLen ? (v > 0 ? 0.7f : 0.25f) : 0.1f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        if (v > 0)
        {
            const int n = juce::jmax (1, (int) proc.slices().size());
            const float h = (r.getHeight() - 30.0f) * (float) juce::jlimit (1, n, v) / (float) n;
            g.setColour (cSec.withAlpha (inLen ? 0.8f : 0.25f)); g.fillRect (r.getX() + 6.0f, r.getBottom() - 8.0f - h, r.getWidth() - 12.0f, h);
        }
        g.setColour (inLen ? cPri : cDim.withAlpha (0.5f)); g.setFont (uiFont (11.0f, true));
        g.drawText (v > 0 ? juce::String (v) : juce::String ("-"), r.removeFromTop (18), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) return; startVal = value(); moved = false; if (startVal > 0) lastOn = startVal; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const int d = -e.getDistanceFromDragStartY() / 8;
        if (d != 0 || moved) { moved = true; set (juce::jmax (1, (startVal > 0 ? startVal : lastOn) + d)); repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        if (! moved) set (value() > 0 ? 0 : lastOn);
        repaint();
    }
    SiltProcessor& proc; int idx; juce::String id; int startVal = 0, lastOn = 1; bool moved = false;
};

class StepGrid final : public juce::Component, private juce::Timer
{
public:
    StepGrid (SiltProcessor& p, Panel& pn) : proc (p)
    {
        for (int i = 0; i < 16; ++i) { auto& c = *cells.emplace_back (std::make_unique<StepCell> (p, i)); addAndMakeVisible (c); pn.registerParam (c, c.id); }
        startTimerHz (20);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff181818)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.35f)); g.drawRoundedRectangle (getLocalBounds().toFloat(), 6.0f, 1.0f);
        g.setColour (cSec.withAlpha (0.9f)); g.fillRect (1, 8, 2, 12);
        g.setColour (cDim); g.setFont (uiFont (12.0f, true));
        g.drawText ("STEPS  (slice per step: drag up / down, click = on / off; hold a key or press play to run it, keys transpose)", 10, 4, getWidth() - 20, 16, juce::Justification::centredLeft);
    }
    void resized() override
    {
        const int w = (getWidth() - 20) / 16;
        for (int i = 0; i < 16; ++i) cells[(size_t) i]->setBounds (10 + i * w, 24, w, getHeight() - 30);
    }
private:
    void timerCallback() override
    {
        const int now = proc.engine().seqStepNow.load();
        if (now != lastStep) { lastStep = now; for (auto& c : cells) c->repaint(); }
    }
    SiltProcessor& proc;
    std::vector<std::unique_ptr<StepCell>> cells;
    int lastStep = -2;
};

// ---- FX page: the rack order (drag a block left / right)
class FxRack final : public juce::Component
{
public:
    FxRack (SiltProcessor& p, Panel& pn) : proc (p) { noFocus (*this); pn.registerParam (*this, "fxOrder"); }
    std::array<int, 4> order() const { return slt::fxPerm (juce::roundToInt (raw (proc, "fxOrder"))); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff181818)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.35f)); g.drawRoundedRectangle (getLocalBounds().toFloat(), 6.0f, 1.0f);
        g.setColour (cDim); g.setFont (uiFont (11.5f, true));
        g.drawText ("FX ORDER  (drag to reorder)", 10, 0, 190, getHeight(), juce::Justification::centredLeft);
        const auto o = order();
        static const char* power[] = { "rpOn", "cpOn", "dlOn", "rvOn" };
        for (int i = 0; i < 4; ++i)
        {
            auto r = block (i).toFloat();
            if (drag == i) r = r.translated ((float) dragDx, 0.0f);
            const bool on = raw (proc, power[o[(size_t) i]]) > 0.5f;
            g.setColour (on ? juce::Colour (0xff262626) : juce::Colour (0xff141414)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (cPri.withAlpha (drag == i ? 1.0f : on ? 0.6f : 0.2f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
            g.setColour (on ? cPri : cDim); g.setFont (uiFont (12.0f, true));
            g.drawText (slt::fxName (o[(size_t) i]) + juce::String (on ? "" : "  (off)"), r.toNearestInt(), juce::Justification::centred);
            if (i < 3) { g.setColour (cDim); g.drawText (">", block (i).getRight(), 0, 24, getHeight(), juce::Justification::centred); }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { drag = -1; if (e.mods.isPopupMenu()) return; for (int i = 0; i < 4; ++i) if (block (i).contains (e.getPosition())) drag = i; dragDx = 0; }
    void mouseDrag (const juce::MouseEvent& e) override { if (drag >= 0) { dragDx = e.getDistanceFromDragStartX(); repaint(); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const int slotW = block (1).getX() - block (0).getX();
        const int to = juce::jlimit (0, 3, drag + juce::roundToInt ((float) e.getDistanceFromDragStartX() / (float) slotW));
        move (drag, to);
        drag = -1; dragDx = 0; repaint();
    }
    void move (int from, int to)   // also the test hook
    {
        auto o = order();
        std::vector<int> v (o.begin(), o.end());
        const int f = v[(size_t) from]; v.erase (v.begin() + from); v.insert (v.begin() + to, f);
        std::array<int, 4> n { v[0], v[1], v[2], v[3] };
        setParam (proc, "fxOrder", (float) slt::fxPermIndex (n));
    }
private:
    juce::Rectangle<int> block (int i) const { const int x0 = 200, w = (getWidth() - x0 - 10 - 3 * 24) / 4; return { x0 + i * (w + 24), 6, w, getHeight() - 12 }; }
    SiltProcessor& proc; int drag = -1, dragDx = 0;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (SiltProcessor& p, Panel& pn) { return std::make_unique<SiltDisplay> (p, pn); }
std::unique_ptr<juce::Component> makeExtra (int kind, SiltProcessor& p, Panel& pn)
{
    if (kind == 1) return std::make_unique<ModMatrix> (p, pn);
    if (kind == 2) return std::make_unique<StepGrid> (p, pn);
    if (kind == 3) return std::make_unique<FxRack> (p, pn);
    return {};
}
// test / screenshot hooks (the classes are private to this file)
void displayDrop (juce::Component* d, int target, const juce::StringArray& files) { if (auto* r = dynamic_cast<SiltDisplay*> (d)) r->dropOn (target, files); }
void displayHover (juce::Component* d, int target) { if (auto* r = dynamic_cast<SiltDisplay*> (d)) r->demoHover (target); }
void fxRackMove (juce::Component* rack, int from, int to) { if (auto* r = dynamic_cast<FxRack*> (rack)) r->move (from, to); }
} // namespace sltui
