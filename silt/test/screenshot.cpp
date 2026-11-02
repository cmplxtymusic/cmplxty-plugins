// Renders the real SiltEditor to a PNG, and runs the processor-level tests.
// usage: SiltScreenshot out.png [banner|menu|big|empty|hover|playing|slice|tune|mod|fx|presets|keys|drop|params|cpu|proc]
#include "PluginEditor.h"
#include "PresetKitTest.h"
#include "SiltParams.h"

namespace sltui { void fxRackMove (juce::Component* rack, int from, int to); }

namespace
{
int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }
constexpr double kPi = 3.14159265358979323846;

// band-limited saw at a MIDI pitch, gentle decay at the end
slt::Audio saw (double midi, double sec, double sr = 44100.0, bool stereo = false)
{
    slt::Audio a; a.rate = sr; a.L.assign ((size_t) (sec * sr), 0.0f);
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    const int nh = (int) std::min (40.0, sr * 0.45 / f);
    for (size_t i = 0; i < a.L.size(); ++i)
    {
        const double t = (double) i / sr; double v = 0;
        for (int h = 1; h <= nh; ++h) v += std::sin (2.0 * kPi * f * h * t) / h;
        const double env = std::min (1.0, t / 0.01) * std::min (1.0, (sec - t) / 0.05);
        a.L[i] = (float) (0.35 * v * env);
    }
    if (stereo) { a.R = a.L; for (size_t i = 1; i < a.R.size(); ++i) a.R[i] = 0.8f * a.L[i - 1]; }   // real stereo (different channels; dual mono is kept mono)
    return a;
}
// four decaying noise / tone hits, 0.25 s apart (clear transients)
slt::Audio hits (double sr = 44100.0)
{
    slt::Audio a; a.rate = sr; a.L.assign ((size_t) (1.0 * sr), 0.0f);
    juce::Random r (3);
    for (int h = 0; h < 4; ++h)
        for (int i = 0; i < (int) (0.2 * sr); ++i)
        {
            const double t = i / sr;
            a.L[(size_t) (h * 0.25 * sr) + (size_t) i] = (float) (std::exp (-t * 25.0) * (0.6 * std::sin (2 * kPi * (80.0 + 60 * h) * t) + 0.3 * (r.nextFloat() - 0.5f)));
        }
    return a;
}
void writeWav (const juce::File& f, const slt::Audio& a)
{
    f.deleteFile();
    juce::WavAudioFormat wav; const int ch = a.R.empty() ? 1 : 2;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), a.rate, (unsigned) ch, 24, {}, 0));
    juce::AudioBuffer<float> b (ch, (int) a.L.size());
    b.copyFrom (0, 0, a.L.data(), (int) a.L.size()); if (ch > 1) b.copyFrom (1, 0, a.R.data(), (int) a.R.size());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct FakeHost final : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0, sr = 48000; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 }); return p; }
    void advance (int n) { if (playing) ppq += n / sr * bpm / 60.0; }
};

void setP (SiltProcessor& p, const char* id, float real) { auto* r = p.apvts.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (real)); }
float getP (SiltProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

struct Ev { int at, note; float vel; };   // vel 0 = note off
// renders `samples` (stereo, 512 blocks); events at absolute sample positions
juce::AudioBuffer<float> run (SiltProcessor& p, int samples, std::vector<Ev> ev = {}, FakeHost* host = nullptr, int nch = 2)
{
    juce::AudioBuffer<float> out (nch, samples); out.clear();
    for (int pos = 0; pos < samples; pos += 512)
    {
        const int n = juce::jmin (512, samples - pos);
        juce::AudioBuffer<float> b (nch, n); b.clear(); juce::MidiBuffer m;
        for (auto& e : ev) if (e.at >= pos && e.at < pos + n)
            m.addEvent (e.vel > 0 ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), e.at - pos);
        p.processBlock (b, m);
        for (int c = 0; c < nch; ++c) out.copyFrom (c, pos, b, c, 0, n);
        if (host) host->advance (n);
    }
    return out;
}
std::vector<float> monoOf (const juce::AudioBuffer<float>& b, int a, int e)
{
    std::vector<float> m; for (int i = a; i < juce::jmin (e, b.getNumSamples()); ++i) m.push_back (b.getNumChannels() > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i));
    return m;
}
double rmsOf (const std::vector<float>& m) { double e = 0; for (float v : m) e += (double) v * v; return std::sqrt (e / std::max<size_t> (1, m.size())); }
bool finite (const juce::AudioBuffer<float>& b, float* pk = nullptr)
{
    bool ok = true; float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { const float x = b.getSample (c, i); ok = ok && std::isfinite (x); p = std::max (p, std::fabs (x)); }
    if (pk) *pk = p; return ok;
}
void measureSetup (SiltProcessor& p)   // dry, steady measurement settings
{
    setP (p, "rvOn", 0); setP (p, "pjit", 0); setP (p, "cohere", 1.0f); setP (p, "spray", 0.02f); setP (p, "aA", 0.005f); setP (p, "aR", 0.05f); setP (p, "bOn", 0);
}

int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double SR = 48000.0;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("slt_proc_test"); tmp.deleteRecursively(); tmp.createDirectory();
    SiltProcessor proc;
    proc.setPlayConfigDetails (0, 2, SR, 512); proc.prepareToPlay (SR, 512);
    check (proc.waitIdle (20000) && proc.source (0) != nullptr && proc.builtInOf (0) >= 0, "a fresh instance has a built-in sample on A, so it plays straight away");
    {
        auto o = run (proc, 48000); float pk = 0;
        check (finite (o, &pk) && pk == 0.0f, "no notes: digital silence");
    }
    // --- file loading
    const auto fA = tmp.getChildFile ("Pad A2.wav"); writeWav (fA, saw (57, 2.0));   // A2 (C3 = 60)
    const auto fUntagged = tmp.getChildFile ("texture 7.wav"); writeWav (fUntagged, saw (52, 2.0, 44100.0, true));   // E2, no key in the name
    const auto fLong = tmp.getChildFile ("too long.wav"); { slt::Audio a; a.rate = 8000; a.L.assign (8000 * 61, 0.01f); writeWav (fLong, a); }
    const auto fJunk = tmp.getChildFile ("junk.wav"); fJunk.replaceWithText ("this is not audio");
    check (proc.loadFile (0, tmp.getChildFile ("missing.wav")).isNotEmpty(), "missing file: error text");
    check (proc.loadFile (0, fJunk).isNotEmpty(), "unreadable file: error text");
    { const auto e = proc.loadFile (0, fLong); check (e.isNotEmpty() && proc.builtInOf (0) >= 0, "a 61 s file is refused (limit 60 s), the old sample stays"); std::printf ("  says: %s\n", e.toRawUTF8()); }
    check (proc.loadFile (0, fA).isEmpty() && proc.waitIdle (20000) && proc.sampleName (0) == "Pad A2", "a dropped wav loads on A");
    {
        auto s = proc.source (0);
        std::printf ("  root: %s (effective %.2f)\n", proc.rootDescription().toRawUTF8(), proc.effectiveRoot());
        check (s && std::abs (proc.effectiveRoot() - 57.0f) < 0.05f && proc.rootDescription().contains ("name"), "root key from the file name: 'Pad A2' -> A2");
    }
    measureSetup (proc);
    for (int note : { 48, 60, 67 })
    {
        auto o = run (proc, 48000, { { 0, note, 0.9f }, { 40000, note, 0 } });
        const auto pr = slt::detectPitch (monoOf (o, 9600, 38400), SR);
        std::printf ("  note %d -> %.3f (%.1f cents)\n", note, pr.midi, (pr.midi - note) * 100.0f);
        check (std::abs (pr.midi - (float) note) < 0.1f, note == 60 ? "C plays C: C3 on an A2 sample sounds C3 (within 10 cents)" : "every key plays its own pitch (within 10 cents)");
        run (proc, 24000);
    }
    check (proc.loadFile (0, fUntagged).isEmpty() && proc.waitIdle (20000), "untagged stereo file loads");
    std::printf ("  untagged root: %s\n", proc.rootDescription().toRawUTF8());
    {
        check (std::abs (proc.effectiveRoot() - 52.0f) < 0.1f && proc.rootDescription().contains ("detected"), "no key in the name: root found by ear (E2)");
        auto o = run (proc, 48000, { { 0, 60, 0.9f }, { 40000, 60, 0 } });
        const auto pr = slt::detectPitch (monoOf (o, 9600, 38400), SR);
        std::printf ("  C3 on it -> %.3f\n", pr.midi);
        check (std::abs (pr.midi - 60.0f) < 0.1f, "...and C3 still plays C3");
        run (proc, 24000);
    }
    {   // Tune Manual + USE DETECTED ROOT
        proc.useDetectedRoot();
        check (juce::roundToInt (getP (proc, "tune")) == slt::tuneManual && std::abs (getP (proc, "root") - 52.0f) < 0.6f, "USE DETECTED ROOT: Tune -> Manual at the detected root");
        setP (proc, "tune", (float) slt::tuneAuto);
    }
    {   // velocity
        auto lo = run (proc, 24000, { { 0, 60, 0.2f }, { 20000, 60, 0 } }); run (proc, 24000);
        auto hi = run (proc, 24000, { { 0, 60, 1.0f }, { 20000, 60, 0 } }); run (proc, 24000);
        const double rl = rmsOf (monoOf (lo, 4800, 19200)), rh = rmsOf (monoOf (hi, 4800, 19200));
        std::printf ("  velocity 0.2 -> %.4f rms, 1.0 -> %.4f rms\n", rl, rh);
        check (rh > rl * 1.5 && rl > 1e-3, "velocity changes the level");
    }
    {   // release: silent after the note and its release
        auto o = run (proc, 48000, { { 0, 60, 1.0f }, { 4800, 60, 0 } });
        check (rmsOf (monoOf (o, 30000, 48000)) < 1e-5, "note off: silent after the release");
    }
    {   // 16 voices
        setP (proc, "voices", 16); std::vector<Ev> ev; for (int i = 0; i < 16; ++i) ev.push_back ({ i * 64, 40 + i * 3, 0.8f });
        auto o = run (proc, 24000, ev); int live = 0; proc.engine().newestPitch (&live); float pk = 0;
        check (finite (o, &pk) && live == 16 && pk < 1.0f, "16 voices at once, no overs (limiter)");
        std::printf ("  16 voices: peak %.3f\n", pk);
        std::vector<Ev> off; for (int i = 0; i < 16; ++i) off.push_back ({ 0, 40 + i * 3, 0 }); run (proc, 48000, off);
    }
    // --- slice mode (key per chop from C1) + host-synced sequencer
    {
        check (proc.loadAudio (0, hits(), "Hits").isEmpty() && proc.waitIdle (20000), "test hits loaded");
        setP (proc, "mode", (float) slt::modeSlice); setP (proc, "slSrc", 0); proc.waitIdle (20000);
        const auto sl = proc.slices();
        std::printf ("  slices at:"); for (int s : sl) std::printf (" %.3f s", s / 44100.0); std::printf ("\n");
        check (sl.size() == 4, "4 hits -> 4 transient slices");
        auto a = run (proc, 12000, { { 0, 37, 1.0f } }); run (proc, 24000, { { 0, 37, 0 } });
        auto none = run (proc, 12000, { { 0, 36 + 4, 1.0f } }); run (proc, 12000, { { 0, 40, 0 } });
        check (rmsOf (monoOf (a, 0, 6000)) > 0.01 && rmsOf (monoOf (none, 0, 12000)) == 0.0, "C#1 plays chop 2; a key past the last chop is silent");
        setP (proc, "slSrc", (float) slt::kSlGrid16); proc.waitIdle (20000);
        check (proc.slices().size() == 16, "Grid 16: 16 equal chops");
        proc.setManualSlices ({ 0, 10000, 30000 }); proc.waitIdle (20000);
        check (juce::roundToInt (getP (proc, "slSrc")) == slt::kSlManual && proc.slices().size() == 3, "editing the slice grid switches to Manual and keeps the edit");
        setP (proc, "slSrc", 0); proc.waitIdle (20000);
        // sequencer runs from the host transport
        setP (proc, "seq", 1); setP (proc, "seqTrig", 1); for (int i = 1; i <= 16; ++i) setP (proc, ("st" + juce::String (i)).toRawUTF8(), (float) (1 + (i - 1) % 4));
        FakeHost host; host.sr = SR; proc.setPlayHead (&host);
        auto o = run (proc, 96000, {}, &host);
        host.playing = false; auto st = run (proc, 48000, {}, &host);
        proc.setPlayHead (nullptr);
        std::printf ("  host playing: %.4f rms, stopped: %.6f rms (last 0.5 s)\n", rmsOf (monoOf (o, 0, 96000)), rmsOf (monoOf (st, 24000, 48000)));
        check (rmsOf (monoOf (o, 0, 96000)) > 0.01 && rmsOf (monoOf (st, 24000, 48000)) < 1e-4, "sequencer (Runs While: Host Play) plays with the DAW and stops with it");
        setP (proc, "seq", 0); setP (proc, "seqTrig", 0); setP (proc, "mode", (float) slt::modeGranular);
    }
    // --- modulation: drag a source onto a knob = next free matrix slot
    {
        const int s = proc.assignMod (slt::msLfo1, slt::mdDensity);
        check (s == 0 && juce::roundToInt (getP (proc, "ms1")) == slt::msLfo1 && juce::roundToInt (getP (proc, "md1")) == slt::mdDensity && proc.modSlotFor (slt::msLfo1, slt::mdDensity) == 0,
               "LFO 1 -> Density goes in slot 1");
        for (int i = 0; i < 7; ++i) proc.assignMod (slt::msRand, 1 + i);
        check (proc.assignMod (slt::msM1, slt::mdCutoff) < 0, "a 9th assignment is refused (8 slots)");
        for (int i = 0; i < 8; ++i) proc.removeMod (i);
        check (juce::roundToInt (getP (proc, "ms1")) == 0 && juce::roundToInt (getP (proc, "ms8")) == 0, "removing clears the slots");
        check (SiltProcessor::modDstForParam ("cutoff") == slt::mdCutoff && SiltProcessor::modDstForParam ("voices") == 0, "knob -> mod target map (Cutoff yes, Voices no)");
    }
    // --- bypass
    {
        auto* bp = proc.apvts.getParameter ("bypass"); bp->setValueNotifyingHost (1.0f);
        run (proc, 4800);
        auto o = run (proc, 24000, { { 0, 60, 1.0f } }); run (proc, 4800, { { 0, 60, 0 } });
        check (rmsOf (monoOf (o, 2400, 24000)) < 1e-6, "bypassed: notes are silent");
        bp->setValueNotifyingHost (0.0f); run (proc, 48000);
    }
    // --- state: samples embedded (FLAC), files deleted
    {
        check (proc.loadFile (0, fA).isEmpty() && proc.loadFile (1, fUntagged).isEmpty() && proc.waitIdle (20000), "A and B loaded from files");
        setP (proc, "bOn", 1); setP (proc, "density", 77); setP (proc, "mode", (float) slt::modeSlice); proc.setManualSlices ({ 0, 20000, 50000 }); proc.waitIdle (20000);
        juce::MemoryBlock st; proc.getStateInformation (st);
        std::printf ("  state size %d KB\n", (int) (st.getSize() / 1024));
        auto a0 = proc.source (0), b0 = proc.source (1);
        tmp.deleteRecursively();
        SiltProcessor p2; p2.setPlayConfigDetails (0, 2, SR, 512); p2.prepareToPlay (SR, 512);
        p2.setStateInformation (st.getData(), (int) st.getSize());
        check (p2.waitIdle (20000) && p2.sampleName (0) == "Pad A2" && p2.sampleName (1) == "texture 7" && p2.hasSample (0) && p2.hasSample (1), "samples + names survive save / load (files deleted)");
        auto a1 = p2.source (0), b1 = p2.source (1);
        float d = 0; bool same = a1 && b1 && a0->audio->len() == a1->audio->len() && b0->audio->len() == b1->audio->len() && ! b1->audio->R.empty();
        if (same) for (size_t i = 0; i < a0->audio->L.size(); ++i) d = std::max (d, std::fabs (a0->audio->L[i] - a1->audio->L[i]));
        if (same) for (size_t i = 0; i < b0->audio->R.size(); ++i) d = std::max (d, std::fabs (b0->audio->R[i] - b1->audio->R[i]));
        if (a1 && b1) std::printf ("  lengths A %d/%d B %d/%d, B right channel %d/%d\n", (int) a0->audio->len(), (int) a1->audio->len(), (int) b0->audio->len(), (int) b1->audio->len(), (int) b0->audio->R.size(), (int) b1->audio->R.size());
        std::printf ("  reloaded sample: largest difference %.2g (24-bit FLAC)\n", d);
        check (same && d < 1e-4f, "the same audio after reopening the project (stereo kept)");
        check (std::abs (getP (p2, "density") - 77.0f) < 0.01f && p2.manualSlices() == std::vector<int> ({ 0, 20000, 50000 }) && p2.slices().size() == 3, "settings and the edited slice grid come back");
        check (std::abs (p2.effectiveRoot() - 57.0f) < 0.05f, "the root key comes back");
        juce::MemoryBlock s2, s3; p2.getStateInformation (s2); p2.getStateInformation (s3);
        check (s2 == s3, "state is stable");
        // built-in samples are stored by reference (small state)
        SiltProcessor p3; p3.setPlayConfigDetails (0, 2, SR, 512); p3.prepareToPlay (SR, 512); p3.waitIdle (20000);
        juce::MemoryBlock sb; p3.getStateInformation (sb); std::printf ("  state with built-ins only: %d bytes\n", (int) sb.getSize());
        check (sb.getSize() < 64 * 1024, "built-in samples are not copied into the session");
        const char junk[] = "SLT1\xff\xff\xff\x7fgarbage"; p2.setStateInformation (junk, sizeof (junk));
        const char junk2[] = "SLT1\x10\x00\x00\x00<SiltState/>\x7f\x7f\x7f\x7f"; p2.setStateInformation (junk2, sizeof (junk2));
        p2.waitIdle (20000);
        check (p2.sampleName (0) == "Pad A2", "garbage state ignored");
        // user preset carries the dropped sample
        auto f = p2.presets.userFileFor ("Silt Sample Trip");
        check (p2.presets.saveFile (f, "Silt Sample Trip"), "user preset saved");
        p2.presets.loadFactory (1); p2.waitIdle (20000);
        check (p2.sampleName (0) != "Pad A2" || getP (p2, "keepSmp") > 0.5f, "factory preset swaps in its built-in sample (Keep My Sample off)");
        check (p2.presets.loadFile (f) && p2.waitIdle (20000) && p2.sampleName (0) == "Pad A2" && p2.hasSample (1), "the user preset brings its own samples back");
        setP (p2, "keepSmp", 1); p2.presets.loadFactory (2); p2.waitIdle (20000);
        check (p2.sampleName (0) == "Pad A2", "Keep My Sample on: a preset changes the sound, not the sample");
        f.deleteFile();
    }
    {   // mono output bus
        SiltProcessor pm; pm.setPlayConfigDetails (0, 1, 44100.0, 256);
        check (pm.checkBusesLayoutSupported ([] { juce::AudioProcessor::BusesLayout l; l.outputBuses.add (juce::AudioChannelSet::mono()); return l; }()), "mono output layout supported");
        pm.prepareToPlay (44100.0, 256); pm.waitIdle (20000);
        auto o = run (pm, 44100, { { 0, 60, 1.0f } }, nullptr, 1); float pk = 0;
        check (finite (o, &pk) && pk > 0.02f && pk < 1.0f, "mono track: plays (summed)");
    }
    // --- every factory preset makes sound from MIDI at 44.1 / 48 / 96 kHz (chord, 2 s)
    {
        int bad = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            SiltProcessor q; q.setPlayConfigDetails (0, 2, rate, 512); q.prepareToPlay (rate, 512);
            for (int i = 0; i < q.presets.numFactory(); ++i)
            {
                q.presets.loadFactory (i); q.waitIdle (20000);
                const int n = (int) (rate * 2);
                juce::AudioBuffer<float> out (2, n); out.clear();
                for (int pos = 0; pos < n; pos += 512)
                {
                    juce::AudioBuffer<float> b (2, juce::jmin (512, n - pos)); juce::MidiBuffer m;
                    if (pos == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 38, 0.9f), 0); }
                    q.processBlock (b, m);
                    for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, b.getNumSamples());
                }
                float pk = 0; const bool fin = finite (out, &pk); const double r = rmsOf (monoOf (out, 0, n));
                if (! fin || pk > 1.0f || r < 0.003) { ++bad; names << pk::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk " << pk << " rms " << r << "; "; }
                juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                juce::AudioBuffer<float> b (2, 512); q.processBlock (b, off); q.reset();
            }
        }
        std::printf ("  %d presets x 3 rates with a 3-note chord: %d bad %s\n", (int) pk::factoryPresets().size(), bad, names.toRawUTF8());
        check (bad == 0, "every factory preset plays from MIDI: audible, no NaN, no overs");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    slt::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("slt_shot_settings");
    slt::upd::dirOverride().deleteRecursively();
    { slt::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      slt::upd::writeJson (slt::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<SiltProcessor> ([] (SiltProcessor& q) -> pk::PresetManager& { return q.presets; }, slt::upd::dataDir().getChildFile ("Presets"),
                                                       0, 1.0f, -1.0,   // a synth: no input, silent without notes (sound is tested above with MIDI)
                                                       [] (const juce::MemoryBlock& mb) -> std::unique_ptr<juce::XmlElement> {   // "SLT1" + int + XML text + samples
                                                           if (mb.getSize() < 8 || std::memcmp (mb.getData(), "SLT1", 4) != 0) return nullptr;
                                                           juce::MemoryInputStream in (mb, false); in.skipNextBytes (4); const int n = in.readInt();
                                                           juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, n); return juce::parseXML (xm.toString()); });
    SiltProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    if (mode == "cpu")   // audio-thread cost through the whole plug-in, 48 kHz stereo, 256-sample blocks, 30 s each
    {
        auto bench = [] (const char* what, std::function<void (SiltProcessor&)> setup, std::vector<int> notes)
        {
            SiltProcessor q; q.setPlayConfigDetails (0, 2, 48000.0, 256); q.prepareToPlay (48000.0, 256); setup (q); q.waitIdle (20000);
            juce::AudioBuffer<float> b (2, 256); const int blocks = 30 * 48000 / 256; double worst = 0;
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < blocks; ++i)
            {
                b.clear(); juce::MidiBuffer m; if (i == 0) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0);
                const auto a = juce::Time::getHighResolutionTicks(); q.processBlock (b, m);
                worst = std::max (worst, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - a));
            }
            const double sec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            std::printf ("CPU %-44s %6.2f %% of one core, worst block %.0f us of %.0f us, %d grains\n", what, sec / 30.0 * 100.0, worst * 1e6, 256.0 / 48000.0 * 1e6, q.engine().activeGrains());
        };
        bench ("Init, one note", [] (SiltProcessor&) {}, { 60 });
        bench ("Glass Cloud, 4-note chord", [] (SiltProcessor& q) { q.presets.loadFactory (1); }, { 48, 55, 60, 64 });
        bench ("Morph Glass to Choir, 4-note chord", [] (SiltProcessor& q) { q.presets.loadFactory (8); }, { 48, 55, 60, 64 });
        bench ("worst: 16 voices, 200/s, 500 ms, B Cross, all FX", [] (SiltProcessor& q) {
            q.presets.loadFactory (10);
            for (auto kv : std::vector<std::pair<const char*, float>> { { "voices", 16 }, { "density", 200 }, { "size", 500 }, { "fOn", 1 }, { "cpOn", 1 }, { "dlOn", 1 }, { "rvOn", 1 }, { "rpOn", 1 } }) setP (q, kv.first, kv.second); },
            { 36, 40, 43, 47, 48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83 });
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
    proc.waitIdle (20000);
    if (mode == "empty") { proc.clearLayer (0); proc.clearLayer (1); proc.waitIdle (20000); }
    if (mode.isEmpty() || mode == "presets" || mode == "playing" || mode == "big") { proc.presets.loadFactory (8); proc.waitIdle (20000); }   // Morph Glass to Choir
    if (mode == "slice") { proc.presets.loadFactory (5); proc.waitIdle (20000); }   // Break Seq 16ths
    if (mode == "tune") { proc.presets.loadFactory (3); proc.waitIdle (20000); }
    if (mode == "mod") { proc.presets.loadFactory (17); proc.waitIdle (20000); }
    if (mode == "fx") { proc.presets.loadFactory (16); proc.waitIdle (20000); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<SiltEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (SiltProcessor::kBaseW * 3 / 2, SiltProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { slt::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/silt/"; i.notes = "More built-in samples"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto pa = pk::test::auditPresets (e->getPanel(), e->getPanel().getPresetBar(), e->getPanel().getPresetMenu(), proc.presets); std::printf ("%s", pa.toRawUTF8());
        return (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK") && pa.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode == "drop")   // drag and drop onto A / B, through the display's own drop handler; drag-to-knob; FX rack reorder
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("slt_drop_test"); tmp.deleteRecursively(); tmp.createDirectory();
        auto mk = [&] (const char* n, double midi) { auto f = tmp.getChildFile (n); writeWav (f, saw (midi, 1.0)); return f.getFullPathName(); };
        const auto one = mk ("Bass C2.wav", 48), two = mk ("Lead G3.wav", 67), three = mk ("Vox D3.aiff.wav", 62);
        tmp.getChildFile ("notes.txt").replaceWithText ("x");
        auto* d = e->getPanel().display();
        sltui::displayDrop (d, 0, { one });  proc.waitIdle (20000); check (proc.sampleName (0) == "Bass C2", "a file dropped on the sample view loads on A");
        sltui::displayDrop (d, 1, { two });  proc.waitIdle (20000); check (proc.sampleName (1) == "Lead G3" && proc.sampleName (0) == "Bass C2", "a file dropped on the B strip loads on B");
        sltui::displayDrop (d, 20, { three, one }); proc.waitIdle (20000); check (proc.sampleName (0) == "Vox D3.aiff" && proc.sampleName (1) == "Bass C2", "two files dropped at once: A then B");
        sltui::displayDrop (d, 0, { tmp.getChildFile ("notes.txt").getFullPathName() }); check (proc.sampleName (0) == "Vox D3.aiff", "a non-audio drop changes nothing");
        auto* tgt = dynamic_cast<juce::FileDragAndDropTarget*> (d);
        check (tgt != nullptr && tgt->isInterestedInFileDrag ({ "/a/x.mp3" }) && tgt->isInterestedInFileDrag ({ "/a/x.AIFF" }) && tgt->isInterestedInFileDrag ({ "/a/x.flac" })
               && tgt->isInterestedInFileDrag ({ "/a/x.ogg" }) && ! tgt->isInterestedInFileDrag ({ "/a/x.txt" }), "wav / aiff / flac / ogg / mp3 are accepted");
        // drag-to-knob modulation through the panel
        const auto id = e->getPanel().assignModTo (slt::msLfo2, "cutoff");
        check (id == "cutoff" && proc.modSlotFor (slt::msLfo2, slt::mdCutoff) >= 0, "dropping the LFO 2 chip on CUTOFF assigns it");
        check (e->getPanel().assignModTo (slt::msLfo2, "voices").isEmpty(), "dropping on a knob that can't be modulated does nothing");
        // FX rack reorder
        e->getPanel().setPage (4);
        const int before = juce::roundToInt (getP (proc, "fxOrder"));
        sltui::fxRackMove (e->getPanel().extra (3), 0, 3);
        const auto o = slt::fxPerm (juce::roundToInt (getP (proc, "fxOrder"))), o0 = slt::fxPerm (before);
        check (o[3] == o0[0] && o[0] == o0[1], "FX rack: dragging the first block to the end moves it last");
        tmp.deleteRecursively();
        std::printf (fails == 0 ? "DROP TESTS PASSED\n" : "%d DROP TEST(S) FAILED\n", fails);
        ed.reset(); return fails;
    }
    if (mode == "hover") sltui::displayHover (e->getPanel().display(), 1);
    if (mode == "tune") e->getPanel().setPage (1);
    if (mode == "mod") e->getPanel().setPage (2);
    if (mode == "slice") e->getPanel().setPage (3);
    if (mode == "fx") e->getPanel().setPage (4);
    if (mode.isEmpty() || mode == "playing" || mode == "big" || mode == "slice" || mode == "tune" || mode == "mod" || mode == "fx")
    {   // play a chord (or run the sequencer) so the grain cloud / play heads / steps show
        FakeHost host; proc.setPlayHead (&host);
        std::vector<int> notes = mode == "slice" ? std::vector<int> { 36 } : std::vector<int> { 48, 55, 60, 64 };
        juce::AudioBuffer<float> b (2, 512);
        for (int i = 0; i < 90; ++i) { b.clear(); juce::MidiBuffer m; if (i == 0) for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.9f), 0); proc.processBlock (b, m); host.advance (512); }
        proc.setPlayHead (nullptr);
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
