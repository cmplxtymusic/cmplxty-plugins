// Renders the real GlitchCrusherEditor to a PNG.  usage: GlitchCrusherScreenshot out.png [banner|menu|off]
#include "PluginEditor.h"
#include <set>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    gc::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gc_shot_settings");
    gc::upd::dirOverride().deleteRecursively();
    // never touch the network from the screenshot tool: pretend a check just failed (counts as the attempt for today)
    { gc::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      gc::upd::writeJson (gc::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc")
    {
        int fails = 0; auto check = [&] (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; };
        auto v = [] (GlitchCrusherProcessor& q, const char* id) { auto* r = q.apvts.getParameter (id); return r->convertFrom0to1 (r->getValue()); };
        auto setp = [] (GlitchCrusherProcessor& q, const char* id, float x) { auto* r = q.apvts.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (x)); };
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            GlitchCrusherProcessor q; q.setRateAndBufferSizeDetails (rate, 512); q.prepareToPlay (rate, 512);
            for (int i = 0; i < q.presets.numFactory(); ++i)
            {
                q.presets.loadFactory (i);
                juce::Random rng (i + 1); juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; float pk = 0; bool fin = true; double e = 0; long ph = 0;
                for (int blk = 0; blk < (int) (rate * 3 / 512); ++blk)
                {
                    for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) b.setSample (c, k, 0.5f * (float) std::sin ((ph + k) * 0.031) + 0.2f * (rng.nextFloat() - 0.5f) + (k == 0 && blk % 40 == 0 ? 0.9f : 0.0f));
                    ph += 512; q.processBlock (b, m);
                    for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) { const float x = b.getSample (c, k); fin = fin && std::isfinite (x); pk = std::max (pk, std::fabs (x)); e += x * x; }
                }
                if (! fin || pk > 4.0f) { ++bad; names << pk::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (e < 1.0) { ++silent; names << pk::factoryPresets()[(size_t) i].name << " silent; "; }
            }
        }
        std::printf ("  %d presets x 3 rates: %d bad, %d silent %s\n", (int) pk::factoryPresets().size(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak <= 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset passes sound");
        check (pk::factoryPresets().size() >= 12, ">= 12 factory presets");
        { GlitchCrusherProcessor tmp; std::set<juce::String> ids; for (auto* prm : tmp.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) ids.insert (r->getParameterID());
          bool ok = true; for (auto& fp : pk::factoryPresets()) for (auto& kv : fp.values) if (! ids.count (kv.first)) { ok = false; std::printf ("  unknown id %s in %s\n", kv.first, fp.name); }
          check (ok, "factory presets only use real parameter ids"); }
        {
            GlitchCrusherProcessor a; setp (a, "bits", 5.5f); setp (a, "reverse", 2); setp (a, "cutoff", 1234.0f);
            const auto f = a.presets.userFileFor ("Round Trip");
            check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == a.presets.userDir() && a.presets.userDir() == gc::upd::dataDir().getChildFile ("Presets"), "user preset saved in the user preset folder");
            GlitchCrusherProcessor b;
            check (b.presets.loadFile (f) && std::fabs (v (b, "bits") - 5.5f) < 0.02f && v (b, "reverse") == 2 && std::fabs (v (b, "cutoff") - 1234.0f) < 2.0f && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
            check (b.presets.userFiles().size() >= 1, "user presets are listed");
            f.deleteFile();
            check (! b.presets.loadFile (juce::File ("/nonexistent.cmplxtypreset")), "missing preset file refused");
        }
        {
            GlitchCrusherProcessor a; a.presets.loadFactory (5); setp (a, "mix", 0.4f);
            juce::MemoryBlock mb; a.getStateInformation (mb);
            GlitchCrusherProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
            check (std::fabs (v (b, "mix") - 0.4f) < 0.01f && v (b, "reverse") == 1, "parameters survive save / load");
            check (b.presets.currentName() == pk::factoryPresets()[5].name, "preset name survives save / load");
            juce::XmlElement old ("GlitchCrusherState"); old.setAttribute ("version", 2); old.setAttribute ("bits", 3.0);
            juce::MemoryBlock ob; juce::AudioProcessor::copyXmlToBinary (old, ob);
            GlitchCrusherProcessor c; c.setStateInformation (ob.getData(), (int) ob.getSize());
            check (std::fabs (v (c, "bits") - 3.0f) < 0.01f && c.presets.currentName() == "Init", "1.0.2 session (no preset name) still loads");
        }
        std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
        return fails;
    }
    GlitchCrusherProcessor proc;
    // a lively, non-default looking state
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("bits", 7.0f); set ("downsample", 6.0f); set ("drive", 0.45f); set ("glitchProb", 0.35f);
    set ("cutoff", 2400.0f); set ("resonance", 0.4f); set ("mix", 0.85f); set ("filterOn", 1.0f);
    if (mode.isEmpty() || mode == "presets") { proc.presets.loadFactory (3); set ("filterOn", 1.0f); }
    if (mode == "off") set ("glitchOn", 0.0f);
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
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<GlitchCrusherEditor*> (ed.get());
    if (mode == "banner") { gc::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/glitch-crusher/"; i.notes = "Smoother filter, new glitch rates"; e->demoBanner (i); }
    if (mode == "keys") { auto r = e->auditKeyboard(); std::printf ("%s", r.toRawUTF8()); auto m = e->auditMenus(); std::printf ("%s", m.toRawUTF8()); auto pa = e->auditPresets(); std::printf ("%s", pa.toRawUTF8()); return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1; }
    if (mode == "menu") e->demoKnobMenu();
    if (mode == "presets") e->getPresetBar().openMenu();
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png; png.writeImageToStream (img, os);
    std::printf ("wrote %s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
