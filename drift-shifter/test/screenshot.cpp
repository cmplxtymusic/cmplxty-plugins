// Renders the real DriftEditor to a PNG.  usage: DriftScreenshot out.png [banner|menu|eco|solo|removed|big|keys|params]
#include "PluginEditor.h"
#include "PresetKitTest.h"

namespace
{
// a lively test signal: a few sawtooth notes + noise, so the spectrum has something to show
void feed (DriftProcessor& proc, double sr, double seconds)
{
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);
    juce::Random rnd (7);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double ph[3] = { 0, 0, 0 };
    const double f[3] = { 110.0, 164.81, 220.0 };
    float lp = 0.0f;
    const int blocks = (int) (sr * seconds / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) { ph[k] += f[k] / sr; if (ph[k] > 1.0) ph[k] -= 1.0; s += (float) (2.0 * ph[k] - 1.0) * 0.12f; }
            const float n = rnd.nextFloat() * 2.0f - 1.0f; lp += 0.35f * (n - lp);
            s += lp * 0.05f + n * 0.004f;
            buf.setSample (0, i, s); buf.setSample (1, i, s * 0.98f);
        }
        midi.clear();
        proc.processBlock (buf, midi);
    }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    df::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("df_shot_settings");
    df::upd::dirOverride().deleteRecursively();
    // never touch the network from the screenshot tool: pretend a check just failed (counts as the attempt for today)
    { df::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      df::upd::writeJson (df::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return pk::test::procTests<DriftProcessor> ([] (DriftProcessor& q) -> pk::PresetManager& { return q.presets; }, df::upd::dataDir().getChildFile ("Presets"));
    DriftProcessor proc;
    // a lively, non-default looking state
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("coarse", 350.0f); set ("fine", 12.5f); set ("spread", 40.0f); set ("mix", 0.85f); set ("feedback", 0.25f);
    set ("xoLow", 180.0f); set ("xoHigh", 3500.0f);
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
    if (mode.isEmpty() || mode == "presets") proc.presets.loadFactory (2);
    if (mode == "removed") { set ("lowBand", 0.0f); set ("keyOn", 1.0f); }
    if (mode == "solo") set ("soloMid", 1.0f);
    if (mode == "eco") set ("eco", 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<DriftEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (1350, 885);
    if (mode == "banner") { df::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/drift-shifter/"; i.notes = "New presets, smoother crossover"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    feed (proc, 48000.0, 1.5);
    if (mode == "menu") e->getPanel().demoKnobMenu();
    if (mode == "presets") e->getPanel().getPresetBar().openMenu();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    feed (proc, 48000.0, 0.3);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png; png.writeImageToStream (img, os);
    std::printf ("wrote %s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
