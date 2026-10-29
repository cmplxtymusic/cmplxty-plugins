// Mix Tools: control layout + the display: the signal chain in its current order, with one live view per module:
// WIDENER = vectorscope (mid up, side across) + correlation, SHAPER = transient gain over time, LIMITER = gain reduction
// over time + output level against the ceiling.
#include "PluginEditor.h"

namespace mtui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY MIX TOOLS", "WIDENER  /  TRANSIENT SHAPER  /  TRUE-PEAK LIMITER",
        { 16, 56, 868, 228 },
        {
            { "WIDENER",  "wOn", { 16, 292, 330, 118 },  { { 'k', "width", "WIDTH" }, { 'k', "monoBass", "MONO BASS" } } },
            { "SHAPER",   "tOn", { 354, 292, 530, 118 }, { { 'k', "attack", "ATTACK" }, { 'k', "sustain", "SUSTAIN" }, { 'c', "speed", "SPEED" } }, 150 },
            { "LIMITER",  "lOn", { 16, 418, 530, 118 },  { { 'k', "drive", "DRIVE" }, { 'k', "ceiling", "CEILING" }, { 'k', "release", "RELEASE" } } },
            { "OUTPUT",   nullptr, { 554, 418, 330, 118 }, { { 'k', "gain", "OUTPUT" }, { 'c', "order", "ORDER" } }, 170 },
        },
        "width"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cOff (0xff4a4a4a);

class ChainDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit ChainDisplay (MixToolsProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        const auto P = proc.currentParams();
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (12, 10);
        auto meters = r.removeFromRight (90); r.removeFromRight (14);
        auto head = r.removeFromTop (24); r.removeFromTop (6);

        // chain order header: IN > A > B > LIMIT > OUT
        const bool wf = P.order == mt::dsp::Order::widenFirst;
        const char* names[3] = { wf ? "WIDEN" : "SHAPE", wf ? "SHAPE" : "WIDEN", "LIMIT" };
        const bool on[3] = { wf ? P.wOn : P.tOn, wf ? P.tOn : P.wOn, P.lOn };
        g.setFont (uiFont (12.0f, true));
        int x = head.getX();
        auto word = [&] (const juce::String& s, juce::Colour c, bool box)
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (uiFont (12.0f, true), s) + (box ? 18 : 6);
            if (box) { g.setColour (c.withAlpha (0.12f)); g.fillRoundedRectangle ((float) x, (float) head.getY() + 2, (float) w, (float) head.getHeight() - 4, 3.0f);
                       g.setColour (c.withAlpha (0.6f)); g.drawRoundedRectangle ((float) x + 0.5f, (float) head.getY() + 2.5f, (float) w - 1, (float) head.getHeight() - 5, 3.0f, 1.0f); }
            g.setColour (c); g.drawText (s, x, head.getY(), w, head.getHeight(), juce::Justification::centred); x += w + 4;
        };
        word ("IN", cDim, false);
        for (int i = 0; i < 3; ++i) { word (">", cDim, false); word (juce::String (names[i]) + (on[i] ? "" : " (OFF)"), on[i] ? cPri : cOff, true); }
        word (">", cDim, false); word ("OUT", cDim, false);
        g.setFont (uiFont (11.0f, true)); g.setColour (cDim);
        g.drawText ("LATENCY " + juce::String (proc.getLatencySamples()) + " SAMPLES", head, juce::Justification::centredRight);

        // three panes
        const int pw = (r.getWidth() - 16) / 3;
        auto pane = [&] (int i) { return juce::Rectangle<int> (r.getX() + i * (pw + 8), r.getY(), pw, r.getHeight()); };
        const int order[2] = { wf ? 0 : 1, wf ? 1 : 0 };   // column of the widener / shaper view follows the chain order
        drawScope (g, pane (order[0]), P.wOn);
        drawShaper (g, pane (order[1]), P.tOn);
        drawLimiter (g, pane (2), P.lOn, P.ceilingDb);

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
        meter (m1, inL, "IN"); meter (meters, outL, "OUT");
    }

    void demoFill()   // screenshot: a drum-ish mix going through all three modules
    {
        juce::Random rnd (3);
        for (int i = 0; i < 128; ++i) { const float m = (rnd.nextFloat() * 2 - 1) * 0.6f; sM[(size_t) i] = m; sS[(size_t) i] = (rnd.nextFloat() * 2 - 1) * 0.28f; }
        for (int i = 0; i < mt::dsp::kHist; ++i)
        {
            const float ph = std::fmod ((float) i / 30.0f, 1.0f);
            hT[(size_t) i] = ph < 0.08f ? 6.0f * (1.0f - ph / 0.08f) : -2.0f * std::exp (-(ph - 0.08f) * 5.0f);
            hG[(size_t) i] = -5.0f * std::exp (-ph * 7.0f) - 0.6f;
            hO[(size_t) i] = 0.85f * (0.6f + 0.4f * std::exp (-ph * 5.0f));
        }
        corrNow = 0.62f; inL = 0.7f; outL = 0.85f; demo = true; repaint();
    }

private:
    void title (juce::Graphics& g, juce::Rectangle<int> a, const juce::String& t, const juce::String& v, bool on)
    {
        g.setColour (cLine); g.drawRect (a);
        g.setFont (uiFont (9.5f, true)); g.setColour (on ? cDim : cOff);
        g.drawText (t, a.reduced (6, 3), juce::Justification::topLeft);
        g.setColour (on ? cPri : cOff); g.drawText (v, a.reduced (6, 3), juce::Justification::topRight);
    }
    void drawScope (juce::Graphics& g, juce::Rectangle<int> a, bool on)
    {
        title (g, a, "WIDENER  /  VECTORSCOPE", "CORR " + juce::String (corrNow, 2), on);
        const auto c = a.toFloat().reduced (8.0f, 18.0f);
        const float rad = std::min (c.getWidth(), c.getHeight()) * 0.5f; const auto ctr = c.getCentre();
        g.setColour (cGrid); g.drawLine (ctr.x - rad, ctr.y, ctr.x + rad, ctr.y); g.drawLine (ctr.x, ctr.y - rad, ctr.x, ctr.y + rad);
        g.drawLine (ctr.x - rad * 0.7f, ctr.y - rad * 0.7f, ctr.x + rad * 0.7f, ctr.y + rad * 0.7f); g.drawLine (ctr.x + rad * 0.7f, ctr.y - rad * 0.7f, ctr.x - rad * 0.7f, ctr.y + rad * 0.7f);
        g.setFont (uiFont (9.0f, true)); g.setColour (cDim.withAlpha (0.6f));
        g.drawText ("L", (int) (ctr.x - rad * 0.75f) - 12, (int) (ctr.y - rad * 0.75f) - 6, 12, 12, juce::Justification::centred);
        g.drawText ("R", (int) (ctr.x + rad * 0.75f), (int) (ctr.y - rad * 0.75f) - 6, 12, 12, juce::Justification::centred);
        for (int i = 0; i < 128; ++i)
        {
            const float px = ctr.x + juce::jlimit (-1.0f, 1.0f, sS[(size_t) i] * 1.4f) * rad, py = ctr.y - juce::jlimit (-1.0f, 1.0f, sM[(size_t) i] * 1.4f) * rad;
            g.setColour ((on ? cPri : cOff).withAlpha (0.55f)); g.fillEllipse (px - 1.5f, py - 1.5f, 3.0f, 3.0f);
        }
        // correlation bar
        auto bar = a.reduced (10, 0).removeFromBottom (12).withTrimmedBottom (4).toFloat();
        g.setColour (cGrid); g.fillRect (bar);
        const float cx = bar.getCentreX(), vx = cx + juce::jlimit (-1.0f, 1.0f, corrNow) * bar.getWidth() * 0.5f;
        g.setColour (on ? cSec : cOff); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, vx), bar.getY(), std::max (cx, vx), bar.getBottom()));
        g.setColour (cDim); g.drawVerticalLine ((int) cx, bar.getY() - 2, bar.getBottom() + 2);
    }
    void drawShaper (juce::Graphics& g, juce::Rectangle<int> a, bool on)
    {
        const float now = hT[(size_t) head];
        title (g, a, "SHAPER  /  GAIN", (now > 0.05f ? "+" : "") + juce::String (now, 1) + " dB", on);
        const auto c = a.toFloat().reduced (8.0f, 20.0f);
        auto y = [&] (float db) { return c.getCentreY() - juce::jlimit (-12.0f, 12.0f, db) / 12.0f * c.getHeight() * 0.5f; };
        g.setColour (cGrid); for (float db : { -6.0f, 6.0f }) g.drawHorizontalLine ((int) y (db), c.getX(), c.getRight());
        g.setColour (cGrid.brighter (0.2f)); g.drawHorizontalLine ((int) y (0), c.getX(), c.getRight());
        g.setFont (uiFont (9.0f)); g.setColour (cDim.withAlpha (0.6f));
        g.drawText ("+6", (int) c.getX(), (int) y (6) - 11, 24, 10, juce::Justification::left); g.drawText ("-6", (int) c.getX(), (int) y (-6) + 1, 24, 10, juce::Justification::left);
        juce::Path pth; const int n = mt::dsp::kHist;
        for (int i = 0; i < n; ++i)
        {
            const float v = hT[(size_t) ((head + 1 + i) % n)], px = c.getX() + (float) i / (float) (n - 1) * c.getWidth();
            if (i == 0) pth.startNewSubPath (px, y (v)); else pth.lineTo (px, y (v));
        }
        g.setColour (on ? cPri : cOff); g.strokePath (pth, juce::PathStrokeType (1.5f));
    }
    void drawLimiter (juce::Graphics& g, juce::Rectangle<int> a, bool on, float ceilDb)
    {
        float worst = 0; for (float v : hG) worst = std::min (worst, v);
        title (g, a, "LIMITER  /  GR + OUTPUT", juce::String (worst, 1) + " dB  @ " + juce::String (ceilDb, 1) + " dBTP", on);
        const auto c = a.toFloat().reduced (8.0f, 20.0f);
        const int n = mt::dsp::kHist;
        auto yo = [&] (float lvl) { return c.getBottom() - juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lvl, -36.0f) + 36.0f) / 36.0f) * c.getHeight(); };
        juce::Path out;
        out.startNewSubPath (c.getX(), c.getBottom());
        for (int i = 0; i < n; ++i) out.lineTo (c.getX() + (float) i / (float) (n - 1) * c.getWidth(), yo (hO[(size_t) ((head + 1 + i) % n)]));
        out.lineTo (c.getRight(), c.getBottom()); out.closeSubPath();
        g.setColour ((on ? cSec : cOff).withAlpha (0.25f)); g.fillPath (out);
        const float yc = yo (juce::Decibels::decibelsToGain (ceilDb));
        g.setColour (on ? cPri.withAlpha (0.7f) : cOff); g.drawHorizontalLine ((int) yc, c.getX(), c.getRight());
        // GR bars from the top (0 .. -12 dB)
        for (int i = 0; i < n; i += 2)
        {
            const float v = hG[(size_t) ((head + 1 + i) % n)], h = juce::jlimit (0.0f, 1.0f, -v / 12.0f) * c.getHeight() * 0.5f;
            g.setColour ((on ? cPri : cOff).withAlpha (0.75f)); g.fillRect (c.getX() + (float) i / (float) (n - 1) * c.getWidth(), c.getY(), 1.5f, h);
        }
    }
    void timerCallback() override
    {
        if (demo) return;
        const auto& e = proc.dspEngine();
        head = e.histHead.load (std::memory_order_relaxed);
        for (int i = 0; i < mt::dsp::kHist; ++i)
        {
            hT[(size_t) i] = e.histT[(size_t) i].load (std::memory_order_relaxed);
            hG[(size_t) i] = e.histGr[(size_t) i].load (std::memory_order_relaxed);
            hO[(size_t) i] = e.histOut[(size_t) i].load (std::memory_order_relaxed);
        }
        for (int i = 0; i < 128; ++i) { sM[(size_t) i] = e.scopeM[(size_t) i].load (std::memory_order_relaxed); sS[(size_t) i] = e.scopeS[(size_t) i].load (std::memory_order_relaxed); }
        corrNow = e.corr.load (std::memory_order_relaxed);
        inL = std::max (proc.inLevel(), inL * 0.85f); outL = std::max (proc.outLevel(), outL * 0.85f);
        repaint();
    }
    MixToolsProcessor& proc;
    std::array<float, mt::dsp::kHist> hT {}, hG {}, hO {};
    std::array<float, 128> sM {}, sS {};
    int head = 0;
    float corrNow = 1, inL = 0, outL = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (MixToolsProcessor& p, Panel&) { return std::make_unique<ChainDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<ChainDisplay*> (c)) d->demoFill(); }
} // namespace mtui
