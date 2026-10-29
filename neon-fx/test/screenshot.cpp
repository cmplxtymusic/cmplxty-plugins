// Renders the real NeonFXEditor to a PNG, and runs the processor-level tests.
// usage: NeonFXScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
#include "PluginEditor.h"
#include <random>
#include <cmath>

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }
double rmsOf (const std::vector<float>& x, size_t from, size_t to = 0) { if (to == 0 || to > x.size()) to = x.size(); double s = 0; for (size_t i = from; i < to; ++i) s += x[i] * x[i]; return to > from ? std::sqrt (s / (double) (to - from)) : 0.0; }
// processor-level: bus layouts, param text, bypass-exact, tempo sync via the host transport, hold params, presets (NaN), user presets, state
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto sine = [&] (int n, double hz, float a, int off = 0) { std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = a * (float) std::sin (2 * 3.14159265358979 * hz * (i + off) / sr); return x; };
    auto render = [&] (NeonFXProcessor& proc, const std::vector<float>& in, int nch = 2, std::vector<float>* right = nullptr)
    {
        std::vector<float> out; juce::AudioBuffer<float> buf (nch, bs); juce::MidiBuffer m;
        for (size_t b = 0; b + (size_t) bs <= in.size(); b += (size_t) bs)
        {
            for (int c = 0; c < nch; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, in[b + (size_t) i] * (c == 0 ? 1.0f : 0.8f));
            proc.processBlock (buf, m);
            for (int i = 0; i < bs; ++i) { out.push_back (buf.getSample (0, i)); if (right && nch > 1) right->push_back (buf.getSample (1, i)); }
        }
        return out;
    };
    auto set = [] (NeonFXProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto v = [] (NeonFXProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    auto txt = [] (NeonFXProcessor& p, const char* id, float val) { auto* q = p.apvts.getParameter (id); return q->getText (q->convertTo0to1 (val), 20); };
    {
        NeonFXProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && proc.getTotalNumOutputChannels() == 2 && ! proc.acceptsMidi() && proc.getLatencySamples() == 0, "effect: stereo in / out, no MIDI, zero latency");
        check (proc.getParameters().size() == 27 && v (proc, "stOn") == 0 && v (proc, "tsOn") == 0 && v (proc, "djFilter") == 0.0f, "27 parameters, hold FX off by default");
        check (txt (proc, "djFilter", 0.0f) == "Off" && txt (proc, "djFilter", -0.5f).startsWith ("LP") && txt (proc, "djFilter", 0.5f).startsWith ("HP"), "DJ filter text Off / LP / HP");
        for (auto* id : { "drOn", "flOn", "gtOn" }) set (proc, id, 0.0f);
        proc.reset();
        const auto in = sine (48000, 440.0, 0.5f); auto y = render (proc, in); double d = 0;
        for (size_t i = 0; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - in[i]));
        check (d == 0.0, "every module off = bit-exact passthrough");
        set (proc, "gtOn", 1.0f); set (proc, "gtDepth", 1.0f); set (proc, "gtPattern", 1.0f); set (proc, "gtRate", 1.0f);
        proc.testTransport = cw::dsp::Transport { 120.0, 0.0, true }; proc.reset();
        y = render (proc, sine (48000, 440.0, 0.5f));
        // 120 bpm, 1/16 = 6000 samples per step: Halves pattern = on / off alternate (72% duty)
        check (rmsOf (y, 1000, 4000) > 0.2 && rmsOf (y, 7000, 11500) < 0.01 && rmsOf (y, 13000, 16000) > 0.2, "trance gate locks to the host tempo (120 bpm, 1/16)");
        proc.testTransport = cw::dsp::Transport { 120.0, 0.25, true }; proc.reset();
        y = render (proc, sine (12000, 440.0, 0.5f));
        check (rmsOf (y, 3000, 5500) < 0.01 && rmsOf (y, 6500, 9500) > 0.2, "gate follows the host song position");
        proc.testTransport.reset();
        set (proc, "gtOn", 0.0f); set (proc, "stOn", 1.0f); proc.reset();
        y = render (proc, sine (48000, 440.0, 0.5f)); bool fin = true; for (float s : y) fin = fin && std::isfinite (s);
        check (fin && proc.dspEngine().stutterOn.load(), "stutter hold param engages");
        set (proc, "stOn", 0.0f); set (proc, "tsOn", 1.0f);
        y = render (proc, sine (96000, 440.0, 0.5f));
        check (proc.dspEngine().speedNow.load() < 0.05f && rmsOf (y, 90000) < 0.05, "tape stop hold param brakes to a stop");
        set (proc, "tsOn", 0.0f);
        y = render (proc, sine (96000, 440.0, 0.5f));
        check (proc.dspEngine().speedNow.load() > 0.99f && rmsOf (y, 80000) > 0.2, "tape stop release spins back up to live");
    }
    {   // mono layout
        NeonFXProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono in / out supported");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! NeonFXProcessor().setBusesLayout (l2), "mismatched in / out refused");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        proc.presets.loadFactory (16);
        auto y = render (proc, sine (24000, 220.0, 0.5f), 1); bool fin = true; for (float s : y) fin = fin && std::isfinite (s);
        check (fin && rmsOf (y, 4800) > 0.01, "mono renders");
    }
    {   // every factory preset with all hold FX pressed and released, finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (-0.5f, 0.5f);
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            NeonFXProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i); proc.reset();
                std::vector<float> in ((size_t) (rate * 0.5)); for (size_t k = 0; k < in.size(); ++k) in[k] = 0.5f * (float) std::sin (k * 0.03) + 0.2f * U (rng);
                std::vector<float> R, all;
                for (int phase = 0; phase < 5; ++phase)
                {
                    if (phase == 1) set (proc, "stOn", 1.0f);
                    if (phase == 2) { set (proc, "stOn", 0.0f); set (proc, "tsOn", 1.0f); }
                    if (phase == 3) set (proc, "tsOn", 0.0f);
                    auto y = render (proc, in, 2, &R); all.insert (all.end(), y.begin(), y.end());
                }
                float pk = 0; bool fin = true; for (size_t k = 0; k < all.size(); ++k) { fin = fin && std::isfinite (all[k]) && std::isfinite (R[k]); pk = std::max ({ pk, std::fabs (all[k]), std::fabs (R[k]) }); }
                if (! fin || pk > 4.0f) { ++bad; names << cw::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (all, 0, in.size()) < 0.003) { ++silent; names << cw::factoryPresets()[(size_t) i].name << " silent; "; }
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent %s\n", NeonFXProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset (with stutter + tape stop pressed): no NaN / inf, peak <= 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset passes sound");
        check (NeonFXProcessor().presets.numFactory() >= 12, ">= 12 factory presets");
    }
    {   // user preset round trip
        NeonFXProcessor a; set (a, "gtPattern", 5); set (a, "flCut", 1234.0f); set (a, "djFilter", -0.3f);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == cw::PresetManager::userDir(), "user preset saved in the user preset folder");
        NeonFXProcessor b;
        check (b.presets.loadFile (f) && v (b, "gtPattern") == 5 && std::fabs (v (b, "flCut") - 1234.0f) < 2.0f && std::fabs (v (b, "djFilter") + 0.3f) < 0.002f && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        NeonFXProcessor a; a.uiWidth = 1320; a.uiHeight = 749;
        a.presets.loadFactory (5); set (a, "mix", 0.4f); set (a, "gtPattern", 6); set (a, "output", -7.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        NeonFXProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (v (b, "mix") - 0.4f) < 0.02f && v (b, "gtPattern") == 6 && std::fabs (v (b, "output") + 7.0f) < 0.05f, "parameters survive save / load");
        check (b.presets.currentName() == cw::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1320 && b.uiHeight == 749, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "gtPattern") == 6, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    cw::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neon-fx_shot_settings");
    cw::upd::dirOverride().deleteRecursively();
    { cw::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      cw::upd::writeJson (cw::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    NeonFXProcessor proc;
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (16);   // Neon Nightdrive for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<NeonFXEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (NeonFXProcessor::kBaseW * 3 / 2, NeonFXProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { cw::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/neon-fx/"; i.notes = "New presets"; e->getPanel().demoBanner (i); }
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
    if (mode != "empty") cwui::demoDisplay (e->getPanel().display());
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
