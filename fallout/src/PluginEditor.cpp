#include "PluginEditor.h"
#include "BinaryData.h"
namespace
{
const juce::Colour bg(0xff101010), panel(0xff191919), line(0xff4a4a4a), dim(0xff777777),
    accent(0xffe8e8e8), ashCol(0xffb8b8b8), slideCol(0xff9ad0c0), accentMark(0xfff2d08b);
}
FalloutLook::FalloutLook()
{
    face = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf,
                                                   BinaryData::BarlowRegular_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf,
                                                   BinaryData::BarlowSemiBold_ttfSize);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff181818));
    setColour(juce::Slider::trackColourId, juce::Colour(0xffbbbbbb));
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff555555));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a3a));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff333333));
    setColour(juce::Label::textColourId, juce::Colours::white);
}
juce::Font FalloutLook::getLabelFont(juce::Label &)
{
    return juce::Font(juce::FontOptions(face).withHeight(12));
}
juce::Font FalloutLook::getComboBoxFont(juce::ComboBox &)
{
    return juce::Font(juce::FontOptions(face).withHeight(13));
}
juce::Font FalloutLook::getTextButtonFont(juce::TextButton &, int)
{
    return juce::Font(juce::FontOptions(face).withHeight(12));
}
juce::Font FalloutLook::getPopupMenuFont() { return juce::Font(juce::FontOptions(face).withHeight(13)); }
void FalloutLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float start,
                                   float end, juce::Slider &)
{
    auto b = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(7);
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
GridView::GridView(FalloutProcessor &p) : proc(p)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}
juce::Rectangle<int> GridView::gridBounds() const { return getLocalBounds().reduced(36, 8).withTrimmedBottom(4); }
int GridView::rowAt(juce::Point<int> p) const
{
    auto g = gridBounds();
    if (!g.contains(p))
        return -1;
    const float rh = g.getHeight() / float(fallout::gridRows);
    int r = int((p.y - g.getY()) / rh);
    return juce::jlimit(0, fallout::gridRows - 1, r);
}
int GridView::colAt(juce::Point<int> p) const
{
    auto g = gridBounds();
    auto pat = proc.pattern();
    if (!g.contains(p) || pat.length < 1)
        return -1;
    const float cw = g.getWidth() / float(pat.length);
    int c = int((p.x - g.getX()) / cw);
    return juce::jlimit(0, pat.length - 1, c);
}
juce::Rectangle<int> GridView::cell(int col, int row) const
{
    auto g = gridBounds();
    auto pat = proc.pattern();
    const int n = std::max(1, pat.length);
    const float cw = g.getWidth() / float(n), rh = g.getHeight() / float(fallout::gridRows);
    return juce::Rectangle<float>(g.getX() + cw * col, g.getY() + rh * row, cw, rh).toNearestInt();
}
void GridView::paint(juce::Graphics &g)
{
    auto pat = proc.pattern();
    const int oct = proc.gridOctave;
    const int base = fallout::baseNote + 12 * oct;
    auto gb = gridBounds();
    g.setColour(panel);
    g.fillRect(gb);
    const int play = proc.playStep();
    const int n = std::max(1, pat.length);
    const float cw = gb.getWidth() / float(n), rh = gb.getHeight() / float(fallout::gridRows);
    for (int r = 0; r < fallout::gridRows; ++r)
    {
        const int note = base + (fallout::gridRows - 1 - r);
        const bool isC = (note % 12) == 0;
        const int pc = ((note % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour(black ? juce::Colour(0xff151515) : juce::Colour(0xff1c1c1c));
        g.fillRect(juce::Rectangle<float>(gb.getX(), gb.getY() + rh * r, gb.getWidth(), rh));
        if (isC)
        {
            g.setColour(dim);
            g.setFont(11.f);
            g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 3),
                       2, int(gb.getY() + rh * r), 32, int(rh), juce::Justification::centredLeft);
        }
    }
    g.setColour(juce::Colour(0xff2a2a2a));
    for (int c = 0; c <= n; ++c)
        g.drawVerticalLine(int(gb.getX() + cw * c), float(gb.getY()), float(gb.getBottom()));
    for (int r = 0; r <= fallout::gridRows; ++r)
        g.drawHorizontalLine(int(gb.getY() + rh * r), float(gb.getX()), float(gb.getRight()));
    if (play >= 0 && play < n)
    {
        g.setColour(juce::Colour(0x22ffffff));
        g.fillRect(juce::Rectangle<float>(gb.getX() + cw * play, float(gb.getY()), cw, float(gb.getHeight())));
    }
    for (int c = 0; c < n; ++c)
    {
        const auto &st = pat.steps[(size_t)c];
        if (st.note < 0)
            continue;
        const int row = (fallout::gridRows - 1) - (st.note - base);
        if (row < 0 || row >= fallout::gridRows)
            continue;
        auto rc = juce::Rectangle<float>(gb.getX() + cw * c, gb.getY() + rh * row, cw, rh).reduced(2, 2);
        if (proc.editMode == 2)
            rc = rc.translated(st.rubato * cw * .45f, 0);
        if (proc.editMode == 1)
            rc = rc.translated(0, -st.micro / 100.f * rh * .4f);
        g.setColour(st.accent ? accentMark : accent);
        g.fillRoundedRectangle(rc, 2);
        if (st.slide)
        {
            g.setColour(slideCol);
            juce::Path tail;
            const float mid = rc.getCentreY();
            tail.addTriangle(rc.getRight() - 2, rc.getY() + 2, rc.getRight() + cw * .28f, mid,
                             rc.getRight() - 2, rc.getBottom() - 2);
            g.fillPath(tail);
        }
        if (st.accent)
        {
            g.setColour(juce::Colour(0xff201810));
            g.fillRect(rc.removeFromTop(3).reduced(2, 0));
        }
    }
    g.setColour(line);
    g.drawRect(gb, 1);
}
void GridView::mouseDown(const juce::MouseEvent &e)
{
    downCol = colAt(e.position.toInt());
    downRow = rowAt(e.position.toInt());
    toggledAccent = toggledSlide = dragging = false;
    if (downCol < 0 || downRow < 0)
        return;
    auto p = proc.pattern();
    const int base = fallout::baseNote + 12 * proc.gridOctave;
    const int note = base + (fallout::gridRows - 1 - downRow);
    downNote = p.steps[(size_t)downCol].note;
    startMicro = p.steps[(size_t)downCol].micro;
    startRubato = p.steps[(size_t)downCol].rubato;
    if (proc.editMode == 0)
    {
        if (downNote == note)
            p.steps[(size_t)downCol] = {};
        else
        {
            p.steps[(size_t)downCol].note = note;
            p.steps[(size_t)downCol].accent = false;
            p.steps[(size_t)downCol].slide = false;
            p.steps[(size_t)downCol].micro = 0;
            p.steps[(size_t)downCol].rubato = 0;
        }
        proc.setPattern(p);
        repaint();
    }
}
void GridView::mouseDrag(const juce::MouseEvent &e)
{
    if (downCol < 0)
        return;
    dragging = true;
    auto p = proc.pattern();
    auto &st = p.steps[(size_t)downCol];
    if (st.note < 0 && proc.editMode != 0)
        return;
    const auto d = e.getOffsetFromDragStart();
    const bool fine = e.mods.isShiftDown();
    if (proc.editMode == 1)
    {
        const float scale = fine ? .25f : 1.f;
        st.micro = juce::jlimit(-100.f, 100.f, startMicro - d.y * scale * .5f);
        proc.setPattern(p);
        repaint();
        return;
    }
    if (proc.editMode == 2)
    {
        const float scale = fine ? .15f : 1.f;
        st.rubato = juce::jlimit(-.45f, .45f, startRubato + d.x * scale * .004f);
        proc.setPattern(p);
        repaint();
        return;
    }
    // Edit: drag up/down toggles accent, left/right toggles slide, once per gesture.
    if (st.note < 0)
        return;
    if (!toggledAccent && std::abs(d.y) > 10 && std::abs(d.y) > std::abs(d.x))
    {
        st.accent = !st.accent;
        toggledAccent = true;
        proc.setPattern(p);
        repaint();
    }
    if (!toggledSlide && std::abs(d.x) > 10 && std::abs(d.x) > std::abs(d.y))
    {
        st.slide = !st.slide;
        toggledSlide = true;
        proc.setPattern(p);
        repaint();
    }
}
void GridView::mouseUp(const juce::MouseEvent &)
{
    downCol = downRow = downNote = -1;
    dragging = false;
}
DragChip::DragChip(FalloutProcessor &p, bool isMidi) : proc(p), midi(isMidi)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}
void DragChip::paint(juce::Graphics &g)
{
    g.setColour(juce::Colour(0xff222222));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
    g.setColour(midi ? juce::Colour(0xff9ad0c0) : juce::Colour(0xfff2d08b));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 4, 1);
    g.setColour(juce::Colours::white);
    g.setFont(12.f);
    g.drawText(midi ? "DRAG MIDI" : "DRAG WAV", getLocalBounds(), juce::Justification::centred);
}
void DragChip::mouseDrag(const juce::MouseEvent &e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;
    juce::String err;
    const auto f = midi ? proc.exportMidi(err) : proc.exportWav(err);
    lastError = err;
    if (!f.existsAsFile())
    {
        repaint();
        return;
    }
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this,
                                                               [this] { dragging = false; });
}
FalloutEditor::FalloutEditor(FalloutProcessor &p)
    : AudioProcessorEditor(&p), proc(p), grid(p), midiChip(p, true), wavChip(p, false)
{
    setLookAndFeel(&look);
    setWantsKeyboardFocus(false);
    const int skipKnob[] = {fallout::Osc, fallout::Rate, fallout::Seq, fallout::MidiMode, fallout::DelayDiv};
    auto isChoice = [&](int i)
    {
        for (int s : skipKnob)
            if (s == i)
                return true;
        return false;
    };
    for (int i = 0; i < fallout::Count; ++i)
    {
        const auto &d = fallout::definitions[i];
        labels[i].setText(d.name, juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(labels[i]);
        if (isChoice(i))
        {
            int k = 1;
            if (i == fallout::Osc)
                for (auto *n : fallout::oscNames)
                    boxes[i].addItem(n, k++);
            else if (i == fallout::Rate || i == fallout::DelayDiv)
                for (auto *n : fallout::rateNames)
                    boxes[i].addItem(n, k++);
            else if (i == fallout::Seq)
            {
                boxes[i].addItem("Off", 1);
                boxes[i].addItem("On", 2);
            }
            else if (i == fallout::MidiMode)
                for (auto *n : fallout::midiNames)
                    boxes[i].addItem(n, k++);
            boxes[i].setWantsKeyboardFocus(false);
            addAndMakeVisible(boxes[i]);
            choices.push_back(std::make_unique<CA>(proc.state, d.id, boxes[i]));
        }
        else
        {
            auto &k = knobs[i];
            k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
            k.setWantsKeyboardFocus(false);
            k.setMouseClickGrabsKeyboardFocus(false);
            k.setDoubleClickReturnValue(true, d.initial);
            k.setTextValueSuffix(juce::String(d.unit).isEmpty() ? juce::String() : juce::String(" ") + d.unit);
            k.setTooltip(juce::String(d.name) + ": drag to adjust; double-click resets.");
            addAndMakeVisible(k);
            attachments.push_back(std::make_unique<SA>(proc.state, d.id, k));
            k.textFromValueFunction = [i](double v)
            {
                const auto &def = fallout::definitions[i];
                const int dec = def.high <= 1.1f ? 2 : (def.high - def.low >= 100 ? 0 : 1);
                return juce::String(v, dec);
            };
            k.valueFromTextFunction = [](const juce::String &t) { return double(t.getFloatValue()); };
            k.updateText();
        }
    }
    auto names = FalloutProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i)
        presets.addItem(names[i], i + 1);
    presets.setTextWhenNothingSelected("FALLOUT / choose a preset");
    presets.onChange = [this]
    {
        if (presets.getSelectedId() > 0)
            proc.factory(presets.getSelectedId() - 1);
        repaint();
    };
    addAndMakeVisible(presets);
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    randBtn.onClick = [this]
    {
        proc.mutatePattern(true);
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    mutBtn.onClick = [this]
    {
        proc.mutatePattern(false);
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    clearBtn.onClick = [this]
    {
        proc.clearPattern();
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    leftBtn.onClick = [this]
    {
        proc.shiftPattern(-1);
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    rightBtn.onClick = [this]
    {
        proc.shiftPattern(1);
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    copyBtn.onClick = [this]
    {
        clip = proc.pattern();
        clipValid = true;
        status.setText("Pattern copied", juce::dontSendNotification);
    };
    pasteBtn.onClick = [this]
    {
        if (!clipValid)
            return;
        proc.setPattern(clip);
        proc.set("length", float(clip.length));
        presets.setSelectedId(0, juce::dontSendNotification);
        repaint();
    };
    octDown.onClick = [this]
    {
        proc.gridOctave = juce::jmax(-2, proc.gridOctave - 1);
        syncButtons();
        repaint();
    };
    octUp.onClick = [this]
    {
        proc.gridOctave = juce::jmin(2, proc.gridOctave + 1);
        syncButtons();
        repaint();
    };
    auto setMode = [this](int m)
    {
        proc.editMode = m;
        syncButtons();
        grid.repaint();
    };
    editBtn.onClick = [setMode] { setMode(0); };
    microBtn.onClick = [setMode] { setMode(1); };
    rubatoBtn.onClick = [setMode] { setMode(2); };
    ashBtn.setClickingTogglesState(true);
    ashBtn.setToggleState(proc.ashEnabled, juce::dontSendNotification);
    ashBtn.onClick = [this]
    {
        proc.ashEnabled = ashBtn.getToggleState();
        repaint();
    };
    for (auto *b : {&save, &load, &randBtn, &mutBtn, &clearBtn, &leftBtn, &rightBtn, &copyBtn, &pasteBtn,
                    &octDown, &octUp, &editBtn, &microBtn, &rubatoBtn, &ashBtn})
    {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    }
    addAndMakeVisible(presets);
    addAndMakeVisible(grid);
    addAndMakeVisible(midiChip);
    addAndMakeVisible(wavChip);
    status.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status);
    octLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(octLabel);
    setSize(1100, 820);
    for (auto &a : ashes)
        resetAsh(a, true);
    syncButtons();
    startTimerHz(24);
}
FalloutEditor::~FalloutEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}
void FalloutEditor::resetAsh(Ash &a, bool spawn)
{
    auto &r = juce::Random::getSystemRandom();
    a.x = r.nextFloat() * float(getWidth());
    a.y = spawn ? r.nextFloat() * float(getHeight()) : -4.f;
    a.vx = (r.nextFloat() - .5f) * 14.f;
    a.vy = 18.f + r.nextFloat() * 42.f;
    a.size = 1.f + r.nextFloat() * 2.2f;
    a.alpha = .12f + r.nextFloat() * .35f;
}
void FalloutEditor::syncButtons()
{
    editBtn.setToggleState(proc.editMode == 0, juce::dontSendNotification);
    microBtn.setToggleState(proc.editMode == 1, juce::dontSendNotification);
    rubatoBtn.setToggleState(proc.editMode == 2, juce::dontSendNotification);
    ashBtn.setToggleState(proc.ashEnabled, juce::dontSendNotification);
    const int oct = proc.gridOctave;
    octLabel.setText(juce::String(oct >= 0 ? "+" : "") + juce::String(oct),
                     juce::dontSendNotification);
}
void FalloutEditor::paint(juce::Graphics &g)
{
    g.fillAll(bg);
    if (proc.ashEnabled)
        for (const auto &a : ashes)
        {
            g.setColour(ashCol.withAlpha(a.alpha));
            g.fillRect(a.x, a.y, a.size, a.size);
        }
    g.setColour(line);
    g.drawRect(getLocalBounds(), 1);
    g.drawHorizontalLine(68, 20, 1080);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(look.bold).withHeight(34)));
    g.drawText("FALLOUT", 24, 10, 210, 48, juce::Justification::centredLeft);
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(13)));
    g.drawText("CMPLXTY  /  ACID GRID", 210, 24, 260, 22, juce::Justification::centredLeft);
    g.setColour(dim);
    g.drawText("v1.0.0  /  click a cell to place  /  drag up accent, left-right slide  /  Shift fine in Micro & Rubato",
               250, 794, 520, 18, juce::Justification::centredLeft);
}
void FalloutEditor::resized()
{
    presets.setBounds(560, 20, 300, 28);
    save.setBounds(870, 20, 56, 28);
    load.setBounds(932, 20, 56, 28);
    ashBtn.setBounds(1000, 20, 76, 28);
    // Main voice knobs / combos across the top strip
    const int mainIds[] = {fallout::Osc,    fallout::Cutoff, fallout::Resonance, fallout::EnvMod,
                           fallout::Decay,  fallout::Accent, fallout::Tune,      fallout::Drive,
                           fallout::Glide,  fallout::Output};
    for (int n = 0; n < 10; ++n)
    {
        const int id = mainIds[n];
        const int x = 16 + n * 108;
        labels[id].setBounds(x, 76, 104, 16);
        if (id == fallout::Osc)
            boxes[id].setBounds(x + 4, 96, 96, 26);
        else
            knobs[id].setBounds(x, 92, 104, 104);
    }
    // Extras + delay
    const int extra[] = {fallout::Pwm,      fallout::Detune,  fallout::Fold,     fallout::Human,
                         fallout::DelayDiv, fallout::DelayFb, fallout::DelayMix, fallout::DelayTone,
                         fallout::Length,   fallout::Rate,    fallout::Seq,      fallout::MidiMode};
    for (int n = 0; n < 12; ++n)
    {
        const int id = extra[n];
        const int x = 16 + n * 90;
        labels[id].setBounds(x, 198, 86, 14);
        if (id == fallout::DelayDiv || id == fallout::Rate || id == fallout::Seq || id == fallout::MidiMode)
            boxes[id].setBounds(x + 2, 214, 82, 24);
        else
            knobs[id].setBounds(x, 210, 86, 86);
    }
    // Toolbar over the grid
    int tx = 16, ty = 304;
    auto place = [&](juce::Component &c, int w)
    {
        c.setBounds(tx, ty, w, 24);
        tx += w + 6;
    };
    place(editBtn, 64);
    place(microBtn, 72);
    place(rubatoBtn, 80);
    place(octDown, 58);
    octLabel.setBounds(tx, ty, 64, 24);
    tx += 70;
    place(octUp, 58);
    place(randBtn, 96);
    place(mutBtn, 78);
    place(clearBtn, 70);
    place(leftBtn, 40);
    place(rightBtn, 40);
    place(copyBtn, 64);
    place(pasteBtn, 64);
    midiChip.setBounds(16, 792, 110, 22);
    wavChip.setBounds(132, 792, 100, 22);
    grid.setBounds(8, 334, 1084, 430);
    status.setBounds(760, 792, 320, 22);
}
void FalloutEditor::timerCallback()
{
    if (proc.ashEnabled)
    {
        const float dt = 1.f / 24.f;
        for (auto &a : ashes)
        {
            a.x += a.vx * dt;
            a.y += a.vy * dt;
            if (a.y > getHeight() + 4 || a.x < -6 || a.x > getWidth() + 6)
                resetAsh(a, false);
        }
        repaint();
    }
    else
        grid.repaint();
    const float db = juce::Decibels::gainToDecibels(proc.meter.load(), -100.f);
    juce::String err = midiChip.lastError.isNotEmpty() ? midiChip.lastError : wavChip.lastError;
    status.setText(err.isNotEmpty()
                       ? err
                       : "OUT " + juce::String(db, 1) + " dBFS   /   LIMIT " +
                             juce::String(proc.reduction.load(), 1) + " dB   /   STEP " +
                             juce::String(proc.playStep() + 1),
                   juce::dontSendNotification);
}
void FalloutEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("CMPLXTY/FALLOUT/Presets");
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(write ? "Save FALLOUT preset" : "Load FALLOUT preset",
                                                  folder, "*.fallout");
    juce::Component::SafePointer<FalloutEditor> safe(this);
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
                if (!f.withFileExtension("fallout").replaceWithData(b.getData(), b.getSize()))
                    safe->status.setText("Could not save preset", juce::dontSendNotification);
            }
            else
            {
                juce::MemoryBlock b;
                if (f.loadFileAsData(b))
                    safe->proc.setStateInformation(b.getData(), int(b.getSize()));
                safe->syncButtons();
                safe->repaint();
            }
            safe->presets.setSelectedId(0, juce::dontSendNotification);
        });
}
