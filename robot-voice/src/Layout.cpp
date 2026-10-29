// Robot Voice: control layout + the display (pitch trace: input in grey, snapped in white; vocoder band meters).
#include "PluginEditor.h"

namespace rvui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY ROBOT VOICE", "PITCH SNAP  /  VOCODER  /  RING MOD",
        { 16, 56, 868, 300 },
        {
            { "SNAP",    "snapOn", { 16, 366, 212, 162 },  { { 'k', "speed", "RETUNE" }, { 'c', "key", "KEY" }, { 'c', "scale", "SCALE" } }, 106 },
            { "VOCODER", "vocOn",  { 236, 366, 226, 162 }, { { 'k', "vocMix", "BLEND" }, { 'c', "carrier", "CARRIER" }, { 'c', "bands", "BANDS" }, { 'c', "octave", "OCTAVE" } }, 112 },
            { "RING",    "ringOn", { 470, 366, 200, 162 }, { { 'k', "ringHz", "FREQ" }, { 'k', "ringMix", "AMOUNT" } } },
            { "OUTPUT",  nullptr,  { 678, 366, 206, 162 }, { { 'k', "mix", "MIX" }, { 'k', "gain", "OUTPUT" } } },
        },
        "speed"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLine (0xff262626), cGrid (0xff1c1c1c), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a), cIn (0xff6e6e6e);
const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
juce::String noteName (float hz)
{
    if (hz <= 0) return "-";
    const int m = (int) std::lround (rv::dsp::hzToMidi (hz));
    return juce::String (kNames[(m % 12 + 12) % 12]) + juce::String (m / 12 - 1);
}

class RobotDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit RobotDisplay (RobotVoiceProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
        auto r = getLocalBounds().reduced (12, 10);
        auto meters = r.removeFromRight (250); r.removeFromRight (14);
        auto readout = r.removeFromTop (34); r.removeFromTop (8);

        // readout
        g.setFont (uiFont (26.0f, true)); g.setColour (cPri);
        float in = 0, out = 0;
        for (int i = kHist - 1; i >= kHist - 12 && in <= 0; --i) { in = histIn[(size_t) i]; out = histOut[(size_t) i]; }   // hold briefly through gaps
        g.drawText (proc.heldNote() >= 0 ? noteName (rv::dsp::midiToHz ((float) proc.heldNote())) : (out > 0 ? noteName (out) : juce::String ("--")), readout.removeFromLeft (90), juce::Justification::centredLeft);
        g.setFont (uiFont (11.0f, true)); g.setColour (cDim);
        juce::String sub = in > 0 ? "IN " + juce::String (in, 1) + " Hz   OUT " + juce::String (out, 1) + " Hz" : "NO PITCH";
        if (proc.heldNote() >= 0) sub << "   MIDI CARRIER";
        g.drawText (sub, readout, juce::Justification::centredLeft);

        // pitch lane: 36..84 midi
        g.setColour (cLine); g.drawRect (r);
        auto yOf = [&] (float m) { return (float) r.getBottom() - (m - 36.0f) / 48.0f * (float) r.getHeight(); };
        for (int m = 36; m <= 84; ++m)
        {
            g.setColour (m % 12 == 0 ? cLine : cGrid);
            if (m % 12 == 0 || m % 12 == 5) g.drawHorizontalLine ((int) yOf ((float) m), (float) r.getX(), (float) r.getRight());
            if (m % 12 == 0) { g.setColour (cDim.withAlpha (0.7f)); g.setFont (uiFont (9.5f)); g.drawText ("C" + juce::String (m / 12 - 1), r.getX() + 3, (int) yOf ((float) m) - 12, 30, 11, juce::Justification::left); }
        }
        auto trace = [&] (const std::vector<float>& h, juce::Colour c, float w)
        {
            juce::Path p; bool pen = false;
            for (size_t i = 0; i < h.size(); ++i)
            {
                const float x = (float) r.getX() + (float) i / (float) (kHist - 1) * (float) r.getWidth();
                if (h[i] <= 0) { pen = false; continue; }
                const float y = juce::jlimit ((float) r.getY(), (float) r.getBottom(), yOf (rv::dsp::hzToMidi (h[i])));
                if (! pen) p.startNewSubPath (x, y); else p.lineTo (x, y);
                pen = true;
            }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (w));
        };
        trace (histIn, cIn, 1.5f); trace (histOut, cPri, 2.0f);

        // band meters
        g.setColour (cLine); g.drawRect (meters);
        g.setFont (uiFont (9.5f, true)); g.setColour (cDim);
        g.drawText ("VOCODER BANDS", meters.getX(), meters.getY() - 1, meters.getWidth(), 14, juce::Justification::centred);
        auto mb = meters.reduced (8).withTrimmedTop (12);
        const int nb = proc.numBands();
        const float bw = (float) mb.getWidth() / (float) nb;
        for (int b = 0; b < nb; ++b)
        {
            const float db = juce::Decibels::gainToDecibels (lev[(size_t) b] * 8.0f, -60.0f);
            const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) mb.getHeight();
            const float x = (float) mb.getX() + (float) b * bw;
            g.setColour (cGrid); g.fillRect (x + 1, (float) mb.getY(), bw - 2, (float) mb.getHeight());
            g.setColour (cSec.withAlpha (0.85f)); g.fillRect (x + 1, (float) mb.getBottom() - h, bw - 2, h);
            g.setColour (cPri); g.fillRect (x + 1, (float) mb.getBottom() - h, bw - 2, juce::jmin (2.0f, h));
        }
    }

    void demoFill()   // screenshot: a plausible sung phrase
    {
        for (int i = 0; i < kHist; ++i)
        {
            const float t = (float) i / kHist; const bool v = std::fmod (t * 5.0f, 1.0f) < 0.82f || i > kHist - 4;
            const float m = 57.0f + 5.0f * std::sin (t * 6.0f) + 0.35f * std::sin (t * 90.0f);
            histIn[(size_t) i] = v ? rv::dsp::midiToHz (m) : 0.0f;
            histOut[(size_t) i] = v ? rv::dsp::midiToHz (rv::dsp::snapMidi (m, 0, 1)) : 0.0f;
        }
        for (int b = 0; b < rv::dsp::kMaxBands; ++b) lev[(size_t) b] = 0.08f * std::exp (-0.12f * b) * (1.0f + 0.5f * std::sin (b * 1.7f));
        demo = true; repaint();
    }

private:
    void timerCallback() override
    {
        if (demo) return;
        std::rotate (histIn.begin(), histIn.begin() + 1, histIn.end()); histIn.back() = proc.detectedHz();
        std::rotate (histOut.begin(), histOut.begin() + 1, histOut.end()); histOut.back() = proc.targetHz();
        for (int b = 0; b < rv::dsp::kMaxBands; ++b) lev[(size_t) b] = std::max (proc.bandLevel (b), lev[(size_t) b] * 0.85f);
        repaint();
    }
    static constexpr int kHist = 150;
    RobotVoiceProcessor& proc;
    std::vector<float> histIn = std::vector<float> (kHist, 0.0f), histOut = std::vector<float> (kHist, 0.0f);
    std::array<float, rv::dsp::kMaxBands> lev {};
    bool demo = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (RobotVoiceProcessor& p, Panel&) { return std::make_unique<RobotDisplay> (p); }
void demoDisplay (juce::Component* c) { if (auto* d = dynamic_cast<RobotDisplay*> (c)) d->demoFill(); }
} // namespace rvui
