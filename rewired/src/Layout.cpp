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
            { "OUTPUT",    nullptr, { 686, 366, 198, 162 }, { { 'k', "bpm", "TEMPO (0=AUTO)" }, { 'c', "quantize", "QUANTIZE" } }, 96 },
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
        notesTab.onClick = [this] { proc.setMapView (false); refresh(); };
        mapTab.onClick   = [this] { proc.setMapView (true); refresh(); };
        loadSong.onClick  = [this] { choose (false); };
        loadStems.onClick = [this] { choose (true); };
        clear.onClick     = [this] { proc.clearAll(); note.clear(); expanded = -1; refresh(); };
        process.onClick   = [this] { if (proc.status() == RewiredProcessor::Status::working) proc.cancelProcess(); else proc.startProcess(); note.clear(); refresh(); };
        exportAll.onClick = [this] { if (proc.mapView()) doExportMap(); else doExport (false); };
        exportSel.onClick = [this] { doExport (true); };
        startTimerHz (20);
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
            auto t = status.withTrimmedTop (5).withTrimmedBottom (3).removeFromRight (124);
            mapTab.setBounds (t.removeFromRight (60)); t.removeFromRight (4); notesTab.setBounds (t.removeFromRight (60));
            status.removeFromRight (132);
        }
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (hover == kAnywhere ? 0.7f : 0.25f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, hover == kAnywhere ? 1.5f : 1.0f);
        const bool ready = proc.status() == RewiredProcessor::Status::ready;
        const auto res = ready ? proc.result() : rw::Result();
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

        double len = 1.0;
        for (auto& p : res.parts) for (auto& n : p) len = std::max (len, n.end);
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
        }
        if (ready && expanded >= 0 && ! mapOn)
        {
            g.setFont (uiFont (9.5f)); g.setColour (cDim);
            g.drawText ("instrument names are a best guess from the sound", body.getX() + 104, body.getBottom() + 1, 400, 11, juce::Justification::centredLeft);
        }
        // status + progress
        g.setFont (uiFont (11.5f)); g.setColour (cSec);
        const bool busy = proc.status() == RewiredProcessor::Status::working;
        g.drawFittedText (note.isNotEmpty() ? note : proc.lastMessage(), status.withTrimmedRight (busy ? 230 : 0).withTrimmedTop (6), juce::Justification::centredLeft, 1);
        if (busy)
        {
            auto bar = status.removeFromRight (220).withSizeKeepingCentre (220, 6);
            g.setColour (cLine); g.fillRect (bar);
            g.setColour (cPri); g.fillRect (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc.progress()))));
        }
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
    void toggleMapMidi (int visibleRow) { if (visibleRow >= 0 && visibleRow < (int) mapRows.size()) proc.setMapMidi (mapRows[(size_t) visibleRow].name, ! proc.mapMidi (mapRows[(size_t) visibleRow].name)); repaint(); }
    int mapRowCount() const { return (int) mapRows.size(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        if (proc.mapView())
        {
            for (auto& mr : mapRows)
                if (mr.toggle.contains (e.getPosition()))
                { proc.setMapMidi (mr.name, ! proc.mapMidi (mr.name)); repaint(); return; }
            return;
        }
        for (auto& ln : lines)
            if (ln.sub < 0 && ln.row.withTrimmedLeft (28).withWidth (76).contains (e.getPosition()) && proc.status() == RewiredProcessor::Status::ready
                && ! proc.result().subs[(size_t) ln.part].empty())
            { expand (ln.part); return; }
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
        const auto r = proc.status() == RewiredProcessor::Status::ready ? proc.result() : rw::Result();
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
            if ((int) show.size() * 12 <= avail) break;
        }
        const int rh = juce::jlimit (10, 22, avail / std::max (1, (int) show.size()));
        auto b = body.withTrimmedTop (22);
        for (int i : show)
        {
            MapRow r; r.lane = i; r.name = juce::String (m.lanes[(size_t) i].name);
            r.row = b.removeFromTop (rh).reduced (0, 1);
            r.toggle = juce::Rectangle<int> (r.row.getX() + 6, r.row.getCentreY() - 5, 10, 10).expanded (3);
            r.strip = r.row.withLeft (body.getX() + 132).withRight (r.row.getRight() - 56).reduced (2, 1);
            mapRows.push_back (r);
        }
    }
    void paintMap (juce::Graphics& g, const rw::Result& res, bool ready)
    {
        const auto& m = res.map;
        if (! ready)
        {
            g.setColour (cLane); g.fillRect (body);
            g.setColour (cLine); g.drawRect (body);
            g.setFont (uiFont (12.0f, true)); g.setColour (cSec);
            g.drawText ("SONG MAP", body.withTrimmedBottom (body.getHeight() / 2 + 4), juce::Justification::centredBottom);
            g.setFont (uiFont (11.0f)); g.setColour (cDim);
            g.drawFittedText ("Load a reference song and press PROCESS. MAP finds the sections (intro, build, drop, break, outro) and the bars,\n"
                              "and draws a lane for every instrument with a block wherever it plays. Drag it into FL Studio as one MIDI file.",
                              body.withTrimmedTop (body.getHeight() / 2 + 4).reduced (20, 0), juce::Justification::centredTop, 3);
            return;
        }
        if (mapRows.empty() || mapRowsFor != proc.contentVersion()) { layoutMapRows (m); mapRowsFor = proc.contentVersion(); }
        const auto stripX = body.getX() + 134, stripR = body.getRight() - 58;
        const double t0 = 0.0, t1 = std::max (res.seconds, m.barTime (m.bars));
        auto xOf = [&] (double t) { return (float) stripX + (float) ((t - t0) / (t1 - t0)) * (float) (stripR - stripX); };
        // ruler
        const auto rulerRow = body.withHeight (20);
        const auto ruler = rulerRow.withLeft (stripX).withRight (stripR);
        g.setColour (cLane); g.fillRect (rulerRow);
        g.setFont (uiFont (9.0f, true)); g.setColour (cDim);
        g.drawText ("REAL MIDI", rulerRow.getX() + 2, rulerRow.getY() + 2, 60, 16, juce::Justification::centredLeft);
        g.drawText (juce::String (m.bpm, m.bpm == std::round (m.bpm) ? 0 : 2) + " BPM / " + juce::String (m.bars) + " BARS", rulerRow.getX() + 50, rulerRow.getY() + 2, 82, 16, juce::Justification::centredRight);
        for (auto& sc : m.sections)
        {
            const float a = xOf (m.barTime (sc.startBar)), z = xOf (m.barTime (sc.endBar));
            juce::Rectangle<float> r (a, (float) ruler.getY() + 1, std::max (1.0f, z - a - 1.0f), (float) ruler.getHeight() - 2);
            g.setColour (sectionShade (sc.type)); g.fillRect (r);
            g.setColour (cPri.withAlpha (0.35f)); g.drawRect (r, 1.0f);
            g.setColour (sc.type == rw::kSecDrop ? cPri : cSec); g.setFont (uiFont (9.5f, sc.type == rw::kSecDrop));
            g.drawFittedText (juce::String (sc.label).toUpperCase() + "  " + juce::String (sc.startBar + 1), r.toNearestInt().reduced (3, 0), juce::Justification::centredLeft, 1, 0.7f);
        }
        // lanes
        for (auto& mr : mapRows)
        {
            const auto& L = m.lanes[(size_t) mr.lane];
            const bool real = proc.mapMidi (mr.name);
            g.setColour (L.child ? cBg.brighter (0.03f) : cLane); g.fillRect (mr.row);
            g.setColour (cLine); g.drawRect (mr.row);
            auto tg = mr.toggle.reduced (3).toFloat();
            g.setColour (real ? cPri : cDim.withAlpha (0.7f));
            if (real) g.fillRoundedRectangle (tg, 2.0f); else g.drawRoundedRectangle (tg, 2.0f, 1.0f);
            g.setColour (L.child ? cSec : cPri); g.setFont (uiFont (L.child ? 9.5f : 10.5f, ! L.child));
            const juce::String nm (L.name);
            g.drawFittedText (L.child ? (nm.contains (" - ") ? nm.fromFirstOccurrenceOf (" - ", false, false) : nm) : nm.toUpperCase(),
                              mr.row.getX() + (L.child ? 34 : 24), mr.row.getY(), 100 - (L.child ? 10 : 0), mr.row.getHeight(), juce::Justification::centredLeft, 1, 0.75f);
            const auto st = mr.strip;
            g.setColour (cLine.brighter (0.1f)); g.drawRect (st);
            const auto shade = groupShade (L.group);
            for (auto& b : L.blocks)
            {
                const float a = xOf (b.start), z = xOf (b.end);
                juce::Rectangle<float> r (a, (float) st.getY() + 1.5f, std::max (1.5f, z - a), (float) st.getHeight() - 3.0f);
                g.setColour (shade.withAlpha (real ? 0.35f : (L.child ? 0.6f : 0.85f))); g.fillRect (r);
                if (L.group == rw::kGrpVocals) { g.setColour (cPri.withAlpha (0.9f)); g.drawRect (r, 1.0f); }
            }
            if (real)   // the notes that go into the file instead of the blocks
            {
                std::vector<const std::vector<rw::Note>*> src;
                if (L.part >= 0) src.push_back (L.sub >= 0 && (size_t) L.sub < res.subs[(size_t) L.part].size() ? &res.subs[(size_t) L.part][(size_t) L.sub].notes : &res.parts[(size_t) L.part]);
                else for (int p : L.midiParts) src.push_back (&res.parts[(size_t) p]);
                g.setColour (cPri);
                for (auto* v : src) for (auto& n : *v)
                {
                    const float a = xOf (n.start), w = L.drumNote >= 0 ? 1.0f : std::max (1.0f, xOf (n.end) - a);
                    g.fillRect (a, (float) st.getCentreY() - 1.0f, w, 2.0f);
                }
            }
            int on = 0; for (auto x : L.barOn) on += x;
            g.setColour (cDim); g.setFont (uiFont (9.5f));
            g.drawText (L.blocks.empty() ? juce::String ("-") : juce::String (on) + (on == 1 ? " bar" : " bars"), st.getRight() + 2, mr.row.getY(), 52, mr.row.getHeight(), juce::Justification::centredRight);
        }
        // section lines across the lanes
        if (! mapRows.empty())
        {
            g.setColour (cPri.withAlpha (0.18f));
            const float y0 = (float) mapRows.front().row.getY(), y1 = (float) mapRows.back().row.getBottom();
            for (auto& sc : m.sections) { const float x = xOf (m.barTime (sc.startBar)); g.drawLine (x, y0, x, y1, 1.0f); }
        }
    }

    void refresh()
    {
        const auto st = proc.status();
        const bool busy = st == RewiredProcessor::Status::working, ready = st == RewiredProcessor::Status::ready;
        process.setButtonText (busy ? "CANCEL" : "PROCESS");
        process.setEnabled (busy || proc.hasAnyInput());
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
        if (proc.status() == RewiredProcessor::Status::working) repaint();
    }

    RewiredProcessor& proc;
    juce::TextButton loadSong { "LOAD SONG..." }, loadStems { "LOAD STEMS..." }, clear { "CLEAR" }, process { "PROCESS" },
                     exportAll { "EXPORT ALL" }, exportSel { "EXPORT SELECTED" }, notesTab { "NOTES" }, mapTab { "MAP" };
    struct MapRow { int lane = 0; juce::String name; juce::Rectangle<int> row, toggle, strip; };
    std::vector<MapRow> mapRows;
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
} // namespace rwui
