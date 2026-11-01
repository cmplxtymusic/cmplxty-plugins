// Renders the real DrFillEditor to a PNG, and runs the processor-level tests.
// usage: DrFillScreenshot out.png [banner|menu|big|empty|keys|params|proc|presets]
#include "PluginEditor.h"
#include "PresetKitTest.h"
#include <random>
#include "TestLoops.h"

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }

void writeWav (const juce::File& f, const drf::Audio& a, int bits = 24)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    const int ch = a.stereo() ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), a.rate, (unsigned) ch, bits, {}, 0));
    juce::AudioBuffer<float> b (ch, a.len());
    b.copyFrom (0, 0, a.L.data(), a.len()); if (ch > 1) b.copyFrom (1, 0, a.R.data(), a.len());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// demo kit: swung 16ths + room at 92 BPM (the screenshots), or straight at any tempo
void loadDemoKit (DrFillProcessor& p, double bpm = 92.0, int variant = 1)
{
    const char* nm[] = { "Kick Loop", "Snare Loop", "Hat Loop" };
    for (int l = 0; l < 3; ++l) p.loadLoopAudio (l, drftest::makeLoop (l, bpm, variant, 44100.0).a, nm[l]);
}

struct FakeHost final : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0, sr = 48000; bool playing = true; int num = 4, den = 4;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setPpqPositionOfLastBarStart (std::floor (ppq / (num * 4.0 / den)) * (num * 4.0 / den));
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { num, den });
        return p;
    }
    void advance (int n) { if (playing) ppq += n / sr * bpm / 60.0; }
};

// runs `blocks` blocks of silence, returns the output (stereo); optional MIDI note at sample `noteAt` of the first block
juce::AudioBuffer<float> runSilence (DrFillProcessor& p, int samples, int note = -1, int noteAt = 0, FakeHost* host = nullptr, float vel = 1.0f)
{
    juce::AudioBuffer<float> out (2, samples); out.clear();
    for (int pos = 0; pos < samples; pos += 512)
    {
        const int n = juce::jmin (512, samples - pos);
        juce::AudioBuffer<float> b (2, n); b.clear(); juce::MidiBuffer m;
        if (pos == 0 && note >= 0) m.addEvent (juce::MidiMessage::noteOn (1, note, vel), noteAt);
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n);
        if (host) host->advance (n);
    }
    return out;
}
// output vs the rendered part, starting at `at`: largest difference
float diffToRender (const juce::AudioBuffer<float>& out, const drf::Render& r, int part, int at)
{
    float d = 0;
    for (int k = 0; k < r.total && at + k < out.getNumSamples(); ++k)
    {
        float l = 0, rr = 0;
        for (int lane = 0; lane < 3; ++lane) if (part == 3 || part == lane) { l += r.L[(size_t) lane][(size_t) k]; rr += r.R[(size_t) lane][(size_t) k]; }
        d = std::max ({ d, std::fabs (out.getSample (0, at + k) - l), std::fabs (out.getSample (1, at + k) - rr) });
    }
    for (int k = 0; k < at; ++k) d = std::max ({ d, std::fabs (out.getSample (0, k)), std::fabs (out.getSample (1, k)) });
    return d;
}

int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    DrFillProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.25f); b.setSample (1, i, -0.5f); }
        proc.processBlock (b, m);
        check (b.getSample (0, 100) == 0.25f && b.getSample (1, 300) == -0.5f && m.isEmpty(), "idle: the track's audio passes through untouched");
    }
    check (DrFillProcessor::laneFromFileName ("Kick 01") == drf::kKick && DrFillProcessor::laneFromFileName ("BD_loop_120") == drf::kKick
           && DrFillProcessor::laneFromFileName ("808 Kick") == drf::kKick && DrFillProcessor::laneFromFileName ("Snare Roll") == drf::kSnare
           && DrFillProcessor::laneFromFileName ("clap loop") == drf::kSnare && DrFillProcessor::laneFromFileName ("SD 3") == drf::kSnare
           && DrFillProcessor::laneFromFileName ("HiHat 16ths") == drf::kHats && DrFillProcessor::laneFromFileName ("Crash") == drf::kHats
           && DrFillProcessor::laneFromFileName ("Top Loop") == drf::kHats && DrFillProcessor::laneFromFileName ("Ride") == drf::kHats
           && DrFillProcessor::laneFromFileName ("HH_open") == drf::kHats && DrFillProcessor::laneFromFileName ("groove 2") == -1
           && DrFillProcessor::laneFromFileName ("Thumbs") == -1, "file names pick the box (kick / bd / 808, snare / sd / clap, hat / hh / cymbal / top / ride)");
    {
        const auto a = DrFillProcessor::assignLanes ({ "/x/Hats.wav", "/x/mystery.wav", "/x/Kick.wav", "/x/extra.wav" }, {});
        check (a.size() == 4 && a[0] == drf::kHats && a[2] == drf::kKick && a[1] == drf::kSnare && a[3] == -1, "several dropped files: by name, then the empty boxes, extras left out");
        std::array<bool, 3> occ {}; occ[drf::kKick] = true;
        const auto b = DrFillProcessor::assignLanes ({ "/x/a.wav", "/x/b.wav" }, occ);
        check (b[0] == drf::kSnare && b[1] == drf::kHats, "unnamed files fill empty boxes before replacing a loaded loop");
        const auto c = DrFillProcessor::assignLanes ({ "/x/a.wav", "/x/b.wav" }, {}, drf::kHats);
        check (c[0] == drf::kHats && c[1] == drf::kKick, "several unnamed files on one box: that box first");
    }

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drf_proc_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    const juce::File fk = tmp.getChildFile ("Kick 100bpm.wav"), fs = tmp.getChildFile ("Snare.wav"), fh = tmp.getChildFile ("Hats.wav");
    writeWav (fk, drftest::makeLoop (drf::kKick, 100.0, 0).a); writeWav (fs, drftest::makeLoop (drf::kSnare, 100.0, 1).a, 16); writeWav (fh, drftest::makeLoop (drf::kHats, 100.0, 2).a);
    check (proc.loadLoop (drf::kKick, fk).isEmpty() && proc.loadLoop (drf::kSnare, fs).isEmpty() && proc.loadLoop (drf::kHats, fh).isEmpty(), "three loops load (24-bit mono, 16-bit, stereo)");
    check (proc.loadLoop (drf::kKick, juce::File ("/nonexistent.wav")).isNotEmpty(), "missing file reports an error");
    juce::File junkFile = tmp.getChildFile ("junk.wav"); junkFile.replaceWithText ("not audio");
    check (proc.loadLoop (drf::kKick, junkFile).isNotEmpty() && proc.loopName (drf::kKick) == "Kick 100bpm", "unreadable file reports an error, keeps the old loop");
    {
        drf::Audio longA; longA.rate = 8000; longA.L.assign (8000 * 61, 0.0f); const auto fl = tmp.getChildFile ("long.wav"); writeWav (fl, longA, 16);
        check (proc.loadLoop (drf::kKick, fl).contains ("60 seconds"), "loops over 60 s are refused with a reason");
    }
    check (proc.waitIdle (20000), "analysis + fill finish in the background");
    {
        const auto k = proc.kit(); const auto r = proc.render();
        std::printf ("  hits: kick %d snare %d hats %d, tempo %.2f (%s), fill %d events, peak %.2f dBFS\n", (int) (*k)[0].hits.size(), (int) (*k)[1].hits.size(), (int) (*k)[2].hits.size(),
                     proc.fillTempo(), proc.tempoSource().toRawUTF8(), r->events, juce::Decibels::gainToDecibels (r->peak));
        check ((*k)[0].hits.size() == 6 && (*k)[1].hits.size() == 8 && (*k)[2].hits.size() >= 29, "the hits in each loop are found");
        check (std::abs (proc.fillTempo() - 100.0) < 0.05 && proc.tempoSource() == "file name", "Standalone (no host tempo): the fill uses the loops' tempo");
        check (r->events > 0 && r->peak <= juce::Decibels::decibelsToGain (-0.99f) && std::abs (r->bpm - 100.0) < 0.05 && r->rate == 48000.0, "a fill is ready at the loops' tempo and the host rate, under -1 dBFS");
    }
    {   // MIDI: D1 plays the snare part sample-exactly from the note; C1 the kicks; other notes the full fill
        const auto r = proc.render();
        auto o = runSilence (proc, r->total + 2048, 38, 100);
        check (diffToRender (o, *r, DrFillProcessor::kPartSnare, 100) < 1e-6f, "MIDI D1 (38): the snare part, from the note's sample");
        o = runSilence (proc, r->total + 2048, 36, 0);
        check (diffToRender (o, *r, DrFillProcessor::kPartKick, 0) < 1e-6f, "MIDI C1 (36): the kick part");
        o = runSilence (proc, r->total + 2048, 60, 7);
        check (diffToRender (o, *r, DrFillProcessor::kPartFull, 7) < 1e-6f, "any other note: the full fill");
        o = runSilence (proc, r->total + 2048, 60, 0, nullptr, 0.5f);
        float mx = 0, ref = 0; for (int k = 0; k < r->total; ++k) { mx = std::max (mx, std::fabs (o.getSample (0, k))); float l = 0; for (int lane = 0; lane < 3; ++lane) l += r->L[(size_t) lane][(size_t) k]; ref = std::max (ref, std::fabs (l)); }
        check (std::abs (mx / ref - 0.5f) < 0.01f, "velocity sets the level");
        proc.preview (DrFillProcessor::kPartHats);
        o = runSilence (proc, r->total + 2048);
        check (diffToRender (o, *r, DrFillProcessor::kPartHats, 0) < 1e-6f && ! proc.isPlaying(), "PREVIEW with the host stopped plays at once, then stops");
    }
    {   // retrigger half way: the cut-off one fades over 5 ms (no click), no NaN
        const auto r = proc.render();
        juce::AudioBuffer<float> out (2, r->total * 2); out.clear();
        int pos = 0; float jump = 0, prev = 0; bool fin = true;
        for (int blk = 0; pos < out.getNumSamples(); ++blk, pos += 512)
        {
            juce::AudioBuffer<float> b (2, 512); b.clear(); juce::MidiBuffer m;
            if (blk == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            if (blk == r->total / 2 / 512) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 200);
            proc.processBlock (b, m);
            for (int k = 0; k < 512; ++k) { const float v = b.getSample (0, k); fin &= std::isfinite (v); jump = std::max (jump, std::fabs (v - prev)); prev = v; }
        }
        float natJump = 0; prev = 0;
        for (int k = 0; k < r->total; ++k) { float l = 0; for (int lane = 0; lane < 3; ++lane) l += r->L[(size_t) lane][(size_t) k]; natJump = std::max (natJump, std::fabs (l - prev)); prev = l; }
        std::printf ("  retrigger: largest sample step %.3f (the fill's own %.3f)\n", jump, natJump);
        check (fin && jump <= natJump * 1.6f + 0.02f, "a new note while one plays: no click");
    }
    {   // host: tempo follows, PREVIEW lands so the fill ENDS on the next bar line
        FakeHost host; host.bpm = 128; host.ppq = 1.5; proc.setPlayHead (&host);
        runSilence (proc, 512, -1, 0, &host);
        check (proc.waitIdle (20000) && std::abs (proc.render()->bpm - 128.0) < 1e-6 && proc.tempoSource() == "host", "the fill re-renders at the host tempo when it changes");
        auto* len = proc.apvts.getParameter ("length"); len->setValueNotifyingHost (len->convertTo0to1 (2));   // 1 bar
        proc.waitIdle (20000); runSilence (proc, 512, -1, 0, &host); proc.waitIdle (20000);
        const auto r = proc.render();
        const double startPpq = host.ppq;
        proc.preview (DrFillProcessor::kPartFull);
        auto o = runSilence (proc, (int) (3.0 * 48000) + r->total, -1, 0, &host);
        // from startPpq (~1.77): 1-bar fill must end at 8.0 -> start at beat 4
        const int expect = (int) std::round ((4.0 - startPpq) * 60.0 / 128.0 * 48000.0);
        int first = -1; for (int k = 0; k < o.getNumSamples() && first < 0; ++k) if (o.getSample (0, k) != 0.0f || o.getSample (1, k) != 0.0f) first = k;
        float d = 1; for (int s = expect - 2; s <= expect + 2; ++s) d = std::min (d, diffToRender (o, *r, 3, s));
        std::printf ("  bar-synced preview: expected start %d, first sound %d (fill starts with its first hit), diff %.2g\n", expect, first, d);
        check (d < 1e-6f, "PREVIEW while the host plays: the fill ends exactly on the next bar line");
        auto* bpm = proc.apvts.getParameter ("bpm"); bpm->setValueNotifyingHost (bpm->convertTo0to1 (140.0f));
        check (proc.waitIdle (20000) && std::abs (proc.render()->bpm - 140.0) < 0.06 && proc.tempoSource() == "TEMPO knob", "the TEMPO knob overrides the host");
        bpm->setValueNotifyingHost (0.0f);
        proc.setPlayHead (nullptr); runSilence (proc, 512); proc.waitIdle (20000);
    }
    {   // the seed: REGENERATE gives a different fill; the same seed the same fill
        const auto f0 = proc.fill(); const int s0 = juce::roundToInt (proc.apvts.getRawParameterValue ("seed")->load());
        proc.regenerate(); proc.waitIdle (20000);
        const auto f1 = proc.fill();
        bool differ = f1.events.size() != f0.events.size() || f1.style != f0.style;
        for (size_t i = 0; i < std::min (f0.events.size(), f1.events.size()) && ! differ; ++i) differ = f0.events[i].beat != f1.events[i].beat || f0.events[i].lane != f1.events[i].lane;
        check (juce::roundToInt (proc.apvts.getRawParameterValue ("seed")->load()) != s0 && differ, "REGENERATE: new seed, new fill");
    }
    {   // bypass: MIDI does nothing audible, the input passes
        auto* bp = proc.apvts.getParameter ("bypass"); bp->setValueNotifyingHost (1.0f);
        runSilence (proc, 48000);   // let the 12 ms crossfade finish
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.1f); b.setSample (1, i, 0.1f); }
        proc.processBlock (b, m);
        float d = 0; for (int i = 0; i < 512; ++i) d = std::max (d, std::fabs (b.getSample (0, i) - 0.1f));
        check (d < 1e-6f, "bypassed: input passes, notes are silent");
        bp->setValueNotifyingHost (0.0f); runSilence (proc, 48000 * 3);
    }
    // export
    juce::String err;
    const auto files = proc.exportAll (err);
    check (files.size() == 4 && err.isEmpty(), "SAVE WAVS writes kick, snare, hats and the full fill");
    {
        const auto r = proc.render();
        juce::AudioFormatManager fm; fm.registerBasicFormats(); bool ok = files.size() == 4;
        for (auto& f : files)
        {
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (f)));
            ok = ok && rd != nullptr && rd->bitsPerSample == 24 && rd->numChannels == 2 && rd->sampleRate == r->rate && rd->lengthInSamples == r->total
                 && juce::File (f).getParentDirectory() == proc.exportFolder() && juce::File (f).getFileName().startsWith ("Dr Fill - ");
            std::printf ("  %s\n", juce::File (f).getFileName().toRawUTF8());
        }
        check (ok, "24-bit stereo WAVs at the host rate, fill + tail, in Documents/CMPLXTY Dr Fill");
        const auto one = proc.exportPart (DrFillProcessor::kPartSnare, err);
        check (one.getFileName().endsWith (" - Snare.wav"), "drag file for one part");
    }

    // state: loops are embedded (FLAC) -> the same fill comes back even if the files are gone
    juce::MemoryBlock st; proc.getStateInformation (st);
    std::printf ("  state size %d KB\n", (int) (st.getSize() / 1024));
    tmp.deleteRecursively();
    {
        DrFillProcessor p2; p2.setPlayConfigDetails (2, 2, 48000.0, 512); p2.prepareToPlay (48000.0, 512);
        p2.setStateInformation (st.getData(), (int) st.getSize());
        check (p2.waitIdle (20000) && p2.hasLoop (0) && p2.hasLoop (1) && p2.hasLoop (2) && p2.loopName (drf::kSnare) == "Snare", "loops + names survive save / load (files deleted)");
        const auto a = proc.render(), b = p2.render();
        float d = 0; bool same = a->total == b->total && a->events == b->events;
        for (int lane = 0; lane < 3 && same; ++lane) for (int k = 0; k < a->total; ++k) d = std::max (d, std::fabs (a->L[(size_t) lane][(size_t) k] - b->L[(size_t) lane][(size_t) k]));
        std::printf ("  reloaded fill: largest difference %.2g (24-bit FLAC)\n", d);
        check (same && d < 1e-4f, "the same fill after reopening the project");
        juce::MemoryBlock st2; p2.getStateInformation (st2); juce::MemoryBlock st3; p2.getStateInformation (st3);
        check (st2 == st3, "state is stable (cached FLAC)");
        p2.clearAll(); check (! p2.hasAnyLoop() && p2.waitIdle (20000) && p2.render()->events == 0, "CLEAR empties all boxes");
        juce::String e2; check (! p2.exportPart (3, e2).existsAsFile() && e2.isNotEmpty(), "saving with no fill explains why");
        const char junk[] = "DRF1\xff\xff\xff\x7fgarbage"; p2.setStateInformation (junk, sizeof (junk));
        const char junk2[] = "DRF1\x10\x00\x00\x00<DrFillState/>\x7f\x7f\x7f\x7f"; p2.setStateInformation (junk2, sizeof (junk2));
        check (! p2.hasAnyLoop(), "garbage state ignored");
    }
    {   // mono output bus
        DrFillProcessor pm; pm.setPlayConfigDetails (1, 1, 44100.0, 256); pm.prepareToPlay (44100.0, 256);
        loadDemoKit (pm); pm.waitIdle (20000);
        juce::AudioBuffer<float> b (1, 256); bool fin = true; float pk = 0;
        for (int i = 0; i < 400; ++i) { b.clear(); juce::MidiBuffer m; if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0); pm.processBlock (b, m);
            for (int k = 0; k < 256; ++k) { fin &= std::isfinite (b.getSample (0, k)); pk = std::max (pk, std::fabs (b.getSample (0, k))); } }
        check (fin && pk > 0.05f && pk < 1.0f, "mono track: the fill plays (summed)");
    }
    proc.exportFolder().deleteRecursively();
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    drf::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drf_shot_settings");
    drf::upd::dirOverride().deleteRecursively();
    { drf::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      drf::upd::writeJson (drf::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<DrFillProcessor> ([] (DrFillProcessor& q) -> pk::PresetManager& { return q.presets; }, drf::upd::dataDir().getChildFile ("Presets"), 2, 4.0f, 1.0,
                                                       [] (const juce::MemoryBlock& mb) -> std::unique_ptr<juce::XmlElement> {   // "DRF1" + int + XML text + loops
                                                           if (mb.getSize() < 8 || std::memcmp (mb.getData(), "DRF1", 4) != 0) return nullptr;
                                                           juce::MemoryInputStream in (mb, false); in.skipNextBytes (4); const int n = in.readInt();
                                                           juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, n); return juce::parseXML (xm.toString()); });
    DrFillProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    if (mode == "cpu")   // audio-thread cost: a full fill retriggered every bar for 60 s of 48 kHz stereo, 256-sample blocks
    {
        DrFillProcessor q; q.setPlayConfigDetails (2, 2, 48000.0, 256); q.prepareToPlay (48000.0, 256);
        loadDemoKit (q); q.waitIdle (20000);
        juce::AudioBuffer<float> b (2, 256); const int blocks = 60 * 48000 / 256; double worst = 0;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < blocks; ++i)
        {
            b.clear(); juce::MidiBuffer m; if (i % 375 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 17);
            const auto a = juce::Time::getHighResolutionTicks(); q.processBlock (b, m);
            worst = std::max (worst, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - a));
        }
        const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        std::printf ("CPU: %.3f %% of one core (60 s of audio in %.1f ms), worst block %.1f us of %.0f us\n", sec / 60.0 * 100.0, sec * 1000.0, worst * 1e6, 256.0 / 48000.0 * 1e6);
        juce::String e; const auto t1 = juce::Time::getHighResolutionTicks(); q.regenerate(); q.waitIdle (20000);
        std::printf ("REGENERATE (worker thread, incl. the 150 ms test wait): %.0f ms\n", juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t1) * 1000.0);
        return 0;
    }
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
        loadDemoKit (proc);
        proc.waitIdle (20000);
    }
    if (mode.isEmpty() || mode == "presets") { proc.presets.loadFactory (3); proc.waitIdle (20000); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<DrFillEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (DrFillProcessor::kBaseW * 3 / 2, DrFillProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { drf::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/dr-fill/"; i.notes = "More fill styles"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode == "drop")   // drag and drop onto the boxes, through the display's own drop handler
    {
        proc.clearAll();
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drf_drop_test"); tmp.deleteRecursively(); tmp.createDirectory();
        auto mk = [&] (const char* n, int lane) { auto f = tmp.getChildFile (n); writeWav (f, drftest::makeLoop (lane, 120, 0).a); return f.getFullPathName(); };
        const auto any = mk ("groove.wav", 1), kick = mk ("BD loop.wav", 0), hat = mk ("Open HH.wav", 2), snr = mk ("Clap.wav", 1);
        tmp.getChildFile ("notes.txt").replaceWithText ("x");
        auto* d = e->getPanel().display();
        drfui::displayDrop (d, drf::kHats, { any });          check (proc.loopName (drf::kHats) == "groove", "one file on a box loads it there, whatever its name");
        drfui::displayDrop (d, 20, { snr, kick, hat });        check (proc.loopName (drf::kKick) == "BD loop" && proc.loopName (drf::kSnare) == "Clap" && proc.loopName (drf::kHats) == "Open HH",
                                                                    "several files dropped anywhere go to the boxes by name");
        drfui::displayDrop (d, 20, { tmp.getChildFile ("notes.txt").getFullPathName() }); check (proc.loopName (drf::kHats) == "Open HH", "a non-audio drop changes nothing");
        auto* tgt = dynamic_cast<juce::FileDragAndDropTarget*> (d);
        check (tgt != nullptr && tgt->isInterestedInFileDrag ({ "/a/x.mp3" }) && tgt->isInterestedInFileDrag ({ "/a/x.AIFF" }) && tgt->isInterestedInFileDrag ({ "/a/x.flac" })
               && ! tgt->isInterestedInFileDrag ({ "/a/x.txt" }), "wav / mp3 / flac / aiff / ogg are accepted");
        check (proc.waitIdle (20000) && proc.render()->events > 0, "a fill is built from the dropped loops");
        tmp.deleteRecursively();
        std::printf (fails == 0 ? "DROP TESTS PASSED\n" : "%d DROP TEST(S) FAILED\n", fails);
        ed.reset(); return fails;
    }
    if (mode == "hover") drfui::displayHover (e->getPanel().display(), drf::kSnare);
    if (mode == "playing") { proc.preview (3); juce::AudioBuffer<float> b (2, 512); for (int i = 0; i < 60; ++i) { b.clear(); juce::MidiBuffer m; proc.processBlock (b, m); } }
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
