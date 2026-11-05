// Drum Player: control layout + the pad display: 8 pads with their note, sound / sample name, waveform, choke group and a hit flash.
// Click a pad to play it; right-click for Load sample / Clear sample; drop an audio file on a pad to load it.
// 1.1.0: pattern strip under the pads - host tempo, the clip (pattern + fill) as a GM note grid with a playhead, and drag-out chips:
// MIDI (.mid with GM / FPC notes and the tempo) and audio loops (full / kick / snare / top / toms, 24-bit WAV, whole bars).
#include "PluginEditor.h"

namespace cwui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY DRUM PLAYER", "8-PAD DRUM PLAYER - PATTERNS & FILLS",
        { 16, 56, 1068, 300 },
        {
            { "PAD 1", nullptr, { 16, 364, 530, 118 }, { { 'k', "p1Tune", "TUNE" }, { 'k', "p1Decay", "DECAY" }, { 'k', "p1Pan", "PAN" }, { 'k', "p1Level", "LEVEL" }, { 'c', "p1Sound", "SOUND" }, { 'c', "p1Choke", "CHOKE" } }, 130 },
            { "PAD 2", nullptr, { 554, 364, 530, 118 }, { { 'k', "p2Tune", "TUNE" }, { 'k', "p2Decay", "DECAY" }, { 'k', "p2Pan", "PAN" }, { 'k', "p2Level", "LEVEL" }, { 'c', "p2Sound", "SOUND" }, { 'c', "p2Choke", "CHOKE" } }, 130 },
            { "PAD 3", nullptr, { 16, 488, 530, 118 }, { { 'k', "p3Tune", "TUNE" }, { 'k', "p3Decay", "DECAY" }, { 'k', "p3Pan", "PAN" }, { 'k', "p3Level", "LEVEL" }, { 'c', "p3Sound", "SOUND" }, { 'c', "p3Choke", "CHOKE" } }, 130 },
            { "PAD 4", nullptr, { 554, 488, 530, 118 }, { { 'k', "p4Tune", "TUNE" }, { 'k', "p4Decay", "DECAY" }, { 'k', "p4Pan", "PAN" }, { 'k', "p4Level", "LEVEL" }, { 'c', "p4Sound", "SOUND" }, { 'c', "p4Choke", "CHOKE" } }, 130 },
            { "PAD 5", nullptr, { 16, 612, 530, 118 }, { { 'k', "p5Tune", "TUNE" }, { 'k', "p5Decay", "DECAY" }, { 'k', "p5Pan", "PAN" }, { 'k', "p5Level", "LEVEL" }, { 'c', "p5Sound", "SOUND" }, { 'c', "p5Choke", "CHOKE" } }, 130 },
            { "PAD 6", nullptr, { 554, 612, 530, 118 }, { { 'k', "p6Tune", "TUNE" }, { 'k', "p6Decay", "DECAY" }, { 'k', "p6Pan", "PAN" }, { 'k', "p6Level", "LEVEL" }, { 'c', "p6Sound", "SOUND" }, { 'c', "p6Choke", "CHOKE" } }, 130 },
            { "PAD 7", nullptr, { 16, 736, 530, 118 }, { { 'k', "p7Tune", "TUNE" }, { 'k', "p7Decay", "DECAY" }, { 'k', "p7Pan", "PAN" }, { 'k', "p7Level", "LEVEL" }, { 'c', "p7Sound", "SOUND" }, { 'c', "p7Choke", "CHOKE" } }, 130 },
            { "PAD 8", nullptr, { 554, 736, 530, 118 }, { { 'k', "p8Tune", "TUNE" }, { 'k', "p8Decay", "DECAY" }, { 'k', "p8Pan", "PAN" }, { 'k', "p8Level", "LEVEL" }, { 'c', "p8Sound", "SOUND" }, { 'c', "p8Choke", "CHOKE" } }, 130 },
            { "MASTER", nullptr, { 16, 860, 1068, 118 }, { { 'k', "volume", "VOLUME" }, { 'k', "velSens", "VELOCITY" }, { 'k', "kitTune", "KIT TUNE" }, { 'c', "noteMap", "NOTE MAP" }, { 'c', "baseNote", "PAD 1 NOTE (CHROMATIC)" } }, 160 },
            { "PATTERN", nullptr, { 16, 984, 530, 118 }, { { 'k', "fillSeed", "FILL VARIATION" }, { 'c', "pattern", "PATTERN" }, { 'c', "fillStyle", "FILL" } }, 220 },
            { "LOOP", nullptr, { 554, 984, 530, 118 }, { { 'c', "fillLen", "FILL LENGTH" }, { 'c', "dragLen", "LOOP LENGTH" }, { 't', "patPlay", "PLAY IN PLUGIN" } }, 220 },
        },
        "p1Tune"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff4a4a4a);

class PadDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    PadDisplay (DrumPlayerProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        const auto P = proc.currentParams();
        for (int i = 0; i < cw::dsp::kPads; ++i)
        {
            auto a = padBounds (i).toFloat();
            const float fl = flash[(size_t) i];
            g.setColour (juce::Colour (0xff181818).interpolatedWith (juce::Colour (0xff3a3a3a), fl)); g.fillRoundedRectangle (a, 5.0f);
            g.setColour (dragPad == i ? cPri : cLine.interpolatedWith (cPri, fl * 0.8f)); g.drawRoundedRectangle (a.reduced (0.5f), 5.0f, dragPad == i ? 2.0f : 1.0f);
            auto r = a.reduced (8.0f, 6.0f);
            auto top = r.removeFromTop (14);
            g.setFont (uiFont (10.5f, true)); g.setColour (cPri);
            g.drawText (juce::String (i + 1), top, juce::Justification::centredLeft);
            g.setColour (cDim);
            g.drawText (P.gm ? juce::String (cw::dsp::gmPadLabel (i)) : juce::MidiMessage::getMidiNoteName (P.baseNote + i, true, true, 3), top, juce::Justification::centredRight);
            const auto& pp = P.pad[(size_t) i];
            const bool isSample = pp.sound >= cw::dsp::kSounds;
            g.setFont (uiFont (11.0f)); g.setColour (isSample && ! proc.hasUserSample (i) ? cOff : cSec);
            g.drawFittedText (names[(size_t) i], r.removeFromTop (16).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            auto bottom = r.removeFromBottom (13);
            g.setFont (uiFont (9.5f, true)); g.setColour (cDim);
            g.drawText (isSample ? "SAMPLE" : "KIT", bottom, juce::Justification::centredLeft);
            if (pp.choke > 0) g.drawText ("CHOKE " + juce::String (pp.choke), bottom, juce::Justification::centredRight);
            // waveform (peak envelope of the whole sound)
            auto w = r.reduced (0, 4);
            const auto& pk = peaks[(size_t) i];
            if (! pk.empty())
            {
                juce::Path p; const int N = (int) pk.size();
                for (int k = 0; k < N; ++k) { const float x = w.getX() + w.getWidth() * (float) k / (float) (N - 1); const float y = w.getCentreY() - pk[(size_t) k] * w.getHeight() * 0.48f; if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
                for (int k = N - 1; k >= 0; --k) { const float x = w.getX() + w.getWidth() * (float) k / (float) (N - 1); p.lineTo (x, w.getCentreY() + pk[(size_t) k] * w.getHeight() * 0.48f); }
                p.closeSubPath();
                g.setColour (cSec.withAlpha (0.35f + 0.5f * fl)); g.fillPath (p);
                // decay curve
                if (pp.decay < cw::dsp::kDecayFull - 0.5f)
                {
                    const double secs = (double) pk.size() * peakStep[(size_t) i];
                    juce::Path d;
                    for (int k = 0; k <= 40; ++k) { const double t = secs * k / 40.0; const float e = (float) std::exp (std::log (0.001) * t / (pp.decay / 1000.0));
                        const float x = w.getX() + w.getWidth() * (float) k / 40.0f, y = w.getCentreY() - e * w.getHeight() * 0.48f; if (k == 0) d.startNewSubPath (x, y); else d.lineTo (x, y); }
                    g.setColour (cPri.withAlpha (0.6f)); g.strokePath (d, juce::PathStrokeType (1.0f));
                }
            }
            else { g.setColour (cOff); g.setFont (uiFont (10.0f)); g.drawText ("DROP AUDIO FILE", w, juce::Justification::centred); }
        }
        paintStrip (g);
    }

    // ---- pattern strip
    juce::Rectangle<int> stripBounds() const { auto r = getLocalBounds().reduced (10, 10); return r.removeFromBottom (120); }
    juce::Rectangle<int> infoBounds() const { return stripBounds().removeFromLeft (190); }
    juce::Rectangle<int> chipArea() const { return stripBounds().removeFromRight (330); }
    juce::Rectangle<int> gridBounds() const { auto r = stripBounds(); r.removeFromLeft (198); r.removeFromRight (338); return r; }
    // chip 0 = MIDI, 1 + part = audio parts, kChips - 1 = NEW FILL (click)
    static constexpr int kChips = 1 + DrumPlayerProcessor::kNumParts + 1;
    juce::Rectangle<int> chipBounds (int c) const
    {
        auto a = chipArea(); const int w = (a.getWidth() - 2 * 6) / 3, h = (a.getHeight() - 6) / 3 - 2;
        if (c == kChips - 1) return { a.getX(), a.getBottom() - h, a.getWidth(), h };
        return { a.getX() + (c % 3) * (w + 6), a.getY() + (c / 3) * (h + 6), w, h };
    }
    static juce::String chipName (int c)
    {
        if (c == 0) return "MIDI  .mid";
        if (c == kChips - 1) return "NEW FILL";
        static const char* n[] = { "FULL LOOP", "KICK", "SNARE", "TOP", "TOMS" };
        return n[c - 1];
    }
    void paintStrip (juce::Graphics& g)
    {
        auto info = infoBounds().toFloat();
        g.setColour (juce::Colour (0xff161616)); g.fillRoundedRectangle (stripBounds().toFloat(), 5.0f);
        g.setColour (cLine); g.drawRoundedRectangle (stripBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
        auto t = info.reduced (10, 8);
        g.setColour (cPri); g.setFont (uiFont (22.0f, true));
        g.drawText (juce::String (proc.tempo(), 1) + " BPM", t.removeFromTop (28), juce::Justification::centredLeft);
        g.setFont (uiFont (10.5f, true)); g.setColour (proc.hostTempo() > 0 ? cSec : cDim);
        g.drawText (proc.hostTempo() > 0 ? "(host)" : "(default - no host tempo)", t.removeFromTop (14), juce::Justification::centredLeft);
        t.removeFromTop (8);
        const auto c = proc.clipSettings();
        g.setColour (cSec); g.setFont (uiFont (11.5f, true));
        g.drawText (cw::pat::patterns()[(size_t) c.pattern].name, t.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (cDim); g.setFont (uiFont (10.5f));
        g.drawText (juce::String (c.bars) + (c.bars == 1 ? " bar" : " bars") + (c.fill != cw::pat::kNoFill ? "  -  " + juce::String (cw::pat::fillName (c.fill)) + " fill #" + juce::String ((int) c.seed) : juce::String ("  -  no fill")), t.removeFromTop (14), juce::Justification::centredLeft);
        g.drawText (proc.currentParams().gm ? "GM / FPC notes" : "chromatic map (MIDI out is GM)", t.removeFromTop (14), juce::Justification::centredLeft);

        // grid
        auto gr = gridBounds().toFloat();
        g.setColour (cBg); g.fillRect (gr);
        static const int order[] = { cw::pat::nCrash, cw::pat::nRide, cw::pat::nOH, cw::pat::nCH, cw::pat::nRim, cw::pat::nClap, cw::pat::nSnare, cw::pat::nTomH, cw::pat::nTomM, cw::pat::nTomL, cw::pat::nKick };
        static const char* label[] = { "CRASH", "RIDE", "OH", "CH", "RIM", "CLAP", "SNARE", "TOM H", "TOM M", "TOM L", "KICK" };
        std::vector<int> rows; for (int k = 0; k < 11; ++k) for (auto& e : clip) if (e.note == order[k]) { rows.push_back (k); break; }
        const double len = c.bars * 4.0;
        const float lw = 40, x0 = gr.getX() + lw, gw = gr.getWidth() - lw;
        if (c.fill != cw::pat::kNoFill)
        {
            const float fx = x0 + gw * (float) ((len - std::min ((double) c.fillBeats, len)) / len);
            g.setColour (juce::Colour (0xff2a2a2a)); g.fillRect (juce::Rectangle<float> (fx, gr.getY(), gr.getRight() - fx, gr.getHeight()));
        }
        for (int b = 0; b <= (int) len; ++b) { g.setColour (b % 4 == 0 ? cLine.brighter (0.4f) : cGrid.brighter (0.2f)); g.fillRect (x0 + gw * (float) (b / len), gr.getY(), 1.0f, gr.getHeight()); }
        if (! rows.empty())
        {
            const float rh = gr.getHeight() / (float) rows.size();
            const float cw = std::max (2.0f, gw / (float) (len * 4) * 0.7f);
            for (size_t r = 0; r < rows.size(); ++r)
            {
                const float y = gr.getY() + rh * (float) r;
                g.setColour (cDim); g.setFont (uiFont (std::min (9.5f, rh * 0.9f)));
                g.drawText (label[rows[r]], juce::Rectangle<float> (gr.getX() + 2, y, lw - 4, rh), juce::Justification::centredLeft);
                for (auto& e : clip)
                    if (e.note == order[rows[r]])
                    {
                        g.setColour (cPri.withAlpha (0.3f + 0.7f * e.vel));
                        g.fillRect (juce::Rectangle<float> (x0 + gw * (float) (e.beat / len), y + 1, cw, std::max (1.0f, rh - 2)));
                    }
            }
        }
        const double pb = proc.playPosition();
        if (pb >= 0 && pb < len) { g.setColour (cPri); g.fillRect (x0 + gw * (float) (pb / len), gr.getY(), 1.5f, gr.getHeight()); }

        // chips
        for (int k = 0; k < kChips; ++k)
        {
            auto cb = chipBounds (k).toFloat();
            g.setColour (hoverChip == k ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff222222)); g.fillRoundedRectangle (cb, 4.0f);
            g.setColour (k == kChips - 1 ? cSec : cPri.withAlpha (0.5f)); g.drawRoundedRectangle (cb.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (cPri); g.setFont (uiFont (10.5f, true));
            g.drawText (chipName (k), cb, juce::Justification::centred);
        }
        g.setColour (note.isNotEmpty() ? cSec : cDim); g.setFont (uiFont (9.5f));
        if (note.isNotEmpty()) g.drawText (note, chipBounds (kChips - 1).translated (0, -15).withHeight (14), juce::Justification::centred);
    }
    int chipAt (juce::Point<int> p) const { for (int k = 0; k < kChips; ++k) if (chipBounds (k).contains (p)) return k; return -1; }
    void mouseMove (const juce::MouseEvent& e) override { const int c = chipAt (e.getPosition()); if (c != hoverChip) { hoverChip = c; repaint (chipArea()); } }
    void mouseExit (const juce::MouseEvent&) override { if (hoverChip >= 0) { hoverChip = -1; repaint (chipArea()); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        if (chipAt (e.getPosition()) == kChips - 1 && chipAt (e.getMouseDownPosition()) == kChips - 1) { proc.newFill(); note = {}; repaint(); }
    }
    // the drag-out file for a chip (also the test hook)
    juce::File exportChip (int c, juce::String& err)
    {
        if (c == 0) return proc.exportMidi (err);
        if (c >= 1 && c <= DrumPlayerProcessor::kNumParts) return proc.exportPart (c - 1, err);
        err = "Not a drag chip"; return {};
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4) return;
        const int c = chipAt (e.getMouseDownPosition());
        if (c < 0 || c == kChips - 1) return;
        juce::String err;
        const auto f = exportChip (c, err);
        if (! f.existsAsFile()) { note = err; repaint(); return; }
        note = {}; dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = padAt (e.getPosition());
        if (i < 0) return;
        if (e.mods.isPopupMenu()) { showPadMenu (i, e.getEventRelativeTo (&panel).getPosition()); return; }
        const float vel = juce::jlimit (0.3f, 1.0f, 1.0f - (float) (e.y - padBounds (i).getY()) / (float) padBounds (i).getHeight() * 0.7f);   // higher = harder
        proc.queuePad (i, vel);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (proc.audioFormats().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { hostkeys::noteFileDrag (*this); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { hostkeys::noteFileDrag (*this); const int p = padAt ({ x, y }); if (p != dragPad) { dragPad = p; repaint(); } }
    void fileDragExit (const juce::StringArray&) override { hostkeys::noteFileDrag (*this); dragPad = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        hostkeys::noteFileDrag (*this);
        int pad = padAt ({ x, y }); dragPad = -1;
        for (auto& f : files)   // several files fill the following pads
        {
            if (pad < 0 || pad >= cw::dsp::kPads) break;
            if (proc.loadSample (pad, juce::File (f))) ++pad;
        }
        refresh(); repaint();
    }

    void demoFill() { flash = { 0.9f, 0, 0.5f, 0, 0.7f, 0, 0, 0.2f }; demo = true; refresh(); repaint(); }
    void refresh()
    {
        for (int i = 0; i < cw::dsp::kPads; ++i)
        {
            names[(size_t) i] = proc.padSoundName (i);
            auto& pk = peaks[(size_t) i]; pk.clear();
            if (const auto* s = proc.padSample (i))
            {
                const int N = 64, len = s->len(); const int step = std::max (1, len / N);
                for (int k = 0; k + step <= len && (int) pk.size() < N; k += step)
                {
                    float m = 0; for (int j = k; j < k + step; ++j) m = std::max ({ m, std::fabs (s->L[(size_t) j]), s->R.empty() ? 0.0f : std::fabs (s->R[(size_t) j]) });
                    pk.push_back (std::min (1.0f, m));
                }
                peakStep[(size_t) i] = step / s->rate;
            }
        }
    }

private:
    juce::Rectangle<int> padBounds (int i) const
    {
        auto r = getLocalBounds().reduced (10, 10); r.setHeight (r.getHeight() - 128);
        const int w = (r.getWidth() - 7 * 8) / 8;
        return { r.getX() + i * (w + 8), r.getY(), w, r.getHeight() };
    }
    int padAt (juce::Point<int> p) const { for (int i = 0; i < cw::dsp::kPads; ++i) if (padBounds (i).contains (p)) return i; return -1; }

    void showPadMenu (int i, juce::Point<int> pos)
    {
        std::vector<PopupOverlay::Item> v;
        v.push_back ({ "&Play pad " + juce::String (i + 1), [this, i] { proc.queuePad (i, 0.9f); } });
        v.push_back ({ "&Load sample...", [this, i] { browse (i); } });
        v.push_back ({ "&Clear sample", [this, i] { proc.clearSample (i); refresh(); repaint(); }, proc.hasUserSample (i) });
        panel.popupOverlay().show (pos, std::move (v));
    }
    void browse (int i)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample into pad " + juce::String (i + 1), juce::File(), proc.audioFormats().getWildcardForAllFormats());
        juce::Component::SafePointer<PadDisplay> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, i] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile()) safe->proc.loadSample (i, f);
            safe->refresh(); safe->repaint(); cwui::hostkeys::releaseLater (*safe);
        });
    }
    void timerCallback() override
    {
        bool dirty = false;
        const auto P = proc.currentParams();
        int key = proc.sampleChangeCount();
        for (auto& pp : P.pad) key = key * 31 + pp.sound;
        if (key != lastKey) { lastKey = key; refresh(); dirty = true; }
        const auto cs = proc.clipSettings();
        const juce::String ck = juce::String (cs.pattern) + "/" + juce::String (cs.fill) + "/" + juce::String (cs.fillBeats) + "/" + juce::String (cs.bars) + "/" + juce::String ((int) cs.seed);
        if (ck != clipKey) { clipKey = ck; clip = proc.clipEvents(); dirty = true; }
        if (proc.playPosition() >= 0 || lastPlay >= 0) { lastPlay = proc.playPosition(); repaint (gridBounds()); }
        if (std::abs (proc.tempo() - lastTempo) > 0.001) { lastTempo = proc.tempo(); dirty = true; }
        if (! demo)
            for (int i = 0; i < cw::dsp::kPads; ++i)
            {
                const int h = proc.dspEngine().hits[(size_t) i].load (std::memory_order_relaxed);
                if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; flash[(size_t) i] = 1.0f; dirty = true; }
                else if (flash[(size_t) i] > 0.01f) { flash[(size_t) i] *= 0.8f; dirty = true; }
                else flash[(size_t) i] = 0;
            }
        if (dirty || ++slow % 10 == 0) repaint();   // knob changes (choke, decay curve, notes) show within 1/3 s
    }

    DrumPlayerProcessor& proc;
    Panel& panel;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<std::vector<float>, cw::dsp::kPads> peaks;
    std::array<double, cw::dsp::kPads> peakStep {};
    std::array<juce::String, cw::dsp::kPads> names;
    std::array<float, cw::dsp::kPads> flash {};
    std::array<int, cw::dsp::kPads> lastHits {};
    int lastKey = -1, dragPad = -1, slow = 0, hoverChip = -1;
    bool demo = false, dragging = false;
    juce::String clipKey, note;
    std::vector<cw::pat::Ev> clip;
    double lastPlay = -1, lastTempo = 0;
public:
    juce::Rectangle<int> chipRect (int c) const { return chipBounds (c); }
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (DrumPlayerProcessor& p, Panel& pn) { return std::make_unique<PadDisplay> (p, pn); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<PadDisplay*> (c)) d->demoFill(); }
juce::File exportChip (juce::Component* c, int chip, juce::String& err) { if (auto* d = dynamic_cast<PadDisplay*> (c)) return d->exportChip (chip, err); err = "no display"; return {}; }
} // namespace cwui
