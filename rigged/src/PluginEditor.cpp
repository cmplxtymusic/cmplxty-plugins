#include "PluginEditor.h"
#include "BinaryData.h"
#include "Presets.h"
#include <algorithm>

RiggedLook::RiggedLook()
{
    face = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf,
                                                   BinaryData::BarlowRegular_ttfSize);
    faceB = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf,
                                                    BinaryData::BarlowSemiBold_ttfSize);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff161616));
    setColour(juce::Slider::trackColourId, juce::Colour(0xffbbbbbb));
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff555555));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffcccccc));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a3a));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff191919));
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff333333));
    setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff666666));
    setColour(juce::Label::textColourId, juce::Colour(0xffdddddd));
    setColour(juce::CaretComponent::caretColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161616));
    setColour(juce::TextEditor::textColourId, juce::Colours::white);
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff555555));
    setColour(juce::FileChooserDialogBox::titleTextColourId, juce::Colours::white);
}

juce::Font RiggedLook::getLabelFont(juce::Label &)
{
    return juce::Font(juce::FontOptions(face).withHeight(12));
}
juce::Font RiggedLook::getComboBoxFont(juce::ComboBox &)
{
    return juce::Font(juce::FontOptions(face).withHeight(13));
}
juce::Font RiggedLook::getTextButtonFont(juce::TextButton &, int)
{
    return juce::Font(juce::FontOptions(faceB).withHeight(13));
}

void RiggedLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float start,
                                  float end, juce::Slider &)
{
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6);
    const float radius = std::min(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    g.setColour(juce::Colour(0xff242424));
    g.fillEllipse(c.x - radius, c.y - radius, 2 * radius, 2 * radius);
    juce::Path track;
    track.addCentredArc(c.x, c.y, radius - 2, radius - 2, 0, start, end, true);
    g.setColour(juce::Colour(0xff4e4e4e));
    g.strokePath(track, juce::PathStrokeType(2));
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, radius - 2, radius - 2, 0, start, start + pos * (end - start), true);
    g.setColour(juce::Colours::white);
    g.strokePath(arc, juce::PathStrokeType(2.6f));
    const float angle = start + pos * (end - start);
    g.drawLine(c.x + std::sin(angle) * radius * 0.50f, c.y - std::cos(angle) * radius * 0.50f,
               c.x + std::sin(angle) * radius * 0.84f, c.y - std::cos(angle) * radius * 0.84f, 2.4f);
}

void RiggedLook::drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1);
    const bool on = b.getToggleState();
    g.setColour(on ? juce::Colour(0xff2e2e2e) : juce::Colour(0xff161616));
    if (over || down)
        g.setColour(on ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff1e1e1e));
    g.fillRoundedRectangle(r, 3);
    g.setColour(on ? juce::Colours::white : juce::Colour(0xff777777));
    g.drawRoundedRectangle(r, 3, on ? 1.4f : 1.f);
    g.setFont(juce::Font(juce::FontOptions(faceB).withHeight(11.5f)));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void RiggedLook::drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over,
                                      bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(down ? juce::Colour(0xff3a3a3a) : (over ? juce::Colour(0xff2c2c2c) : juce::Colour(0xff202020)));
    g.fillRoundedRectangle(r, 3);
    g.setColour(juce::Colour(0xff666666));
    g.drawRoundedRectangle(r, 3, 1);
}

// ----- Dice (decorative only) ------------------------------------------------

DiceView::DiceView() { startTimerHz(30); }

void DiceView::kick()
{
    spin = 0.55f;
    pip = 1 + (pip * 7 + 3) % 6;
}

void DiceView::setReduce(bool r)
{
    reduce = r;
    if (reduce)
        speed = 0;
}

void DiceView::timerCallback()
{
    if (reduce)
        return;
    speed = 0.018f + spin;
    spin *= 0.94f;
    if (spin < 0.002f)
        spin = 0;
    angle += speed;
    if (angle > juce::MathConstants<float>::twoPi)
        angle -= juce::MathConstants<float>::twoPi;
    if (spin > 0.05f && (((int)(angle * 8) & 3) == 0))
        pip = 1 + (int)(std::fmod(angle * 13.7f, 6.f));
    repaint();
}

void DiceView::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat().reduced(3);
    const auto c = b.getCentre();
    const float s = std::min(b.getWidth(), b.getHeight()) * 0.38f;
    const float ca = std::cos(angle), sa = std::sin(angle);
    // A simple 2.5D cube, monochrome wire + pips. Purely decorative.
    auto rot = [&](float x, float y, float z) {
        const float x1 = x * ca - z * sa;
        const float z1 = x * sa + z * ca;
        const float y1 = y * 0.92f - z1 * 0.22f;
        return juce::Point<float>(c.x + x1 * s, c.y + y1 * s);
    };
    const juce::Point<float> p[8] = { rot(-1, -1, -1), rot(1, -1, -1), rot(1, 1, -1), rot(-1, 1, -1),
                                      rot(-1, -1, 1),  rot(1, -1, 1),  rot(1, 1, 1),  rot(-1, 1, 1) };
    const int edges[][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
                             { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    juce::Path faceP;
    faceP.startNewSubPath(p[4]);
    faceP.lineTo(p[5]);
    faceP.lineTo(p[6]);
    faceP.lineTo(p[7]);
    faceP.closeSubPath();
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillPath(faceP);
    g.setColour(juce::Colour(0xffe0e0e0));
    for (auto &e : edges)
        g.drawLine({ p[e[0]], p[e[1]] }, 1.4f);
    // Pips on the front face.
    const auto mid = (p[4] + p[5] + p[6] + p[7]) * 0.25f;
    const auto u = (p[5] - p[4]) * 0.28f;
    const auto v = (p[7] - p[4]) * 0.28f;
    auto pipAt = [&](float xu, float yv) {
        const auto q = mid + u * xu + v * yv;
        g.fillEllipse(q.x - 2.2f, q.y - 2.2f, 4.4f, 4.4f);
    };
    g.setColour(juce::Colours::white);
    const int n = juce::jlimit(1, 6, pip);
    if (n % 2)
        pipAt(0, 0);
    if (n >= 2)
    {
        pipAt(-1, -1);
        pipAt(1, 1);
    }
    if (n >= 4)
    {
        pipAt(1, -1);
        pipAt(-1, 1);
    }
    if (n == 6)
    {
        pipAt(-1, 0);
        pipAt(1, 0);
    }
}

// ----- Module ----------------------------------------------------------------

ModulePanel::ModulePanel(RiggedProcessor &p, int fx, RiggedLook &lf) : fxIndex(fx), proc(p), look(lf)
{
    setLookAndFeel(&look);
    const int first = rigged::fxFirst[fx];
    const int n = rigged::fxCountParams[fx];
    on.setWantsKeyboardFocus(false);
    lock.setWantsKeyboardFocus(false);
    on.setClickingTogglesState(true);
    lock.setClickingTogglesState(true);
    on.setTriggeredOnMouseDown(false);
    addAndMakeVisible(on);
    addAndMakeVisible(lock);
    bAtt.push_back(std::make_unique<BA>(proc.state, rigged::definitions[(size_t)first].id, on));
    bAtt.push_back(std::make_unique<BA>(proc.state, rigged::definitions[(size_t)(first + 1)].id, lock));
    on.setTooltip("Turn this effect on or off. Automatable.");
    lock.setTooltip("LOCK: left out of RANDOMIZE. Automatable.");

    for (int k = 2; k < n; ++k)
    {
        const auto &d = rigged::definitions[(size_t)(first + k)];
        if (d.kind == rigged::Kind::Choice)
        {
            auto box = std::make_unique<juce::ComboBox>();
            for (int c = 0; c < d.choices; ++c)
                box->addItem(d.labels[c], c + 1);
            box->setWantsKeyboardFocus(false);
            box->setTooltip(juce::String(d.name) + ": automatable.");
            addAndMakeVisible(*box);
            cAtt.push_back(std::make_unique<CA>(proc.state, d.id, *box));
            auto lab = std::make_unique<juce::Label>();
            lab->setText(d.name, juce::dontSendNotification);
            lab->setJustificationType(juce::Justification::centred);
            lab->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(*lab);
            boxes.push_back(std::move(box));
            labels.push_back(std::move(lab));
        }
        else
        {
            auto sl = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::TextBoxBelow);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 14);
            sl->setWantsKeyboardFocus(false);
            sl->setMouseClickGrabsKeyboardFocus(false);
            sl->setDoubleClickReturnValue(true, d.initial);
            sl->setTooltip(juce::String(d.name) +
                           ": drag to adjust; double-click resets. Automatable.");
            sl->setTextValueSuffix(juce::String(d.unit).isEmpty() ? juce::String()
                                                                 : juce::String(" ") + d.unit);
            addAndMakeVisible(*sl);
            sAtt.push_back(std::make_unique<SA>(proc.state, d.id, *sl));
            auto lab = std::make_unique<juce::Label>();
            lab->setText(d.name, juce::dontSendNotification);
            lab->setJustificationType(juce::Justification::centred);
            lab->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(*lab);
            knobs.push_back(std::move(sl));
            labels.push_back(std::move(lab));
        }
    }
}

void ModulePanel::paint(juce::Graphics &g)
{
    auto r = getLocalBounds().toFloat().reduced(2);
    g.setColour(juce::Colour(0xff161616));
    g.fillRoundedRectangle(r, 5);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRoundedRectangle(r, 5, 1);
    auto head = r.removeFromTop(26);
    g.setColour(juce::Colour(0xff202020));
    g.fillRoundedRectangle(head, 4);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(13)));
    g.drawText(rigged::fxNames[fxIndex], head.reduced(8, 0).withTrimmedRight(86),
               juce::Justification::centredLeft);
}

void ModulePanel::updateDim()
{
    const bool alive = on.getToggleState();
    const float a = alive ? 1.0f : 0.38f;
    for (auto &k : knobs) k->setAlpha(a);
    for (auto &l : labels) l->setAlpha(a);
    for (auto &b : boxes) b->setAlpha(a);
}

void ModulePanel::resized()
{
    auto r = getLocalBounds().reduced(6);
    auto head = r.removeFromTop(22);
    head.removeFromLeft((int)head.getWidth() - 86);
    on.setBounds(head.removeFromLeft(40).reduced(1, 1));
    head.removeFromLeft(2);
    lock.setBounds(head.reduced(1, 1));
    r.removeFromTop(6);
    // Combos first in a thin row, knobs in a grid.
    const int nBox = (int)boxes.size();
    const int nKnob = (int)knobs.size();
    if (nBox)
    {
        auto row = r.removeFromTop(36);
        const int w = row.getWidth() / nBox;
        int bi = 0, li = 0;
        // labels[0..] follow construction: for each param, either box+label or knob+label.
        // Easier: we stored labels in the same order as the non-on/lock params, mixed.
        // Place combos on this row and knobs below; match labels by walking.
        (void)w;
        (void)bi;
        (void)li;
        (void)row;
    }
    // Walk children in construction groups: labels are 1:1 with knobs then we need a better map.
    // We stored: for each param, either (box,label) or (knob,label) in the order of the loops,
    // but knobs/boxes are separate vectors and labels is shared in sequence.
    std::vector<juce::Component *> controls;
    // Reconstruct param-aligned list: we pushed to boxes OR knobs, and always a label.
    // We can't recover which is which from labels alone. Use component Y later.
    // Simpler layout: one row of combos (full width each), then knobs in 2-3 columns.
    int labi = 0;
    // This is messy because labels is mixed. Let's just place by type.
    // We'll re-do: place all boxes at top, their labels are the ones added when box was added.
    // I stored labels in param order. I'll store a parallel kind... For now, match by counting.
    // Look at the effect's params from 2 onward and step through.
    const int first = rigged::fxFirst[fxIndex];
    const int n = rigged::fxCountParams[fxIndex];
    int boxi = 0, knobi = 0, labeli = 0;
    std::vector<int> boxLabel, knobLabel;
    for (int k = 2; k < n; ++k)
    {
        const auto &d = rigged::definitions[(size_t)(first + k)];
        if (d.kind == rigged::Kind::Choice)
            boxLabel.push_back(labeli++);
        else
            knobLabel.push_back(labeli++);
        (void)boxi;
        (void)knobi;
    }
    if (!boxes.empty())
    {
        auto row = r.removeFromTop(40);
        const int w = row.getWidth() / (int)boxes.size();
        for (int i = 0; i < (int)boxes.size(); ++i)
        {
            auto cell = row.removeFromLeft(w);
            if (boxLabel[(size_t)i] < (int)labels.size())
            {
                labels[(size_t)boxLabel[(size_t)i]]->setBounds(cell.removeFromTop(12));
                boxes[(size_t)i]->setBounds(cell.reduced(2, 1));
            }
        }
        r.removeFromTop(2);
    }
    if (!knobs.empty())
    {
        const int cols = knobs.size() >= 3 ? 2 : (int)knobs.size();
        const int rows = ((int)knobs.size() + cols - 1) / cols;
        const int cw = r.getWidth() / cols;
        const int rh = r.getHeight() / std::max(1, rows);
        for (int i = 0; i < (int)knobs.size(); ++i)
        {
            const int col = i % cols, row = i / cols;
            auto cell = juce::Rectangle<int>(r.getX() + col * cw, r.getY() + row * rh, cw, rh).reduced(2);
            if (knobLabel[(size_t)i] < (int)labels.size())
                labels[(size_t)knobLabel[(size_t)i]]->setBounds(cell.removeFromTop(14));
            knobs[(size_t)i]->setBounds(cell);
        }
    }
}

void ModulePanel::mouseDown(const juce::MouseEvent &e)
{
    if (headerBounds().contains(e.getPosition()) && onDragStart)
        onDragStart(this, e.getEventRelativeTo(getParentComponent()).position.roundToInt());
}

void ModulePanel::mouseDrag(const juce::MouseEvent &e)
{
    if (onDrag)
        onDrag(this, e.getEventRelativeTo(getParentComponent()).position.roundToInt());
}

void ModulePanel::mouseUp(const juce::MouseEvent &e)
{
    if (onDragEnd)
        onDragEnd(this, e.getEventRelativeTo(getParentComponent()).position.roundToInt());
}

// ----- Editor ----------------------------------------------------------------

RiggedEditor::RiggedEditor(RiggedProcessor &p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&look);
    setWantsKeyboardFocus(false);
    dice.setFace(look.face);
    addAndMakeVisible(dice);

    auto setupKnob = [&](juce::Slider &s, juce::Label &l, const char *id, const char *name) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 14);
        s.setWantsKeyboardFocus(false);
        s.setMouseClickGrabsKeyboardFocus(false);
        const auto &d = *std::find_if(rigged::definitions.begin(), rigged::definitions.end(),
                                      [&](const rigged::Definition &x) { return juce::String(x.id) == id; });
        s.setDoubleClickReturnValue(true, d.initial);
        s.setTooltip(juce::String(name) + ": drag to adjust; double-click resets. Automatable.");
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
        sAtt.push_back(std::make_unique<SA>(proc.state, id, s));
    };
    setupKnob(amount, amountL, "amount", "AMOUNT");
    setupKnob(mix, mixL, "mix", "MIX");
    setupKnob(output, outputL, "output", "OUTPUT");

    auto setupToggle = [&](juce::ToggleButton &b, const char *id, const char *tip) {
        b.setWantsKeyboardFocus(false);
        b.setClickingTogglesState(true);
        b.setTooltip(tip);
        addAndMakeVisible(b);
        bAtt.push_back(std::make_unique<BA>(proc.state, id, b));
    };
    setupToggle(match, "gainmatch", "Automatic gain match: rolls stay at a similar loudness. Automatable.");
    setupToggle(limit, "limiter", "Safety limiter at -1 dBFS. On by default. Automatable.");
    setupToggle(bypass, "bypass", "Bypass the whole rack, with a short fade. Automatable.");
    setupToggle(reduce, "reducemotion", "Accessibility: turn off dice animation and pixel effects.");

    roll.setWantsKeyboardFocus(false);
    roll.setTooltip("Roll unlocked effect parameters. Amount sets how far they move. Automatable via RANDOMIZE.");
    roll.onClick = [this] { proc.randomize(); };
    addAndMakeVisible(roll);
    undo.setWantsKeyboardFocus(false);
    redo.setWantsKeyboardFocus(false);
    undo.onClick = [this] { proc.undo(); };
    redo.onClick = [this] { proc.redo(); };
    undo.setTooltip("Undo the last roll.");
    redo.setTooltip("Redo a roll you undid.");
    addAndMakeVisible(undo);
    addAndMakeVisible(redo);
    save.setWantsKeyboardFocus(false);
    load.setWantsKeyboardFocus(false);
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    addAndMakeVisible(save);
    addAndMakeVisible(load);

    presets.setWantsKeyboardFocus(false);
    presets.addItemList(RiggedProcessor::presetNames(), 1);
    presets.setSelectedItemIndex(0, juce::dontSendNotification);
    presets.onChange = [this] {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0)
            proc.factory(i);
    };
    presets.setTooltip("Factory presets. Your own presets are .rigged files via SAVE / LOAD.");
    addAndMakeVisible(presets);

    status.setJustificationType(juce::Justification::centredLeft);
    status.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(status);

    for (int i = 0; i < rigged::kFxCount; ++i)
    {
        modules[(size_t)i] = std::make_unique<ModulePanel>(proc, i, look);
        auto *m = modules[(size_t)i].get();
        m->onDragStart = [this](ModulePanel *p, juce::Point<int>) {
            dragFx = p->fxIndex;
            hoverSlot = -1;
        };
        m->onDrag = [this](ModulePanel *, juce::Point<int> pos) {
            hoverSlot = -1;
            int best = 1 << 30;
            auto s = proc.getSettings();
            for (int slot = 0; slot < rigged::kFxCount; ++slot)
            {
                auto *other = modules[(size_t)s.order[(size_t)slot]].get();
                const int d = std::abs(other->getBounds().getCentreX() - pos.x) +
                              std::abs(other->getBounds().getCentreY() - pos.y);
                if (d < best)
                {
                    best = d;
                    hoverSlot = slot;
                }
            }
            repaint();
        };
        m->onDragEnd = [this](ModulePanel *p, juce::Point<int>) {
            if (dragFx >= 0 && hoverSlot >= 0)
            {
                auto s = proc.getSettings();
                int from = -1;
                for (int slot = 0; slot < rigged::kFxCount; ++slot)
                    if (s.order[(size_t)slot] == p->fxIndex)
                        from = slot;
                if (from >= 0 && from != hoverSlot)
                {
                    const int moved = s.order[(size_t)from];
                    if (from < hoverSlot)
                    {
                        for (int k = from; k < hoverSlot; ++k)
                            s.order[(size_t)k] = s.order[(size_t)(k + 1)];
                    }
                    else
                    {
                        for (int k = from; k > hoverSlot; --k)
                            s.order[(size_t)k] = s.order[(size_t)(k - 1)];
                    }
                    s.order[(size_t)hoverSlot] = moved;
                    proc.setOrder(s.order);
                    layoutModules();
                }
            }
            dragFx = -1;
            hoverSlot = -1;
            repaint();
        };
        addAndMakeVisible(*m);
    }

    setOpaque(true);
    setSize(1240, 720);
    setResizable(true, true);
    setResizeLimits(1080, 640, 1800, 1100);
    startTimerHz(20);
    layoutModules();
}

RiggedEditor::~RiggedEditor()
{
    setLookAndFeel(nullptr);
}

void RiggedEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colour(0xff0e0e0e));
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff4a4a4a));
    g.drawRect(bounds, 1);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(look.faceB).withHeight(36)));
    g.drawText("RIGGED", 22, 10, 180, 44, juce::Justification::centredLeft);
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(13)));
    g.setColour(juce::Colour(0xff999999));
    g.drawText("CMPLXTY  /  EFFECTS RACK", 200, 24, 260, 22, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawHorizontalLine(86, 16, (float)getWidth() - 16);
    g.drawHorizontalLine(getHeight() - 36, 16, (float)getWidth() - 16);

    g.setColour(juce::Colour(0xff666666));
    g.setFont(juce::Font(juce::FontOptions(look.face).withHeight(12)));
    g.drawText("v1.0.0   /   drag a module header to reorder   /   double-click knobs to reset", 22,
               getHeight() - 30, 720, 22, juce::Justification::centredLeft);

    const bool reduceOn = proc.getSettings()[rigged::ReduceMotion] >= 0.5f;
    if (!reduceOn)
    {
        g.setColour(juce::Colour(0x14ffffff));
        for (int y = 0; y < getHeight(); y += 3)
            g.drawHorizontalLine(y, 0, (float)getWidth());
    }

    if (dragFx >= 0 && hoverSlot >= 0)
    {
        auto s = proc.getSettings();
        auto *m = modules[(size_t)s.order[(size_t)hoverSlot]].get();
        g.setColour(juce::Colours::white);
        g.drawRect(m->getBounds().expanded(2), 2);
    }
}

void RiggedEditor::layoutModules()
{
    const auto s = proc.getSettings();
    auto body = getLocalBounds().reduced(12, 0);
    body.removeFromTop(92);
    body.removeFromBottom(38);
    // Two rows: first 6, then 5.
    const int topN = 6, botN = 5;
    auto top = body.removeFromTop(body.getHeight() / 2).reduced(0, 3);
    auto bot = body.reduced(0, 3);
    const int tw = top.getWidth() / topN;
    const int bw = bot.getWidth() / botN;
    for (int slot = 0; slot < rigged::kFxCount; ++slot)
    {
        const int fx = s.order[(size_t)slot];
        auto *m = modules[(size_t)fx].get();
        if (slot < topN)
            m->setBounds(top.getX() + slot * tw + 2, top.getY(), tw - 4, top.getHeight());
        else
        {
            const int k = slot - topN;
            m->setBounds(bot.getX() + k * bw + 2, bot.getY(), bw - 4, bot.getHeight());
        }
        m->toFront(false);
    }
    if (dragFx >= 0)
        modules[(size_t)dragFx]->toFront(false);
}

void RiggedEditor::resized()
{
    dice.setBounds(455, 10, 64, 64);
    amountL.setBounds(528, 6, 70, 14);
    amount.setBounds(528, 18, 70, 66);
    roll.setBounds(608, 22, 118, 30);
    undo.setBounds(732, 22, 62, 30);
    redo.setBounds(798, 22, 62, 30);
    presets.setBounds(870, 22, 180, 30);
    save.setBounds(1056, 22, 58, 30);
    load.setBounds(1118, 22, 58, 30);

    mixL.setBounds(608, 54, 50, 14);
    mix.setBounds(600, 52, 66, 34); // will look tight; put mix/out as small rotaries in the second strip
    // Redo layout of global row more cleanly:
    // Row 1 (y=16-54): dice, amount, randomize, undo, redo, presets, save, load
    // We'll leave mix/output/toggles on the right of the title row at y=54-84? They overlap.
    // Place MIX/OUT as compact knobs to the right of REDUCE, on y=54.
    mixL.setBounds(200, 56, 50, 12);
    mix.setBounds(190, 52, 70, 36);
    // That's overlapping the subtitle. Put globals on a slim strip at y=50-86,
    // and move subtitle only on the left.
    // Actually title is y=10-54. Globals sit y=8-82 on the right half. Mix as rotary is tall.
    // Use smaller placement: MATCH/LIMIT/BYPASS/REDUCE as buttons at y=56, MIX and OUT at 528+ after amount.

    // Final header plan (painted title uses 22,10):
    // [RIGGED    CMPLXTY] [dice] [AMOUNT] [RANDOMIZE][UNDO][REDO] [preset][SAVE][LOAD]
    //                     already set. Second line of buttons under randomize:
    mixL.setBounds(608, 54, 54, 12);
    // skip overlapping rotaries - put MIX and OUTPUT as sliders with text boxes at bottom of header.
    // Use the area x=870 y=56 for status; mix/output at 528+70 already used by amount.
    // Place MIX and OUTPUT just left of dice, small.
    mixL.setBounds(328, 8, 58, 12);
    mix.setBounds(320, 16, 66, 68);
    outputL.setBounds(392, 8, 58, 12);
    output.setBounds(384, 16, 66, 68);
    match.setBounds(608, 56, 68, 24);
    limit.setBounds(680, 56, 68, 24);
    bypass.setBounds(752, 56, 76, 24);
    reduce.setBounds(832, 56, 130, 24);
    status.setBounds(970, 56, 250, 24);
    // Amount sits after dice at 528. Dice at 455. Mix at 320, Out at 384 — may crowd subtitle.
    // Subtitle is 200-460. Let me move subtitle in paint... it's already drawn at 200.
    // I'll leave paint subtitle at 200,24 and accept it sits above mix? Mix is at 320,16 — overlap.
    // Change: subtitle stays, mix/output go to the right of amount in a compact form.
    // amount is 528-598. Then roll at 608. Conflict.
    // Let me just call a clean layout:

    const int y0 = 12;
    dice.setBounds(470, 8, 68, 70);
    amountL.setBounds(544, 8, 72, 12);
    amount.setBounds(542, 16, 76, 70);
    roll.setBounds(626, 16, 124, 32);
    undo.setBounds(756, 16, 64, 32);
    redo.setBounds(824, 16, 64, 32);
    presets.setBounds(896, 16, 188, 32);
    save.setBounds(1088, 16, 58, 32);
    load.setBounds(1150, 16, 58, 32);
    mixL.setBounds(626, 50, 50, 12);
    mix.setBounds(618, 48, 64, 38);
    outputL.setBounds(684, 50, 58, 12);
    output.setBounds(678, 48, 64, 38);
    match.setBounds(748, 54, 70, 26);
    limit.setBounds(822, 54, 68, 26);
    bypass.setBounds(894, 54, 76, 26);
    reduce.setBounds(974, 54, 128, 26);
    status.setBounds(1106, 54, 122, 26);

    // MIX/OUT rotaries at 38px tall with text box will be cramped. That's ok for a header.
    layoutModules();
}

void RiggedEditor::timerCallback()
{
    const int k = proc.diceKick.load();
    if (k != lastKick)
    {
        lastKick = k;
        dice.kick();
    }
    dice.setReduce(proc.getSettings()[rigged::ReduceMotion] >= 0.5f);
    const float db = juce::Decibels::gainToDecibels(proc.meter.load(), -100.f);
    status.setText("OUT " + juce::String(db, 1) + "   LIM " + juce::String(proc.reduction.load(), 1),
                   juce::dontSendNotification);
    undo.setEnabled(proc.canUndo() > 0);
    redo.setEnabled(proc.canRedo() > 0);
    for (auto &m : modules)
        if (m) m->updateDim();
}

void RiggedEditor::file(bool write)
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("CMPLXTY/RIGGED/Presets");
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(write ? "Save RIGGED preset" : "Load RIGGED preset", folder,
                                                  "*.rigged");
    juce::Component::SafePointer<RiggedEditor> safe(this);
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
                if (!f.hasFileExtension(".rigged"))
                    f = f.withFileExtension(".rigged");
                juce::MemoryBlock mb;
                safe->proc.getStateInformation(mb);
                f.replaceWithData(mb.getData(), mb.getSize());
            }
            else
            {
                juce::MemoryBlock mb;
                if (f.loadFileAsData(mb))
                    safe->proc.setStateInformation(mb.getData(), (int)mb.getSize());
                safe->layoutModules();
            }
        });
}
