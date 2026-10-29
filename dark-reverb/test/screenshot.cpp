// Renders the real DarkReverbEditor to a PNG, and runs the processor-level tests.
// usage: DarkReverbScreenshot out.png [banner|menu|big|empty|keys|params|proc]
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

// processor-level: processBlock with stereo + mono layouts, params -> engine, freeze, tail length, state round trip
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto runProc = [&] (DarkReverbProcessor& proc, const std::vector<float>& x, int nch)
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
    auto set = [] (DarkReverbProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    const auto x = harm (230.0, (int) sr * 2);
    std::vector<float> burst = x; for (size_t i = 12000; i < burst.size(); ++i) burst[i] = 0.0f;
    {
        DarkReverbProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && ! proc.acceptsMidi(), "stereo in/out, no MIDI");
        set (proc, "mix", 1.0f); set (proc, "decay", 4.0f);
        auto y = runProc (proc, burst, 2);
        check (rmsOf (y, 48000) > 0.01, "reverb tail continues after the input stops");
        check (proc.wetLevel() > 0.0005f, "wet meter moves");
        check (std::fabs (proc.getTailLengthSeconds() - (4.0 * 1.5 + 0.02)) < 0.01, "tail length follows decay");
        set (proc, "decay", 0.5f); runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        auto z = runProc (proc, std::vector<float> (48000, 0.0f), 2);
        check (rmsOf (z, 0) < 1e-4, "decay parameter reaches the engine (short tail dies away)");
    }
    {   // freeze
        DarkReverbProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 1.0f); set (proc, "decay", 3.0f);
        runProc (proc, x, 2);
        set (proc, "freeze", 1.0f);
        auto a = runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        auto b = runProc (proc, std::vector<float> (48000 * 2, 0.0f), 2);
        const double ra = rmsOf (a, 48000), rb = rmsOf (b, 48000);
        check (ra > 0.01 && std::fabs (20 * std::log10 (rb / ra)) < 1.0, "freeze holds the tail");
        check (proc.freezeAmount() > 0.9f && proc.getTailLengthSeconds() >= 60.0, "freeze state and tail length reported");
        set (proc, "freeze", 0.0f);
        auto c = runProc (proc, std::vector<float> (48000 * 6, 0.0f), 2);
        check (rmsOf (c, 48000 * 5) < ra * 0.1, "unfreeze lets it decay");
    }
    {   // mono layout
        DarkReverbProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        auto y = runProc (proc, x, 1); bool fin = true; for (float v : y) fin = fin && std::isfinite (v);
        check (fin && rmsOf (y, 48000) > 0.05, "mono processing works");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! proc.checkBusesLayoutSupported (l2), "mono in / stereo out refused");
    }
    {   // mix 0 = dry
        DarkReverbProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 0.0f);
        auto y = runProc (proc, x, 2); double d = 0; for (size_t i = 24000; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i]));
        check (d < 1e-5, "mix 0 = dry signal");
    }
    {   // state
        DarkReverbProcessor a; set (a, "decay", 12.0f); set (a, "shimPitch", 2); set (a, "dark", 2000.0f); set (a, "shimOn", 1); a.uiWidth = 1080; a.uiHeight = 672;
        juce::MemoryBlock mb; a.getStateInformation (mb);
        DarkReverbProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        auto v = [] (DarkReverbProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
        check (std::fabs (v (b, "decay") - 12.0f) < 0.05f && v (b, "shimPitch") == 2 && std::fabs (v (b, "dark") - 2000.0f) < 1.0f && v (b, "shimOn") == 1, "parameters survive save / load");
        check (b.uiWidth == 1080 && b.uiHeight == 672, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "shimPitch") == 2, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    dr::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dr_shot_settings");
    dr::upd::dirOverride().deleteRecursively();
    { dr::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      dr::upd::writeJson (dr::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<DarkReverbProcessor> ([] (DarkReverbProcessor& q) -> pk::PresetManager& { return q.presets; }, dr::upd::dataDir().getChildFile ("Presets"));
    DarkReverbProcessor proc;
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
    auto* e = dynamic_cast<DarkReverbEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (DarkReverbProcessor::kBaseW * 3 / 2, DarkReverbProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { dr::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/dark-reverb/"; i.notes = "New carrier: choir"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode != "empty") drui::demoDisplay (e->getPanel().display());
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
