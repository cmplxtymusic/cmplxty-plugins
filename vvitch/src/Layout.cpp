// VVITCH: control layout + the display: the output scope (between thin crosses), the filter response at its live cutoff, the vox
// formant / pitch pane (vowel, pitch drop + DRAG), and a strip with voices, LFO and output. Monochrome only.
#include "PluginEditor.h"

namespace vvui
{
const PanelSpec& panelSpec()
{
    // 5 rows of 114 px under a 110 px display; x runs 16..1084
    constexpr int r1 = 176, r2 = 296, r3 = 416, r4 = 536, r5 = 656, h = 114;
    static const PanelSpec s {
        "VVitch", "WITCH HOUSE POLYSYNTH  /  CHIP  /  TAPE  /  DRAG",
        { 16, 58, 1068, 110 },
        {
            { "Oscillator 1", nullptr, { 16, r1, 250, h },  { { 'k', "oct1", "OCTAVE" }, { 'k', "lvl1", "LEVEL" }, { 'c', "w1", "WAVE" } }, 104 },
            { "Oscillator 2", nullptr, { 274, r1, 330, h }, { { 'k', "semi2", "SEMI" }, { 'k', "fine2", "FINE" }, { 'k', "lvl2", "LEVEL" }, { 'c', "w2", "WAVE" } }, 104 },
            { "Pulse", nullptr, { 612, r1, 236, h },        { { 'k', "pw", "WIDTH" }, { 'k', "pwm", "PWM" }, { 'k', "pwmRate", "RATE" } } },
            { "Unison", nullptr, { 856, r1, 228, h },       { { 'k', "uni", "VOICES" }, { 'k', "detune", "DETUNE" }, { 'k', "width", "WIDTH" } } },

            { "Noise", nullptr, { 16, r2, 160, h },         { { 'k', "noise", "LEVEL" }, { 'k', "noiseTone", "TONE" } } },
            { "Choral Vox", nullptr, { 184, r2, 236, h },   { { 'k', "vox", "AMOUNT" }, { 'k', "vowel", "VOWEL" }, { 'k', "voxMove", "MOTION" } } },
            { "Filter", nullptr, { 428, r2, 440, h },       { { 'k', "cutoff", "CUTOFF" }, { 'k', "reso", "RESO" }, { 'k', "drive", "DRIVE" }, { 'k', "fEnv", "ENV" }, { 'k', "fKey", "KEY" }, { 'c', "fType", "TYPE" } }, 104 },
            { "Filter Env", nullptr, { 876, r2, 208, h },   { { 'k', "fA", "A" }, { 'k', "fD", "D" }, { 'k', "fS", "S" } } },

            { "Amp Env", nullptr, { 16, r3, 296, h },       { { 'k', "ampA", "A" }, { 'k', "ampD", "D" }, { 'k', "ampS", "S" }, { 'k', "ampR", "R" } } },
            { "Pitch Drop", nullptr, { 320, r3, 160, h },   { { 'k', "drop", "DROP" }, { 'k', "dropTime", "TIME" } } },
            { "Drag", "drag", { 488, r3, 120, h },          { { 'k', "dragAmt", "DOWN" } } },
            { "Tape", nullptr, { 616, r3, 236, h },         { { 'k', "wow", "WOW" }, { 'k', "flutter", "FLUTTER" }, { 'k', "age", "AGE" } } },
            { "Chip", nullptr, { 860, r3, 224, h },         { { 'k', "bits", "BITS" }, { 'k', "crushHz", "RATE" }, { 'k', "chipMix", "MIX" } } },

            { "Motion", nullptr, { 16, r4, 340, h },        { { 'k', "lfoRate", "RATE" }, { 'k', "lfoCut", "CUTOFF" }, { 'k', "lfoPitch", "PITCH" }, { 'c', "lfoShape", "SHAPE" } }, 104 },
            { "Arp", "arp", { 364, r4, 420, h },            { { 'k', "arpOct", "OCTAVES" }, { 'k', "arpGate", "GATE" }, { 'c', "arpMode", "MODE" }, { 'c', "arpDiv", "RATE" } }, 104 },
            { "Voice", nullptr, { 792, r4, 292, h },        { { 'k', "voices", "VOICES" }, { 'k', "glide", "GLIDE" }, { 'k', "volume", "VOLUME" } } },

            { "Delay", nullptr, { 16, r5, 340, h },         { { 'k', "dlyTime", "TIME" }, { 'k', "dlyFb", "FEEDBACK" }, { 'k', "dlyMix", "MIX" } } },
            { "Reverb", nullptr, { 364, r5, 340, h },       { { 'k', "revSize", "SIZE" }, { 'k', "revDark", "DARK" }, { 'k', "revMix", "MIX" } } },
        },
        "cutoff"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff0a0a0a), cLine (0xff262626), cGrid (0xff1a1a1a), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff4a4a4a);

class SynthDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit SynthDisplay (VvitchProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto P = proc.currentParams();
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (10, 8);
        const int w0 = 380, w1 = 250, w2 = 200, w3 = r.getWidth() - w0 - w1 - w2 - 24;
        auto a = r.removeFromLeft (w0); r.removeFromLeft (8);
        auto b = r.removeFromLeft (w1); r.removeFromLeft (8);
        auto c = r.removeFromLeft (w2); r.removeFromLeft (8);
        drawScope (g, a);
        drawFilter (g, b, P);
        drawVox (g, c, P);
        drawStrip (g, r.withWidth (w3), P);
    }

    void demoFill()
    {
        for (size_t i = 0; i < scope.size(); ++i)
        {
            const float t = (float) i / (float) scope.size();
            scope[i] = 0.55f * std::sin (t * 6.2831853f * 3.0f) + 0.25f * std::sin (t * 6.2831853f * 7.0f + 1.0f) * std::sin (t * 3.14159f);
        }
        cut = 1800; vowel = 0.35f; lfo = 0.4f; pitch = -5; voices = 6; outL = 0.6f; demo = true; repaint();
    }

private:
    void frame (juce::Graphics& g, juce::Rectangle<int> a, const juce::String& t, const juce::String& v)
    {
        g.setColour (cLine); g.drawRect (a);
        g.setFont (uiFont (9.5f, true)); g.setColour (cDim);
        g.drawText (t, a.reduced (6, 3), juce::Justification::topLeft);
        g.setColour (cPri); g.drawText (v, a.reduced (6, 3), juce::Justification::topRight);
    }
    void drawScope (juce::Graphics& g, juce::Rectangle<int> a)
    {
        frame (g, a, "OUTPUT", juce::String (juce::Decibels::gainToDecibels (outL, -60.0f), 1) + " dB");
        auto c = a.toFloat().reduced (28.0f, 16.0f); c.removeFromTop (2);
        g.setColour (cGrid); g.drawHorizontalLine ((int) c.getCentreY(), c.getX(), c.getRight());
        g.setColour (cDim.withAlpha (0.7f));
        drawCross (g, { (float) a.getX() + 14.0f, (float) a.getCentreY() + 4.0f }, 18.0f, 1.0f);
        drawCross (g, { (float) a.getRight() - 14.0f, (float) a.getCentreY() + 4.0f }, 18.0f, 1.0f, true);
        // align the trace on a rising zero crossing so it stands still
        const int N = (int) scope.size(); int start = 0;
        for (int i = 1; i < N / 2; ++i) if (scope[(size_t) i - 1] <= 0 && scope[(size_t) i] > 0) { start = i; break; }
        juce::Path p; const int len = N / 2;
        for (int i = 0; i < len; ++i)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, scope[(size_t) ((start + i) % N)]);
            const float x = c.getX() + c.getWidth() * (float) i / (float) (len - 1), y = c.getCentreY() - v * c.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (cPri.withAlpha (0.18f)); g.strokePath (p, juce::PathStrokeType (4.0f));
        g.setColour (cPri); g.strokePath (p, juce::PathStrokeType (1.3f));
    }
    void drawFilter (juce::Graphics& g, juce::Rectangle<int> a, const vv::dsp::Params& P)
    {
        static const char* types[] = { "LP 24", "LP 12", "BAND", "HIGH" };
        const float fc = demo || voices > 0 ? cut : P.cutoff;
        frame (g, a, juce::String ("FILTER  /  ") + types[juce::jlimit (0, 3, (int) P.fType)], juce::String (juce::roundToInt (fc)) + " Hz");
        auto c = a.toFloat().reduced (10.0f, 16.0f); c.removeFromTop (4);
        auto xOf = [&] (float hz) { return c.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * c.getWidth(); };
        g.setColour (cGrid); for (float hz : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) xOf (hz), c.getY(), c.getBottom());
        const float q = 0.5f + P.reso * 6.0f;
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / 120.0f), w = hz / std::max (20.0f, fc);
            const float w2 = w * w, den = std::sqrt ((1 - w2) * (1 - w2) + w2 / (q * q));
            float m = 1;
            switch (P.fType) { case vv::dsp::FilterType::lp12: m = 1 / den; break; case vv::dsp::FilterType::lp24: m = 1 / (den * den) * q; break;
                               case vv::dsp::FilterType::bp: m = (w / q) / den; break; default: m = w2 / den; }
            const float db = juce::jlimit (-36.0f, 18.0f, juce::Decibels::gainToDecibels (m, -60.0f));
            const float y = c.getY() + (18.0f - db) / 54.0f * c.getHeight();
            if (i == 0) p.startNewSubPath (c.getX(), y); else p.lineTo (c.getX() + c.getWidth() * (float) i / 120.0f, y);
        }
        g.setColour (cPri); g.strokePath (p, juce::PathStrokeType (1.4f));
        const float xc = juce::jlimit (c.getX(), c.getRight(), xOf (juce::jlimit (20.0f, 20000.0f, fc)));
        g.setColour (cSec); drawTriangle (g, { xc, c.getBottom() - 3.0f }, 7.0f, 1.0f);
    }
    void drawVox (juce::Graphics& g, juce::Rectangle<int> a, const vv::dsp::Params& P)
    {
        frame (g, a, "VOX  /  PITCH", P.drag ? "DRAG" : "");
        auto c = a.reduced (10, 18); c.removeFromTop (2);
        auto row = c.removeFromTop (26);
        static const char* vowels[] = { "A", "E", "I", "O", "U" };
        const float v = vowel * 4.0f;
        const int cw = row.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
        {
            const float closeness = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (v - (float) i));
            const bool on = P.vox > 0.01f || demo;
            g.setColour ((on ? cPri : cOff).withAlpha (0.25f + 0.75f * closeness));
            g.setFont (gothFont (20.0f));
            g.drawText (vowels[i], row.getX() + i * cw, row.getY(), cw, row.getHeight(), juce::Justification::centred);
        }
        c.removeFromTop (6);
        // pitch meter: -24..+24 st, centre line; the live offset as a downward triangle
        auto m = c.removeFromTop (14).toFloat();
        g.setColour (cGrid); g.fillRect (m.withHeight (1.0f).withY (m.getCentreY()));
        for (int st = -24; st <= 24; st += 12) { const float x = m.getX() + (st + 24) / 48.0f * m.getWidth(); g.setColour (st == 0 ? cDim : cOff); g.fillRect (x, m.getY() + 3, 1.0f, m.getHeight() - 6); }
        const float pv = demo || voices > 0 ? pitch : -(P.drag ? P.dragAmt : 0.0f);
        const float px = m.getX() + juce::jlimit (0.0f, 1.0f, (pv + 24.0f) / 48.0f) * m.getWidth();
        g.setColour (cPri); drawTriangle (g, { px, m.getCentreY() }, 8.0f, 1.2f, true);
        g.setFont (uiFont (9.5f, true)); g.setColour (cDim);
        g.drawText (juce::String (pv, 1) + " st", c.removeFromTop (14), juce::Justification::centred);
    }
    void drawStrip (juce::Graphics& g, juce::Rectangle<int> a, const vv::dsp::Params& P)
    {
        frame (g, a, "VOICES", juce::String (voices) + " / " + juce::String (P.voices));
        auto c = a.reduced (10, 20); c.removeFromTop (2);
        // the voices as a row of small crosses, lit when sounding
        auto vr = c.removeFromTop (18);
        const float step = (float) vr.getWidth() / (float) vv::dsp::kMaxVoices;
        for (int i = 0; i < vv::dsp::kMaxVoices; ++i)
        {
            g.setColour (i < voices ? cPri : i < P.voices ? cOff : cGrid);
            drawCross (g, { vr.getX() + step * (i + 0.5f), (float) vr.getCentreY() }, 12.0f, 1.0f);
        }
        c.removeFromTop (6);
        auto bar = [&] (const char* name, float val, bool bipolar)
        {
            auto row = c.removeFromTop (16); c.removeFromTop (4);
            g.setFont (uiFont (9.5f, true)); g.setColour (cDim); g.drawText (name, row.removeFromLeft (40), juce::Justification::centredLeft);
            auto b = row.toFloat().reduced (0, 5); g.setColour (cGrid); g.fillRect (b);
            const float x0 = bipolar ? b.getCentreX() : b.getX(), x1 = bipolar ? b.getCentreX() + juce::jlimit (-1.0f, 1.0f, val) * b.getWidth() * 0.5f : b.getX() + juce::jlimit (0.0f, 1.0f, val) * b.getWidth();
            g.setColour (cSec); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, x1), b.getY(), std::max (x0, x1), b.getBottom()));
        };
        bar ("LFO", lfo, true);
        bar ("OUT", juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (outL, -60.0f) + 60.0f) / 60.0f), false);
    }
    void timerCallback() override
    {
        if (demo) return;
        const auto& e = proc.dspEngine();
        const int pos = e.scopePos.load (std::memory_order_relaxed);
        for (size_t i = 0; i < scope.size(); ++i) scope[i] = e.scope[(size_t) ((pos + (int) i) % vv::dsp::kScope)].load (std::memory_order_relaxed);
        cut = e.dispCut.load (std::memory_order_relaxed); vowel = e.dispVowel.load (std::memory_order_relaxed);
        lfo = e.dispLfo.load (std::memory_order_relaxed); pitch = e.dispPitch.load (std::memory_order_relaxed);
        voices = e.activeVoices.load (std::memory_order_relaxed);
        outL = std::max (proc.outLevel(), outL * 0.85f);
        repaint();
    }
    VvitchProcessor& proc;
    std::array<float, vv::dsp::kScope> scope {};
    float cut = 1000, vowel = 0, lfo = 0, pitch = 0, outL = 0;
    int voices = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (VvitchProcessor& p, Panel&) { return std::make_unique<SynthDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<SynthDisplay*> (c)) d->demoFill(); }
} // namespace vvui
