// Drum Player: control layout + the pad display: 8 pads with their note, sound / sample name, waveform, choke group and a hit flash.
// Click a pad to play it; right-click for Load sample / Clear sample; drop an audio file on a pad to load it.
#include "PluginEditor.h"

namespace cwui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY DRUM PLAYER", "8-PAD DRUM & ONE-SHOT PLAYER",
        { 16, 56, 1068, 170 },
        {
            { "PAD 1", nullptr, { 16, 234, 530, 118 }, { { 'k', "p1Tune", "TUNE" }, { 'k', "p1Decay", "DECAY" }, { 'k', "p1Pan", "PAN" }, { 'k', "p1Level", "LEVEL" }, { 'c', "p1Sound", "SOUND" }, { 'c', "p1Choke", "CHOKE" } }, 130 },
            { "PAD 2", nullptr, { 554, 234, 530, 118 }, { { 'k', "p2Tune", "TUNE" }, { 'k', "p2Decay", "DECAY" }, { 'k', "p2Pan", "PAN" }, { 'k', "p2Level", "LEVEL" }, { 'c', "p2Sound", "SOUND" }, { 'c', "p2Choke", "CHOKE" } }, 130 },
            { "PAD 3", nullptr, { 16, 358, 530, 118 }, { { 'k', "p3Tune", "TUNE" }, { 'k', "p3Decay", "DECAY" }, { 'k', "p3Pan", "PAN" }, { 'k', "p3Level", "LEVEL" }, { 'c', "p3Sound", "SOUND" }, { 'c', "p3Choke", "CHOKE" } }, 130 },
            { "PAD 4", nullptr, { 554, 358, 530, 118 }, { { 'k', "p4Tune", "TUNE" }, { 'k', "p4Decay", "DECAY" }, { 'k', "p4Pan", "PAN" }, { 'k', "p4Level", "LEVEL" }, { 'c', "p4Sound", "SOUND" }, { 'c', "p4Choke", "CHOKE" } }, 130 },
            { "PAD 5", nullptr, { 16, 482, 530, 118 }, { { 'k', "p5Tune", "TUNE" }, { 'k', "p5Decay", "DECAY" }, { 'k', "p5Pan", "PAN" }, { 'k', "p5Level", "LEVEL" }, { 'c', "p5Sound", "SOUND" }, { 'c', "p5Choke", "CHOKE" } }, 130 },
            { "PAD 6", nullptr, { 554, 482, 530, 118 }, { { 'k', "p6Tune", "TUNE" }, { 'k', "p6Decay", "DECAY" }, { 'k', "p6Pan", "PAN" }, { 'k', "p6Level", "LEVEL" }, { 'c', "p6Sound", "SOUND" }, { 'c', "p6Choke", "CHOKE" } }, 130 },
            { "PAD 7", nullptr, { 16, 606, 530, 118 }, { { 'k', "p7Tune", "TUNE" }, { 'k', "p7Decay", "DECAY" }, { 'k', "p7Pan", "PAN" }, { 'k', "p7Level", "LEVEL" }, { 'c', "p7Sound", "SOUND" }, { 'c', "p7Choke", "CHOKE" } }, 130 },
            { "PAD 8", nullptr, { 554, 606, 530, 118 }, { { 'k', "p8Tune", "TUNE" }, { 'k', "p8Decay", "DECAY" }, { 'k', "p8Pan", "PAN" }, { 'k', "p8Level", "LEVEL" }, { 'c', "p8Sound", "SOUND" }, { 'c', "p8Choke", "CHOKE" } }, 130 },
            { "MASTER", nullptr, { 16, 730, 1068, 118 }, { { 'k', "volume", "VOLUME" }, { 'k', "velSens", "VELOCITY" }, { 'k', "kitTune", "KIT TUNE" }, { 'c', "baseNote", "PAD 1 NOTE" } }, 160 },
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
            g.drawText ("PAD " + juce::String (i + 1), top, juce::Justification::centredLeft);
            g.setColour (cDim);
            g.drawText (juce::MidiMessage::getMidiNoteName (P.baseNote + i, true, true, 3), top, juce::Justification::centredRight);
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
    void fileDragMove (const juce::StringArray&, int x, int y) override { const int p = padAt ({ x, y }); if (p != dragPad) { dragPad = p; repaint(); } }
    void fileDragExit (const juce::StringArray&) override { dragPad = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
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
        auto r = getLocalBounds().reduced (10, 10);
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
    int lastKey = -1, dragPad = -1, slow = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (DrumPlayerProcessor& p, Panel& pn) { return std::make_unique<PadDisplay> (p, pn); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<PadDisplay*> (c)) d->demoFill(); }
} // namespace cwui
