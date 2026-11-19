// Rewired: control layout + the main display (load song / stems, process, 7 part rows with drag-out chips, export).
#include "PluginEditor.h"
#include "RewiredIO.h"

namespace rwui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY REWIRED", "AUDIO TO MIDI  /  SONG OR STEMS  /  OFFLINE",
        { 16, 56, 868, 300 },
        {
            { "DETECTION", nullptr, { 16, 366, 520, 162 },  { { 'k', "onset", "ONSET" }, { 'k', "frame", "HOLD" }, { 'k', "minNote", "SHORTEST" },
                                                              { 'k', "melGap", "MELODY GAP" }, { 'k', "chordMin", "CHORD NOTES" }, { 'c', "melSrc", "MELODY FROM" } }, 124 },
            { "DRUMS",     nullptr, { 546, 366, 130, 162 }, { { 'k', "drumSens", "SENSITIVITY" } } },
            { "OUTPUT",    nullptr, { 686, 366, 198, 162 }, { { 'k', "bpm", "TEMPO" }, { 'c', "quantize", "QUANTIZE" } }, 96 },
        },
        "onset"
    };
    return s;
}

// which stem slot a file name points at ("Bass.wav" -> bass, "drums" -> drums, "vocal" / "lead" -> melody, "keys" / "piano" -> chords);
// -1 when the name says nothing
int stemSlotByName (const juce::String& fileName)
{
    const auto n = fileName.toLowerCase();
    if (n.contains ("bass") || n.contains ("808") || n.contains ("sub")) return RewiredProcessor::kBassStem;
    if (n.contains ("drum") || n.contains ("perc") || n.contains ("kick") || n.contains ("beat") || n.contains ("snare")) return RewiredProcessor::kDrumsStem;
    if (n.contains ("vocal") || n.contains ("vox") || n.contains ("lead") || n.contains ("melod") || n.contains ("voice") || n.contains ("sing"))
        return RewiredProcessor::kMelodyStem;
    for (auto* k : { "chord", "key", "piano", "pad", "synth", "guitar", "gtr", "other", "string", "horn", "brass", "organ", "music", "inst", "accomp" })
        if (n.contains (k)) return RewiredProcessor::kChordsStem;
    return -1;
}
int guessStemSlot (const juce::String& fileName) { const int s = stemSlotByName (fileName); return s < 0 ? (int) RewiredProcessor::kChordsStem : s; }

// several stem files at once: each goes where its name points; files whose name says nothing (or clash) take the empty slots, in the order
// chords, melody, bass, drums (dropped on one stem box: that box first). Returns slot per file (-1 = no room).
std::vector<int> assignStemSlots (const juce::StringArray& files, const std::array<bool, 5>& occupied, int preferred)
{
    std::vector<int> out ((size_t) files.size(), -1);
    std::array<bool, 5> taken {};
    for (int i = 0; i < files.size(); ++i)
    {
        const int s = stemSlotByName (juce::File (files[i]).getFileNameWithoutExtension());
        if (s > 0 && ! taken[(size_t) s]) { out[(size_t) i] = s; taken[(size_t) s] = true; }
    }
    if (preferred > 0 && ! taken[(size_t) preferred])   // dropped on one stem box: the first file whose name says nothing goes there
        for (int i = 0; i < files.size(); ++i)
            if (out[(size_t) i] < 0) { out[(size_t) i] = preferred; taken[(size_t) preferred] = true; break; }
    const int order[] = { RewiredProcessor::kChordsStem, RewiredProcessor::kMelodyStem, RewiredProcessor::kBassStem, RewiredProcessor::kDrumsStem };
    for (int pass = 0; pass < 2; ++pass)   // first slots that are empty now, then ones that only hold an older file
        for (int i = 0; i < files.size(); ++i)
        {
            if (out[(size_t) i] >= 0) continue;
            for (int s : order) if (! taken[(size_t) s] && (pass == 1 || ! occupied[(size_t) s])) { out[(size_t) i] = s; taken[(size_t) s] = true; break; }
        }
    return out;
}

namespace
{
const juce::Colour cBg (0xff101010), cLane (0xff161616), cLine (0xff2d2d2d), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);

// 1.1.0 transport + tempo controls. None of them takes the keyboard focus (Space and the other keys stay the host's).
struct SmallBtn : juce::TextButton
{
    explicit SmallBtn (const juce::String& t) : juce::TextButton (t) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
};

// PLAY / STOP with a drawn icon (the bundled font has no transport glyphs)
struct PlayBtn : juce::Button
{
    PlayBtn() : juce::Button ("PLAY") { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    bool on = false;
    juce::String label;
    void paintButton (juce::Graphics& g, bool hi, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, juce::Colour (on ? 0xff4a4a4a : 0xff1c1c1c), hi && isEnabled(), down);
        const auto c = isEnabled() ? cPri : cDim.withAlpha (0.6f);
        auto r = getLocalBounds().reduced (8, 0);
        auto ic = r.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat();
        g.setColour (c);
        if (on) g.fillRect (ic);
        else { juce::Path t; t.addTriangle (ic.getX(), ic.getY() - 0.5f, ic.getX(), ic.getBottom() + 0.5f, ic.getRight() + 1.0f, ic.getCentreY()); g.fillPath (t); }
        g.setFont (uiFont (10.5f, true));
        g.drawText (label.isNotEmpty() ? label : (on ? juce::String ("STOP") : juce::String ("PLAY")), r.withTrimmedLeft (5), juce::Justification::centredLeft);
    }
};

// The tempo: drag up / down (1 bpm per 4 px), mouse wheel, or double-click and type. Whole bpm only.
class BpmBox : public juce::Component, private juce::TextEditor::Listener
{
public:
    std::function<double()> value;          // what to show (0 = nothing yet)
    std::function<void (double)> onChange;  // whole bpm, live while dragging
    std::function<void (bool)> gesture;     // drag start / end (one undo step per drag)
    std::function<void (bool)> onTyping;    // the type-in box opened / closed
    BpmBox()
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        addChildComponent (ed);
        ed.setJustification (juce::Justification::centred);
        ed.setInputRestrictions (5, "0123456789.autoAUTO");   // a tempo ("174", "86.5" rounds) or "auto"
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff3a3a3a));   // brighter than the box: clearly typing
        ed.setColour (juce::TextEditor::textColourId, cPri);
        ed.setColour (juce::TextEditor::outlineColourId, cPri.withAlpha (0.6f));
        ed.setColour (juce::TextEditor::focusedOutlineColourId, cPri);
        ed.addListener (this);
    }
    bool typing() const { return ed.isVisible(); }
    void startTyping()
    {
        if (! isEnabled()) return;
        const double v = value ? value() : 0.0;
        ed.setFont (uiFont (12.0f, true));
        ed.setText (v > 0 ? juce::String (juce::roundToInt (v)) : juce::String(), false);
        ed.setBounds (getLocalBounds()); ed.setVisible (true);
        ed.setWantsKeyboardFocus (true);
        ed.grabKeyboardFocus(); ed.selectAll();
        if (onTyping) onTyping (true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff1c1c1c)); g.fillRoundedRectangle (r, 3.0f);
        g.setColour (isEnabled() ? cPri.withAlpha (dragging ? 0.9f : 0.55f) : cLine); g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const double v = value ? value() : 0.0;
        g.setColour (isEnabled() ? cPri : cDim); g.setFont (uiFont (12.0f, true));
        g.drawText (v > 0 ? juce::String (juce::roundToInt (v)) : juce::String ("--"), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || e.mods.isPopupMenu()) return;
        start = value ? value() : 0.0; if (start <= 0) start = 120.0;
        dragging = true; lastSent = -1; if (gesture) gesture (true); repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const double v = juce::jlimit (20.0, 250.0, std::round (start - e.getDistanceFromDragStartY() / 4.0));
        if (v != lastSent && onChange) { lastSent = v; onChange (v); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; if (gesture) gesture (false); repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        juce::Component::SafePointer<BpmBox> sp (this);
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->startTyping(); });   // after the click's own focus hand-back
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (! isEnabled() || std::abs (w.deltaY) < 1e-4f) return;
        double v = value ? value() : 0.0; if (v <= 0) v = 120.0;
        if (onChange) onChange (juce::jlimit (20.0, 250.0, std::round (v) + (w.deltaY > 0 ? 1.0 : -1.0)));
        repaint();
    }
private:
    void finish (bool apply)
    {
        if (! ed.isVisible()) return;
        const auto t = ed.getText().trim();
        ed.setVisible (false); ed.setWantsKeyboardFocus (false);
        if (apply && onChange)
        {
            if (t.equalsIgnoreCase ("auto") || t.equalsIgnoreCase ("a")) onChange (0.0);
            else if (t.isNotEmpty() && t.containsOnly ("0123456789.") && t.getDoubleValue() > 0.0)
                onChange (juce::jlimit (20.0, 250.0, std::round (t.getDoubleValue())));
        }
        hostkeys::releaseLater (*this);   // typing done: Space goes to the host again
        if (onTyping) onTyping (false);
        repaint();
    }
    void textEditorReturnKeyPressed (juce::TextEditor&) override { finish (true); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { finish (false); }
    void textEditorFocusLost (juce::TextEditor&) override { finish (true); }
    juce::TextEditor ed;
    double start = 0, lastSent = -1;
    bool dragging = false;
};

class RewiredDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    // drop targets: 0..4 = the SONG / BASS / MELODY / CHORDS / DRUMS boxes (= processor slots), then the two load buttons, then "anywhere else"
    enum Target { kNone = -1, kBtnSong = 10, kBtnStems = 11, kAnywhere = 20 };

    RewiredDisplay (RewiredProcessor& p) : proc (p)
    {
        for (auto* b : { &loadSong, &loadStems, &clear, &process, &exportAll, &exportSel })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
        }
        for (int i = 0; i < rw::kNumParts; ++i)
        {
            auto& t = toggles[(size_t) i];
            addAndMakeVisible (t);
            t.setToggleState (proc.partOn (i), juce::dontSendNotification);
            t.onClick = [this, i] { proc.setPartOn (i, toggles[(size_t) i].getToggleState()); };
        }
        for (auto* b : { &notesTab, &mapTab })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a4a4a));
            b->setColour (juce::TextButton::textColourOnId, cPri); b->setColour (juce::TextButton::textColourOffId, cSec);
        }
        for (juce::Component* c : { (juce::Component*) &playBtn, (juce::Component*) &bpmDown, (juce::Component*) &bpmUp, (juce::Component*) &bpmHalf,
                                    (juce::Component*) &bpmDouble, (juce::Component*) &bpmAuto, (juce::Component*) &bpmBox })
            addAndMakeVisible (*c);
        bpmAuto.setClickingTogglesState (false);
        bpmAuto.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a4a4a));
        bpmAuto.setColour (juce::TextButton::textColourOnId, cPri); bpmAuto.setColour (juce::TextButton::textColourOffId, cSec);
        playBtn.onClick = [this] { proc.togglePlay(); refresh(); };
        bpmBox.value = [this] { return shownBpm(); };
        bpmBox.onChange = [this] (double v) { proc.setBpm (v); };
        bpmBox.onTyping = [this] (bool on) { typingHint = on; repaint(); };
        bpmBox.gesture = [this] (bool on) { auto* p = proc.apvts.getParameter ("bpm"); if (on) p->beginChangeGesture(); else p->endChangeGesture(); };
        bpmDown.onClick   = [this] { nudgeBpm (-1.0, 1.0); };
        bpmUp.onClick     = [this] { nudgeBpm (1.0, 1.0); };
        bpmHalf.onClick   = [this] { nudgeBpm (0.0, 0.5); };
        bpmDouble.onClick = [this] { nudgeBpm (0.0, 2.0); };
        // AUTO on = the detected tempo; click it off to keep the tempo shown as your own (it then stays when you PROCESS again)
        bpmAuto.onClick   = [this] { if (proc.bpmIsAuto()) { const double v = shownBpm(); proc.setBpm (v > 0 ? v : 120.0); } else proc.setBpm (0.0); refresh(); };
        notesTab.onClick = [this] { proc.setMapView (false); refresh(); };
        mapTab.onClick   = [this] { proc.setMapView (true); refresh(); };
        loadSong.onClick  = [this] { choose (false); };
        loadStems.onClick = [this] { choose (true); };
        clear.onClick     = [this] { proc.clearAll(); note.clear(); expanded = -1; refresh(); };
        process.onClick   = [this] { if (proc.status() == RewiredProcessor::Status::working) proc.cancelProcess(); else proc.startProcess(); note.clear(); refresh(); };
        exportAll.onClick = [this] { if (proc.mapView()) doExportMap(); else doExport (false); };
        exportSel.onClick = [this] { doExport (true); };
        startTimerHz (30);
        refresh();
    }
    ~RewiredDisplay() override { stopTimer(); }

    // the tempo shown: the result's (whole bpm) or, before a run, the knob's (0 = auto: nothing yet)
    double shownBpm() const { return res.ok ? res.bpm : (proc.bpmIsAuto() ? 0.0 : proc.effectiveBpm()); }
    void nudgeBpm (double add, double mul)
    {
        double v = shownBpm(); if (v <= 0) v = 120.0;
        proc.setBpm (juce::jlimit (20.0, 250.0, std::round (v * mul + add)));
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8).removeFromTop (26);
        loadSong.setBounds (r.removeFromLeft (100)); r.removeFromLeft (6);
        loadStems.setBounds (r.removeFromLeft (104)); r.removeFromLeft (6);
        clear.setBounds (r.removeFromLeft (64)); r.removeFromLeft (6);
        process.setBounds (r.removeFromLeft (92)); r.removeFromLeft (16);
        exportAll.setBounds (r.removeFromLeft (98)); r.removeFromLeft (6);
        exportSel.setBounds (r.removeFromLeft (128)); r.removeFromLeft (6);
        dragAll = r;
        // five drop boxes: the song box is wider (it is the one most people use)
        auto strip = getLocalBounds().reduced (10, 0).withTrimmedTop (40).removeFromTop (22);
        const int songW = 248, gap = 6, stemW = (strip.getWidth() - songW - 4 * gap) / 4;
        boxes[0] = strip.removeFromLeft (songW);
        for (int s = 1; s < RewiredProcessor::kNumSlots; ++s) { strip.removeFromLeft (gap); boxes[s] = s == 4 ? strip : strip.removeFromLeft (stemW); }
        body = getLocalBounds().reduced (10, 0).withTrimmedTop (68).withTrimmedBottom (28);
        status = getLocalBounds().reduced (10, 0).removeFromBottom (26);
        {
            // [PLAY] 1:23.4 / 3:05  BAR 17.3 | BPM [-][140][+] [1/2][x2][AUTO] | status text ... [NOTES][MAP]
            auto t = status.withTrimmedTop (5).withTrimmedBottom (3);
            auto tabs = t.removeFromRight (124);
            mapTab.setBounds (tabs.removeFromRight (60)); tabs.removeFromRight (4); notesTab.setBounds (tabs.removeFromRight (60));
            t.removeFromRight (8);
            playBtn.setBounds (t.removeFromLeft (62)); t.removeFromLeft (6);
            timeArea = t.removeFromLeft (118); t.removeFromLeft (4);
            bpmLabel = t.removeFromLeft (26);
            bpmDown.setBounds (t.removeFromLeft (18)); t.removeFromLeft (2);
            bpmBox.setBounds (t.removeFromLeft (38)); t.removeFromLeft (2);
            bpmUp.setBounds (t.removeFromLeft (18)); t.removeFromLeft (5);
            bpmHalf.setBounds (t.removeFromLeft (28)); t.removeFromLeft (2);
            bpmDouble.setBounds (t.removeFromLeft (26)); t.removeFromLeft (2);
            bpmAuto.setBounds (t.removeFromLeft (40)); t.removeFromLeft (10);
            msgArea = t;
        }
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (hover == kAnywhere ? 0.7f : 0.25f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, hover == kAnywhere ? 1.5f : 1.0f);
        const bool ready = proc.status() == RewiredProcessor::Status::ready && res.ok;
        const bool mapOn = proc.mapView();
        const bool mapReady = ready && res.map.ok;
        if (mapOn) chip (g, dragAll, mapReady, mapReady ? "DRAG MAP (.MID)" : "DRAG MAP: -");
        else chip (g, dragAll, ready, ready ? "DRAG ALL (.MID)" : "DRAG ALL: -");

        // drop boxes
        for (int s = 0; s < RewiredProcessor::kNumSlots; ++s)
        {
            const auto b = boxes[s];
            const bool hot = hover == s, filled = proc.input (s) != juce::File();
            g.setColour (hot ? cPri.withAlpha (0.12f) : cLane); g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (hot ? cPri : (filled ? cSec.withAlpha (0.7f) : cLine.brighter (0.15f)));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.0f, hot ? 1.5f : 1.0f);
            auto t = b.reduced (7, 0);
            g.setFont (uiFont (10.0f, true)); g.setColour (hot || filled ? cPri : cSec);
            const juce::String label = s == 0 ? "SONG" : juce::String (RewiredProcessor::slotName (s)).toUpperCase();
            g.drawText (label, t.removeFromLeft (s == 0 ? 36 : 52), juce::Justification::centredLeft);
            g.setFont (uiFont (10.5f)); g.setColour (filled ? cSec : cDim.withAlpha (0.8f));
            const juce::String what = filled ? proc.input (s).getFileName()
                                    : hot ? juce::String ("release to load") : (s == 0 ? juce::String ("drop a song (split for you)") : juce::String ("drop stem"));
            g.drawFittedText (what, t, juce::Justification::centredLeft, 1, 0.9f);
        }

        const double len = notesLength();
        if (mapOn) paintMap (g, res, mapReady);
        else for (auto& ln : lines)
        {
            const int i = ln.part;
            const auto r = ln.row;
            const bool on = proc.partOn (i);
            const bool sub = ln.sub >= 0;
            const auto& subs = res.subs[(size_t) i];
            if (sub && (size_t) ln.sub >= subs.size()) continue;
            const auto& notes = sub ? subs[(size_t) ln.sub].notes : res.parts[(size_t) i];
            g.setColour (sub ? cBg.brighter (0.03f) : cLane); g.fillRect (r);
            g.setColour (cLine); g.drawRect (r);
            if (! sub)
            {
                g.setColour (on ? cPri : cDim); g.setFont (uiFont (11.0f, true));
                const int nSubs = ready ? (int) subs.size() : 0;
                auto nameR = juce::Rectangle<int> (r.getX() + 32, r.getY(), 70, r.getHeight());
                if (nSubs > 0 && r.getHeight() >= 22)
                {
                    g.drawText (juce::String (rw::partName (i)).toUpperCase(), nameR.removeFromTop (r.getHeight() / 2 + 2), juce::Justification::bottomLeft);
                    g.setFont (uiFont (9.5f)); g.setColour (cSec);
                    g.drawText (juce::String (expanded == i ? "- " : "+ ") + juce::String (nSubs) + (nSubs == 1 ? " INSTR." : " INSTR."), nameR, juce::Justification::topLeft);
                }
                else g.drawText (juce::String (rw::partName (i)).toUpperCase(), nameR, juce::Justification::centredLeft);
            }
            else
            {
                const auto& sp = subs[(size_t) ln.sub];
                g.setColour (on ? cSec : cDim); g.setFont (uiFont (10.5f));
                g.drawFittedText (juce::String (sp.name), r.getX() + 40, r.getY(), 62, r.getHeight(), juce::Justification::centredLeft, 1, 0.8f);
            }
            const auto lane = ln.lane;
            g.setColour (cLine.brighter (0.1f)); g.drawRect (lane);
            if (! notes.empty())
            {
                int lo = 127, hi = 0;
                for (auto& n : res.parts[(size_t) i]) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }   // same scale as the parent row
                const bool drum = rw::isDrumPart (i);
                g.setColour (on ? cPri.withAlpha (sub ? 0.7f : 0.85f) : cDim.withAlpha (0.45f));
                for (auto& n : notes)
                {
                    const float x0 = lane.getX() + (float) (n.start / len) * lane.getWidth();
                    const float w = drum ? 1.0f : std::max (1.0f, (float) ((n.end - n.start) / len) * lane.getWidth());
                    const float y = drum ? (float) lane.getY() + 2.0f
                                         : lane.getBottom() - 3.0f - (hi > lo ? (float) (n.pitch - lo) / (float) (hi - lo) : 0.5f) * (lane.getHeight() - 6.0f);
                    g.fillRect (x0, drum ? y : y - 1.0f, w, drum ? lane.getHeight() - 4.0f : 2.0f);
                }
            }
            g.setColour (cDim); g.setFont (uiFont (sub ? 9.5f : 10.5f));
            juce::String right = ready ? juce::String ((int) notes.size()) + " notes" : juce::String ("-");
            if (sub) { const auto& sp = subs[(size_t) ln.sub]; right = sp.fromName ? "file name" : "guess " + juce::String (juce::roundToInt (sp.confidence * 100.0f)) + "%"; }
            g.drawText (right, lane.getRight() + 2, r.getY(), 48, r.getHeight(), juce::Justification::centredRight);
            chip (g, ln.chip, ready && ! notes.empty(), ready && ! notes.empty() ? "DRAG MIDI" : "-");
            if (showHead())   // playhead
            {
                const float x = lane.getX() + (float) juce::jlimit (0.0, 1.0, proc.playPosition() / len) * lane.getWidth();
                g.setColour (cPri); g.fillRect (x - 0.5f, (float) r.getY(), 1.5f, (float) r.getHeight());
            }
        }
        if (ready && expanded >= 0 && ! mapOn)
        {
            g.setFont (uiFont (9.5f)); g.setColour (cDim);
            g.drawText ("instrument names are a best guess from the sound", body.getX() + 104, body.getBottom() + 1, 400, 11, juce::Justification::centredLeft);
        }
        // transport time, tempo label, status + progress
        {
            const double pos = proc.playPosition(), total = proc.playLength();
            auto mmss = [] (double t) { const int s10 = (int) std::floor (std::max (0.0, t) * 10.0); return juce::String (s10 / 600) + ":" + juce::String ((s10 / 10) % 60).paddedLeft ('0', 2) + "." + juce::String (s10 % 10); };
            const bool have = proc.canPlay();
            auto ta = timeArea.withTrimmedTop (5).withTrimmedBottom (3);
            g.setFont (uiFont (11.0f, true)); g.setColour (have ? cPri : cDim);
            juce::String bar;
            if (res.ok && res.map.ok) { const double b = res.map.toBar (pos); bar = b < 0 ? juce::String ("LEAD-IN") : "BAR " + juce::String ((int) std::floor (b) + 1) + "." + juce::String ((int) std::floor ((b - std::floor (b)) * 4.0) + 1); }
            g.drawText (proc.previewLoading() ? juce::String ("LOADING...") : mmss (pos) + (total > 0 ? " / " + mmss (total).upToFirstOccurrenceOf (".", false, false) : juce::String()),
                        ta.removeFromLeft (72), juce::Justification::centredLeft);
            g.setFont (uiFont (10.0f)); g.setColour (cSec);
            g.drawText (bar, ta, juce::Justification::centredLeft);
            g.setFont (uiFont (9.5f, true)); g.setColour (cSec);
            g.drawText ("BPM", bpmLabel.withTrimmedTop (5).withTrimmedBottom (3), juce::Justification::centredLeft);
        }
        g.setFont (uiFont (11.0f)); g.setColour (cSec);
        const bool busy = proc.status() == RewiredProcessor::Status::working;
        auto msg = msgArea.withTrimmedTop (5).withTrimmedBottom (3);
        if (busy)
        {
            auto bar = msg.removeFromRight (std::min (140, msg.getWidth() / 2)).withSizeKeepingCentre (std::min (140, msg.getWidth() / 2), 6);
            g.setColour (cLine); g.fillRect (bar);
            g.setColour (cPri); g.fillRect (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc.progress()))));
            msg.removeFromRight (8);
        }
        g.setColour (cSec);
        g.drawFittedText (statusText(), msg, juce::Justification::centredLeft, 1, 0.8f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (hover != kBtnSong && hover != kBtnStems) return;
        const auto b = (hover == kBtnSong ? loadSong : loadStems).getBounds().toFloat().expanded (2.0f);
        g.setColour (cPri.withAlpha (0.12f)); g.fillRoundedRectangle (b, 4.0f);
        g.setColour (cPri); g.drawRoundedRectangle (b, 4.0f, 1.5f);
    }

    // ---- file drops
    static bool audioFile (const juce::String& f) { return juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        if (proc.status() == RewiredProcessor::Status::working) return false;
        for (auto& f : files) if (audioFile (f)) return true;
        return false;
    }
    int targetAt (juce::Point<int> p) const
    {
        for (int s = 0; s < RewiredProcessor::kNumSlots; ++s) if (boxes[s].contains (p)) return s;
        if (loadSong.getBounds().contains (p)) return kBtnSong;
        if (loadStems.getBounds().contains (p)) return kBtnStems;
        return kAnywhere;
    }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { setHover (targetAt ({ x, y })); }
    void fileDragMove (const juce::StringArray&, int x, int y) override  { setHover (targetAt ({ x, y })); }
    void fileDragExit (const juce::StringArray&) override                { setHover (kNone); }
    void filesDropped (const juce::StringArray& files, int x, int y) override { setHover (kNone); dropOn (targetAt ({ x, y }), files); }

public:
    // also the test hook: what a drop of `files` on `target` does
    void dropOn (int target, const juce::StringArray& dropped)
    {
        juce::StringArray files; for (auto& f : dropped) if (audioFile (f)) files.add (f);
        const int skipped = dropped.size() - files.size();
        if (files.isEmpty()) { note = "Rewired reads WAV, AIFF, FLAC, OGG and MP3"; repaint(); return; }
        note.clear();
        if (target == RewiredProcessor::kSong || target == kBtnSong || (target == kAnywhere && files.size() == 1))
        {
            note = proc.setInput (RewiredProcessor::kSong, juce::File (files[0]));
            if (note.isEmpty() && files.size() > 1) note = "A song is one file - loaded " + juce::File (files[0]).getFileName() + "; drop several files on the stem boxes instead";
        }
        else if (target >= 1 && target < RewiredProcessor::kNumSlots && files.size() == 1)
        {
            proc.clearInput (RewiredProcessor::kSong);
            note = proc.setInput (target, juce::File (files[0]));
        }
        else
        {
            proc.clearInput (RewiredProcessor::kSong);
            std::array<bool, 5> occ {}; for (int s = 1; s < RewiredProcessor::kNumSlots; ++s) occ[(size_t) s] = proc.input (s) != juce::File();
            const auto slots = assignStemSlots (files, occ, target >= 1 && target < RewiredProcessor::kNumSlots ? target : -1);
            int extra = 0;
            for (int i = 0; i < files.size(); ++i)
            {
                if (slots[(size_t) i] < 0) { ++extra; continue; }
                const auto e = proc.setInput (slots[(size_t) i], juce::File (files[i]));
                if (e.isNotEmpty()) note = e;
            }
            if (note.isEmpty() && extra > 0) note = juce::String (extra) + (extra == 1 ? " file" : " files") + " left out - there are four stem slots";
        }
        if (note.isEmpty() && skipped > 0) note = juce::String (skipped) + " non-audio file" + (skipped == 1 ? "" : "s") + " ignored";
        refresh();
    }
    void expand (int part) { expanded = (expanded == part ? -1 : part); layoutRows(); repaint(); }
    int expandedPart() const { return expanded; }
    juce::Rectangle<int> boxBounds (int slot) const { return boxes[slot]; }
    void demoHover (int target) { hover = target; repaint(); }
    int mapRowCount() const { return (int) mapRows.size(); }
    // test hooks: a click where a user would click (row = visible map row)
    void clickAt (juce::Point<int> p) { handleClick (p); }
    juce::Point<int> mapToggleAt (int row) const { return row >= 0 && row < (int) mapRows.size() ? mapRows[(size_t) row].toggle.getCentre() : juce::Point<int>(); }
    juce::Point<int> mapSoloAt (int row) const { return row >= 0 && row < (int) mapRows.size() ? mapRows[(size_t) row].solo.getCentre() : juce::Point<int>(); }
    juce::Point<int> mapStripAt (int row, double seconds) const { return row >= 0 && row < (int) mapRows.size() ? juce::Point<int> ((int) std::lround (mapX (seconds)), mapRows[(size_t) row].strip.getCentreY()) : juce::Point<int>(); }
    juce::Point<int> sectionAt (int i) const
    {
        if (! res.map.ok || i < 0 || i >= (int) res.map.sections.size()) return {};
        const auto& sc = res.map.sections[(size_t) i];
        return { (int) std::lround ((mapX (res.map.barTime (sc.startBar)) + mapX (res.map.barTime (sc.endBar))) / 2.0f), rulerBounds().getCentreY() };
    }
    juce::Component* control (const juce::String& n)
    {
        if (n == "play") return &playBtn; if (n == "bpm-") return &bpmDown; if (n == "bpm+") return &bpmUp; if (n == "half") return &bpmHalf;
        if (n == "double") return &bpmDouble; if (n == "auto") return &bpmAuto; if (n == "bpm") return &bpmBox; if (n == "notes") return &notesTab;
        if (n == "map") return &mapTab; if (n == "process") return &process; if (n == "exportAll") return &exportAll; if (n == "exportSel") return &exportSel;
        if (n == "clear") return &clear; if (n.startsWith ("part")) return &toggles[(size_t) juce::jlimit (0, rw::kNumParts - 1, n.getTrailingIntValue())];
        return nullptr;
    }
    juce::String statusText() const
    {
        if (typingHint) return "Type a tempo (20-250) or AUTO, then Enter (Esc cancels)";
        return note.isNotEmpty() ? note : hint.isNotEmpty() ? hint : proc.lastMessage();
    }
    void typeBpm (const juce::String& t, bool commit) { bpmBox.startTyping(); for (auto* c : bpmBox.getChildren()) if (auto* ed = dynamic_cast<juce::TextEditor*> (c)) { ed->setText (t); if (commit) ed->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); } }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        handleClick (e.getPosition());
    }
    void mouseMove (const juce::MouseEvent& e) override { updateHint (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { if (hint.isNotEmpty() || hoverSection >= 0) { hint.clear(); hoverSection = -1; repaint(); } }

    // what a click does. MAP: M = real MIDI in the file, S = solo (hear that lane), a section = play from its start, a lane = move the playhead.
    // NOTES: a part's name opens its instruments, a lane moves the playhead.
    void handleClick (juce::Point<int> p)
    {
        note.clear();
        if (proc.mapView())
        {
            if (! res.ok || ! res.map.ok) return;
            for (auto& mr : mapRows)
            {
                if (mr.toggle.expanded (2).contains (p)) { proc.setMapMidi (mr.name, ! proc.mapMidi (mr.name)); updateHint (p); repaint(); return; }
                if (mr.solo.expanded (2).contains (p))
                {
                    const auto& L = res.map.lanes[(size_t) mr.lane];
                    if (proc.laneSoloSource (L, res) == 0) { note = mr.name + ": nothing to play for this lane"; repaint(); return; }
                    const bool on = proc.solo() != mr.name;
                    proc.setSolo (on ? mr.name : juce::String());
                    if (on && ! proc.playing()) proc.play (proc.playPosition() >= proc.playLength() - 0.05 ? 0.0 : proc.playPosition());
                    updateHint (p); repaint(); return;
                }
            }
            if (rulerBounds().contains (p))
            {
                const int b = (int) std::floor (res.map.toBar (mapTime (p.x)));
                for (auto& sc : res.map.sections)
                    if (b >= sc.startBar && b < sc.endBar) { proc.play (std::max (0.0, res.map.barTime (sc.startBar))); repaint(); return; }
                return;
            }
            for (auto& mr : mapRows) if (mr.strip.contains (p)) { proc.seek (mapTime (p.x)); repaint(); return; }
            return;
        }
        for (auto& ln : lines)
            if (ln.sub < 0 && ln.row.withTrimmedLeft (28).withWidth (76).contains (p) && res.ok && ! res.subs[(size_t) ln.part].empty())
            { expand (ln.part); return; }
        for (auto& ln : lines)
            if (res.ok && ln.lane.contains (p)) { proc.seek ((double) (p.x - ln.lane.getX()) / std::max (1, ln.lane.getWidth()) * notesLength()); repaint(); return; }
    }

    // hover help in the status line (the switches are small, so say what they do)
    void updateHint (juce::Point<int> p)
    {
        juce::String h; int sec = -1;
        if (proc.mapView() && res.ok && res.map.ok)
        {
            for (auto& mr : mapRows)
            {
                if (mr.toggle.expanded (2).contains (p))
                    h = proc.mapMidi (mr.name) ? "MIDI on: " + mr.name + " goes into the map file as its transcribed notes - click for empty blocks"
                                               : "MIDI off: " + mr.name + " is empty blocks in the map file - click to put its real notes in";
                else if (mr.solo.expanded (2).contains (p))
                {
                    const int src = proc.laneSoloSource (res.map.lanes[(size_t) mr.lane], res);
                    h = proc.solo() == mr.name ? "SOLO: click to hear the whole song again"
                      : src == 1 ? "SOLO: hear only " + mr.name + " (its stem)" : src == 2 ? "SOLO: hear " + mr.name + "'s notes" : mr.name + ": nothing to play";
                }
            }
            if (rulerBounds().contains (p))
            {
                const int b = (int) std::floor (res.map.toBar (mapTime (p.x)));
                for (size_t i = 0; i < res.map.sections.size(); ++i)
                    if (b >= res.map.sections[i].startBar && b < res.map.sections[i].endBar) { sec = (int) i; h = "Click to play from " + juce::String (res.map.sections[i].label) + " (bar " + juce::String (res.map.sections[i].startBar + 1) + ")"; }
            }
        }
        if (h != hint || sec != hoverSection) { hint = h; hoverSection = sec; repaint(); }
        setMouseCursor (sec >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || proc.status() != RewiredProcessor::Status::ready) return;
        if (proc.mapView())
        {
            if (! dragAll.contains (e.getMouseDownPosition())) return;
            juce::String err; const auto f = proc.exportMap (err);
            if (! f.existsAsFile()) { note = err; repaint(); return; }
            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
            return;
        }
        int part = -2, sub = -1;
        if (dragAll.contains (e.getMouseDownPosition())) part = -1;
        for (auto& ln : lines) if (ln.chip.contains (e.getMouseDownPosition())) { part = ln.part; sub = ln.sub; }
        if (part == -2) return;
        juce::String err;
        const auto f = part < 0 ? proc.exportMidi (true, err) : sub >= 0 ? proc.exportSub (part, sub, err) : proc.exportPart (part, err);
        if (! f.existsAsFile()) { note = err; repaint(); return; }
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }

private:
    struct Line { int part = 0, sub = -1; juce::Rectangle<int> row, lane, chip; };
    void layoutRows()
    {
        lines.clear();
        const auto& r = res;
        if (expanded >= 0 && r.subs[(size_t) expanded].empty()) expanded = -1;
        for (int i = 0; i < rw::kNumParts; ++i)
        {
            lines.push_back ({ i, -1, {}, {}, {} });
            if (i == expanded) for (int k = 0; k < (int) r.subs[(size_t) i].size(); ++k) lines.push_back ({ i, k, {}, {}, {} });
        }
        auto b = body;
        const int rh = b.getHeight() / (int) lines.size();
        for (auto& ln : lines)
        {
            ln.row = b.removeFromTop (rh).reduced (0, 1);
            if (ln.sub >= 0) ln.row = ln.row.withTrimmedLeft (28);
            ln.chip = ln.row.withLeft (ln.row.getRight() - 112).reduced (3, ln.sub >= 0 ? 2 : 3);
            ln.lane = ln.row.withLeft (body.getX() + 104).withRight (ln.row.getRight() - 160).reduced (2, 3);
            if (ln.sub < 0) toggles[(size_t) ln.part].setBounds (ln.row.withWidth (24).withSizeKeepingCentre (22, 20).translated (4, 0));
        }
    }
    void chip (juce::Graphics& g, juce::Rectangle<int> r, bool live, const juce::String& text)
    {
        g.setColour (live ? cPri.withAlpha (0.9f) : cLine);
        juce::Path p; p.addRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f);
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setFont (uiFont (10.5f, true)); g.setColour (live ? cPri : cDim.withAlpha (0.6f));
        g.drawText (text, r, juce::Justification::centred);
    }
    void choose (bool stems)
    {
        chooser = std::make_unique<juce::FileChooser> (stems ? "Load stems (bass / drums / vocals or lead / chords or keys)" : "Load a song",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), rw::io::kExtensions);
        juce::Component::SafePointer<RewiredDisplay> safe (this);
        hostkeys::rememberForDialog (*this);
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        if (stems) flags |= juce::FileBrowserComponent::canSelectMultipleItems;
        chooser->launchAsync (flags, [safe, stems] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                juce::Timer::callAfterDelay (60, [sp = juce::Component::SafePointer<juce::Component> (safe.getComponent())] { if (sp != nullptr) hostkeys::restoreAfterDialog (*sp); });
            if (safe == nullptr || fc.getResults().isEmpty()) return;
            juce::StringArray f; for (auto& r : fc.getResults()) f.add (r.getFullPathName());
            safe->dropOn (stems ? (int) kBtnStems : (int) kBtnSong, f);
        });
    }
    void doExport (bool selected)
    {
        juce::String err; auto f = proc.exportMidi (selected, err);
        note = err.isNotEmpty() ? err : "Saved " + f.getFileName() + " in Documents/CMPLXTY Rewired/" + f.getParentDirectory().getFileName();
        if (f.existsAsFile()) f.revealToUser();
        repaint();
    }
    void doExportMap()
    {
        juce::String err; auto f = proc.exportMap (err);
        note = err.isNotEmpty() ? err : "Saved " + f.getFileName() + " in Documents/CMPLXTY Rewired/" + f.getParentDirectory().getFileName();
        if (f.existsAsFile()) f.revealToUser();
        repaint();
    }

    // ---- MAP view: a section ruler, then one lane per instrument with a block wherever it plays (greys by group; the UI is monochrome)
    static juce::Colour groupShade (int group)
    {
        static const juce::uint32 c[] = { 0xffededed, 0xffcfcfcf, 0xffb2b2b2, 0xff979797, 0xff7d7d7d, 0xff626262 };
        return juce::Colour (c[juce::jlimit (0, rw::kNumMapGroups - 1, group)]);
    }
    static juce::Colour sectionShade (int type)
    {
        static const juce::uint32 c[] = { 0xff222222, 0xff2c2c2c, 0xff444444, 0xff1a1a1a, 0xff222222 };
        return juce::Colour (c[juce::jlimit (0, rw::kNumSecTypes - 1, type)]);
    }
    void layoutMapRows (const rw::SongMap& m)
    {
        mapRows.clear();
        if (! m.ok) return;
        const int avail = body.getHeight() - 22;
        // too many lanes for the height: hide instrument sub-lanes first, then the drum pieces (the file still has them all)
        std::vector<int> show;
        for (int pass = 0; pass < 3; ++pass)
        {
            show.clear();
            for (int i = 0; i < (int) m.lanes.size(); ++i)
            {
                const auto& L = m.lanes[(size_t) i];
                const bool sub = L.child && L.group != rw::kGrpDrums, piece = L.child && L.group == rw::kGrpDrums;
                if ((pass >= 1 && sub) || (pass >= 2 && piece)) continue;
                show.push_back (i);
            }
            if ((int) show.size() * 11 <= avail) break;
        }
        hiddenLanes = (int) m.lanes.size() - (int) show.size();
        const int rh = juce::jlimit (10, 22, avail / std::max (1, (int) show.size()));
        auto b = body.withTrimmedTop (22);
        for (int i : show)
        {
            MapRow r; r.lane = i; r.name = juce::String (m.lanes[(size_t) i].name);
            r.row = b.removeFromTop (rh).reduced (0, 1);
            const int sz = juce::jlimit (8, 12, r.row.getHeight() - 1), cy = r.row.getCentreY();
            r.toggle = juce::Rectangle<int> (r.row.getX() + 5, cy - sz / 2, sz, sz);
            r.solo = juce::Rectangle<int> (r.row.getX() + 21, cy - sz / 2, sz, sz);
            r.strip = r.row.withLeft (stripLeft()).withRight (stripRight()).reduced (0, 1);
            mapRows.push_back (r);
        }
    }
    int stripLeft() const  { return body.getX() + 136; }
    int stripRight() const { return body.getRight() - 56; }
    double mapLength() const { return std::max (res.seconds, res.map.ok ? res.map.barTime (res.map.bars) : 0.0); }
    float mapX (double t) const { return (float) stripLeft() + (float) (juce::jlimit (0.0, 1.0, t / std::max (1e-6, mapLength()))) * (float) (stripRight() - stripLeft()); }
    double mapTime (int x) const { return juce::jlimit (0.0, mapLength(), (double) (x - stripLeft()) / (double) std::max (1, stripRight() - stripLeft()) * mapLength()); }
    juce::Rectangle<int> rulerBounds() const { return body.withHeight (20).withLeft (stripLeft()).withRight (stripRight()); }
    double notesLength() const
    {
        double len = std::max (1.0, res.seconds);
        for (auto& p : res.parts) for (auto& n : p) len = std::max (len, n.end);
        return len;
    }
    bool showHead() const { return res.ok && (proc.playing() || proc.playPosition() > 0.0); }

    // a small square switch with a letter: filled = on
    static void letterBox (juce::Graphics& g, juce::Rectangle<int> r, const char* letter, bool on, bool live)
    {
        const auto f = r.toFloat().reduced (0.5f);
        if (on) { g.setColour (cPri); g.fillRoundedRectangle (f, 2.0f); }
        else { g.setColour (live ? cSec.withAlpha (0.8f) : cLine.brighter (0.2f)); g.drawRoundedRectangle (f, 2.0f, 1.0f); }
        g.setColour (on ? cBg : (live ? cSec : cDim.withAlpha (0.5f)));
        g.setFont (uiFont (r.getHeight() >= 11 ? 8.5f : 7.5f, true));
        g.drawText (letter, r, juce::Justification::centred);
    }

    void paintMap (juce::Graphics& g, const rw::Result& r, bool ready)
    {
        const auto& m = r.map;
        if (! ready)
        {
            g.setColour (cLane); g.fillRect (body);
            g.setColour (cLine); g.drawRect (body);
            const bool busy = proc.status() == RewiredProcessor::Status::working;
            g.setFont (uiFont (12.0f, true)); g.setColour (cSec);
            g.drawText (busy ? "MAPPING THE SONG..." : "SONG MAP", body.withTrimmedBottom (body.getHeight() / 2 + 4), juce::Justification::centredBottom);
            g.setFont (uiFont (11.0f)); g.setColour (cDim);
            const juce::String text = proc.hasAnyInput()
                ? "Press PROCESS. MAP finds the tempo, the bars and the sections (intro, build, drop, break, outro), and draws a lane\n"
                  "for every instrument with a block wherever it plays. PLAY listens along; drag the map into FL Studio as one MIDI file."
                : "Drop a reference song here (or your stems), then press PROCESS. MAP finds the tempo, the bars and the sections, and draws\n"
                  "a lane for every instrument with a block wherever it plays. Drag it into FL Studio as one MIDI file.";
            g.drawFittedText (text, body.withTrimmedTop (body.getHeight() / 2 + 4).reduced (20, 0), juce::Justification::centredTop, 3);
            return;
        }
        if (mapRows.empty() || mapRowsFor != proc.contentVersion()) { layoutMapRows (m); mapRowsFor = proc.contentVersion(); }
        const auto soloName = proc.solo();
        const bool anySolo = soloName.isNotEmpty();
        // ruler: column heads on the left, the sections (click one to play from there)
        const auto rulerRow = body.withHeight (20);
        const auto ruler = rulerBounds();
        g.setColour (cLane); g.fillRect (rulerRow);
        g.setFont (uiFont (9.5f, true)); g.setColour (cSec);
        g.drawText (juce::String (m.bars) + " BARS", rulerRow.getX() + 6, rulerRow.getY() + 2, stripLeft() - rulerRow.getX() - 10, 16, juce::Justification::centredLeft);
        if (hiddenLanes > 0)
        { g.setFont (uiFont (9.0f)); g.setColour (cDim); g.drawText ("+" + juce::String (hiddenLanes) + " in .mid", rulerRow.getX() + 6, rulerRow.getY() + 2, stripLeft() - rulerRow.getX() - 10, 16, juce::Justification::centredRight); }
        for (size_t i = 0; i < m.sections.size(); ++i)
        {
            const auto& sc = m.sections[i];
            const float a = mapX (m.barTime (sc.startBar)), z = mapX (m.barTime (sc.endBar));
            juce::Rectangle<float> rr (a, (float) ruler.getY() + 1, std::max (1.0f, z - a - 1.0f), (float) ruler.getHeight() - 2);
            const bool hot = hoverSection == (int) i;
            g.setColour (sectionShade (sc.type).brighter (hot ? 0.35f : 0.0f)); g.fillRect (rr);
            g.setColour (cPri.withAlpha (hot ? 0.8f : 0.35f)); g.drawRect (rr, 1.0f);
            // the name, and the bar it starts on (dimmer, right) when there is room for both
            auto tr = rr.toNearestInt().reduced (3, 0);
            const juce::String barNo (sc.startBar + 1);
            g.setFont (uiFont (9.0f));
            const int bw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), barNo) + 4;
            g.setFont (uiFont (9.5f, sc.type == rw::kSecDrop));
            const juce::String lab = juce::String (sc.label).toUpperCase();
            const bool room = tr.getWidth() > juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), lab) + bw + 6;
            g.setColour (sc.type == rw::kSecDrop || hot ? cPri : cSec);
            g.drawFittedText (lab, room ? tr.withTrimmedRight (bw) : tr, juce::Justification::centredLeft, 1, 0.7f);
            if (room) { g.setFont (uiFont (9.0f)); g.setColour (cDim); g.drawText (barNo, tr.removeFromRight (bw), juce::Justification::centredRight); }
        }
        // lanes
        for (auto& mr : mapRows)
        {
            const auto& L = m.lanes[(size_t) mr.lane];
            const bool real = proc.mapMidi (mr.name), soloed = anySolo && mr.name == soloName, dim = anySolo && ! soloed;
            const int soloSrc = proc.laneSoloSource (L, r);
            g.setColour (soloed ? cLane.brighter (0.12f) : L.child ? cBg.brighter (0.03f) : cLane); g.fillRect (mr.row);
            g.setColour (soloed ? cPri.withAlpha (0.6f) : cLine); g.drawRect (mr.row);
            letterBox (g, mr.toggle, "M", real, true);
            letterBox (g, mr.solo, "S", soloed, soloSrc != 0);
            g.setColour ((L.child ? cSec : cPri).withAlpha (dim ? 0.45f : 1.0f)); g.setFont (uiFont (L.child ? 9.5f : 10.5f, ! L.child));
            const juce::String nm (L.name);
            const int nx = mr.row.getX() + (L.child ? 46 : 38);
            g.drawFittedText (L.child ? (nm.contains (" - ") ? nm.fromFirstOccurrenceOf (" - ", false, false) : nm) : nm.toUpperCase(),
                              nx, mr.row.getY(), stripLeft() - nx - 4, mr.row.getHeight(), juce::Justification::centredLeft, 1, 0.75f);
            const auto st = mr.strip;
            g.setColour (cLine.brighter (0.1f)); g.drawRect (st);
            const auto shade = groupShade (L.group).withMultipliedAlpha (dim ? 0.35f : 1.0f);
            for (auto& b : L.blocks)
            {
                const float a = mapX (b.start), z = mapX (b.end);
                juce::Rectangle<float> rr (a, (float) st.getY() + 1.5f, std::max (1.5f, z - a), (float) st.getHeight() - 3.0f);
                g.setColour (shade.withMultipliedAlpha (real ? 0.45f : (L.child ? 0.6f : 0.85f))); g.fillRect (rr);
                if (L.group == rw::kGrpVocals) { g.setColour (cPri.withAlpha (dim ? 0.3f : 0.9f)); g.drawRect (rr, 1.0f); }
            }
            if (real)   // the notes that go into the file instead of the blocks
            {
                std::vector<const std::vector<rw::Note>*> src;
                if (L.part >= 0) src.push_back (L.sub >= 0 && (size_t) L.sub < r.subs[(size_t) L.part].size() ? &r.subs[(size_t) L.part][(size_t) L.sub].notes : &r.parts[(size_t) L.part]);
                else for (int p : L.midiParts) src.push_back (&r.parts[(size_t) p]);
                // a mini piano roll: pitch sets the height inside the lane (drum hits are short full-height ticks)
                int lo = 127, hi = 0; for (auto* v : src) for (auto& n : *v) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                const float top = (float) st.getY() + 2.5f, span = std::max (1.0f, (float) st.getHeight() - 6.0f);
                g.setColour (cPri.withAlpha (dim ? 0.4f : 1.0f));
                for (auto* v : src) for (auto& n : *v)
                {
                    const float a = mapX (n.start);
                    if (L.drumNote >= 0) { g.fillRect (a, top, 1.0f, span + 1.0f); continue; }
                    const float w = std::max (1.0f, mapX (n.end) - a), y = top + span * (hi > lo ? (float) (hi - n.pitch) / (float) (hi - lo) : 0.5f);
                    g.fillRect (a, y - 0.5f, w, 1.5f);
                }
            }
            int on = 0; for (auto x : L.barOn) on += x;
            g.setColour (cDim); g.setFont (uiFont (9.5f));
            g.drawText (L.blocks.empty() ? juce::String ("-") : juce::String (on) + (on == 1 ? " bar" : " bars"), st.getRight() + 2, mr.row.getY(), 52, mr.row.getHeight(), juce::Justification::centredRight);
        }
        if (! mapRows.empty())
        {
            // section lines across the lanes
            g.setColour (cPri.withAlpha (0.18f));
            const float y0 = (float) mapRows.front().row.getY(), y1 = (float) mapRows.back().row.getBottom();
            for (auto& sc : m.sections) { const float x = mapX (m.barTime (sc.startBar)); g.drawLine (x, y0, x, y1, 1.0f); }
            // playhead over the ruler and every lane
            if (showHead())
            {
                const float x = mapX (proc.playPosition());
                g.setColour (cPri); g.fillRect (x - 0.75f, (float) ruler.getY(), 1.5f, y1 - (float) ruler.getY());
                juce::Path tri; tri.addTriangle (x - 4.0f, (float) ruler.getY(), x + 4.0f, (float) ruler.getY(), x, (float) ruler.getY() + 5.0f);
                g.fillPath (tri);
            }
        }
    }

    void refresh()
    {
        const auto st = proc.status();
        const bool busy = st == RewiredProcessor::Status::working, ready = st == RewiredProcessor::Status::ready;
        res = ready ? proc.result() : rw::Result();
        reprocess = ready && proc.detectionChanged();
        process.setButtonText (busy ? "CANCEL" : reprocess ? "RE-PROCESS" : "PROCESS");
        process.setEnabled (busy || proc.hasAnyInput());
        process.setColour (juce::TextButton::buttonColourId, reprocess ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff1c1c1c));
        if (reprocess && note.isEmpty()) note = "Detection settings changed - press RE-PROCESS to apply them (tempo and quantize apply at once)";
        else if (! reprocess && note.startsWith ("Detection settings changed")) note.clear();
        wasPlaying = proc.playing();
        playBtn.on = wasPlaying; playBtn.setEnabled (proc.canPlay() && ! busy); playBtn.repaint();
        const bool autoBpm = proc.bpmIsAuto();
        bpmAuto.setToggleState (autoBpm, juce::dontSendNotification);
        for (auto* c : { (juce::Component*) &bpmDown, (juce::Component*) &bpmUp, (juce::Component*) &bpmHalf, (juce::Component*) &bpmDouble, (juce::Component*) &bpmBox })
            c->setEnabled (! busy);
        bpmAuto.setEnabled (! busy);
        bpmBox.repaint();
        for (auto* b : { &loadSong, &loadStems, &clear }) b->setEnabled (! busy);
        exportAll.setEnabled (ready); exportSel.setEnabled (ready);
        const bool mapOn = proc.mapView();
        notesTab.setToggleState (! mapOn, juce::dontSendNotification); mapTab.setToggleState (mapOn, juce::dontSendNotification);
        exportAll.setButtonText (mapOn ? "EXPORT MAP" : "EXPORT ALL");
        exportSel.setVisible (! mapOn);
        for (int i = 0; i < rw::kNumParts; ++i) toggles[(size_t) i].setToggleState (proc.partOn (i), juce::dontSendNotification);
        for (auto& t : toggles) t.setVisible (! mapOn);
        mapRows.clear(); mapRowsFor = -1;
        lastVersion = proc.contentVersion();
        layoutRows();
        repaint();
    }
    void timerCallback() override
    {
        if (proc.contentVersion() != lastVersion) { refresh(); return; }
        if (proc.playing() != wasPlaying) { refresh(); return; }   // the preview ran to the end / the DAW started
        if (++slow % 6 == 0 && (proc.status() == RewiredProcessor::Status::ready && proc.detectionChanged()) != reprocess) { refresh(); return; }
        if (proc.status() == RewiredProcessor::Status::working) repaint();
        else if (proc.playing() || proc.previewLoading()) { repaint (body.expanded (2)); repaint (timeArea); }
    }

    RewiredProcessor& proc;
    juce::TextButton loadSong { "LOAD SONG..." }, loadStems { "LOAD STEMS..." }, clear { "CLEAR" }, process { "PROCESS" },
                     exportAll { "EXPORT ALL" }, exportSel { "EXPORT SELECTED" }, notesTab { "NOTES" }, mapTab { "MAP" };
    struct MapRow { int lane = 0; juce::String name; juce::Rectangle<int> row, toggle, solo, strip; };
    std::vector<MapRow> mapRows;
    PlayBtn playBtn;
    SmallBtn bpmDown { "-" }, bpmUp { "+" }, bpmHalf { "1/2" }, bpmDouble { "x2" }, bpmAuto { "AUTO" };
    BpmBox bpmBox;
    bool typingHint = false;
    juce::Rectangle<int> timeArea, bpmLabel, msgArea;
    rw::Result res;                      // the result as of the last refresh (paint never copies it)
    juce::String hint;
    int hoverSection = -1, hiddenLanes = 0, slow = 0;
    bool wasPlaying = false, reprocess = false;
    int mapRowsFor = -1;
    std::array<Btn, rw::kNumParts> toggles;
    std::vector<Line> lines;
    juce::Rectangle<int> boxes[RewiredProcessor::kNumSlots], body, dragAll, status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note;
    int lastVersion = -1, hover = kNone, expanded = -1;
    bool dragging = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (RewiredProcessor& p, Panel&) { return std::make_unique<RewiredDisplay> (p); }
// test / screenshot hooks (the display class is private to this file)
void displayDrop (juce::Component* d, int target, const juce::StringArray& files) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) r->dropOn (target, files); }
void displayExpand (juce::Component* d, int part) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) r->expand (part); }
void displayHover (juce::Component* d, int target) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) r->demoHover (target); }
int displayMapRows (juce::Component* d) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) return r->mapRowCount(); return 0; }
// 1.1.0 test hooks: click where a user would (what: "midi" / "solo" / "strip" / "section" / a control name), read the status line, type a tempo
bool displayClick (juce::Component* d, const juce::String& what, int index, double seconds)
{
    auto* r = dynamic_cast<RewiredDisplay*> (d);
    if (r == nullptr) return false;
    juce::Point<int> p;
    if (what == "midi") p = r->mapToggleAt (index); else if (what == "solo") p = r->mapSoloAt (index);
    else if (what == "strip") p = r->mapStripAt (index, seconds); else if (what == "section") p = r->sectionAt (index);
    else if (auto* c = r->control (what)) { if (! c->isEnabled() || ! c->isVisible()) return false; if (auto* b = dynamic_cast<juce::Button*> (c)) { b->triggerClick(); /* async: the caller runs the message loop */ return true; } return false; }
    if (p == juce::Point<int>()) return false;
    r->clickAt (p);
    return true;
}
juce::Component* displayControl (juce::Component* d, const juce::String& n) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) return r->control (n); return nullptr; }
juce::String displayStatus (juce::Component* d) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) return r->statusText(); return {}; }
void displayTypeBpm (juce::Component* d, const juce::String& t, bool commit) { if (auto* r = dynamic_cast<RewiredDisplay*> (d)) r->typeBpm (t, commit); }
} // namespace rwui
