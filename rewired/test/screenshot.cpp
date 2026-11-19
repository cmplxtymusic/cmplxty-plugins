// Renders the real RewiredEditor to a PNG, and runs the processor-level tests.
// usage: RewiredScreenshot out.png [banner|menu|big|empty|keys|params|proc|presets|map|mapmidi|mapempty]
//   map / mapmidi: with RW_MAP_SONG=<file> (and the models) a real run of that song, else the demo result; mapmidi switches real MIDI on for two lanes
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
    // 1.0.1 instrument sub-parts: chords = strings for the first half, piano after; melody all horns
    rw::SubPart st, pn, hn;
    st.inst = rw::kInstStrings; st.name = "Strings"; st.confidence = 0.71f;
    pn.inst = rw::kInstKeys; pn.name = "Piano / Keys"; pn.confidence = 0.64f;
    hn.inst = rw::kInstHorns; hn.name = "Horns"; hn.confidence = 0.58f;
    for (auto& n : r.parts[rw::kChords]) (n.start < 8.0 ? st : pn).notes.push_back (n);
    hn.notes = r.parts[rw::kMelody];
    r.subs[rw::kChords] = { st, pn }; r.subs[rw::kMelody] = { hn };
    r.map = rw::buildSongMap (r, {});   // 1.1.0: a map from the notes alone (no stem audio here)
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
    {
        const auto a = rwui::assignStemSlots ({ "/x/Song - Horns.wav", "/x/mystery.wav", "/x/BASS.flac", "/x/vox.wav", "/x/extra.wav" }, {});
        check (a.size() == 5 && a[0] == RewiredProcessor::kChordsStem && a[2] == RewiredProcessor::kBassStem && a[3] == RewiredProcessor::kMelodyStem
               && a[1] == RewiredProcessor::kDrumsStem && a[4] == -1, "several dropped stems: by name, then the empty slots, extras left out");
        std::array<bool, 5> occ {}; occ[RewiredProcessor::kChordsStem] = true;
        const auto b = rwui::assignStemSlots ({ "/x/a.wav", "/x/b.wav" }, occ);
        check (b[0] == RewiredProcessor::kMelodyStem && b[1] == RewiredProcessor::kBassStem, "unnamed stems fill empty slots before replacing a loaded one");
        check (rw::instFromFileName ("Brass Section") == rw::kInstHorns && rw::instFromFileName ("violins") == rw::kInstStrings && rw::instFromFileName ("mix") == -1,
               "stem file names can name the instrument");
    }

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

    proc.setInput (RewiredProcessor::kSong, song);   // (the model run above may have left stems mode)
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
    check (readMidi (all, na) == 8 && na.joinIntoString (",") == "Bass,Melody (Horns),Chords - Strings,Chords - Piano / Keys,Kick,Snare,Hats,Cymbals",
           "full file: a named track per part, one per instrument where a part has several");
    check (readMidi (sel, ns) == 7 && ! ns.contains ("Hats"), "selected file leaves out the switched-off part");
    {
        auto sub = proc.exportSub (rw::kChords, 0, err); juce::StringArray nsub;
        check (sub.existsAsFile() && readMidi (sub, nsub) == 1 && nsub[0] == "Chords - Strings" && sub.getFileName().endsWith (" - Chords - Strings.mid"),
               "one instrument drags out on its own");
        juce::MidiFile mf; juce::FileInputStream in (sub); mf.readFrom (in); int prog = -1, notes = 0;
        for (auto* e : *mf.getTrack (1)) { if (e->message.isProgramChange()) prog = e->message.getProgramChangeNumber(); if (e->message.isNoteOn()) ++notes; }
        check (prog == rw::instGmProgram (rw::kInstStrings, rw::kChords) && notes == (int) demoResult().subs[rw::kChords][0].notes.size(), "its track has the instrument's GM program and only its notes");
        juce::String e3; check (! proc.exportSub (rw::kChords, 5, e3).existsAsFile() && e3.isNotEmpty(), "a missing instrument explains why");
    }
    check (readMidi (one, n1) == 1 && n1[0] == "Bass" && one.getFileName().endsWith (" - Bass.mid"), "drag file holds one named part");
    {
        juce::MidiFile mf; juce::FileInputStream in (all); mf.readFrom (in);
        bool drumsOn10 = true; for (auto* e : *mf.getTrack (5)) if (e->message.isNoteOn()) drumsOn10 &= e->message.getChannel() == 10;
        check (drumsOn10, "drums on MIDI channel 10");
    }

    // ---- 1.1.0 MAP mode: the file FL Studio gets (markers, named tracks, blocks or real notes), and the map in the saved state
    {
        const auto r = proc.result();
        check (r.map.ok && r.map.bars == 8 && ! r.map.sections.empty() && std::abs (r.map.bpm - 120.0) < 0.01 && std::abs (r.map.firstDownbeat) < 0.01,
               "demo result has a map: 120 bpm, bar 1 at 0 s, 8 bars");
        juce::StringArray lanes; for (auto& L : r.map.lanes) lanes.add (L.name);
        check (lanes.joinIntoString (",") == "Drums,Kick,Snare,Hats,Cymbals,Bass,Chords,Chords - Strings,Chords - Piano / Keys,Vocals",
               ("map lanes: drums + pieces, bass, chords + instruments, vocals = the sung melody (" + lanes.joinIntoString (",") + ")").toRawUTF8());
        int vocBars = 0; for (auto& L : r.map.lanes) if (L.name == "Vocals") for (auto b : L.barOn) vocBars += b;
        check (vocBars == 8, "vocal lane (sung melody, no stem audio): all 8 bars");
        int kickBars = 0; for (auto& L : r.map.lanes) if (L.name == "Kick") for (auto b : L.barOn) kickBars += b;
        check (kickBars == 8, "kick lane plays in all 8 bars");
        const rw::MapLane* strings = nullptr; for (auto& L : r.map.lanes) if (L.name == "Chords - Strings") strings = &L;
        check (strings != nullptr && strings->blocks.size() == 1 && std::abs (strings->blocks[0].start) < 0.01 && std::abs (strings->blocks[0].end - 8.0) < 0.6,
               "strings lane: one block over the first 4 bars (0-8 s)");
        auto mapF = proc.exportMap (err);
        check (mapF.existsAsFile() && mapF.getFileName().endsWith (" - Map.mid") && err.isEmpty(), "EXPORT MAP writes <title> - Map.mid");
        juce::MidiFile mf; { juce::FileInputStream in (mapF); mf.readFrom (in); }
        juce::StringArray markers, names; int blocks = -1, bassNotes = 0;
        for (auto* e : *mf.getTrack (0)) if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6) markers.add (e->message.getTextFromTextMetaEvent());
        for (int t = 1; t < mf.getNumTracks(); ++t)
        {
            juce::String nm; int notes = 0;
            for (auto* e : *mf.getTrack (t)) { if (e->message.isTrackNameEvent() && nm.isEmpty()) nm = e->message.getTextFromTextMetaEvent(); if (e->message.isNoteOn()) ++notes; }
            names.add (nm);
            if (nm == "Chords - Strings") blocks = notes;
        }
        juce::StringArray secs; for (auto& sc : r.map.sections) secs.add (juce::String (sc.label)); secs.add ("End");
        check (markers == secs, ("one marker per section + End (" + markers.joinIntoString (",") + ")").toRawUTF8());
        check (names[0] == "Sections" && names.size() == (int) r.map.lanes.size() + 1 && names[1] == "Drums" && names[names.size() - 1] == "Vocals", "a Sections track, then one named track per lane");
        check (blocks == 1, "an empty-block lane holds one note per block");
        proc.setMapMidi ("Bass", true);
        auto mapF2 = proc.exportMap (err);
        { juce::MidiFile m2; juce::FileInputStream in (mapF2); m2.readFrom (in);
          for (int t = 1; t < m2.getNumTracks(); ++t)
          { bool isBass = false; int n = 0; for (auto* e : *m2.getTrack (t)) { if (e->message.isTrackNameEvent() && e->message.getTextFromTextMetaEvent() == "Bass") isBass = true; if (e->message.isNoteOn()) ++n; }
            if (isBass) bassNotes = n; } }
        check (bassNotes == (int) demoResult().parts[rw::kBass].size(), "REAL MIDI on a lane writes its transcribed notes instead of blocks");
        proc.setMapView (true);
    }

    juce::MemoryBlock st; proc.getStateInformation (st);
    RewiredProcessor p2; p2.setStateInformation (st.getData(), (int) st.getSize());
    check (p2.hasResult() && p2.result().parts[rw::kKick].size() == demoResult().parts[rw::kKick].size() && ! p2.partOn (rw::kHats) && p2.partOn (rw::kBass), "result + part switches survive save / load");
    check (p2.result().subs[rw::kChords].size() == 2 && p2.result().subs[rw::kChords][1].name == "Piano / Keys"
           && p2.result().subs[rw::kChords][0].notes.size() == demoResult().subs[rw::kChords][0].notes.size(), "instrument sub-parts survive save / load");
    check (p2.input (RewiredProcessor::kSong) == song && p2.songMode(), "inputs survive save / load");
    {
        const auto a = proc.result().map, b = p2.result().map;
        bool same = a.ok == b.ok && a.bars == b.bars && a.sections.size() == b.sections.size() && a.lanes.size() == b.lanes.size();
        for (size_t i = 0; same && i < a.lanes.size(); ++i) same = a.lanes[i].name == b.lanes[i].name && a.lanes[i].blocks.size() == b.lanes[i].blocks.size() && a.lanes[i].barOn == b.lanes[i].barOn;
        check (same && p2.mapView() && p2.mapMidi ("Bass") && ! p2.mapMidi ("Kick"), "song map, MAP view and REAL MIDI lanes survive save / load");
    }
    p2.clearAll(); check (! p2.hasResult() && ! p2.hasAnyInput() && p2.status() == RewiredProcessor::Status::idle, "clear");
    const char junk[] = "RWR1\xff\xff\xff\x7fgarbage"; p2.setStateInformation (junk, sizeof (junk)); check (true, "garbage state ignored");
    juce::String e2; check (! p2.exportMidi (false, e2).existsAsFile() && e2.isNotEmpty(), "export without a result explains why");
    proc.exportFolder().deleteRecursively();
    tmp.deleteRecursively();
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}

// ---- 1.1.0 controls audit: every button / toggle on the display, clicked where a user would, checked by what it changes
struct TestHead final : juce::AudioPlayHead
{
    bool on = false;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying (on); return p; }
};

std::shared_ptr<const rw::Preview> testPreview (double secs)
{
    auto p = std::make_shared<rw::Preview>();
    const size_t n = (size_t) (secs * rw::Preview::kMixRate), m = n / 2;
    p->mixL.resize (n); p->mixR.resize (n);
    for (size_t i = 0; i < n; ++i) { p->mixL[i] = rw::pcm16 (0.5f * (float) std::sin (6.2831853 * 330.0 * (double) i / rw::Preview::kMixRate)); p->mixR[i] = p->mixL[i]; }
    for (int s = 0; s < 4; ++s)   // stem s: a sine of amplitude 0.1 (s + 1), so a solo can be told apart by its level
    {
        auto& v = p->stems[(size_t) s]; v.resize (m);
        for (size_t i = 0; i < m; ++i) v[i] = rw::pcm16 (0.1f * (float) (s + 1) * (float) std::sin (6.2831853 * 200.0 * (double) i / rw::Preview::kStemRate));
    }
    return p;
}

double blockRms (RewiredProcessor& p, int blocks = 8)   // run the processor on silence, the RMS of what it outputs
{
    double sum = 0.0; int n = 0;
    for (int k = 0; k < blocks; ++k)
    {
        juce::AudioBuffer<float> b (2, 512); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
        for (int i = 0; i < 512; ++i) { sum += (double) b.getSample (0, i) * b.getSample (0, i); ++n; }
    }
    return std::sqrt (sum / std::max (1, n));
}

double midiTempo (const juce::File& f)
{
    juce::MidiFile mf; juce::FileInputStream in (f); if (! in.openedOk() || ! mf.readFrom (in)) return 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) if (e->message.isTempoMetaEvent()) return 60.0 / e->message.getTempoSecondsPerQuarterNote();
    return 0.0;
}

int controlTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rw_ctl_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    auto song = tmp.getChildFile ("Control Song.wav"); writeTone (song, 44100.0, 16.0);
    RewiredProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    proc.setInput (RewiredProcessor::kSong, song);
    {
        auto r = demoResult();   // + stem loudness for drums / bass / chords, so those lanes solo their stem audio
        const std::pair<int, int> gs[] = { { rw::kGrpDrums, rw::kStemDrums }, { rw::kGrpBass, rw::kStemBass }, { rw::kGrpChords, rw::kStemChords } };
        for (auto [g, st] : gs)
        {
            rw::MapEnvelope en; en.group = g; en.stem = st; en.pow.resize (1600);
            for (size_t i = 0; i < en.pow.size(); ++i) en.pow[i] = (i % 50) < 10 ? 0.1f : 0.02f;   // a hit on each beat
            r.mapFeat.env.push_back (en);
        }
        r.map = rw::buildSongMap (r, r.mapFeat);
        proc.testSetResult (r);
    }
    proc.testSetPreview (testPreview (16.0));
    proc.setMapView (true);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<RewiredEditor*> (ed.get());
    ed->setVisible (true);
    auto* d = e->getPanel().display();
    auto settle = [&] (int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); };
    auto click = [&] (const juce::String& w, int i = 0, double t = 0.0) { const bool ok = rwui::displayClick (d, w, i, t); settle(); return ok; };
    auto param = [&] (const char* id) { return proc.apvts.getParameter (id); };
    auto near = [] (double a, double b, double tol) { return std::abs (a - b) <= tol; };
    auto laneIndex = [&] (const juce::String& n) { const auto r = proc.result(); for (size_t i = 0; i < r.map.lanes.size(); ++i) if (r.map.lanes[i].name == n) return (int) i; return -1; };
    settle (120);
    juce::String err;

    // ---- BPM: whole numbers, - / + / half / double / AUTO / typing, all re-grid the map + NOTES MIDI
    check (proc.effectiveBpm() == 120.0 && proc.bpmIsAuto() && param ("bpm")->getCurrentValueAsText() == "AUTO", "tempo starts on AUTO at the detected 120 bpm");
    check (click ("double") && proc.effectiveBpm() == 240.0 && ! proc.bpmIsAuto() && proc.result().map.bars == 16 && param ("bpm")->getCurrentValueAsText() == "240",
           "x2 doubles the tempo: 240 bpm, the 8 bars become 16");
    check (near (midiTempo (proc.exportMidi (false, err)), 240.0, 0.01) && near (midiTempo (proc.exportMap (err)), 240.0, 0.01), "the NOTES and MAP files carry the new tempo");
    check (click ("half") && proc.effectiveBpm() == 120.0 && proc.result().map.bars == 8, "1/2 halves it back (8 bars)");
    check (click ("half") && proc.effectiveBpm() == 60.0 && proc.result().map.bars == 4 && near (proc.result().map.firstDownbeat, 0.0, 0.01), "1/2 again: 60 bpm, 4 bars, bar 1 stays at 0 s");
    check (click ("auto") && proc.bpmIsAuto() && proc.effectiveBpm() == 120.0 && proc.result().map.bars == 8, "AUTO goes back to the detected tempo");
    check (click ("auto") && ! proc.bpmIsAuto() && proc.effectiveBpm() == 120.0, "AUTO off keeps the shown tempo as your own");
    check (click ("bpm+") && proc.effectiveBpm() == 121.0 && click ("bpm-") && click ("bpm-") && proc.effectiveBpm() == 119.0, "+ / - step one whole bpm");
    rwui::displayTypeBpm (d, "174"); settle();
    check (proc.effectiveBpm() == 174.0 && near (midiTempo (proc.exportMidi (false, err)), 174.0, 0.01), "typing 174 sets 174 bpm (and the MIDI tempo)");
    rwui::displayTypeBpm (d, "86.6"); settle();
    check (proc.effectiveBpm() == 87.0, "a typed decimal rounds to a whole bpm");
    rwui::displayTypeBpm (d, "fast"); settle();
    check (proc.effectiveBpm() == 87.0, "typing junk changes nothing");
    rwui::displayTypeBpm (d, "auto"); settle();
    check (proc.bpmIsAuto() && proc.effectiveBpm() == 120.0, "typing auto goes back to AUTO");
    param ("bpm")->setValueNotifyingHost (param ("bpm")->convertTo0to1 (140.0f)); settle();
    check (proc.effectiveBpm() == 140.0 && proc.result().map.bars == 10, "the TEMPO knob / automation re-grids the same way (140 bpm: 16 s = 10 bars)");
    {
        auto* q = param ("quantize");
        q->setValueNotifyingHost (q->convertTo0to1 (1.0f)); settle();   // 1/16 at 140 bpm
        const double g16 = 60.0 / 140.0 / 4.0; bool onGrid = true;
        const auto rq = proc.result();   // (a copy: a range-for over a temporary's member would dangle)
        const double off = rq.map.firstDownbeat;   // the grid starts at bar 1
        for (auto& n : rq.parts[rw::kMelody]) { const double ph = std::fmod (n.start - off + 100.0 * g16 + 1e-9, g16); onGrid &= ph < 1e-4 || g16 - ph < 1e-4; }
        check (onGrid, "QUANTIZE 1/16 applies at once, on the new tempo's grid");
        q->setValueNotifyingHost (q->convertTo0to1 (0.0f)); settle();
        bool same = proc.result().parts[rw::kMelody].size() == demoResult().parts[rw::kMelody].size();
        for (size_t i = 0; same && i < proc.result().parts[rw::kMelody].size(); ++i) same = near (proc.result().parts[rw::kMelody][i].start, demoResult().parts[rw::kMelody][i].start, 1e-9);
        check (same, "QUANTIZE Off gives back the notes as played");
    }
    {
        juce::MemoryBlock st; proc.getStateInformation (st);
        RewiredProcessor p3; p3.setStateInformation (st.getData(), (int) st.getSize());
        check (p3.effectiveBpm() == 140.0 && ! p3.bpmIsAuto() && p3.result().map.bars == 10 && p3.result().detectedBpm == 120.0, "your tempo survives save / load (and the detected one is kept)");
        p3.setBpm (0.0); check (p3.effectiveBpm() == 120.0 && p3.result().map.bars == 8, "...and AUTO still works after reopening");
    }
    proc.setBpm (0.0); settle();

    // ---- transport: PLAY / STOP, section click, strip seek, the DAW starting, CLEAR
    check (blockRms (proc) < 1e-6, "silent before PLAY (the plugin passes the DAW's audio through)");
    check (click ("play") && proc.playing(), "PLAY starts the song");
    const double r1 = blockRms (proc, 16);
    check (near (r1, 0.9 * 0.5 / std::sqrt (2.0), 0.03), "PLAY: the song comes out (mix level, 0.9 gain)");
    check (proc.playPosition() > 0.1 && proc.playPosition() < 0.3, "the playhead moves with the audio");
    check (click ("play") && ! proc.playing(), "STOP stops it");
    blockRms (proc, 2); check (blockRms (proc) < 1e-6, "silent after STOP (after an 8 ms fade)");
    {
        const auto r = proc.result(); const int si = (int) r.map.sections.size() - 1;
        const double t = r.map.barTime (r.map.sections[(size_t) si].startBar);
        check (click ("section", si) && proc.playing() && near (proc.playPosition(), t, 0.05), "clicking a section plays from its start");
        check (click ("strip", 0, 6.0) && near (proc.playPosition(), 6.0, 0.15), "clicking a lane moves the playhead there");
    }
    {
        TestHead h; proc.setPlayHead (&h);
        blockRms (proc, 1); h.on = true; blockRms (proc, 4); settle();
        check (! proc.playing(), "the DAW starting playback stops the preview");
        h.on = false; blockRms (proc, 1); proc.setPlayHead (nullptr);
    }

    // ---- MAP lane toggles: M (real MIDI) and S (solo)
    const int kick = laneIndex ("Kick"), bass = laneIndex ("Bass"), drums = laneIndex ("Drums");
    {
        const auto r = proc.result();
        check (drums >= 0 && kick >= 0 && bass >= 0 && r.map.lanes[(size_t) drums].stem == rw::kStemDrums && r.map.lanes[(size_t) bass].stem == rw::kStemBass
               && r.map.lanes[(size_t) kick].stem < 0, "lanes know their stem (a drum piece has none of its own)");
    }
    check (click ("midi", bass) && proc.mapMidi ("Bass"), "M on the Bass lane turns REAL MIDI on");
    {
        juce::MidiFile mf; juce::FileInputStream in (proc.exportMap (err)); mf.readFrom (in); int n = 0;
        for (int t = 1; t < mf.getNumTracks(); ++t) { bool isB = false; int k = 0; for (auto* ev : *mf.getTrack (t)) { if (ev->message.isTrackNameEvent() && ev->message.getTextFromTextMetaEvent() == "Bass") isB = true; if (ev->message.isNoteOn()) ++k; } if (isB) n = k; }
        check (n == (int) demoResult().parts[rw::kBass].size(), "...and EXPORT MAP writes its notes");
    }
    check (click ("midi", bass) && ! proc.mapMidi ("Bass"), "M again turns it off");
    proc.stop(); blockRms (proc, 2);
    check (click ("solo", drums) && proc.solo() == "Drums" && proc.playing(), "S on Drums solos it (and plays)");
    {
        const auto r = proc.result();
        check (proc.laneSoloSource (r.map.lanes[(size_t) drums], r) == 1, "the Drums solo plays the drum stem");
        proc.seek (1.0); blockRms (proc, 2);
        check (near (blockRms (proc, 16), 0.9 * 0.1 * (rw::kStemDrums + 1) / std::sqrt (2.0), 0.02), "the solo is audible at the drum stem's level (not the mix)");
        click ("midi", drums);
        check (proc.mapMidi ("Drums") && proc.laneSoloSource (proc.result().map.lanes[(size_t) drums], proc.result()) == 2, "M on the soloed lane switches the solo to its notes");
        click ("midi", drums);
        check (click ("solo", kick) && proc.solo() == "Kick" && proc.laneSoloSource (r.map.lanes[(size_t) kick], r) == 2, "S on Kick plays the kick notes (a synth kick)");
        proc.seek (0.0); blockRms (proc, 1);
        double peak = 0.0; for (int k = 0; k < 20; ++k) peak = std::max (peak, blockRms (proc, 1));
        check (peak > 0.02, "...which are audible");
        const int voc = laneIndex ("Vocals");
        check (voc >= 0 && proc.laneSoloSource (r.map.lanes[(size_t) voc], r) == 2, "a lane without its own stem (the sung melody here) solos its notes");
    }
    {
        juce::MemoryBlock st; proc.getStateInformation (st);
        RewiredProcessor p3; p3.setStateInformation (st.getData(), (int) st.getSize());
        check (p3.solo() == "Kick" && ! p3.playing() && p3.result().map.lanes[(size_t) drums].stem == rw::kStemDrums, "the solo lane and lane stems survive save / load (reopening never starts playback)");
    }
    check (click ("solo", kick) && proc.solo().isEmpty(), "S again: back to the whole song");
    proc.seek (2.0); blockRms (proc, 2);
    check (near (blockRms (proc, 16), 0.9 * 0.5 / std::sqrt (2.0), 0.03), "...and the mix plays again");

    // ---- tabs, part toggles, RE-PROCESS, CLEAR
    check (click ("map") && proc.mapView() && click ("notes") && ! proc.mapView(), "NOTES / MAP tabs switch the view");
    check (! rwui::displayControl (d, "part2")->isShowing() || ! proc.mapView(), "part switches only show in NOTES");
    check (click ("part2") && ! proc.partOn (2) && click ("part2") && proc.partOn (2), "a part's switch turns it off and on");
    proc.setPartOn (rw::kSnare, false);
    {
        juce::MidiFile mf; juce::FileInputStream in (proc.exportMidi (true, err)); mf.readFrom (in); bool snare = false;
        for (int t = 1; t < mf.getNumTracks(); ++t) for (auto* ev : *mf.getTrack (t)) if (ev->message.isTrackNameEvent() && ev->message.getTextFromTextMetaEvent() == "Snare") snare = true;
        check (! snare, "EXPORT SELECTED leaves a switched-off part out");
        proc.setPartOn (rw::kSnare, true);
    }
    {
        auto* pb = dynamic_cast<juce::Button*> (rwui::displayControl (d, "process"));
        check (pb != nullptr && pb->getButtonText() == "PROCESS" && ! proc.detectionChanged(), "PROCESS when the result matches the settings");
        auto* on = param ("onset"); on->setValueNotifyingHost (on->convertTo0to1 (0.7f)); settle (400);
        check (proc.detectionChanged() && pb->getButtonText() == "RE-PROCESS" && rwui::displayStatus (d).contains ("RE-PROCESS"), "changing a detection knob asks to RE-PROCESS");
        on->setValueNotifyingHost (on->convertTo0to1 (0.5f)); settle (400);
        check (pb->getButtonText() == "PROCESS", "...and stops asking when it is set back");
    }
    {
        bool all = true;   // every detection setting (knobs + Melody From) asks for RE-PROCESS, and stops asking when set back
        for (const char* id : { "onset", "frame", "minNote", "melSrc", "melGap", "chordMin", "drumSens" })
        {
            auto* pp = param (id); const float v0 = pp->getValue();
            pp->setValueNotifyingHost (v0 < 0.5f ? 1.0f : 0.0f); const bool on = proc.detectionChanged();
            pp->setValueNotifyingHost (v0); const bool off = ! proc.detectionChanged();
            if (! (on && off)) { std::printf ("  %s: changed=%d back=%d\n", id, (int) on, (int) off); all = false; }
        }
        check (all, "all 7 detection settings mark the result stale (RE-PROCESS); TEMPO / QUANTIZE do not");
        auto* t = param ("bpm"); t->setValueNotifyingHost (t->convertTo0to1 (150.0f)); settle();
        check (! proc.detectionChanged(), "...a tempo change never asks for RE-PROCESS");
        settle (700);   // (APVTS copies parameter values into its undoable tree on a timer)
        proc.undoManager.beginNewTransaction();
        proc.setBpm (100.0); settle (700);
        proc.undoManager.beginNewTransaction();
        check (proc.effectiveBpm() == 100.0, "tempo 100");
        proc.undoManager.undo(); settle();
        check (proc.effectiveBpm() == 150.0 && proc.result().map.bars == 10, "UNDO takes a tempo edit back (and re-grids)");
        proc.undoManager.redo(); settle();
        check (proc.effectiveBpm() == 100.0, "REDO puts it back");
        proc.setBpm (0.0); settle();
    }
    click ("map");
    proc.play (0.0); settle();
    check (click ("clear") && ! proc.playing() && ! proc.hasResult() && ! proc.canPlay(), "CLEAR stops playback and empties the map");
    check (! click ("play") && ! proc.playing(), "PLAY is off with nothing loaded");

    // ---- reopening a project: PLAY loads the audio from the input files first
    {
        RewiredProcessor p4; p4.setPlayConfigDetails (2, 2, 48000.0, 512); p4.prepareToPlay (48000.0, 512);
        p4.setInput (RewiredProcessor::kSong, song); p4.testSetResult (demoResult());
        p4.play (0.0);
        check (p4.testWaitPreview (20000) && p4.playing() && near (p4.playLength(), 16.0, 0.05), "PLAY on a reopened project loads the song, then plays");
        check (near (blockRms (p4, 16), 0.9 * 0.3 / std::sqrt (2.0), 0.02), "...the loaded song is what comes out");
        p4.stop();
        // a load still running when the song changes is thrown away (it was the old file)
        auto song2 = tmp.getChildFile ("Other Song.wav"); writeTone (song2, 44100.0, 4.0);
        p4.testSetPreview (nullptr); p4.play (0.0); p4.setInput (RewiredProcessor::kSong, song2);
        p4.testWaitPreview (20000); juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const bool idle = ! p4.playing();
        p4.play (0.0); p4.testWaitPreview (20000);
        check (idle && near (p4.playLength(), 4.0, 0.05), "changing the song while its audio loads never plays the old song");
    }
    ed.reset();
    tmp.deleteRecursively();
    std::printf (fails == 0 ? "CONTROL TESTS PASSED\n" : "%d CONTROL TEST(S) FAILED\n", fails);
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
    if (mode == "controls") return controlTests();
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
    const bool mapMode = mode == "map" || mode == "mapmidi" || mode == "mapplay" || mode == "bpmedit" || mode == "notesplay";
    const auto mapSong = juce::SystemStats::getEnvironmentVariable ("RW_MAP_SONG", {});
    if (mapMode && mapSong.isNotEmpty() && proc.modelDir() != juce::File())
    {
        proc.setInput (RewiredProcessor::kSong, juce::File (mapSong));
        proc.startProcess();
        if (! proc.testWaitProcess (600000)) { std::printf ("map run failed: %s\n", proc.lastMessage().toRawUTF8()); return 1; }
        std::printf ("%s\n", proc.lastMessage().toRawUTF8());
    }
    else if (mode != "empty" && mode != "mapempty")
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
    if (mode == "drop")   // drag and drop onto the boxes / buttons, through the display's own drop handler
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rw_drop_test"); tmp.deleteRecursively(); tmp.createDirectory();
        auto mk = [&] (const char* n) { auto f = tmp.getChildFile (n); writeTone (f, 44100.0, 0.5); return f.getFullPathName(); };
        const auto song = mk ("My Song.wav"), b = mk ("b1.wav"), hornsF = mk ("Horns.wav"), vox = mk ("Lead Vox.wav"), dr = mk ("drums.wav");
        tmp.getChildFile ("notes.txt").replaceWithText ("x");
        auto* d = e->getPanel().display();
        rwui::displayDrop (d, 0, { song });                      check (proc.songMode() && proc.input (0).getFullPathName() == song, "drop on SONG loads the song");
        rwui::displayDrop (d, RewiredProcessor::kBassStem, { b }); check (! proc.songMode() && proc.input (RewiredProcessor::kBassStem).getFullPathName() == b, "drop on BASS loads that stem (any name) and leaves song mode");
        rwui::displayDrop (d, 11, { vox, hornsF, dr });          check (proc.input (RewiredProcessor::kMelodyStem).getFullPathName() == vox && proc.input (RewiredProcessor::kChordsStem).getFullPathName() == hornsF
                                                                       && proc.input (RewiredProcessor::kDrumsStem).getFullPathName() == dr && proc.input (RewiredProcessor::kBassStem).getFullPathName() == b, "several files on LOAD STEMS go by name");
        rwui::displayDrop (d, RewiredProcessor::kDrumsStem, { song, b }); check (proc.input (RewiredProcessor::kDrumsStem).getFullPathName() == song && proc.input (RewiredProcessor::kChordsStem).getFullPathName() == b,
                                                                       "several unnamed files on one stem box: that box first, then the others");
        rwui::displayDrop (d, 10, { song });                     check (proc.songMode(), "drop on LOAD SONG loads the song");
        rwui::displayDrop (d, 20, { tmp.getChildFile ("notes.txt").getFullPathName() }); check (proc.songMode() && proc.input (0).getFullPathName() == song, "a non-audio drop changes nothing");
        auto* tgt = dynamic_cast<juce::FileDragAndDropTarget*> (d);
        check (tgt != nullptr && tgt->isInterestedInFileDrag ({ "/a/x.mp3" }) && tgt->isInterestedInFileDrag ({ "/a/x.AIFF" }) && tgt->isInterestedInFileDrag ({ "/a/x.ogg" })
               && ! tgt->isInterestedInFileDrag ({ "/a/x.txt" }), "wav / mp3 / flac / aiff / ogg are accepted");
        tmp.deleteRecursively();
        std::printf (fails == 0 ? "DROP TESTS PASSED\n" : "%d DROP TEST(S) FAILED\n", fails);
        ed.reset(); return fails;
    }
    if ((mapMode && mode != "notesplay") || mode == "mapempty") proc.setMapView (true);
    if (mode == "mapplay" || mode == "notesplay")   // playing, the playhead ~40 % in (no audio device here, so it stays put)
    {
        if (mode == "mapplay") proc.setMapMidi ("Bass", true);
        proc.play (0.0); proc.testWaitPreview (60000); proc.seek (proc.playLength() * 0.41);
    }
    if (mode == "bpmedit") proc.setBpm (std::round (proc.effectiveBpm() / 2.0));   // DnB at half time: 87 bpm, half the bars
    if (mode == "bpmtype") { proc.setMapView (true); rwui::displayTypeBpm (e->getPanel().display(), "87", false); }
    if (mode == "mapmidi") { proc.setMapMidi ("Bass", true); proc.setMapMidi ("Kick", true); proc.setMapMidi ("Snare", true); }
    if (mode == "subs") { rwui::displayExpand (e->getPanel().display(), rw::kChords); rwui::displayHover (e->getPanel().display(), RewiredProcessor::kChordsStem); }
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
