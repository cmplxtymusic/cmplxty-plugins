// Renders the real GranularEditor to a PNG, and runs the processor-level tests.
// usage: GranularScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
#include "PluginEditor.h"
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

// processor-level: processBlock with stereo + mono layouts, params -> engine, freeze, tail length, state round trip
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto runProc = [&] (GranularProcessor& proc, const std::vector<float>& x, int nch)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer none;
        for (size_t b = 0; b + bs <= x.size(); b += bs)
        {
            for (int c = 0; c < nch; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, x[b + (size_t) i]);
            proc.processBlock (buf, none);
            for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    };
    auto set = [] (GranularProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    const auto x = harm (230.0, (int) sr * 2);
    std::vector<float> burst = x; for (size_t i = 12000; i < burst.size(); ++i) burst[i] = 0.0f;
    {
        GranularProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && ! proc.acceptsMidi(), "stereo in/out, no MIDI");
        set (proc, "mix", 1.0f);
        auto y = runProc (proc, x, 2);
        check (rmsOf (y, 24000) > 0.02, "grains come out (mix 100%)");
        check (proc.outLevel() > 0.001f && proc.dspEngine().activeGrains.load() > 0, "output meter + active grain count move");
        int live = 0; for (auto& t : proc.dspEngine().taps) live += t.age.load() >= 0.0f ? 1 : 0;
        check (live > 0, "display taps report live grains");
        set (proc, "density", 1.0f); set (proc, "size", 10.0f); set (proc, "spray", 0.0f);
        auto z = runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        check (rmsOf (z, 48000) < 1e-3, "after the input stops (no feedback) the cloud dies away");
        check (proc.getTailLengthSeconds() > 3.0 && proc.getTailLengthSeconds() < 30.0, "tail length reported");
    }
    {   // freeze
        GranularProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 1.0f); set (proc, "size", 200.0f); set (proc, "density", 30.0f);
        runProc (proc, x, 2);
        set (proc, "freeze", 1.0f);
        auto a = runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        auto b = runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        const double ra = rmsOf (a, 48000), rb = rmsOf (b, 48000);
        check (ra > 0.01 && std::fabs (20 * std::log10 (rb / ra)) < 3.0, "freeze keeps the cloud going without input");
        check (proc.freezeAmount() > 0.9f && proc.getTailLengthSeconds() >= 60.0, "freeze state and tail length reported");
        set (proc, "freeze", 0.0f);
        auto c = runProc (proc, std::vector<float> (48000 * 6, 0.0f), 2);
        check (rmsOf (c, 48000 * 5) < ra * 0.1, "unfreeze lets it die away");
    }
    {   // mono layout
        GranularProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 1.0f);
        auto y = runProc (proc, x, 1); bool fin = true; for (float v : y) fin = fin && std::isfinite (v);
        check (fin && rmsOf (y, 24000) > 0.02, "mono processing works");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! proc.checkBusesLayoutSupported (l2), "mono in / stereo out refused");
    }
    {   // mix 0 = dry
        GranularProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 0.0f);
        auto y = runProc (proc, x, 2); double d = 0; for (size_t i = 24000; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i]));
        check (d < 1e-5, "mix 0 = dry signal");
    }
    {   // every factory preset: loads, sounds, stays finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            GranularProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            const auto xin = harm (196.0, (int) rate * 3, rate);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i);
                proc.reset();
                auto y = runProc (proc, xin, 2);
                float pk = 0; bool fin = true; for (float v : y) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
                if (! fin || pk > 4.0f) { ++bad; names << gr::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (y, y.size() / 2) < 0.005) { ++silent; names << gr::factoryPresets()[(size_t) i].name << " silent; "; }
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent %s\n", GranularProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak < 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset produces sound");
        check (GranularProcessor().presets.numFactory() >= 12, ">= 12 factory presets");
    }
    {   // user preset round trip through a file in the (test) user folder
        GranularProcessor a; set (a, "size", 333.0f); set (a, "spreadMode", 2); set (a, "freeze", 1);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == gr::PresetManager::userDir(), "user preset saved in the user preset folder");
        GranularProcessor b;
        auto v = [] (GranularProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
        check (b.presets.loadFile (f) && std::fabs (v (b, "size") - 333.0f) < 0.5f && v (b, "spreadMode") == 2 && v (b, "freeze") == 1 && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        GranularProcessor a; set (a, "size", 420.0f); set (a, "shape", 2); set (a, "tone", 2000.0f); set (a, "freeze", 1); a.uiWidth = 1080; a.uiHeight = 672;
        a.presets.loadFactory (5); set (a, "size", 420.0f); set (a, "shape", 2); set (a, "tone", 2000.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        GranularProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto v = [] (GranularProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
        check (std::fabs (v (b, "size") - 420.0f) < 0.5f && v (b, "shape") == 2 && std::fabs (v (b, "tone") - 2000.0f) < 1.0f, "parameters survive save / load");
        check (b.presets.currentName() == gr::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1080 && b.uiHeight == 672, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "shape") == 2, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    gr::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("granular_shot_settings");
    gr::upd::dirOverride().deleteRecursively();
    { gr::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      gr::upd::writeJson (gr::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    GranularProcessor proc;
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (4);   // a busier cloud for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<GranularEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (GranularProcessor::kBaseW * 3 / 2, GranularProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { gr::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/granular/"; i.notes = "New grain shapes"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto p = e->getPanel().auditPresets();  std::printf ("%s", p.toRawUTF8());
        proc.undoManager.clearUndoHistory(); proc.presets.loadFactory (4);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        const bool undoOk = proc.undoManager.canUndo();
        std::printf ("%s a preset load can be undone\n", undoOk ? "  ok  " : "  FAIL");
        if (! undoOk) p = "PRESET AUDIT FAILED";
        return (r.contains ("KEYBOARD AUDIT OK") && m.contains ("MENU AUDIT OK") && p.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode != "empty") grui::demoDisplay (e->getPanel().display());
    if (mode == "menu") e->getPanel().demoKnobMenu();
    if (mode == "presets") e->getPanel().demoPresetMenu();
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
