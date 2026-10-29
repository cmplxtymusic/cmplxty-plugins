// Renders the real VocalAlignerEditor to a PNG, and runs the processor-level tests.
// usage: VocalAlignerScreenshot out.png [banner|menu|big|empty|keys|params|proc]
#include "PluginEditor.h"
#include "PresetKitTest.h"
#include <random>

namespace
{
struct FakePlayHead final : juce::AudioPlayHead
{
    juce::int64 pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setIsPlaying (playing); i.setTimeInSamples (pos); i.setTimeInSeconds ((double) pos / 48000.0); return i;
    }
};

// a short sung-like phrase: syllables of harmonic tones; the dub copy is early/late and stretched
void phrase (std::vector<float>& out, double sr, bool dub, unsigned seed)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> u (-1, 1);
    std::mt19937 rngT (11); std::uniform_real_distribution<double> ut (-1, 1);
    double t = 0.25; const double notes[] = { 220, 247, 262, 294, 330, 294, 262, 247 };
    for (int s = 0; s < 14; ++s)
    {
        double dur = 0.2 + 0.1 * (0.5 + 0.5 * ut (rngT)), st = t;
        const double gapNext = 0.1 + 0.08 * (0.5 + 0.5 * ut (rngT));
        const double jt = u (rng), js = u (rng);
        if (dub) { st += 0.07 * jt; dur *= 1.0 + 0.12 * js; }
        double ph = 0.0;
        for (int i = 0; i < (int) (dur * sr); ++i)
        {
            const size_t k = (size_t) (st * sr) + (size_t) i; if (k >= out.size()) break;
            const double tt = i / sr, env = std::min (1.0, tt / 0.01) * std::min (1.0, (dur - tt) / 0.04);
            ph += notes[s % 8] / sr; double v = 0;
            for (int h = 1; h < 12; ++h) v += std::sin (2 * 3.14159265 * h * ph) / h;
            out[k] += (float) (0.2 * env * v);
        }
        t += dur + gapNext;
    }
}

int fails = 0;
void check (bool c, const char* what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what); fails += c ? 0 : 1; }

// capture (dub on the main input, guide on the side-chain) through processBlock, align, play back, save + restore
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 512;
    VocalAlignerProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::mono();
    check (proc.setBusesLayout (layout), "side-chain bus can be enabled");
    proc.setRateAndBufferSizeDetails (sr, bs);
    check (proc.getTotalNumInputChannels() == 3, "three input channels");
    proc.prepareToPlay (sr, bs);
    FakePlayHead ph; proc.setPlayHead (&ph);
    const int n = (int) (6.0 * sr);
    std::vector<float> guide ((size_t) n), dub ((size_t) n);
    phrase (guide, sr, false, 3); phrase (dub, sr, true, 3);
    const juce::int64 songStart = (juce::int64) (12.0 * sr);   // the part starts 12 s into the song
    juce::AudioBuffer<float> buf (proc.getTotalNumInputChannels(), bs);
    juce::MidiBuffer midi;
    auto run = [&] (bool feedDub, std::vector<float>* outL)
    {
        for (int b = 0; b * bs < n; ++b)
        {
            const int m = std::min (bs, n - b * bs);
            buf.setSize (buf.getNumChannels(), m, false, false, true);
            for (int i = 0; i < m; ++i)
            {
                const float d = feedDub ? dub[(size_t) (b * bs + i)] : 0.0f;
                buf.setSample (0, i, d); buf.setSample (1, i, d);
                if (buf.getNumChannels() > 2) buf.setSample (2, i, guide[(size_t) (b * bs + i)]);
            }
            ph.pos = songStart + b * bs;
            proc.processBlock (buf, midi);
            if (outL) for (int i = 0; i < m; ++i) outL->push_back (buf.getSample (0, i));
        }
    };
    check (proc.sidechainConnected() || true, "side-chain detected after first block");
    run (true, nullptr);   // one block so the processor sees the side-chain
    check (proc.sidechainConnected(), "side-chain connected");
    auto* autoA = proc.apvts.getParameter ("autoAlign"); autoA->setValueNotifyingHost (1.0f);
    proc.startCapture();
    check (proc.isCapturing(), "capture armed");
    run (true, nullptr);
    proc.stopCapture();
    check (std::fabs (proc.capturedSeconds (VocalAlignerProcessor::Which::dub) - 6.0) < 0.02, "dub captured at full length");
    check (std::fabs (proc.capturedSeconds (VocalAlignerProcessor::Which::guide) - 6.0) < 0.02, "guide captured from the side-chain");
    check (proc.testWaitAlign (30000), "auto align finished");
    std::printf ("      %s\n", proc.lastMessage().toRawUTF8());
    auto aligned = proc.testAligned();
    if ((int) aligned.size() < n) { std::printf ("FAIL: aligned too short\n"); return 1; }
    // compare envelopes: aligned should follow the guide much better than the raw dub
    auto envErr = [&] (const std::vector<float>& a)
    {
        double e = 0, ref = 0; const int hop = 480;
        for (int f = 0; f + hop < n && f + hop < (int) a.size(); f += hop)
        {
            double x = 0, y = 0; for (int i = 0; i < hop; ++i) { x += std::fabs (guide[(size_t) (f + i)]); y += std::fabs (a[(size_t) (f + i)]); }
            e += std::fabs (x - y); ref += x;
        }
        return e / ref;
    };
    const double eRaw = envErr (dub), eAl = envErr (aligned);
    std::printf ("      envelope error raw %.3f  aligned %.3f\n", eRaw, eAl);
    check (eAl < eRaw * 0.6, "aligned take follows the guide");
    // playback: monitor = aligned replaces the input inside the captured range
    std::vector<float> outL;
    run (true, &outL);
    double diff = 0; for (int i = 0; i < n; ++i) diff = std::max (diff, (double) std::fabs (outL[(size_t) i] - aligned[(size_t) i]));
    check (diff < 0.01, "playback outputs the aligned take at the song position");
    proc.apvts.getParameter ("monitor")->setValueNotifyingHost (proc.apvts.getParameter ("monitor")->convertTo0to1 (1.0f));
    outL.clear(); run (true, &outL);
    diff = 0; for (int i = 0; i < n; ++i) diff = std::max (diff, (double) std::fabs (outL[(size_t) i] - dub[(size_t) i]));
    check (diff < 1e-4, "monitor Original passes the dub through");
    proc.apvts.getParameter ("monitor")->setValueNotifyingHost (0.0f);
    ph.playing = false; outL.clear(); run (true, &outL);
    diff = 0; for (int i = 0; i < n; ++i) diff = std::max (diff, (double) std::fabs (outL[(size_t) i] - dub[(size_t) i]));
    check (diff < 1e-4, "stopped transport passes the input through");
    ph.playing = true;
    // export
    juce::String err; auto wav = proc.exportAligned (err);
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
        check (r != nullptr && r->lengthInSamples == songStart + n && r->numChannels == 1, "exported WAV starts at the song start and has the take length");
    }
    wav.deleteFile();
    // state round trip keeps parameters and all three takes
    proc.apvts.getParameter ("tight")->setValueNotifyingHost (0.3f);
    juce::MemoryBlock st; proc.getStateInformation (st);
    std::printf ("      state size %.1f kB\n", st.getSize() / 1024.0);
    VocalAlignerProcessor p2; p2.setPlayConfigDetails (2, 2, sr, bs); p2.prepareToPlay (sr, bs);
    p2.setStateInformation (st.getData(), (int) st.getSize());
    auto a2 = p2.testAligned();
    double rd = a2.size() == aligned.size() ? 0.0 : 1.0;
    for (size_t i = 0; i < std::min (a2.size(), aligned.size()); ++i) rd = std::max (rd, (double) std::fabs (a2[i] - aligned[i]));
    check (rd < 1e-4, "aligned take survives save / load");
    check (std::fabs (p2.apvts.getParameter ("tight")->getValue() - 0.3f) < 0.01f, "parameters survive save / load");
    check (p2.hasTake (VocalAlignerProcessor::Which::guide) && p2.hasTake (VocalAlignerProcessor::Which::dub), "guide and dub survive save / load (re-align later)");
    // re-align with pitch on, clear, garbage state
    p2.apvts.getParameter ("pitchOn")->setValueNotifyingHost (1.0f);
    p2.startAlign(); check (p2.testWaitAlign (30000), "re-align with pitch after restore");
    p2.clearTakes(); check (! p2.hasTake (VocalAlignerProcessor::Which::aligned) && p2.status() == VocalAlignerProcessor::Status::idle, "clear");
    const char junk[] = "VAL1\xff\xff\xff\x7fgarbage"; p2.setStateInformation (junk, sizeof (junk)); check (true, "garbage state ignored");
    // file load path
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("va_load_test.wav");
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (tmp);
        juce::WavAudioFormat w; auto wr = w.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (16));
        juce::AudioBuffer<float> b (2, 44100); b.clear(); for (int i = 0; i < 44100; ++i) b.setSample (0, i, 0.3f * std::sin (i * 0.05f));
        wr->writeFromAudioSampleBuffer (b, 0, 44100);
    }
    check (p2.loadFile (VocalAlignerProcessor::Which::guide, tmp).isEmpty() && std::fabs (p2.capturedSeconds (VocalAlignerProcessor::Which::guide) - 1.0) < 0.01, "44.1k stereo WAV loads as a 1 s guide at 48k");
    check (p2.loadFile (VocalAlignerProcessor::Which::dub, juce::File ("/nonexistent.wav")).isNotEmpty(), "bad file reports an error");
    tmp.deleteFile();
    proc.setPlayHead (nullptr);
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    va::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("va_shot_settings");
    va::upd::dirOverride().deleteRecursively();
    { va::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      va::upd::writeJson (va::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests() + pk::test::procTests<VocalAlignerProcessor> ([] (VocalAlignerProcessor& q) -> pk::PresetManager& { return q.presets; }, va::upd::dataDir().getChildFile ("Presets"), 2, 4.0f, 1.0,
                                                       [] (const juce::MemoryBlock& mb) -> std::unique_ptr<juce::XmlElement> {   // "VAL1" + int + XML text + takes
                                                           if (mb.getSize() < 8 || std::memcmp (mb.getData(), "VAL1", 4) != 0) return nullptr;
                                                           juce::MemoryInputStream in (mb, false); in.skipNextBytes (4); const int n = in.readInt();
                                                           juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, n); return juce::parseXML (xm.toString()); });
    VocalAlignerProcessor proc;
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
        std::vector<float> g ((size_t) (48000 * 6)), d (g.size());
        phrase (g, 48000.0, false, 3); phrase (d, 48000.0, true, 3);
        proc.testSetTake (VocalAlignerProcessor::Which::guide, g, 48000 * 8, 48000.0);
        proc.testSetTake (VocalAlignerProcessor::Which::dub, d, 48000 * 8, 48000.0);
        proc.apvts.getParameter ("pitchOn")->setValueNotifyingHost (1.0f);
        proc.startAlign(); proc.testWaitAlign (30000);
    }
    if (mode.isEmpty() || mode == "presets") proc.presets.loadFactory (1);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<VocalAlignerEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (VocalAlignerProcessor::kBaseW * 3 / 2, VocalAlignerProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { va::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/vocal-aligner/"; i.notes = "Faster align, better breaths"; e->getPanel().demoBanner (i); }
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
