// Renders the real DrumPlayerEditor to a PNG, and runs the processor-level tests.
// usage: DrumPlayerScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
#include "PluginEditor.h"
#include <random>
#include <cmath>

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }
double rmsOf (const std::vector<float>& x, size_t from, size_t to = 0) { if (to == 0 || to > x.size()) to = x.size(); double s = 0; for (size_t i = from; i < to; ++i) s += x[i] * x[i]; return to > from ? std::sqrt (s / (double) (to - from)) : 0.0; }
juce::File writeWav (const juce::String& name, double hz, int n, int ch)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::AudioBuffer<float> b (ch, n);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, (c == 0 ? 0.5f : 0.25f) * (float) std::sin (2 * 3.14159265358979 * hz * i / 44100.0));
    juce::WavAudioFormat wav;
    if (auto os = f.createOutputStream())
        if (auto* w = wav.createWriterFor (os.get(), 44100.0, (unsigned) ch, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> ww (w); ww->writeFromAudioSampleBuffer (b, 0, n); }
    return f;
}

// processor-level: MIDI pads, sample-accurate hits, choke, controllers, user samples in presets + state, mono, presets, state
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    auto render = [&] (DrumPlayerProcessor& proc, int blocks, std::vector<std::pair<int, juce::MidiMessage>> ev, int nch = 2, std::vector<float>* right = nullptr)
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
    auto set = [] (DrumPlayerProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto v = [] (DrumPlayerProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    auto txt = [] (DrumPlayerProcessor& p, const char* id, float val) { auto* q = p.apvts.getParameter (id); return q->getText (q->convertTo0to1 (val), 20); };
    auto on = [] (int n, float vel = 0.8f) { return juce::MidiMessage::noteOn (1, n, vel); };
    auto freq = [&] (const std::vector<float>& x, size_t a, size_t b)
    {
        int n = 0; double first = -1, last = 0;
        for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
        return n > 1 ? (n - 1) * sr / (last - first) : 0.0;
    };
    {
        DrumPlayerProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 0 && proc.getTotalNumOutputChannels() == 2 && proc.acceptsMidi() && proc.getLatencySamples() == 0, "instrument: no audio in, stereo out, MIDI in, zero latency");
        check (proc.getParameters().size() == 52 && v (proc, "p1Sound") == 1 && v (proc, "p8Sound") == 10 && v (proc, "p5Choke") == 1 && v (proc, "baseNote") == 1, "52 parameters with defaults (kit, hats choke, base C1)");
        check (txt (proc, "p1Pan", -0.5f) == "L50" && txt (proc, "p1Pan", 0.0f) == "C" && txt (proc, "p1Decay", 4000.0f) == "Full" && txt (proc, "p1Sound", 16) == "Sample", "pan / decay / sound text");
        auto y = render (proc, 20, {});
        check (rmsOf (y, 0) == 0.0, "no MIDI = silence");
        y = render (proc, 20, { { 0, on (36) }, { 4800, juce::MidiMessage::noteOff (1, 36) } });
        check (rmsOf (y, 0, 4800) > 0.02 && rmsOf (y, 4800, 9600) > 0.001, "C1 plays pad 1; note off is ignored (one-shot)");
        render (proc, 4, { { 0, juce::MidiMessage::allSoundOff (1) } });
        DrumPlayerProcessor p2; p2.setRateAndBufferSizeDetails (sr, bs); p2.prepareToPlay (sr, bs);
        auto z = render (p2, 4, { { 1000, on (38, 1.0f) } });
        double before = 0, after = 0; for (size_t k = 0; k < 1000; ++k) before += std::fabs (z[k]); for (size_t k = 1000; k < 1200; ++k) after += std::fabs (z[k]);
        check (before == 0.0 && after > 0.5, "hits start on their sample, not the block boundary");
        y = render (proc, 10, { { 0, on (36) }, { 0, on (38) }, { 0, on (42) }, { 2400, juce::MidiMessage::allSoundOff (1) } });
        check (rmsOf (y, 2400) == 0.0 && proc.dspEngine().activeVoices.load() == 0, "all sound off (CC120) cuts every voice");
        y = render (proc, 10, { { 0, on (36) }, { 0, on (40) }, { 0, on (43) }, { 2400, juce::MidiMessage::allNotesOff (1) } });
        check (rmsOf (y, 3000) == 0.0, "all notes off (CC123) fades every voice");
        set (proc, "baseNote", 3);   // C3 (60)
        y = render (proc, 10, { { 0, on (36) } }); check (rmsOf (y, 0) == 0.0, "pad 1 note moved to C3: 36 now silent");
        y = render (proc, 10, { { 0, on (60) } }); check (rmsOf (y, 0) > 0.02, "and 60 plays pad 1");
        proc.queuePad (2, 0.9f); y = render (proc, 10, {});
        check (rmsOf (y, 0) > 0.01 && proc.dspEngine().hits[2].load() >= 1, "pads clicked in the editor play");
    }
    {   // choke through the processor: pad 4 (D#1, closed hat) cuts pad 5 (E1, open hat)
        DrumPlayerProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "p5Sound", 5); set (proc, "p4Sound", 4); set (proc, "p4Level", -60.0f);
        auto y = render (proc, 30, { { 0, on (40, 1) }, { 4800, on (39, 1) } });
        const double choked = rmsOf (y, 4800 + 480, 14400);
        render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
        set (proc, "p4Choke", 0);
        y = render (proc, 30, { { 0, on (40, 1) }, { 4800, on (39, 1) } });
        const double open = rmsOf (y, 4800 + 480, 14400);
        std::printf ("  open hat after closed hat: choked %.6f, unchoked %.6f\n", choked, open);
        check (open > 1e-3 && choked < open * 0.05, "choke group: pad 4 hit silences pad 5; choke off lets it ring");
    }
    {   // user samples: load from a WAV, pitch, preset + state round trip with the audio embedded
        const auto wavM = writeWav ("cmplxty-dp-test-mono.wav", 441.0, 44100, 1), wavS = writeWav ("cmplxty-dp-test-st.wav", 882.0, 22050, 2);
        DrumPlayerProcessor a; a.setRateAndBufferSizeDetails (sr, bs); a.prepareToPlay (sr, bs);
        check (a.loadSample (2, wavM) && a.hasUserSample (2) && v (a, "p3Sound") == 16 && a.padSoundName (2) == "cmplxty-dp-test-mono", "WAV loads into pad 3 and switches it to Sample");
        check (a.loadSample (6, wavS) && ! a.loadSample (7, juce::File ("/nonexistent/x.wav")) && ! a.hasUserSample (7), "stereo WAV loads; a missing file is refused");
        std::vector<float> R;
        auto y = render (a, 30, { { 0, on (38, 1) } }, 2, &R);
        const double f = freq (y, 200, 12000); std::printf ("  user sample: %.2f Hz (441 expected)\n", f);
        check (std::fabs (f - 441.0) < 0.5 && std::fabs (rmsOf (y, 200, 12000) - rmsOf (R, 200, 12000)) < 1e-3, "44.1k WAV plays at its pitch at 48k, mono to both sides");
        set (a, "p3Tune", 12.0f); render (a, 2, { { 0, juce::MidiMessage::allSoundOff (1) } }); y = render (a, 30, { { 0, on (38, 1) } });
        check (std::fabs (freq (y, 600, 12000) - 882.0) < 1.0, "pad tune applies to user samples");
        R.clear(); y = render (a, 30, { { 0, juce::MidiMessage::allSoundOff (1) }, { 480, on (42, 1) } }, 2, &R);
        check (std::fabs (rmsOf (y, 1000, 10000) / rmsOf (R, 1000, 10000) - 2.0) < 0.05, "stereo WAV keeps left / right");
        const auto pf = a.presets.userFileFor ("Kit Round Trip");
        check (a.presets.saveFile (pf, "Kit Round Trip"), "user kit preset saved");
        DrumPlayerProcessor b; b.setRateAndBufferSizeDetails (sr, bs); b.prepareToPlay (sr, bs);
        wavM.deleteFile(); wavS.deleteFile();   // the preset must carry the audio itself
        check (b.presets.loadFile (pf) && b.hasUserSample (2) && b.hasUserSample (6) && v (b, "p3Tune") == 12.0f && b.padSoundName (6) == "cmplxty-dp-test-st", "kit preset restores both samples with the files gone");
        y = render (b, 30, { { 0, on (38, 1) } });
        check (std::fabs (freq (y, 600, 12000) - 882.0) < 1.0, "restored sample plays");
        juce::MemoryBlock mb; b.getStateInformation (mb);
        DrumPlayerProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
        check (c.hasUserSample (2) && c.hasUserSample (6) && c.padSample (6) != nullptr && c.padSample (6)->len() == 22050, "samples survive the host's save / load");
        c.presets.loadFactory (1);
        check (! c.hasUserSample (2) && v (c, "p3Sound") != 16, "loading a factory kit clears user samples");
        c.setStateInformation (mb.getData(), (int) mb.getSize()); c.clearSample (6);
        check (! c.hasUserSample (6) && c.hasUserSample (2) && c.padSoundName (6) == "(drop a sample)", "clear sample");
        pf.deleteFile();
    }
    {   // mono layout
        DrumPlayerProcessor proc;
        auto l = proc.getBusesLayout(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono output supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "p1Pan", -1.0f);
        auto y = render (proc, 20, { { 0, on (36) }, { 0, on (42) } }, 1); bool fin = true; for (float s : y) fin = fin && std::isfinite (s);
        check (fin && rmsOf (y, 0, 4800) > 0.02, "mono output renders (hard-panned pad still heard)");
    }
    {   // every factory kit: all 8 pads hit, plus a fast roll, finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            DrumPlayerProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i); proc.reset();
                const int blocks = (int) (rate * 2 / bs); const int base = 36;
                std::vector<std::pair<int, juce::MidiMessage>> ev;
                for (int p = 0; p < 8; ++p) ev.push_back ({ p * 4000, on (base + p, 1.0f) });
                for (int k = 0; k < 40; ++k) ev.push_back ({ 40000 + k * 300, on (base + k % 8, 0.5f + 0.5f * (k % 3) / 2.0f) });
                std::vector<float> R; auto y = render (proc, blocks, ev, 2, &R);
                float pk = 0; bool fin = true; for (size_t k = 0; k < y.size(); ++k) { fin = fin && std::isfinite (y[k]) && std::isfinite (R[k]); pk = std::max ({ pk, std::fabs (y[k]), std::fabs (R[k]) }); }
                if (! fin || pk > 4.0f) { ++bad; names << cw::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                for (int p = 0; p < 8; ++p) if (rmsOf (y, (size_t) (p * 4000), (size_t) (p * 4000 + 2000)) < 0.003) { ++silent; names << cw::factoryPresets()[(size_t) i].name << " pad " << (p + 1) << " silent; "; }
                render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
            }
        }
        std::printf ("  %d factory kits x 3 rates: %d bad, %d silent pads %s\n", DrumPlayerProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory kit: no NaN / inf, peak <= 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every pad of every factory kit makes sound");
        check (DrumPlayerProcessor().presets.numFactory() >= 16, ">= 16 factory kits");
    }
    {   // user preset round trip without samples
        DrumPlayerProcessor a; set (a, "p2Sound", 12); set (a, "p7Decay", 333.0f); set (a, "kitTune", -3.0f);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == cw::PresetManager::userDir(), "user preset saved in the user preset folder");
        DrumPlayerProcessor b;
        check (b.presets.loadFile (f) && v (b, "p2Sound") == 12 && std::fabs (v (b, "p7Decay") - 333.0f) < 1.0f && v (b, "kitTune") == -3.0f && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        DrumPlayerProcessor a; a.uiWidth = 1320; a.uiHeight = 1046;
        a.presets.loadFactory (5); set (a, "p3Pan", 0.4f); set (a, "p8Choke", 3); set (a, "volume", -7.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        DrumPlayerProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (v (b, "p3Pan") - 0.4f) < 0.02f && v (b, "p8Choke") == 3 && std::fabs (v (b, "volume") + 7.0f) < 0.05f, "parameters survive save / load");
        check (b.presets.currentName() == cw::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1320 && b.uiHeight == 1046, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "p8Choke") == 3, "garbage state ignored");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    cw::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drum-player_shot_settings");
    cw::upd::dirOverride().deleteRecursively();
    { cw::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      cw::upd::writeJson (cw::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    DrumPlayerProcessor proc;
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (2);   // Techno Warehouse for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<DrumPlayerEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (DrumPlayerProcessor::kBaseW * 3 / 2, DrumPlayerProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { cw::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/drum-player/"; i.notes = "New kits"; e->getPanel().demoBanner (i); }
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
