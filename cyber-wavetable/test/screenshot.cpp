// Renders the real CyberWavetableEditor to a PNG, and runs the processor-level tests.
// usage: CyberWavetableScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
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

// processor-level: MIDI rendering, sample-accurate notes, controllers, mono layout, presets, user presets, state
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    // render `blocks` blocks; events: (sample index from start, message)
    auto render = [&] (CyberWavetableProcessor& proc, int blocks, std::vector<std::pair<int, juce::MidiMessage>> ev, int nch = 2, std::vector<float>* right = nullptr)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (nch, bs);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m;
            for (auto& e : ev) if (e.first >= b * bs && e.first < (b + 1) * bs) m.addEvent (e.second, e.first - b * bs);
            buf.clear();
            proc.processBlock (buf, m);
            for (int i = 0; i < bs; ++i) { out.push_back (buf.getSample (0, i)); if (right && nch > 1) right->push_back (buf.getSample (1, i)); }
        }
        return out;
    };
    auto set = [] (CyberWavetableProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto v = [] (CyberWavetableProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    auto on = [] (int n, float vel = 0.8f) { return juce::MidiMessage::noteOn (1, n, vel); };
    auto off = [] (int n) { return juce::MidiMessage::noteOff (1, n); };
    auto freq = [&] (const std::vector<float>& x, size_t a, size_t b)
    {
        int n = 0; double first = -1, last = 0;
        for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
        return n > 1 ? (n - 1) * sr / (last - first) : 0.0;
    };
    auto sine = [&] (CyberWavetableProcessor& p) { set (p, "aPos", 0.0f); set (p, "cutoff", 20000.0f); set (p, "reso", 0.0f); set (p, "fKey", 0.0f); set (p, "width", 0.0f); set (p, "fType", 0); };
    {
        CyberWavetableProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 0 && proc.getTotalNumOutputChannels() == 2 && proc.acceptsMidi() && proc.getLatencySamples() == 0, "synth: no audio in, stereo out, MIDI in, zero latency");
        check (proc.getParameters().size() == 52 && v (proc, "voices") == 8.0f && v (proc, "cutoff") == 12000.0f, "52 parameters with defaults (8 voices, 12 kHz cutoff)");
        check (proc.apvts.getParameter ("aUni")->getText (proc.apvts.getParameter ("aUni")->convertTo0to1 (5.0f), 20) == "5"
               && proc.apvts.getParameter ("aBank")->getText (proc.apvts.getParameter ("aBank")->convertTo0to1 (2.0f), 20) == "Vocal", "unison shows whole numbers, table names shown");
        auto y = render (proc, 20, {});
        check (rmsOf (y, 0) == 0.0, "no MIDI = silence");
        sine (proc);
        y = render (proc, 100, { { 0, on (69) }, { 24000, off (69) } });
        const double f = freq (y, 2400, 24000);
        std::printf ("  A4 through the processor: %.2f Hz, out meter %.2f\n", f, proc.outLevel());
        check (std::fabs (f - 440.0) < 0.3, "MIDI note 69 plays 440 Hz");
        check (rmsOf (y, 40000) < 1e-5, "note off releases to silence");
        // sample-accurate start: note at sample 1000 inside block 2
        CyberWavetableProcessor p2; p2.setRateAndBufferSizeDetails (sr, bs); p2.prepareToPlay (sr, bs); sine (p2); set (p2, "ampA", 1.0f);
        auto z = render (p2, 4, { { 1000, on (69, 1.0f) } });
        double before = 0, after = 0; for (size_t k = 0; k < 1000; ++k) before += std::fabs (z[k]); for (size_t k = 1000; k < 1200; ++k) after += std::fabs (z[k]);
        check (before == 0.0 && after > 1.0, "notes start on their sample, not the block boundary");
    }
    {   // controllers: pitch bend, mod wheel (via the mod matrix), sustain, all-notes-off
        CyberWavetableProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs); sine (proc);
        auto y = render (proc, 50, { { 0, juce::MidiMessage::pitchWheel (1, 16383) }, { 0, on (69) } });
        check (std::fabs (freq (y, 4800, 24000) - 440.0 * std::pow (2.0, 2.0 / 12.0)) < 0.6, "pitch bend +2 semitones");
        set (proc, "m1Src", 5); set (proc, "m1Dst", 6); set (proc, "m1Amt", 0.5f);
        y = render (proc, 50, { { 0, juce::MidiMessage::pitchWheel (1, 8192) }, { 0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
        check (std::fabs (freq (y, 4800, 24000) - 880.0) < 1.0, "mod wheel (CC1) -> pitch through the mod matrix");
        y = render (proc, 50, { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0, off (69) } });
        std::printf ("  held rms %.3f\n", rmsOf (y, 12000));
        check (rmsOf (y, 12000) > 0.02, "sustain pedal (CC64) holds the note");
        y = render (proc, 50, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } });
        std::printf ("  pedal-up tail rms %.6f\n", rmsOf (y, 12000));
        check (rmsOf (y, 21000) < 1e-4, "pedal up releases");
        y = render (proc, 20, { { 0, on (60) }, { 0, on (64) }, { 2400, juce::MidiMessage::allSoundOff (1) } });
        check (rmsOf (y, 2400) == 0.0 && proc.dspEngine().activeVoices.load() == 0, "all sound off (CC120) cuts every voice");
        y = render (proc, 50, { { 0, on (60) }, { 0, on (64) }, { 2400, juce::MidiMessage::allNotesOff (1) } });
        std::printf ("  all-notes-off tail rms %.6f\n", rmsOf (y, 21000));
        check (rmsOf (y, 21000) < 1e-5, "all notes off (CC123) releases every voice");
    }
    {   // polyphony via the voices parameter, UI-queued notes
        CyberWavetableProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs); set (proc, "voices", 3);
        render (proc, 10, { { 0, on (60) }, { 0, on (62) }, { 0, on (64) }, { 0, on (65) }, { 0, on (67) } });
        check (proc.dspEngine().activeVoices.load() == 3, "voice count parameter caps polyphony");
        CyberWavetableProcessor q; q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs);
        q.queueNote (60, 0.9f); auto y = render (q, 10, {});
        check (rmsOf (y, 0) > 0.01, "notes queued from the editor play");
        q.queueNote (60, 0.0f); y = render (q, 60, {});
        check (rmsOf (y, 24000) < 1e-5, "and release");
    }
    {   // mono layout
        CyberWavetableProcessor proc;
        auto l = proc.getBusesLayout(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono output supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "aUni", 7); set (proc, "width", 1.0f);
        auto y = render (proc, 50, { { 0, on (57) } }, 1); bool fin = true; for (float s : y) fin = fin && std::isfinite (s);
        check (fin && rmsOf (y, 4800) > 0.02, "mono output renders");
    }
    {   // every factory preset: plays a chord, stays finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            CyberWavetableProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i);
                proc.reset();
                const int blocks = (int) (rate * 3 / bs);
                std::vector<float> R;
                auto y = render (proc, blocks, { { 0, on (48, 0.9f) }, { 0, on (55, 0.7f) }, { 0, on (63, 0.8f) }, { blocks * bs / 2, juce::MidiMessage::controllerEvent (1, 1, 100) },
                                                 { blocks * bs / 3, juce::MidiMessage::pitchWheel (1, 12000) }, { blocks * bs / 2, juce::MidiMessage::channelPressureChange (1, 90) } }, 2, &R);
                float pk = 0; bool fin = true; for (size_t k = 0; k < y.size(); ++k) { fin = fin && std::isfinite (y[k]) && std::isfinite (R[k]); pk = std::max ({ pk, std::fabs (y[k]), std::fabs (R[k]) }); }
                if (! fin || pk > 2.0f) { ++bad; names << cw::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (std::vector<float> (y.begin(), y.begin() + (long) (rate / 2)), 0) < 0.01) { ++silent; names << cw::factoryPresets()[(size_t) i].name << " silent @" << (int) rate << " rms=" << rmsOf (std::vector<float> (y.begin(), y.begin() + (long) (rate / 2)), 0) << "; "; }
                render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent %s\n", CyberWavetableProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak <= 2 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset produces sound");
        check (CyberWavetableProcessor().presets.numFactory() >= 24, ">= 24 factory presets");
    }
    {   // user preset round trip through a file in the (test) user folder
        CyberWavetableProcessor a; set (a, "aBank", 4); set (a, "cutoff", 777.0f); set (a, "m2Dst", 7);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == cw::PresetManager::userDir(), "user preset saved in the user preset folder");
        CyberWavetableProcessor b;
        check (b.presets.loadFile (f) && v (b, "aBank") == 4 && std::fabs (v (b, "cutoff") - 777.0f) < 1.0f && v (b, "m2Dst") == 7 && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        CyberWavetableProcessor a; a.uiWidth = 1320; a.uiHeight = 888;
        a.presets.loadFactory (5); set (a, "ampR", 420.0f); set (a, "fType", 3); set (a, "volume", -3.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        CyberWavetableProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (v (b, "ampR") - 420.0f) < 0.5f && v (b, "fType") == 3 && std::fabs (v (b, "volume") + 3.0f) < 0.05f, "parameters survive save / load");
        check (b.presets.currentName() == cw::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1320 && b.uiHeight == 888, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "fType") == 3, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    cw::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cyber-wavetable_shot_settings");
    cw::upd::dirOverride().deleteRecursively();
    { cw::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      cw::upd::writeJson (cw::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    CyberWavetableProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (1);   // Neon Supersaw for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<CyberWavetableEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (CyberWavetableProcessor::kBaseW * 3 / 2, CyberWavetableProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { cw::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/cyber-wavetable/"; i.notes = "New wavetables"; e->getPanel().demoBanner (i); }
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
