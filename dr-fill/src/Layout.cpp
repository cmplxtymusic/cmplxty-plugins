// Dr Fill: control layout + the main display (three drop boxes, the fill view, preview / regenerate / save, drag-out chips).
#include "PluginEditor.h"

namespace drfui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY DR FILL", "DRUM LOOPS IN  /  ONE FILL OUT",
        { 16, 56, 968, 326 },
        {
            { "FILL",   nullptr, { 16, 390, 330, 200 },  { { 'k', "density", "DENSITY" }, { 'k', "complex", "COMPLEXITY" }, { 'k', "ramp", "RAMP" },
                                                         { 'c', "length", "LENGTH" }, { 'c', "style", "STYLE" }, { 'c', "ending", "ENDING" } }, 128 },
            { "FEEL",   nullptr, { 354, 390, 262, 200 }, { { 'k', "swing", "SWING" }, { 'k', "humanize", "HUMANIZE" }, { 'k', "flam", "FLAMS" }, { 'k', "pitch", "PITCH GLIDE" } } },
            { "PARTS",  nullptr, { 624, 390, 136, 200 }, { { 'k', "kick", "KICK" }, { 'k', "hats", "HATS" } } },
            { "OUTPUT", nullptr, { 768, 390, 216, 200 }, { { 'k', "level", "LEVEL" }, { 'k', "bpm", "TEMPO" }, { 'k', "sens", "HIT SENS" } } },
        },
        "density"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLane (0xff161616), cLine (0xff2d2d2d), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);
const char* kLaneTitle[] = { "KICKS", "SNARES", "HATS / CYMBALS" };

class DrFillDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    enum Target { kNone = -1, kAnywhere = 20 };

    DrFillDisplay (DrFillProcessor& p) : proc (p)
    {
        for (auto* b : { &load, &clearAll, &previewBtn, &regen, &seedDown, &seedUp, &save })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
        }
        for (int l = 0; l < drf::kLanes; ++l)
        {
            auto& x = clearLane[(size_t) l];
            addAndMakeVisible (x); x.setWantsKeyboardFocus (false); x.setMouseClickGrabsKeyboardFocus (false);
            x.onClick = [this, l] { proc.clearLoop (l); note.clear(); refresh(); };
        }
        load.onClick       = [this] { choose (kAnywhere); };
        clearAll.onClick   = [this] { proc.clearAll(); note.clear(); refresh(); };
        previewBtn.onClick = [this] { if (proc.isPlaying()) proc.stopPreview(); else proc.preview (DrFillProcessor::kPartFull); };
        regen.onClick      = [this] { proc.regenerate(); note.clear(); };
        seedDown.onClick   = [this] { stepSeed (-1); };
        seedUp.onClick     = [this] { stepSeed (1); };
        save.onClick       = [this] { doSave(); };
        startTimerHz (25);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8).removeFromTop (26);
        load.setBounds (r.removeFromLeft (92)); r.removeFromLeft (6);
        clearAll.setBounds (r.removeFromLeft (64)); r.removeFromLeft (18);
        previewBtn.setBounds (r.removeFromLeft (92)); r.removeFromLeft (6);
        regen.setBounds (r.removeFromLeft (108)); r.removeFromLeft (6);
        seedDown.setBounds (r.removeFromLeft (26)); seedArea = r.removeFromLeft (78); seedUp.setBounds (r.removeFromLeft (26)); r.removeFromLeft (18);
        save.setBounds (r.removeFromLeft (98));
        tempoArea = r.withTrimmedLeft (10);

        auto body = getLocalBounds().reduced (10, 0).withTrimmedTop (42);
        for (int l = 0; l < drf::kLanes; ++l)
        {
            rows[l] = body.removeFromTop (50); body.removeFromTop (4);
            boxes[l] = rows[l].withWidth (190);
            waves[l] = rows[l].withTrimmedLeft (198).reduced (0, 2);
            clearLane[(size_t) l].setBounds (boxes[l].getRight() - 24, boxes[l].getY() + 4, 20, 18);
        }
        body.removeFromTop (2);
        fillView = body.removeFromTop (86);
        body.removeFromTop (6);
        auto c = body.removeFromTop (28);
        for (int p = 0; p < DrFillProcessor::kNumParts; ++p) { chips[p] = c.removeFromLeft (p == 3 ? 150 : 110); c.removeFromLeft (8); }
        status = c.withTrimmedLeft (8);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (hover == kAnywhere ? 0.7f : 0.25f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, hover == kAnywhere ? 1.5f : 1.0f);

        const auto kit = proc.kit();
        const auto fill = proc.fill();
        const auto ren = proc.render();

        // seed + tempo
        g.setFont (uiFont (11.5f, true)); g.setColour (cPri);
        g.drawText ("SEED " + juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("seed")->load())), seedArea, juce::Justification::centred);
        g.setFont (uiFont (11.0f)); g.setColour (cSec);
        const auto t = proc.detectedTempo();
        juce::String tempo = juce::String (proc.fillTempo(), 1) + " BPM (" + proc.tempoSource() + ")";
        if (t.bpm > 0 && proc.tempoSource() != "loops" && proc.tempoSource() != "file name") tempo << "  -  loops: " << juce::String (t.bpm, 1);
        g.drawFittedText (tempo, tempoArea, juce::Justification::centredRight, 1, 0.85f);

        // three drop boxes + loop overviews with the hits found
        for (int l = 0; l < drf::kLanes; ++l)
        {
            const bool hot = hover == l, filled = proc.hasLoop (l);
            const auto b = boxes[l];
            g.setColour (hot ? cPri.withAlpha (0.12f) : cLane); g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (hot ? cPri : (filled ? cSec.withAlpha (0.7f) : cLine.brighter (0.15f)));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.0f, hot ? 1.5f : 1.0f);
            auto tr = b.reduced (8, 5);
            g.setFont (uiFont (11.0f, true)); g.setColour (hot || filled ? cPri : cSec);
            g.drawText (kLaneTitle[l], tr.removeFromTop (16), juce::Justification::centredLeft);
            g.setFont (uiFont (10.5f)); g.setColour (filled ? cSec : cDim.withAlpha (0.8f));
            const auto& lp = (*kit)[(size_t) l];
            const bool analysed = filled && ! lp.empty() && juce::String (lp.name) == proc.loopName (l);
            juce::String what = filled ? proc.loopName (l) : hot ? juce::String ("release to load") : juce::String ("drop a loop or one-shot");
            g.drawFittedText (what, tr.removeFromTop (15), juce::Justification::centredLeft, 1, 0.85f);
            if (filled)
            {
                g.setColour (cDim);
                g.drawText (analysed ? juce::String ((int) lp.hits.size()) + (lp.hits.size() == 1 ? " hit, " : " hits, ") + juce::String (lp.lengthSec, 2) + " s" : juce::String ("slicing..."),
                            tr, juce::Justification::centredLeft);
            }
            clearLane[(size_t) l].setVisible (filled);

            const auto w = waves[l];
            g.setColour (cLane); g.fillRect (w);
            g.setColour (cLine); g.drawRect (w);
            if (analysed && ! lp.overview.empty())
            {
                const float mid = (float) w.getCentreY(), hh = w.getHeight() * 0.45f;
                g.setColour (cSec.withAlpha (0.55f));
                const int np = (int) lp.overview.size();
                for (int x = 0; x < w.getWidth(); ++x)
                {
                    const float v = juce::jmin (1.0f, lp.overview[(size_t) juce::jmin (np - 1, x * np / juce::jmax (1, w.getWidth()))]);
                    g.drawVerticalLine (w.getX() + x, mid - v * hh, mid + v * hh);
                }
                const double len = juce::jmax (1, lp.audio.len());
                for (auto& h : lp.hits)
                {
                    const float x = w.getX() + (float) (h.onset / len) * w.getWidth();
                    g.setColour (h.kind == 1 && l != drf::kHats ? cDim : cPri);
                    g.fillRect (x, (float) w.getY() + 1.0f, 1.0f, 6.0f);
                }
            }
            else if (! filled)
            {
                g.setColour (cDim.withAlpha (0.5f)); g.setFont (uiFont (10.5f));
                g.drawText (l == drf::kKick ? "kick loop: the fill uses its kicks under the roll"
                          : l == drf::kSnare ? "snare / clap loop: the main voice of the fill (rolls, flams, toms)"
                          : "hat / cymbal loop: hats in the fill and the open hat / crash at the end", w, juce::Justification::centred);
            }
        }

        // the fill: one row per part, beats grid, events, play head
        auto fv = fillView;
        g.setColour (cLane); g.fillRect (fv);
        g.setColour (cLine); g.drawRect (fv);
        auto head = fv.removeFromLeft (190).reduced (8, 6);
        g.setFont (uiFont (11.0f, true)); g.setColour (cPri);
        g.drawText ("FILL", head.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (uiFont (10.5f)); g.setColour (cSec);
        if (ren->events > 0)
        {
            g.drawText (juce::String (drf::styleName (fill.style)) + ", " + beatsName (fill.lengthBeats), head.removeFromTop (15), juce::Justification::centredLeft);
            g.setColour (cDim);
            g.drawText (juce::String (ren->events) + " hits, peak " + juce::String (juce::Decibels::gainToDecibels (ren->peak, -99.0f), 1) + " dB", head.removeFromTop (15), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (cDim);
            g.drawFittedText (proc.busy() ? juce::String ("building...") : juce::String ("no fill yet: load at least a snare or kick loop"), head.removeFromTop (30), juce::Justification::topLeft, 2);
        }
        auto grid = fv.reduced (8, 6);
        const int beats = juce::jmax (1, fill.lengthBeats);
        const int sub = beats <= 2 ? 4 : beats <= 4 ? 4 : 2;
        for (int i = 0; i <= beats * sub; ++i)
        {
            const float x = grid.getX() + (float) i / (beats * sub) * grid.getWidth();
            g.setColour (i % sub == 0 ? cLine.brighter (0.35f) : cLine);
            g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        }
        const float rh = grid.getHeight() / 3.0f;
        if (ren->events > 0)
            for (auto& e : fill.events)
            {
                if (e.hit < 0) continue;
                const float x = grid.getX() + (float) (e.beat / beats) * grid.getWidth();
                const float y = grid.getY() + rh * (float) (e.lane == drf::kHats ? 0 : e.lane == drf::kSnare ? 1 : 2);
                const float h = juce::jmax (2.0f, (rh - 4.0f) * juce::jlimit (0.1f, 1.0f, e.vel));
                g.setColour (e.grace ? cDim : cPri.withAlpha (0.9f));
                g.fillRect (x, y + rh - 2.0f - h, e.ring ? 5.0f : 3.0f, h);
            }
        g.setFont (uiFont (9.5f)); g.setColour (cDim.withAlpha (0.7f));
        g.drawText ("HAT", grid.getRight() - 40, grid.getY(), 38, (int) rh, juce::Justification::centredRight);
        g.drawText ("SNR", grid.getRight() - 40, grid.getY() + (int) rh, 38, (int) rh, juce::Justification::centredRight);
        g.drawText ("KICK", grid.getRight() - 40, grid.getY() + (int) (2 * rh), 38, (int) rh, juce::Justification::centredRight);
        const float pp = proc.playPosition();
        if (pp >= 0.0f) { g.setColour (cPri); g.fillRect (grid.getX() + pp * grid.getWidth(), (float) grid.getY(), 1.5f, (float) grid.getHeight()); }

        // drag chips (click = preview that part)
        for (int p = 0; p < DrFillProcessor::kNumParts; ++p)
        {
            const bool live = ren->events > 0 && partHasHits (*ren, p);
            chip (g, chips[p], live, live ? (p == 3 ? juce::String ("DRAG FULL FILL") : "DRAG " + juce::String (DrFillProcessor::partName (p)).toUpperCase()) : juce::String ("-"));
        }
        g.setFont (uiFont (11.0f)); g.setColour (cSec);
        g.drawFittedText (note.isNotEmpty() ? note : proc.lastMessage(), status, juce::Justification::centredLeft, 1, 0.85f);
    }

    // ---- file drops
    static bool audioFile (const juce::String& f) { return juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (audioFile (f)) return true; return false; }
    int targetAt (juce::Point<int> p) const { for (int l = 0; l < drf::kLanes; ++l) if (rows[l].contains (p)) return l; return kAnywhere; }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { setHover (targetAt ({ x, y })); }
    void fileDragMove (const juce::StringArray&, int x, int y) override  { setHover (targetAt ({ x, y })); }
    void fileDragExit (const juce::StringArray&) override                { setHover (kNone); }
    void filesDropped (const juce::StringArray& files, int x, int y) override { setHover (kNone); dropOn (targetAt ({ x, y }), files); }

    // also the test hook: what a drop of `files` on `target` (0..2 a lane box, 20 anywhere else) does
    void dropOn (int target, const juce::StringArray& dropped)
    {
        juce::StringArray files; for (auto& f : dropped) if (audioFile (f)) files.add (f);
        const int skipped = dropped.size() - files.size();
        if (files.isEmpty()) { note = "Dr Fill reads WAV, AIFF, FLAC, OGG and MP3"; repaint(); return; }
        note.clear();
        std::vector<int> lanes;
        if (target >= 0 && target < drf::kLanes && files.size() == 1) lanes = { target };   // one file on a box: that box, whatever its name
        else
        {
            std::array<bool, 3> occ {}; for (int l = 0; l < 3; ++l) occ[(size_t) l] = proc.hasLoop (l);
            lanes = DrFillProcessor::assignLanes (files, occ, target >= 0 && target < drf::kLanes ? target : -1);
        }
        int extra = 0; juce::StringArray loaded;
        for (int i = 0; i < files.size(); ++i)
        {
            if (lanes[(size_t) i] < 0) { ++extra; continue; }
            const auto e = proc.loadLoop (lanes[(size_t) i], juce::File (files[i]));
            if (e.isNotEmpty()) note = e; else loaded.add (juce::String (drf::laneName (lanes[(size_t) i])) + ": " + juce::File (files[i]).getFileNameWithoutExtension());
        }
        if (note.isEmpty() && loaded.size() > 1) note = loaded.joinIntoString ("   ");
        if (note.isEmpty() && extra > 0) note = juce::String (extra) + (extra == 1 ? " file" : " files") + " left out - there are three boxes";
        if (note.isEmpty() && skipped > 0) note = juce::String (skipped) + " non-audio file" + (skipped == 1 ? "" : "s") + " ignored";
        refresh();
    }
    void demoHover (int target) { hover = target; repaint(); }
    juce::Rectangle<int> chipBounds (int p) const { return chips[p]; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        for (int p = 0; p < DrFillProcessor::kNumParts; ++p) if (chips[p].contains (e.getPosition())) { proc.preview (p); return; }
        for (int l = 0; l < drf::kLanes; ++l) if (boxes[l].contains (e.getPosition()) && ! proc.hasLoop (l)) { choose (l); return; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4) return;
        int part = -1;
        for (int p = 0; p < DrFillProcessor::kNumParts; ++p) if (chips[p].contains (e.getMouseDownPosition())) part = p;
        if (part < 0) return;
        juce::String err;
        const auto f = proc.exportPart (part, err);
        if (! f.existsAsFile()) { note = err; repaint(); return; }
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }

private:
    static juce::String beatsName (int b) { return b <= 1 ? "1 beat" : b == 2 ? "2 beats" : b == 4 ? "1 bar" : "2 bars"; }
    static bool partHasHits (const drf::Render& r, int p)
    {
        if (p == DrFillProcessor::kPartFull) return r.events > 0;
        for (auto v : r.L[(size_t) p]) if (v != 0.0f) return true;
        return false;
    }
    void stepSeed (int d)
    {
        auto* p = proc.apvts.getParameter ("seed");
        const int s = juce::jlimit (1, 9999, juce::roundToInt (proc.apvts.getRawParameterValue ("seed")->load()) + d);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) s)); p->endChangeGesture();
    }
    void chip (juce::Graphics& g, juce::Rectangle<int> r, bool live, const juce::String& text)
    {
        g.setColour (live ? cPri.withAlpha (0.9f) : cLine);
        juce::Path p; p.addRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f);
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setFont (uiFont (10.5f, true)); g.setColour (live ? cPri : cDim.withAlpha (0.6f));
        g.drawText (text, r, juce::Justification::centred);
    }
    void choose (int target)
    {
        chooser = std::make_unique<juce::FileChooser> (target == kAnywhere ? "Load drum loops (kick / snare / hat files: sorted by name)" : juce::String ("Load ") + kLaneTitle[target],
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<DrFillDisplay> safe (this);
        hostkeys::rememberForDialog (*this);
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        if (target == kAnywhere) flags |= juce::FileBrowserComponent::canSelectMultipleItems;
        chooser->launchAsync (flags, [safe, target] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                juce::Timer::callAfterDelay (60, [sp = juce::Component::SafePointer<juce::Component> (safe.getComponent())] { if (sp != nullptr) hostkeys::restoreAfterDialog (*sp); });
            if (safe == nullptr || fc.getResults().isEmpty()) return;
            juce::StringArray f; for (auto& r : fc.getResults()) f.add (r.getFullPathName());
            safe->dropOn (target, f);
        });
    }
    void doSave()
    {
        juce::String err; const auto files = proc.exportAll (err);
        if (files.isEmpty()) { note = err; repaint(); return; }
        note = "Saved " + juce::String (files.size()) + " WAVs in Documents/CMPLXTY Dr Fill";
        juce::File (files[0]).revealToUser();
        repaint();
    }
    void refresh()
    {
        lastVersion = proc.contentVersion();
        repaint();
    }
    void timerCallback() override
    {
        const bool playing = proc.isPlaying();
        previewBtn.setButtonText (playing ? "STOP" : "PREVIEW");
        const bool anyLoop = proc.hasAnyLoop();
        clearAll.setEnabled (anyLoop); save.setEnabled (anyLoop); previewBtn.setEnabled (anyLoop || playing);
        if (proc.contentVersion() != lastVersion || playing || wasPlaying || proc.busy()) refresh();
        wasPlaying = playing;
    }

    DrFillProcessor& proc;
    juce::TextButton load { "LOAD..." }, clearAll { "CLEAR" }, previewBtn { "PREVIEW" }, regen { "REGENERATE" }, seedDown { "<" }, seedUp { ">" }, save { "SAVE WAVS" };
    std::array<juce::TextButton, drf::kLanes> clearLane { juce::TextButton ("x"), juce::TextButton ("x"), juce::TextButton ("x") };
    juce::Rectangle<int> rows[drf::kLanes], boxes[drf::kLanes], waves[drf::kLanes], fillView, chips[DrFillProcessor::kNumParts], status, seedArea, tempoArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note;
    int lastVersion = -1, hover = kNone;
    bool dragging = false, wasPlaying = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (DrFillProcessor& p, Panel&) { return std::make_unique<DrFillDisplay> (p); }
// test / screenshot hooks (the display class is private to this file)
void displayDrop (juce::Component* d, int target, const juce::StringArray& files) { if (auto* r = dynamic_cast<DrFillDisplay*> (d)) r->dropOn (target, files); }
void displayHover (juce::Component* d, int target) { if (auto* r = dynamic_cast<DrFillDisplay*> (d)) r->demoHover (target); }
} // namespace drfui
