#include "PluginEditor.h"
#include "BinaryData.h"
#include "Presets.h"
#include <cmath>

namespace
{
juce::Colour bg() { return juce::Colour(0xff0c0c0c); }
juce::Colour panel() { return juce::Colour(0xff161616); }
juce::Colour ink() { return juce::Colour(0xffe8e8e8); }
juce::Colour dim() { return juce::Colour(0xff8a8a8a); }
juce::Colour line() { return juce::Colour(0xff3a3a3a); }

float hzToX(float hz, float x0, float w, float fMin = 40.f, float fMax = 20000.f)
{
    const float t = std::log(std::max(hz, fMin) / fMin) / std::log(fMax / fMin);
    return x0 + stoke::clampf(t, 0.f, 1.f) * w;
}
} // namespace

StokeLook::StokeLook()
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

juce::Font StokeLook::getLabelFont(juce::Label &)
{
    return juce::Font(juce::FontOptions(face).withHeight(13.f));
}
juce::Font StokeLook::getComboBoxFont(juce::ComboBox &)
{
    return juce::Font(juce::FontOptions(face).withHeight(14.f));
}
juce::Font StokeLook::getTextButtonFont(juce::TextButton &, int)
{
    return juce::Font(juce::FontOptions(faceB).withHeight(13.f));
}
juce::Font StokeLook::getPopupMenuFont() { return juce::Font(juce::FontOptions(face).withHeight(14.f)); }

void StokeLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1,
                                 juce::Slider &s)
{
    const auto b =
        juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(s.isEnabled() ? 5.f : 6.f);
    const float r = std::min(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const bool hero = r > 80.f;
    g.setColour(juce::Colour(0xff141414));
    g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
    g.setColour(hero ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff3a3a3a));
    g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, hero ? 2.4f : 1.4f);

    juce::Path track;
    const float rad = r - (hero ? 10.f : 6.f);
    track.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
    g.setColour(juce::Colour(0xff2a2a2a));
    g.strokePath(track, juce::PathStrokeType(hero ? 6.f : 3.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    juce::Path fill;
    const float ang = a0 + pos * (a1 - a0);
    fill.addCentredArc(c.x, c.y, rad, rad, 0, a0, ang, true);
    g.setColour(ink());
    g.strokePath(fill, juce::PathStrokeType(hero ? 6.f : 3.5f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    const float nx = c.x + std::cos(ang) * (r - (hero ? 22.f : 14.f));
    const float ny = c.y + std::sin(ang) * (r - (hero ? 22.f : 14.f));
    g.setColour(juce::Colours::white);
    g.drawLine(c.x, c.y, nx, ny, hero ? 2.6f : 1.8f);
    g.fillEllipse(c.x - (hero ? 5.f : 3.f), c.y - (hero ? 5.f : 3.f), hero ? 10.f : 6.f, hero ? 10.f : 6.f);
}

void StokeLook::drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool)
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

void StokeLook::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over,
                                     bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    g.setColour(down ? juce::Colour(0xff3a3a3a) : (over ? juce::Colour(0xff2c2c2c) : juce::Colour(0xff1c1c1c)));
    g.fillRoundedRectangle(r, 3.f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(r, 3.f, 1.f);
}

void StokeLook::drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &)
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

void StokeLook::drawPopupMenuBackground(juce::Graphics &g, int w, int h)
{
    g.fillAll(juce::Colour(0xff141414));
    g.setColour(line());
    g.drawRect(0, 0, w, h, 1);
}

StokeEditor::StokeEditor(StokeProcessor &p)
    : AudioProcessorEditor(p), proc(p),
      aFocus(*p.state.getParameter("focus"), focus), aCentre(*p.state.getParameter("centre"), centre),
      aWidth(*p.state.getParameter("width"), width), aGlow(*p.state.getParameter("glow"), glow),
      aSmooth(*p.state.getParameter("smooth"), smooth), aMix(*p.state.getParameter("mix"), mix),
      aOut(*p.state.getParameter("output"), output),
      aMid(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("midonly")), midOnly),
      aMatch(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("gainmatch")), match),
      aLimit(*dynamic_cast<juce::AudioParameterBool *>(p.state.getParameter("limiter")), limit)
{
    setLookAndFeel(&look);
    setOpaque(true);
    setSize(800, 720);
    setWantsKeyboardFocus(false);

    makeKnob(focus, "FOCUS: how hard the mids come forward. At zero the sound is untouched.");
    makeKnob(centre, "CENTRE: where in the midrange to lift, from 300 Hz (body) to 5 kHz (bite).");
    makeKnob(width, "WIDTH: how wide the lift is, in octaves. Narrow is a tight presence; wide is a broad mid.");
    makeKnob(glow, "GLOW: adds warm mid harmonics on the lifted band. Zero is a clean boost.");
    makeKnob(smooth, "SMOOTH: tames harsh peaks around 2-5 kHz and low-mid mud, automatically.");
    makeKnob(mix, "MIX: wet / dry.");
    makeKnob(output, "OUTPUT: gain in decibels.");

    makeLab(brand, "CMPLXTY", 13.f, true);
    brand.setJustificationType(juce::Justification::centredLeft);
    makeLab(title, "STOKE", 28.f, true);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    makeLab(focusName, "FOCUS", 16.f, true);
    makeLab(centreName, "CENTRE", 12.f, true);
    makeLab(widthName, "WIDTH", 12.f, true);
    makeLab(glowName, "GLOW", 12.f, true);
    makeLab(smoothName, "SMOOTH", 12.f, true);
    makeLab(mixName, "MIX", 11.5f, true);
    makeLab(outName, "OUTPUT", 11.5f, true);
    for (auto *v : { &focusVal, &centreVal, &widthVal, &glowVal, &smoothVal, &mixVal, &outVal })
    {
        makeLab(*v, "", 12.f, false);
        v->setColour(juce::Label::textColourId, dim());
    }
    makeLab(status, "", 12.f, false);
    status.setJustificationType(juce::Justification::centredRight);

    auto setupTog = [this](juce::ToggleButton &b, const char *t, const char *tip) {
        b.setButtonText(t);
        b.setTooltip(tip);
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };
    setupTog(midOnly, "MID ONLY", "Lift only the centre of a stereo image. The sides stay as they were.");
    setupTog(match, "MATCH", "Automatic gain match. Keeps loudness close to the input. On by default.");
    setupTog(limit, "LIMIT", "Safety limiter at -1 dBFS. On by default.");

    presets.addItemList(StokeProcessor::presetNames(), 1);
    presets.setSelectedItemIndex(0, juce::dontSendNotification);
    presets.onChange = [this] {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0)
        {
            proc.factory(i);
            currentPreset = i;
        }
    };
    presets.setWantsKeyboardFocus(false);
    addAndMakeVisible(presets);

    auto setupBtn = [this](juce::TextButton &b) {
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };
    setupBtn(prev);
    setupBtn(next);
    setupBtn(save);
    setupBtn(load);
    prev.onClick = [this] {
        const int i = (currentPreset + stoke::presetCount - 1) % stoke::presetCount;
        revealPreset(i);
        proc.factory(i);
    };
    next.onClick = [this] {
        const int i = (currentPreset + 1) % stoke::presetCount;
        revealPreset(i);
        proc.factory(i);
    };
    save.setTooltip("Save a user preset as a .stoke file.");
    load.setTooltip("Load a .stoke user preset.");
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };

    binsIn.fill(-90.f);
    binsOut.fill(-90.f);
    startTimerHz(30);
    updateDim();
}

StokeEditor::~StokeEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void StokeEditor::makeKnob(juce::Slider &s, const char *tip)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 2.25f,
                          true);
    s.setTooltip(tip);
    s.setWantsKeyboardFocus(false);
    s.setDoubleClickReturnValue(true, s.getValue()); // attachment overwrites; we set after
    addAndMakeVisible(s);
}

void StokeEditor::makeLab(juce::Label &l, const char *t, float h, bool bold)
{
    l.setText(t, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(juce::FontOptions(bold ? look.faceB : look.face).withHeight(h)));
    l.setColour(juce::Label::textColourId, bold ? ink() : dim());
    l.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(l);
}

void StokeEditor::revealPreset(int index)
{
    currentPreset = stoke::clampi(index, 0, stoke::presetCount - 1);
    presets.setSelectedItemIndex(currentPreset, juce::dontSendNotification);
}

void StokeEditor::updateDim()
{
    const auto s = proc.getSettings();
    const bool on = stoke::focusActive(s[stoke::Focus]);
    const auto setA = [](juce::Component &c, bool active) { c.setAlpha(active ? 1.f : 0.38f); };
    setA(glow, stoke::glowActive(s[stoke::Focus], s[stoke::Glow]) || on);
    setA(glowName, on);
    setA(glowVal, on);
    setA(smooth, on);
    setA(smoothName, on);
    setA(smoothVal, on);
    setA(midOnly, on);
    setA(match, on);
}

void StokeEditor::timerCallback()
{
    auto fmt = [this](juce::Slider &s, juce::Label &l) {
        if (auto *p = proc.state.getParameter(s.getComponentID().isEmpty() ? juce::String() : s.getComponentID()))
            l.setText(p->getCurrentValueAsText(), juce::dontSendNotification);
    };
    // Attachments don't set component IDs; just use the parameter directly.
    auto text = [this](const char *id) {
        if (auto *p = proc.state.getParameter(id))
            return p->getCurrentValueAsText();
        return juce::String();
    };
    focusVal.setText(text("focus"), juce::dontSendNotification);
    centreVal.setText(text("centre"), juce::dontSendNotification);
    widthVal.setText(text("width"), juce::dontSendNotification);
    glowVal.setText(text("glow"), juce::dontSendNotification);
    smoothVal.setText(text("smooth"), juce::dontSendNotification);
    mixVal.setText(text("mix"), juce::dontSendNotification);
    outVal.setText(text("output"), juce::dontSendNotification);

    const float mk = proc.makeup.load();
    const float rd = proc.reduction.load();
    juce::String st;
    if (std::abs(mk) > 0.15f)
        st = (mk >= 0 ? "+" : "") + juce::String(mk, 1) + " dB match";
    if (rd > 0.15f)
        st = st + (st.isEmpty() ? "" : "   ") + "-" + juce::String(rd, 1) + " dB limit";
    status.setText(st, juce::dontSendNotification);

    const auto &eng = proc.getEngine();
    const int wr = eng.scopeW.load(std::memory_order_relaxed);
    analyseScope(eng.scopeIn, wr, binsIn);
    analyseScope(eng.scope, wr, binsOut);

    updateDim();
    (void)fmt;
    repaint();
}

void StokeEditor::analyseScope(const std::array<float, stoke::Engine::kScope> &ring, int writePos,
                              std::array<float, kBins> &dest)
{
    constexpr int N = 2048;
    std::array<float, N> re{}, im{};
    for (int i = 0; i < N; ++i)
    {
        const int idx = (writePos - N + i + stoke::Engine::kScope) & (stoke::Engine::kScope - 1);
        const float win = 0.5f - 0.5f * std::cos(2.f * stoke::kPi * (float)i / (float)(N - 1));
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
        const float ang = -2.f * stoke::kPi / (float)len;
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
        if (hz < 40.f || hz > 20000.f)
            continue;
        const float t = std::log(hz / 40.f) / std::log(20000.f / 40.f);
        const int b = stoke::clampi((int)(t * (float)kBins), 0, kBins - 1);
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


void StokeEditor::paintSpectrum(juce::Graphics &g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour(0xff101010));
    g.fillRoundedRectangle(area, 4.f);
    g.setColour(line());
    g.drawRoundedRectangle(area, 4.f, 1.f);

    auto body = area.reduced(10.f, 8.f);
    const auto labels = body.removeFromBottom(16.f);
    const auto eqArea = body.removeFromTop(64.f);
    body.removeFromTop(4.f);
    const auto spArea = body;

    const auto s = proc.getSettings();
    const float cHz = stoke::centreHz(s[stoke::Centre]);
    const float oct = stoke::clampf(s[stoke::Width], stoke::kWidthMin, stoke::kWidthMax);
    const float lo = cHz / std::pow(2.f, oct * 0.5f);
    const float hi = cHz * std::pow(2.f, oct * 0.5f);
    const float x0 = eqArea.getX(), w = eqArea.getWidth();
    const float xl = hzToX(lo, x0, w), xr = hzToX(hi, x0, w), xc = hzToX(cHz, x0, w);

    // Subtle mid-band region across both panes.
    g.setColour(juce::Colour(0x0ae8e8e8));
    const float bandW = std::max(2.f, xr - xl);
    g.fillRect(xl, eqArea.getY(), bandW, eqArea.getHeight());
    g.fillRect(xl, spArea.getY(), bandW, spArea.getHeight());

    // Frequency grid.
    const float marks[] = { 100.f, 300.f, 1000.f, 3000.f, 10000.f };
    const char *labs[] = { "100", "300", "1k", "3k", "10k" };
    g.setColour(juce::Colour(0x223a3a3a));
    for (int i = 0; i < 5; ++i)
    {
        const float x = hzToX(marks[i], x0, w);
        g.drawLine(x, eqArea.getY(), x, spArea.getBottom(), 1.f);
    }
    g.setColour(juce::Colour(0x333a3a3a));
    g.drawLine(xc, eqArea.getY(), xc, spArea.getBottom(), 1.f);

    // ---- EQ curve pane: +12 dB at the top, -12 dB at the bottom. ----
    auto eqY = [&](float db) {
        const float t = stoke::clampf((db + 12.f) / 24.f, 0.f, 1.f);
        return eqArea.getBottom() - t * eqArea.getHeight();
    };
    g.setColour(juce::Colour(0x443a3a3a));
    g.drawHorizontalLine((int)std::round(eqY(6.f)), eqArea.getX(), eqArea.getRight());
    g.drawHorizontalLine((int)std::round(eqY(-6.f)), eqArea.getX(), eqArea.getRight());
    {
        const float dashes[2] = { 3.f, 3.f };
        g.setColour(juce::Colour(0x88555555));
        g.drawDashedLine({ eqArea.getX(), eqY(0.f), eqArea.getRight(), eqY(0.f) }, dashes, 2, 1.f);
    }

    const auto &eng = proc.getEngine();
    juce::Path curve;
    constexpr int kCurve = 200;
    for (int i = 0; i < kCurve; ++i)
    {
        const float t = (float)i / (float)(kCurve - 1);
        const float hz = 40.f * std::pow(20000.f / 40.f, t);
        const float x = x0 + t * w;
        const float y = eqY(eng.curveMagDb(hz));
        if (i == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    g.setColour(juce::Colour(0x33e8e8e8));
    g.strokePath(curve, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffe8e8e8));
    g.strokePath(curve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.f)));
    auto dbLab = [&](float db, const char *txt) {
        g.drawText(txt, (int)eqArea.getX() + 2, (int)eqY(db) - 7, 28, 14, juce::Justification::centredLeft);
    };
    dbLab(6.f, "+6");
    dbLab(0.f, "0");
    dbLab(-6.f, "-6");

    // ---- Spectrum pane: 0 dB at the top, -90 dB at the bottom. ----
    auto spY = [&](float db) {
        const float t = stoke::clampf((db + 90.f) / 90.f, 0.f, 1.f);
        return spArea.getBottom() - t * spArea.getHeight();
    };
    g.setColour(juce::Colour(0x223a3a3a));
    g.drawHorizontalLine((int)std::round(spY(-30.f)), spArea.getX(), spArea.getRight());
    g.drawHorizontalLine((int)std::round(spY(-60.f)), spArea.getX(), spArea.getRight());

    auto buildSpec = [&](const std::array<float, kBins> &bins, bool close) {
        juce::Path path;
        bool started = false;
        for (int i = 0; i < kBins; ++i)
        {
            const float t = (float)i / (float)(kBins - 1);
            const float x = spArea.getX() + t * spArea.getWidth();
            const float y = spY(bins[(size_t)i]);
            if (!started)
            {
                if (close)
                    path.startNewSubPath(x, spArea.getBottom());
                else
                    path.startNewSubPath(x, y);
                if (close)
                    path.lineTo(x, y);
                started = true;
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

    auto inPath = buildSpec(binsIn, true);
    g.setColour(juce::Colour(0x55666666));
    g.fillPath(inPath);
    g.setColour(juce::Colour(0xa0a0a0a0));
    g.strokePath(buildSpec(binsIn, false),
                 juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto outPath = buildSpec(binsOut, false);
    g.setColour(juce::Colour(0xffd8d8d8));
    g.strokePath(outPath, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Legend.
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.f)));
    const float lx = spArea.getRight() - 118.f;
    const float ly = spArea.getY() + 3.f;
    g.setColour(juce::Colour(0x888a8a8a));
    g.drawLine(lx, ly + 6.f, lx + 14.f, ly + 6.f, 1.2f);
    g.drawText("IN", (int)lx + 16, (int)ly, 22, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffd8d8d8));
    g.drawLine(lx + 42.f, ly + 6.f, lx + 56.f, ly + 6.f, 1.6f);
    g.drawText("OUT", (int)lx + 58, (int)ly, 28, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffe8e8e8));
    g.drawLine(lx + 90.f, eqArea.getY() + 6.f, lx + 104.f, eqArea.getY() + 6.f, 1.6f);
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(10.f)));
    g.setColour(dim());
    g.drawText("EQ", (int)lx + 106, (int)eqArea.getY() + 1, 22, 12, juce::Justification::centredLeft);

    // Frequency labels sit in a reserved strip so they never cover the curves.
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(11.f)));
    for (int i = 0; i < 5; ++i)
    {
        const float x = hzToX(marks[i], labels.getX(), labels.getWidth());
        g.drawText(labs[i], (int)x - 16, (int)labels.getY(), 32, (int)labels.getHeight(),
                   juce::Justification::centred);
    }
}

void StokeEditor::paint(juce::Graphics &g)
{
    g.fillAll(bg());
    g.setColour(juce::Colour(0xff101010));
    g.fillRect(0, 0, getWidth(), 86);
    g.setColour(line());
    g.drawLine(0, 86, (float)getWidth(), 86, 1);

    const auto spec = juce::Rectangle<float>(24.f, 94.f, (float)getWidth() - 48.f, 176.f);
    paintSpectrum(g, spec);

    g.setColour(line());
    g.drawLine(24.f, (float)getHeight() - 32.f, (float)getWidth() - 24.f, (float)getHeight() - 32.f, 1);
    g.setColour(dim());
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(12.f)));
    g.drawText("v1.0.0    double-click a knob to reset    CMPLXTY", 28, getHeight() - 26, getWidth() - 56, 20,
               juce::Justification::centredLeft);
}

void StokeEditor::resized()
{
    // Default window is 800 x 720. Every control is laid out inside a 28 px margin.
    brand.setBounds(28, 14, 90, 18);
    title.setBounds(118, 8, 160, 36);
    status.setBounds(getWidth() - 280, 14, 252, 20);

    presets.setBounds(28, 50, 220, 26);
    prev.setBounds(256, 50, 28, 26);
    next.setBounds(288, 50, 28, 26);
    save.setBounds(328, 50, 64, 26);
    load.setBounds(396, 50, 64, 26);

    // Spectrum occupies y = 94..270. Controls start at y = 286.
    const int ky = 286;
    focus.setBounds(40, ky, 220, 220);
    focusName.setBounds(40, 506, 220, 20);
    focusVal.setBounds(40, 526, 220, 18);

    // Three-button column under FOCUS, even 10 px gaps, same width as the knob.
    midOnly.setBounds(40, 554, 220, 28);
    match.setBounds(40, 592, 220, 28);
    limit.setBounds(40, 630, 220, 28);

    auto place = [](juce::Slider &s, juce::Label &n, juce::Label &v, int x, int y) {
        s.setBounds(x, y, 156, 124);
        n.setBounds(x, y + 124, 156, 18);
        v.setBounds(x, y + 142, 156, 16);
    };
    // Right cluster: 40 px right margin (last column ends at 760).
    const int rx = 280;
    const int gap = 160;
    place(centre, centreName, centreVal, rx, ky);
    place(width, widthName, widthVal, rx + gap, ky);
    place(glow, glowName, glowVal, rx + 2 * gap, ky);
    const int ky2 = 456;
    place(smooth, smoothName, smoothVal, rx, ky2);
    place(mix, mixName, mixVal, rx + gap, ky2);
    place(output, outName, outVal, rx + 2 * gap, ky2);
}

void StokeEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("CMPLXTY/STOKE/Presets");
    folder.createDirectory();
    const juce::String ext = "*.stoke";
    if (write)
    {
        juce::FileChooser ch("Save STOKE preset", folder, ext);
        if (ch.browseForFileToSave(true))
        {
            auto f = ch.getResult();
            if (auto xml = proc.state.copyState().createXml())
                xml->writeTo(f);
        }
    }
    else
    {
        juce::FileChooser ch("Load STOKE preset", folder, ext);
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
}
