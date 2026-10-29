// CMPLXTY Visualizer test harness: renders the real editor to PNGs and runs the processor / preset / keyboard / CPU tests.
// usage: VisualizerScreenshot out.png [shot] [tab=N] [preset=NAME] [mode=N] [image=1] [grid=N] [frame=1] [big=1] [menu=1] [banner=1]
//        VisualizerScreenshot - proc|presets|params|keys|cpu
// Run under Xvfb. Set HOME to a scratch folder: user presets / images are written below it.
#include "VizUI.h"
#include <random>
#include <cmath>
#include <chrono>

namespace
{
int fails = 0;
void check (bool c, const juce::String& what) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", what.toRawUTF8()); fails += c ? 0 : 1; }

// A short synthetic track: kick on every beat, snare on 2 and 4, eighth hats, bass line, pad. 48 kHz mono.
std::vector<float> music (double secs, double sr = 48000.0, double bpm = 120.0)
{
    const int n = (int) (secs * sr);
    std::vector<float> x ((size_t) n);
    std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    const double spb = 60.0 / bpm;
    double kph = 0, bph = 0, hpPrev = 0;
    const double bassNotes[4] = { 55.0, 55.0, 65.41, 49.0 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double beatT = std::fmod (t, spb); const int beat = (int) (t / spb);
        const double kf = 45.0 + 110.0 * std::exp (-beatT * 30.0);
        kph += kf / sr;
        double s = 0.9 * std::sin (2 * M_PI * kph) * std::exp (-beatT * 7.0);
        if (beat % 2 == 1) s += 0.35 * U (rng) * std::exp (-beatT * 18.0);
        const double eT = std::fmod (t, spb * 0.5);
        const float nz = U (rng); const double hp = nz - hpPrev; hpPrev = nz;
        s += 0.12 * hp * std::exp (-eT * 60.0);
        bph += bassNotes[(beat / 4) % 4] / sr;
        s += 0.25 * (2.0 * (bph - std::floor (bph)) - 1.0) * (0.6 + 0.4 * std::exp (-beatT * 4.0));
        for (double f : { 220.0, 277.18, 329.63 }) s += 0.05 * std::sin (2 * M_PI * f * t + 0.3 * std::sin (t));
        x[(size_t) i] = (float) (0.5 * s);
    }
    return x;
}

struct FakeHead final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Feeder
{
    VisualizerProcessor& proc; std::vector<float> src; size_t pos = 0; FakeHead head; int bs = 400;
    Feeder (VisualizerProcessor& p, double secs = 20.0) : proc (p), src (music (secs)) { proc.setPlayHead (&head); }
    ~Feeder() { proc.setPlayHead (nullptr); }
    void feed (int samples)
    {
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer none;
        for (int done = 0; done < samples; done += bs)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) buf.setSample (c, i, src[(pos + (size_t) i) % src.size()]);
            proc.processBlock (buf, none);
            pos += (size_t) bs; head.ppq += bs / 48000.0 * head.bpm / 60.0;
        }
    }
};

// Test pictures, made here so nothing third-party ships: a sun-over-hills "photo" and a ring logo.
juce::Image testPicture (int which)
{
    juce::Image im (juce::Image::ARGB, 960, 640, true);
    juce::Graphics g (im);
    if (which == 0)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b2a6b), 0, 0, juce::Colour (0xfff08a4b), 0, 420, false)); g.fillAll();
        g.setColour (juce::Colour (0xffffe08a)); g.fillEllipse (380, 170, 200, 200);
        juce::Path hills; hills.startNewSubPath (0, 640); hills.lineTo (0, 430);
        for (int x = 0; x <= 960; x += 20) hills.lineTo ((float) x, 430.0f - 60.0f * std::sin (x * 0.011f) - 25.0f * std::sin (x * 0.037f));
        hills.lineTo (960, 640); hills.closeSubPath();
        g.setColour (juce::Colour (0xff1a1030)); g.fillPath (hills);
        g.setColour (juce::Colour (0xff3c1f4f)); g.fillRect (0, 560, 960, 80);
    }
    else
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        g.drawEllipse (330, 170, 300, 300, 26.0f);
        g.fillEllipse (440, 280, 80, 80);
        g.setFont (juce::FontOptions (96.0f, juce::Font::bold));
        g.drawText ("CMPLXTY", 0, 470, 960, 120, juce::Justification::centred);
    }
    return im;
}
juce::MemoryBlock encode (const juce::Image& im, bool jpg)
{
    juce::MemoryOutputStream os;
    if (jpg) { juce::JPEGImageFormat f; f.setQuality (0.9f); f.writeImageToStream (im, os); }
    else     { juce::PNGImageFormat f; f.writeImageToStream (im, os); }
    return os.getMemoryBlock();
}
bool writePng (const juce::Image& img, const juce::String& path)
{
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("wrote %s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok;
}
double litFraction (const juce::Image& im)
{
    juce::Image::BitmapData bd (im, juce::Image::BitmapData::readOnly);
    int lit = 0, tot = 0;
    for (int y = 0; y < im.getHeight(); y += 3) for (int x = 0; x < im.getWidth(); x += 3) { auto c = bd.getPixelColour (x, y); lit += (c.getBrightness() > 0.08f) ? 1 : 0; ++tot; }
    return tot > 0 ? (double) lit / tot : 0.0;
}
float pv (VisualizerProcessor& p, const char* id) { auto* q = p.apvts.getParameter (id); return q->convertFrom0to1 (q->getValue()); }
void setp (VisualizerProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

//==============================================================================================================
int procTests()
{
    const double sr = 48000.0; const int bs = 480;
    const auto x = music (2.0);
    {   // pass-through: bit exact, stereo
        VisualizerProcessor proc; proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        check (proc.getTotalNumInputChannels() == 2 && ! proc.acceptsMidi() && proc.getTailLengthSeconds() == 0.0, "stereo in/out, no MIDI, no tail");
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer none; double maxd = 0;
        for (size_t b = 0; b + bs <= x.size(); b += bs)
        {
            for (int i = 0; i < bs; ++i) { buf.setSample (0, i, x[b + (size_t) i]); buf.setSample (1, i, -0.5f * x[b + (size_t) i]); }
            proc.processBlock (buf, none);
            for (int i = 0; i < bs; ++i) maxd = std::max ({ maxd, (double) std::fabs (buf.getSample (0, i) - x[b + (size_t) i]), (double) std::fabs (buf.getSample (1, i) + 0.5f * x[b + (size_t) i]) });
        }
        check (maxd == 0.0, "audio passes through unchanged (bit exact)");
        // the render side sees it
        vz::ana::Features::Settings st; proc.features.setSampleRate (sr);
        const auto& s = proc.features.consume (proc.analyzer.frames, proc.analyzer.samples, 1.0 / 30.0, st);
        check (s.rawLevelDb > -40.0f, "analysis FIFO delivers level to the render thread (" + juce::String (s.rawLevelDb, 1) + " dB)");
    }
    {   // mono
        VisualizerProcessor proc;
        auto l = proc.getBusesLayout(); l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (proc.setBusesLayout (l), "mono layout supported");
        proc.setRateAndBufferSizeDetails (sr, bs); proc.prepareToPlay (sr, bs);
        juce::AudioBuffer<float> buf (1, bs); juce::MidiBuffer none; bool same = true;
        for (int i = 0; i < bs; ++i) buf.setSample (0, i, x[(size_t) i]);
        proc.processBlock (buf, none);
        for (int i = 0; i < bs; ++i) same = same && buf.getSample (0, i) == x[(size_t) i];
        check (same, "mono passes through unchanged");
    }
    {   // host tempo reaches the snapshot
        VisualizerProcessor proc; proc.setRateAndBufferSizeDetails (sr, 400); proc.prepareToPlay (sr, 400);
        Feeder fd (proc); fd.head.bpm = 128.0;
        vz::ana::Features::Settings st; st.sync = true; proc.features.setSampleRate (sr);
        vz::ana::Snapshot s;
        for (int f = 0; f < 60; ++f) { fd.feed (1600); s = proc.features.consume (proc.analyzer.frames, proc.analyzer.samples, 1.0 / 30.0, st); }
        check (s.hostClock && std::fabs (s.bpm - 128.0f) < 0.01f && s.playing, "host tempo + transport used for sync (" + juce::String (s.bpm, 2) + " BPM)");
    }
    {   // state round trip: params, images, text, font-less, tiles, setlist, window size
        VisualizerProcessor a;
        setp (a, "mode", 7); setp (a, "palette", 12); setp (a, "speed", 1.7f); setp (a, "grid", 3); setp (a, "imgOn", 1);
        check (a.addImageData ("sunset.jpg", encode (testPicture (0), true), false).isEmpty(), "JPG accepted");
        check (a.addImageData ("logo.png", encode (testPicture (1), false), false).isEmpty(), "PNG accepted");
        juce::MemoryBlock junkImg ("nope", 4);
        check (a.addImageData ("bad.png", junkImg, false).isNotEmpty(), "broken image refused with a message");
        a.setSelectedImage (1);
        a.setLines (juce::StringArray { "first line", "second line" }, false);
        a.setTile (2, "Init"); a.setSlot (0, vz::presets::factory()[3].name); a.setSlot (35, "Init");
        a.uiWidth = 1500; a.uiHeight = 900;
        juce::MemoryBlock mb; a.getStateInformation (mb);
        VisualizerProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto eb = b.getExtras();
        check ((int) pv (b, "mode") == 7 && (int) pv (b, "palette") == 12 && std::fabs (pv (b, "speed") - 1.7f) < 0.02f && (int) pv (b, "grid") == 3, "parameters survive save / load");
        check (eb.images.size() == 2 && eb.selectedImage == 1 && eb.images[1].name == "logo.png", "images are saved inside the project");
        check (eb.lines.size() == 2 && eb.lines[1] == "second line", "text survives save / load");
        check (eb.tiles[2] == "Init" && eb.slots[0] == vz::presets::factory()[3].name && eb.slots[35] == "Init", "grid tiles and setlist survive save / load");
        check (b.uiWidth == 1500 && b.uiHeight == 900, "window size survives save / load");
        check (mb.getSize() < 2 * 1024 * 1024, "project state stays small (" + juce::String ((int) (mb.getSize() / 1024)) + " KB with 2 images)");
        const char junk[] = "not a state"; b.setStateInformation (junk, sizeof junk);
        check ((int) pv (b, "mode") == 7 && b.getExtras().images.size() == 2, "garbage state ignored");
        // big images get downscaled before storing
        juce::Image huge (juce::Image::RGB, 5000, 3000, true);
        VisualizerProcessor c; c.addImageData ("huge.png", encode (huge, false), false);
        auto dec = juce::ImageFileFormat::loadFrom (c.getExtras().images[0].data.getData(), c.getExtras().images[0].data.getSize());
        check (dec.isValid() && juce::jmax (dec.getWidth(), dec.getHeight()) <= 1920, "huge images are downscaled before saving (" + juce::String (dec.getWidth()) + "x" + juce::String (dec.getHeight()) + ")");
    }
    {   // user preset with images
        VisualizerProcessor a;
        a.addImageData ("logo.png", encode (testPicture (1), false), false);
        setp (a, "mode", 20); setp (a, "imgOn", 1); setp (a, "imgPulse", 0.8f);
        const auto f = a.saveUserPreset ("Harness Test", true);
        check (f.existsAsFile() && f.getParentDirectory() == vz::presets::userDir(), "user preset saved (" + f.getFullPathName() + ")");
        VisualizerProcessor b;
        check (b.allPresetNames().contains ("user:Harness Test"), "user preset listed");
        check (b.applyPreset ("user:Harness Test"), "user preset loads");
        check ((int) pv (b, "mode") == 20 && std::fabs (pv (b, "imgPulse") - 0.8f) < 0.01f && b.getExtras().images.size() == 1, "user preset restores params and its images");
        const auto f2 = a.saveUserPreset ("Harness NoImg", false);
        VisualizerProcessor c2; c2.loadUserPreset (f2);
        check (c2.getExtras().images.empty(), "preset saved without images carries none");
        f.deleteFile(); f2.deleteFile();
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}

//==============================================================================================================
int presetTests()
{
    const auto& fs = vz::presets::factory();
    check (fs.size() >= 60, juce::String ((int) fs.size()) + " factory presets (need 60+)");
    juce::StringArray names; int dup = 0;
    for (auto& f : fs) { if (names.contains (f.name)) { ++dup; std::printf ("  duplicate: %s\n", f.name.toRawUTF8()); } names.add (f.name); }
    check (dup == 0, "preset names are unique");
    std::array<int, vz::kNumModes> modeUse {};
    VisualizerProcessor proc; proc.setRateAndBufferSizeDetails (48000.0, 400); proc.prepareToPlay (48000.0, 400);
    proc.addImageData ("logo.png", encode (testPicture (1), false), false);
    Feeder fd (proc);
    vz::Renderer r; auto assets = std::make_shared<vz::Assets>();
    assets->images.push_back (testPicture (1).convertedToFormat (juce::Image::ARGB));
    assets->uiBold = vzui::uiTypeface (true);
    auto img = vz::makeSoftwareImage (480, 270);
    vz::ana::Features::Settings st; proc.features.setSampleRate (48000.0);
    int badSpec = 0, badApply = 0, dark = 0;
    for (int i = 0; i < (int) fs.size(); ++i)
    {
        vz::ParamValues base = vz::presets::defaults();
        const int unknown = vz::presets::applySpec (base, fs[(size_t) i].spec);
        if (unknown != 0) { ++badSpec; std::printf ("  %s: %d unknown keys in \"%s\"\n", fs[(size_t) i].name.toRawUTF8(), unknown, fs[(size_t) i].spec.toRawUTF8()); }
        proc.applyFactory (i);
        const auto want = vz::presets::valuesFor (fs[(size_t) i]); const auto got = proc.values();
        for (int p = 0; p < vz::kNumParams; ++p)
            if (! vz::presets::isGlobalParam (p) && std::fabs (want[(size_t) p] - got[(size_t) p]) > 0.011f * juce::jmax (1.0f, std::fabs (want[(size_t) p])))
            { ++badApply; std::printf ("  %s: %s want %g got %g\n", fs[(size_t) i].name.toRawUTF8(), vz::kParams[p].id, want[(size_t) p], got[(size_t) p]); break; }
        if (proc.currentPresetName() != fs[(size_t) i].name) ++badApply;
        modeUse[(size_t) juce::jlimit (0, vz::kNumModes - 1, (int) got[vz::pMode])]++;
        r.reset();
        assets->lines = proc.getExtras().lines;
        double lit = 0;
        for (int f = 0; f < 24; ++f)
        {
            fd.feed (1600);
            const auto& s = proc.features.consume (proc.analyzer.frames, proc.analyzer.samples, 1.0 / 30.0, st);
            r.render (img, got, s, *assets, 1.0 / 30.0);
            lit = std::max (lit, litFraction (img));
        }
        if (lit < 0.01) { ++dark; std::printf ("  %s: renders (almost) black: %.4f lit\n", fs[(size_t) i].name.toRawUTF8(), lit); }
    }
    check (badSpec == 0, "every preset spec parses (no unknown keys)");
    check (badApply == 0, "every preset loads into the processor exactly");
    check (dark == 0, "every preset renders a visible picture with music");
    int unused = 0; for (int m = 0; m < vz::kNumModes; ++m) if (modeUse[(size_t) m] == 0) { ++unused; std::printf ("  mode without a preset: %s\n", vz::kModes[m].name); }
    check (unused == 0, "every mode has at least one factory preset");
    std::printf (fails == 0 ? "PRESET TESTS PASSED\n" : "%d PRESET TEST(S) FAILED\n", fails);
    return fails;
}

//==============================================================================================================
int keyTests (VisualizerEditor& e, VisualizerProcessor& proc)
{
    auto r = e.getPanel().auditKeyboard(); std::printf ("%s", r.toRawUTF8());
    auto m = e.getPanel().auditMenus();    std::printf ("%s", m.toRawUTF8());
    check (r.contains ("AUDIT OK") && m.contains ("MENU AUDIT OK"), "editor never takes the keyboard; menus work");
    auto* st = dynamic_cast<vzui::Stage*> (e.getPanel().display());
    check (st != nullptr, "stage present");
    if (st == nullptr) return ++fails;
    const auto& fs = vz::presets::factory();
    check (proc.getExtras().slots[0].isNotEmpty() && proc.getExtras().slots[35].isNotEmpty(), "setlist comes pre-filled (36 slots)");
    for (int i = 0; i < vz::kNumSlots; ++i) proc.setSlot (i, {});
    proc.setSlot (0, fs[5].name); proc.setSlot (10, fs[9].name);   // keys 1 and Q
    // the plug-in window ignores performance keys (they belong to the host there)
    const auto before = proc.currentPresetName();
    const bool used1 = e.getPanel().keyPressed (juce::KeyPress ('1', {}, '1')) || e.keyPressed (juce::KeyPress ('1', {}, '1'));
    const bool usedSp = e.getPanel().keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) || e.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    check (! used1 && ! usedSp && proc.currentPresetName() == before, "plug-in window: slot keys and Space are not consumed (they go to the DAW)");
    st->openPopOut (false);
    auto* pw = st->popWindow(); auto* v = st->popView();
    if (pw != nullptr) std::printf ("  pop-out: parent=%p onDesktop=%d childOfEditor=%d\n", (void*) pw->getParentComponent(), (int) pw->isOnDesktop(), (int) e.isParentOf (pw));
    check (pw != nullptr && v != nullptr && pw->getParentComponent() == nullptr && pw->isOnDesktop() && ! e.isParentOf (pw), "pop-out is its own top-level window");
    check (! e.hasKeyboardFocus (true) && (v == nullptr || ! v->hasKeyboardFocus (true)), "opening the pop-out does not steal the keyboard");
    if (v == nullptr) return ++fails;
    check (v->getWantsKeyboardFocus(), "pop-out view accepts keys once clicked");
    check (v->keyPressed (juce::KeyPress ('1', {}, '1')) && proc.currentPresetName() == fs[5].name && proc.activeSlot == 0, "pop-out: key 1 fires slot 1");
    check (v->keyPressed (juce::KeyPress ('q', {}, 'q')) && proc.currentPresetName() == fs[9].name && proc.activeSlot == 10, "pop-out: key Q fires slot 11 (case-insensitive)");
    check (v->keyPressed (juce::KeyPress (juce::KeyPress::leftKey)) && proc.activeSlot == 0, "pop-out: LEFT steps to the previous filled slot");
    check (v->keyPressed (juce::KeyPress ('w', {}, 'w')) && proc.activeSlot == 0, "pop-out: empty slot leaves the preset alone");
    const int mode0 = (int) proc.value (vz::pMode);
    check (v->keyPressed (juce::KeyPress (juce::KeyPress::upKey)) && (int) proc.value (vz::pMode) == (mode0 + 1) % vz::kNumModes, "pop-out: UP = next mode");
    check (v->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)) && proc.value (vz::pStrobe) > 0.5f, "pop-out: hold ENTER = strobe on");
    v->keyStateChanged (false);
    check (proc.value (vz::pStrobe) < 0.5f, "pop-out: releasing ENTER = strobe off");
    check (v->keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey)) && proc.value (vz::pBlackout) > 0.5f, "pop-out: BACKSPACE = blackout");
    v->keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
    check (proc.value (vz::pBlackout) < 0.5f, "pop-out: BACKSPACE again = blackout off");
    check (! v->keyPressed (juce::KeyPress ('1', juce::ModifierKeys::commandModifier, '1')), "pop-out: Ctrl/Cmd shortcuts are not swallowed");
    check (v->keyPressed (juce::KeyPress (juce::KeyPress::F1Key)) && v->helpShown(), "pop-out: F1 shows the key help");
    st->closePopOut();
    check (st->popWindow() == nullptr && ! e.hasKeyboardFocus (true), "closing the pop-out hands nothing to the plug-in window");
    std::printf (fails == 0 ? "KEY TESTS PASSED\n" : "%d KEY TEST(S) FAILED\n", fails);
    return fails;
}

//==============================================================================================================
int cpuTests()
{
    VisualizerProcessor proc; proc.setRateAndBufferSizeDetails (48000.0, 512); proc.prepareToPlay (48000.0, 512);
    {   // audio thread cost
        const auto x = music (10.0);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer none;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t b = 0; b + 512 <= x.size(); b += 512) { for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, x.data() + b, 512); proc.processBlock (buf, none); }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  audio thread: 10 s of stereo audio analysed in %.1f ms (%.2f%% of one core)\n", ms, ms / 100.0);
        check (ms < 300.0, "audio thread cost is small (" + juce::String (ms / 100.0, 2) + "% CPU)");
    }
    Feeder fd (proc);
    vz::Renderer r; vz::Assets as; as.images.push_back (testPicture (0).convertedToFormat (juce::Image::ARGB)); as.uiBold = vzui::uiTypeface (true);
    as.lines = juce::StringArray { "CMPLXTY", "VISUALIZER", "TURN IT UP" };
    vz::ana::Features::Settings st; proc.features.setSampleRate (48000.0);
    auto run = [&] (vz::ParamValues v, int h, int frames)
    {
        auto img = vz::makeSoftwareImage (h * 16 / 9, h);
        r.reset(); double tot = 0;
        for (int f = 0; f < frames; ++f)
        {
            fd.feed (1600);
            const auto& s = proc.features.consume (proc.analyzer.frames, proc.analyzer.samples, 1.0 / 30.0, st);
            const auto t0 = std::chrono::steady_clock::now();
            r.render (img, v, s, as, 1.0 / 30.0);
            if (f >= 4) tot += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        }
        return tot / (frames - 4);
    };
    double worst = 0, sum = 0; juce::String worstName;
    for (int m = 0; m < vz::kNumModes; ++m)
    {
        auto v = vz::presets::defaults(); v[vz::pMode] = (float) m;
        if (std::getenv ("VZ_CPU_FLAT") != nullptr) { v[vz::pGlow] = 0; v[vz::pTrails] = 0; }
        const double ms = run (v, 360, 24);
        std::printf ("  %-16s %6.2f ms/frame @ 640x360\n", vz::kModes[m].name, ms);
        sum += ms; if (ms > worst) { worst = ms; worstName = vz::kModes[m].name; }
    }
    const double avg = sum / vz::kNumModes;
    std::printf ("  average %.2f ms, worst %.2f ms (%s) at the default Medium quality\n", avg, worst, worstName.toRawUTF8());
    auto heavy = vz::presets::defaults();
    heavy[vz::pMode] = 0; heavy[vz::pGrid] = 6; heavy[vz::pImgOn] = 1; heavy[vz::pTexOn] = 1; heavy[vz::pDitOn] = 1; heavy[vz::pTrkOn] = 1; heavy[vz::pGlsOn] = 1; heavy[vz::pTextOver] = 1;
    const double hv = run (heavy, 360, 20);
    const double hi = run (vz::presets::defaults(), 720, 12);
    std::printf ("  heavy (3x3 grid + image + text + all four FX) %.2f ms; default look at High (1280x720) %.2f ms\n", hv, hi);
    check (avg < 1000.0 / 30.0, "average mode renders faster than 30 fps at Medium (" + juce::String (avg, 1) + " ms)");
    check (worst < 1000.0 / 15.0, "slowest mode stays above 15 fps at Medium (" + juce::String (worst, 1) + " ms)");
    std::printf (fails == 0 ? "CPU TESTS PASSED\n" : "%d CPU TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "screenshot.png";
    juce::StringPairArray opt; juce::String mode;
    for (int i = 2; i < argc; ++i) { juce::String a (argv[i]); if (a.contains ("=")) opt.set (a.upToFirstOccurrenceOf ("=", false, false), a.fromFirstOccurrenceOf ("=", false, false)); else mode = a; }
    vz::upd::dirOverride() = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vz_shot_settings");
    vz::upd::dirOverride().deleteRecursively();
    { vz::upd::Settings s; s.enabled = true; s.save();
      auto* o = new juce::DynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000)); o->setProperty ("ok", false);
      vz::upd::writeJson (vz::upd::dataDir().getChildFile ("update-cache.json"), juce::var (o)); }
    if (mode == "proc") return procTests();
    if (mode == "presets") return presetTests();
    if (mode == "cpu") return cpuTests();
    VisualizerProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 400); proc.prepareToPlay (48000.0, 400);
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
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<VisualizerEditor*> (ed.get());
    ed->setVisible (true);
    if (mode == "keys") { const int r = keyTests (*e, proc); ed.reset(); return r; }

    auto* st = dynamic_cast<vzui::Stage*> (e->getPanel().display());
    if (st != nullptr) st->stopEngine();
    if (opt["big"] == "1") ed->setSize (VisualizerProcessor::kBaseW * 3 / 2, VisualizerProcessor::kBaseH * 3 / 2);
    if (opt["image"].isNotEmpty())
    {
        const int which = opt["image"].getIntValue();
        proc.addImageData (which == 2 ? "logo.png" : "sunset.jpg", encode (testPicture (which == 2 ? 1 : 0), which != 2), false);
        if (which == 3) proc.addImageData ("logo.png", encode (testPicture (1), false), false);
    }
    if (opt["preset"].isNotEmpty() && ! proc.applyPreset (opt["preset"])) { std::printf ("unknown preset %s\n", opt["preset"].toRawUTF8()); return 2; }
    if (opt["mode"].isNotEmpty()) vzui::setParamGesture (proc, vz::pMode, (float) opt["mode"].getIntValue());
    if (opt["grid"].isNotEmpty()) vzui::setParamGesture (proc, vz::pGrid, (float) opt["grid"].getIntValue());
    if (opt["imgOn"].isNotEmpty()) vzui::setParamGesture (proc, vz::pImgOn, (float) opt["imgOn"].getIntValue());
    if (opt["quality"].isNotEmpty()) vzui::setParamGesture (proc, vz::pQuality, (float) opt["quality"].getIntValue());
    if (opt["tab"].isNotEmpty()) e->getPanel().setTab (opt["tab"].getIntValue());
    if (opt["setlist"] == "1") { const auto& fs = vz::presets::factory(); for (int i = 0; i < 20; ++i) proc.setSlot (i, fs[(size_t) (1 + i * 3 % (int) fs.size())].name); proc.activeSlot = 4; }
    if (opt["tiles"] == "1") { const auto& fs = vz::presets::factory(); for (int i = 0; i < 9; ++i) proc.setTile (i, fs[(size_t) (2 + i * 7 % (int) fs.size())].name); }
    if (opt["banner"] == "1") { vz::upd::Info i; i.version = "1.1.0"; i.url = "https://cmplxty.music/plugins/visualizer/"; i.notes = "New modes"; e->getPanel().demoBanner (i); }
    if (st != nullptr && mode != "empty")
    {
        Feeder fd (proc);
        const int frames = opt["frames"].isNotEmpty() ? opt["frames"].getIntValue() : 75;
        for (int f = 0; f < frames; ++f) { fd.feed (1600); st->step (1, 1.0 / 30.0); }
    }
    if (opt["menu"] == "1") e->getPanel().demoKnobMenu();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    if (opt["frame"] == "1" && st != nullptr) return writePng (st->engine().frame(), out) ? 0 : 1;
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    const bool ok = writePng (img, out);
    ed.reset();
    return ok ? 0 : 1;
}
