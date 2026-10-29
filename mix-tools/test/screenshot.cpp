// Renders the real MixToolsEditor to a PNG, and runs the processor-level tests.
// usage: MixToolsScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
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

// processor-level: latency, limiter ceiling, module switches, width, mono layout, presets, user presets, state
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto runProc = [&] (MixToolsProcessor& proc, const std::vector<float>& x, int nch)
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
    auto set = [] (MixToolsProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    const auto x = harm (230.0, (int) sr * 2);
    auto v = [] (MixToolsProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    {
        MixToolsProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && ! proc.acceptsMidi(), "stereo in/out, no MIDI");
        check (proc.getLatencySamples() == proc.dspEngine().latency() && proc.getLatencySamples() > 0, "lookahead latency reported to the host");
        { auto* mb = proc.apvts.getParameter ("monoBass");
          check (mb->getText (0.0f, 20) == "Off" && mb->getText (1.0f, 20).contains ("500") && mb->getValueForText ("off") == 0.0f, "Mono Bass shows / accepts 'Off' at minimum"); }
        check (proc.getParameters().size() == 13 && v (proc, "ceiling") == -1.0f && v (proc, "width") == 1.0f, "13 parameters, defaults (ceiling -1 dBTP, width 100 %)");
        set (proc, "drive", 18.0f);
        auto y = runProc (proc, x, 2); float pk = 0; for (size_t i = 4800; i < y.size(); ++i) pk = std::max (pk, std::fabs (y[i]));
        std::printf ("  drive +18 dB: out peak %.2f dBFS, GR %.1f dB\n", 20 * std::log10 (pk), proc.dspEngine().grDb.load());
        check (pk <= juce::Decibels::decibelsToGain (-1.0f) * 1.001f && proc.dspEngine().grDb.load() < -6.0f, "limiter holds the ceiling through the processor, GR meter moves");
        check (proc.outLevel() > 0.3f, "output meter moves");
    }
    {   // module power switches: everything off = delayed dry
        MixToolsProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "wOn", 0); set (proc, "tOn", 0); set (proc, "lOn", 0); set (proc, "width", 0.0f); set (proc, "attack", 1.0f); set (proc, "drive", 24.0f);
        auto y = runProc (proc, x, 2); const int lat = proc.getLatencySamples(); double d = 0;
        for (size_t i = 24000; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i - (size_t) lat]));
        check (d < 1e-5, "all three modules switched off: clean signal, only delayed by the reported latency");
    }
    {   // width 0 through the processor -> L == R
        MixToolsProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "width", 0.0f);
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer none; double d = 0;
        for (int b = 0; b < 50; ++b)
        {
            for (int i = 0; i < bs; ++i) { buf.setSample (0, i, x[(size_t) (b * bs + i)]); buf.setSample (1, i, 0.2f * (float) std::sin (0.07 * (b * bs + i))); }
            proc.processBlock (buf, none);
            for (int i = 0; i < bs; ++i) d = std::max (d, (double) std::fabs (buf.getSample (0, i) - buf.getSample (1, i)));
        }
        check (d < 1e-5, "width 0 % makes the output mono");
    }
    {   // mono layout
        MixToolsProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "drive", 12.0f); set (proc, "attack", 0.5f);
        auto y = runProc (proc, x, 1); bool fin = true; float pk = 0; for (float s : y) { fin = fin && std::isfinite (s); pk = std::max (pk, std::fabs (s)); }
        check (fin && rmsOf (y, 24000) > 0.05 && pk <= juce::Decibels::decibelsToGain (-1.0f) * 1.001f, "mono processing works and is limited");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! proc.checkBusesLayoutSupported (l2), "mono in / stereo out refused");
    }
    {   // every factory preset: loads, sounds, stays finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            MixToolsProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            const auto xin = harm (196.0, (int) rate * 3, rate);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i);
                proc.reset();
                auto y = runProc (proc, xin, 2);
                float pk = 0; bool fin = true; for (float v : y) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
                if (! fin || pk > 4.0f) { ++bad; names << mt::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (y, y.size() / 2) < 0.02) { ++silent; names << mt::factoryPresets()[(size_t) i].name << " silent; "; }
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent %s\n", MixToolsProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak < 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset produces sound");
        check (MixToolsProcessor().presets.numFactory() >= 12, ">= 12 factory presets");
    }
    {   // user preset round trip through a file in the (test) user folder
        MixToolsProcessor a; set (a, "width", 1.5f); set (a, "speed", 2); set (a, "lOn", 0);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == mt::PresetManager::userDir(), "user preset saved in the user preset folder");
        MixToolsProcessor b;
        check (b.presets.loadFile (f) && std::fabs (v (b, "width") - 1.5f) < 0.01f && v (b, "speed") == 2 && v (b, "lOn") == 0 && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        MixToolsProcessor a; a.uiWidth = 1080; a.uiHeight = 672;
        a.presets.loadFactory (5); set (a, "release", 420.0f); set (a, "order", 1); set (a, "ceiling", -3.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        MixToolsProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (v (b, "release") - 420.0f) < 0.5f && v (b, "order") == 1 && std::fabs (v (b, "ceiling") + 3.0f) < 0.05f, "parameters survive save / load");
        check (b.presets.currentName() == mt::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1080 && b.uiHeight == 672, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "order") == 1, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    mt::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mix-tools_shot_settings");
    mt::upd::dirOverride().deleteRecursively();
    { mt::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      mt::upd::writeJson (mt::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    MixToolsProcessor proc;
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (7);   // Punchy Drums: all three modules on for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<MixToolsEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (MixToolsProcessor::kBaseW * 3 / 2, MixToolsProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { mt::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/mix-tools/"; i.notes = "New limiter modes"; e->getPanel().demoBanner (i); }
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
    if (mode != "empty") mtui::demoDisplay (e->getPanel().display());
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
