#include "PluginEditor.h"
#include "BinaryData.h"
#include "Presets.h"

namespace
{
juce::Colour bg() { return juce::Colour(0xff0c0c0c); }
juce::Colour panel() { return juce::Colour(0xff161616); }
juce::Colour ink() { return juce::Colour(0xffe8e8e8); }
juce::Colour dim() { return juce::Colour(0xff8a8a8a); }
juce::Colour line() { return juce::Colour(0xff3a3a3a); }
} // namespace

FeverLook::FeverLook()
{
    face = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf,
                                                   BinaryData::BarlowRegular_ttfSize);
    faceB = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf,
                                                    BinaryData::BarlowSemiBold_ttfSize);
    setDefaultSansSerifTypeface(face);
    setColour(juce::ResizableWindow::backgroundColourId, bg());
    setColour(juce::Label::textColourId, ink());
    setColour(juce::ToggleButton::textColourId, ink());
    setColour(juce::ToggleButton::tickColourId, ink());
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff666666));
    setColour(juce::ComboBox::backgroundColourId, panel());
    setColour(juce::ComboBox::textColourId, ink());
    setColour(juce::ComboBox::outlineColourId, line());
    setColour(juce::ComboBox::arrowColourId, dim());
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff141414));
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a2a2a));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, panel());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, panel());
    setColour(juce::TextEditor::textColourId, ink());
    setColour(juce::TextEditor::outlineColourId, line());
    setColour(juce::FileChooserDialogBox::titleTextColourId, ink());
    setColour(juce::Slider::textBoxTextColourId, ink());
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101010));
    setColour(juce::Slider::textBoxOutlineColourId, line());
}

juce::Font FeverLook::getLabelFont(juce::Label &) { return juce::Font(juce::FontOptions(face).withHeight(13.f)); }
juce::Font FeverLook::getComboBoxFont(juce::ComboBox &)
{
    return juce::Font(juce::FontOptions(face).withHeight(14.f));
}
juce::Font FeverLook::getTextButtonFont(juce::TextButton &, int)
{
    return juce::Font(juce::FontOptions(faceB).withHeight(13.f));
}
juce::Font FeverLook::getPopupMenuFont() { return juce::Font(juce::FontOptions(face).withHeight(14.f)); }

void FeverLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1,
                                 juce::Slider &s)
{
    const auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(s.isEnabled() ? 5.f : 6.f);
    const float r = std::min(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const bool hero = r > 80.f;
    g.setColour(juce::Colour(0xff141414));
    g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
    g.setColour(hero ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff3a3a3a));
    g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, hero ? 2.4f : 1.4f);

    // Track arc
    juce::Path track;
    track.addCentredArc(c.x, c.y, r - (hero ? 10.f : 6.f), r - (hero ? 10.f : 6.f), 0, a0, a1, true);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.strokePath(track, juce::PathStrokeType(hero ? 6.f : 3.5f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    juce::Path fill;
    const float ang = a0 + pos * (a1 - a0);
    const float mid = 0.5f * (a0 + a1);
    if (hero)
        fill.addCentredArc(c.x, c.y, r - 10.f, r - 10.f, 0, mid, ang, true);
    else
        fill.addCentredArc(c.x, c.y, r - 6.f, r - 6.f, 0, a0, ang, true);
    g.setColour(ink());
    g.strokePath(fill, juce::PathStrokeType(hero ? 6.f : 3.5f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // Needle
    const float nx = c.x + std::cos(ang) * (r - (hero ? 22.f : 14.f));
    const float ny = c.y + std::sin(ang) * (r - (hero ? 22.f : 14.f));
    g.setColour(juce::Colours::white);
    g.drawLine(c.x, c.y, nx, ny, hero ? 2.6f : 1.8f);
    g.fillEllipse(c.x - (hero ? 5.f : 3.f), c.y - (hero ? 5.f : 3.f), hero ? 10.f : 6.f, hero ? 10.f : 6.f);
    // Tick at centre for the hero (12 o'clock is not centre; centre of a0-a1 is 12 o'clock typically)
    if (hero)
    {
        const float tx = c.x + std::cos(mid) * (r - 2.f);
        const float ty = c.y + std::sin(mid) * (r - 2.f);
        g.setColour(juce::Colour(0xff888888));
        g.fillEllipse(tx - 2.5f, ty - 2.5f, 5.f, 5.f);
    }
}

void FeverLook::drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    const bool on = b.getToggleState();
    g.setColour(on ? juce::Colour(0xff2a2a2a) : (over ? juce::Colour(0xff1a1a1a) : juce::Colour(0xff121212)));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(on ? juce::Colour(0xff888888) : juce::Colour(0xff333333));
    g.drawRoundedRectangle(r, 3.f, 1.f);
    g.setColour(on ? juce::Colours::white : dim());
    g.setFont(juce::Font(juce::FontOptions(faceB).withHeight(12.5f)));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void FeverLook::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over,
                                     bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    g.setColour(down ? juce::Colour(0xff3a3a3a) : (over ? juce::Colour(0xff2c2c2c) : juce::Colour(0xff1c1c1c)));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(r, 3.f, 1.f);
}

void FeverLook::drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &)
{
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(1.f);
    g.setColour(panel());
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(line());
    g.drawRoundedRectangle(r, 3.f, 1.f);
    juce::Path p;
    const float cx = (float)w - 14.f, cy = (float)h * 0.5f;
    p.addTriangle(cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.5f);
    g.setColour(dim());
    g.fillPath(p);
}

void FeverLook::drawPopupMenuBackground(juce::Graphics &g, int w, int h)
{
    g.fillAll(juce::Colour(0xff141414));
    g.setColour(line());
    g.drawRect(0, 0, w, h, 1);
}

FeverEditor::FeverEditor(FeverProcessor &p)
    : AudioProcessorEditor(p), proc(p),
      aFever(*p.state.getParameter("fever"), fever), aSideA(*p.state.getParameter("sidea"), sideA),
      aSideB(*p.state.getParameter("sideb"), sideB), aMix(*p.state.getParameter("mix"), mix),
      aOut(*p.state.getParameter("output"), output), aDepth(*p.state.getParameter("heartdepth"), depth),
      aMatch(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("gainmatch")), match),
      aLimit(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("limiter")), limit),
      aHeart(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("heartbeat")), heart),
      aReduce(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("reducemotion")), reduce)
{
    setLookAndFeel(&look);
    setOpaque(true);
    setSize(800, 600);
    setWantsKeyboardFocus(false);

    auto setupKnob = [this](juce::Slider &s, const char *tip) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 2.25f,
                              true);
        s.setTooltip(tip);
        s.setWantsKeyboardFocus(false);
        addAndMakeVisible(s);
    };
    setupKnob(fever, "FEVER: centre is dry. Left opens a pitch cloud of octaves and fifths. Right opens a spectral rift.");
    setupKnob(sideA, "Side knob: DENSITY on the cloud side, FREEZE on the rift side.");
    setupKnob(sideB, "Side knob: SPREAD on the cloud side, HALO on the rift side.");
    setupKnob(mix, "Wet/dry mix.");
    setupKnob(output, "Output gain, decibels.");
    setupKnob(depth, "How far the heartbeat wobbles the FEVER knob.");

    auto setupLab = [this](juce::Label &l, const char *t, float h, bool bold) {
        l.setText(t, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions(bold ? look.faceB : look.face).withHeight(h)));
        l.setColour(juce::Label::textColourId, bold ? ink() : dim());
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
    };
    setupLab(brand, "CMPLXTY", 13.f, true);
    brand.setJustificationType(juce::Justification::centredLeft);
    setupLab(title, "FEVER", 28.f, true);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    setupLab(sideAName, "DENSITY", 13.f, true);
    setupLab(sideBName, "SPREAD", 13.f, true);
    setupLab(mixName, "MIX", 11.5f, true);
    setupLab(outName, "OUTPUT", 11.5f, true);
    setupLab(depthName, "DEPTH", 11.5f, true);
    setupLab(status, "", 12.f, false);
    status.setJustificationType(juce::Justification::centredRight);

    auto setupTog = [this](juce::ToggleButton &b, const char *t, const char *tip) {
        b.setButtonText(t);
        b.setTooltip(tip);
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };
    setupTog(match, "MATCH", "Automatic gain match. Keeps loudness close across the FEVER sweep.");
    setupTog(limit, "LIMIT", "Safety limiter at -1 dBFS. On by default.");
    setupTog(heart, "HEART", "Wobble FEVER in time with the DAW tempo.");
    setupTog(reduce, "REDUCE MOTION", "Turn off the mood animation and the heartbeat visual.");

    rate.addItemList({ "1/4", "1/8", "1/8T", "1/16", "1/2" }, 1);
    rate.setTooltip("Heartbeat rate, in note divisions of the host tempo.");
    rate.setWantsKeyboardFocus(false);
    addAndMakeVisible(rate);
    aRate = std::make_unique<juce::ComboBoxParameterAttachment>(
        *dynamic_cast<juce::AudioParameterChoice *>(p.state.getParameter("heartrate")), rate);

    presets.addItemList(FeverProcessor::presetNames(), 1);
    presets.setSelectedItemIndex(0, juce::dontSendNotification);
    presets.onChange = [this] {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0)
            proc.factory(i);
    };
    presets.setTooltip("Factory presets. Your own presets are .fever files via SAVE / LOAD.");
    presets.setWantsKeyboardFocus(false);
    addAndMakeVisible(presets);

    save.setButtonText("SAVE");
    load.setButtonText("LOAD");
    save.setTooltip("Save a user preset as a .fever file.");
    load.setTooltip("Load a .fever user preset.");
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    save.setWantsKeyboardFocus(false);
    load.setWantsKeyboardFocus(false);
    addAndMakeVisible(save);
    addAndMakeVisible(load);

    updateSideLabels();
    startTimerHz(30);
}

FeverEditor::~FeverEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void FeverEditor::revealPreset(int i)
{
    presets.setSelectedItemIndex(fever::clampi(i, 0, fever::presetCount - 1),
                                 juce::dontSendNotification);
}

void FeverEditor::updateSideLabels()
{
    const float f = proc.getSettings()[fever::FeverAmt];
    const int side = fever::onHateSide(f) ? 1 : 0;
    if (side != lastSide)
    {
        lastSide = side;
        sideAName.setText(fever::sideAName(f), juce::dontSendNotification);
        sideBName.setText(fever::sideBName(f), juce::dontSendNotification);
    }
    // Dim SIDE knobs while FEVER sits at the centre (a wire — they have nothing to do).
    // Dim HEART RATE / DEPTH while HEART is off.
    const bool active = fever::loveAmt(f) > 0.02f || fever::hateAmt(f) > 0.02f;
    const float a = active ? 1.0f : 0.38f;
    sideA.setAlpha(a);
    sideB.setAlpha(a);
    sideAName.setAlpha(a);
    sideBName.setAlpha(a);
    const bool heart = proc.getSettings()[fever::Heartbeat] >= 0.5f;
    const float ha = heart ? 1.0f : 0.38f;
    depth.setAlpha(ha);
    depthName.setAlpha(ha);
}

void FeverEditor::timerCallback()
{
    const bool motion = proc.getSettings()[fever::ReduceMotion] < 0.5f;
    if (motion)
        anim += 0.033f;
    updateSideLabels();
    const float db = juce::Decibels::gainToDecibels(proc.meter.load(), -100.f);
    const float mk = proc.makeup.load();
    status.setText("OUT " + juce::String(db, 1) + "   MATCH " + juce::String(mk, 1) + " dB   LIM " +
                       juce::String(proc.reduction.load(), 1),
                   juce::dontSendNotification);
    repaint();
}

void FeverEditor::paintMood(juce::Graphics &g, juce::Rectangle<float> area, float fv, float pulse, bool motion)
{
    const auto c = area.getCentre();
    const float love = fever::loveAmt(fv);
    const float hate = fever::hateAmt(fv);
    const float rad = std::min(area.getWidth(), area.getHeight()) * 0.5f;
    const float beat = motion ? (0.85f + 0.15f * pulse) : 1.f;
    const float t = motion ? anim : 0.f;

    if (love > 0.01f)
    {
        // Soft glow: concentric rings and drifting orbs.
        const int rings = 5;
        for (int i = rings; i >= 1; --i)
        {
            const float k = (float)i / (float)rings;
            const float rr = rad * (0.55f + 0.42f * k) * (0.92f + 0.08f * love) * beat;
            const float a = love * (0.18f + 0.10f * k);
            g.setColour(juce::Colours::white.withAlpha(a * (0.55f + 0.45f * k)));
            g.drawEllipse(c.x - rr, c.y - rr, rr * 2, rr * 2, 1.6f + 2.2f * love * (1.f - k));
        }
        for (int i = 0; i < 10; ++i)
        {
            const float ang = t * (0.25f + 0.07f * (float)i) + (float)i * 0.62f;
            const float d = rad * (0.62f + 0.22f * std::sin(t * 0.7f + (float)i));
            const float x = c.x + std::cos(ang) * d;
            const float y = c.y + std::sin(ang * 0.93f) * d * 0.86f;
            const float s = (5.f + 9.f * love) * beat;
            g.setColour(juce::Colours::white.withAlpha(0.10f + 0.22f * love));
            g.fillEllipse(x - s, y - s, s * 2, s * 2);
        }
    }
    if (hate > 0.01f)
    {
        // Jagged shards and a glitch field.
        juce::Random rng(0x4F1E2A11u + (motion ? (unsigned)(t * 14.f) : 7u));
        g.setColour(juce::Colours::white.withAlpha(0.12f + 0.22f * hate));
        for (int i = 0; i < 14; ++i)
        {
            const float y = area.getY() + rng.nextFloat() * area.getHeight();
            const float x0 = area.getX() + rng.nextFloat() * area.getWidth();
            const float w = 8.f + rng.nextFloat() * (30.f + 50.f * hate);
            g.fillRect(x0, y, w, 1.2f + hate);
        }
        for (int i = 0; i < 18; ++i)
        {
            const float base = (float)i / 18.f * juce::MathConstants<float>::twoPi + t * 0.4f;
            const float jit = (rng.nextFloat() - 0.5f) * 0.35f * hate;
            const float inner = rad * 0.50f;
            const float outer = rad * (0.72f + 0.28f * hate * (0.55f + 0.45f * rng.nextFloat())) * beat;
            juce::Path shard;
            const float a0 = base + jit;
            const float a1 = base + 0.10f + jit * 0.4f;
            shard.startNewSubPath(c.x + std::cos(a0) * inner, c.y + std::sin(a0) * inner);
            shard.lineTo(c.x + std::cos(a0 + 0.03f) * outer, c.y + std::sin(a0) * outer);
            shard.lineTo(c.x + std::cos(a1) * inner, c.y + std::sin(a1) * inner);
            shard.closeSubPath();
            g.setColour(juce::Colours::white.withAlpha(0.12f + 0.40f * hate * rng.nextFloat()));
            g.fillPath(shard);
        }
        // Broken ring
        g.setColour(juce::Colours::white.withAlpha(0.25f + 0.45f * hate));
        for (int i = 0; i < 9; ++i)
        {
            const float a0 = t * 1.1f + (float)i * 0.7f;
            const float a1 = a0 + 0.22f + 0.15f * hate;
            juce::Path arc;
            const float rr = rad * 0.96f;
            arc.addCentredArc(c.x, c.y, rr, rr, 0, a0, a1, true);
            g.strokePath(arc, juce::PathStrokeType(1.5f + 2.f * hate));
        }
    }
    if (love < 0.02f && hate < 0.02f)
    {
        g.setColour(juce::Colour(0xff2a2a2a));
        const float rr = rad * 0.72f;
        g.drawEllipse(c.x - rr, c.y - rr, rr * 2, rr * 2, 1.2f);
    }
}

void FeverEditor::paint(juce::Graphics &g)
{
    g.fillAll(bg());
    // Header rule
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(0, 58, getWidth(), 1);
    g.fillRect(0, 405, getWidth(), 1);

    const auto settings = proc.getSettings();
    const float fv = settings[fever::Heartbeat] >= 0.5f ? proc.viewFever.load() : settings[fever::FeverAmt];
    const float pulse = proc.heartPulse.load();
    const bool motion = proc.getSettings()[fever::ReduceMotion] < 0.5f;
    auto mood = juce::Rectangle<float>(150.f, 70.f, 500.f, 310.f);
    paintMood(g, mood, fv, pulse, motion);

    // End labels
    g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(14.f)));
    g.setColour(juce::Colours::white.withAlpha(0.35f + 0.65f * fever::loveAmt(fv)));
    g.drawText("CLOUD", 36, 210, 90, 24, juce::Justification::centred);
    g.setColour(juce::Colours::white.withAlpha(0.35f + 0.65f * fever::hateAmt(fv)));
    g.drawText("RIFT", getWidth() - 126, 210, 90, 24, juce::Justification::centred);

    // Readable value under the hero
    juce::String tag = "STILL";
    if (fever::loveAmt(fv) > 0.02f)
        tag = "CLOUD  " + juce::String((int)std::round(fever::loveAmt(fv) * 100));
    else if (fever::hateAmt(fv) > 0.02f)
        tag = "RIFT  " + juce::String((int)std::round(fever::hateAmt(fv) * 100));
    g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(13.f)));
    g.setColour(ink());
    g.drawText("FEVER", 0, 358, getWidth(), 18, juce::Justification::centred);
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(12.f)));
    g.drawText(tag, 0, 378, getWidth(), 16, juce::Justification::centred);
    // RATE sits next to HEART; a small caption keeps it readable.
    g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(10.f)));
    g.setColour(dim());
    g.drawText("RATE", 448, 530, 62, 14, juce::Justification::centred);

    // Heartbeat ECG, bottom of the mood field
    if (proc.getSettings()[fever::Heartbeat] >= 0.5f)
    {
        juce::Path eeg;
        const float y0 = 338.f;
        const float x0 = 180.f, x1 = (float)getWidth() - 180.f;
        const int N = 120;
        const float phase = proc.getEngine().heartPhase;
        for (int i = 0; i <= N; ++i)
        {
            const float u = (float)i / (float)N;
            const float ph = u * 3.f + (motion ? phase : 0.f);
            const float f = std::fmod(ph, 1.f);
            // A double-beat pulse shape
            float v = 0;
            if (f < 0.08f)
                v = std::sin(f / 0.08f * juce::MathConstants<float>::pi);
            else if (f < 0.16f)
                v = 0.45f * std::sin((f - 0.08f) / 0.08f * juce::MathConstants<float>::pi);
            const float x = x0 + u * (x1 - x0);
            const float y = y0 - v * 16.f * (motion ? (0.5f + 0.5f * pulse) : 1.f);
            if (i == 0)
                eeg.startNewSubPath(x, y);
            else
                eeg.lineTo(x, y);
        }
        g.setColour(juce::Colours::white.withAlpha(motion ? 0.55f : 0.28f));
        g.strokePath(eeg, juce::PathStrokeType(1.3f));
    }
}

void FeverEditor::resized()
{
    brand.setBounds(22, 10, 90, 18);
    title.setBounds(22, 24, 160, 32);
    presets.setBounds(430, 16, 180, 30);
    save.setBounds(618, 16, 70, 30);
    load.setBounds(696, 16, 70, 30);

    // Hero knob, centred
    fever.setBounds(250, 80, 300, 280);

    sideA.setBounds(48, 415, 145, 145);
    sideAName.setBounds(48, 556, 145, 18);
    sideB.setBounds(607, 415, 145, 145);
    sideBName.setBounds(607, 556, 145, 18);

    mix.setBounds(210, 445, 84, 96);
    mixName.setBounds(210, 542, 84, 16);
    output.setBounds(292, 445, 84, 96);
    outName.setBounds(292, 542, 84, 16);
    depth.setBounds(515, 445, 84, 96);
    depthName.setBounds(515, 542, 84, 16);

    match.setBounds(390, 440, 108, 26);
    limit.setBounds(390, 470, 108, 26);
    heart.setBounds(390, 500, 54, 28);
    rate.setBounds(448, 500, 62, 28);
    reduce.setBounds(250, 16, 160, 30);
    status.setBounds(430, 48, 336, 14);
}

void FeverEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("CMPLXTY/FEVER/Presets");
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(write ? "Save FEVER preset" : "Load FEVER preset", folder,
                                                  "*.fever");
    juce::Component::SafePointer<FeverEditor> safe(this);
    chooser->launchAsync(
        juce::FileBrowserComponent::canSelectFiles |
            (write ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                   : juce::FileBrowserComponent::openMode),
        [safe, write](const juce::FileChooser &c) {
            if (!safe)
                return;
            auto f = c.getResult();
            if (f == juce::File())
                return;
            if (write)
            {
                if (!f.hasFileExtension(".fever"))
                    f = f.withFileExtension(".fever");
                juce::MemoryBlock mb;
                safe->proc.getStateInformation(mb);
                f.replaceWithData(mb.getData(), mb.getSize());
            }
            else
            {
                juce::MemoryBlock mb;
                if (f.loadFileAsData(mb))
                    safe->proc.setStateInformation(mb.getData(), (int)mb.getSize());
            }
        });
}
