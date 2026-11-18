// Drum Player: control layout + the pad display: 8 pads with their note, sound / sample name, waveform, choke group and a hit flash.
// Click a pad to play it; right-click for Load sample / Clear sample; drop an audio file on a pad to load it.
// 1.1.0: pattern strip under the pads - host tempo, the clip (pattern + fill) as a GM note grid with a playhead, and drag-out chips:
// MIDI (.mid with GM / FPC notes and the tempo) and audio loops (full / kick / snare / top / toms, 24-bit WAV, whole bars).
// 1.2.0: PLAY / STOP previews the loop through the plug-in (no host transport needed); click a loop chip to hear just that part,
// drag it to export it. WITH HOST plays the pattern locked to the host's song position while the host runs.
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
            { "MASTER", nullptr, { 16, 860, 1068, 118 }, { { 'k', "volume", "VOLUME" }, { 'k', "velSens", "VELOCITY" }, { 'k', "kitTune", "KIT TUNE" }, { 'c', "noteMap", "NOTE MAP" }, { 'c', "baseNote", "PAD 1 NOTE (CHROMATIC)" }, { 't', "softClip", "SOFT CLIP" } }, 330 },
            { "PATTERN", nullptr, { 16, 984, 530, 118 }, { { 'k', "swing", "SWING" }, { 'k', "fillSeed", "FILL VARIATION" }, { 'k', "fillDensity", "FILL DENSITY" }, { 'c', "pattern", "PATTERN" }, { 'c', "fillStyle", "FILL" } }, 200 },
            { "LOOP", nullptr, { 554, 984, 530, 118 }, { { 'k', "bpm", "TEMPO (HOST / BPM)" }, { 'c', "fillLen", "FILL LENGTH" }, { 'c', "dragLen", "LOOP LENGTH" }, { 'c', "fillEnd", "FILL ENDING" } }, 330 },
        },
        "p1Tune"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff4a4a4a);

// ---- 1.2.0 sound swap menu (pad right-click, lane right-click): the pad's own items, Reset to kit, then every factory kit and the
// built-in sounds. A kit opens its 8 pads; hovering one auditions it on this pad (same voice code as playback), clicking takes it.
void showSwapMenu (DrumPlayerProcessor& proc, Panel& panel, int pad, juce::Point<int> pos, std::vector<PopupOverlay::Item> head, std::function<void()> changed);

void showSwapLevel2 (DrumPlayerProcessor& proc, Panel& panel, int pad, int kit, juce::Point<int> pos, std::function<void()> changed)
{
    std::vector<PopupOverlay::Item> v;
    const auto cur = proc.currentParams().pad[(size_t) pad];
    v.push_back ({ "PAD " + juce::String (pad + 1) + "  <  " + (kit >= 0 ? DrumPlayerProcessor::kitName (kit).toUpperCase() : juce::String ("BUILT-IN SOUNDS")) + "\thover to listen", nullptr, false });
    v.push_back ({ "< &Back", [&proc, &panel, pad, pos, changed] { showSwapMenu (proc, panel, pad, pos, {}, changed); } });
    if (kit >= 0)
        for (int i = 0; i < cw::dsp::kPads; ++i)
        {
            const auto q = proc.kitPad (kit, i);
            if (q.sound >= cw::dsp::kSounds) continue;
            const bool same = q.sound == cur.sound && std::abs (q.tune - cur.tune) < 0.05f && std::abs (q.decay - cur.decay) < 0.5f && std::abs (q.levelDb - cur.levelDb) < 0.05f;
            juce::String hint = cw::dsp::gmPadLabel (i);
            if (std::abs (q.tune) > 0.05f) hint = juce::String (q.tune > 0 ? "+" : "") + juce::String (q.tune, 1) + " st  " + hint;
            v.push_back ({ juce::String (cw::dsp::soundName (q.sound)) + "\t" + hint, [&proc, pad, kit, i, changed] { proc.swapFromKit (pad, kit, i); if (changed) changed(); },
                           true, same, i == 0, [&proc, pad, q] { proc.audition (pad, q); } });
        }
    else
        for (int s = 0; s < cw::dsp::kSounds; ++s)
        {
            cw::dsp::PadP q; q.sound = s; q.levelDb = cur.levelDb;
            v.push_back ({ juce::String (cw::dsp::soundName (s)), [&proc, pad, s, changed] { proc.swapBuiltIn (pad, s); if (changed) changed(); },
                           true, cur.sound == s, s == 0, [&proc, pad, q] { proc.audition (pad, q); } });
        }
    panel.popupOverlay().show (pos, std::move (v));
    panel.popupOverlay().onClose = [&proc] { proc.stopAudition(); };
}

void showSwapMenu (DrumPlayerProcessor& proc, Panel& panel, int pad, juce::Point<int> pos, std::vector<PopupOverlay::Item> head, std::function<void()> changed)
{
    if (pad < 0 || pad >= cw::dsp::kPads) return;
    auto v = std::move (head);
    const auto ref = proc.kitRefPad (pad);
    const juce::String refSound = ref.sound >= cw::dsp::kSounds ? juce::String ("Sample") : juce::String (cw::dsp::soundName (ref.sound));
    v.push_back ({ "&Reset to kit\t" + proc.kitRefName() + ": " + refSound, [&proc, pad, changed] { proc.resetPadToKit (pad); if (changed) changed(); },
                   proc.padSwapped (pad), false, ! v.empty() });
    v.push_back ({ "SWAP PAD " + juce::String (pad + 1) + " SOUND FROM", nullptr, false, false, true });
    for (int k = 0; k < DrumPlayerProcessor::numKits(); ++k)
        v.push_back ({ DrumPlayerProcessor::kitName (k) + "\t>", [&proc, &panel, pad, k, pos, changed] { showSwapLevel2 (proc, panel, pad, k, pos, changed); } });
    v.push_back ({ "Built-in sounds\t>", [&proc, &panel, pad, pos, changed] { showSwapLevel2 (proc, panel, pad, -1, pos, changed); }, true, false, true });
    panel.popupOverlay().show (pos, std::move (v));
}

// ---- 1.2.0 step editor: the selected pattern as 11 lanes x 32 sixteenths over the pad sections. Click an empty step to add a hit (80 %),
// click a hit to remove it, drag up / down on a step to set its velocity, right-click a step for accent / normal / ghost, right-click a lane
// name to swap that lane's pad sound. Edits play at once in the preview and go into the MIDI / WAV drag-outs and the session.
class StepEditor final : public juce::Component, private juce::Timer
{
public:
    StepEditor (DrumPlayerProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); setVisible (false);
    }
    std::function<void()> onVisibility;
    void open() { setVisible (true); toFront (false); startTimerHz (30); if (onVisibility) onVisibility(); repaint(); }
    void close() { setVisible (false); stopTimer(); if (onVisibility) onVisibility(); }

    static constexpr int kHeader = 38, kFooter = 24, kLabelW = 150;
    enum Btn { kClear = 0, kReset, kSave, kLoad, kClose, kNumBtn };
    static const char* btnName (int b) { static const char* n[] = { "CLEAR", "RESET TO FACTORY", "SAVE PATTERN...", "LOAD PATTERN", "CLOSE" }; return n[b]; }
    juce::Rectangle<int> btnBounds (int b) const
    {
        static const int w[] = { 70, 140, 124, 116, 70 };
        int x = getWidth() - 10; for (int i = kNumBtn - 1; i >= b; --i) x -= w[i] + (i < kNumBtn - 1 ? 6 : 0);
        return { x, 7, w[b], 24 };
    }
    juce::Rectangle<int> gridArea() const { return getLocalBounds().reduced (10, 0).withTrimmedTop (kHeader + 4).withTrimmedBottom (kFooter + 2); }
    juce::Rectangle<int> labelBounds (int lane) const { auto g = gridArea(); const float rh = g.getHeight() / (float) cw::pat::kLanes; return { g.getX(), g.getY() + (int) (rh * lane), kLabelW - 6, (int) rh }; }
    juce::Rectangle<int> cellBounds (int lane, int step) const
    {
        auto g = gridArea(); g.removeFromLeft (kLabelW);
        const float rh = g.getHeight() / (float) cw::pat::kLanes, cw = g.getWidth() / (float) cw::pat::kSteps;
        const int x0 = g.getX() + (int) (cw * step), x1 = g.getX() + (int) (cw * (step + 1)), y0 = g.getY() + (int) (rh * lane), y1 = g.getY() + (int) (rh * (lane + 1));
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    bool cellAt (juce::Point<int> p, int& lane, int& step) const
    {
        auto g = gridArea(); g.removeFromLeft (kLabelW);
        if (! g.contains (p)) return false;
        lane = juce::jlimit (0, cw::pat::kLanes - 1, (int) ((p.y - g.getY()) * cw::pat::kLanes / (float) g.getHeight()));
        step = juce::jlimit (0, cw::pat::kSteps - 1, (int) ((p.x - g.getX()) * cw::pat::kSteps / (float) g.getWidth()));
        return true;
    }
    int laneLabelAt (juce::Point<int> p) const { for (int l = 0; l < cw::pat::kLanes; ++l) if (labelBounds (l).contains (p)) return l; return -1; }
    static int lanePad (int lane) { return cw::dsp::gmMap (cw::pat::laneNotes()[lane]).pad; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff121212)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.45f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        const auto cs = proc.clipSettings();
        const bool ed = proc.patternEdited (cs.pattern);
        g.setColour (cPri); g.setFont (uiFont (13.0f, true));
        g.drawText ("STEP EDITOR", 14, 7, 120, 24, juce::Justification::centredLeft);
        g.setColour (cSec); g.setFont (uiFont (12.0f, true));
        g.drawText (juce::String (cw::pat::patterns()[(size_t) cs.pattern].name) + (ed ? "  -  edited" : "  -  factory"), 130, 7, 260, 24, juce::Justification::centredLeft);
        for (int b = 0; b < kNumBtn; ++b)
        {
            const auto r = btnBounds (b).toFloat(); const bool en = b != kReset || ed;
            g.setColour (hoverBtn == b && en ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff222222)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (en ? cPri.withAlpha (0.6f) : cOff); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (en ? cPri : cOff); g.setFont (uiFont (10.5f, true)); g.drawText (btnName (b), r, juce::Justification::centred);
        }
        // grid
        uint8_t grid[cw::pat::kGrid]; proc.getGrid (cs.pattern, grid);
        const int loopSteps = cs.bars == 1 ? 16 : 32;   // a 1-bar loop plays the first bar of the grid only
        const int fillSteps = cs.fill != cw::pat::kNoFill ? std::min (cs.fillBeats, cs.bars * 4) * 4 : 0;
        const int fillFrom = loopSteps - std::min (fillSteps, loopSteps);
        const double pb = proc.playPosition();
        const int playStep = pb >= 0 ? (int) (std::fmod (pb, 8.0) * 4.0) : -1;
        for (int l = 0; l < cw::pat::kLanes; ++l)
        {
            const auto lb = labelBounds (l);
            const int pad = lanePad (l);
            g.setColour (hoverLabel == l ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff181818)); g.fillRect (lb.reduced (0, 1));
            g.setColour (cPri); g.setFont (uiFont (10.5f, true));
            g.drawText (cw::pat::laneName (l), lb.reduced (6, 0).removeFromTop (lb.getHeight() / 2 + 2), juce::Justification::bottomLeft);
            g.setColour (proc.padSwapped (pad) ? cPri : cDim); g.setFont (uiFont (9.5f));
            g.drawText (proc.padSoundName (pad) + (proc.padSwapped (pad) ? "  *" : ""), lb.reduced (6, 0).removeFromBottom (lb.getHeight() / 2 - 2), juce::Justification::topLeft);
            for (int k = 0; k < cw::pat::kSteps; ++k)
            {
                auto c = cellBounds (l, k).toFloat().reduced (1.0f);
                const bool inLoop = k < loopSteps, inFill = k >= fillFrom && k < loopSteps;
                juce::Colour bg = (k / 4) % 2 == 0 ? juce::Colour (0xff1e1e1e) : juce::Colour (0xff191919);
                if (inFill) bg = bg.interpolatedWith (juce::Colour (0xff3a3020), 0.5f);
                if (k == playStep) bg = bg.brighter (0.35f);
                if (! inLoop) bg = bg.darker (0.5f);
                g.setColour (bg); g.fillRect (c);
                const int v = grid[l * cw::pat::kSteps + k];
                if (v > 0)
                {
                    const float h = c.getHeight() * (0.25f + 0.75f * v / 100.0f);
                    g.setColour (cPri.withAlpha ((inLoop ? 0.45f + 0.55f * v / 100.0f : 0.25f) * (inFill ? 0.55f : 1.0f)));
                    g.fillRect (c.withTrimmedTop (c.getHeight() - h));
                    if (dragLane == l && dragStep == k && moved) { g.setColour (cBg); g.setFont (uiFont (9.5f, true)); g.drawText (juce::String (v), c, juce::Justification::centredTop); }
                }
            }
        }
        auto ga = gridArea(); ga.removeFromLeft (kLabelW);
        g.setColour (cPri.withAlpha (0.5f)); g.fillRect ((float) ga.getX() + ga.getWidth() * 0.5f - 1.0f, (float) ga.getY(), 2.0f, (float) ga.getHeight());   // bar line
        g.setFont (uiFont (9.5f, true)); g.setColour (cDim);
        g.drawText ("BAR 1", ga.getX() + 2, ga.getY() - 13, 60, 12, juce::Justification::left);
        g.drawText (loopSteps == 16 ? "BAR 2 (not in a 1-bar loop)" : "BAR 2", ga.getCentreX() + 4, ga.getY() - 13, 200, 12, juce::Justification::left);
        if (fillSteps > 0)
        {
            const int fx = cellBounds (0, fillFrom).getX();
            g.setColour (juce::Colour (0xffd8b878)); g.drawText ("FILL REPLACES THESE ON THE LAST BAR", fx, ga.getY() - 13, cellBounds (0, loopSteps - 1).getRight() - fx, 12, juce::Justification::right);
        }
        g.setColour (cDim); g.setFont (uiFont (10.0f));
        g.drawText ("click: add / remove   drag up-down: velocity   right-click a step: accent / normal / ghost   right-click a lane name: swap its sound   click a lane name: listen",
                    getLocalBounds().removeFromBottom (kFooter).reduced (14, 0), juce::Justification::centredLeft);
        if (note.isNotEmpty()) { g.setColour (cSec); g.drawText (note, getLocalBounds().removeFromBottom (kFooter).reduced (14, 0), juce::Justification::centredRight); }
    }

    // edit gestures (also the test hooks)
    void press (int lane, int step)
    {
        const int p = proc.clipSettings().pattern;
        dragLane = lane; dragStep = step; moved = false;
        startVel = proc.stepAt (p, lane, step); added = startVel == 0;
        if (added) { startVel = 80; proc.setStep (p, lane, step, 80); }
        repaint();
    }
    void dragBy (int dy)
    {
        if (dragLane < 0) return;
        if (! moved && std::abs (dy) < 3) return;
        moved = true;
        proc.setStep (proc.clipSettings().pattern, dragLane, dragStep, juce::jlimit (1, 100, startVel - dy));
        repaint();
    }
    void release()
    {
        if (dragLane >= 0 && ! moved && ! added) proc.setStep (proc.clipSettings().pattern, dragLane, dragStep, 0);
        dragLane = dragStep = -1; repaint();
    }
    void button (int b)
    {
        const int p = proc.clipSettings().pattern;
        if (b == kClear) { proc.clearPattern (p); note = "cleared - RESET TO FACTORY brings it back"; }
        else if (b == kReset) { proc.resetPattern (p); note = "factory pattern restored"; }
        else if (b == kSave) savePattern();
        else if (b == kLoad) showLoadMenu (btnBounds (kLoad).getBottomLeft() + getPosition());
        else if (b == kClose) close();
        repaint();
    }
    void showStepMenu (int lane, int step, juce::Point<int> pos)
    {
        const int p = proc.clipSettings().pattern, v = proc.stepAt (p, lane, step);
        std::vector<PopupOverlay::Item> m;
        m.push_back ({ juce::String (cw::pat::laneName (lane)) + "  step " + juce::String (step + 1) + "\t" + (v > 0 ? juce::String (v) + " %" : juce::String ("off")), nullptr, false });
        m.push_back ({ "&Accent\t100 %", [this, p, lane, step] { proc.setStep (p, lane, step, 100); repaint(); }, true, v == 100, true });
        m.push_back ({ "&Normal\t80 %", [this, p, lane, step] { proc.setStep (p, lane, step, 80); repaint(); }, true, v == 80 });
        m.push_back ({ "&Ghost\t40 %", [this, p, lane, step] { proc.setStep (p, lane, step, 40); repaint(); }, true, v == 40 });
        m.push_back ({ "&Delete", [this, p, lane, step] { proc.setStep (p, lane, step, 0); repaint(); }, v > 0 });
        m.push_back ({ "Clear this &lane", [this, p, lane] { uint8_t g[cw::pat::kGrid]; proc.getGrid (p, g); std::fill (g + lane * cw::pat::kSteps, g + (lane + 1) * cw::pat::kSteps, (uint8_t) 0); proc.setGrid (p, g); repaint(); }, true, false, true });
        panel.popupOverlay().show (pos, std::move (m));
    }
    void showLaneMenu (int lane, juce::Point<int> pos)
    {
        const int pad = lanePad (lane);
        std::vector<PopupOverlay::Item> head;
        head.push_back ({ "&Play " + juce::String (cw::pat::laneName (lane)) + "\tpad " + juce::String (pad + 1), [this, pad] { proc.queuePad (pad, 0.9f); } });
        showSwapMenu (proc, panel, pad, pos, std::move (head), [this] { repaint(); });
    }
    void showLoadMenu (juce::Point<int> pos)
    {
        std::vector<PopupOverlay::Item> m;
        const auto files = DrumPlayerProcessor::patternFiles();
        m.push_back ({ "Load pattern &file...", [this] { loadPatternFile(); } });
        m.push_back ({ "&Open pattern folder", [] { auto d = DrumPlayerProcessor::patternDir(); d.createDirectory(); d.startAsProcess(); } });
        if (files.isEmpty()) m.push_back ({ "(no saved patterns yet - SAVE PATTERN... makes one)", nullptr, false, false, true });
        for (int i = 0; i < files.size(); ++i)
            m.push_back ({ files[i].getFileNameWithoutExtension(), [this, f = files[i]] { note = proc.loadPattern (f) ? "loaded " + f.getFileNameWithoutExtension() : juce::String ("not a Drum Player pattern"); repaint(); }, true, false, i == 0 });
        panel.popupOverlay().show (pos, std::move (m));
    }
    void savePattern()
    {
        DrumPlayerProcessor::patternDir().createDirectory();
        const auto base = juce::String (cw::pat::patterns()[(size_t) proc.clipSettings().pattern].name) + " edit";
        chooser = std::make_unique<juce::FileChooser> ("Save pattern", DrumPlayerProcessor::patternDir().getChildFile (juce::File::createLegalFileName (base) + DrumPlayerProcessor::kPatternExt),
                                                       juce::String ("*") + DrumPlayerProcessor::kPatternExt, true);
        juce::Component::SafePointer<StepEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f != juce::File()) { f = f.withFileExtension (DrumPlayerProcessor::kPatternExt); safe->note = safe->proc.savePattern (f, f.getFileNameWithoutExtension()) ? "saved " + f.getFileNameWithoutExtension() : juce::String ("could not save"); }
            safe->repaint(); hostkeys::releaseLater (*safe);
        });
    }
    void loadPatternFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load pattern", DrumPlayerProcessor::patternDir(), juce::String ("*") + DrumPlayerProcessor::kPatternExt, true);
        juce::Component::SafePointer<StepEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f != juce::File()) safe->note = safe->proc.loadPattern (f) ? "loaded " + f.getFileNameWithoutExtension() : juce::String ("not a Drum Player pattern");
            safe->repaint(); hostkeys::releaseLater (*safe);
        });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition(); const auto pp = e.getEventRelativeTo (&panel).getPosition();
        for (int b = 0; b < kNumBtn; ++b) if (btnBounds (b).contains (p)) { if (! e.mods.isPopupMenu() && (b != kReset || proc.patternEdited (proc.clipSettings().pattern))) button (b); return; }
        if (const int l = laneLabelAt (p); l >= 0) { if (e.mods.isPopupMenu()) showLaneMenu (l, pp); else proc.queuePad (lanePad (l), 0.9f); return; }
        int lane, step;
        if (cellAt (p, lane, step)) { if (e.mods.isPopupMenu()) showStepMenu (lane, step, pp); else { note = {}; press (lane, step); } }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) dragBy (e.getDistanceFromDragStartY()); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) release(); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int hb = -1; for (int b = 0; b < kNumBtn; ++b) if (btnBounds (b).contains (e.getPosition())) hb = b;
        const int hl = laneLabelAt (e.getPosition());
        if (hb != hoverBtn || hl != hoverLabel) { hoverBtn = hb; hoverLabel = hl; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverBtn = hoverLabel = -1; repaint(); }

private:
    void timerCallback() override
    {
        const auto cs = proc.clipSettings();
        const double pb = proc.playPosition();
        const int ps = pb >= 0 ? (int) (std::fmod (pb, 8.0) * 4.0) : -1;
        uint32_t key = (uint32_t) proc.stepVersion() * 7919u + (uint32_t) (cs.pattern * 131 + cs.bars * 17 + cs.fill * 5 + cs.fillBeats) + (uint32_t) (ps + 1) * 1000003u + (uint32_t) proc.sampleChangeCount() * 31u;
        for (auto& q : proc.currentParams().pad) key = key * 31u + (uint32_t) (q.sound + juce::roundToInt (q.tune * 10.0f) * 7 + juce::roundToInt (q.levelDb * 10.0f) * 131 + juce::roundToInt (q.decay));
        if (key != lastKey) { lastKey = key; repaint(); }
    }
    DrumPlayerProcessor& proc;
    Panel& panel;
    std::unique_ptr<juce::FileChooser> chooser;
    int dragLane = -1, dragStep = -1, startVel = 0, hoverBtn = -1, hoverLabel = -1;
    uint32_t lastKey = 0;
    bool moved = false, added = false;
public:
    juce::String note;
};

class PadDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    PadDisplay (DrumPlayerProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        steps = std::make_unique<StepEditor> (proc, panel);
        panel.addChildComponent (*steps);
        steps->setBounds (16, 364, 1068, 490);   // over the eight pad sections (panel base coordinates)
        steps->onVisibility = [this] { repaint (stripBounds()); };
        startTimerHz (30);
    }
    StepEditor* stepEditor() { return steps.get(); }
    void toggleStepEditor() { if (steps->isVisible()) steps->close(); else steps->open(); }

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
    // 1.2.0: transport block on the left (PLAY / STOP preview + WITH HOST), then the info, the note grid and the drag chips
    juce::Rectangle<int> transportBounds() const { return stripBounds().reduced (6).removeFromLeft (124); }
    juce::Rectangle<int> playBounds() const { auto r = transportBounds(); return r.removeFromTop (r.getHeight() - 30); }
    juce::Rectangle<int> syncBounds() const { auto r = transportBounds(); return r.removeFromBottom (26); }
    juce::Rectangle<int> infoBounds() const { auto r = stripBounds(); r.removeFromLeft (134); return r.removeFromLeft (178); }
    juce::Rectangle<int> chipArea() const { return stripBounds().removeFromRight (330); }
    juce::Rectangle<int> gridBounds() const { auto r = stripBounds(); r.removeFromLeft (134 + 184); r.removeFromRight (338); return r; }
    // chip 0 = MIDI, 1 + part = audio parts, kChips - 1 = NEW FILL (click)
    static constexpr int kChips = 1 + DrumPlayerProcessor::kNumParts + 1;
    juce::Rectangle<int> chipBounds (int c) const
    {
        auto a = chipArea(); const int w = (a.getWidth() - 2 * 6) / 3, h = (a.getHeight() - 6) / 3 - 2;
        if (c == kChips - 1) return { a.getX() + (a.getWidth() + 6) / 2, a.getBottom() - h, a.getWidth() - (a.getWidth() + 6) / 2, h };   // NEW FILL: right half
        return { a.getX() + (c % 3) * (w + 6), a.getY() + (c / 3) * (h + 6), w, h };
    }
    // 1.2.0: EDIT STEPS opens the step editor (left half of the bottom row)
    juce::Rectangle<int> editBounds() const { const auto f = chipBounds (kChips - 1); auto a = chipArea(); return { a.getX(), f.getY(), f.getX() - 6 - a.getX(), f.getHeight() }; }
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
        paintTransport (g);
        auto t = info.reduced (10, 8);
        g.setColour (cPri); g.setFont (uiFont (22.0f, true));
        g.drawText (juce::String (proc.tempo(), 1) + " BPM", t.removeFromTop (28), juce::Justification::centredLeft);
        const auto src = proc.tempoSource();
        g.setFont (uiFont (10.5f, true)); g.setColour (src == "default" ? cDim : cSec);
        g.drawText (src == "manual" ? "(TEMPO knob)" : src == "host" ? (proc.hostIsPlaying() ? "(host)" : "(host - last tempo)") : "(no host tempo - 120)", t.removeFromTop (14), juce::Justification::centredLeft);
        t.removeFromTop (8);
        const auto c = proc.clipSettings();
        g.setColour (cSec); g.setFont (uiFont (11.5f, true));
        g.drawText (proc.patternLabel(), t.removeFromTop (16), juce::Justification::centredLeft);
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
                const int pv = proc.previewPart(), rp = cw::dsp::gmMap (order[rows[r]]).pad;
                const float dim = pv > 0 && ! DrumPlayerProcessor::partHasPad (pv, rp) ? 0.25f : 1.0f;   // lanes not in the soloed part
                for (auto& e : clip)
                    if (e.note == order[rows[r]])
                    {
                        g.setColour (cPri.withAlpha ((0.3f + 0.7f * e.vel) * dim));
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
            const bool live = k >= 1 && k <= DrumPlayerProcessor::kNumParts && proc.previewPart() == k - 1;   // this part is in the preview
            g.setColour (live ? juce::Colour (0xff4a4a4a) : hoverChip == k ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff222222)); g.fillRoundedRectangle (cb, 4.0f);
            g.setColour (live ? cPri : k == kChips - 1 ? cSec : cPri.withAlpha (0.5f)); g.drawRoundedRectangle (cb.reduced (0.5f), 4.0f, live ? 1.5f : 1.0f);
            g.setColour (cPri); g.setFont (uiFont (10.5f, true));
            g.drawText (chipName (k), cb, juce::Justification::centred);
        }
        {
            auto eb = editBounds().toFloat(); const bool open = steps->isVisible();
            g.setColour (open ? juce::Colour (0xff4a4a4a) : hoverEdit ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff222222)); g.fillRoundedRectangle (eb, 4.0f);
            g.setColour (open ? cPri : cSec); g.drawRoundedRectangle (eb.reduced (0.5f), 4.0f, open ? 1.5f : 1.0f);
            g.setColour (cPri); g.setFont (uiFont (10.5f, true));
            g.drawText (open ? "CLOSE EDITOR" : "EDIT STEPS", eb, juce::Justification::centred);
        }
        g.setColour (note.isNotEmpty() ? cSec : cDim); g.setFont (uiFont (9.5f));
        if (note.isNotEmpty()) g.drawText (note, chipBounds (kChips - 1).translated (0, -15).withHeight (14), juce::Justification::centred);
        else if (hoverChip >= 0 && hoverChip < kChips - 1) { g.setColour (cDim); g.drawText ("click to listen - drag into your DAW", gridBounds().removeFromBottom (14).translated (0, 0), juce::Justification::centredRight); }
    }
    void paintTransport (juce::Graphics& g)
    {
        const int part = proc.previewPart();
        const bool playing = part >= 0, synced = proc.syncedToHost();
        auto pb = playBounds().toFloat();
        g.setColour (playing ? cPri : hoverPlay ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff262626)); g.fillRoundedRectangle (pb, 5.0f);
        g.setColour (playing ? cPri : cPri.withAlpha (0.6f)); g.drawRoundedRectangle (pb.reduced (0.5f), 5.0f, 1.5f);
        const juce::Colour ink = playing ? cBg : cPri;
        auto icon = pb.withSizeKeepingCentre (18, 18).withY (pb.getY() + 12);
        g.setColour (ink);
        if (playing) g.fillRect (icon.reduced (2));
        else { juce::Path tri; tri.addTriangle (icon.getX() + 2, icon.getY(), icon.getX() + 2, icon.getBottom(), icon.getRight(), icon.getCentreY()); g.fillPath (tri); }
        g.setFont (uiFont (14.0f, true));
        g.drawText (playing ? "STOP" : synced ? "WITH HOST" : "PLAY", pb.withTrimmedTop (34).withHeight (18), juce::Justification::centred);
        g.setFont (uiFont (9.5f, true)); g.setColour (playing ? cBg.withAlpha (0.8f) : cDim);
        g.drawText (playing ? juce::String (chipName (1 + part)) : juce::String ("PREVIEW LOOP"), pb.withTrimmedTop (52).withHeight (13), juce::Justification::centred);
        auto sb = syncBounds().toFloat();
        const bool on = proc.apvts.getRawParameterValue ("patPlay")->load() > 0.5f;
        g.setColour (on ? juce::Colour (0xff2e2e2e) : juce::Colour (0xff1c1c1c)); g.fillRoundedRectangle (sb, 4.0f);
        g.setColour (on ? cPri : cLine.brighter (0.3f)); g.drawRoundedRectangle (sb.reduced (0.5f), 4.0f, 1.0f);
        auto led = sb.removeFromLeft (22).withSizeKeepingCentre (8, 8);
        g.setColour (on ? (synced ? cPri : cSec) : cOff); g.fillEllipse (led);
        g.setColour (on ? cPri : cDim); g.setFont (uiFont (10.0f, true));
        g.drawText ("WITH HOST", sb.withTrimmedRight (6), juce::Justification::centredLeft);
    }
    void togglePreview (int part)
    {
        const int now = proc.previewPart();   // part < 0: the PLAY / STOP button (starts the full loop)
        if (now >= 0 && (part < 0 || part == now)) proc.stopPreview();
        else proc.startPreview (juce::jmax (0, part));
        repaint (stripBounds());
    }
    void toggleSync()
    {
        if (auto* p = proc.apvts.getParameter ("patPlay")) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        repaint (stripBounds());
    }
public:
    // test hooks
    void clickPlay() { togglePreview (-1); }
    void clickChip (int c) { if (c >= 0 && c <= DrumPlayerProcessor::kNumParts) togglePreview (c == 0 ? 0 : c - 1); }
    int chipAt (juce::Point<int> p) const { for (int k = 0; k < kChips; ++k) if (chipBounds (k).contains (p)) return k; return -1; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int c = chipAt (e.getPosition()); const bool hp = playBounds().contains (e.getPosition()), he = editBounds().contains (e.getPosition());
        if (c != hoverChip || hp != hoverPlay || he != hoverEdit) { hoverChip = c; hoverPlay = hp; hoverEdit = he; repaint (stripBounds()); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverChip >= 0 || hoverPlay || hoverEdit) { hoverChip = -1; hoverPlay = hoverEdit = false; repaint (stripBounds()); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown() || e.mods.isPopupMenu()) return;
        const int c = chipAt (e.getPosition());
        if (c != chipAt (e.getMouseDownPosition())) return;
        if (c == kChips - 1) { proc.newFill(); note = {}; repaint(); return; }
        if (c >= 0) { note = {}; clickChip (c); return; }   // a click on a loop chip previews that part (MIDI = the full loop)
        if (editBounds().contains (e.getPosition()) && editBounds().contains (e.getMouseDownPosition())) { toggleStepEditor(); return; }
        if (gridBounds().contains (e.getPosition()) && gridBounds().contains (e.getMouseDownPosition())) { if (! steps->isVisible()) steps->open(); return; }   // click the notes: edit them
        if (playBounds().contains (e.getPosition()) && playBounds().contains (e.getMouseDownPosition())) clickPlay();
        else if (syncBounds().contains (e.getPosition()) && syncBounds().contains (e.getMouseDownPosition())) toggleSync();
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
        if (e.mods.isPopupMenu() && gridBounds().contains (e.getPosition()))   // right-click a lane of the note grid: swap that pad's sound
        {
            if (const int pad = stripPadAt (e.getPosition()); pad >= 0) showSwapMenu (proc, panel, pad, e.getEventRelativeTo (&panel).getPosition(), {}, [this] { refresh(); repaint(); });
            return;
        }
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
        showSwapMenu (proc, panel, i, pos, std::move (v), [this] { refresh(); repaint(); });   // 1.2.0: + Reset to kit and the swap sub-menus
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
        const juce::String ck = juce::String (cs.pattern) + "/" + juce::String (cs.fill) + "/" + juce::String (cs.fillBeats) + "/" + juce::String (cs.bars) + "/" + juce::String ((int) cs.seed) + "/" + juce::String (juce::roundToInt (cs.swing * 100.0f))
                              + "/" + juce::String (juce::roundToInt (cs.density * 100.0f)) + "/" + juce::String (cs.ending) + "/" + juce::String (proc.stepVersion());
        if (ck != clipKey) { clipKey = ck; clip = proc.clipEvents(); dirty = true; }
        if (proc.playPosition() >= 0 || lastPlay >= 0) { lastPlay = proc.playPosition(); repaint (gridBounds()); }
        if (const int pp = proc.previewPart() * 4 + (proc.syncedToHost() ? 2 : 0) + (proc.apvts.getRawParameterValue ("patPlay")->load() > 0.5f ? 1 : 0); pp != lastTransport) { lastTransport = pp; repaint (stripBounds()); }
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
    int lastKey = -1, dragPad = -1, slow = 0, hoverChip = -1, lastTransport = -99;
    bool demo = false, dragging = false, hoverPlay = false, hoverEdit = false;
    std::unique_ptr<StepEditor> steps;
    // the pad of the note-grid lane under p (rows = the GM lanes present in the clip, top to bottom as painted)
    int stripPadAt (juce::Point<int> p) const
    {
        std::vector<int> rows; for (int k = 0; k < cw::pat::kLanes; ++k) for (auto& e : clip) if (e.note == cw::pat::laneNotes()[k]) { rows.push_back (k); break; }
        const auto gr = gridBounds();
        if (rows.empty() || ! gr.contains (p)) return -1;
        const int r = juce::jlimit (0, (int) rows.size() - 1, (int) ((p.y - gr.getY()) * (int) rows.size() / juce::jmax (1, gr.getHeight())));
        return StepEditor::lanePad (rows[(size_t) r]);
    }
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
static StepEditor* stepEditorOf (juce::Component* c) { if (auto* d = dynamic_cast<PadDisplay*> (c)) return d->stepEditor(); return nullptr; }
void stepEditorOpen (juce::Component* c, bool open) { if (auto* e = stepEditorOf (c)) { if (open) e->open(); else e->close(); } }
bool stepEditorIsOpen (juce::Component* c) { auto* e = stepEditorOf (c); return e != nullptr && e->isVisible(); }
void stepEditorGesture (juce::Component* c, int lane, int step, int dy) { if (auto* e = stepEditorOf (c)) { e->press (lane, step); if (dy != 0) { e->dragBy (dy / 2); e->dragBy (dy); } e->release(); } }
void stepEditorButton (juce::Component* c, int b) { if (auto* e = stepEditorOf (c)) e->button (b); }
void stepEditorStepMenu (juce::Component* c, int lane, int step) { if (auto* e = stepEditorOf (c)) e->showStepMenu (lane, step, e->getBounds().getCentre()); }
void stepEditorLaneMenu (juce::Component* c, int lane) { if (auto* e = stepEditorOf (c)) e->showLaneMenu (lane, e->getPosition() + e->labelBounds (lane).getCentre()); }
void padSwapMenu (juce::Component* c, Panel& pn, DrumPlayerProcessor& p, int pad) { showSwapMenu (p, pn, pad, c != nullptr ? c->getBounds().getCentre() : juce::Point<int> (400, 200), {}, nullptr); }
void clickPreview (juce::Component* c, int chip) { if (auto* d = dynamic_cast<PadDisplay*> (c)) { if (chip < 0) d->clickPlay(); else d->clickChip (chip); } }
} // namespace cwui
