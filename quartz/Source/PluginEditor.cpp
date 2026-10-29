#include "PluginEditor.h"

using namespace qz;
using ui::Fonts;

static const char* U (const char* s) { return s; }
static juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

// ------------------------------------------------------------------ backdrop (facets, panels, static labels)
void QuartzEditor::Backdrop::paint (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (juce::Colour (0xff0b0b0b));
    {   // background facets
        Lcg r (5); const int cols = 14, rows = 10; const float W = 1100.0f / cols, H = 720.0f / rows;
        juce::Point<float> pts[rows + 1][cols + 1];
        for (int j = 0; j <= rows; ++j)
            for (int i = 0; i <= cols; ++i)
            {
                const float x = i * W + ((i && i < cols) ? (r() - 0.5f) * W * 0.7f : 0.0f);
                const float y = j * H + ((j && j < rows) ? (r() - 0.5f) * H * 0.7f : 0.0f);
                pts[j][i] = { x, y };
            }
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < cols; ++i)
            {
                const auto a = pts[j][i], b = pts[j][i + 1], c = pts[j + 1][i], d = pts[j + 1][i + 1];
                const juce::Point<float> tri[2][3] = { { a, b, c }, { b, d, c } };
                for (auto& t : tri)
                {
                    const float v = 9.0f + r() * 9.0f;
                    Path p; p.addTriangle (t[0], t[1], t[2]);
                    g.setColour (grey (v)); g.fillPath (p);
                    g.setColour (juce::Colour (0xff050505)); g.strokePath (p, juce::PathStrokeType (0.8f));
                }
            }
    }
    // header
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1b1b), 0, 0, juce::Colour (0xff0e0e0e), 0, 44, false));
    g.fillRect (0, 0, 1100, 44);
    g.setColour (juce::Colour (0xff555555)); g.fillRect (0, 43, 1100, 1);
    {   // logo mark
        const float s = 30.0f / 40.0f, ox = 22.0f, oy = 7.0f;
        auto Q = [&] (float x, float y) { return juce::Point<float> (ox + x * s, oy + y * s); };
        auto poly = [&] (std::initializer_list<juce::Point<float>> pts, juce::Colour fill)
        { Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath (q); else p.lineTo (q); first = false; } p.closeSubPath(); g.setColour (fill); g.fillPath (p); };
        poly ({ Q (20, 2), Q (36, 14), Q (20, 38), Q (4, 14) }, juce::Colour (0xff2a2a2a));
        poly ({ Q (20, 2), Q (36, 14), Q (20, 16) }, juce::Colour (0xffd8d8d8));
        poly ({ Q (20, 2), Q (4, 14), Q (20, 16) }, juce::Colour (0xff9a9a9a));
        poly ({ Q (4, 14), Q (20, 16), Q (20, 38) }, juce::Colour (0xff4c4c4c));
        poly ({ Q (36, 14), Q (20, 16), Q (20, 38) }, juce::Colour (0xff777777));
        Path o; o.startNewSubPath (Q (20, 2)); o.lineTo (Q (36, 14)); o.lineTo (Q (20, 38)); o.lineTo (Q (4, 14)); o.closeSubPath();
        g.setColour (juce::Colour (0xffeeeeee)); g.strokePath (o, juce::PathStrokeType (1.4f * s));
    }
    {
        const auto f = Fonts::title (23.0f, 9.0f);
        text (g, "QUARTZ", { 64, 0, 300, 44 }, f, juce::Colours::white, juce::Justification::centredLeft);
        const float x = 64.0f + textWidth (f, "QUARTZ") + 9.0f + 12.0f + 6.0f;
        g.setColour (juce::Colour (0xff555555)); g.fillRect (x, 14.0f, 1.0f, 16.0f);
        text (g, "VOCAL ALCHEMY", { x + 15, 0, 300, 44 }, Fonts::ui (9.5f, 2.6f), juce::Colour (0xff8a8a8a), juce::Justification::centredLeft);
    }
    // Quartz panels
    for (float px : { 24.0f, 746.0f })
        cutPanel (g, { px, 86.0f, 330.0f, 366.0f }, 16.0f, juce::Colour::fromFloatRGBA (32 / 255.0f, 32 / 255.0f, 32 / 255.0f, 0.92f), juce::Colour::fromFloatRGBA (14 / 255.0f, 14 / 255.0f, 14 / 255.0f, 0.92f), juce::Colour (0xff3a3a3a));
    text (g, "SPECTRUM " + utf8 ("\xC2\xB7") + " INTENSITY", { 354, 94, 392, 14 }, Fonts::ui (9.5f, 3.0f), juce::Colour (0xff888888));
    text (g, "DRIVE TYPE", { 970, 349, 96, 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffaaaaaa));
    // ALTER section
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff171717), 0, 454, juce::Colour (0xff0c0c0c), 0, 720, false));
    g.fillRect (0, 454, 1100, 266);
    g.setColour (juce::Colour (0xff8a8a8a)); g.fillRect (0, 454, 1100, 1);
    cutPanel (g, { 24.0f, 501.0f, 1052.0f, 196.0f }, 18.0f, juce::Colour::fromFloatRGBA (30 / 255.0f, 30 / 255.0f, 30 / 255.0f, 0.9f), juce::Colour::fromFloatRGBA (12 / 255.0f, 12 / 255.0f, 12 / 255.0f, 0.9f), juce::Colour (0xff3a3a3a));
    cutPanel (g, { 407.0f, 529.0f, 138.0f, 141.0f }, 10.0f, juce::Colour (0xff161616), juce::Colour (0xff0c0c0c), juce::Colour (0xff3c3c3c));
    text (g, "MODE", { 40, 542, 100, 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffaaaaaa), juce::Justification::centredLeft);
    text (g, "KEY / SCALE", { 948, 555, 112, 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffaaaaaa));
    text (g, "SPEED = RETUNE", { 948, 632, 112, 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xff777777));
    text (g, "PITCH " + utf8 ("\xC3\x97") + " FORMANT", { 553, 646, 116, 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffaaaaaa));
}

void QuartzEditor::Footer::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff2a2a2a)); g.fillRect (0, 0, getWidth(), 1);
    const auto f = Fonts::ui (9.0f, 2.0f);
    ui::text (g, utf8 ("QUARTZ v" QUARTZ_VERSION_STRING " \xC2\xB7 VST3 \xC2\xB7 44.1/48/96 kHz"), { 24, 0, 380, (float) getHeight() }, f, juce::Colour (0xff666666), juce::Justification::centredLeft);
    ui::text (g, hint, { 330, 0, 440, (float) getHeight() }, Fonts::ui (9.5f, 0.6f), juce::Colour (0xff9a9a9a));
    ui::text (g, right, { getWidth() - 24.0f - 300.0f, 0, 300, (float) getHeight() }, f, juce::Colour (0xff666666), juce::Justification::centredRight);
}

// ------------------------------------------------------------------ editor
QuartzEditor::QuartzEditor (QuartzProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ctx { p, {}, {} }
{
    using namespace qzui;
    setLookAndFeel (&laf);
    ctx.hint = [this] (const juce::String& s) { footer.hint = s; footer.repaint(); };
    footer.ctx = &ctx;

    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);
    root.addAndMakeVisible (backdrop); backdrop.setBounds (0, 0, kW, kH); backdrop.setBufferedToImage (true);

    // ---- Quartz knobs (left grid, right grid)
    auto knob = [&] (int pid, const char* label, int x, int y, int s = 74, int w = 104, KnobStyle st = KnobStyle::Facet)
    { auto* k = new KnobComp (ctx, pid, label, s, w, st); return add (k, x, y, w, k->totalHeight()); };
    const int lx = 28, ly = 93, rx = 750;
    const struct { int pid; const char* l; } left[9] = { { pGate, "GATE" }, { pBits, "BITS" }, { pRate, "RATE" }, { pDrive, "DRIVE" }, { pGrit, "GRIT" }, { pTone, "TONE" }, { pVocode, "VOCODE" }, { pCNote, "CARRIER" }, { pLowCut, "LOW CUT" } };
    const struct { int pid; const char* l; } right[8] = { { pHighCut, "HIGH CUT" }, { pDouble, "DOUBLE" }, { pWiden, "WIDEN" }, { pDelay, "DELAY" }, { pFb, "FEEDBACK" }, { pReverb, "REVERB" }, { pDecay, "DECAY" }, { pMix, "MIX" } };
    for (int i = 0; i < 9; ++i) knob (left[i].pid, left[i].l, lx + (i % 3) * 108, ly + (i / 3) * 118 + 6);
    for (int i = 0; i < 8; ++i) knob (right[i].pid, right[i].l, rx + (i % 3) * 108, ly + (i / 3) * 118 + 6);
    add (new SegGroup (ctx, pDType, 90, 22, 3, 10, 8, 1.5f), 973, 372, 90, 72);

    // ---- centre: gem, input / output
    gem = add (new KnobComp (ctx, pIntensity, "INTENSITY", 0, 392, KnobStyle::Gem), 354, 86, 392, 366);
    knob (pIn, "INPUT", 358, 332, 60, 84);
    knob (pOut, "OUTPUT", 658, 332, 60, 84);
    meterIn = add (new MeterComp(), 352, 428, 90, 12); meterIn->label = "IN";
    meterOut = add (new MeterComp(), 652, 428, 90, 12); meterOut->label = "OUT";

    // ---- ALTER body (positions follow the flex layout of the design)
    add (new SegGroup (ctx, pMode, 100, 28, 6, 8, 6, 0.9f), 40, 560, 100, 96);
    knob (pPitch, "PITCH", 148, 538, 92, 96);
    add (new LinkComp (ctx), 251, 562, 44, 73);
    knob (pFormant, "FORMANT", 303, 538, 92, 96);
    knob (pGender, "GENDER", 411, 536, 100, 128, KnobStyle::Gender);
    add (new XYPad (ctx), 553, 541, 116, 100);
    const int kx[4] = { 676, 742, 808, 874 };
    knob (pADrive, "DRIVE", kx[0], 555, 60, 66); knob (pSpeed, "SPEED", kx[1], 555, 60, 66);
    knob (pAMix, "MIX", kx[2], 555, 60, 66); knob (pAOut, "OUTPUT", kx[3], 555, 60, 66);
    add (new DropComp (ctx, pKey), 951, 572, 106, 24);
    add (new DropComp (ctx, pScale), 951, 601, 106, 24);

    // ---- on/off overlays, titles, preset bars
    add (new OffOverlay (ctx, pQon), 24, 86, 1052, 366);
    add (new OffOverlay (ctx, pAon), 24, 501, 1052, 196);
    add (new SectionTitle (ctx, pQon, "QUARTZ ENGINE", {}, 0), 24 - 16, 44, 320 + 16, 40);
    add (new SectionTitle (ctx, pAon, "ALTER", "VOICE MANIPULATOR", 1), 24 - 16, 455, 420 + 16, 40);
    add (new PresetBar (ctx, secQuartz, false), 1076 - PresetBar::totalW, 51, PresetBar::totalW, 26);
    add (new PresetBar (ctx, secAlter, true), 1076 - PresetBar::totalW, 462, PresetBar::totalW, 26);

    // ---- header buttons
    auto hbtn = [&] (const char* txt, int right, const juce::String& hint)
    {
        auto* b = new TextButtonComp(); b->text = txt; b->font = 10.0f; b->spacing = 1.5f; b->bg = juce::Colours::transparentBlack; b->ctx = &ctx; b->hint = hint;
        const int w = juce::roundToInt (ui::textWidth (Fonts::ui (10.0f, 1.5f), txt)) + 22;
        add (b, right - w, 10, w, 24); return b;
    };
    btnBypass = hbtn ("BYPASS OFF", 1078, "Bypass the whole plugin (latency-matched, click-free)");
    btnUndo = hbtn ("UNDO", 1078 - 98 - 8, "Undo the last change");
    btnAB = hbtn ("A / B", 1078 - 98 - 8 - 56 - 8, "Compare two settings: click to switch between A and B (B starts as a copy of A)");
    btnBypass->onClick = [this] { ctx.toggleGesture (pBypass); };
    btnUndo->onClick = [this] { proc.undo(); };
    btnAB->onClick = [this] { proc.toggleAB(); };
    bypassAtt = std::make_unique<juce::ParameterAttachment> (ctx.P (pBypass), [this] (float v)
    {
        btnBypass->text = v > 0.5f ? "BYPASS ON" : "BYPASS OFF"; btnBypass->on = v < 0.5f; btnBypass->repaint();
    });
    bypassAtt->sendInitialUpdate();

    root.addAndMakeVisible (footer); footer.setBounds (0, 696, kW, 24);

    // ---- update notice (1.0.2): hidden unless UpdateCheck finds a newer version
    banner = new qzui::UpdateBanner (ctx); owned.add (banner); root.addChildComponent (banner);
    banner->onDismiss = [this] { banner->setVisible (false); };
    banner->onSkipVersion = [] (const juce::String& v) { auto s = qz::upd::Settings::load(); s.skipVersion = v; s.save(); };
    versionBtn = new VersionButton(); owned.add (versionBtn); root.addAndMakeVisible (versionBtn);
    versionBtn->ctx = &ctx; versionBtn->setBounds (20, 696, 230, 24); versionBtn->onClick = [this] { showUpdateMenu(); };

    for (int i = 0; i < 1024; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * ui::kPi * (float) i / 1023.0f);
    proc.historyChanged.addChangeListener (this);
    changeListenerCallback (nullptr);

    constrainer.setFixedAspectRatio ((double) kW / (double) kH);
    constrainer.setSizeLimits (660, 432, 2200, 1440);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const int w = juce::jlimit (660, 2200, proc.editorWidth);
    setSize (w, juce::roundToInt ((double) w * kH / kW));
    // 1.0.3: the editor never keeps the keyboard (see HostKeys.h) - Space & co. stay with the host's transport.
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    startTimerHz (30);
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<QuartzEditor> (this)] { if (sp != nullptr && ! sp->ctx.demo) sp->startUpdateCheck(); });
}

void QuartzEditor::startUpdateCheck (bool force, const juce::String& urlOverride)
{
    checker.start ([this, force] (const qz::upd::Result& r)
    {
        if (force && ! qz::upd::shouldNotify (r, qz::upd::Settings::load(), true))
            footer.hint = r.ok ? juce::String ("Quartz ") + QUARTZ_VERSION_STRING + " is up to date" : juce::String ("Could not reach the update server");
        footer.repaint();
        showUpdateNotice (r, force);
    }, force, urlOverride);
}

void QuartzEditor::showUpdateNotice (const qz::upd::Result& r, bool ignoreSkip)
{
    if (qz::upd::shouldNotify (r, qz::upd::Settings::load(), ignoreSkip))
    {
        banner->show (r.info);
        banner->setTopLeftPosition (836 - banner->getWidth(), 9);
    }
}

void QuartzEditor::showUpdateMenu()
{
    using qzui::MenuEntry;
    const bool on = qz::upd::Settings::load().enabled;
    std::vector<MenuEntry> v;
    v.push_back ({ "Check for &updates automatically", [this, on] { auto s = qz::upd::Settings::load(); s.enabled = ! on; s.save(); footer.hint = s.enabled ? "Update checks on" : "Update checks off: Quartz never contacts the network"; footer.repaint(); }, true, on, false });
    v.push_back ({ "Check &now", [this] { footer.hint = "Checking..."; footer.repaint(); startUpdateCheck (true); }, true, false, false });
    qzui::showEntryMenu (*versionBtn, std::move (v));
}

QuartzEditor::~QuartzEditor()
{
    stopTimer();
    checker.cancelAndWait();
    juce::PopupMenu::dismissAllActiveMenus();
    qzui::hostMenuKeepAlive().reset();
    proc.historyChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void QuartzEditor::resized()
{
    const float sc = (float) getWidth() / (float) kW;
    root.setTransform (juce::AffineTransform::scale (sc));
    proc.editorWidth = getWidth();
}

void QuartzEditor::parentHierarchyChanged()
{
    // MinGW builds of JUCE default to the GDI software renderer (see patches/juce-8.0.9-mingw.patch); nothing to do.
}

bool QuartzEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown())
    {
        if (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z') { if (k.getModifiers().isShiftDown()) proc.redo(); else proc.undo(); return true; }
        if (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y') { proc.redo(); return true; }
    }
    return false;
}

void QuartzEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (btnUndo) { btnUndo->enabled = proc.canUndo(); btnUndo->repaint(); }
    if (btnAB) { btnAB->on = proc.abSlot == 1; btnAB->text = proc.abSlot == 1 ? "B" : "A / B"; btnAB->repaint(); }
}

void QuartzEditor::updateSpectrum()
{
    if (ctx.demo)
    {
        ui::Lcg r (3);
        for (int i = 0; i < 48; ++i)
        {
            const float t = (float) i / 48.0f;
            float v = 0.15f + 0.65f * std::exp (-std::pow ((t - 0.22f) / 0.22f, 2.0f)) + 0.28f * std::exp (-std::pow ((t - 0.55f) / 0.1f, 2.0f)) + 0.18f * std::exp (-std::pow ((t - 0.78f) / 0.06f, 2.0f));
            v *= 0.7f + 0.5f * r(); ctx.spec[i] = std::min (1.0f, v);
        }
        return;
    }
    const int wp = proc.scopePos.load (std::memory_order_acquire);
    for (int i = 0; i < 1024; ++i)
        fftData[(size_t) i] = proc.scope[(size_t) ((wp - 1024 + i) & (QuartzProcessor::kScope - 1))].load (std::memory_order_relaxed) * window[(size_t) i];
    std::fill (fftData.begin() + 1024, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const double sr = std::max (8000.0, proc.getSampleRate());
    const double binHz = sr / 1024.0;
    for (int b = 0; b < 48; ++b)
    {
        const double f = 70.0 * std::pow (14000.0 / 70.0, (double) b / 47.0);
        const int lo = std::max (1, (int) std::floor (f * 0.94 / binHz)), hi = std::min (511, std::max (lo, (int) std::ceil (f * 1.06 / binHz)));
        float m = 0.0f; for (int k = lo; k <= hi; ++k) m = std::max (m, fftData[(size_t) k]);
        const float db = 20.0f * std::log10 (m / 256.0f + 1.0e-9f) + 12.0f * (float) b / 47.0f;
        const float v = juce::jlimit (0.0f, 1.0f, (db + 75.0f) / 55.0f);
        const float shown = 0.3f + 0.7f * v;
        ctx.spec[b] = std::max (shown, ctx.spec[b] * 0.88f);
    }
}

void QuartzEditor::timerCallback()
{
    auto level = [] (float p) { const float db = 20.0f * std::log10 (std::max (p, 1.0e-6f)); return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    for (int c = 0; c < 2; ++c)
    {
        const float i = level (proc.inPeak[c].exchange (0.0f)), o = level (proc.outPeak[c].exchange (0.0f));
        smoothIn[c] = std::max (i, smoothIn[c] * 0.86f); smoothOut[c] = std::max (o, smoothOut[c] * 0.86f);
    }
    meterIn->setLevels (smoothIn[0], smoothIn[1]); meterOut->setLevels (smoothOut[0], smoothOut[1]);
    if (ctx.demo) { meterIn->setLevels (0.62f, 0.55f); meterOut->setLevels (0.7f, 0.64f); }
    updateSpectrum();
    gem->repaint();
    if (tick++ % 10 == 0)
    {
        const double sr = std::max (8000.0, proc.getSampleRate());
        footer.right = juce::String ("LATENCY ") + juce::String (1000.0 * proc.latencySamples() / sr, 1) + " ms    " + utf8 ("\xC2\xB7") + "    CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + " %";
        footer.repaint();
    }
}
