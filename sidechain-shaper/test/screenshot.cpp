// Renders the real SidechainShaperEditor to a PNG, and runs the processor-level tests.
// usage: SidechainShaperScreenshot out.png [banner|menu|presets|curvemenu|big|empty|keys|params|proc]
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

int presetIndex (const char* name) { auto& f = sc::factoryPresets(); for (size_t i = 0; i < f.size(); ++i) if (juce::String (f[i].name) == name) return (int) i; return 0; }

// processor-level: stereo + mono layouts, the sidechain bus, tempo sync from the play head, presets, curve state + undo, state round trip
int procTests()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double sr = 48000.0; const int bs = 480;
    struct Head final : juce::AudioPlayHead
    {
        double bpm = 120, ppq = 0; bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        { PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); p.setTimeSignature (TimeSignature { 4, 4 }); return p; }
    };
    // runs the main input x (and optionally a sidechain signal) through processBlock; the play head advances with the blocks
    auto runProc = [&] (SidechainShaperProcessor& proc, const std::vector<float>& x, int nch, const std::vector<float>* scIn = nullptr, Head* head = nullptr, double rate = 48000.0)
    {
        std::vector<float> out;
        const int total = proc.getTotalNumInputChannels() > nch ? proc.getTotalNumInputChannels() : nch;
        juce::AudioBuffer<float> buf (total, bs); juce::MidiBuffer none;
        if (head != nullptr) proc.setPlayHead (head);
        for (size_t b = 0; b + bs <= x.size(); b += bs)
        {
            buf.clear();
            for (int c = 0; c < nch; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, x[b + (size_t) i]);
            if (scIn != nullptr) for (int c = nch; c < total; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, (*scIn)[b + (size_t) i]);
            proc.processBlock (buf, none);
            for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
            if (head != nullptr) head->ppq += bs / rate * head->bpm / 60.0;
        }
        proc.setPlayHead (nullptr);
        return out;
    };
    auto set = [] (SidechainShaperProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto val = [] (SidechainShaperProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    auto envAt = [] (const std::vector<float>& y, size_t at, size_t w = 240) { double s = 0; for (size_t i = at; i < at + w && i < y.size(); ++i) s += y[i] * y[i]; return std::sqrt (s / (double) w); };
    const std::vector<float> dc (48000 * 4, 0.5f);           // steady input: the output is the gain curve itself
    const auto x = harm (230.0, (int) sr * 2);
    {
        SidechainShaperProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && proc.getTotalNumOutputChannels() == 2 && ! proc.acceptsMidi(), "stereo in/out, sidechain bus off by default, no MIDI");
        check (proc.getBusCount (true) == 2 && proc.getBus (true, 1)->getName() == "Sidechain", "a sidechain input bus is offered to the host");
        check (proc.getTailLengthSeconds() == 0.0 && proc.getLatencySamples() == 0, "zero latency, no tail");
        Head h; h.bpm = 120;
        auto y = runProc (proc, dc, 2, nullptr, &h);
        // Pump at 1/4, 120 BPM: a beat is 24000 samples; quiet at the downbeat, back up by the end of the beat
        const double lo = envAt (y, 24000 * 4 + 200), hi = envAt (y, 24000 * 4 + 20000);
        std::printf ("  pump: start of beat %.3f, end of beat %.3f\n", lo, hi);
        check (lo < 0.1 && hi > 0.4, "tempo mode: pumps in time with the host play head");
        h.bpm = 60; h.ppq = 0;
        auto y2 = runProc (proc, dc, 2, nullptr, &h);
        check (envAt (y2, 48000 * 2 + 24000) > 0.3 && envAt (y2, 48000 * 3 + 200) < 0.1, "follows a tempo change (60 BPM: one beat = 1 s)");
        check (proc.hostBpm.load() == 60.0f && proc.hostPlaying.load(), "host tempo and transport reach the display");
        check (proc.outLevel() > 0.001f, "output meter moves");
    }
    {   // the sidechain bus: kicks on the sidechain duck a steady main signal
        SidechainShaperProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        check (proc.setBusesLayout (l) && proc.getTotalNumInputChannels() == 4, "sidechain bus can be enabled (stereo)");
        auto lm = l; lm.inputBuses.getReference (1) = juce::AudioChannelSet::mono();
        check (proc.checkBusesLayoutSupported (lm), "mono sidechain supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mode", 1); set (proc, "rate", 2); set (proc, "threshold", -24.0f);
        std::vector<float> kicks (dc.size(), 0.0f);
        for (size_t k = 12000; k < kicks.size(); k += 21000) for (size_t i = 0; i < 2400 && k + i < kicks.size(); ++i) kicks[k + i] = 0.8f * (float) std::sin (2 * 3.14159265 * 55.0 * (double) i / sr) * (float) std::exp (-(double) i / 900.0);
        auto y = runProc (proc, dc, 2, &kicks);
        double dip = 1; for (size_t i = 12000 + 21000 * 3; i < 12000 + 21000 * 3 + 2400; i += 60) dip = std::min (dip, envAt (y, i, 120));
        const double before = envAt (y, 12000 + 21000 * 3 - 600);
        std::printf ("  sidechain: before kick %.3f, after kick %.3f, triggers %d\n", before, dip, proc.dspEngine().triggers.load());
        check (dip < 0.1 && before > 0.35, "a kick on the sidechain ducks the main signal");
        check (proc.dspEngine().usingExternal.load() == 1 && proc.dspEngine().triggers.load() >= 5, "external sidechain used, one trigger per kick");
        auto quiet = runProc (proc, dc, 2, nullptr);   // sidechain connected but silent: no triggers, signal passes
        check (envAt (quiet, 96000) > 0.45, "silent sidechain: no ducking");
    }
    {   // mono layout
        SidechainShaperProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        auto y = runProc (proc, x, 1); bool fin = true; for (float v : y) fin = fin && std::isfinite (v);
        check (fin && rmsOf (y, 24000) > 0.02, "mono processing works");
        auto l2 = l; l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        check (! proc.checkBusesLayoutSupported (l2), "mono in / stereo out refused");
    }
    {   // mix 0 = dry, depth 0 = dry
        SidechainShaperProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "mix", 0.0f);
        auto y = runProc (proc, x, 2); double d = 0; for (size_t i = 4800; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i]));
        check (d < 1e-4, "mix 0 = dry signal");
        set (proc, "mix", 1.0f); set (proc, "depth", 0.0f);
        y = runProc (proc, x, 2); d = 0; for (size_t i = 4800; i < y.size(); ++i) d = std::max (d, (double) std::fabs (y[i] - x[i]));
        check (d < 1e-4, "depth 0 = dry signal");
    }
    {   // every factory preset: loads, stays finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; juce::String names;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            SidechainShaperProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            const auto xin = harm (196.0, (int) rate * 3, rate);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i);
                proc.reset();
                Head h;
                auto y = runProc (proc, xin, 2, nullptr, &h, rate);
                float pk = 0; bool fin = true; for (float v : y) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
                if (! fin || pk > 4.0f) { ++bad; names << sc::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (y, y.size() / 2) < 0.01) { ++silent; names << sc::factoryPresets()[(size_t) i].name << " silent; "; }
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent %s\n", SidechainShaperProcessor().presets.numFactory(), bad, silent, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak < 4 at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset passes sound");
        check (SidechainShaperProcessor().presets.numFactory() >= 12, ">= 12 factory presets");
        SidechainShaperProcessor p;
        p.presets.loadFactory (presetIndex ("Trance Gate"));
        const auto tg = p.curve();
        p.presets.loadFactory (presetIndex ("Riser Bar"));
        check (tg.size() > 10 && sc::dsp::evalCurve (p.curve(), 0.05f) < 0.2f && sc::dsp::evalCurve (p.curve(), 0.95f) > 0.8f, "presets carry their curve shapes");
        p.presets.loadFactory (0);
        check (sc::dsp::formatCurve (p.curve()) == sc::dsp::formatCurve (sc::dsp::defaultCurve()), "Init resets the curve");
    }
    {   // user preset round trip, including a hand-drawn curve
        SidechainShaperProcessor a; set (a, "depth", 0.42f); set (a, "rate", 9); set (a, "mode", 1);
        const sc::dsp::Curve drawn { { 0, 1, 0 }, { 0.2f, 0.1f, 0.5f }, { 0.55f, 0.7f, -0.3f }, { 1, 0.3f, 0 } };
        a.setCurve (drawn, true);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == sc::PresetManager::userDir(), "user preset saved in the user preset folder");
        SidechainShaperProcessor b;
        check (b.presets.loadFile (f) && std::fabs (val (b, "depth") - 0.42f) < 0.002f && val (b, "rate") == 9 && val (b, "mode") == 1 && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        check (b.curveText() == a.curveText() && b.curve().size() == 4, "the drawn curve travels with the user preset");
        f.deleteFile();
    }
    {   // curve edits are undoable; the engine follows the state
        SidechainShaperProcessor a; a.setRateAndBufferSizeDetails (sr, bs); a.prepareToPlay (sr, bs);
        a.undoManager.clearUndoHistory();
        const auto before = a.curveText();
        a.setCurve (sc::dsp::shapeByName ("Flat"), true);
        auto y = runProc (a, dc, 2);
        check (envAt (y, 48000) > 0.49, "the engine uses the new curve (Flat = no change)");
        a.undoManager.undo();
        check (a.curveText() == before, "curve change undone");
        y = runProc (a, dc, 2);
        double mn = 1; for (size_t i = 48000; i + 240 < y.size(); i += 240) mn = std::min (mn, envAt (y, i));
        check (mn < 0.1, "undo puts the pumping curve back into the engine");
        const auto txt = a.curveText();
        a.commitCurve (txt, sc::dsp::shapeByName ("Ramp Up"));
        a.undoManager.undo();
        check (a.curveText() == txt, "a finished drag is one undo step");
    }
    {   // state
        SidechainShaperProcessor a; a.uiWidth = 1080; a.uiHeight = 672;
        a.presets.loadFactory (5); set (a, "depth", 0.66f); set (a, "rate", 12); set (a, "focus", 150.0f);
        a.setCurve (sc::dsp::shapeByName ("Swell"), false);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        SidechainShaperProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (val (b, "depth") - 0.66f) < 0.002f && val (b, "rate") == 12 && std::fabs (val (b, "focus") - 150.0f) < 1.0f, "parameters survive save / load");
        check (b.curveText() == a.curveText(), "the curve survives save / load");
        check (b.presets.currentName() == sc::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1080 && b.uiHeight == 672, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (val (b, "rate") == 12 && b.curveText() == a.curveText(), "garbage state ignored");
        juce::XmlElement old ("STATE");   // a state without a curve (e.g. hand-edited) gets the default
        juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (old, mb2);
        b.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check (b.curve().size() >= 2, "state without a curve falls back to the default curve");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    sc::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sidechain-shaper_shot_settings");
    sc::upd::dirOverride().deleteRecursively();
    { sc::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      sc::upd::writeJson (sc::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    SidechainShaperProcessor proc;
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
    if (mode.isEmpty() || mode == "big" || mode == "curvemenu") proc.presets.loadFactory (presetIndex ("Hard EDM Pump"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<SidechainShaperEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (SidechainShaperProcessor::kBaseW * 3 / 2, SidechainShaperProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { sc::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/sidechain-shaper/"; i.notes = "New curve shapes"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto p = e->getPanel().auditPresets();  std::printf ("%s", p.toRawUTF8());
        auto cv = scui::auditCurve (e->getPanel().display(), proc); std::printf ("%s", cv.toRawUTF8());
        auto items = e->getPanel().popupOverlay().itemTexts();
        const bool menuOk = items.size() >= 25 && e->getPanel().popupOverlay().activate ("&Ramp Up") && sc::dsp::evalCurve (proc.curve(), 0.95f) > 0.8f
                            && proc.undoManager.canUndo();
        std::printf ("%s curve menu: %d items, a shape loads from it and can be undone\n", menuOk ? "  ok  " : "  FAIL", items.size());
        if (! menuOk) cv = "CURVE AUDIT FAILED";
        proc.undoManager.clearUndoHistory(); proc.presets.loadFactory (4);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        const bool undoOk = proc.undoManager.canUndo();
        std::printf ("%s a preset load can be undone\n", undoOk ? "  ok  " : "  FAIL");
        if (! undoOk) p = "PRESET AUDIT FAILED";
        return (r.contains ("KEYBOARD AUDIT OK") && m.contains ("MENU AUDIT OK") && p.contains ("PRESET AUDIT OK") && cv.contains ("CURVE AUDIT OK")) ? 0 : 1;
    }
    if (mode != "empty") scui::demoDisplay (e->getPanel().display());
    if (mode == "menu") e->getPanel().demoKnobMenu();
    if (mode == "presets") e->getPanel().demoPresetMenu();
    if (mode == "curvemenu") scui::demoCurveMenu (e->getPanel().display());
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
