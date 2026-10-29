// Dark Reverb: control layout + the display (decay envelope, scrolling tail level, input / wet meters, freeze state).
#include "PluginEditor.h"

namespace drui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY DARK REVERB", "SPACE  /  METAL  /  SHIMMER  /  FREEZE",
        { 16, 56, 868, 228 },
        {
            { "SPACE",   nullptr,  { 16, 292, 430, 118 },  { { 'k', "preDelay", "PRE-DELAY" }, { 'k', "size", "SIZE" }, { 'k', "decay", "DECAY" }, { 'k', "diffusion", "DIFFUSION" } } },
            { "TONE",    nullptr,  { 454, 292, 430, 118 }, { { 'k', "dark", "DARK" }, { 'k', "lowCut", "LOW CUT" }, { 'k', "metal", "METAL" }, { 'k', "mod", "MOD" } } },
            { "SHIMMER", "shimOn", { 16, 418, 300, 118 },  { { 'k', "shimAmt", "AMOUNT" }, { 'c', "shimPitch", "PITCH" } }, 140 },
            { "OUTPUT",  nullptr,  { 324, 418, 560, 118 }, { { 't', "freeze", "FREEZE" }, { 'k', "width", "WIDTH" }, { 'k', "mix", "MIX" }, { 'k', "gain", "OUTPUT" } }, 120 },
        },
        "decay"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cIn (0xff6e6e6e);

class ReverbDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit ReverbDisplay (DarkReverbProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto P = proc.currentParams();
        const float frz = demo ? demoFrz : proc.freezeAmount();
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (12, 10);
        auto meters = r.removeFromRight (90); r.removeFromRight (14);
        auto readout = r.removeFromTop (30); r.removeFromTop (6);

        g.setFont (uiFont (24.0f, true)); g.setColour (cPri);
        g.drawText (frz > 0.5f ? juce::String ("FROZEN") : juce::String (P.decayS, P.decayS < 10 ? 1 : 0) + " s", readout.removeFromLeft (130), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f, true)); g.setColour (cDim);
        juce::String sub; sub << "PRE " << juce::String (P.preDelayMs, 0) << " ms   SIZE " << juce::roundToInt (P.size * 100) << "%   DARK "
                              << (P.darkHz >= 1000 ? juce::String (P.darkHz / 1000.0f, 1) + "k" : juce::String (P.darkHz, 0)) << " Hz   METAL " << juce::roundToInt (P.metal * 100) << "%";
        if (P.shimOn) sub << "   SHIMMER " << (P.shimSemis > 0 ? "+" : "") << juce::String ((int) P.shimSemis) << " st";
        g.drawText (sub, readout, juce::Justification::centredLeft);

        // left 55 %: envelope (pre-delay, then exponential decay; flat when frozen). right: scrolling wet level.
        auto env = r.removeFromLeft ((int) (r.getWidth() * 0.55f)); r.removeFromLeft (10);
        g.setColour (cLine); g.drawRect (env); g.drawRect (r);
        const float span = juce::jmax (2.0f, P.decayS * 1.2f + P.preDelayMs * 0.001f);
        g.setFont (uiFont (9.5f)); 
        for (int i = 1; i < 4; ++i)
        {
            const int x = env.getX() + env.getWidth() * i / 4;
            g.setColour (cGrid); g.drawVerticalLine (x, (float) env.getY(), (float) env.getBottom());
            g.setColour (cDim.withAlpha (0.7f)); g.drawText (juce::String (span * i / 4.0f, 1) + " s", x + 3, env.getBottom() - 13, 50, 11, juce::Justification::left);
        }
        juce::Path p; const auto ef = env.toFloat().reduced (2.0f, 4.0f);
        const float preX = ef.getX() + ef.getWidth() * (P.preDelayMs * 0.001f / span);
        p.startNewSubPath (ef.getX(), ef.getBottom()); p.lineTo (preX, ef.getBottom());
        for (int i = 0; i <= 120; ++i)
        {
            const float t = (float) i / 120.0f * (span - P.preDelayMs * 0.001f);
            const float db = frz > 0.5f ? 0.0f : -60.0f * t / P.decayS;
            p.lineTo (preX + (ef.getRight() - preX) * (float) i / 120.0f, ef.getY() + juce::jlimit (0.0f, 1.0f, -db / 72.0f) * ef.getHeight());
        }
        auto fillP = p; fillP.lineTo (ef.getRight(), ef.getBottom()); fillP.closeSubPath();
        g.setColour (cSec.withAlpha (0.12f)); g.fillPath (fillP);
        g.setColour (cPri); g.strokePath (p, juce::PathStrokeType (2.0f));
        g.setColour (cDim); g.setFont (uiFont (9.5f, true)); g.drawText ("DECAY ENVELOPE", env.reduced (6, 3), juce::Justification::topRight);

        juce::Path h; bool first = true;
        for (int i = 0; i < kHist; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (hist[(size_t) i], -60.0f);
            const float x = (float) r.getX() + (float) i / (float) (kHist - 1) * (float) r.getWidth();
            const float y = (float) r.getBottom() - 2.0f - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) (r.getHeight() - 4);
            if (first) h.startNewSubPath (x, y); else h.lineTo (x, y);
            first = false;
        }
        g.setColour (cSec); g.strokePath (h, juce::PathStrokeType (1.5f));
        g.setColour (cDim); g.drawText ("TAIL", r.reduced (6, 3), juce::Justification::topRight);

        // meters
        auto meter = [&] (juce::Rectangle<int> m, float lvl, const char* name)
        {
            g.setColour (cDim); g.setFont (uiFont (9.5f, true));
            g.drawText (name, m.removeFromBottom (14), juce::Justification::centred);
            g.setColour (cGrid); g.fillRect (m);
            const float hh = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lvl, -60.0f) + 60.0f) / 60.0f) * (float) m.getHeight();
            g.setColour (cSec.withAlpha (0.85f)); g.fillRect ((float) m.getX(), (float) m.getBottom() - hh, (float) m.getWidth(), hh);
            g.setColour (cPri); g.fillRect ((float) m.getX(), (float) m.getBottom() - hh, (float) m.getWidth(), juce::jmin (2.0f, hh));
        };
        auto m1 = meters.removeFromLeft (40); meters.removeFromLeft (10);
        meter (m1, inL, "IN"); meter (meters, wetL, "WET");
        if (frz > 0.01f) { g.setColour (cPri.withAlpha (frz)); g.drawRect (getLocalBounds().reduced (3), 2); }
    }

    void demoFill()   // screenshot: a long tail after a few hits
    {
        for (int i = 0; i < kHist; ++i)
        {
            const float t = (float) i / kHist;
            const float hit = std::fmod (t * 3.0f, 1.0f);
            hist[(size_t) i] = 0.4f * std::exp (-hit * 3.5f) + 0.02f;
        }
        inL = 0.25f; wetL = 0.18f; demoFrz = 0.0f; demo = true; repaint();
    }

private:
    void timerCallback() override
    {
        if (demo) return;
        std::rotate (hist.begin(), hist.begin() + 1, hist.end()); hist.back() = proc.wetLevel();
        inL = std::max (proc.inLevel(), inL * 0.85f); wetL = std::max (proc.wetLevel(), wetL * 0.85f);
        repaint();
    }
    static constexpr int kHist = 150;
    DarkReverbProcessor& proc;
    std::vector<float> hist = std::vector<float> (kHist, 0.0f);
    float inL = 0, wetL = 0, demoFrz = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (DarkReverbProcessor& p, Panel&) { return std::make_unique<ReverbDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<ReverbDisplay*> (c)) d->demoFill(); }
} // namespace drui
