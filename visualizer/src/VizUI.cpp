#include "VizUI.h"

namespace
{
const juce::Colour kBg (0xff0c0c0c), kPanel (0xff181818), kPri (0xffffffff), kSec (0xffb8b8b8), kDim (0xff9f9f9f), kTrack (0xff2d2d2d), kLine (0xff2a2a2a);
using vzui::uiFont;

juce::Rectangle<float> fitRect (int iw, int ih, juce::Rectangle<float> into)
{
    if (iw <= 0 || ih <= 0) return into;
    const float s = juce::jmin (into.getWidth() / (float) iw, into.getHeight() / (float) ih);
    return juce::Rectangle<float> ((float) iw * s, (float) ih * s).withCentre (into.getCentre());
}

juce::String displayName (const juce::String& preset) { return preset.startsWith ("user:") ? preset.substring (5) : preset; }

juce::uint64 quickHash (const juce::MemoryBlock& m)
{
    juce::uint64 h = 1469598103934665603ull;
    const auto* d = static_cast<const juce::uint8*> (m.getData());
    const size_t n = m.getSize(), stride = juce::jmax ((size_t) 1, n / 65536);
    for (size_t i = 0; i < n; i += stride) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}

bool isImageFile (const juce::File& f) { return f.hasFileExtension ("jpg;jpeg;png"); }
bool isFontFile (const juce::File& f)  { return f.hasFileExtension ("ttf;otf"); }
} // namespace

namespace vzui
{
void setParamGesture (VisualizerProcessor& proc, int p, float plain)
{
    if (auto* prm = proc.apvts.getParameter (vz::kParams[p].id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}

juce::PopupMenu presetMenu (VisualizerProcessor& proc, const juce::String& current, std::function<void (const juce::String&)> pick, bool withAuto)
{
    juce::PopupMenu m;
    if (withAuto) { m.addItem (juce::PopupMenu::Item ("Automatic (variation)").setTicked (current.isEmpty()).setAction ([pick] { pick ({}); })); m.addSeparator(); }
    juce::String group; juce::PopupMenu sub; bool subTicked = false;
    auto flush = [&] { if (group.isNotEmpty()) m.addSubMenu (group.toUpperCase(), sub, true, nullptr, subTicked); sub = {}; subTicked = false; };
    for (auto& f : vz::presets::factory())
    {
        if (f.group != group) { flush(); group = f.group; }
        const auto name = f.name;
        subTicked = subTicked || name == current;
        sub.addItem (juce::PopupMenu::Item (name).setTicked (name == current).setAction ([pick, name] { pick (name); }));
    }
    flush();
    juce::PopupMenu user;
    for (auto& f : vz::presets::userPresets())
    {
        const auto name = "user:" + f.getFileNameWithoutExtension();
        user.addItem (juce::PopupMenu::Item (f.getFileNameWithoutExtension()).setTicked (name == current).setAction ([pick, name] { pick (name); }));
    }
    m.addSeparator();
    if (user.getNumItems() > 0) m.addSubMenu ("USER", user);
    else m.addItem (juce::PopupMenu::Item ("(no user presets yet)").setEnabled (false));
    juce::ignoreUnused (proc);
    return m;
}

bool isImportable (const juce::String& path)
{
    const juce::File f (path);
    return isImageFile (f) || isFontFile (f) || f.hasFileExtension ("cvzpreset;txt");
}

juce::String importFiles (VisualizerProcessor& proc, const juce::StringArray& paths)
{
    int imgs = 0; juce::String err, last;
    for (auto& p : paths)
    {
        const juce::File f (p);
        if (isImageFile (f))
        {
            const auto e = proc.addImageFile (f);
            if (e.isEmpty()) ++imgs; else err = f.getFileName() + ": " + e;
        }
        else if (isFontFile (f))
        {
            const auto e = proc.setFontFile (f);
            if (e.isEmpty()) last = "Font loaded: " + f.getFileName(); else err = f.getFileName() + ": " + e;
        }
        else if (f.hasFileExtension ("cvzpreset"))
        {
            if (proc.loadUserPreset (f)) last = "Preset loaded: " + f.getFileNameWithoutExtension(); else err = f.getFileName() + ": not a Visualizer preset";
        }
        else if (f.hasFileExtension ("txt") && f.getSize() < 256 * 1024)
        {
            juce::StringArray lines; lines.addLines (f.loadFileAsString()); lines.removeEmptyStrings();
            if (lines.size() > 400) lines.removeRange (400, lines.size());
            proc.setLines (lines, false); last = "Text loaded: " + juce::String (lines.size()) + " lines";
        }
    }
    if (err.isNotEmpty()) return err;
    if (imgs > 0) return juce::String (imgs) + (imgs == 1 ? " image added" : " images added") + " - it reacts to the music (IMAGE tab)";
    return last;
}

//==============================================================================================================
RenderEngine::RenderEngine (VisualizerProcessor& p) : juce::Thread ("CMPLXTY Visualizer render"), proc (p)
{
    assets = std::make_shared<vz::Assets>();
}
RenderEngine::~RenderEngine() { stop(); }
void RenderEngine::start() { if (! isThreadRunning()) startThread(); }
void RenderEngine::stop() { if (isThreadRunning()) { signalThreadShouldExit(); notify(); stopThread (2000); } }

void RenderEngine::setAssets (std::shared_ptr<const vz::Assets> a) { const juce::SpinLock::ScopedLockType l (assetLock); assets = std::move (a); }
juce::Image RenderEngine::frame() const { const juce::SpinLock::ScopedLockType l (frameLock); return front; }
vz::ana::Snapshot RenderEngine::snapshot() const { const juce::SpinLock::ScopedLockType l (snapLock); return snap; }

void RenderEngine::renderOnce (double dt)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const auto pv = proc.values();
    const double sr = proc.sampleRate.load();
    if (sr != lastSr) { proc.features.setSampleRate (sr); lastSr = sr; }
    vz::ana::Features::Settings st;
    st.gainDb = pv[vz::pGain]; st.smooth = pv[vz::pSmooth]; st.react = pv[vz::pReact];
    st.beatMode = (int) pv[vz::pBeatMode]; st.sync = pv[vz::pSync] > 0.5f;
    const auto& s = proc.features.consume (proc.analyzer.frames, proc.analyzer.samples, dt, st);

    const int q = juce::jlimit (0, 3, (int) pv[vz::pQuality]);
    const int h = vz::kQualityLines[q];
    const int w = juce::jlimit (64, 4096, ((int) std::lround ((float) h * aspect.load()) + 1) & ~1);
    if (! back.isValid() || back.getWidth() != w || back.getHeight() != h || back.getReferenceCount() > 1)
        back = vz::makeSoftwareImage (w, h);
    std::shared_ptr<const vz::Assets> a;
    { const juce::SpinLock::ScopedLockType l (assetLock); a = assets; }
    renderer.render (back, pv, s, *a, dt);
    { const juce::SpinLock::ScopedLockType l (frameLock); std::swap (front, back); }
    { const juce::SpinLock::ScopedLockType l (snapLock); snap = s; }
    frames.fetch_add (1);
    lastMs.store ((float) (juce::Time::getMillisecondCounterHiRes() - t0));
}

void RenderEngine::run()
{
    double last = juce::Time::getMillisecondCounterHiRes(), fpsT = last; int fpsN = 0;
    while (! threadShouldExit())
    {
        if (! active.load()) { wait (60); last = juce::Time::getMillisecondCounterHiRes(); continue; }
        const int fps = vz::kFpsValues[juce::jlimit (0, 2, (int) proc.value (vz::pFps))];
        const double start = juce::Time::getMillisecondCounterHiRes();
        renderOnce (juce::jlimit (0.0, 0.25, (start - last) * 0.001));
        last = start; ++fpsN;
        if (start - fpsT >= 1000.0) { measuredFps.store ((float) (fpsN * 1000.0 / (start - fpsT))); fpsN = 0; fpsT = start; }
        const double spent = juce::Time::getMillisecondCounterHiRes() - start;
        const int sleepMs = (int) juce::jmax (1.0, 1000.0 / fps - spent);   // frame cap; always yields at least 1 ms
        wait (sleepMs);
    }
}

//==============================================================================================================
namespace
{
// Live output, scaled to the view (letterboxed).
class Canvas final : public juce::Component, private juce::Timer
{
public:
    Canvas (Stage& s) : stage (s) { startTimerHz (60); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 6.0f);
        const auto img = stage.engine().frame();
        if (img.isValid())
        {
            g.saveState();
            juce::Path clip; clip.addRoundedRectangle (r.reduced (1.0f), 5.0f); g.reduceClipRegion (clip);
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImage (img, fitRect (img.getWidth(), img.getHeight(), r));
            g.restoreState();
        }
        else
        {
            g.setColour (kDim); g.setFont (uiFont (12.0f));
            g.drawText ("starting...", getLocalBounds(), juce::Justification::centred);
        }
        g.setColour (kPri.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        if (stage.popOpen())
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.removeFromBottom (22.0f).reduced (6.0f, 2.0f), 4.0f);
            g.setColour (kSec); g.setFont (uiFont (10.5f, true));
            g.drawText ("POP-OUT WINDOW OPEN  -  CLICK INTO IT FOR THE PERFORMANCE KEYS", getLocalBounds().removeFromBottom (22), juce::Justification::centred);
        }
        if (hover && ! stage.popOpen())
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (getLocalBounds().removeFromBottom (22).reduced (6, 2).toFloat(), 4.0f);
            g.setColour (kSec); g.setFont (uiFont (10.5f, true));
            g.drawText ("DROP JPG / PNG / FONT HERE   -   DOUBLE-CLICK: POP OUT   -   RIGHT-CLICK: MENU", getLocalBounds().removeFromBottom (22), juce::Justification::centred);
        }
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { stage.togglePopOut(); }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) stage.showCanvasMenu (e.getScreenPosition()); }
private:
    void timerCallback() override
    {
        const int f = stage.engine().frameCounter();
        if (f != lastFrame) { lastFrame = f; repaint(); }
    }
    Stage& stage;
    int lastFrame = -1;
    bool hover = false;
};

// Category tabs + the modes of the browsed category + prev / next mode and palette.
class ModeStrip final : public juce::Component, private juce::Timer
{
public:
    ModeStrip (Stage& s) : stage (s), proc (s.processor()) { startTimerHz (10); }
    int curMode() const { return juce::jlimit (0, vz::kNumModes - 1, (int) proc.value (vz::pMode)); }
    int curPal() const  { return juce::jlimit (0, vz::kNumPalettes - 1, (int) proc.value (vz::pPalette)); }
    std::vector<int> modesOf (int cat) const { std::vector<int> v; for (int i = 0; i < vz::kNumModes; ++i) if (vz::kModes[i].cat == cat) v.push_back (i); return v; }
    juce::Rectangle<int> catRect (int c) const { const int W = getWidth(); return { W * c / vz::kNumCats, 0, W * (c + 1) / vz::kNumCats - W * c / vz::kNumCats, 24 }; }
    juce::Rectangle<int> modeRect (int i, int n) const { const int W = getWidth(); const int k = juce::jmax (n, 4); return { W * i / k, 30, W * (i + 1) / k - W * i / k, 28 }; }
    juce::Rectangle<int> row3() const { return { 0, 64, getWidth(), getHeight() - 64 }; }
    juce::Rectangle<int> btn (int which) const   // 0 mode prev, 1 mode next, 2 pal prev, 3 pal next, 4 random
    {
        auto r = row3(); const int half = r.getWidth() / 2;
        switch (which)
        {
            case 0: return { r.getX(), r.getY(), 28, r.getHeight() };
            case 1: return { r.getX() + half - 34, r.getY(), 28, r.getHeight() };
            case 2: return { r.getX() + half, r.getY(), 28, r.getHeight() };
            case 3: return { r.getRight() - 28 - 80, r.getY(), 28, r.getHeight() };
            default: return { r.getRight() - 76, r.getY(), 76, r.getHeight() };
        }
    }
    void paint (juce::Graphics& g) override
    {
        const int cm = curMode(), cc = vz::kModes[cm].cat;
        for (int c = 0; c < vz::kNumCats; ++c)
        {
            auto r = catRect (c).reduced (1, 0).toFloat();
            const bool browsing = c == browse, active = c == cc;
            g.setColour (browsing ? kTrack : kPanel); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (kPri.withAlpha (browsing ? 0.8f : 0.18f)); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (active ? kPri : kDim); g.setFont (uiFont (10.0f, true));
            g.drawText (vz::kCatNames[c], r, juce::Justification::centred, false);
            if (active) { g.setColour (kPri); g.fillRect (r.getX() + 8.0f, r.getBottom() - 3.0f, r.getWidth() - 16.0f, 2.0f); }
        }
        const auto ms = modesOf (browse);
        for (int i = 0; i < (int) ms.size(); ++i)
        {
            auto r = modeRect (i, (int) ms.size()).reduced (2, 0).toFloat();
            const bool sel = ms[(size_t) i] == cm;
            g.setColour (sel ? kPri : juce::Colour (0xff141414)); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (kPri.withAlpha (sel ? 1.0f : 0.3f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (sel ? kBg : kSec); g.setFont (uiFont (12.0f, true));
            g.drawFittedText (juce::String (vz::kModes[ms[(size_t) i]].name).toUpperCase(), r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
        }
        g.setFont (uiFont (14.0f, true));
        for (int b = 0; b < 4; ++b)
        {
            auto r = btn (b).toFloat().reduced (1.0f);
            g.setColour (kPanel); g.fillRoundedRectangle (r, 3.0f); g.setColour (kPri.withAlpha (0.3f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (kSec); g.drawText ((b % 2) == 0 ? "<" : ">", r, juce::Justification::centred);
        }
        {
            auto r = btn (4).toFloat().reduced (1.0f);
            g.setColour (kPanel); g.fillRoundedRectangle (r, 3.0f); g.setColour (kPri.withAlpha (0.3f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (kSec); g.setFont (uiFont (10.5f, true)); g.drawText ("RANDOM", r, juce::Justification::centred);
        }
        g.setFont (uiFont (11.0f, true));
        auto a = juce::Rectangle<int> (btn (0).getRight() + 8, row3().getY(), btn (1).getX() - btn (0).getRight() - 16, row3().getHeight());
        g.setColour (kDim); g.drawText ("MODE " + juce::String (cm + 1) + "/" + juce::String (vz::kNumModes), a, juce::Justification::centredLeft);
        g.setColour (kPri); g.drawText (juce::String (vz::kModes[cm].name).toUpperCase(), a.withTrimmedLeft (78), juce::Justification::centredLeft);
        auto b = juce::Rectangle<int> (btn (2).getRight() + 8, row3().getY(), btn (3).getX() - btn (2).getRight() - 16, row3().getHeight());
        g.setColour (kDim); g.drawText ("PALETTE", b, juce::Justification::centredLeft);
        g.setColour (kPri); g.drawText (juce::String (vz::kPalettes[curPal()].name).toUpperCase(), b.withTrimmedLeft (60), juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& panel = stage.getPanel();
        const auto pos = e.getPosition();
        const bool menu = e.mods.isPopupMenu();
        if (menu)
        {
            const char* id = pos.y >= row3().getY() && pos.x >= getWidth() / 2 ? "palette" : "mode";
            panel.showParamMenu (id, nullptr, panel.getLocalPoint (nullptr, e.getScreenPosition()));
            return;
        }
        for (int c = 0; c < vz::kNumCats; ++c) if (catRect (c).contains (pos)) { browse = c; repaint(); return; }
        const auto ms = modesOf (browse);
        for (int i = 0; i < (int) ms.size(); ++i) if (modeRect (i, (int) ms.size()).contains (pos)) { setParamGesture (proc, vz::pMode, (float) ms[(size_t) i]); repaint(); return; }
        if (btn (0).contains (pos)) step (vz::pMode, -1, vz::kNumModes);
        else if (btn (1).contains (pos)) step (vz::pMode, 1, vz::kNumModes);
        else if (btn (2).contains (pos)) step (vz::pPalette, -1, vz::kNumPalettes);
        else if (btn (3).contains (pos)) step (vz::pPalette, 1, vz::kNumPalettes);
        else if (btn (4).contains (pos))
        {
            const auto& fs = vz::presets::factory();
            proc.applyFactory (1 + juce::Random::getSystemRandom().nextInt ((int) fs.size() - 1));
            stage.note ("Random preset: " + proc.currentPresetName());
        }
    }
private:
    void step (int p, int d, int n)
    {
        const int v = ((int) proc.value (p) + d + n) % n;
        setParamGesture (proc, p, (float) v);
        if (p == vz::pMode) browse = vz::kModes[v].cat;
        repaint();
    }
    void timerCallback() override
    {
        const int m = curMode(), pal = curPal();
        if (m != lastMode) { if (lastMode >= 0 || browse < 0) browse = vz::kModes[m].cat; lastMode = m; repaint(); }
        if (browse < 0) browse = vz::kModes[m].cat;
        if (pal != lastPal) { lastPal = pal; repaint(); }
    }
    Stage& stage;
    VisualizerProcessor& proc;
    int browse = -1, lastMode = -1, lastPal = -1;
};

// Spectrum, band levels, beat lamp, tempo source.
class Meters final : public juce::Component, private juce::Timer
{
public:
    Meters (Stage& s) : stage (s) { startTimerHz (30); setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const auto& a = s;
        auto r = getLocalBounds();
        g.setColour (kPanel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kPri.withAlpha (0.18f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        r = r.reduced (10, 8);
        // spectrum
        auto sp = r.removeFromLeft (330); r.removeFromLeft (14);
        g.setColour (kDim); g.setFont (uiFont (9.5f, true)); g.drawText ("SPECTRUM", sp.removeFromTop (12), juce::Justification::centredLeft);
        const int n = vz::ana::Snapshot::kBands; const float bw = (float) sp.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, a.spec[(size_t) i]);
            const float hh = juce::jmax (1.0f, v * (float) sp.getHeight());
            g.setColour (kPri.withAlpha (0.35f + 0.65f * v));
            g.fillRect ((float) sp.getX() + (float) i * bw, (float) sp.getBottom() - hh, juce::jmax (1.0f, bw - 1.0f), hh);
        }
        // band bars
        auto bands = r.removeFromLeft (190); r.removeFromLeft (14);
        const float vals[4] = { a.level, a.bass, a.mid, a.high };
        const char* names[4] = { "LEVEL", "BASS", "MID", "HIGH" };
        for (int i = 0; i < 4; ++i)
        {
            auto row = bands.removeFromTop (bands.getHeight() / (4 - i));
            g.setColour (kDim); g.setFont (uiFont (9.5f, true)); g.drawText (names[i], row.removeFromLeft (40), juce::Justification::centredLeft);
            auto bar = row.reduced (0, 4).toFloat();
            g.setColour (kTrack); g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (i == 0 ? kPri : kSec); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, vals[i])), 2.0f);
        }
        // beat + tempo
        auto beat = r.removeFromLeft (44);
        const float bv = juce::jlimit (0.0f, 1.0f, a.beat);
        auto lamp = juce::Rectangle<float> (28.0f, 28.0f).withCentre (beat.toFloat().getCentre().translated (0, -6));
        g.setColour (kTrack); g.fillEllipse (lamp);
        g.setColour (kPri.withAlpha (bv)); g.fillEllipse (lamp.reduced (2.0f));
        g.setColour (kDim); g.setFont (uiFont (9.5f, true)); g.drawText ("BEAT", beat.removeFromBottom (12), juce::Justification::centred);
        r.removeFromLeft (10);
        g.setColour (kPri); g.setFont (uiFont (22.0f, true));
        g.drawText (a.bpm > 0 ? juce::String (a.bpm, 1) : juce::String ("---"), r.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (uiFont (10.0f, true));
        juce::String src = a.hostClock ? (a.playing ? "BPM  -  HOST TEMPO (PLAYING)" : "BPM  -  HOST TEMPO") : (a.bpm > 0 ? "BPM  -  DETECTED FROM AUDIO" : "BPM  -  LISTENING");
        g.drawText (src, r.removeFromTop (14), juce::Justification::centredLeft);
        auto dots = r.removeFromTop (16);
        const int bi = ((int) std::floor (a.beatPos)) & 3;
        for (int i = 0; i < 4; ++i)
        {
            auto d = juce::Rectangle<float> ((float) dots.getX() + (float) i * 18.0f, (float) dots.getCentreY() - 4.0f, 12.0f, 8.0f);
            g.setColour (i == bi ? kPri : kTrack); g.fillRoundedRectangle (d, 2.0f);
        }
        g.setColour (kDim.withAlpha (0.8f)); g.setFont (uiFont (9.5f));
        g.drawText (juce::String (stage.engine().fps(), 0) + " FPS  " + juce::String (stage.engine().renderMs(), 1) + " MS",
                    dots.withTrimmedLeft (84), juce::Justification::centredLeft);
    }
private:
    void timerCallback() override { s = stage.engine().snapshot(); repaint(); }
    Stage& stage;
    vz::ana::Snapshot s;
};
} // namespace

//==============================================================================================================
PopView::PopView (VisualizerProcessor& p, RenderEngine& e, Stage& s) : proc (p), engine (e), stage (s)
{
    setWantsKeyboardFocus (true);              // this window is not inside the plug-in window: keys here never come from the host
    setMouseClickGrabsKeyboardFocus (true);
    startTimerHz (60);
}

void PopView::timerCallback()
{
    const int f = engine.frameCounter();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (f != lastFrame) { lastFrame = f; repaint(); }
    if (stage.isFullscreen() && now - lastMouse > 2500.0) setMouseCursor (juce::MouseCursor::NoCursor);
}

void PopView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const auto img = engine.frame();
    if (img.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (img, fitRect (img.getWidth(), img.getHeight(), getLocalBounds().toFloat()));
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (osd.isNotEmpty() && now < osdUntil)
    {
        const float a = (float) juce::jlimit (0.0, 1.0, (osdUntil - now) / 400.0);
        auto r = getLocalBounds().removeFromTop (70).reduced (20, 14);
        g.setColour (juce::Colours::black.withAlpha (0.6f * a)); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kPri.withAlpha (a)); g.setFont (uiFont (22.0f, true)); g.drawText (osd, r, juce::Justification::centred);
    }
    if (! hasKeyboardFocus (true) && ! help)
    {
        auto r = getLocalBounds().removeFromBottom (34).reduced (getWidth() / 2 - 230, 6);
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (kSec); g.setFont (uiFont (11.0f, true));
        g.drawText ("CLICK HERE TO USE THE PERFORMANCE KEYS   -   F1: HELP", r, juce::Justification::centred);
    }
    if (help)
    {
        auto r = getLocalBounds().reduced (juce::jmax (20, getWidth() / 2 - 260), juce::jmax (20, getHeight() / 2 - 190));
        g.setColour (juce::Colours::black.withAlpha (0.82f)); g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (kPri.withAlpha (0.5f)); g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);
        r = r.reduced (22, 16);
        g.setColour (kPri); g.setFont (uiFont (17.0f, true)); g.drawText ("PERFORMANCE KEYS", r.removeFromTop (28), juce::Justification::centredLeft);
        const char* rows[][2] = {
            { "1 - 0, Q - P, A - L, Z - M", "fire setlist slot 1 - 36" },
            { "LEFT / RIGHT", "previous / next slot" },
            { "UP / DOWN", "next / previous mode" },
            { "HOLD ENTER", "strobe" },
            { "BACKSPACE / DELETE", "blackout on / off" },
            { "F11  or  CTRL+F", "fullscreen" },
            { "ESC", "leave fullscreen / close" },
            { "F1  or  ?", "this help" },
        };
        g.setFont (uiFont (13.0f));
        for (auto& rw : rows)
        {
            auto line = r.removeFromTop (24);
            g.setColour (kPri); g.drawText (rw[0], line.removeFromLeft (220), juce::Justification::centredLeft);
            g.setColour (kSec); g.drawText (rw[1], line, juce::Justification::centredLeft);
        }
        r.removeFromTop (8);
        g.setColour (kDim); g.setFont (uiFont (11.0f));
        g.drawFittedText ("Keys only work in this window. In the plug-in window your DAW keeps Space and its other shortcuts. "
                          "Set up the setlist in the PERFORM tab.", r, juce::Justification::topLeft, 3);
    }
}

void PopView::fireSlot (int i)
{
    if (i < 0 || i >= vz::kNumSlots) return;
    const auto name = proc.getExtras().slots[(size_t) i];
    const juce::String key = juce::String::charToString (vz::kSlotKeys[i]);
    if (name.isEmpty()) { showOsd (key + "  -  EMPTY SLOT"); return; }
    if (proc.applyPreset (name)) { proc.activeSlot.store (i); showOsd (key + "  " + displayName (name).toUpperCase()); }
    else showOsd (key + "  -  PRESET NOT FOUND");
}

bool PopView::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::F11Key || (mods.isCommandDown() && (code == 'F' || code == 'f'))) { stage.setFullscreen (! stage.isFullscreen()); return true; }
    if (code == juce::KeyPress::escapeKey)
    {
        if (help) { help = false; repaint(); }
        else if (stage.isFullscreen()) stage.setFullscreen (false);
        else juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Stage> (&stage)] { if (sp != nullptr) sp->closePopOut(); });
        return true;
    }
    if (code == juce::KeyPress::F1Key || k.getTextCharacter() == '?') { help = ! help; repaint(); return true; }
    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown()) return false;
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const int cur = proc.activeSlot.load(), d = code == juce::KeyPress::rightKey ? 1 : -1;
        const auto slots = proc.getExtras().slots;
        int i = cur;
        for (int tries = 0; tries < vz::kNumSlots; ++tries)
        {
            i = (i + d + vz::kNumSlots) % vz::kNumSlots;
            if (slots[(size_t) i].isNotEmpty()) { fireSlot (i); break; }
        }
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int m = ((int) proc.value (vz::pMode) + (code == juce::KeyPress::upKey ? 1 : -1) + vz::kNumModes) % vz::kNumModes;
        setParamGesture (proc, vz::pMode, (float) m);
        showOsd (juce::String (vz::kModes[m].name).toUpperCase());
        return true;
    }
    if (code == juce::KeyPress::returnKey)
    {
        if (! strobeHeld) { strobeHeld = true; setParamGesture (proc, vz::pStrobe, 1.0f); }
        return true;
    }
    if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey)
    {
        const bool on = proc.value (vz::pBlackout) < 0.5f;
        setParamGesture (proc, vz::pBlackout, on ? 1.0f : 0.0f);
        showOsd (on ? "BLACKOUT" : "BLACKOUT OFF");
        return true;
    }
    juce::juce_wchar c = k.getTextCharacter();
    if (c == 0) c = (juce::juce_wchar) code;
    c = juce::CharacterFunctions::toUpperCase (c);
    for (int i = 0; i < vz::kNumSlots; ++i)
        if (vz::kSlotKeys[i] == (char) c && c < 128) { fireSlot (i); return true; }
    return false;
}

bool PopView::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && strobeHeld && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey))
    {
        strobeHeld = false; setParamGesture (proc, vz::pStrobe, 0.0f);
        return true;
    }
    return false;
}

void PopView::focusLost (FocusChangeType)
{
    if (strobeHeld) { strobeHeld = false; setParamGesture (proc, vz::pStrobe, 0.0f); }
    repaint();
}

void PopView::mouseMove (const juce::MouseEvent&) { lastMouse = juce::Time::getMillisecondCounterHiRes(); setMouseCursor (juce::MouseCursor::NormalCursor); }
void PopView::mouseDoubleClick (const juce::MouseEvent&) { stage.setFullscreen (! stage.isFullscreen()); }
void PopView::mouseDown (const juce::MouseEvent& e)
{
    lastMouse = juce::Time::getMillisecondCounterHiRes(); setMouseCursor (juce::MouseCursor::NormalCursor);
    if (! e.mods.isPopupMenu()) { repaint(); return; }
    juce::PopupMenu m;
    juce::Component::SafePointer<PopView> sp (this);
    juce::Component::SafePointer<Stage> st (&stage);
    m.addItem (juce::PopupMenu::Item (stage.isFullscreen() ? "Leave fullscreen" : "Fullscreen").setAction ([st] { if (st != nullptr) st->setFullscreen (! st->isFullscreen()); }));
    m.addItem (juce::PopupMenu::Item ("Keys help").setTicked (help).setAction ([sp] { if (sp != nullptr) { sp->help = ! sp->help; sp->repaint(); } }));
    m.addSeparator();
    m.addItem (juce::PopupMenu::Item ("Close window").setAction ([st] { if (st != nullptr) juce::MessageManager::callAsync ([st] { if (st != nullptr) st->closePopOut(); }); }));
    m.setLookAndFeel (&stage.getPanel().look);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

PopWindow::PopWindow (VisualizerProcessor& p, RenderEngine& e, Stage& s)
    : juce::DocumentWindow ("CMPLXTY Visualizer", juce::Colours::black, juce::DocumentWindow::allButtons, false), view (p, e, s), stage (s)
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (&view, false);
    setResizable (true, false);
    setResizeLimits (320, 180, 8192, 8192);
}
void PopWindow::closeButtonPressed()
{
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Stage> (&stage)] { if (sp != nullptr) sp->closePopOut(); });
}

//==============================================================================================================
Stage::Stage (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn), eng (p)
{
    canvas = std::make_unique<Canvas> (*this);
    strip = std::make_unique<ModeStrip> (*this);
    meters = std::make_unique<Meters> (*this);
    for (auto* c : { canvas.get(), strip.get(), meters.get() }) addAndMakeVisible (*c);
    rebuildAssets();
    eng.start();
    startTimerHz (10);
}

Stage::~Stage()
{
    stopTimer();
    if (fullscreen) juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
    pop.reset();
    eng.stop();
}

void Stage::paint (juce::Graphics&) {}
void Stage::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver) return;
    auto r = canvas->getBounds().toFloat().reduced (3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kPri); g.drawRoundedRectangle (r, 6.0f, 2.0f);
    g.setFont (uiFont (20.0f, true)); g.drawText ("DROP TO ADD", r, juce::Justification::centred);
}

void Stage::resized()
{
    auto r = getLocalBounds();
    canvas->setBounds (r.removeFromTop (450));
    r.removeFromTop (8);
    strip->setBounds (r.removeFromTop (88));
    r.removeFromTop (8);
    meters->setBounds (r);
}

bool Stage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isImportable (f)) return true;
    return false;
}

void Stage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    note (importFiles (proc, files));
}

void Stage::rebuildAssets()
{
    builtVersion = proc.extrasVersion();
    const auto ex = proc.getExtras();
    auto a = std::make_shared<vz::Assets>();
    std::map<juce::String, juce::Image> keep;
    for (auto& im : ex.images)
    {
        const juce::String key = im.name + ":" + juce::String ((juce::int64) im.data.getSize()) + ":"
                               + juce::String::toHexString ((juce::int64) quickHash (im.data));
        juce::Image img;
        if (auto it = imageCache.find (key); it != imageCache.end()) img = it->second;
        else
        {
            auto src = juce::ImageFileFormat::loadFrom (im.data.getData(), im.data.getSize());
            if (src.isValid())
            {
                const float s = juce::jmin (1.0f, 1280.0f / (float) juce::jmax (src.getWidth(), src.getHeight()));
                const int w = juce::jmax (1, juce::roundToInt ((float) src.getWidth() * s)), h = juce::jmax (1, juce::roundToInt ((float) src.getHeight() * s));
                img = vz::makeSoftwareImage (w, h);
                juce::Graphics g (img);
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (src, 0, 0, w, h, 0, 0, src.getWidth(), src.getHeight());
            }
        }
        if (img.isValid()) { keep[key] = img; a->images.push_back (img); }
    }
    imageCache = std::move (keep);
    a->selectedImage = juce::jlimit (0, juce::jmax (0, (int) a->images.size() - 1), ex.selectedImage);
    a->lines = ex.lines;
    if (ex.font.getSize() > 0)
    {
        const auto key = ex.fontName + juce::String ((juce::int64) ex.font.getSize());
        if (key != fontKey || fontCache == nullptr) { fontCache = juce::Typeface::createSystemTypefaceFor (ex.font.getData(), ex.font.getSize()); fontKey = key; }
        a->textFont = fontCache;
    }
    a->uiBold = uiTypeface (true);
    for (int i = 0; i < vz::kMaxTiles; ++i)
        if (ex.tiles[(size_t) i].isNotEmpty())
            a->tileValid[(size_t) i] = proc.resolvePreset (ex.tiles[(size_t) i], a->tiles[(size_t) i]);
    eng.setAssets (a);
}

void Stage::timerCallback()
{
    if (proc.extrasVersion() != builtVersion) rebuildAssets();
    const bool popVisible = pop != nullptr && pop->isShowing();
    eng.setActive (isShowing() || popVisible);
    if (popVisible && pop->view.getHeight() > 0) eng.setAspect ((float) pop->view.getWidth() / (float) pop->view.getHeight());
    else eng.setAspect (16.0f / 9.0f);
}

void Stage::step (int n, double dt)
{
    if (proc.extrasVersion() != builtVersion) rebuildAssets();
    eng.setAspect (16.0f / 9.0f);
    for (int i = 0; i < n; ++i) eng.renderOnce (dt);
    canvas->repaint(); meters->repaint();
}

void Stage::togglePopOut() { if (pop != nullptr) closePopOut(); else openPopOut (false); }

void Stage::openPopOut (bool full)
{
    if (pop == nullptr)
    {
        pop = std::make_unique<PopWindow> (proc, eng, *this);
        auto area = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()) ;
        auto user = area != nullptr ? area->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 800);
        const int w = juce::jmin (960, user.getWidth() - 40), h = w * 9 / 16;
        pop->setBounds (juce::Rectangle<int> (w, h + 30).withCentre (user.getCentre()));
        pop->addToDesktop (pop->getDesktopWindowStyleFlags());
        pop->setVisible (true);       // shown without activation: the keyboard stays with the host until you click into it
        pop->toFront (false);
        note ("Pop-out open: click into it for the performance keys (F1 there shows them)");
    }
    if (full) setFullscreen (true);
    canvas->repaint();
}

void Stage::closePopOut()
{
    if (pop == nullptr) return;
    if (fullscreen) { juce::Desktop::getInstance().setKioskModeComponent (nullptr, false); fullscreen = false; }
    if (proc.value (vz::pStrobe) > 0.5f) setParamGesture (proc, vz::pStrobe, 0.0f);
    pop.reset();
    canvas->repaint();
    hostkeys::releaseLater (*this);
}

void Stage::setFullscreen (bool on)
{
    if (on && pop == nullptr) openPopOut (false);
    if (pop == nullptr || on == fullscreen) return;
    fullscreen = on;
    juce::Desktop::getInstance().setKioskModeComponent (on ? pop.get() : nullptr, false);
    if (on) { pop->toFront (true); pop->view.grabKeyboardFocus(); }
    else pop->view.setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Stage::loadImageDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Load images (JPG / PNG)", juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.jpg;*.jpeg;*.png");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [sp = juce::Component::SafePointer<Stage> (this)] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            juce::StringArray paths; for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
            if (! paths.isEmpty()) sp->note (importFiles (sp->proc, paths));
            hostkeys::releaseLater (*sp);
        });
}

void Stage::loadFontDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a font for the text (TTF / OTF)", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.ttf;*.otf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [sp = juce::Component::SafePointer<Stage> (this)] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            if (fc.getResult().existsAsFile()) sp->note (importFiles (sp->proc, { fc.getResult().getFullPathName() }));
            hostkeys::releaseLater (*sp);
        });
}

void Stage::saveFrameDialog()
{
    const auto img = eng.frame();
    if (! img.isValid()) return;
    auto copy = img.createCopy();
    chooser = std::make_unique<juce::FileChooser> ("Save frame as PNG", juce::File::getSpecialLocation (juce::File::userPicturesDirectory).getChildFile ("cmplxty-visualizer.png"), "*.png");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [sp = juce::Component::SafePointer<Stage> (this), copy] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            auto f = fc.getResult();
            if (f != juce::File())
            {
                f = f.withFileExtension ("png"); f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat png;
                sp->note (os.openedOk() && png.writeImageToStream (copy, os) ? "Saved " + f.getFileName() : juce::String ("Could not save the frame"));
            }
            hostkeys::releaseLater (*sp);
        });
}

void Stage::showCanvasMenu (juce::Point<int> screenPos)
{
    std::vector<PopupOverlay::Item> v;
    juce::Component::SafePointer<Stage> sp (this);
    v.push_back ({ pop != nullptr ? "Close &pop-out window\tdouble-click" : "&Pop out window\tdouble-click", [sp] { if (sp != nullptr) sp->togglePopOut(); } });
    v.push_back ({ "&Fullscreen\tF11 in pop-out", [sp] { if (sp != nullptr) sp->setFullscreen (true); } });
    v.push_back ({ "Load &image...\tor drop", [sp] { if (sp != nullptr) sp->loadImageDialog(); }, proc.getExtras().images.size() < (size_t) vz::kMaxImages, false, true });
    v.push_back ({ "Load f&ont for text...", [sp] { if (sp != nullptr) sp->loadFontDialog(); } });
    v.push_back ({ "&Next mode", [sp] { if (sp != nullptr) setParamGesture (sp->proc, vz::pMode, (float) (((int) sp->proc.value (vz::pMode) + 1) % vz::kNumModes)); }, true, false, true });
    v.push_back ({ "P&revious mode", [sp] { if (sp != nullptr) setParamGesture (sp->proc, vz::pMode, (float) (((int) sp->proc.value (vz::pMode) + vz::kNumModes - 1) % vz::kNumModes)); } });
    v.push_back ({ "Ran&dom preset", [sp] { if (sp != nullptr) { sp->proc.applyFactory (1 + juce::Random::getSystemRandom().nextInt ((int) vz::presets::factory().size() - 1)); sp->note ("Random preset: " + sp->proc.currentPresetName()); } } });
    v.push_back ({ "&Save frame as PNG...", [sp] { if (sp != nullptr) sp->saveFrameDialog(); }, true, false, true });
    panel.popupOverlay().show (panel.getLocalPoint (nullptr, screenPos), std::move (v));
}
} // namespace vzui
