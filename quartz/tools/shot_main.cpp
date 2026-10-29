// Developer tool (Linux): renders the Quartz editor to a PNG so it can be compared against the design.
//   QuartzShot --out file.png [--w 1100] [--demo] [--banner] [--q "Glass Scream"] [--a "Soft Feminize"] [--bypass b_tone,b_fb] [--off qon|aon] [--on id,id]
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String out = "quartz.png", pq, pa, byp, off, on; int w = 1100; bool demo = false, banner = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]); auto nextArg = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--out") out = nextArg(); else if (a == "--w") w = nextArg().getIntValue(); else if (a == "--demo") demo = true; else if (a == "--banner") banner = true;
        else if (a == "--q") pq = nextArg(); else if (a == "--a") pa = nextArg(); else if (a == "--bypass") byp = nextArg(); else if (a == "--off") off = nextArg(); else if (a == "--on") on = nextArg();
    }
    QuartzProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    if (std::getenv ("QZ_SHOT_DEBUG")) for (auto id : { "key", "scale" }) std::fprintf (stderr, "%s = %s\n", id, proc.apvts.getParameter (id)->getCurrentValueAsText().toRawUTF8());
    for (int s = 0; s < 2; ++s)
    {
        const auto& name = s == 0 ? pq : pa;
        if (name.isNotEmpty()) { const int idx = proc.presets.indexOfName (s, name); if (idx >= 0) proc.loadPreset (s, idx); else std::fprintf (stderr, "preset not found: %s\n", name.toRawUTF8()); }
    }
    for (auto& id : juce::StringArray::fromTokens (byp, ",", ""))
        if (auto* p = proc.apvts.getParameter (id.trim())) p->setValueNotifyingHost (1.0f);
    if (off.isNotEmpty()) for (auto& id : juce::StringArray::fromTokens (off, ",", "")) if (auto* p = proc.apvts.getParameter (id.trim())) p->setValueNotifyingHost (0.0f);
    for (auto& id : juce::StringArray::fromTokens (on, ",", "")) if (auto* p = proc.apvts.getParameter (id.trim())) p->setValueNotifyingHost (1.0f);
    {
        std::unique_ptr<QuartzEditor> ed (static_cast<QuartzEditor*> (proc.createEditor()));
        ed->enableDemo (demo);
        if (banner) { qz::upd::Result r; r.ok = true; r.info = { "1.0.3", "https://cmplxty.music/quartz-engine/", "", "" }; ed->showUpdateNotice (r); }
        ed->setSize (w, juce::roundToInt (w * 720.0 / 1100.0));
        for (int i = 0; i < 4; ++i) ed->tickForTest();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (out); f.deleteFile();
        juce::FileOutputStream os (f);
        if (os.openedOk()) { juce::PNGImageFormat().writeImageToStream (img, os); std::printf ("wrote %s (%dx%d)\n", out.toRawUTF8(), img.getWidth(), img.getHeight()); }
    }
    return 0;
}
