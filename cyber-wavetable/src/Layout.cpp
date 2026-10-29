// Cyber Wavetable: control layout + the display: both oscillators' current wavetable frame (with the neighbouring frames
// stacked behind it), the filter response at its live cutoff, and a modulation strip (LFOs, mod envelope, voices, output).
#include "PluginEditor.h"

namespace cwui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY CYBER WAVETABLE", "2-OSC MORPHING WAVETABLE SYNTH",
        { 16, 56, 1068, 140 },
        {
            { "OSC A", "aOn", { 16, 204, 530, 118 },  { { 'k', "aPos", "POSITION" }, { 'k', "aSemi", "SEMI" }, { 'k', "aFine", "FINE" }, { 'k', "aUni", "UNISON" }, { 'k', "aDetune", "DETUNE" }, { 'k', "aLevel", "LEVEL" }, { 'c', "aBank", "TABLE" } }, 110 },
            { "OSC B", "bOn", { 554, 204, 530, 118 }, { { 'k', "bPos", "POSITION" }, { 'k', "bSemi", "SEMI" }, { 'k', "bFine", "FINE" }, { 'k', "bUni", "UNISON" }, { 'k', "bDetune", "DETUNE" }, { 'k', "bLevel", "LEVEL" }, { 'c', "bBank", "TABLE" } }, 110 },
            { "MIX", nullptr, { 16, 330, 250, 118 },   { { 'k', "sub", "SUB" }, { 'k', "noise", "NOISE" }, { 'k', "width", "WIDTH" } } },
            { "FILTER", nullptr, { 274, 330, 520, 118 }, { { 'k', "cutoff", "CUTOFF" }, { 'k', "reso", "RESO" }, { 'k', "fDrive", "DRIVE" }, { 'k', "fEnv", "ENV" }, { 'k', "fKey", "KEY" }, { 'c', "fType", "TYPE" } }, 110 },
            { "AMP ENV", nullptr, { 802, 330, 282, 118 }, { { 'k', "ampA", "A" }, { 'k', "ampD", "D" }, { 'k', "ampS", "S" }, { 'k', "ampR", "R" } } },
            { "MOD ENV", nullptr, { 16, 456, 300, 118 }, { { 'k', "modA", "A" }, { 'k', "modD", "D" }, { 'k', "modS", "S" }, { 'k', "modR", "R" } } },
            { "LFO 1", nullptr, { 324, 456, 250, 118 }, { { 'k', "l1Rate", "RATE" }, { 'c', "l1Shape", "SHAPE" } }, 120 },
            { "LFO 2", nullptr, { 582, 456, 250, 118 }, { { 'k', "l2Rate", "RATE" }, { 'c', "l2Shape", "SHAPE" } }, 120 },
            { "VOICE", nullptr, { 840, 456, 244, 118 }, { { 'k', "voices", "VOICES" }, { 'k', "glide", "GLIDE" }, { 'k', "volume", "VOLUME" } } },
            { "MOD 1", nullptr, { 16, 582, 262, 118 },  { { 'k', "m1Amt", "AMOUNT" }, { 'c', "m1Src", "SOURCE" }, { 'c', "m1Dst", "TARGET" } }, 140 },
            { "MOD 2", nullptr, { 286, 582, 262, 118 }, { { 'k', "m2Amt", "AMOUNT" }, { 'c', "m2Src", "SOURCE" }, { 'c', "m2Dst", "TARGET" } }, 140 },
            { "MOD 3", nullptr, { 556, 582, 262, 118 }, { { 'k', "m3Amt", "AMOUNT" }, { 'c', "m3Src", "SOURCE" }, { 'c', "m3Dst", "TARGET" } }, 140 },
            { "MOD 4", nullptr, { 826, 582, 258, 118 }, { { 'k', "m4Amt", "AMOUNT" }, { 'c', "m4Src", "SOURCE" }, { 'c', "m4Dst", "TARGET" } }, 140 },
        },
        "aPos"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff4a4a4a);
const char* kBankNames[] = { "ANALOG", "DIGITAL", "VOCAL", "SYNC", "METAL", "CHIP" };

class SynthDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit SynthDisplay (CyberWavetableProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto P = proc.currentParams();
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (10, 8);
        const int pw = (r.getWidth() - 24) / 4;
        auto pane = [&] (int i) { return juce::Rectangle<int> (r.getX() + i * (pw + 8), r.getY(), pw, r.getHeight()); };
        drawWave (g, pane (0), "OSC A", P.a, posA);
        drawWave (g, pane (1), "OSC B", P.b, posB);
        drawFilter (g, pane (2), P);
        drawMod (g, pane (3));
    }

    void demoFill() { posA = 0.42f; posB = 0.7f; cut = 2600; l1 = 0.6f; l2 = -0.3f; env = 0.8f; voices = 6; outL = 0.7f; demo = true; repaint(); }

private:
    void title (juce::Graphics& g, juce::Rectangle<int> a, const juce::String& t, const juce::String& v, bool on)
    {
        g.setColour (cLine); g.drawRect (a);
        g.setFont (uiFont (9.5f, true)); g.setColour (on ? cDim : cOff);
        g.drawText (t, a.reduced (6, 3), juce::Justification::topLeft);
        g.setColour (on ? cPri : cOff); g.drawText (v, a.reduced (6, 3), juce::Justification::topRight);
    }
    void drawWave (juce::Graphics& g, juce::Rectangle<int> a, const char* name, const cw::dsp::OscP& o, float pos)
    {
        const int bank = juce::jlimit (0, cw::dsp::kBanks - 1, o.bank);
        title (g, a, juce::String (name) + "  /  " + kBankNames[bank], juce::String (juce::roundToInt (pos * 100.0f)) + " %", o.on);
        auto c = a.toFloat().reduced (10.0f, 18.0f); c.removeFromTop (2);
        // the neighbouring frames, offset up-right as a stack, then the live frame on top
        const int N = 96;
        for (int f = cw::dsp::kFrames - 1; f >= -1; --f)
        {
            const bool live = f < 0;
            const float fp = live ? pos : (float) f / (float) (cw::dsp::kFrames - 1);
            const float dx = (live ? pos : fp) * 18.0f, dy = -(live ? pos : fp) * 14.0f;
            auto box = c.withTrimmedRight (18).withTrimmedTop (14).translated (dx, dy + 0);
            juce::Path p;
            for (int i = 0; i <= N; ++i)
            {
                const float v = cw::dsp::sampleAt (bank, fp, 3, (double) i / N);
                const float x = box.getX() + box.getWidth() * (float) i / N, y = box.getCentreY() - v * box.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (! o.on ? cOff.withAlpha (live ? 0.9f : 0.25f) : live ? cPri : cDim.withAlpha (0.22f));
            g.strokePath (p, juce::PathStrokeType (live ? 1.6f : 0.8f));
        }
    }
    void drawFilter (juce::Graphics& g, juce::Rectangle<int> a, const cw::dsp::Params& P)
    {
        static const char* types[] = { "LP 12", "LP 24", "BAND", "HIGH" };
        const float fc = demo ? cut : (voices > 0 ? cut : P.cutoff);
        title (g, a, juce::String ("FILTER  /  ") + types[juce::jlimit (0, 3, (int) P.fType)], juce::String (juce::roundToInt (fc)) + " Hz", true);
        auto c = a.toFloat().reduced (10.0f, 18.0f);
        auto xOf = [&] (float hz) { return c.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * c.getWidth(); };
        g.setColour (cGrid); for (float hz : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) xOf (hz), c.getY(), c.getBottom());
        const float q = 0.5f + P.reso * 6.0f;
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / 120.0f), w = hz / std::max (20.0f, fc);
            const float w2 = w * w, den = std::sqrt ((1 - w2) * (1 - w2) + w2 / (q * q));
            float m = 1;
            switch (P.fType) { case cw::dsp::FilterType::lp12: m = 1 / den; break; case cw::dsp::FilterType::lp24: m = 1 / (den * den) * q; break;
                               case cw::dsp::FilterType::bp: m = (w / q) / den; break; default: m = w2 / den; }
            const float db = juce::jlimit (-36.0f, 18.0f, juce::Decibels::gainToDecibels (m, -60.0f));
            const float y = c.getY() + (18.0f - db) / 54.0f * c.getHeight();
            if (i == 0) p.startNewSubPath (c.getX(), y); else p.lineTo (c.getX() + c.getWidth() * (float) i / 120.0f, y);
        }
        g.setColour (cPri); g.strokePath (p, juce::PathStrokeType (1.5f));
    }
    void drawMod (juce::Graphics& g, juce::Rectangle<int> a)
    {
        title (g, a, "MOD  /  VOICES", juce::String (voices) + " / " + juce::String (juce::roundToInt (proc.currentParams().voices)), true);
        auto c = a.reduced (10, 20); c.removeFromTop (2);
        auto bar = [&] (const char* name, float v, bool bipolar)
        {
            auto row = c.removeFromTop (18); c.removeFromTop (4);
            g.setFont (uiFont (9.5f, true)); g.setColour (cDim); g.drawText (name, row.removeFromLeft (46), juce::Justification::centredLeft);
            auto b = row.toFloat().reduced (0, 5); g.setColour (cGrid); g.fillRect (b);
            const float x0 = bipolar ? b.getCentreX() : b.getX(), x1 = bipolar ? b.getCentreX() + juce::jlimit (-1.0f, 1.0f, v) * b.getWidth() * 0.5f : b.getX() + juce::jlimit (0.0f, 1.0f, v) * b.getWidth();
            g.setColour (cSec); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, x1), b.getY(), std::max (x0, x1), b.getBottom()));
        };
        bar ("LFO 1", l1, true); bar ("LFO 2", l2, true); bar ("ENV", env, false);
        bar ("OUT", juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (outL, -60.0f) + 60.0f) / 60.0f), false);
    }
    void timerCallback() override
    {
        if (demo) return;
        const auto& e = proc.dspEngine();
        posA = e.dispPosA.load (std::memory_order_relaxed); posB = e.dispPosB.load (std::memory_order_relaxed);
        l1 = e.dispLfo1.load (std::memory_order_relaxed); l2 = e.dispLfo2.load (std::memory_order_relaxed);
        env = e.dispEnv.load (std::memory_order_relaxed); cut = e.dispCut.load (std::memory_order_relaxed);
        voices = e.activeVoices.load (std::memory_order_relaxed);
        outL = std::max (proc.outLevel(), outL * 0.85f);
        repaint();
    }
    CyberWavetableProcessor& proc;
    float posA = 0.5f, posB = 0, l1 = 0, l2 = 0, env = 0, cut = 1000, outL = 0;
    int voices = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (CyberWavetableProcessor& p, Panel&) { return std::make_unique<SynthDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<SynthDisplay*> (c)) d->demoFill(); }
} // namespace cwui
