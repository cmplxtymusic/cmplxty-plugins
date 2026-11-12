#include "PluginEditor.h"
#include "BinaryData.h"
HeftLook::HeftLook()
{
    face = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf,
                                                   BinaryData::BarlowRegular_ttfSize);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff181818));
    setColour(juce::Slider::trackColourId, juce::Colour(0xffbbbbbb));
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff555555));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
}
juce::Font HeftLook::getLabelFont(juce::Label &)
{
    return juce::Font(juce::FontOptions(face).withHeight(13));
}
juce::Font HeftLook::getComboBoxFont(juce::ComboBox &)
{
    return juce::Font(juce::FontOptions(face).withHeight(14));
}
juce::Font HeftLook::getTextButtonFont(juce::TextButton &, int)
{
    return juce::Font(juce::FontOptions(face).withHeight(14));
}
void HeftLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float start,
                                float end, juce::Slider &)
{
    auto b = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(8);
    float radius = std::min(b.getWidth(), b.getHeight()) * .5f;
    auto c = b.getCentre();
    g.setColour(juce::Colour(0xff262626));
    g.fillEllipse(c.x - radius, c.y - radius, 2 * radius, 2 * radius);
    juce::Path track;
    track.addCentredArc(c.x, c.y, radius - 2, radius - 2, 0, start, end, true);
    g.setColour(juce::Colour(0xff555555));
    g.strokePath(track, juce::PathStrokeType(2));
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, radius - 2, radius - 2, 0, start, start + pos * (end - start), true);
    g.setColour(juce::Colours::white);
    g.strokePath(arc, juce::PathStrokeType(3));
    float angle = start + pos * (end - start);
    g.drawLine(c.x + std::sin(angle) * radius * .55f, c.y - std::cos(angle) * radius * .55f,
               c.x + std::sin(angle) * radius * .84f, c.y - std::cos(angle) * radius * .84f, 3);
}
HeftEditor::HeftEditor(HeftProcessor &p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&look);
    setWantsKeyboardFocus(false);
    for (int i = 0; i < heft::Count; ++i)
    {
        auto &k = knobs[i];
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 20);
        k.setWantsKeyboardFocus(false);
        k.setMouseClickGrabsKeyboardFocus(false);
        const auto &d = heft::definitions[i];
        k.setDoubleClickReturnValue(true, d.initial);
        k.setNumDecimalPlacesToDisplay(d.high <= 1 ? 2 : 1);
        k.setTextValueSuffix(juce::String(" ") + d.unit);
        k.setTooltip(juce::String(d.name) +
                     ": drag to adjust; double-click resets. All controls are automatable.");
        addAndMakeVisible(k);
        labels[i].setText(d.name, juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels[i]);
        attachments.push_back(std::make_unique<SA>(proc.state, d.id, k));
        k.textFromValueFunction = [i](double v)
        {
            const auto &def = heft::definitions[i];
            return juce::String(v, def.high <= 1 || def.high < 20 ? 2 : (def.high > 100 ? 0 : 1));
        };
        k.valueFromTextFunction = [](const juce::String &text) { return double(text.getFloatValue()); };
        k.updateText();
    }
    auto names = HeftProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i)
        presets.addItem(names[i], i + 1);
    presets.setTextWhenNothingSelected("HEFT / choose a preset");
    presets.onChange = [this]
    {
        if (presets.getSelectedId() > 0)
            proc.factory(presets.getSelectedId() - 1);
    };
    addAndMakeVisible(presets);
    diceButton.onClick = [this]
    {
        proc.dice();
        presets.setSelectedId(0, juce::dontSendNotification);
    };
    cover.onClick = [this] { showAdvanced(!advanced); };
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    for (auto *b : {&diceButton, &cover, &save, &load})
    {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    }
    diceButton.setTooltip(
        "Randomize the colour sections and one modulation route. Mix, output and limiter stay safe.");
    for (int i = 0; i < 4; ++i)
    {
        int j = 1;
        for (auto name : {"Off", "Envelope", "LFO", "Noise", "Sidechain"})
            sources[i].addItem(name, j++);
        j = 1;
        for (auto name : {"Slam", "Fuzz", "Wide", "Disperser", "Freq shift", "Transient", "Tight"})
            destinations[i].addItem(name, j++);
        addAndMakeVisible(sources[i]);
        addAndMakeVisible(destinations[i]);
        depths[i].setSliderStyle(juce::Slider::LinearHorizontal);
        depths[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        depths[i].setNumDecimalPlacesToDisplay(2);
        depths[i].setWantsKeyboardFocus(false);
        addAndMakeVisible(depths[i]);
        juce::String id = "mod" + juce::String(i);
        choices.push_back(std::make_unique<CA>(proc.state, id + "src", sources[i]));
        choices.push_back(std::make_unique<CA>(proc.state, id + "dst", destinations[i]));
        attachments.push_back(std::make_unique<SA>(proc.state, id + "amt", depths[i]));
    }
    status.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status);
    setSize(960, 570);
    showAdvanced(false);
    startTimerHz(15);
}
HeftEditor::~HeftEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}
void HeftEditor::showAdvanced(bool on)
{
    advanced = on;
    cover.setButtonText(on ? "UNDER THE HOOD -" : "UNDER THE HOOD +");
    for (int i = heft::Drive; i < heft::Count; ++i)
    {
        knobs[i].setVisible(on);
        labels[i].setVisible(on);
    }
    for (int i = 0; i < 4; ++i)
    {
        sources[i].setVisible(on);
        destinations[i].setVisible(on);
        depths[i].setVisible(on);
    }
    setSize(960, on ? 920 : 570);
    resized();
    repaint();
}
void HeftEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colour(0xff101010));
    g.setColour(juce::Colour(0xff4a4a4a));
    g.drawRect(getLocalBounds(), 1);
    g.drawHorizontalLine(73, 24, 936);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(37)));
    g.drawText("HEFT", 26, 13, 150, 48, juce::Justification::centredLeft);
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(13)));
    g.drawText("CMPLXTY / DENSITY & MOTION", 180, 28, 290, 25, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff777777));
    g.drawText("01  /  WEIGHT", 40, 91, 230, 24, juce::Justification::centredLeft);
    g.drawText("02  /  COLOUR & SHAPE", 325, 91, 580, 24, juce::Justification::centredLeft);
    g.drawVerticalLine(300, 100, 480);
    g.drawHorizontalLine(485, 24, 936);
    g.setColour(juce::Colour(0xffaaaaaa));
    g.drawText("DISTORTION + COMPRESSION", 35, 397, 245, 22, juce::Justification::centred);
    g.drawText("Linked safety ceiling / -1 dBFS", 35, 423, 245, 22, juce::Justification::centred);
    g.setColour(juce::Colour(0xff777777));
    g.drawText("v1.0.0  /  64-sample latency  /  double-click to reset", 28, 535, 535, 22,
               juce::Justification::centredLeft);
    if (advanced)
    {
        g.drawHorizontalLine(573, 24, 936);
        g.setColour(juce::Colours::white);
        g.drawText("03 / INTERNALS", 28, 580, 300, 22, juce::Justification::centredLeft);
        g.drawText("04 / MODULATION   source > destination > bipolar depth", 28, 756, 760, 22,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff999999));
        g.drawText("Envelope / sidechain: unipolar. LFO / noise: bipolar. MOTION scales all routes. Shift "
                   "depth: +/-250 Hz.",
                   28, 891, 900, 20, juce::Justification::centredLeft);
    }
}
void HeftEditor::resized()
{
    presets.setBounds(480, 24, 265, 28);
    diceButton.setBounds(760, 24, 72, 28);
    save.setBounds(843, 24, 48, 28);
    load.setBounds(895, 24, 48, 28);
    labels[0].setBounds(55, 122, 210, 26);
    knobs[0].setBounds(43, 149, 235, 242);
    const int ids[] = {heft::Fuzz, heft::Wide, heft::Smear, heft::Shift, heft::Transient, heft::Tight};
    for (int n = 0; n < 6; ++n)
    {
        int x = 325 + (n % 3) * 155, y = 125 + (n / 3) * 175;
        labels[ids[n]].setBounds(x, y, 140, 20);
        knobs[ids[n]].setBounds(x, y + 23, 140, 140);
    }
    for (int n = 0; n < 2; ++n)
    {
        int id = heft::Mix + n;
        labels[id].setBounds(804, 143 + n * 166, 128, 20);
        knobs[id].setBounds(804, 166 + n * 166, 128, 127);
    }
    cover.setBounds(28, 500, 210, 28);
    status.setBounds(490, 500, 440, 28);
    for (int i = heft::Drive; i < heft::Count; ++i)
    {
        int n = i - heft::Drive, x = 25 + n * 92;
        labels[i].setBounds(x, 611, 90, 20);
        knobs[i].setBounds(x, 635, 90, 111);
    }
    for (int i = 0; i < 4; ++i)
    {
        int x = 30 + (i % 2) * 465, y = 790 + (i / 2) * 46;
        sources[i].setBounds(x, y, 130, 27);
        destinations[i].setBounds(x + 140, y, 140, 27);
        depths[i].setBounds(x + 285, y, 155, 27);
    }
}
void HeftEditor::timerCallback()
{
    float db = juce::Decibels::gainToDecibels(proc.meter.load(), -100.f);
    status.setText("OUT " + juce::String(db, 1) + " dBFS   /   LIMIT " +
                       juce::String(proc.reduction.load(), 1) + " dB",
                   juce::dontSendNotification);
    const auto s = proc.getSettings();
    auto dim = [&](heft::Parameter id, bool on) {
        const float a = on ? 1.0f : 0.38f;
        knobs[(size_t)id].setAlpha(a);
        labels[(size_t)id].setAlpha(a);
    };
    dim(heft::Tone, s.value[heft::Fuzz] > 0.001f);          // NOISE TONE
    dim(heft::Focus, s.value[heft::Smear] > 0.001f);        // DISP FOCUS
    // LFO RATE, ENV RELEASE, MOTION and the four mod routes: only live when a route uses them
    bool anyRoute = false, lfoUsed = false, envUsed = false;
    for (int i = 0; i < 4; ++i)
    {
        const bool assigned = s.routes[i].source != 0 && s.routes[i].destination != 0 && std::abs(s.routes[i].amount) > 0.001f;
        anyRoute = anyRoute || assigned;
        // Combo items: 0 Off, 1 Envelope, 2 LFO, 3 Noise, 4 Sidechain
        const int src = s.routes[i].source;
        if (assigned && src == 2) lfoUsed = true;
        if (assigned && src == 1) envUsed = true;
        const float ra = assigned ? 1.0f : 0.38f;
        sources[(size_t)i].setAlpha(ra);
        destinations[(size_t)i].setAlpha(ra);
        depths[(size_t)i].setAlpha(ra);
    }
    dim(heft::Rate, lfoUsed);
    dim(heft::EnvTime, envUsed);
    dim(heft::Motion, anyRoute);
}
void HeftEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("CMPLXTY/HEFT/Presets");
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(write ? "Save HEFT preset" : "Load HEFT preset", folder,
                                                  "*.heft");
    juce::Component::SafePointer<HeftEditor> safe(this);
    chooser->launchAsync(
        juce::FileBrowserComponent::canSelectFiles |
            (write ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                   : juce::FileBrowserComponent::openMode),
        [safe, write](const juce::FileChooser &c)
        {
            if (!safe)
                return;
            auto f = c.getResult();
            if (f == juce::File())
                return;
            if (write)
            {
                juce::MemoryBlock b;
                safe->proc.getStateInformation(b);
                if (!f.withFileExtension("heft").replaceWithData(b.getData(), b.getSize()))
                    safe->status.setText("Could not save preset", juce::dontSendNotification);
            }
            else
            {
                juce::MemoryBlock b;
                if (f.loadFileAsData(b))
                    safe->proc.setStateInformation(b.getData(), int(b.getSize()));
            }
            safe->presets.setSelectedId(0, juce::dontSendNotification);
        });
}
