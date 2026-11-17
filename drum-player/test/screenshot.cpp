// Renders the real DrumPlayerEditor to a PNG, and runs the processor-level tests.
// usage: DrumPlayerScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc|preview]
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

// host transport for the tests: tempo, playing / stopped, song position advanced by the test
struct FakeHead final : juce::AudioPlayHead
{
    double bpm = 0, ppq = 0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        if (bpm > 0) p.setBpm (bpm);
        p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

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
        check (proc.getParameters().size() == 62 && std::fabs (v (proc, "swing") - 0.5f) < 1e-6 && v (proc, "noteMap") == 0 && v (proc, "patPlay") == 0 && v (proc, "p1Sound") == 1 && v (proc, "p8Sound") == 10 && v (proc, "p5Choke") == 1 && v (proc, "baseNote") == 1
               && v (proc, "bpm") == 0 && v (proc, "softClip") == 1, "62 parameters with defaults (kit, hats choke, GM map, pattern stopped, tempo Host, soft clip on, swing straight)");
        set (proc, "noteMap", 1);   // the 1.0 tests below use the chromatic map
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
        set (proc, "noteMap", 1); set (proc, "p5Sound", 5); set (proc, "p4Sound", 4); set (proc, "p4Level", -60.0f);
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
        DrumPlayerProcessor a; set (a, "noteMap", 1); a.setRateAndBufferSizeDetails (sr, bs); a.prepareToPlay (sr, bs);
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
        DrumPlayerProcessor b; set (b, "noteMap", 1); b.setRateAndBufferSizeDetails (sr, bs); b.prepareToPlay (sr, bs);
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
                set (proc, "noteMap", 1); proc.presets.loadFactory (i); proc.reset();
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
    {   // 1.1: GM / FPC note map
        DrumPlayerProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        struct { int note, pad; } gm[] = { { 36, 0 }, { 38, 1 }, { 39, 2 }, { 42, 3 }, { 46, 4 }, { 41, 5 }, { 45, 5 }, { 48, 5 }, { 37, 6 }, { 49, 7 }, { 51, 7 } };
        int bad = 0;
        for (auto g : gm)
        {
            render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
            const int before = proc.dspEngine().hits[g.pad].load();
            render (proc, 4, { { 0, on (g.note) } });
            if (proc.dspEngine().hits[g.pad].load() <= before) { ++bad; std::printf ("  note %d did not hit pad %d\n", g.note, g.pad + 1); }
        }
        check (bad == 0, "GM map: 36 kick, 38 snare, 39 clap, 42 CH, 46 OH, 41/45/48 toms, 37 rim, 49 crash, 51 ride");
        render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
        auto lo = render (proc, 20, { { 0, on (41, 1) } }); render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
        auto hi = render (proc, 20, { { 0, on (48, 1) } });
        check (freq (hi, 200, 9000) > freq (lo, 200, 9000) * 1.2, "tom 48 is pitched above tom 41");
        set (proc, "noteMap", 1); render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
        const int h1 = proc.dspEngine().hits[1].load(); render (proc, 4, { { 0, on (37) } });
        check (proc.dspEngine().hits[1].load() > h1, "chromatic map still available (37 = pad 2)");
    }
    {   // 1.1: old (1.0) sessions keep the chromatic map; kit presets don't touch pattern settings
        DrumPlayerProcessor a; set (a, "noteMap", 1);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (xml) for (auto* e = xml->getFirstChildElement(); e != nullptr; ) { auto* n = e->getNextElement(); if (e->getStringAttribute ("id") == "noteMap") xml->removeChildElement (e, true); e = n; }
        juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2);
        DrumPlayerProcessor b; b.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check (xml != nullptr && v (b, "noteMap") == 1, "a 1.0 session (no noteMap) loads with the chromatic map");
        DrumPlayerProcessor c; set (c, "pattern", 5); set (c, "fillStyle", 3); set (c, "dragLen", 3); c.presets.loadFactory (4);
        check (v (c, "pattern") == 5 && v (c, "fillStyle") == 3 && v (c, "dragLen") == 3, "loading a kit keeps the pattern / fill / drag settings");
    }
    {   // 1.1: patterns and fills
        DrumPlayerProcessor p; int bad = 0, nofill = 0;
        const int np = p.apvts.getParameter ("pattern")->getNumSteps(), nf = p.apvts.getParameter ("fillStyle")->getNumSteps();
        for (int pi = 0; pi < np; ++pi) for (int fi = 0; fi < nf; ++fi) for (int fl = 0; fl < 4; ++fl) for (int bl : { 0, 3 })
        {
            set (p, "pattern", (float) pi); set (p, "fillStyle", (float) fi); set (p, "fillLen", (float) fl); set (p, "dragLen", (float) bl);
            const auto ev = p.clipEvents(); const double len = 4.0 * (1 << bl);
            bool ok = ! ev.empty();
            for (size_t k = 0; k < ev.size(); ++k) ok = ok && ev[k].beat >= 0 && ev[k].beat < len && ev[k].vel > 0 && ev[k].vel <= 1 && (k == 0 || ev[k - 1].beat <= ev[k].beat);
            bad += ok ? 0 : 1;
            if (fi != 0) { bool crash = false; for (auto& e : ev) crash = crash || (e.beat == 0 && e.note == 49); nofill += crash ? 0 : 1; }
        }
        check (bad == 0, "every pattern x fill x length: hits sorted, inside the clip, velocities 0..1");
        check (nofill == 0, "with a fill on, the clip comes back round with a crash on beat 1");
        set (p, "pattern", 0); set (p, "fillStyle", 2); set (p, "dragLen", 2); set (p, "fillLen", 2);
        auto a = p.clipEvents(); p.newFill(); auto b = p.clipEvents();
        bool sameGroove = true, diffFill = a.size() != b.size();
        for (size_t k = 0; k < std::min (a.size(), b.size()); ++k)
        { if (a[k].beat < 12) sameGroove = sameGroove && a[k].beat == b[k].beat && a[k].note == b[k].note; else diffFill = diffFill || a[k].beat != b[k].beat || a[k].note != b[k].note || a[k].vel != b[k].vel; }
        check (sameGroove && diffFill, "NEW FILL changes the fill bar and leaves the groove alone");
        // 1.2 swing: 50 % = the 1.1 clip exactly; 66 % moves every second 16th of the groove by 1/12 beat, fills stay straight
        set (p, "pattern", 1); set (p, "fillStyle", 2); set (p, "dragLen", 2); set (p, "fillLen", 2);
        cw::pat::Clip c0 = p.clipSettings(); c0.swing = 0.5f; std::vector<cw::pat::Ev> straight; cw::pat::build (c0, straight);
        const auto now = p.clipEvents();
        bool same = now.size() == straight.size(); for (size_t k = 0; same && k < now.size(); ++k) same = now[k].beat == straight[k].beat && now[k].note == straight[k].note;
        set (p, "swing", 0.66f); const auto sw = p.clipEvents();
        int moved = 0, wrong = 0;
        for (auto& e : sw)
        {
            const double st = e.beat * 4.0, fr = st - std::floor (st + 1e-9);
            if (e.beat < 12.0 && std::fabs (fr - 0.32) < 1e-6) ++moved;            // 0.08 beat = 0.32 of a 16th
            else if (e.beat < 12.0 && fr > 1e-6 && std::fabs (fr - 0.5) > 1e-6) ++wrong;
            if (e.beat >= 12.0 && std::fabs (fr) > 1e-6 && std::fabs (fr - 0.5) > 1e-6) ++wrong;   // Roll fill: straight 16ths / 32nds
        }
        check (same && moved > 20 && wrong == 0, "SWING: 50 % = 1.1 timing; 66 % delays the off 16ths of the groove only");
        check (p.clipTitle().contains ("swing 66"), "the drag-out file name says the swing");
        set (p, "swing", 0.5f);
    }
    {   // 1.1: pattern playback follows tempo; MIDI and WAV drag-out files
        DrumPlayerProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        auto y = render (proc, 100, {});
        check (rmsOf (y, 0) == 0.0 && proc.hostTempo() == 0 && proc.tempo() == 120.0 && proc.tempoSource() == "default", "pattern off = silent; no host tempo -> 120 default");
        set (proc, "patPlay", 1); y = render (proc, 100, {});
        check (rmsOf (y, 0) == 0.0 && proc.playPosition() < 0, "PLAY WITH HOST is silent while the host is not playing (1.1 free-ran)");
        set (proc, "patPlay", 0);
        proc.startPreview (0); y = render (proc, 100, {});
        check (rmsOf (y, 0) > 0.01 && proc.playPosition() >= 0 && proc.previewPart() == 0, "PREVIEW plays with no host at all and moves the playhead");
        proc.stopPreview(); render (proc, 20, {});
        set (proc, "dragLen", 1); set (proc, "fillStyle", 2);
        juce::String err; auto mf = proc.exportMidi (err);
        juce::MidiFile mid; bool parsed = false; int notes = 0, nonGm = 0; double bpm = 0, endBeat = 0;
        if (juce::FileInputStream in (mf); mf.existsAsFile() && in.openedOk()) parsed = mid.readFrom (in);
        if (parsed && mid.getNumTracks() > 0)
        {
            const double tpq = mid.getTimeFormat();
            for (int t = 0; t < mid.getNumTracks(); ++t) for (auto* me : *mid.getTrack (t))
            {
                auto& m = me->message;
                if (m.isTempoMetaEvent()) bpm = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (m.isNoteOn()) { ++notes; const int n = m.getNoteNumber(); nonGm += (n == 36 || n == 38 || n == 39 || n == 37 || n == 42 || n == 46 || n == 49 || n == 51 || (n >= 41 && n <= 50)) ? 0 : 1; nonGm += m.getChannel() == 10 ? 0 : 1; }
                if (m.isEndOfTrackMetaEvent()) endBeat = std::max (endBeat, m.getTimeStamp() / tpq);
            }
        }
        std::printf ("  midi: %s  notes=%d bpm=%.2f end=%.2f err=%s\n", mf.getFileName().toRawUTF8(), notes, bpm, endBeat, err.toRawUTF8());
        check (parsed && notes > 16 && nonGm == 0 && std::fabs (bpm - 120.0) < 0.01 && std::fabs (endBeat - 8.0) < 1e-6, ".mid drag-out: GM notes on channel 10, tempo, exactly 2 bars");
        { juce::String e0; check (! proc.exportPart (DrumPlayerProcessor::kPartToms, e0).exists() && e0.contains ("no"), "a part with no hits (toms, Roll fill) is refused with a message, not a silent file"); }
        set (proc, "fillStyle", 4);   // Toms fill so every part has hits
        int okParts = 0;
        for (int part = 0; part < DrumPlayerProcessor::kNumParts; ++part)
        {
            juce::String e2; auto wf = proc.exportPart (part, e2);
            juce::WavAudioFormat wav;
            if (auto* in = wf.createInputStream().release())
                if (std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (in, true)); r != nullptr)
                {
                    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                    const juce::int64 want = (juce::int64) std::llround (8 * 60.0 / 120.0 * r->sampleRate);
                    const bool ok = std::abs (r->lengthInSamples - want) <= 1 && r->bitsPerSample == 24 && b.getRMSLevel (0, 0, b.getNumSamples()) > 1e-3;
                    std::printf ("  wav %-28s %lld samples (want %lld) %d-bit rms=%.4f\n", wf.getFileName().toRawUTF8(), (long long) r->lengthInSamples, (long long) want, (int) r->bitsPerSample, b.getRMSLevel (0, 0, b.getNumSamples()));
                    okParts += ok ? 1 : 0;
                }
            wf.deleteFile();
        }
        check (okParts == DrumPlayerProcessor::kNumParts, "audio drag-outs (full, kick, snare, top, toms): 24-bit, exactly 2 bars, not silent");
        mf.deleteFile();
    }
    {   // 1.2: preview = drag-out, sample for sample, once the longest sound has had time to wrap (the 4.8 s crash of Cinematic Impacts needs 2 passes) (host stopped at 140 BPM, several kits / patterns / parts)
        FakeHead head; head.bpm = 140.0;
        int bad = 0, n = 0; float worst = 0;
        for (int kit : { 0, 2, 4, 15 }) for (int pat : { 0, 4, 7, 13 }) for (int part : { 0, 1, 3 })
        {
            DrumPlayerProcessor proc; proc.setPlayHead (&head); proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
            proc.presets.loadFactory (kit); set (proc, "pattern", (float) pat); set (proc, "fillStyle", (float) (1 + pat % 8)); set (proc, "dragLen", 1);
            set (proc, "swing", pat == 13 ? 0.62f : 0.5f);
            render (proc, 1, {});   // the processor learns the host tempo
            juce::AudioBuffer<float> dl; proc.renderClip (part, dl, sr);
            const int N = dl.getNumSamples();
            proc.startPreview (part);
            std::vector<float> R; auto y = render (proc, (4 * N) / bs + 2, {}, 2, &R);
            float d = 0;
            for (int pass = 2; pass < 4; ++pass) for (int i = 0; i < N; ++i)
                d = std::max ({ d, std::fabs (y[(size_t) (pass * N + i)] - dl.getSample (0, i)), std::fabs (R[(size_t) (pass * N + i)] - dl.getSample (1, i)) });
            if (d >= 1e-5f)
            {
                int at = 0; float dm = 0; for (int pass = 2; pass < 4; ++pass) for (int i = 0; i < N; ++i) { const float q = std::fabs (y[(size_t) (pass * N + i)] - dl.getSample (0, i)); if (q > dm) { dm = q; at = pass * N + i; } }
                std::printf ("  mismatch kit %d pattern %d part %d: %.3g at pass %d sample %d (N %d)\n", kit, pat, part, d, at / N, at % N, N);
            }
            worst = std::max (worst, d); ++n; bad += d < 1e-5f ? 0 : 1;
            const bool lenOk = std::abs (N - (int) std::llround (8 * 60.0 / 140.0 * sr)) <= 1;
            bad += lenOk ? 0 : 1;
        }
        std::printf ("  preview vs drag-out: %d cases, worst max diff %.3g (%.1f dBFS)\n", n, worst, 20 * std::log10 (worst + 1e-30));
        check (bad == 0, "preview at the host's last tempo (host stopped) = the drag-out WAV once the longest tail has come round (passes 3 and 4), every part");
    }
    {   // 1.2: preview start / stop / host transport
        FakeHead head; head.bpm = 128.0;
        DrumPlayerProcessor proc; proc.setPlayHead (&head); proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "fillStyle", 0);
        proc.startPreview (0);
        auto y = render (proc, 1, {});
        check (y[0] != 0.0f && proc.dspEngine().hits[0].load() == 1, "preview starts on the first sample of the next block with the beat-1 kick");
        y = render (proc, 200, {});   // ~2 s
        const int hitsBefore = proc.dspEngine().hits[0].load();
        proc.stopPreview();
        auto z = render (proc, 10, {});
        double after = rmsOf (z, (size_t) (0.035 * sr));
        float step = 0; for (size_t i = 1; i < z.size(); ++i) step = std::max (step, std::fabs (z[i] - z[i - 1]));
        float stepPlay = 0; for (size_t i = 1; i < y.size(); ++i) stepPlay = std::max (stepPlay, std::fabs (y[i] - y[i - 1]));
        std::printf ("  stop: rms after 35 ms %.2g, largest sample step during the fade %.4f (while playing %.4f)\n", after, step, stepPlay);
        check (after == 0.0 && proc.previewPart() < 0 && proc.dspEngine().hits[0].load() == hitsBefore, "STOP: the preview's sounds fade out in 30 ms, no new hits");
        check (step <= stepPlay, "STOP: the fade adds no step bigger than the pattern itself has (no click)");
        // the host starting playback ends the preview; PLAY WITH HOST then follows the song position
        proc.startPreview (0); render (proc, 50, {});
        head.playing = true; head.ppq = 0; render (proc, 1, {});
        check (proc.previewPart() < 0, "host transport start stops the preview (no doubling with the song)");
        set (proc, "patPlay", 1); head.ppq = 3.0;
        proc.startPreview (0); render (proc, 1, {});
        check (proc.previewPart() < 0 && proc.syncedToHost(), "while PLAY WITH HOST is sounding, PREVIEW does not start a second copy");
        set (proc, "patPlay", 0); head.playing = false; render (proc, 1, {});
        proc.startPreview (1); render (proc, 2, {}); proc.startPreview (3); render (proc, 2, {});
        check (proc.previewPart() == 3, "clicking another loop chip switches the preview to that part");
        head.playing = true; render (proc, 1, {}); head.playing = false;
        // last host tempo is kept while stopped; TEMPO knob overrides it
        DrumPlayerProcessor q; FakeHead h2; h2.bpm = 97.0; q.setPlayHead (&h2); q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs);
        render (q, 1, {}); h2.bpm = 0; render (q, 1, {});
        check (std::fabs (q.tempo() - 97.0) < 1e-9 && q.tempoSource() == "host", "the host's last tempo is kept when it stops sending one");
        set (q, "bpm", 174.0f);
        juce::AudioBuffer<float> dl; q.renderClip (0, dl, sr);
        check (std::fabs (q.tempo() - 174.0) < 1e-3 && q.tempoSource() == "manual" && std::abs (dl.getNumSamples() - (int) std::llround (16 * 60.0 / 174.0 * sr)) <= 1, "TEMPO knob overrides the host for the preview and the drag-outs");
        check (q.apvts.getParameter ("bpm")->getText (0.0f, 20) == "Host" && std::fabs (q.apvts.getParameter ("bpm")->getValueForText ("host")) < 1e-6, "TEMPO shows / takes Host at its minimum");
    }
    {   // 1.2: soft clip + seamless loops + voices
        DrumPlayerProcessor a; a.setRateAndBufferSizeDetails (sr, bs); a.prepareToPlay (sr, bs);
        a.presets.loadFactory (15);   // Cinematic Impacts: the hottest kit (+7 dBFS peaks in 1.1)
        set (a, "pattern", 16); set (a, "dragLen", 2);
        juce::AudioBuffer<float> bOn; a.renderClip (0, bOn, sr);
        set (a, "softClip", 0); juce::AudioBuffer<float> bOff; a.renderClip (0, bOff, sr);
        const float pkOn = bOn.getMagnitude (0, bOn.getNumSamples()), pkOff = bOff.getMagnitude (0, bOff.getNumSamples());
        std::printf ("  soft clip: peak %.2f dBFS on, %.2f dBFS off\n", 20 * std::log10 (pkOn), 20 * std::log10 (pkOff));
        check (pkOn <= cw::dsp::kClipCeil && pkOff > 1.0f, "SOFT CLIP keeps the drag-out under -0.3 dBFS; off it goes over 0 dBFS like 1.1");
        juce::String err; const auto f = a.exportPart (0, err);
        juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
        check (r != nullptr && r->bitsPerSample == 32 && r->usesFloatingPointData, "a drag-out over 0 dBFS is written as 32-bit float (nothing cut off)");
        r.reset(); f.deleteFile();
        float lin = 0; for (float x : { 0.1f, 0.5f, 0.7f }) lin = std::max (lin, std::fabs (cw::dsp::softClip (x) - x));
        check (lin == 0.0f && cw::dsp::softClip (10.0f) <= cw::dsp::kClipCeil && cw::dsp::softClip (-10.0f) >= -cw::dsp::kClipCeil, "soft clip curve: untouched below -3 dBFS, never past -0.3 dBFS");
        // old sessions (no softClip) keep their sound
        juce::MemoryBlock mb; a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (xml) for (auto* e = xml->getFirstChildElement(); e != nullptr; ) { auto* nx = e->getNextElement(); if (e->getStringAttribute ("id") == "softClip") xml->removeChildElement (e, true); e = nx; }
        juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2);
        DrumPlayerProcessor b; set (b, "softClip", 1); b.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check (v (b, "softClip") == 0, "a 1.1 session loads with Soft Clip off (sounds exactly as before)");
        DrumPlayerProcessor c; set (c, "softClip", 1); c.presets.loadFactory (3);
        check (v (c, "softClip") == 1, "loading a kit leaves Soft Clip alone");
        // seamless loop: an open hat ringing over the loop point is choked by the closed hat on beat 1, as when the loop repeats
        DrumPlayerProcessor d; d.setRateAndBufferSizeDetails (sr, bs); d.prepareToPlay (sr, bs);
        set (d, "pattern", 7); set (d, "fillStyle", 0); set (d, "dragLen", 0); set (d, "p5Decay", 4000.0f);
        juce::AudioBuffer<float> one; d.renderClip (0, one, sr);
        d.startPreview (0); const int N = one.getNumSamples(); auto y = render (d, (4 * N) / bs + 2, {});
        float dd = 0; for (int i = 0; i < N; ++i) dd = std::max (dd, std::fabs (y[(size_t) (3 * N + i)] - one.getSample (0, i)));
        check (dd < 1e-5f, "1-bar drag-out = the 4th pass of the looping preview (tails + chokes across the loop point)");
        // voices: a 64-hit burst never hard-cuts a voice
        DrumPlayerProcessor e; e.setRateAndBufferSizeDetails (sr, bs); e.prepareToPlay (sr, bs); set (e, "noteMap", 1);
        for (int p = 1; p <= 8; ++p) set (e, (juce::String ("p") + juce::String (p) + "Choke").toRawUTF8(), 0);
        std::vector<std::pair<int, juce::MidiMessage>> ev; for (int k = 0; k < 120; ++k) ev.push_back ({ k * 240, on (36 + k % 8, 1.0f) });
        y = render (e, 70, ev);
        check (e.dspEngine().hardSteals == 0, "120 overlapping hits (60 in 5 ms windows max): soft steal only, no hard-cut voices");
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
    if (mode.isEmpty() || mode == "big" || mode == "preview") proc.presets.loadFactory (2);   // Techno Warehouse for the product shot
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
    if (mode == "preview")   // the preview running (TOP part soloed via its chip), playhead mid-loop
    {
        cwui::clickPreview (e->getPanel().display(), 4);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
        for (int k = 0; k < 300; ++k) { buf.clear(); proc.processBlock (buf, m); }
    }
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
