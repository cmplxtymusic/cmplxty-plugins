// Renders the real VvitchEditor to a PNG, and runs the processor-level tests.
// usage: VvitchScreenshot out.png [banner|menu|presets|big|empty|keys|params|proc]
#include "PluginEditor.h"
#include <random>
#include <cmath>
#include <cstring>

// 1.2.0: counts heap allocations made on a thread while it is flagged (the audio-thread check for the macro map)
static thread_local bool gCountAllocs = false;
static std::atomic<long> gAllocs { 0 };
void* operator new (std::size_t n) { if (gCountAllocs) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCountAllocs) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

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
    auto render = [&] (VvitchProcessor& proc, int blocks, std::vector<std::pair<int, juce::MidiMessage>> ev, int nch = 2, std::vector<float>* right = nullptr)
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
    auto set = [] (VvitchProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto v = [] (VvitchProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); };
    auto on = [] (int n, float vel = 0.8f) { return juce::MidiMessage::noteOn (1, n, vel); };
    auto off = [] (int n) { return juce::MidiMessage::noteOff (1, n); };
    auto freq = [&] (const std::vector<float>& x, size_t a, size_t b)
    {
        int n = 0; double first = -1, last = 0;
        for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
        return n > 1 ? (n - 1) * sr / (last - first) : 0.0;
    };
    auto sine = [&] (VvitchProcessor& p) { set (p, "w1", 3); set (p, "lvl2", 0.0f); set (p, "uni", 1); set (p, "cutoff", 1200.0f); set (p, "reso", 0.0f); set (p, "drive", 0.0f);
                                           set (p, "fEnv", 0.0f); set (p, "fKey", 0.0f); set (p, "width", 0.0f); set (p, "fType", 0); set (p, "revMix", 0.0f); set (p, "dlyMix", 0.0f);
                                           set (p, "ampS", 1.0f); set (p, "ampR", 50.0f); set (p, "volume", 0.0f); };
    {
        VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 0 && proc.getTotalNumOutputChannels() == 2 && proc.acceptsMidi() && proc.getLatencySamples() == 0, "synth: no audio in, stereo out, MIDI in, zero latency");
        check (proc.getParameters().size() == 83 && v (proc, "voices") == 8.0f && std::fabs (v (proc, "cutoff") - 3000.0f) < 1.0f, "83 parameters with defaults (8 voices, 3 kHz cutoff)");
        check (proc.apvts.getParameter ("uni")->getText (proc.apvts.getParameter ("uni")->convertTo0to1 (5.0f), 20) == "5"
               && proc.apvts.getParameter ("w1")->getText (proc.apvts.getParameter ("w1")->convertTo0to1 (2.0f), 20) == "PWM", "unison shows whole numbers, wave names shown");
        auto y = render (proc, 20, {});
        check (rmsOf (y, 0) == 0.0, "no MIDI = silence");
        sine (proc);
        y = render (proc, 100, { { 0, on (69) }, { 24000, off (69) } });
        const double f = freq (y, 2400, 24000);
        std::printf ("  A4 through the processor: %.2f Hz, out meter %.2f\n", f, proc.outLevel());
        check (std::fabs (f - 440.0) < 0.3, "MIDI note 69 plays 440 Hz");
        check (rmsOf (y, 40000) < 1e-5, "note off releases to silence");
        // sample-accurate start: note at sample 1000 inside block 2
        VvitchProcessor p2; p2.setRateAndBufferSizeDetails (sr, bs); p2.prepareToPlay (sr, bs); sine (p2); set (p2, "ampA", 1.0f);
        auto z = render (p2, 4, { { 1000, on (69, 1.0f) } });
        double before = 0, after = 0; for (size_t k = 0; k < 1000; ++k) before += std::fabs (z[k]); for (size_t k = 1000; k < 1200; ++k) after += std::fabs (z[k]);
        check (before == 0.0 && after > 1.0, "notes start on their sample, not the block boundary");
    }
    {   // controllers: pitch bend, mod wheel (via the mod matrix), sustain, all-notes-off
        VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs); sine (proc);
        auto y = render (proc, 50, { { 0, juce::MidiMessage::pitchWheel (1, 16383) }, { 0, on (69) } });
        check (std::fabs (freq (y, 4800, 24000) - 440.0 * std::pow (2.0, 2.0 / 12.0)) < 0.6, "pitch bend +2 semitones");
        y = render (proc, 50, { { 0, juce::MidiMessage::pitchWheel (1, 8192) }, { 0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
        {   // wheel = vibrato: the pitch now wanders around 440 Hz
            double lo = 1e9, hi = 0; for (size_t a = 4800; a + 2400 <= 24000; a += 1200) { const double f2 = freq (y, a, a + 2400); lo = std::min (lo, f2); hi = std::max (hi, f2); }
            std::printf ("  wheel vibrato range %.1f .. %.1f Hz\n", lo, hi);
            check (hi - lo > 3.0 && lo > 400 && hi < 480, "mod wheel (CC1) adds vibrato");
        }
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
        VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs); set (proc, "voices", 3);
        render (proc, 10, { { 0, on (60) }, { 0, on (62) }, { 0, on (64) }, { 0, on (65) }, { 0, on (67) } });
        check (proc.dspEngine().activeVoices.load() == 3, "voice count parameter caps polyphony");
        VvitchProcessor q; q.setRateAndBufferSizeDetails (sr, bs); q.prepareToPlay (sr, bs); set (q, "revMix", 0.0f); set (q, "dlyMix", 0.0f);
        q.queueNote (60, 0.9f); auto y = render (q, 10, {});
        check (rmsOf (y, 0) > 0.01, "notes queued from the editor play");
        q.queueNote (60, 0.0f); y = render (q, 60, {});
        check (rmsOf (y, 24000) < 1e-5, "and release");
    }
    {   // mono layout
        VvitchProcessor proc;
        auto l = proc.getBusesLayout(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono output supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        set (proc, "uni", 7); set (proc, "width", 1.0f);
        auto y = render (proc, 50, { { 0, on (57) } }, 1); bool fin = true; for (float s : y) fin = fin && std::isfinite (s);
        check (fin && rmsOf (y, 4800) > 0.02, "mono output renders");
    }
    {   // every factory preset: plays a chord, stays finite and bounded, at 44.1 / 48 / 96 kHz
        int bad = 0, silent = 0; float worst = 0; juce::String names;
        {   // every value in the preset table names a real parameter and sits inside its range
            VvitchProcessor proc; int badIds = 0;
            for (auto& fp : vv::factoryPresets())
                for (auto& [id, val] : fp.values)
                    if (auto* q = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id)); q == nullptr || val < q->getNormalisableRange().start || val > q->getNormalisableRange().end)
                    { ++badIds; std::printf ("  bad preset value %s / %s = %g\n", fp.name, id, (double) val); }
            check (badIds == 0, "every preset value names a parameter and is in range");
            juce::StringArray cats; for (auto& fp : vv::factoryPresets()) cats.addIfNotAlreadyThere (fp.category);
            int perCat[6] {}; const char* need[] = { "Leads", "Pads", "Bass", "Vox", "FX", "Keys" };
            for (auto& fp : vv::factoryPresets()) for (int c = 0; c < 6; ++c) if (juce::String (fp.category) == need[c]) ++perCat[c];
            bool allCats = true; for (int c : perCat) allCats = allCats && c >= 6;
            check (allCats, "categories Leads / Pads / Bass / Vox / FX / Keys, 6+ presets each");
        }
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            VvitchProcessor proc; proc.setRateAndBufferSizeDetails (rate, bs); proc.prepareToPlay (rate, bs);
            for (int i = 0; i < proc.presets.numFactory(); ++i)
            {
                proc.presets.loadFactory (i);
                proc.reset();
                const int blocks = (int) (rate * 3 / bs);
                std::vector<float> R;
                auto y = render (proc, blocks, { { 0, on (48, 0.9f) }, { 0, on (55, 0.7f) }, { 0, on (63, 0.8f) }, { blocks * bs / 2, juce::MidiMessage::controllerEvent (1, 1, 100) },
                                                 { blocks * bs / 3, juce::MidiMessage::pitchWheel (1, 12000) }, { blocks * bs / 2, juce::MidiMessage::channelPressureChange (1, 90) } }, 2, &R);
                float pk = 0; bool fin = true; for (size_t k = 0; k < y.size(); ++k) { fin = fin && std::isfinite (y[k]) && std::isfinite (R[k]); pk = std::max ({ pk, std::fabs (y[k]), std::fabs (R[k]) }); }
                worst = std::max (worst, pk);
                if (! fin || pk > 0.95f) { ++bad; names << vv::factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
                if (rmsOf (y, 0) < 0.01) { ++silent; names << vv::factoryPresets()[(size_t) i].name << " silent @" << (int) rate << " rms=" << rmsOf (y, 0) << "; "; }
                render (proc, 2, { { 0, juce::MidiMessage::allSoundOff (1) } });
            }
        }
        std::printf ("  %d factory presets x 3 rates: %d bad, %d silent, loudest peak %.3f %s\n", VvitchProcessor().presets.numFactory(), bad, silent, (double) worst, names.toRawUTF8());
        check (bad == 0, "every factory preset: no NaN / inf, peak <= 0.95 (no clipping) at 44.1 / 48 / 96 kHz");
        check (silent == 0, "every factory preset produces sound");
        check (VvitchProcessor().presets.numFactory() >= 41, ">= 40 factory presets (+ Init)");
    }
    {   // user preset round trip through a file in the (test) user folder
        VvitchProcessor a; set (a, "w2", 2); set (a, "cutoff", 777.0f); set (a, "arpDiv", 5);
        const auto f = a.presets.userFileFor ("Round Trip");
        check (a.presets.saveFile (f, "Round Trip") && f.getParentDirectory() == vv::PresetManager::userDir(), "user preset saved in the user preset folder");
        VvitchProcessor b;
        check (b.presets.loadFile (f) && v (b, "w2") == 2 && std::fabs (v (b, "cutoff") - 777.0f) < 1.0f && v (b, "arpDiv") == 5 && b.presets.currentName() == "Round Trip", "user preset loads back into another instance");
        f.deleteFile();
    }
    {   // state
        VvitchProcessor a; a.uiWidth = 1320; a.uiHeight = 888;
        a.presets.loadFactory (5); set (a, "ampR", 420.0f); set (a, "fType", 3); set (a, "volume", -3.0f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        VvitchProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::fabs (v (b, "ampR") - 420.0f) < 0.5f && v (b, "fType") == 3 && std::fabs (v (b, "volume") + 3.0f) < 0.05f, "parameters survive save / load");
        check (b.presets.currentName() == vv::factoryPresets()[5].name, "preset name survives save / load");
        check (b.uiWidth == 1320 && b.uiHeight == 888, "window size survives save / load");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check (v (b, "fType") == 3, "garbage state ignored");
    }
    {   // 1.1.0: macros move their targets on EVERY factory preset, without touching stored values
        VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        int moved = 0, n = proc.presets.numFactory();
        for (int i = 0; i < n; ++i)
        {
            proc.presets.loadFactory (i);
            const auto a = proc.currentParams(); const float storedCut = v (proc, "cutoff");
            auto sameP = [] (const vv::dsp::Params& a, const vv::dsp::Params& b)
            {
                for (size_t k = 0; k < a.fx.size(); ++k) if (a.fx[k].type != b.fx[k].type || a.fx[k].a != b.fx[k].a || a.fx[k].b != b.fx[k].b || a.fx[k].mix != b.fx[k].mix) return false;
                return a.w1 == b.w1 && a.w2 == b.w2 && a.oct1 == b.oct1 && a.semi2 == b.semi2 && a.fine2 == b.fine2 && a.lvl1 == b.lvl1 && a.lvl2 == b.lvl2 && a.pw == b.pw && a.pwm == b.pwm && a.pwmRate == b.pwmRate && a.uni == b.uni && a.detune == b.detune && a.width == b.width && a.noise == b.noise && a.noiseTone == b.noiseTone && a.vox == b.vox && a.vowel == b.vowel && a.voxMove == b.voxMove && a.fType == b.fType && a.cutoff == b.cutoff && a.reso == b.reso && a.drive == b.drive && a.fEnv == b.fEnv && a.fKey == b.fKey && a.fA == b.fA && a.fD == b.fD && a.fS == b.fS && a.aA == b.aA && a.aD == b.aD && a.aS == b.aS && a.aR == b.aR && a.drop == b.drop && a.dropTime == b.dropTime && a.drag == b.drag && a.dragAmt == b.dragAmt && a.wow == b.wow && a.flutter == b.flutter && a.age == b.age && a.bits == b.bits && a.crushHz == b.crushHz && a.chipMix == b.chipMix && a.lfoShape == b.lfoShape && a.lfoRate == b.lfoRate && a.lfoCut == b.lfoCut && a.lfoPitch == b.lfoPitch && a.arp == b.arp && a.arpMode == b.arpMode && a.arpDiv == b.arpDiv && a.arpOct == b.arpOct && a.arpGate == b.arpGate && a.voices == b.voices && a.glide == b.glide && a.bpm == b.bpm && a.dlyTime == b.dlyTime && a.dlyFb == b.dlyFb && a.dlyMix == b.dlyMix && a.revSize == b.revSize && a.revMix == b.revMix && a.revDark == b.revDark && a.volumeDb == b.volumeDb && a.bendRange == b.bendRange;
            };
            // 1.3.0: every preset has its own eight macros; each one at full must move the engine, the stored knobs never change
            int ok8 = 0;
            for (int m = 0; m < VvitchProcessor::kMacros; ++m)
            {
                const auto t = proc.macroTargets (m);
                set (proc, ("macro" + juce::String (m + 1)).toRawUTF8(), 1.0f);
                const auto b = proc.currentParams();
                const bool movedAny = ! sameP (a, b);
                set (proc, ("macro" + juce::String (m + 1)).toRawUTF8(), 0.0f);
                const auto c = proc.currentParams();
                ok8 += (movedAny && t.size() >= 2 && t.size() <= 6 && sameP (a, c)) ? 1 : 0;
            }
            if (ok8 == VvitchProcessor::kMacros && std::fabs (v (proc, "cutoff") - storedCut) < 1e-3f) ++moved;
        }
        check (moved == n, "every factory preset: all 8 macros (2-6 targets each) move the sound at full and return exactly at 0, stored values unchanged");
        proc.presets.loadFactory (1);
        check (v (proc, "macro1") == 0.0f, "loading a preset resets the macro knobs");
        proc.setMacroMapString ({});   // the 1.1.x stock map
        set (proc, "macro5", 1.0f); check (std::fabs (proc.currentParams().drop - (v (proc, "drop") - 12.0f)) < 0.2f, "stock macro DROP: -12 st at full");
        set (proc, "macro5", 0.0f);
        // names: editable, in the session state, in user presets; factory presets restore the defaults
        const auto initSpec = juce::StringArray::fromTokens (vv::factoryMacroSpec ("Init"), "|", "");
        auto specName = [] (const char* preset, int m) { return juce::StringArray::fromTokens (vv::factoryMacroSpec (preset), "|", "")[m].upToFirstOccurrenceOf (">", false, false); };
        check (initSpec.size() == 8 && proc.macroName (0) == specName (vv::factoryPresets()[1].name, 0) && proc.macroName (7) == specName (vv::factoryPresets()[1].name, 7), ("preset 1 brings its own macro names (" + proc.macroName (0) + " ... " + proc.macroName (7) + ")").toRawUTF8());
        { VvitchProcessor fresh; check (fresh.macroName (0) == specName ("Init", 0) && fresh.macroName (7) == specName ("Init", 7), ("a new instance has Init's own macro names (" + fresh.macroName (0) + " ... " + fresh.macroName (7) + ")").toRawUTF8()); }
        check (juce::String (VvitchProcessor::defaultMacroName (0)) == "BRIGHT" && juce::String (VvitchProcessor::defaultMacroName (7)) == "FX", "the 1.1.x stock names stay the fallback for older sessions / presets");
        proc.setMacroName (0, "Shine|x"); check (proc.macroName (0) == "Shinex", "rename a macro ('|' stripped)");
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        VvitchProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (p2.macroName (0) == "Shinex" && p2.macroName (1) == specName (vv::factoryPresets()[1].name, 1), "macro names survive the session state");
        auto f = juce::File::createTempFile (".cmplxtypreset");
        proc.presets.saveFile (f, "macro test"); proc.presets.loadFactory (2);
        check (proc.macroName (0) == specName (vv::factoryPresets()[2].name, 0) && proc.macroName (0).isNotEmpty() && proc.macroName (0) != "Shinex", ("a factory preset brings its own macro names (" + proc.macroName (0) + ")").toRawUTF8());
        proc.presets.loadFile (f); check (proc.macroName (0) == "Shinex", "a user preset carries its macro names");
        f.deleteFile();
    }
#ifdef VV_HAS_MACRO_MAP
    {   // 1.2.0: macro assignment
        VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        auto ids = [&] (int m) { juce::StringArray a; for (auto& t : proc.macroTargets (m)) a.add (t.id + ":" + juce::String (t.amount, 2)); return a.joinIntoString (","); };
        bool allDefault = true; for (int m = 0; m < VvitchProcessor::kMacros; ++m) allDefault &= proc.macroIsDefault (m);
        check (allDefault && proc.presetMacroSpec() == juce::String (vv::factoryMacroSpec ("Init")), "a new instance starts on Init's own macro targets");
        proc.setMacroMapString ({});
        check (ids (0) == "cutoff:0.35,reso:0.20" && ids (3) == "chipMix:0.80,bits:-0.60,crushHz:-0.50" && ids (7) == "fx1Mix:0.60,fx2Mix:0.60,fx3Mix:0.60,fx4Mix:0.60",
               "an empty map = the 1.1.x stock targets (BRIGHT, CRUSH, FX ...)");
        // reassign: M2 (SPACE) -> cutoff -100 %, reso +40 %, plus up to 8 targets
        proc.presets.loadFactory (1);
        proc.clearMacro (1);
        check (proc.assignMacro (1, "cutoff", -1.0f) && proc.assignMacro (1, "reso", 0.4f), "assign two targets to M2");
        const auto a0 = proc.currentParams(); set (proc, "macro2", 1.0f); const auto a1 = proc.currentParams();
        check (a1.cutoff < a0.cutoff * 0.05f && a1.reso > a0.reso + 0.35f && std::fabs (a1.revMix - a0.revMix) < 1e-6f, "M2 now closes the cutoff and opens the resonance; the reverb (old target) no longer moves");
        set (proc, "macro2", 0.5f); const auto ah = proc.currentParams();
        check (ah.cutoff < a0.cutoff && ah.cutoff > a1.cutoff, "half way = in between");
        set (proc, "macro2", 0.0f);
        check (! proc.assignMacro (1, "w1", 0.5f) && ! proc.assignMacro (1, "macro3", 0.5f) && ! proc.assignMacro (1, "nope", 0.5f), "combo / switch / macro / unknown ids can't be targets");
        const char* more[] = { "drive", "noise", "vox", "width", "detune", "pwm" }; int added = 0;
        for (auto* id : more) added += proc.assignMacro (1, id, 0.25f) ? 1 : 0;
        check (added == 6 && (int) proc.macroTargets (1).size() == VvitchProcessor::kMaxTargets && ! proc.assignMacro (1, "glide", 0.1f), "up to 8 targets per macro (the 9th is refused)");
        proc.assignMacro (1, "reso", 2.5f); check (std::fabs (proc.macroAmount (1, "reso") - 1.0f) < 1e-6f, "amounts are clamped to -100..+100 %");
        proc.removeMacroTarget (1, "drive"); check ((int) proc.macroTargets (1).size() == 7 && proc.macroAmount (1, "drive") == 0.0f, "remove one target");
        // the same knob on two macros adds up
        proc.assignMacro (0, "reso", 0.3f);
        set (proc, "macro1", 1.0f); set (proc, "macro2", 1.0f); const auto both = proc.currentParams(); set (proc, "macro1", 0.0f); set (proc, "macro2", 0.0f);
        check (both.reso >= 0.999f, "two macros on one knob add up (clamped to the knob's range)");
        // session state
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        VvitchProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        check (p2.macroMapString() == proc.macroMapString() && ! p2.macroIsDefault (1) && std::fabs (p2.macroAmount (1, "cutoff") + 1.0f) < 1e-6f, "the macro map survives the session state");
        // user preset carries it; factory preset restores the stock map; a 1.1.x user preset (no map) gets the stock map
        auto f = juce::File::createTempFile (".cmplxtypreset");
        proc.presets.saveFile (f, "map test");
        proc.presets.loadFactory (3); bool stock = true; for (int m = 0; m < VvitchProcessor::kMacros; ++m) stock &= proc.macroIsDefault (m);
        check (stock && proc.presetMacroSpec().isNotEmpty() && proc.macroTargets (0).size() >= 2 && ids (0) != "cutoff:0.35,reso:0.20", ("a factory preset loads with its own macro targets (M1 " + ids (0) + ")").toRawUTF8());
        proc.clearMacro (2); check (! proc.macroIsDefault (2), "edited macro is no longer the preset's");
        proc.restoreDefaultMacro (2); check (proc.macroIsDefault (2) && proc.macroTargets (2).size() >= 2, "restore brings back the preset's own targets");
        {   // a session saved on a factory preset keeps its saved map, and still knows the preset for 'restore'
            proc.clearMacro (4); juce::MemoryBlock ms; proc.getStateInformation (ms);
            VvitchProcessor p4; p4.setStateInformation (ms.getData(), (int) ms.getSize());
            check (p4.macroTargets (4).empty() && p4.presetMacroSpec() == proc.presetMacroSpec(), "a session keeps its own map (edits included) and its preset's targets for restore");
            proc.restoreDefaultMacro (4);
        }
        proc.presets.loadFile (f); check (std::fabs (proc.macroAmount (1, "cutoff") + 1.0f) < 1e-6f && (int) proc.macroTargets (1).size() == 7, "a user preset carries its macro map");
        auto x = juce::XmlDocument::parse (f); x->getChildByName ("EXTRA")->removeAttribute ("macroMap"); x->writeTo (f);
        proc.presets.loadFile (f); stock = true; for (int m = 0; m < VvitchProcessor::kMacros; ++m) stock &= proc.macroIsDefault (m);
        check (stock, "a 1.1.x user preset (no map) loads with the stock macro targets");
        f.deleteFile();
        {   // a 1.1.x session (no map in the state) -> stock map
            VvitchProcessor p3; p3.clearMacro (0);
            juce::MemoryBlock m3; p3.getStateInformation (m3);
            auto xml = juce::AudioProcessor::getXmlFromBinary (m3.getData(), (int) m3.getSize()); xml->removeAttribute ("macroMap");
            juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
            p3.setStateInformation (old.getData(), (int) old.getSize());
            juce::StringArray a0; for (auto& t : p3.macroTargets (0)) a0.add (t.id + ":" + juce::String (t.amount, 2));
            check (a0.joinIntoString (",") == "cutoff:0.35,reso:0.20" && p3.macroName (0) == "BRIGHT", "a 1.1.x session (no map, no names) gets the stock macro targets and names, so it sounds as before");
        }
        // audio thread: no allocation while macros move and the map is edited between blocks
        proc.presets.loadFactory (10);
        juce::AudioBuffer<float> buf (2, bs); long allocs = 0;
        proc.queueNote (48, 0.9f); proc.queueNote (55, 0.8f);
        for (int b = 0; b < 2000; ++b)
        {
            if (b % 50 == 0) { proc.assignMacro (b / 50 % 8, "cutoff", (float) ((b / 50) % 21 - 10) / 10.0f); }   // edits on this (message) thread, between blocks
            set (proc, "macro1", (float) (b % 100) / 100.0f); set (proc, "macro5", (float) ((b * 7) % 100) / 100.0f);
            juce::MidiBuffer m; buf.clear();
            gAllocs = 0; gCountAllocs = true; proc.processBlock (buf, m); gCountAllocs = false; allocs += gAllocs.load();
        }
        std::printf ("  heap allocations inside processBlock over 2000 blocks with moving macros + map edits: %ld\n", allocs);
        check (allocs == 0, "processBlock never allocates (macro map read from atomics)");
    }
#endif
    {   // 1.1.0: FX RACK: off = bit-identical bypass; every type renders finite, non-silent audio; mix 0 = bypass
        auto run = [&] (int type, float mix)
        {
            VvitchProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
            set (proc, "revMix", 0.0f); set (proc, "dlyMix", 0.0f);
            if (type > 0) { set (proc, "fx1Type", (float) type); set (proc, "fx1Mix", mix); set (proc, "fx1A", 0.7f); set (proc, "fx1B", 0.7f); }
            return render (proc, 100, { { 0, on (48) }, { 30000, off (48) } });
        };
        const auto dry = run (0, 0);
        bool ok = true; juce::String bad;
        for (int t = 1; t < vv::fx::kNumTypes; ++t)
        {
            const auto w = run (t, 1.0f); bool fin = true; double d = 0;
            for (size_t i = 0; i < w.size(); ++i) { fin &= std::isfinite (w[i]) && std::fabs (w[i]) < 4.0f; d = std::max (d, (double) std::fabs (w[i] - dry[i])); }
            if (! fin || rmsOf (w, 2000) < 1e-3 || d < 1e-4) { ok = false; bad << vv::fx::typeName (t) << " "; }
            const auto z = run (t, 0.0f); double dz = 0; for (size_t i = 0; i < z.size(); ++i) dz = std::max (dz, (double) std::fabs (z[i] - dry[i]));
            if (dz > 1e-6) { ok = false; bad << vv::fx::typeName (t) << "(mix0) "; }
        }
        std::printf ("  fx types bad: %s\n", bad.toRawUTF8());
        check (ok, "FX RACK: all 7 types change the sound, stay finite; mix 0 is a bypass");
        int withFx = 0; VvitchProcessor proc;
        for (int i = 0; i < proc.presets.numFactory(); ++i) { proc.presets.loadFactory (i); withFx += proc.currentParams().fx[0].type > 0 ? 1 : 0; }
        check (withFx >= 16 && proc.presets.numFactory() >= 65, ">= 64 factory presets, >= 16 of them use the FX rack");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png", mode = argc > 2 ? argv[2] : "";
    vv::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vvitch_shot_settings");
    vv::upd::dirOverride().deleteRecursively();
    { vv::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      vv::upd::writeJson (vv::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    VvitchProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512); proc.prepareToPlay (48000.0, 512);
    if (mode == "render")   // every factory preset: a chord, macros all at argv[3] (0 = default), raw float32 stereo into folder `out`
    {
        const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (out)); dir.createDirectory();
        const float mv = argc > 3 ? (float) std::atof (argv[3]) : 0.0f;
        const int n = proc.presets.numFactory(); double sec = 0;
        for (int i = 0; i < n; ++i)
        {
            VvitchProcessor p; p.setPlayConfigDetails (0, 2, 48000.0, 480); p.prepareToPlay (48000.0, 480); p.presets.loadFactory (i);
            for (int m = 1; m <= VvitchProcessor::kMacros; ++m) if (auto* q = p.apvts.getParameter ("macro" + juce::String (m))) q->setValueNotifyingHost (mv);
            juce::MemoryOutputStream mo;
            juce::AudioBuffer<float> b (2, 480);
            for (int blk = 0; blk < 300; ++blk)   // 3 s: chord on at 0, off at 2 s
            {
                juce::MidiBuffer m;
                if (blk == 0) for (int nt : { 48, 55, 63 }) m.addEvent (juce::MidiMessage::noteOn (1, nt, 0.85f), 7);
                if (blk == 200) for (int nt : { 48, 55, 63 }) m.addEvent (juce::MidiMessage::noteOff (1, nt), 0);
                b.clear(); const auto t0 = juce::Time::getHighResolutionTicks(); p.processBlock (b, m); sec += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                for (int k = 0; k < 480; ++k) { mo.writeFloat (b.getSample (0, k)); mo.writeFloat (b.getSample (1, k)); }
            }
            dir.getChildFile ("p" + juce::String (i).paddedLeft ('0', 2) + ".f32").replaceWithData (mo.getData(), mo.getDataSize());
        }
        std::printf ("RENDERED %d presets, macros at %.2f; audio thread %.1f ms for %.0f s of audio (%.3f %% of one core)\n", n, mv, sec * 1000.0, n * 3.0, sec / (n * 3.0) * 100.0);
        return 0;
    }
#ifdef VV_HAS_MACRO_MAP
    if (mode == "sweep")   // a reassigned macro, automated 0 -> 1 over 8 s while a chord holds; raw float32 into folder `out`
    {
        const juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (out)); dir.createDirectory();
        struct Case { const char* tag; int preset; int macro; std::vector<std::pair<const char*, float>> targets; };
        const std::vector<Case> cases {
            { "init_M2_cutoff+100", 0, 1, { { "cutoff", 1.0f } } },
            { "init_M2_cleared",    0, 1, {} },
            { "init_M2_stock",      0, 1, { { "*", 0 } } },
            { "init_M3_cutoff-100_reso+60", 0, 2, { { "cutoff", -1.0f }, { "reso", 0.6f } } },
            { "init_M8_noise+80_detune+50_width+100_pwm+70", 0, 7, { { "noise", 0.8f }, { "detune", 0.5f }, { "width", 1.0f }, { "pwm", 0.7f } } },
        };
        for (auto& c : cases)
        {
            VvitchProcessor p; p.setPlayConfigDetails (0, 2, 48000.0, 480); p.prepareToPlay (48000.0, 480); p.presets.loadFactory (c.preset);
            if (c.targets.empty() || juce::String (c.targets[0].first) != "*") { p.clearMacro (c.macro); for (auto& t : c.targets) p.assignMacro (c.macro, t.first, t.second); }
            auto* mq = p.apvts.getParameter ("macro" + juce::String (c.macro + 1));
            juce::AudioBuffer<float> all (2, 48000 * 9); all.clear(); juce::AudioBuffer<float> b (2, 480);
            for (int blk = 0; blk < 900; ++blk)
            {
                juce::MidiBuffer m;
                if (blk == 0) for (int nt : { 48, 55, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, nt, 0.85f), 0);
                mq->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (blk - 50) / 800.0f));   // host automation: 0 for 0.5 s, then a ramp to 1 at 8.5 s
                b.clear(); p.processBlock (b, m);
                for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, blk * 480, b, ch, 0, 480);
            }
            juce::MemoryOutputStream mo;   // raw float32, stereo interleaved, 48 kHz (no audio-format module in this test app)
            for (int k = 0; k < all.getNumSamples(); ++k) { mo.writeFloat (all.getSample (0, k)); mo.writeFloat (all.getSample (1, k)); }
            dir.getChildFile (juce::String (c.tag) + ".f32").replaceWithData (mo.getData(), mo.getDataSize());
            std::printf ("  %s: map %s\n", c.tag, p.macroMapString().toRawUTF8());
        }
        return 0;
    }
#endif
#ifdef VV_HAS_PRESET_MACROS
    if (mode == "macrotest")   // 1.3.0: one factory preset (argv[3]), every macro: static renders at 0/.25/.5/.75/1 and a 0 -> 1 ramp; raw float32 to `out`
    {
        const int pi = argc > 3 ? std::atoi (argv[3]) : 0;
        juce::StringPairArray over;   // VV_SPEC_FILE: "name<TAB>spec" lines that override the compiled-in specs (tuning)
        if (auto* sf = std::getenv ("VV_SPEC_FILE"))
            for (auto& line : juce::StringArray::fromLines (juce::File (juce::String (sf)).loadFileAsString()))
                if (line.containsChar ('\t')) over.set (line.upToFirstOccurrenceOf ("\t", false, false), line.fromFirstOccurrenceOf ("\t", false, false));
        const auto& fp = vv::factoryPresets()[(size_t) pi];
        juce::FileOutputStream os (juce::File::getCurrentWorkingDirectory().getChildFile (out));
        os.setPosition (0); os.truncate();
        double sec = 0, audio = 0;
        auto make = [&] (std::unique_ptr<VvitchProcessor>& p)
        {
            p = std::make_unique<VvitchProcessor>(); p->setPlayConfigDetails (0, 2, 48000.0, 480); p->prepareToPlay (48000.0, 480); p->presets.loadFactory (pi);
            if (over.containsKey (fp.name)) p->applyMacroSpec (over[fp.name]);
        };
        auto run = [&] (VvitchProcessor& p, int blocks, int offAt, std::function<void (int)> perBlock)
        {
            juce::AudioBuffer<float> b (2, 480);
            for (int blk = 0; blk < blocks; ++blk)
            {
                juce::MidiBuffer m;
                if (blk == 0) for (int nt : { 48, 55, 63 }) m.addEvent (juce::MidiMessage::noteOn (1, nt, 0.85f), 7);
                if (blk == offAt) for (int nt : { 48, 55, 63 }) m.addEvent (juce::MidiMessage::noteOff (1, nt), 0);
                if (perBlock) perBlock (blk);
                b.clear(); const auto t0 = juce::Time::getHighResolutionTicks(); p.processBlock (b, m); sec += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                for (int k = 0; k < 480; ++k) { os.writeFloat (b.getSample (0, k)); os.writeFloat (b.getSample (1, k)); }
            }
            audio += blocks * 0.01;
        };
        auto setM = [] (VvitchProcessor& p, int m, float v) { p.apvts.getParameter ("macro" + juce::String (m + 1))->setValueNotifyingHost (v); };
        std::unique_ptr<VvitchProcessor> p;
        make (p); std::printf ("PRESET %d\t%s\n", pi, fp.name);
        for (int m = 0; m < VvitchProcessor::kMacros; ++m)
        {
            juce::StringArray t; for (auto& x : p->macroTargets (m)) t.add (x.id + ":" + juce::String (x.amount, 3));
            std::printf ("MACRO %d\t%s\t%s\n", m + 1, p->macroName (m).toRawUTF8(), t.joinIntoString (",").toRawUTF8());
        }
        run (*p, 250, 150, {});                                       // static, macros at 0
        for (int m = 0; m < VvitchProcessor::kMacros; ++m)
            for (float v : { 0.25f, 0.5f, 0.75f, 1.0f }) { make (p); setM (*p, m, v); run (*p, 250, 150, {}); }
        for (int m = 0; m <= VvitchProcessor::kMacros + 1; ++m)       // ramps (m == 8: all eight together, m == 9: none = the reference), chord held
        {
            make (p);
            run (*p, 900, -1, [&] (int blk) { const float v = juce::jlimit (0.0f, 1.0f, (blk - 50) / 800.0f);
                                              for (int k = 0; k < VvitchProcessor::kMacros; ++k) if (k == m || m == VvitchProcessor::kMacros) setM (*p, k, v); });
        }
        std::printf ("CPU %.6f\n", sec / audio * 100.0);
        return 0;
    }
#endif
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
    if (mode.isEmpty() || mode == "big") proc.presets.loadFactory (1);   // Glass Coffin for the product shot
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<VvitchEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "big") ed->setSize (VvitchProcessor::kBaseW * 3 / 2, VvitchProcessor::kBaseH * 3 / 2);
    if (mode == "banner") { vv::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/vvitch/"; i.notes = "New factory presets"; e->getPanel().demoBanner (i); }
    if (mode == "keys")
    {
        auto r = e->getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
        auto m = e->getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
        auto p = e->getPanel().auditPresets();  std::printf ("%s", p.toRawUTF8());
       #ifdef VV_HAS_PRESET_MACROS
        auto d = e->getPanel().auditDragDrop(); std::printf ("%s", d.toRawUTF8());
        if (! d.contains ("DRAG AUDIT OK")) p = "PRESET AUDIT FAILED";
       #endif
        proc.undoManager.clearUndoHistory(); proc.presets.loadFactory (4);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        const bool undoOk = proc.undoManager.canUndo();
        std::printf ("%s a preset load can be undone\n", undoOk ? "  ok  " : "  FAIL");
        if (! undoOk) p = "PRESET AUDIT FAILED";
        return (r.contains ("KEYBOARD AUDIT OK") && m.contains ("MENU AUDIT OK") && p.contains ("PRESET AUDIT OK")) ? 0 : 1;
    }
    if (mode != "empty") vvui::demoDisplay (e->getPanel().display());
    if (mode == "menu") e->getPanel().demoKnobMenu();
    if (mode == "presets") e->getPanel().demoPresetMenu();
   #ifdef VV_HAS_PRESET_MACROS
    if (mode == "drag") { proc.presets.loadFactory (1); juce::MessageManager::getInstance()->runDispatchLoopUntil (200); e->getPanel().demoDrag (0, "glide"); }
   #endif
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
