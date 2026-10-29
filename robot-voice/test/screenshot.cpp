// Renders the real RobotVoiceEditor to a PNG, and runs the processor-level tests.
// usage: RobotVoiceScreenshot out.png [banner|menu|big|empty|keys|params|proc]
#include "PluginEditor.h"
#include "PresetKitTest.h"
#include <random>
#include <cmath>

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }
std::vector<float> harm (double hz, int n, double sr = 48000.0)
{
    std::vector<float> x ((size_t) n); double ph = 0;
    for (auto& v : x) { ph += hz / sr; double s = 0; for (int h = 1; h <= 8; ++h) s += std::sin (2 * 3.14159265358979 * h * ph) / h; v = (float) (0.3 * s); }
    return x;
}
double rmsOf (const std::vector<float>& x, size_t from) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i]; return std::sqrt (s / (double) (x.size() - from)); }

// processor-level: processBlock with stereo + mono layouts, MIDI carrier, params -> engine, state round trip
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto runProc = [&] (RobotVoiceProcessor& proc, const std::vector<float>& x, int nch, juce::MidiBuffer* firstMidi)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer none;
        for (size_t b = 0; b + bs <= x.size(); b += bs)
        {
            for (int c = 0; c < nch; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, x[b + (size_t) i]);
            proc.processBlock (buf, (b == 0 && firstMidi) ? *firstMidi : none);
            for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    };
    auto set = [] (RobotVoiceProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    const auto x = harm (230.0, (int) sr * 2);
    {
        RobotVoiceProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && proc.acceptsMidi(), "stereo in/out, accepts MIDI");
        set (proc, "vocOn", 0.0f);
        auto y = runProc (proc, x, 2, nullptr);
        check (proc.detectedHz() > 225 && proc.detectedHz() < 235, "processor tracks the input pitch");
        check (std::fabs (proc.targetHz() - 233.08f) < 1.5f, "snapped target is A#3 (chromatic)");
        set (proc, "scale", 1.0f);   // C major
        runProc (proc, x, 2, nullptr);
        check (std::fabs (proc.targetHz() - 220.0f) < 1.5f, "scale parameter reaches the engine (C major -> A3)");
        check (rmsOf (y, 48000) > 0.1, "snap output has signal");
    }
    {
        RobotVoiceProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "vocMix", 1.0f); set (proc, "snapOn", 0.0f);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        runProc (proc, x, 2, &m);
        check (proc.heldNote() == 64, "MIDI note held as the vocoder carrier");
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        runProc (proc, std::vector<float> (bs * 2, 0.0f), 2, &off);
        check (proc.heldNote() == -1, "note-off releases it");
        float lv = 0; runProc (proc, x, 2, nullptr); for (int b = 0; b < proc.numBands(); ++b) lv = std::max (lv, proc.bandLevel (b));
        check (lv > 0.001f, "band meters move");
    }
    {   // mono layout
        RobotVoiceProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        auto y = runProc (proc, x, 1, nullptr); bool fin = true; for (float v : y) fin = fin && std::isfinite (v);
        check (fin && rmsOf (y, 48000) > 0.05, "mono processing works");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! proc.checkBusesLayoutSupported (l2), "mono in / stereo out refused");
    }
    {   // all off = dry
        RobotVoiceProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "snapOn", 0); set (proc, "vocOn", 0); set (proc, "ringOn", 0);
        auto y = runProc (proc, x, 2, nullptr); double d = 0; for (size_t i = 24000; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i]));
        check (d < 1e-6, "all sections off = exact dry signal (after the switch fades)");
    }
    {   // state
        RobotVoiceProcessor a; set (a, "key", 7); set (a, "carrier", 2); set (a, "ringHz", 333.0f); set (a, "ringOn", 1); a.uiWidth = 1080; a.uiHeight = 672;
        juce::MemoryBlock mb; a.getStateInformation (mb);
        RobotVoiceProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto v = [] (RobotVoiceProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
        check (v (b, "key") == 7 && v (b, "carrier") == 2 && std::fabs (v (b, "ringHz") - 333.0f) < 0.5f && v (b, "ringOn") == 1, "parameters survive save / load");
        check (b.uiWidth == 1080 && b.uiHeight == 672, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "key") == 7, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    rv::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rv_shot_settings");
    rv::upd::dirOverride().deleteRecursively();
    { rv::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      rv::upd::writeJson (rv::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<RobotVoiceProcessor> ([] (RobotVoiceProcessor& q) -> pk::PresetManager& { return q.presets; }, rv::upd::dataDir().getChildFile ("Presets"));
    RobotVoiceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    if (mode == "params")
    {
        int bad = 0;
        for (auto* p : proc.getParameters())
        {
            const bool ok = p->isAutomatable() && ! p->isMetaParameter();
            std::printf ("  param %-18s automatable=%d default=%s\n", p->getName (40).toRawUTF8(), (int) ok, p->getText (p->getDefaultValue(), 20).toRawUTF8());
            bad += ok ? 0 : 1;
        }
        std::printf ("%d params, %d not automatable\n", proc.getParameters().size(), bad);
        return bad;
    }
    if (mode.isEmpty() || mode == "presets") proc.presets.loadFactory (1);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<RobotVoiceEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (RobotVoiceProcessor::kBaseW * 3 / 2, RobotVoiceProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { rv::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/robot-voice/"; i.notes = "New carrier: choir"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode != "empty") rvui::demoDisplay (e->getPanel().display());
    if (mode == "menu") e->getPanel().demoKnobMenu();
    if (mode == "presets") e->getPanel().getPresetBar().openMenu();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png; png.writeImageToStream (img, os);
    std::printf ("wrote %s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
