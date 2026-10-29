// Renders the real GlitchCrusherEditor to a PNG.  usage: GlitchCrusherScreenshot out.png [banner|menu|off]
#include "PluginEditor.h"

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
    GlitchCrusherProcessor proc;
    // a lively, non-default looking state
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("bits", 7.0f); set ("downsample", 6.0f); set ("drive", 0.45f); set ("glitchProb", 0.35f);
    set ("cutoff", 2400.0f); set ("resonance", 0.4f); set ("mix", 0.85f); set ("filterOn", 1.0f);
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
    if (mode == "keys") { auto r = e->auditKeyboard(); std::printf ("%s", r.toRawUTF8()); auto m = e->auditMenus(); std::printf ("%s", m.toRawUTF8()); return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK")) ? 0 : 1; }
    if (mode == "menu") e->demoKnobMenu();
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
