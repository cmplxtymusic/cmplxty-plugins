// Neon FX: control layout + the performance display: the 16-step gate pattern with the playing step, two hold pads (STUTTER and
// TAPE STOP: active while the mouse is held; the section switch latches them), the tape speed, the filter LFO and the output level.
#include "PluginEditor.h"

namespace cwui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY NEON FX", "PERFORMANCE FX: STUTTER / GATE / TAPE STOP / FILTER / DRIVE",
        { 16, 56, 1068, 170 },
        {
            { "DRIVE", "drOn", { 16, 234, 530, 118 }, { { 'k', "drDrive", "DRIVE" }, { 'k', "drTone", "TONE" }, { 'c', "drMode", "MODE" } }, 130 },
            { "FILTER", "flOn", { 554, 234, 530, 118 }, { { 'k', "flCut", "CUTOFF" }, { 'k', "flReso", "RESO" }, { 'k', "flVowel", "VOWEL" }, { 'k', "flLfoDepth", "LFO DEPTH" }, { 'c', "flMode", "MODE" }, { 'c', "flLfoRate", "LFO RATE" } }, 130 },
            { "TRANCE GATE", "gtOn", { 16, 358, 530, 118 }, { { 'k', "gtDepth", "DEPTH" }, { 'k', "gtSmooth", "SMOOTH" }, { 'c', "gtRate", "RATE" }, { 'c', "gtPattern", "PATTERN" } }, 130 },
            { "STUTTER", "stOn", { 554, 358, 530, 118 }, { { 'k', "stGate", "GATE" }, { 'k', "stDecay", "DECAY" }, { 'c', "stLen", "LENGTH" } }, 130 },
            { "TAPE STOP", "tsOn", { 16, 482, 530, 118 }, { { 'k', "tsStop", "STOP" }, { 'k', "tsStart", "START" } }, 130 },
            { "MACROS", nullptr, { 554, 482, 530, 118 }, { { 'k', "djFilter", "DJ FILTER" }, { 'k', "intensity", "INTENSITY" }, { 'k', "mix", "MIX" }, { 'k', "output", "OUTPUT" } }, 130 },
        },
        "flCut"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff3a3a3a), cAcc (0xffffe600);

class PerfDisplay final : public juce::Component, private juce::Timer
{
public:
    PerfDisplay (NeonFXProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        const auto P = proc.currentParams();
        const auto& e = proc.dspEngine();
        // ---- gate pattern
        {
            auto r = gateArea().toFloat();
            g.setFont (uiFont (10.5f, true)); g.setColour (P.gtOn ? cPri : cDim);
            g.drawText ("GATE  " + juce::String (gateName (P.gtPattern)).toUpperCase(), r.removeFromTop (16), juce::Justification::centredLeft);
            const char* pat = cw::dsp::gatePattern (P.gtPattern);
            const int cur = demo ? 5 : e.stepNow.load (std::memory_order_relaxed);
            const float w = (r.getWidth() - 15 * 4.0f) / 16.0f;
            for (int i = 0; i < 16; ++i)
            {
                auto c = juce::Rectangle<float> (r.getX() + i * (w + 4.0f), r.getY() + 8, w, r.getHeight() - 30);
                const bool on = pat[i] == '1';
                g.setColour (on ? (P.gtOn ? cSec : cOff.brighter (0.3f)) : juce::Colour (0xff1a1a1a)); g.fillRoundedRectangle (c, 2.5f);
                if (P.gtOn && i == cur) { g.setColour (on ? cAcc : cAcc.withAlpha (0.4f)); g.drawRoundedRectangle (c.expanded (1.5f), 3.0f, 1.5f); if (on) g.fillRoundedRectangle (c, 2.5f); }
            }
            g.setFont (uiFont (10.0f)); g.setColour (cDim);
            g.drawText (juce::String (proc.hostBpm.load(), 1) + " BPM" + (proc.hostPlaying.load() ? "" : "  (free)"), r.removeFromBottom (14), juce::Justification::centredLeft);
        }
        // ---- hold pads
        for (int i = 0; i < 2; ++i)
        {
            auto a = padArea (i).toFloat();
            const bool on = i == 0 ? (demo ? true : P.stOn) : (demo ? false : P.tsOn);
            const bool live = i == 0 ? e.stutterOn.load() || e.stutterCapturing.load() : e.speedNow.load() < 0.999f;
            g.setColour (on ? juce::Colour (0xff2a2700) : juce::Colour (0xff181818)); g.fillRoundedRectangle (a, 5.0f);
            g.setColour (on || live ? cAcc : cLine.brighter (0.4f)); g.drawRoundedRectangle (a.reduced (0.5f), 5.0f, on ? 2.0f : 1.0f);
            g.setFont (uiFont (14.0f, true)); g.setColour (on ? cAcc : cPri);
            g.drawText (i == 0 ? "STUTTER" : "TAPE STOP", a.withTrimmedBottom (18), juce::Justification::centred);
            g.setFont (uiFont (10.0f)); g.setColour (cDim);
            const juce::String sub = i == 0 ? juce::String (lenName (P.stLen)) + "  HOLD" : "HOLD";
            g.drawText (sub, a.withTrimmedTop (a.getHeight() * 0.5f + 6), juce::Justification::centredTop);
        }
        // ---- meters: tape speed, filter LFO, output
        {
            auto r = meterArea().toFloat();
            auto row = [&] (const juce::String& label, float v, bool active, bool bip)
            {
                auto l = r.removeFromTop (34);
                g.setFont (uiFont (10.0f, true)); g.setColour (active ? cSec : cDim);
                g.drawText (label, l.removeFromTop (14), juce::Justification::centredLeft);
                auto bar = l.removeFromTop (8);
                g.setColour (juce::Colour (0xff1c1c1c)); g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (active ? cPri : cOff);
                if (bip) { const float c = bar.getCentreX(), x = c + v * bar.getWidth() * 0.5f; g.fillRoundedRectangle (juce::Rectangle<float> (std::min (c, x), bar.getY(), std::abs (x - c) + 1.0f, bar.getHeight()), 2.0f); }
                else g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, v)), 2.0f);
            };
            const float sp = demo ? 1.0f : e.speedNow.load();
            row ("TAPE SPEED  " + juce::String (juce::roundToInt (sp * 100)) + "%", sp, P.tsOn || sp < 0.999f, false);
            row ("FILTER LFO", P.flOn && P.flLfoDepth > 0 ? (demo ? 0.5f : e.lfoOut.load()) * P.flLfoDepth : 0.0f, P.flOn && P.flLfoDepth > 0, true);
            const float pk = demo ? 0.7f : proc.outLevel();
            const float db = pk > 1e-5f ? 20.0f * std::log10 (pk) : -60.0f;
            row ("OUTPUT  " + (db <= -59.0f ? juce::String ("-inf") : juce::String (db, 1)) + " dB", juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, 1.0f), true, false);
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        const int i = padAt (ev.getPosition());
        if (ev.mods.isPopupMenu()) { showMenu (i, ev.getPosition()); return; }
        if (i < 0) return;
        held = i; setBool (i == 0 ? "stOn" : "tsOn", true);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (held >= 0) setBool (held == 0 ? "stOn" : "tsOn", false);
        held = -1; cwui::hostkeys::releaseLater (*this);
    }
    void demoFill() { demo = true; repaint(); }
    bool holdPad (int i, bool down) { if (i < 0 || i > 1) return false; if (down) { held = i; setBool (i == 0 ? "stOn" : "tsOn", true); } else { setBool (i == 0 ? "stOn" : "tsOn", false); held = -1; } return true; }

private:
    static const char* gateName (int i) { static const char* n[] = { "Straight", "Halves", "Offbeat", "Trance 1", "Trance 2", "Syncopate", "Build", "Stair" }; return n[juce::jlimit (0, 7, i)]; }
    static const char* lenName (int i) { static const char* n[] = { "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T" }; return n[juce::jlimit (0, 7, i)]; }
    juce::Rectangle<int> gateArea() const { return { 14, 10, 470, getHeight() - 20 }; }
    juce::Rectangle<int> padArea (int i) const { return { 500 + i * 158, 14, 148, getHeight() - 28 }; }
    juce::Rectangle<int> meterArea() const { return { 828, 14, getWidth() - 842, getHeight() - 28 }; }
    int padAt (juce::Point<int> p) const { for (int i = 0; i < 2; ++i) if (padArea (i).contains (p)) return i; return -1; }
    void setBool (const char* id, bool v)
    {
        if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (v ? 1.0f : 0.0f); p->endChangeGesture(); }
        repaint();
    }
    void showMenu (int i, juce::Point<int> pos)
    {
        std::vector<PopupOverlay::Item> v;
        const auto P = proc.currentParams();
        if (i != 1) v.push_back ({ P.stOn ? "Release &stutter" : "Latch &stutter", [this, on = P.stOn] { setBool ("stOn", ! on); } });
        if (i != 0) v.push_back ({ P.tsOn ? "Release &tape stop" : "Latch &tape stop", [this, on = P.tsOn] { setBool ("tsOn", ! on); } });
        v.push_back ({ "Release &all", [this] { setBool ("stOn", false); setBool ("tsOn", false); } });
        panel.popupOverlay().show (pos, std::move (v));
    }
    void timerCallback() override { if (! demo) repaint(); }

    NeonFXProcessor& proc;
    Panel& panel;
    int held = -1;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (NeonFXProcessor& p, Panel& pn) { return std::make_unique<PerfDisplay> (p, pn); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<PerfDisplay*> (c)) d->demoFill(); }
} // namespace cwui
