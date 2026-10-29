// Renders the real RewiredEditor to a PNG, and runs the processor-level tests.
// usage: RewiredScreenshot out.png [banner|menu|big|empty|keys|params|proc|presets]
#include "PluginEditor.h"
#include "PresetKitTest.h"
#include <random>

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }

// a plausible 16 s result for the screenshots / state tests: bass line, top melody, chord pads, a four-on-the-floor kit
rw::Result demoResult()
{
    rw::Result r; r.ok = true; r.bpm = 120.0; r.seconds = 16.0; r.melodyFrom = "vocals";
    std::mt19937 rng (5);
    const int roots[] = { 45, 41, 48, 43 }, mel[] = { 76, 74, 72, 74, 76, 79, 77, 76 };
    for (int bar = 0; bar < 8; ++bar)
    {
        const double t = bar * 2.0; const int root = roots[bar % 4];
        for (int k = 0; k < 4; ++k) r.parts[rw::kBass].push_back ({ t + k * 0.5, t + k * 0.5 + 0.4, root - 12 + (k == 3 ? 7 : 0), 100 });
        for (int iv : { 12, 16, 19 }) r.parts[rw::kChords].push_back ({ t, t + 1.9, root + iv, 80 });
        for (int k = 0; k < 4; ++k) r.parts[rw::kMelody].push_back ({ t + k * 0.5, t + k * 0.5 + 0.45, mel[(bar * 4 + k + (int) (rng() % 2)) % 8], 96 });
        for (int b = 0; b < 4; ++b)
        {
            const double bt = t + b * 0.5;
            r.parts[rw::kKick].push_back ({ bt, bt + 0.1, 36, 110 });
            if (b % 2) r.parts[rw::kSnare].push_back ({ bt, bt + 0.1, 38, 105 });
            r.parts[rw::kHats].push_back ({ bt + 0.25, bt + 0.3, 42, 80 });
        }
        if (bar % 4 == 0) r.parts[rw::kCymbals].push_back ({ t, t + 0.5, 49, 100 });
    }
    return r;
}

void writeTone (const juce::File& f, double sr, double secs)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0));
    juce::AudioBuffer<float> b (2, (int) (sr * secs));
    for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.3f * (float) std::sin (2 * 3.14159265 * 220.0 * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    RewiredProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.25f); b.setSample (1, i, -0.5f); }
        proc.processBlock (b, m);
        check (b.getSample (0, 100) == 0.25f && b.getSample (1, 300) == -0.5f && m.isEmpty(), "audio passes through untouched, no MIDI out");
    }
    check (rwui::guessStemSlot ("My Song - Bass.wav") == RewiredProcessor::kBassStem && rwui::guessStemSlot ("drums_01.flac") == RewiredProcessor::kDrumsStem
           && rwui::guessStemSlot ("Lead Vox.wav") == RewiredProcessor::kMelodyStem && rwui::guessStemSlot ("keys.wav") == RewiredProcessor::kChordsStem, "stem names map to slots");

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rw_proc_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    const auto song = tmp.getChildFile ("Test Song.wav"), bass = tmp.getChildFile ("bass.wav");
    writeTone (song, 44100.0, 2.0); writeTone (bass, 48000.0, 2.0);
    check (proc.setInput (RewiredProcessor::kSong, song).isEmpty() && proc.songMode() && proc.title() == "Test Song", "a song loads");
    check (proc.setInput (RewiredProcessor::kBassStem, bass).isEmpty() && ! proc.songMode() && proc.input (RewiredProcessor::kSong) == juce::File(), "a stem replaces the song (stems mode)");
    check (proc.setInput (RewiredProcessor::kDrumsStem, juce::File ("/nonexistent.wav")).isNotEmpty(), "missing file reports an error");
    juce::File junkFile = tmp.getChildFile ("junk.wav"); junkFile.replaceWithText ("not audio");
    check (proc.setInput (RewiredProcessor::kDrumsStem, junkFile).isNotEmpty(), "unreadable file reports an error");

    // the real models, when they can be found (RW_MODEL_DIR or an install): a full run in both modes
    if (proc.modelDir() != juce::File())
    {
        proc.startProcess();
        check (proc.testWaitProcess (120000), "stems run finishes");
        proc.setInput (RewiredProcessor::kSong, song);
        proc.startProcess();
        check (proc.testWaitProcess (120000), "song run finishes (separation + transcription)");
        check (proc.result().parts[rw::kBass].size() + proc.result().parts[rw::kChords].size() + proc.result().parts[rw::kMelody].size() > 0, "a 220 Hz tone gives pitched notes");
        proc.startProcess(); proc.cancelProcess();
        check (proc.status() != RewiredProcessor::Status::working, "cancel stops the worker");
    }
    else std::printf ("note: models not found (set RW_MODEL_DIR) - skipping the real-model run\n");

    proc.testSetResult (demoResult());
    proc.setPartOn (rw::kHats, false);
    juce::String err;
    auto all = proc.exportMidi (false, err), sel = proc.exportMidi (true, err), one = proc.exportPart (rw::kBass, err);
    check (all.existsAsFile() && sel.existsAsFile() && one.existsAsFile() && err.isEmpty(), "export all / selected / one part");
    auto readMidi = [] (const juce::File& f, juce::StringArray& names) {
        juce::MidiFile mf; juce::FileInputStream in (f); if (! in.openedOk() || ! mf.readFrom (in)) return -1;
        for (int t = 1; t < mf.getNumTracks(); ++t)
            for (auto* e : *mf.getTrack (t)) if (e->message.isTrackNameEvent()) { names.add (e->message.getTextFromTextMetaEvent()); break; }
        return mf.getNumTracks() - 1; };
    juce::StringArray na, ns, n1;
    check (readMidi (all, na) == 7 && na.joinIntoString (",") == "Bass,Melody,Chords,Kick,Snare,Hats,Cymbals", "full file: 7 named tracks");
    check (readMidi (sel, ns) == 6 && ! ns.contains ("Hats"), "selected file leaves out the switched-off part");
    check (readMidi (one, n1) == 1 && n1[0] == "Bass" && one.getFileName().endsWith (" - Bass.mid"), "drag file holds one named part");
    {
        juce::MidiFile mf; juce::FileInputStream in (all); mf.readFrom (in);
        bool drumsOn10 = true; for (auto* e : *mf.getTrack (4)) if (e->message.isNoteOn()) drumsOn10 &= e->message.getChannel() == 10;
        check (drumsOn10, "drums on MIDI channel 10");
    }

    juce::MemoryBlock st; proc.getStateInformation (st);
    RewiredProcessor p2; p2.setStateInformation (st.getData(), (int) st.getSize());
    check (p2.hasResult() && p2.result().parts[rw::kKick].size() == demoResult().parts[rw::kKick].size() && ! p2.partOn (rw::kHats) && p2.partOn (rw::kBass), "result + part switches survive save / load");
    check (p2.input (RewiredProcessor::kSong) == song && p2.songMode(), "inputs survive save / load");
    p2.clearAll(); check (! p2.hasResult() && ! p2.hasAnyInput() && p2.status() == RewiredProcessor::Status::idle, "clear");
    const char junk[] = "RWR1\xff\xff\xff\x7fgarbage"; p2.setStateInformation (junk, sizeof (junk)); check (true, "garbage state ignored");
    juce::String e2; check (! p2.exportMidi (false, e2).existsAsFile() && e2.isNotEmpty(), "export without a result explains why");
    proc.exportFolder().deleteRecursively();
    tmp.deleteRecursively();
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    rw::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rw_shot_settings");
    rw::upd::dirOverride().deleteRecursively();
    { rw::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      rw::upd::writeJson (rw::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<RewiredProcessor> ([] (RewiredProcessor& q) -> pk::PresetManager& { return q.presets; }, rw::upd::dataDir().getChildFile ("Presets"), 2, 4.0f, 1.0,
                                                       [] (const juce::MemoryBlock& mb) -> std::unique_ptr<juce::XmlElement> {   // "RWR1" + int + XML text
                                                           if (mb.getSize() < 8 || std::memcmp (mb.getData(), "RWR1", 4) != 0) return nullptr;
                                                           juce::MemoryInputStream in (mb, false); in.skipNextBytes (4); const int n = in.readInt();
                                                           juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, n); return juce::parseXML (xm.toString()); });
    RewiredProcessor proc;
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
    if (mode != "empty")
    {
        juce::File demo ("/workspace/rw-songs/song1/mix.wav");
        if (demo.existsAsFile()) proc.setInput (RewiredProcessor::kSong, demo);
        proc.testSetResult (demoResult());
        proc.setPartOn (rw::kCymbals, false);
    }
    if (mode.isEmpty() || mode == "presets") proc.presets.loadFactory (1);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<RewiredEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (RewiredProcessor::kBaseW * 3 / 2, RewiredProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { rw::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/rewired/"; i.notes = "Better chord detection"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
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
