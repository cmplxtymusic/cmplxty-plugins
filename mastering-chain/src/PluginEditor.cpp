#include "PluginEditor.h"
#include "BinaryData.h"
#include "Presets.h"
#include <cmath>

namespace
{
juce::Colour bg() { return juce::Colour(0xff0c0c0c); }
juce::Colour panelC() { return juce::Colour(0xff151515); }
juce::Colour ink() { return juce::Colour(0xffe8e8e8); }
juce::Colour dim() { return juce::Colour(0xff9a9a9a); }
juce::Colour line() { return juce::Colour(0xff343434); }

float hzToX(float hz, float x0, float w, float fMin = 20.f, float fMax = 20000.f)
{
    const float t = std::log(std::max(hz, fMin) / fMin) / std::log(fMax / fMin);
    return x0 + glaze::clampf(t, 0.f, 1.f) * w;
}

juce::String lufsText(float v) { return v <= -69.f ? juce::String("--") : juce::String(v, 1); }

constexpr int kKnob = 64, kCellH = 94, kPanelTitle = 30;
} // namespace

GlazeLook::GlazeLook()
{
    face = gzui::uiTypeface(false);
    faceB = gzui::uiTypeface(true);
    setDefaultSansSerifTypeface(face);
    setColour(juce::ResizableWindow::backgroundColourId, bg());
    setColour(juce::Label::textColourId, ink());
    setColour(juce::ToggleButton::textColourId, ink());
    setColour(juce::ToggleButton::tickColourId, ink());
    setColour(juce::ComboBox::backgroundColourId, panelC());
    setColour(juce::ComboBox::textColourId, ink());
    setColour(juce::ComboBox::outlineColourId, line());
    setColour(juce::ComboBox::arrowColourId, dim());
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff141414));
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a2a2a));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, panelC());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, panelC());
    setColour(juce::TextEditor::textColourId, ink());
    setColour(juce::TextEditor::outlineColourId, line());
    setColour(juce::Slider::textBoxTextColourId, ink());
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff101010));
    setColour(juce::Slider::textBoxOutlineColourId, line());
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff202020));
    setColour(juce::TooltipWindow::textColourId, ink());
    setColour(juce::TooltipWindow::outlineColourId, line());
}

juce::Font GlazeLook::getLabelFont(juce::Label &) { return juce::Font(juce::FontOptions(face).withHeight(13.f)); }
juce::Font GlazeLook::getComboBoxFont(juce::ComboBox &) { return juce::Font(juce::FontOptions(face).withHeight(14.f)); }
juce::Font GlazeLook::getTextButtonFont(juce::TextButton &, int) { return juce::Font(juce::FontOptions(faceB).withHeight(13.f)); }
juce::Font GlazeLook::getPopupMenuFont() { return juce::Font(juce::FontOptions(face).withHeight(14.f)); }

void GlazeLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1,
                                 juce::Slider &s)
{
    const auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.f);
    const float r = std::min(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    g.setColour(juce::Colour(0xff141414));
    g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
    g.setColour(juce::Colour(0xff3c3c3c));
    g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, 1.4f);

    const float rad = r - 6.f;
    juce::Path track;
    track.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.strokePath(track, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ang = a0 + pos * (a1 - a0);
    float startAng = a0;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
    {
        const float zp = (float)s.valueToProportionOfLength(0.0);
        startAng = a0 + zp * (a1 - a0);
        g.setColour(juce::Colour(0xff6a6a6a));
        const float zx = c.x + std::cos(startAng) * (rad + 1.f), zy = c.y + std::sin(startAng) * (rad + 1.f);
        g.fillEllipse(zx - 1.5f, zy - 1.5f, 3.f, 3.f);
    }
    juce::Path fill;
    fill.addCentredArc(c.x, c.y, rad, rad, 0, std::min(startAng, ang), std::max(startAng, ang), true);
    g.setColour(s.isEnabled() ? ink() : dim());
    g.strokePath(fill, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float nx = c.x + std::cos(ang) * (r - 14.f), ny = c.y + std::sin(ang) * (r - 14.f);
    g.setColour(juce::Colours::white);
    g.drawLine(c.x, c.y, nx, ny, 1.8f);
    g.fillEllipse(c.x - 3.f, c.y - 3.f, 6.f, 6.f);
}

void GlazeLook::drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    const bool on = b.getToggleState();
    g.setColour(on ? juce::Colour(0xff2a2a2a) : (over ? juce::Colour(0xff1a1a1a) : juce::Colour(0xff121212)));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(on ? juce::Colour(0xff9a9a9a) : juce::Colour(0xff333333));
    g.drawRoundedRectangle(r, 3.f, 1.f);
    g.setColour(on ? juce::Colours::white : dim());
    g.setFont(juce::Font(juce::FontOptions(faceB).withHeight(12.5f)));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void GlazeLook::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    g.setColour(down ? juce::Colour(0xff3a3a3a) : (over ? juce::Colour(0xff2c2c2c) : juce::Colour(0xff1c1c1c)));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(r, 3.f, 1.f);
}

void GlazeLook::drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &)
{
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(1.f);
    g.setColour(panelC());
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(line());
    g.drawRoundedRectangle(r, 3.f, 1.f);
    juce::Path p;
    const float cx = (float)w - 14.f, cy = (float)h * 0.5f;
    p.addTriangle(cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.5f);
    g.setColour(dim());
    g.fillPath(p);
}

void GlazeLook::drawPopupMenuBackground(juce::Graphics &g, int w, int h)
{
    g.fillAll(juce::Colour(0xff141414));
    g.setColour(line());
    g.drawRect(0, 0, w, h, 1);
}

GlazeEditor::GlazeEditor(GlazeProcessor &p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&look);
    setOpaque(true);
    setSize(kW, kH);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    using namespace glaze;
    // ---- module layout
    const int rowA = 278, rowB = 504, panelH = kPanelTitle + 2 * kCellH + 4;
    modules.push_back({ "EQ", { 20, rowA, 640, panelH }, EqOn, { Hpf, LowG, LmG, HmG, HighG, LowF, LmF, HmF, HighF } });
    modules.push_back({ "GLUE COMPRESSOR", { 672, rowA, 548, panelH }, GlueOn, { GThr, GRatio, GAtk, GRel, GMakeup, GMix } });
    const int cw = (1200 - 36) / 12;
    int x = 20;
    modules.push_back({ "MULTIBAND", { x, rowB, cw * 4, panelH }, MbOn, { Xo1, Mb1, Mb2, Mb3, Xo2, Ml1, Ml2, Ml3 } });
    x += cw * 4 + 12;
    modules.push_back({ "SATURATION", { x, rowB, cw * 3, panelH }, SatOn, { Drive, Tone, SatMix } });
    x += cw * 3 + 12;
    modules.push_back({ "STEREO", { x, rowB, cw * 2, panelH }, WidthOn, { Width, BassMono } });
    x += cw * 2 + 12;
    modules.push_back({ "LIMITER", { x, rowB, cw * 3, panelH }, LimOn, { Push, Ceiling, LimRel } });

    static const char *tips[Count] = {
        "INPUT: gain into the whole chain.",
        "BYPASS: latency-matched bypass of the whole chain.",
        "EQ ON", "HPF: high-pass to clear sub rumble. Off at the minimum.",
        "LOW FREQ: corner of the low shelf.", "LOW: low shelf gain.",
        "LOW-MID FREQ: centre of the low-mid bell. A small cut here clears mud.", "LOW-MID: bell gain.",
        "HIGH-MID FREQ: centre of the high-mid bell.", "HIGH-MID: bell gain.",
        "HIGH FREQ: corner of the high shelf.", "HIGH: high shelf gain.",
        "GLUE ON", "THRESH: where the glue compressor starts working. 0 dB means it does nothing.",
        "RATIO: how hard it squeezes above the threshold. 1.5 to 3 is glue.",
        "ATTACK: slower lets transients punch through.", "RELEASE: how fast it lets go.",
        "MAKEUP: gain after the compressor.", "MIX: parallel blend of compressed and dry.",
        "MULTI ON", "LOW / MID crossover.", "MID / HIGH crossover.",
        "LOW DYNAMICS: evens out loud peaks in the low band against its own average. 0 does nothing.",
        "MID DYNAMICS: the same for the mid band.", "HIGH DYNAMICS: the same for the high band.",
        "LOW LEVEL: band gain.", "MID LEVEL: band gain.", "HIGH LEVEL: band gain.",
        "SAT ON", "DRIVE: how hard the saturator is pushed. 8x oversampled.", "TYPE: TAPE soft, TUBE warm and even, HARD tight and bright.",
        "TONE: tilts the saturated sound darker or brighter.", "MIX: saturator blend.",
        "STEREO ON", "WIDTH: side level. 100 % is unchanged.",
        "BASS MONO: everything below this is made mono. Off at the minimum.",
        "LIMIT ON", "PUSH: gain into the true-peak limiter. This is where loudness comes from.",
        "CEILING: the output never goes above this true-peak level.", "RELEASE: limiter release time."
    };
    const char *labels[Count] = {};
    labels[Hpf] = "HPF"; labels[LowG] = "LOW"; labels[LmG] = "LOW-MID"; labels[HmG] = "HIGH-MID"; labels[HighG] = "HIGH";
    labels[LowF] = "LOW FREQ"; labels[LmF] = "LM FREQ"; labels[HmF] = "HM FREQ"; labels[HighF] = "HIGH FREQ";
    labels[GThr] = "THRESH"; labels[GRatio] = "RATIO"; labels[GAtk] = "ATTACK"; labels[GRel] = "RELEASE";
    labels[GMakeup] = "MAKEUP"; labels[GMix] = "MIX"; labels[Xo1] = "LOW/MID"; labels[Xo2] = "MID/HIGH";
    labels[Mb1] = "LOW DYN"; labels[Mb2] = "MID DYN"; labels[Mb3] = "HIGH DYN"; labels[Ml1] = "LOW LVL";
    labels[Ml2] = "MID LVL"; labels[Ml3] = "HIGH LVL"; labels[Drive] = "DRIVE"; labels[Tone] = "TONE";
    labels[SatMix] = "MIX"; labels[Width] = "WIDTH"; labels[BassMono] = "BASS MONO"; labels[Push] = "PUSH";
    labels[Ceiling] = "CEILING"; labels[LimRel] = "RELEASE"; labels[Input] = "INPUT";

    auto mk = [&](int param) {
        auto c = std::make_unique<Cell>();
        c->param = param;
        auto *prm = proc.state.getParameter(definitions[(size_t)param].id);
        auto &k = c->knob;
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        k.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 2.25f, true);
        k.setTooltip(tips[param]);
        c->att = std::make_unique<juce::SliderParameterAttachment>(*prm, k);
        k.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
        k.onContextMenu = [this, id = juce::String(definitions[(size_t)param].id)](gzui::Knob &kn, juce::Point<int> sp) {
            showParamMenu(id, &kn, getLocalPoint(nullptr, sp));
        };
        addAndMakeVisible(k);
        makeLab(c->name, labels[param], 12.f, true);
        makeLab(c->val, "", 12.f, false);
        c->val.setColour(juce::Label::textColourId, dim());
        cells[(size_t)param] = std::move(c);
    };
    for (auto &m : modules)
        for (int param : m.knobs)
            mk(param);
    mk(Input);

    auto mkSwitch = [&](int param, const char *text) {
        auto s = std::make_unique<Switch>();
        s->param = param;
        auto *prm = dynamic_cast<juce::RangedAudioParameter *>(proc.state.getParameter(definitions[(size_t)param].id));
        s->btn.setButtonText(text);
        s->btn.setTooltip(tips[param]);
        s->att = std::make_unique<juce::ButtonParameterAttachment>(*prm, s->btn);
        s->btn.onContextMenu = [this, id = juce::String(definitions[(size_t)param].id)](gzui::Btn &, juce::Point<int> sp) {
            showParamMenu(id, nullptr, getLocalPoint(nullptr, sp));
        };
        addAndMakeVisible(s->btn);
        switches[(size_t)param] = std::move(s);
    };
    for (auto &m : modules)
        mkSwitch(m.onParam, "ON");
    mkSwitch(Bypass, "BYPASS");

    // saturation type combo
    {
        for (int i = 0; i < 3; ++i)
            satType.addItem(kSatLabels[i], i + 1);
        auto *prm = dynamic_cast<juce::RangedAudioParameter *>(proc.state.getParameter("sattype"));
        aSatType = std::make_unique<juce::ComboBoxParameterAttachment>(*prm, satType);
        satType.setTooltip(tips[SatType]);
        addAndMakeVisible(satType);
    }

    makeLab(brand, "CMPLXTY", 13.f, true);
    brand.setJustificationType(juce::Justification::centredLeft);
    makeLab(title, "GLAZE", 30.f, true);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    makeLab(footer, "", 11.5f, false);
    footer.setJustificationType(juce::Justification::centredLeft);
    footer.setInterceptsMouseClicks(false, false);
    footer.setColour(juce::Label::textColourId, dim());

    presets.addItemList(GlazeProcessor::presetNames(), 1);
    presets.setSelectedItemIndex(0, juce::dontSendNotification);
    presets.onChange = [this] {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0)
        {
            proc.factory(i);
            currentPreset = i;
        }
    };
    addAndMakeVisible(presets);
    for (auto *b : { &prev, &next, &save, &load, &resetMeters })
        addAndMakeVisible(b);
    prev.onClick = [this] {
        const int i = (currentPreset + glaze::presetCount - 1) % glaze::presetCount;
        revealPreset(i);
        proc.factory(i);
    };
    next.onClick = [this] {
        const int i = (currentPreset + 1) % glaze::presetCount;
        revealPreset(i);
        proc.factory(i);
    };
    save.setTooltip("Save a user preset as a .glaze file.");
    load.setTooltip("Load a .glaze user preset.");
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    resetMeters.setTooltip("Reset the integrated loudness and the true-peak hold.");
    resetMeters.onClick = [this] { proc.getEngine().resetIntegratedFlag.store(true); };

    addChildComponent(popup);
    binsIn.fill(-90.f);
    binsOut.fill(-90.f);
    resized();

    // nothing in the editor may take keyboard focus (host keys such as Space must keep working)
    gzui::hostkeys::noKeyboardFocus(*this, &popup);
    startTimerHz(30);
    updateDim();
    if (juce::SystemStats::getEnvironmentVariable("GLAZE_NO_UPDATE_CHECK", {}).isNotEmpty())
        return;
    juce::MessageManager::callAsync([sp = juce::Component::SafePointer<GlazeEditor>(this)] {
        if (sp != nullptr)
            sp->startUpdateCheck();
    });
}

GlazeEditor::~GlazeEditor()
{
    stopTimer();
    checker.cancelAndWait();
    setLookAndFeel(nullptr);
}

void GlazeEditor::makeLab(juce::Label &l, const char *t, float h, bool bold)
{
    l.setText(t, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(juce::FontOptions(bold ? look.faceB : look.face).withHeight(h)));
    l.setColour(juce::Label::textColourId, bold ? ink() : dim());
    l.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(l);
}

void GlazeEditor::revealPreset(int index)
{
    currentPreset = glaze::clampi(index, 0, glaze::presetCount - 1);
    presets.setSelectedItemIndex(currentPreset, juce::dontSendNotification);
}

void GlazeEditor::layoutKnob(int param, int x, int y)
{
    auto &c = *cells[(size_t)param];
    c.knob.setBounds(x, y, kKnob, kKnob);
    c.name.setBounds(x - 20, y + kKnob + 1, kKnob + 40, 15);
    c.val.setBounds(x - 20, y + kKnob + 15, kKnob + 40, 14);
}

void GlazeEditor::resized()
{
    using namespace glaze;
    if (switches[Bypass] == nullptr) // called from setSize() before the controls exist
        return;
    brand.setBounds(24, 10, 120, 14);
    title.setBounds(24, 22, 200, 36);
    presets.setBounds(250, 18, 270, 30);
    prev.setBounds(222, 18, 26, 30);
    next.setBounds(524, 18, 26, 30);
    save.setBounds(560, 18, 62, 30);
    load.setBounds(628, 18, 62, 30);
    // input knob in the top bar
    {
        auto &c = *cells[Input];
        c.knob.setBounds(960, 10, 46, 46);
        c.name.setBounds(884, 14, 72, 15);
        c.name.setJustificationType(juce::Justification::centredRight);
        c.val.setBounds(884, 31, 72, 14);
        c.val.setJustificationType(juce::Justification::centredRight);
    }
    switches[Bypass]->btn.setBounds(1112, 18, 108, 30);

    specArea = { 20, 68, 880, 200 };
    meterArea = { 912, 68, 308, 200 };
    footerArea = { 20, kH - 28, kW - 40, 22 };
    footer.setBounds(footerArea.withTrimmedRight(220));
    resetMeters.setBounds(meterArea.getRight() - 70, meterArea.getY() + 116, 58, 22);

    for (auto &m : modules)
    {
        switches[(size_t)m.onParam]->btn.setBounds(m.area.getRight() - 52, m.area.getY() + 5, 44, 20);
    }
    const int x0 = modules[0].area.getX(), y0 = modules[0].area.getY() + kPanelTitle;
    // EQ: gains row 0, freqs row 1; 5 columns
    {
        const int colW = modules[0].area.getWidth() / 5;
        const int cx = x0 + (colW - kKnob) / 2;
        layoutKnob(Hpf, cx, y0 + 2);
        layoutKnob(LowG, cx + colW, y0 + 2);
        layoutKnob(LmG, cx + 2 * colW, y0 + 2);
        layoutKnob(HmG, cx + 3 * colW, y0 + 2);
        layoutKnob(HighG, cx + 4 * colW, y0 + 2);
        layoutKnob(LowF, cx + colW, y0 + kCellH + 2);
        layoutKnob(LmF, cx + 2 * colW, y0 + kCellH + 2);
        layoutKnob(HmF, cx + 3 * colW, y0 + kCellH + 2);
        layoutKnob(HighF, cx + 4 * colW, y0 + kCellH + 2);
    }
    {
        const auto &a = modules[1].area;
        const int colW = a.getWidth() / 4, cx = a.getX() + (colW - kKnob) / 2, y = a.getY() + kPanelTitle + 2;
        layoutKnob(GThr, cx, y);
        layoutKnob(GRatio, cx + colW, y);
        layoutKnob(GAtk, cx + 2 * colW, y);
        layoutKnob(GRel, cx + 3 * colW, y);
        layoutKnob(GMakeup, cx, y + kCellH);
        layoutKnob(GMix, cx + colW, y + kCellH);
    }
    {
        const auto &a = modules[2].area;
        const int colW = a.getWidth() / 4, cx = a.getX() + (colW - kKnob) / 2, y = a.getY() + kPanelTitle + 2;
        layoutKnob(Xo1, cx, y);
        layoutKnob(Xo2, cx, y + kCellH);
        layoutKnob(Mb1, cx + colW, y);
        layoutKnob(Mb2, cx + 2 * colW, y);
        layoutKnob(Mb3, cx + 3 * colW, y);
        layoutKnob(Ml1, cx + colW, y + kCellH);
        layoutKnob(Ml2, cx + 2 * colW, y + kCellH);
        layoutKnob(Ml3, cx + 3 * colW, y + kCellH);
    }
    {
        const auto &a = modules[3].area;
        const int colW = a.getWidth() / 3, cx = a.getX() + (colW - kKnob) / 2, y = a.getY() + kPanelTitle + 2;
        layoutKnob(Drive, cx, y);
        layoutKnob(Tone, cx + colW, y);
        layoutKnob(SatMix, cx + 2 * colW, y);
        satType.setBounds(a.getX() + 18, y + kCellH + 18, a.getWidth() - 36, 30);
    }
    {
        const auto &a = modules[4].area;
        const int colW = a.getWidth() / 2, cx = a.getX() + (colW - kKnob) / 2, y = a.getY() + kPanelTitle + 2;
        layoutKnob(Width, cx, y);
        layoutKnob(BassMono, cx + colW, y);
    }
    {
        const auto &a = modules[5].area;
        const int colW = a.getWidth() / 3, cx = a.getX() + (colW - kKnob) / 2, y = a.getY() + kPanelTitle + 2;
        layoutKnob(Push, cx, y);
        layoutKnob(Ceiling, cx + colW, y);
        layoutKnob(LimRel, cx + 2 * colW, y);
    }
}

void GlazeEditor::updateDim()
{
    using namespace glaze;
    const auto s = proc.getSettings();
    for (auto &m : modules)
    {
        const bool on = s[m.onParam] >= 0.5f;
        for (int p : m.knobs)
        {
            const float a = on ? 1.f : 0.38f;
            cells[(size_t)p]->knob.setAlpha(a);
            cells[(size_t)p]->name.setAlpha(a);
            cells[(size_t)p]->val.setAlpha(a);
        }
        if (m.onParam == SatOn)
            satType.setAlpha(on ? 1.f : 0.38f);
    }
}

void GlazeEditor::timerCallback()
{
    for (auto &c : cells)
        if (c != nullptr)
            if (auto *p = proc.state.getParameter(glaze::definitions[(size_t)c->param].id))
                c->val.setText(p->getCurrentValueAsText(), juce::dontSendNotification);

    const auto &eng = proc.getEngine();
    const int wr = eng.scopeW.load(std::memory_order_relaxed);
    analyseScope(eng.scopeIn, wr, binsIn);
    analyseScope(eng.scope, wr, binsOut);

    const double sr = proc.getSampleRate();
    const double lat = sr > 0 ? 1000.0 * eng.latencySamples() / sr : 0.0;
    juce::String t = juce::String("CMPLXTY GLAZE ") + GLAZE_VERSION_STRING + "   |   latency " + juce::String(lat, 1) + " ms";
    if (footerNote.isNotEmpty())
        t = footerNote;
    footer.setText(t, juce::dontSendNotification);

    if ((++tickCount % 3) == 0)
    {
        gzui::hostkeys::remember(*this);
        if (!popup.isOpen())
            gzui::hostkeys::release(*this);
    }
    updateDim();
    repaint();
}

void GlazeEditor::analyseScope(const std::array<float, glaze::Engine::kScope> &ring, int writePos,
                              std::array<float, kBins> &dest)
{
    constexpr int N = 2048;
    std::array<float, N> re{}, im{};
    for (int i = 0; i < N; ++i)
    {
        const int idx = (writePos - N + i + glaze::Engine::kScope) & (glaze::Engine::kScope - 1);
        const float win = 0.5f - 0.5f * std::cos(2.f * glaze::kPi * (float)i / (float)(N - 1));
        re[(size_t)i] = ring[(size_t)idx] * win;
    }
    for (int i = 1, j = 0; i < N; ++i)
    {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[(size_t)i], re[(size_t)j]);
            std::swap(im[(size_t)i], im[(size_t)j]);
        }
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const float ang = -2.f * glaze::kPi / (float)len;
        const float wr0 = std::cos(ang), wi0 = std::sin(ang);
        for (int i = 0; i < N; i += len)
        {
            float wr = 1, wi = 0;
            for (int k = 0; k < len / 2; ++k)
            {
                const int i1 = i + k, i2 = i1 + len / 2;
                const float tr = wr * re[(size_t)i2] - wi * im[(size_t)i2];
                const float ti = wr * im[(size_t)i2] + wi * re[(size_t)i2];
                re[(size_t)i2] = re[(size_t)i1] - tr;
                im[(size_t)i2] = im[(size_t)i1] - ti;
                re[(size_t)i1] += tr;
                im[(size_t)i1] += ti;
                const float nwr = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = nwr;
            }
        }
    }

    const float sr = (float)std::max(proc.getSampleRate(), 44100.0);
    std::array<float, kBins> sum{}, cnt{};
    for (int k = 1; k < N / 2; ++k)
    {
        const float hz = sr * (float)k / (float)N;
        if (hz < 20.f || hz > 20000.f)
            continue;
        const float t = std::log(hz / 20.f) / std::log(20000.f / 20.f);
        const int b = glaze::clampi((int)(t * (float)kBins), 0, kBins - 1);
        const float mag = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
        sum[(size_t)b] += mag;
        cnt[(size_t)b] += 1.f;
    }
    // Hann coherent gain 0.5; 20 log10 so a full-scale sine sits near 0 dBFS.
    const float norm = 1.f / (0.5f * (float)N);
    for (int i = 0; i < kBins; ++i)
    {
        const float mag = (cnt[(size_t)i] > 0.f ? sum[(size_t)i] / cnt[(size_t)i] : 0.f) * norm;
        const float db = 20.f * std::log10(std::max(mag, 1.0e-12f));
        dest[(size_t)i] += 0.40f * (db - dest[(size_t)i]);
    }
}


void GlazeEditor::paintSpectrum(juce::Graphics &g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(area, 4.f);
    g.setColour(line());
    g.drawRoundedRectangle(area, 4.f, 1.f);

    auto body = area.reduced(10.f, 8.f);
    const auto labels = body.removeFromBottom(16.f);
    const auto eqArea = body.removeFromTop(56.f);
    body.removeFromTop(4.f);
    const auto spArea = body;
    const auto s = proc.getSettings();
    const double sr = std::max(proc.getSampleRate(), 44100.0);
    const float x0 = eqArea.getX(), w = eqArea.getWidth();

    const float marks[] = { 50.f, 100.f, 300.f, 1000.f, 3000.f, 10000.f };
    const char *labs[] = { "50", "100", "300", "1k", "3k", "10k" };
    g.setColour(juce::Colour(0x223a3a3a));
    for (float m : marks)
        g.drawLine(hzToX(m, x0, w), eqArea.getY(), hzToX(m, x0, w), spArea.getBottom(), 1.f);

    // crossover and bass-mono markers
    {
        const float dashes[2] = { 3.f, 4.f };
        g.setColour(juce::Colour(0x77888888));
        if (s[glaze::MbOn] >= 0.5f)
            for (float f : { s[glaze::Xo1], s[glaze::Xo2] })
            {
                const float x = hzToX(f, x0, w);
                g.drawDashedLine({ x, eqArea.getY(), x, spArea.getBottom() }, dashes, 2, 1.f);
            }
        if (s[glaze::WidthOn] >= 0.5f && glaze::bassMonoActive(s[glaze::BassMono]))
        {
            const float x = hzToX(s[glaze::BassMono], x0, w);
            g.setColour(juce::Colour(0x55b0b0b0));
            g.drawLine(x, spArea.getY(), x, spArea.getBottom(), 1.f);
        }
    }

    // EQ curve pane: +10 dB at the top, -10 dB at the bottom
    auto eqY = [&](float db) {
        const float t = glaze::clampf((db + 10.f) / 20.f, 0.f, 1.f);
        return eqArea.getBottom() - t * eqArea.getHeight();
    };
    g.setColour(juce::Colour(0x443a3a3a));
    g.drawHorizontalLine((int)std::round(eqY(5.f)), eqArea.getX(), eqArea.getRight());
    g.drawHorizontalLine((int)std::round(eqY(-5.f)), eqArea.getX(), eqArea.getRight());
    {
        const float dashes[2] = { 3.f, 3.f };
        g.setColour(juce::Colour(0x88555555));
        g.drawDashedLine({ eqArea.getX(), eqY(0.f), eqArea.getRight(), eqY(0.f) }, dashes, 2, 1.f);
    }
    juce::Path curve;
    constexpr int kCurve = 220;
    for (int i = 0; i < kCurve; ++i)
    {
        const float t = (float)i / (float)(kCurve - 1);
        const float hz = 20.f * std::pow(20000.f / 20.f, t);
        const float x = x0 + t * w;
        const float y = eqY((float)glaze::eqResponseDb(s, hz, sr));
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    g.setColour(juce::Colour(0x33e8e8e8));
    g.strokePath(curve, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(s[glaze::EqOn] >= 0.5f ? ink() : dim());
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.f)));
    auto dbLab = [&](float db, const char *txt) {
        g.drawText(txt, (int)eqArea.getX() + 2, (int)eqY(db) - 7, 28, 14, juce::Justification::centredLeft);
    };
    dbLab(5.f, "+5");
    dbLab(0.f, "0");
    dbLab(-5.f, "-5");

    // spectrum pane: 0 dB at the top, -90 dB at the bottom
    auto spY = [&](float db) {
        const float t = glaze::clampf((db + 90.f) / 90.f, 0.f, 1.f);
        return spArea.getBottom() - t * spArea.getHeight();
    };
    g.setColour(juce::Colour(0x223a3a3a));
    g.drawHorizontalLine((int)std::round(spY(-30.f)), spArea.getX(), spArea.getRight());
    g.drawHorizontalLine((int)std::round(spY(-60.f)), spArea.getX(), spArea.getRight());
    auto buildSpec = [&](const std::array<float, kBins> &bins, bool close) {
        juce::Path path;
        for (int i = 0; i < kBins; ++i)
        {
            const float t = (float)i / (float)(kBins - 1);
            const float x = spArea.getX() + t * spArea.getWidth(), y = spY(bins[(size_t)i]);
            if (i == 0)
            {
                if (close)
                {
                    path.startNewSubPath(x, spArea.getBottom());
                    path.lineTo(x, y);
                }
                else
                    path.startNewSubPath(x, y);
            }
            else
                path.lineTo(x, y);
        }
        if (close)
        {
            path.lineTo(spArea.getRight(), spArea.getBottom());
            path.closeSubPath();
        }
        return path;
    };
    g.setColour(juce::Colour(0x55666666));
    g.fillPath(buildSpec(binsIn, true));
    g.setColour(juce::Colour(0xa0a0a0a0));
    g.strokePath(buildSpec(binsIn, false), juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffd8d8d8));
    g.strokePath(buildSpec(binsOut, false), juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.f)));
    const float lx = spArea.getRight() - 118.f, ly = spArea.getY() + 3.f;
    g.setColour(juce::Colour(0x888a8a8a));
    g.drawLine(lx, ly + 6.f, lx + 14.f, ly + 6.f, 1.2f);
    g.drawText("IN", (int)lx + 16, (int)ly, 22, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffd8d8d8));
    g.drawLine(lx + 42.f, ly + 6.f, lx + 56.f, ly + 6.f, 1.6f);
    g.drawText("OUT", (int)lx + 58, (int)ly, 28, 12, juce::Justification::centredLeft);
    g.setColour(dim());
    g.drawText("EQ CURVE", (int)(eqArea.getRight() - 70), (int)eqArea.getY() + 1, 68, 12, juce::Justification::centredRight);

    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(11.f)));
    for (int i = 0; i < 6; ++i)
    {
        const float x = hzToX(marks[i], labels.getX(), labels.getWidth());
        g.drawText(labs[i], (int)x - 16, (int)labels.getY(), 32, (int)labels.getHeight(), juce::Justification::centred);
    }
}

void GlazeEditor::paintMeters(juce::Graphics &g, juce::Rectangle<float> a)
{
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(a, 4.f);
    g.setColour(line());
    g.drawRoundedRectangle(a, 4.f, 1.f);
    const auto &e = proc.getEngine();
    const float lufsI = e.integrated.load(), lufsM = e.momentary.load(), lufsS = e.shortTerm.load(),
                lufsIn = e.shortTermIn.load(), tp = e.tpMaxDb.load();
    const float x = a.getX() + 14.f, y = a.getY();
    auto font = [&](float h, bool b) { g.setFont(juce::Font(juce::FontOptions(b ? look.faceB : look.face).withHeight(h))); };

    g.setColour(dim());
    font(10.5f, true);
    g.drawText("LOUDNESS (LUFS)", (int)x, (int)y + 8, 150, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    font(44.f, true);
    g.drawText(lufsText(lufsI), (int)x - 2, (int)y + 20, 150, 50, juce::Justification::centredLeft);
    g.setColour(dim());
    font(11.f, false);
    g.drawText("INTEGRATED", (int)x, (int)y + 70, 150, 13, juce::Justification::centredLeft);

    const float cx = a.getX() + 170.f;
    auto row = [&](float yy, const char *lab, const juce::String &v) {
        g.setColour(dim());
        font(10.5f, true);
        g.drawText(lab, (int)cx, (int)(y + yy), 120, 11, juce::Justification::centredLeft);
        g.setColour(ink());
        font(19.f, true);
        g.drawText(v, (int)cx, (int)(y + yy + 10), 120, 22, juce::Justification::centredLeft);
    };
    row(8.f, "MOMENTARY", lufsText(lufsM));
    row(40.f, "SHORT-TERM", lufsText(lufsS));
    row(72.f, "INPUT SHORT-TERM", lufsText(lufsIn));

    // true peak
    const float ceil = proc.getSettings()[glaze::Ceiling];
    const bool hot = tp > ceil + 0.15f && proc.getSettings()[glaze::LimOn] >= 0.5f;
    g.setColour(dim());
    font(10.5f, true);
    g.drawText("TRUE PEAK MAX (dBTP)", (int)x, (int)y + 98, 150, 11, juce::Justification::centredLeft);
    g.setColour(hot ? juce::Colours::white : ink());
    if (hot)
    {
        g.fillRoundedRectangle(x - 4.f, y + 108.f, 112.f, 26.f, 3.f);
        g.setColour(juce::Colours::black);
    }
    font(19.f, true);
    g.drawText(tp <= -100.f ? juce::String("--") : juce::String(tp, 1), (int)x, (int)y + 110, 104, 22, juce::Justification::centredLeft);

    // gain reduction bars
    struct Bar
    {
        const char *label;
        float v, maxv;
    } bars[3] = { { "GLUE", e.glueGrDb.load(), 10.f }, { "MULTI", e.mbGrDb.load(), 10.f }, { "LIMIT", e.limGrDb.load(), 15.f } };
    float by = y + 144.f;
    for (auto &b : bars)
    {
        g.setColour(dim());
        font(10.5f, true);
        g.drawText(b.label, (int)x, (int)by, 44, 14, juce::Justification::centredLeft);
        const juce::Rectangle<float> track(x + 46.f, by + 3.f, a.getWidth() - 14.f * 2 - 46.f - 56.f, 8.f);
        g.setColour(juce::Colour(0xff252525));
        g.fillRoundedRectangle(track, 2.f);
        const float f = glaze::clampf(b.v / b.maxv, 0.f, 1.f);
        g.setColour(ink());
        g.fillRoundedRectangle(track.withWidth(std::max(0.f, track.getWidth() * f)), 2.f);
        g.setColour(dim());
        font(11.f, false);
        g.drawText("-" + juce::String(b.v, 1) + " dB", (int)track.getRight() + 4, (int)by, 52, 14, juce::Justification::centredRight);
        by += 17.f;
    }
}

void GlazeEditor::paint(juce::Graphics &g)
{
    g.fillAll(bg());
    // top bar divider
    g.setColour(line());
    g.drawHorizontalLine(62, 20.f, (float)kW - 20.f);
    paintSpectrum(g, specArea.toFloat());
    paintMeters(g, meterArea.toFloat());
    for (auto &m : modules)
    {
        const auto r = m.area.toFloat();
        g.setColour(panelC());
        g.fillRoundedRectangle(r, 5.f);
        g.setColour(line());
        g.drawRoundedRectangle(r, 5.f, 1.f);
        g.setColour(dim());
        g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(12.5f)));
        g.drawText(m.title, m.area.getX() + 14, m.area.getY() + 4, 220, 22, juce::Justification::centredLeft);
    }
    // multiband labels for the three bands
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.5f)));
    {
        const auto &a = modules[2].area;
        g.drawText("CROSSOVERS", a.getX() + 4, a.getY() + kPanelTitle - 10, 100, 12, juce::Justification::centred);
    }
    g.setColour(updateReady ? ink() : dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(11.5f)));
    const auto ub = updateLinkBounds();
    g.drawText(updateReady ? "Update " + updateInfo.version + " available" : juce::String("Updates"), ub, juce::Justification::centredRight);
}

// ------------------------------------------------------------------------------------------------ menus, files, updates
void GlazeEditor::resetAll()
{
    for (auto *prm : proc.getParameters())
        if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(prm))
            if (rp != proc.state.getParameter("bypass"))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(rp->getDefaultValue());
                rp->endChangeGesture();
            }
    revealPreset(0);
}

// Right-click menu: our own items (with shortcut letters) plus the host's own parameter menu when it offers one
// (VST3 IComponentHandler3 -> automation lane, MIDI learn, ...). Without a host menu there is an "Automate" item.
void GlazeEditor::showParamMenu(const juce::String &id, gzui::Knob *knobPtr, juce::Point<int> pos)
{
    auto *prm = id.isNotEmpty() ? proc.state.getParameter(id) : nullptr;
    juce::Component::SafePointer<gzui::Knob> knob(knobPtr);
    std::vector<gzui::PopupOverlay::Item> v;
    if (prm != nullptr)
    {
        v.push_back({ knob != nullptr ? "&Reset to default\tdouble-click" : "&Reset to default", [prm] {
                         prm->beginChangeGesture();
                         prm->setValueNotifyingHost(prm->getDefaultValue());
                         prm->endChangeGesture();
                     } });
        if (knob != nullptr)
        {
            v.push_back({ "&Type in value...", [knob] {
                             if (knob != nullptr)
                                 knob->typeValue();
                         } });
            v.push_back({ "&Copy value", [knob] {
                             if (knob != nullptr)
                                 juce::SystemClipboard::copyTextToClipboard(knob->getTextFromValue(knob->getValue()));
                         }, true, false, true });
            const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
            const bool pasteOk = clip.isNotEmpty() && clip.length() < 24 && clip.containsAnyOf("0123456789");
            v.push_back({ "&Paste value", [knob, clip, prm] {
                             if (knob != nullptr)
                             {
                                 prm->beginChangeGesture();
                                 knob->setValue(knob->getValueFromText(clip), juce::sendNotificationSync);
                                 prm->endChangeGesture();
                             }
                         }, pasteOk });
        }
        std::shared_ptr<juce::HostProvidedContextMenu> host;
        if (auto *hc = getHostContext())
            if (auto m = hc->getContextMenuForParameter(prm))
                host = std::move(m);
        if (host != nullptr)
        {
            bool first = true;
            std::function<void(const juce::PopupMenu &, const juce::String &)> add = [&](const juce::PopupMenu &m, const juce::String &prefix) {
                juce::PopupMenu::MenuItemIterator it(m);
                while (it.next())
                {
                    auto &item = it.getItem();
                    if (item.isSeparator)
                    {
                        first = true;
                        continue;
                    }
                    if (item.subMenu != nullptr)
                    {
                        add(*item.subMenu, prefix + item.text.replace("&", "") + " > ");
                        continue;
                    }
                    if (item.text.isEmpty() || v.size() > 14)
                        continue;
                    auto act = item.action;
                    v.push_back({ prefix + item.text.replace("&", ""), [host, act] {
                                     if (act)
                                         act();
                                 }, item.isEnabled && (bool)act, item.isTicked, first });
                    first = false;
                }
            };
            add(host->getEquivalentPopupMenu(), {});
        }
        else
        {
            v.push_back({ "&Automate", [this, prm] {
                             prm->beginChangeGesture();
                             prm->setValueNotifyingHost(prm->getValue());
                             prm->endChangeGesture();
                             footerNote = "Selected for automation: now use your host's automation command for this parameter";
                         }, true, false, true });
        }
    }
    v.push_back({ "&Init all controls", [this] { resetAll(); }, true, false, true });
    popup.setBounds(getLocalBounds());
    popup.show(pos, std::move(v));
}

void GlazeEditor::showParamMenuFor(const juce::String &id)
{
    for (auto &c : cells)
        if (c != nullptr && juce::String(glaze::definitions[(size_t)c->param].id) == id)
        {
            showParamMenu(id, &c->knob, c->knob.getBounds().getCentre());
            return;
        }
}

void GlazeEditor::mouseDown(const juce::MouseEvent &e)
{
    if (!e.mods.isPopupMenu() && updateLinkBounds().contains(e.getPosition()))
    {
        if (updateReady)
            juce::URL(gz::upd::pageForUrl(updateInfo.url)).launchInDefaultBrowser();
        else
            showUpdateMenu();
    }
}

juce::Rectangle<int> GlazeEditor::updateLinkBounds() const
{
    return footerArea.withTrimmedLeft(footerArea.getWidth() - 230).reduced(0, 2);
}

void GlazeEditor::startUpdateCheck(bool force)
{
    if (!force && !gz::upd::Settings::load().enabled)
        return;
    checker.start([this, force](const gz::upd::Result &r) {
        if (r.ok && gz::upd::shouldNotify(r, gz::upd::Settings::load(), force))
        {
            updateInfo = r.info;
            updateReady = true;
            footerNote = r.info.notes.isNotEmpty() ? "What's new: " + r.info.notes : juce::String();
        }
        else if (force)
            footerNote = r.ok ? juce::String("GLAZE ") + GLAZE_VERSION_STRING + " is up to date"
                              : juce::String("Could not reach the update server");
        repaint();
    }, force);
}

void GlazeEditor::showUpdateMenu()
{
    const bool on = gz::upd::Settings::load().enabled;
    std::vector<gzui::PopupOverlay::Item> v;
    v.push_back({ "Check for &updates automatically", [this, on] {
                     auto s = gz::upd::Settings::load();
                     s.enabled = !on;
                     s.save();
                     footerNote = s.enabled ? "Update check on (once a day, nothing is sent about you)"
                                            : "Update check off: the plugin never contacts the network";
                 }, true, on });
    v.push_back({ "Check &now", [this] {
                     footerNote = "Checking...";
                     startUpdateCheck(true);
                 } });
    const auto b = updateLinkBounds();
    popup.setBounds(getLocalBounds());
    popup.show({ b.getRight() - 250, b.getY() - 70 }, std::move(v));
}

void GlazeEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CMPLXTY/GLAZE/Presets");
    folder.createDirectory();
    const juce::String ext = "*.glaze";
    if (write)
    {
        juce::FileChooser ch("Save GLAZE preset", folder, ext);
        if (ch.browseForFileToSave(true))
        {
            auto f = ch.getResult();
            if (auto xml = proc.state.copyState().createXml())
                xml->writeTo(f);
        }
    }
    else
    {
        juce::FileChooser ch("Load GLAZE preset", folder, ext);
        if (ch.browseForFileToOpen())
        {
            auto f = ch.getResult();
            if (auto xml = juce::XmlDocument::parse(f))
            {
                proc.state.replaceState(juce::ValueTree::fromXml(*xml));
                proc.snapEngine();
            }
        }
    }
    gzui::hostkeys::releaseLater(*this);
}

// Walks every component: nothing except the open popup / a text editor may want keyboard focus.
juce::String GlazeEditor::auditKeyboard()
{
    juce::String out;
    int n = 0, bad = 0;
    for (auto &c : cells)
        if (c != nullptr)
        {
            c->knob.setEnabled(false);
            c->knob.setEnabled(true);
            c->knob.sendLookAndFeelChange();
        }
    std::function<void(juce::Component &)> walk = [&](juce::Component &c) {
        ++n;
        if (c.getWantsKeyboardFocus() && &c != &popup && dynamic_cast<juce::TextEditor *>(&c) == nullptr)
        {
            ++bad;
            out << "  wants focus: " << c.getName() << " (" << typeid(c).name() << ")\n";
        }
        for (auto *ch : c.getChildren())
            walk(*ch);
    };
    walk(*this);
    out << "  " << n << " components, " << bad << " ask for keyboard focus (expected 0)\n";
    return out;
}

