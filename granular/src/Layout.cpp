// Granular: control layout + the grain display (buffer waveform, every live grain as a dot: x = where it reads in the buffer,
// y = its pitch, size = its envelope, hollow = reversed), the position / spray window, input / output meters and the freeze state.
#include "PluginEditor.h"

namespace grui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY GRANULAR", "GRAINS  /  CLOUD  /  PITCH  /  FREEZE",
        { 16, 56, 868, 228 },
        {
            { "GRAINS", nullptr, { 16, 292, 330, 118 },  { { 'k', "size", "SIZE" }, { 'k', "density", "DENSITY" }, { 'c', "shape", "SHAPE" } }, 120 },
            { "CLOUD",  nullptr, { 354, 292, 530, 118 }, { { 'k', "position", "POSITION" }, { 'k', "spray", "SPRAY" }, { 'k', "reverse", "REVERSE" }, { 'k', "chaos", "CHAOS" }, { 'k', "width", "WIDTH" } } },
            { "PITCH",  nullptr, { 16, 418, 330, 118 },  { { 'k', "pitch", "PITCH" }, { 'k', "spread", "SPREAD" }, { 'c', "spreadMode", "SPREAD MODE" } }, 120 },
            { "OUTPUT", nullptr, { 354, 418, 530, 118 }, { { 't', "freeze", "FREEZE" }, { 'k', "feedback", "FEEDBACK" }, { 'k', "tone", "TONE" }, { 'k', "mix", "MIX" }, { 'k', "gain", "OUTPUT" } }, 112 },
        },
        "size"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cWave (0xff3a3a3a);

class GrainDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit GrainDisplay (GranularProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

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
        g.drawText (frz > 0.5f ? juce::String ("FROZEN") : juce::String (juce::roundToInt (P.sizeMs)) + " ms", readout.removeFromLeft (130), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f, true)); g.setColour (cDim);
        juce::String sub; sub << juce::String (P.density, P.density < 10 ? 1 : 0) << " GRAINS/S   " << grains << " ACTIVE   PITCH " << (P.pitch > 0.05f ? "+" : "") << juce::String (P.pitch, 1)
                              << " st   SPREAD " << juce::String (P.spread, 1) << " st   REVERSE " << juce::roundToInt (P.reverse * 100) << "%";
        g.drawText (sub, readout, juce::Justification::centredLeft);

        // cloud view. x: right edge = now, left edge = 4 s ago. y: pitch -24 (bottom) .. +24 st (top)
        g.setColour (cLine); g.drawRect (r);
        const auto c = r.toFloat().reduced (2.0f);
        auto yOfPitch = [&] (float st) { return c.getCentreY() - juce::jlimit (-26.0f, 26.0f, st) / 26.0f * c.getHeight() * 0.5f; };
        auto xOfAge = [&] (float age01) { return c.getRight() - juce::jlimit (0.0f, 1.0f, age01) * c.getWidth(); };
        g.setFont (uiFont (9.5f));
        for (int i = 1; i < 4; ++i)
        {
            const float x = c.getRight() - c.getWidth() * (float) i / 4.0f;
            g.setColour (cGrid); g.drawVerticalLine ((int) x, c.getY(), c.getBottom());
            g.setColour (cDim.withAlpha (0.7f)); g.drawText ("-" + juce::String (i) + " s", (int) x + 3, (int) c.getBottom() - 13, 40, 11, juce::Justification::left);
        }
        for (int st : { -12, 12 }) { g.setColour (cGrid); g.drawHorizontalLine ((int) yOfPitch ((float) st), c.getX(), c.getRight()); }
        g.setColour (cGrid.brighter (0.15f)); g.drawHorizontalLine ((int) yOfPitch (0), c.getX(), c.getRight());
        g.setColour (cDim.withAlpha (0.7f));
        g.drawText ("+12", (int) c.getX() + 3, (int) yOfPitch (12) - 12, 30, 11, juce::Justification::left);
        g.drawText ("-12", (int) c.getX() + 3, (int) yOfPitch (-12) + 1, 30, 11, juce::Justification::left);

        // buffer waveform (newest on the right)
        {
            juce::Path w; const int n = (int) wave.size();
            for (int i = 0; i < n; ++i)
            {
                const float x = c.getRight() - (float) i / (float) (n - 1) * c.getWidth();
                const float h = juce::jlimit (0.0f, 1.0f, std::sqrt (wave[(size_t) (n - 1 - i)])) * c.getHeight() * 0.42f;
                w.addRectangle (x - 1.0f, c.getCentreY() - h, 2.0f, juce::jmax (1.0f, 2.0f * h));
            }
            g.setColour (cWave); g.fillPath (w);
        }
        // position + spray window
        {
            const float len = 4.0f * 1000.0f;   // ms in the buffer view
            const float x0 = xOfAge (P.positionMs / len), x1 = xOfAge ((P.positionMs + P.sprayMs + P.sizeMs) / len);
            g.setColour (cPri.withAlpha (0.05f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, c.getY(), x0, c.getBottom()));
            g.setColour (cSec.withAlpha (0.55f)); g.drawVerticalLine ((int) x0, c.getY(), c.getBottom());
            g.setColour (cSec.withAlpha (0.25f)); g.drawVerticalLine ((int) x1, c.getY(), c.getBottom());
        }
        // grains
        for (auto& gr : dots)
        {
            if (gr.age < 0.0f) continue;
            const float d = 4.0f + 12.0f * gr.amp;
            const auto e = juce::Rectangle<float> (d, d).withCentre ({ xOfAge (gr.age), yOfPitch (gr.pitch) });
            g.setColour (cPri.withAlpha (0.10f + 0.12f * gr.amp)); g.fillEllipse (e.expanded (4.0f));   // soft glow
            if (gr.rev) { g.setColour (cPri.withAlpha (0.4f + 0.6f * gr.amp)); g.drawEllipse (e, 1.5f); }
            else        { g.setColour (cSec.brighter (0.3f).withAlpha (0.45f + 0.55f * gr.amp)); g.fillEllipse (e); }
        }
        g.setColour (cDim); g.setFont (uiFont (9.5f, true)); g.drawText ("GRAIN CLOUD   (FILLED = FORWARD, RING = REVERSE)", r.reduced (6, 3), juce::Justification::topRight);

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
        meter (m1, inL, "IN"); meter (meters, outL, "OUT");
        if (frz > 0.01f) { g.setColour (cPri.withAlpha (frz)); g.drawRect (getLocalBounds().reduced (3), 2); }
    }

    void demoFill()   // screenshot: a phrase in the buffer and a busy cloud
    {
        for (size_t i = 0; i < wave.size(); ++i)
        {
            const float t = (float) i / (float) wave.size();
            const float hit = std::fmod (t * 7.0f, 1.0f);
            wave[i] = 0.5f * std::exp (-hit * 4.0f) * (0.6f + 0.4f * std::sin (t * 40.0f)) + 0.02f;
        }
        juce::Random rnd (7);
        const auto P = proc.currentParams();
        for (auto& d : dots)
        {
            d.age = (P.positionMs + rnd.nextFloat() * (P.sprayMs + P.sizeMs)) / 4000.0f;
            d.pitch = P.pitch + (rnd.nextFloat() * 2.0f - 1.0f) * P.spread; d.amp = rnd.nextFloat(); d.rev = rnd.nextFloat() < P.reverse;
            if (rnd.nextFloat() < 0.6f) d.age = -1.0f;
        }
        grains = 26; inL = 0.3f; outL = 0.25f; demoFrz = 0.0f; demo = true; repaint();
    }

private:
    void timerCallback() override
    {
        if (demo) return;
        const auto& e = proc.dspEngine();
        const int head = e.peakHead.load (std::memory_order_relaxed);
        for (int i = 0; i < gr::dsp::kPeakBins; ++i) wave[(size_t) i] = e.peaks[(size_t) ((head + 1 + i) % gr::dsp::kPeakBins)].load (std::memory_order_relaxed);
        for (size_t i = 0; i < dots.size(); ++i)
        {
            auto& t = e.taps[i];
            dots[i] = { t.age.load (std::memory_order_relaxed), t.pitch.load (std::memory_order_relaxed), t.amp.load (std::memory_order_relaxed), t.rev.load (std::memory_order_relaxed) != 0 };
        }
        grains = e.activeGrains.load (std::memory_order_relaxed);
        inL = std::max (proc.inLevel(), inL * 0.85f); outL = std::max (proc.outLevel(), outL * 0.85f);
        repaint();
    }
    struct Dot { float age = -1, pitch = 0, amp = 0; bool rev = false; };
    GranularProcessor& proc;
    std::vector<float> wave = std::vector<float> ((size_t) gr::dsp::kPeakBins, 0.0f);
    std::array<Dot, gr::dsp::kMaxGrains> dots {};
    int grains = 0;
    float inL = 0, outL = 0, demoFrz = 0;
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (GranularProcessor& p, Panel&) { return std::make_unique<GrainDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<GrainDisplay*> (c)) d->demoFill(); }
} // namespace grui
