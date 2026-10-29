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

// which stem slot a file name points at ("Bass.wav" -> bass, "drums" -> drums, "vocal" / "lead" -> melody, anything else -> chords)
int guessStemSlot (const juce::String& fileName)
{
    const auto n = fileName.toLowerCase();
    if (n.contains ("bass") || n.contains ("808") || n.contains ("sub")) return RewiredProcessor::kBassStem;
    if (n.contains ("drum") || n.contains ("perc") || n.contains ("kick") || n.contains ("beat")) return RewiredProcessor::kDrumsStem;
    if (n.contains ("vocal") || n.contains ("vox") || n.contains ("lead") || n.contains ("melod")) return RewiredProcessor::kMelodyStem;
    return RewiredProcessor::kChordsStem;
}

namespace
{
const juce::Colour cBg (0xff101010), cLane (0xff161616), cLine (0xff2d2d2d), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);

class RewiredDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
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
        loadSong.onClick  = [this] { choose (false); };
        loadStems.onClick = [this] { choose (true); };
        clear.onClick     = [this] { proc.clearAll(); note.clear(); refresh(); };
        process.onClick   = [this] { if (proc.status() == RewiredProcessor::Status::working) proc.cancelProcess(); else proc.startProcess(); note.clear(); refresh(); };
        exportAll.onClick = [this] { doExport (false); };
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
        inputsLine = getLocalBounds().reduced (10, 0).withTrimmedTop (38).removeFromTop (18);
        auto body = getLocalBounds().reduced (10, 0).withTrimmedTop (58).withTrimmedBottom (28);
        const int rh = body.getHeight() / rw::kNumParts;
        for (int i = 0; i < rw::kNumParts; ++i)
        {
            rows[i] = body.removeFromTop (rh).reduced (0, 1);
            toggles[(size_t) i].setBounds (rows[i].withWidth (24).withSizeKeepingCentre (22, 20).translated (4, 0));
            chips[i] = rows[i].withLeft (rows[i].getRight() - 112).reduced (3, 3);
            lanes[i] = rows[i].withTrimmedLeft (104).withTrimmedRight (160).reduced (2, 3);
        }
        status = getLocalBounds().reduced (10, 0).removeFromBottom (26);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        const bool ready = proc.status() == RewiredProcessor::Status::ready;
        const auto res = ready ? proc.result() : rw::Result();
        chip (g, dragAll, ready, ready ? "DRAG ALL (.MID)" : "DRAG ALL: -");

        // inputs
        g.setFont (uiFont (11.0f)); g.setColour (cSec);
        juce::String in;
        if (proc.songMode()) in = "SONG  " + proc.input (RewiredProcessor::kSong).getFileName() + "   (split into stems automatically)";
        else if (proc.hasAnyInput())
            for (int s = 1; s < RewiredProcessor::kNumSlots; ++s)
                in << juce::String (RewiredProcessor::slotName (s)).toUpperCase() << "  " << (proc.input (s) == juce::File() ? juce::String ("-") : proc.input (s).getFileName()) << "     ";
        else { in = "Drop one song, or several stems (named bass / drums / vocals / keys ...), anywhere here"; g.setColour (cDim); }
        g.drawFittedText (in, inputsLine, juce::Justification::centredLeft, 1);

        double len = 1.0;
        for (auto& p : res.parts) for (auto& n : p) len = std::max (len, n.end);
        for (int i = 0; i < rw::kNumParts; ++i)
        {
            const auto r = rows[i];
            g.setColour (cLane); g.fillRect (r);
            g.setColour (cLine); g.drawRect (r);
            const bool on = proc.partOn (i);
            const auto& notes = res.parts[(size_t) i];
            g.setColour (on ? cPri : cDim); g.setFont (uiFont (11.0f, true));
            g.drawText (juce::String (rw::partName (i)).toUpperCase(), r.getX() + 32, r.getY(), 70, r.getHeight(), juce::Justification::centredLeft);
            const auto lane = lanes[i];
            g.setColour (cLine.brighter (0.1f)); g.drawRect (lane);
            if (! notes.empty())
            {
                int lo = 127, hi = 0; for (auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
                const bool drum = rw::isDrumPart (i);
                g.setColour (on ? cPri.withAlpha (0.85f) : cDim.withAlpha (0.45f));
                for (auto& n : notes)
                {
                    const float x0 = lane.getX() + (float) (n.start / len) * lane.getWidth();
                    const float w = drum ? 1.0f : std::max (1.0f, (float) ((n.end - n.start) / len) * lane.getWidth());
                    const float y = drum ? (float) lane.getY() + 2.0f
                                         : lane.getBottom() - 3.0f - (hi > lo ? (float) (n.pitch - lo) / (float) (hi - lo) : 0.5f) * (lane.getHeight() - 6.0f);
                    g.fillRect (x0, drum ? y : y - 1.0f, w, drum ? lane.getHeight() - 4.0f : 2.0f);
                }
            }
            g.setColour (cDim); g.setFont (uiFont (10.5f));
            g.drawText (ready ? juce::String ((int) notes.size()) + " notes" : juce::String ("-"), lane.getRight() + 4, r.getY(), 44, r.getHeight(), juce::Justification::centredRight);
            chip (g, chips[i], ready && ! notes.empty(), ready && ! notes.empty() ? "DRAG MIDI" : "-");
        }
        // status + progress
        g.setFont (uiFont (11.5f)); g.setColour (cSec);
        const bool busy = proc.status() == RewiredProcessor::Status::working;
        g.drawFittedText (note.isNotEmpty() ? note : proc.lastMessage(), status.withTrimmedRight (busy ? 230 : 0), juce::Justification::centredLeft, 1);
        if (busy)
        {
            auto bar = status.removeFromRight (220).withSizeKeepingCentre (220, 6);
            g.setColour (cLine); g.fillRect (bar);
            g.setColour (cPri); g.fillRect (bar.withWidth ((int) (bar.getWidth() * juce::jlimit (0.0f, 1.0f, proc.progress()))));
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int) override { load (files, files.size() > 1); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || proc.status() != RewiredProcessor::Status::ready) return;
        int part = -2;
        if (dragAll.contains (e.getMouseDownPosition())) part = -1;
        for (int i = 0; i < rw::kNumParts; ++i) if (chips[i].contains (e.getMouseDownPosition())) part = i;
        if (part == -2) return;
        juce::String err;
        const auto f = part < 0 ? proc.exportMidi (true, err) : proc.exportPart (part, err);
        if (! f.existsAsFile()) { note = err; repaint(); return; }
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }

private:
    void chip (juce::Graphics& g, juce::Rectangle<int> r, bool live, const juce::String& text)
    {
        g.setColour (live ? cPri.withAlpha (0.9f) : cLine);
        juce::Path p; p.addRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f);
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setFont (uiFont (10.5f, true)); g.setColour (live ? cPri : cDim.withAlpha (0.6f));
        g.drawText (text, r, juce::Justification::centred);
    }
    void load (const juce::StringArray& files, bool asStems)
    {
        note.clear();
        if (! asStems) { note = proc.setInput (RewiredProcessor::kSong, juce::File (files[0])); refresh(); return; }
        for (auto& f : files)
        {
            int s = guessStemSlot (juce::File (f).getFileName());
            if (proc.input (s) != juce::File())   // name clash: first free stem slot
                for (int k = 1; k < RewiredProcessor::kNumSlots; ++k) if (proc.input (k) == juce::File()) { s = k; break; }
            const auto e = proc.setInput (s, juce::File (f));
            if (e.isNotEmpty()) note = e;
        }
        refresh();
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
            if (stems) proc_clearSong (*safe);
            juce::StringArray f; for (auto& r : fc.getResults()) f.add (r.getFullPathName());
            safe->load (f, stems);
        });
    }
    static void proc_clearSong (RewiredDisplay& d) { d.proc.clearInput (RewiredProcessor::kSong); }
    void doExport (bool selected)
    {
        juce::String err; auto f = proc.exportMidi (selected, err);
        note = err.isNotEmpty() ? err : "Saved " + f.getFileName() + " in Documents/CMPLXTY Rewired/" + f.getParentDirectory().getFileName();
        if (f.existsAsFile()) f.revealToUser();
        repaint();
    }
    void refresh()
    {
        const auto st = proc.status();
        const bool busy = st == RewiredProcessor::Status::working, ready = st == RewiredProcessor::Status::ready;
        process.setButtonText (busy ? "CANCEL" : "PROCESS");
        process.setEnabled (busy || proc.hasAnyInput());
        for (auto* b : { &loadSong, &loadStems, &clear }) b->setEnabled (! busy);
        exportAll.setEnabled (ready); exportSel.setEnabled (ready);
        for (int i = 0; i < rw::kNumParts; ++i) toggles[(size_t) i].setToggleState (proc.partOn (i), juce::dontSendNotification);
        lastVersion = proc.contentVersion();
        repaint();
    }
    void timerCallback() override
    {
        if (proc.contentVersion() != lastVersion) { refresh(); return; }
        if (proc.status() == RewiredProcessor::Status::working) repaint();
    }

    RewiredProcessor& proc;
    juce::TextButton loadSong { "LOAD SONG..." }, loadStems { "LOAD STEMS..." }, clear { "CLEAR" }, process { "PROCESS" },
                     exportAll { "EXPORT ALL" }, exportSel { "EXPORT SELECTED" };
    std::array<Btn, rw::kNumParts> toggles;
    juce::Rectangle<int> rows[rw::kNumParts], lanes[rw::kNumParts], chips[rw::kNumParts], dragAll, inputsLine, status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note;
    int lastVersion = -1;
    bool dragging = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (RewiredProcessor& p, Panel&) { return std::make_unique<RewiredDisplay> (p); }
} // namespace rwui
