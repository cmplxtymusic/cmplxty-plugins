#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
const juce::Colour bg(0xff101010), panel(0xff191919), line(0xff4a4a4a), dim(0xff777777),
    accent(0xffe8e8e8), playCol(0xffc8c8c8);
const juce::Colour sleeveFill[8] = { juce::Colour(0xaa1a1a1a), juce::Colour(0xaa2a2a2a), juce::Colour(0xaa333333),
                                     juce::Colour(0xaa3d3d3d), juce::Colour(0xaa2e2e2e), juce::Colour(0xaa262626),
                                     juce::Colour(0xaa303030), juce::Colour(0xaa383838) };
juce::Font fontOf(juce::Typeface::Ptr t, float s)
{
    return juce::Font(juce::FontOptions(t).withHeight(s));
}
void styleKnob(juce::Slider &s, juce::Label &l, const juce::String &name, juce::Slider::SliderStyle st)
{
    s.setSliderStyle(st);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 12);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setInterceptsMouseClicks(false, false);
}
} // namespace

SmotherLook::SmotherLook()
{
    face = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
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
    setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff666666));
}
juce::Font SmotherLook::getLabelFont(juce::Label &) { return fontOf(face, 11); }
juce::Font SmotherLook::getComboBoxFont(juce::ComboBox &) { return fontOf(face, 12); }
juce::Font SmotherLook::getTextButtonFont(juce::TextButton &, int) { return fontOf(bold, 11); }
juce::Font SmotherLook::getPopupMenuFont() { return fontOf(face, 13); }
void SmotherLook::drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1,
                                   juce::Slider &)
{
    const auto c = juce::Point<float>(x + w * 0.5f, y + h * 0.5f);
    const float r = juce::jmin(w, h) * 0.42f;
    g.setColour(juce::Colour(0xff262626));
    g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
    juce::Path p;
    p.addCentredArc(c.x, c.y, r - 2, r - 2, 0, a0, a0 + (a1 - a0) * pos, true);
    g.setColour(juce::Colour(0xff555555));
    g.strokePath(p, juce::PathStrokeType(2.2f));
    const float ang = a0 + (a1 - a0) * pos;
    g.setColour(juce::Colours::white);
    g.drawLine(c.x, c.y, c.x + std::cos(ang) * (r - 4), c.y + std::sin(ang) * (r - 4), 1.6f);
}

GridView::GridView(SmotherProcessor &p) : proc(p) { setOpaque(false); }

juce::Rectangle<int> GridView::rowBounds(int visualRow) const
{
    const int n = proc.pattern().numLanes + 1; // + ALL
    const int h = getHeight();
    const int rowH = std::max(18, h / std::max(1, n));
    return { 0, visualRow * rowH, getWidth(), rowH };
}

juce::Rectangle<int> GridView::stepCell(int visualRow, int step, int length) const
{
    auto r = rowBounds(visualRow).withTrimmedLeft(headerW).reduced(1, 3);
    const int n = std::max(1, length);
    const int w = r.getWidth() / n;
    return { r.getX() + step * w, r.getY(), std::max(2, w - 1), r.getHeight() };
}

void GridView::paint(juce::Graphics &g)
{
    auto p = proc.pattern();
    const int nL = p.numLanes;
    auto look = dynamic_cast<SmotherLook *>(&getLookAndFeel());
    auto f = look ? fontOf(look->face, 11) : juce::Font(juce::FontOptions(11));
    auto fb = look ? fontOf(look->bold, 10) : juce::Font(juce::FontOptions(10));

    auto paintRow = [&](int visual, const juce::String &name, int length, int play, bool isAll,
                        const smother::Lane *ln) {
        auto rb = rowBounds(visual);
        g.setColour(visual % 2 ? juce::Colour(0xff161616) : juce::Colour(0xff141414));
        g.fillRect(rb);
        g.setColour(line);
        g.drawHorizontalLine(rb.getBottom() - 1, (float)rb.getX(), (float)rb.getRight());
        auto head = rb.withWidth(headerW).reduced(4, 2);
        g.setColour(isAll ? accent : juce::Colours::white);
        g.setFont(fb);
        g.drawText(name, head, juce::Justification::centredLeft);
        if (ln != nullptr)
        {
            g.setFont(f);
            g.setColour(dim);
            juce::String meta = juce::String(ln->length) + "  " +
                                smother::rateNames[smother::clampi((int)ln->rate, 0, 5)];
            if (ln->doubleLen)
                meta += "  x2";
            g.drawText(meta, head.withTrimmedLeft(48), juce::Justification::centredRight);
        }
        const int cols = std::max(1, length);
        for (int s = 0; s < cols; ++s)
        {
            auto c = stepCell(visual, s, cols);
            const bool beat = (s % 4) == 0;
            g.setColour(beat ? juce::Colour(0xff222222) : juce::Colour(0xff1c1c1c));
            g.fillRect(c);
            if (!isAll && ln != nullptr)
            {
                const float v = (s < smother::maxSteps) ? ln->vel[(size_t)s] : 0;
                if (v > 0)
                {
                    auto fill = c.reduced(1).withHeight(std::max(3, int(c.getHeight() * v)));
                    fill = fill.withY(c.getBottom() - fill.getHeight() - 1);
                    g.setColour(accent.withAlpha(0.55f + 0.4f * v));
                    g.fillRect(fill);
                }
            }
            if (play == s)
            {
                g.setColour(playCol.withAlpha(0.28f));
                g.fillRect(c);
            }
            g.setColour(juce::Colour(0xff2a2a2a));
            g.drawRect(c, 1);
        }
    };

    paintRow(0, "ALL", 16, proc.allPlayStep(), true, nullptr);
    for (int i = 0; i < nL; ++i)
    {
        const auto &ln = p.lanes[(size_t)i];
        paintRow(i + 1, smother::drumLabel(ln.drum), ln.length, proc.playStep(i), false, &ln);
    }

    auto paintSleeves = [&](int visual, const smother::Sleeve *list, int n, int length, bool all, int sel) {
        for (int i = 0; i < n; ++i)
        {
            const auto &sv = list[i];
            const int a = smother::clampi(sv.start, 0, length - 1);
            const int b = smother::clampi(sv.start + sv.length, a + 1, length);
            auto left = stepCell(visual, a, length);
            auto right = stepCell(visual, b - 1, length);
            auto box = left.getUnion(right).reduced(1, 1);
            const int ti = smother::clampi((int)sv.type, 0, 7);
            const bool selected = (sel == i);
            g.setColour(sleeveFill[ti].withAlpha(selected ? 0.85f : 0.62f));
            g.fillRoundedRectangle(box.toFloat(), 3.f);
            // Type texture (monochrome patterns so types stay distinct).
            g.saveState();
            g.reduceClipRegion(box);
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            if (ti == 0)
            { // MUTE — dense verticals
                for (int x = box.getX(); x < box.getRight(); x += 3)
                    g.drawVerticalLine(x, (float)box.getY(), (float)box.getBottom());
            }
            else if (ti == 1)
            { // LP — fade from left
                juce::ColourGradient gr(juce::Colours::white.withAlpha(0.18f), (float)box.getX(), 0,
                                        juce::Colours::transparentWhite, (float)box.getRight(), 0, false);
                g.setGradientFill(gr);
                g.fillRect(box);
            }
            else if (ti == 2)
            { // HP — fade from right
                juce::ColourGradient gr(juce::Colours::transparentWhite, (float)box.getX(), 0,
                                        juce::Colours::white.withAlpha(0.18f), (float)box.getRight(), 0, false);
                g.setGradientFill(gr);
                g.fillRect(box);
            }
            else if (ti == 3)
            { // CRUSH — checker
                for (int y = box.getY(); y < box.getBottom(); y += 4)
                    for (int x = box.getX() + ((y / 4) & 1) * 4; x < box.getRight(); x += 8)
                        g.fillRect(x, y, 4, 4);
            }
            else if (ti == 4)
            { // DISTORT — thick border only, handled below
            }
            else if (ti == 5)
            { // FLANGE — diagonals
                for (int x = box.getX() - box.getHeight(); x < box.getRight(); x += 6)
                    g.drawLine((float)x, (float)box.getY(), (float)(x + box.getHeight()), (float)box.getBottom(),
                               1.f);
            }
            else if (ti == 6)
            { // DELAY — dots
                for (int y = box.getY() + 3; y < box.getBottom(); y += 6)
                    for (int x = box.getX() + 3; x < box.getRight(); x += 6)
                        g.fillEllipse((float)x, (float)y, 2.f, 2.f);
            }
            else
            { // REVERB — soft inner glow
                g.setColour(juce::Colours::white.withAlpha(0.08f));
                g.fillRoundedRectangle(box.reduced(3).toFloat(), 4.f);
            }
            g.restoreState();
            g.setColour(selected ? juce::Colours::white : juce::Colour(0xffaaaaaa));
            g.drawRoundedRectangle(box.toFloat(), 3.f, selected || ti == 4 ? 1.8f : 1.1f);
            g.setFont(fb);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.drawText(smother::sleeveNames[ti], box.reduced(3, 0), juce::Justification::centred);
            // Resize handles
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.fillRect(box.getX(), box.getY(), 3, box.getHeight());
            g.fillRect(box.getRight() - 3, box.getY(), 3, box.getHeight());
            juce::ignoreUnused(all);
        }
    };

    paintSleeves(0, p.allSleeves.data(), p.allCount, 16, true, proc.selectedAll);
    for (int i = 0; i < nL; ++i)
    {
        const auto &ln = p.lanes[(size_t)i];
        const int sel = (proc.selectedLane == i) ? proc.selectedSleeve : -1;
        paintSleeves(i + 1, ln.sleeves.data(), ln.sleeveCount, ln.length, false, sel);
    }
}

static int hitStep(GridView &g, int visual, int length, juce::Point<int> pt)
{
    for (int s = 0; s < length; ++s)
        if (g.stepCell(visual, s, length).contains(pt))
            return s;
    return -1;
}

static int hitSleeve(const smother::Sleeve *list, int n, int length, int visual, GridView &g,
                     juce::Point<int> pt, int &edge)
{
    edge = 0;
    for (int i = n - 1; i >= 0; --i)
    {
        const auto &sv = list[i];
        const int a = smother::clampi(sv.start, 0, length - 1);
        const int b = smother::clampi(sv.start + sv.length, a + 1, length);
        auto box = g.stepCell(visual, a, length).getUnion(g.stepCell(visual, b - 1, length)).reduced(1, 1);
        if (!box.contains(pt))
            continue;
        if (pt.x <= box.getX() + 6)
            edge = -1;
        else if (pt.x >= box.getRight() - 6)
            edge = 1;
        return i;
    }
    return -1;
}

void GridView::mouseDown(const juce::MouseEvent &e)
{
    auto p = proc.pattern();
    const int nL = p.numLanes;
    downRow = -1;
    drag = None;
    for (int v = 0; v <= nL; ++v)
        if (rowBounds(v).contains(e.getPosition()))
            downRow = v;
    if (downRow < 0)
        return;
    const bool all = downRow == 0;
    const int lane = downRow - 1;
    const int length = all ? 16 : p.lanes[(size_t)lane].length;
    int edge = 0;
    const int si =
        all ? hitSleeve(p.allSleeves.data(), p.allCount, length, 0, *this, e.getPosition(), edge)
            : hitSleeve(p.lanes[(size_t)lane].sleeves.data(), p.lanes[(size_t)lane].sleeveCount, length,
                        downRow, *this, e.getPosition(), edge);
    downStep = hitStep(*this, downRow, length, e.getPosition());
    if (si >= 0)
    {
        dragSleeve = si;
        dragAll = all ? 1 : 0;
        if (all)
        {
            proc.selectedAll = si;
            proc.selectedSleeve = -1;
            proc.selectedLane = 0;
        }
        else
        {
            proc.selectedLane = lane;
            proc.selectedSleeve = si;
            proc.selectedAll = -1;
        }
        auto sv = all ? p.allSleeves[(size_t)si] : p.lanes[(size_t)lane].sleeves[(size_t)si];
        startStart = sv.start;
        startLen = sv.length;
        drag = edge < 0 ? ResizeL : (edge > 0 ? ResizeR : MoveSleeve);
        return;
    }
    if (sleeveMode || e.mods.isShiftDown() || all)
    {
        if (downStep < 0)
            return;
        drag = NewSleeve;
        dragAll = all ? 1 : 0;
        lastToggle = downStep;
        return;
    }
    if (!all && downStep >= 0)
    {
        proc.selectedLane = lane;
        proc.selectedSleeve = -1;
        proc.selectedAll = -1;
        auto pat = p;
        auto &v = pat.lanes[(size_t)lane].vel[(size_t)downStep];
        v = v > 0 ? 0.f : (e.mods.isCommandDown() ? 0.6f : 1.f);
        proc.setPattern(pat);
        drag = Step;
        lastToggle = downStep;
    }
}

void GridView::mouseDrag(const juce::MouseEvent &e)
{
    if (drag == None || downRow < 0)
        return;
    auto p = proc.pattern();
    const bool all = dragAll != 0;
    const int lane = downRow - 1;
    const int length = all ? 16 : (lane >= 0 ? p.lanes[(size_t)lane].length : 16);
    const int step = hitStep(*this, downRow, length, e.getPosition());
    if (step < 0)
        return;
    if (drag == Step && !all && step != lastToggle)
    {
        auto &v = p.lanes[(size_t)lane].vel[(size_t)step];
        v = v > 0 ? 0.f : 1.f;
        lastToggle = step;
        proc.setPattern(p);
        return;
    }
    if (drag == NewSleeve)
    {
        const int a = std::min(downStep, step), b = std::max(downStep, step);
        // Preview is committed on mouseUp; stash in a temp via start*.
        startStart = a;
        startLen = b - a + 1;
        return;
    }
    if ((drag == MoveSleeve || drag == ResizeL || drag == ResizeR) && dragSleeve >= 0)
    {
        const int delta = step - downStep;
        smother::Sleeve *sv = nullptr;
        if (all && dragSleeve < p.allCount)
            sv = &p.allSleeves[(size_t)dragSleeve];
        else if (!all && lane >= 0 && dragSleeve < p.lanes[(size_t)lane].sleeveCount)
            sv = &p.lanes[(size_t)lane].sleeves[(size_t)dragSleeve];
        if (sv == nullptr)
            return;
        if (drag == MoveSleeve)
        {
            sv->start = smother::clampi(startStart + delta, 0, length - 1);
            sv->length = smother::clampi(startLen, 1, length - sv->start);
        }
        else if (drag == ResizeL)
        {
            const int end = startStart + startLen;
            sv->start = smother::clampi(startStart + delta, 0, end - 1);
            sv->length = smother::clampi(end - sv->start, 1, length - sv->start);
        }
        else
        {
            sv->start = startStart;
            sv->length = smother::clampi(startLen + delta, 1, length - sv->start);
        }
        proc.setPattern(p);
    }
}

void GridView::mouseUp(const juce::MouseEvent &)
{
    if (drag == NewSleeve && startLen >= 1)
    {
        auto p = proc.pattern();
        smother::Sleeve s;
        s.start = startStart;
        s.length = startLen;
        s.type = smother::SleeveType::Mute;
        s.attackMs = 20;
        s.releaseMs = 40;
        s.depth = 1;
        s.curve = 0.3f;
        if (dragAll)
        {
            if (p.allCount < smother::maxSleeves)
            {
                p.allSleeves[(size_t)p.allCount] = s;
                proc.selectedAll = p.allCount;
                proc.selectedSleeve = -1;
                ++p.allCount;
            }
        }
        else if (downRow > 0)
        {
            auto &ln = p.lanes[(size_t)(downRow - 1)];
            if (ln.sleeveCount < smother::maxSleeves)
            {
                ln.sleeves[(size_t)ln.sleeveCount] = s;
                proc.selectedLane = downRow - 1;
                proc.selectedSleeve = ln.sleeveCount;
                proc.selectedAll = -1;
                ++ln.sleeveCount;
            }
        }
        proc.setPattern(p);
    }
    drag = None;
    startLen = 0;
}

void GridView::mouseDoubleClick(const juce::MouseEvent &e)
{
    auto p = proc.pattern();
    const bool all = downRow == 0;
    int edge = 0;
    const int lane = downRow - 1;
    const int length = all ? 16 : (lane >= 0 ? p.lanes[(size_t)lane].length : 16);
    // Re-hit in case mouseDown didn't run on this path first.
    int vrow = -1;
    for (int v = 0; v <= p.numLanes; ++v)
        if (rowBounds(v).contains(e.getPosition()))
            vrow = v;
    if (vrow < 0)
        return;
    const bool isAll = vrow == 0;
    const int ln = vrow - 1;
    const int len = isAll ? 16 : p.lanes[(size_t)ln].length;
    const int si =
        isAll ? hitSleeve(p.allSleeves.data(), p.allCount, len, 0, *this, e.getPosition(), edge)
              : hitSleeve(p.lanes[(size_t)ln].sleeves.data(), p.lanes[(size_t)ln].sleeveCount, len, vrow,
                          *this, e.getPosition(), edge);
    juce::ignoreUnused(edge, length, all);
    if (si < 0)
        return;
    auto &sv = isAll ? p.allSleeves[(size_t)si] : p.lanes[(size_t)ln].sleeves[(size_t)si];
    sv.type = (smother::SleeveType)(((int)sv.type + 1) % smother::sleeveTypeCount);
    proc.setPattern(p);
}

DragChip::DragChip(SmotherProcessor &p, bool m) : proc(p), midi(m) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
void DragChip::paint(juce::Graphics &g)
{
    g.setColour(juce::Colour(0xff222222));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
    g.setColour(midi ? juce::Colour(0xffb0b0b0) : juce::Colour(0xffd0d0d0));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.f, 1.f);
    g.setColour(juce::Colours::white);
    g.setFont(11.f);
    g.drawText(midi ? "DRAG MIDI" : "DRAG WAV", getLocalBounds(), juce::Justification::centred);
}
void DragChip::mouseDrag(const juce::MouseEvent &e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    juce::String err;
    auto f = midi ? proc.exportMidi(err) : proc.exportWav(err);
    lastError = err;
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this, [this]() { dragging = false; });
    else
        dragging = false;
}

SampleDrop::SampleDrop(SmotherProcessor &p) : proc(p) {}
void SampleDrop::paint(juce::Graphics &g)
{
    g.setColour(hover ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff1c1c1c));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.f);
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.f, 1.f);
    auto pat = proc.pattern();
    const int ln = juce::jlimit(0, pat.numLanes - 1, proc.selectedLane);
    const auto &lane = pat.lanes[(size_t)ln];
    g.setColour(juce::Colours::white);
    g.setFont(11.f);
    const juce::String t = lane.hasUser ? juce::String(lane.userName) : "DROP SAMPLE  (WAV / AIFF / FLAC)";
    g.drawText(t, getLocalBounds().reduced(6, 0), juce::Justification::centredLeft);
}
void SampleDrop::mouseDown(const juce::MouseEvent &)
{
    auto chooser = std::make_shared<juce::FileChooser>("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser &c) {
                             auto f = c.getResult();
                             if (f.existsAsFile())
                                 proc.loadSample(proc.selectedLane, f);
                         });
}
bool SampleDrop::isInterestedInFileDrag(const juce::StringArray &files)
{
    for (auto &s : files)
        if (s.endsWithIgnoreCase(".wav") || s.endsWithIgnoreCase(".aif") || s.endsWithIgnoreCase(".aiff") ||
            s.endsWithIgnoreCase(".flac") || s.endsWithIgnoreCase(".ogg"))
            return true;
    return false;
}
void SampleDrop::filesDropped(const juce::StringArray &files, int, int)
{
    hover = false;
    for (auto &s : files)
    {
        juce::File f(s);
        if (isInterestedInFileDrag({ s }) && proc.loadSample(proc.selectedLane, f))
            break;
    }
    repaint();
}

SmotherEditor::SmotherEditor(SmotherProcessor &p)
    : AudioProcessorEditor(&p), proc(p), grid(p), midiChip(p, true), wavChip(p, false), drop(p)
{
    setLookAndFeel(&look);
    setSize(1180, 740);
    setResizable(true, true);
    setResizeLimits(980, 620, 1800, 1200);
    for (int i = 0; i < smother::Count; ++i)
    {
        const auto &d = smother::definitions[i];
        labels[(size_t)i].setText(d.name, juce::dontSendNotification);
        labels[(size_t)i].setJustificationType(juce::Justification::centred);
        labels[(size_t)i].setInterceptsMouseClicks(false, false);
        addAndMakeVisible(labels[(size_t)i]);
        if (d.choice)
        {
            if (juce::String(d.id) == "kit")
                for (auto *nm : smother::kitNames)
                    boxes[(size_t)i].addItem(nm, boxes[(size_t)i].getNumItems() + 1);
            else
                boxes[(size_t)i].addItemList({ "Off", "On" }, 1);
            addAndMakeVisible(boxes[(size_t)i]);
            choices.push_back(std::make_unique<CA>(proc.state, d.id, boxes[(size_t)i]));
        }
        else
        {
            knobs[(size_t)i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knobs[(size_t)i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 12);
            knobs[(size_t)i].setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                                 juce::MathConstants<float>::pi * 2.8f, true);
            addAndMakeVisible(knobs[(size_t)i]);
            attachments.push_back(std::make_unique<SA>(proc.state, d.id, knobs[(size_t)i]));
        }
    }
    presets.addItemList(SmotherProcessor::presetNames(), 1);
    presets.setSelectedItemIndex(proc.lastPreset, juce::dontSendNotification);
    presets.onChange = [this] {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0)
        {
            proc.factory(i);
            grid.repaint();
        }
    };
    addAndMakeVisible(presets);
    auto arm = [this](juce::Button &b) { addAndMakeVisible(b); };
    arm(save);
    arm(load);
    arm(addLane);
    arm(remLane);
    arm(stepMode);
    arm(sleeveMode);
    arm(delSleeve);
    arm(clearSample);
    save.onClick = [this] { file(true); };
    load.onClick = [this] { file(false); };
    addLane.onClick = [this] {
        proc.addLane();
        resized();
        grid.repaint();
    };
    remLane.onClick = [this] {
        proc.removeLane();
        resized();
        grid.repaint();
    };
    stepMode.setClickingTogglesState(true);
    sleeveMode.setClickingTogglesState(true);
    stepMode.setRadioGroupId(77);
    sleeveMode.setRadioGroupId(77);
    stepMode.setToggleState(true, juce::dontSendNotification);
    stepMode.onClick = [this] { grid.sleeveMode = false; };
    sleeveMode.onClick = [this] { grid.sleeveMode = true; };
    delSleeve.onClick = [this] {
        auto p = proc.pattern();
        if (proc.selectedAll >= 0 && proc.selectedAll < p.allCount)
        {
            for (int i = proc.selectedAll; i < p.allCount - 1; ++i)
                p.allSleeves[(size_t)i] = p.allSleeves[(size_t)i + 1];
            --p.allCount;
            proc.selectedAll = -1;
        }
        else if (proc.selectedSleeve >= 0 && proc.selectedLane >= 0 && proc.selectedLane < p.numLanes)
        {
            auto &ln = p.lanes[(size_t)proc.selectedLane];
            if (proc.selectedSleeve < ln.sleeveCount)
            {
                for (int i = proc.selectedSleeve; i < ln.sleeveCount - 1; ++i)
                    ln.sleeves[(size_t)i] = ln.sleeves[(size_t)i + 1];
                --ln.sleeveCount;
                proc.selectedSleeve = -1;
            }
        }
        proc.setPattern(p);
        grid.repaint();
    };
    clearSample.onClick = [this] {
        proc.clearSample(proc.selectedLane);
        drop.repaint();
    };
    addAndMakeVisible(grid);
    addAndMakeVisible(midiChip);
    addAndMakeVisible(wavChip);
    addAndMakeVisible(drop);
    addAndMakeVisible(status);
    addAndMakeVisible(inspTitle);
    addAndMakeVisible(laneTitle);
    status.setJustificationType(juce::Justification::centredLeft);
    inspTitle.setJustificationType(juce::Justification::centredLeft);
    laneTitle.setJustificationType(juce::Justification::centredLeft);

    auto prep = [this](juce::Slider &s, juce::Label &l, const juce::String &n, double lo, double hi, double mid) {
        styleKnob(s, l, n, juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRange(lo, hi, (hi - lo) > 100 ? 1 : ((hi - lo) > 20 ? 0.1 : 0.01));
        if (hi / std::max(lo, 1.0) > 20 && lo > 0)
            s.setSkewFactorFromMidPoint(mid);
        s.onValueChange = [this] { writeInspector(); };
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    prep(tune, tuneL, "TUNE", -12, 12, 0);
    prep(decay, decayL, "DECAY", 0, 1, 0.5);
    prep(tone, toneL, "TONE", 0, 1, 0.5);
    prep(pan, panL, "PAN", -1, 1, 0);
    prep(level, levelL, "LEVEL", 0, 1, 0.7);
    prep(eqLo, eqLoL, "EQ LO", -12, 12, 0);
    prep(eqMid, eqMidL, "EQ MID", -12, 12, 0);
    prep(eqHi, eqHiL, "EQ HI", -12, 12, 0);
    prep(hp, hpL, "HP", 20, 2000, 200);
    prep(lp, lpL, "LP", 200, 20000, 4000);
    prep(drive, driveL, "DRIVE", 0, 1, 0.4);
    prep(crush, crushL, "CRUSH", 0, 1, 0.4);
    prep(trans, transL, "TRANS", 0, 1, 0.4);
    prep(ott, ottL, "OTT", 0, 1, 0.4);
    prep(chop0, chop0L, "START", 0, 1, 0.5);
    prep(chop1, chop1L, "END", 0, 1, 0.5);
    prep(att, attL, "ATTACK", 1, 500, 40);
    prep(rel, relL, "RELEASE", 1, 500, 60);
    prep(depth, depthL, "DEPTH", 0, 1, 0.5);
    prep(curve, curveL, "CURVE", 0, 1, 0.5);
    length.setRange(1, 32, 1);
    length.onValueChange = [this] { writeInspector(); };
    lengthL.setText("LENGTH", juce::dontSendNotification);
    lengthL.setJustificationType(juce::Justification::centred);
    midiNote.setRange(0, 127, 1);
    midiNote.onValueChange = [this] { writeInspector(); };
    midiL.setText("MIDI", juce::dontSendNotification);
    midiL.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(length);
    addAndMakeVisible(lengthL);
    addAndMakeVisible(midiNote);
    addAndMakeVisible(midiL);
    length.setSliderStyle(juce::Slider::LinearHorizontal);
    length.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 14);
    midiNote.setSliderStyle(juce::Slider::LinearHorizontal);
    midiNote.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 14);

    for (auto *nm : smother::rateNames)
        rateBox.addItem(nm, rateBox.getNumItems() + 1);
    for (auto *nm : smother::envNames)
        envBox.addItem(nm, envBox.getNumItems() + 1);
    for (auto *nm : smother::drumNames)
        drumBox.addItem(nm, drumBox.getNumItems() + 1);
    for (auto *nm : smother::sleeveNames)
        typeBox.addItem(nm, typeBox.getNumItems() + 1);
    for (auto *nm : smother::kitNames)
        kitLane.addItem(nm, kitLane.getNumItems() + 1);
    auto combo = [this](juce::ComboBox &c) {
        c.onChange = [this] { writeInspector(); };
        addAndMakeVisible(c);
    };
    combo(rateBox);
    combo(envBox);
    combo(drumBox);
    combo(typeBox);
    combo(kitLane);
    addAndMakeVisible(dbl);
    addAndMakeVisible(mute);
    addAndMakeVisible(solo);
    dbl.onClick = [this] { writeInspector(); };
    mute.onClick = [this] { writeInspector(); };
    solo.onClick = [this] { writeInspector(); };

    syncInspector();
    startTimerHz(30);
}

SmotherEditor::~SmotherEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SmotherEditor::layoutKnob(juce::Slider &s, juce::Label &l, juce::Rectangle<int> r)
{
    l.setBounds(r.removeFromTop(12));
    s.setBounds(r);
}

void SmotherEditor::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(54);
    auto brand = top.removeFromLeft(200);
    juce::ignoreUnused(brand);
    presets.setBounds(top.removeFromLeft(200).reduced(4, 10));
    save.setBounds(top.removeFromLeft(64).reduced(2, 12));
    load.setBounds(top.removeFromLeft(64).reduced(2, 12));
    // Global knobs / choices
    auto *kitBox = &boxes[smother::Kit];
    kitBox->setBounds(top.removeFromLeft(110).reduced(2, 12));
    labels[smother::Kit].setBounds(kitBox->getBounds().translated(0, -16).withHeight(14));
    auto *seqBox = &boxes[smother::Seq];
    seqBox->setBounds(top.removeFromLeft(70).reduced(2, 12));
    labels[smother::Seq].setBounds(seqBox->getBounds().translated(0, -16).withHeight(14));
    auto placeG = [&](int id, int w) {
        auto cell = top.removeFromLeft(w);
        labels[(size_t)id].setBounds(cell.removeFromTop(14));
        knobs[(size_t)id].setBounds(cell);
    };
    placeG(smother::Swing, 64);
    placeG(smother::Human, 64);
    placeG(smother::SleeveHuman, 64);
    placeG(smother::MasterHP, 64);
    placeG(smother::MasterLP, 64);
    placeG(smother::MasterOTT, 64);
    placeG(smother::Output, 64);
    addLane.setBounds(top.removeFromLeft(72).reduced(2, 12));
    remLane.setBounds(top.removeFromLeft(72).reduced(2, 12));

    auto bot = r.removeFromBottom(48);
    midiChip.setBounds(bot.removeFromRight(100).reduced(4, 10));
    wavChip.setBounds(bot.removeFromRight(100).reduced(4, 10));
    stepMode.setBounds(bot.removeFromLeft(80).reduced(2, 10));
    sleeveMode.setBounds(bot.removeFromLeft(90).reduced(2, 10));
    status.setBounds(bot.reduced(8, 8));

    auto right = r.removeFromRight(280).reduced(4, 2);
    laneTitle.setBounds(right.removeFromTop(18));
    auto row1 = right.removeFromTop(26);
    drumBox.setBounds(row1.removeFromLeft(80).reduced(1, 2));
    kitLane.setBounds(row1.removeFromLeft(80).reduced(1, 2));
    envBox.setBounds(row1.removeFromLeft(80).reduced(1, 2));
    auto row2 = right.removeFromTop(26);
    rateBox.setBounds(row2.removeFromLeft(72).reduced(1, 2));
    dbl.setBounds(row2.removeFromLeft(78).reduced(1, 2));
    mute.setBounds(row2.removeFromLeft(58).reduced(1, 2));
    solo.setBounds(row2.removeFromLeft(58).reduced(1, 2));
    auto row3 = right.removeFromTop(22);
    lengthL.setBounds(row3.removeFromLeft(56));
    length.setBounds(row3);
    auto row4 = right.removeFromTop(22);
    midiL.setBounds(row4.removeFromLeft(56));
    midiNote.setBounds(row4);

    auto knobsArea = right.removeFromTop(210);
    const int kw = knobsArea.getWidth() / 5;
    const int kh = knobsArea.getHeight() / 3;
    juce::Slider *ks[] = { &tune, &decay, &tone, &pan, &level, &eqLo, &eqMid, &eqHi, &hp,
                           &lp,   &drive, &crush, &trans, &ott, &chop0 };
    juce::Label *ls[] = { &tuneL, &decayL, &toneL, &panL, &levelL, &eqLoL, &eqMidL, &eqHiL, &hpL,
                          &lpL,   &driveL, &crushL, &transL, &ottL, &chop0L };
    for (int i = 0; i < 15; ++i)
    {
        const int c = i % 5, rw = i / 5;
        layoutKnob(*ks[i], *ls[i], { knobsArea.getX() + c * kw, knobsArea.getY() + rw * kh, kw, kh });
    }
    auto chopRow = right.removeFromTop(70);
    layoutKnob(chop1, chop1L, chopRow.removeFromLeft(kw));
    drop.setBounds(chopRow.removeFromTop(36).reduced(4, 4));
    clearSample.setBounds(chopRow.reduced(4, 4));

    inspTitle.setBounds(right.removeFromTop(18));
    typeBox.setBounds(right.removeFromTop(24).reduced(2, 2));
    auto slvK = right.removeFromTop(90);
    const int sw = slvK.getWidth() / 4;
    layoutKnob(att, attL, slvK.removeFromLeft(sw));
    layoutKnob(rel, relL, slvK.removeFromLeft(sw));
    layoutKnob(depth, depthL, slvK.removeFromLeft(sw));
    layoutKnob(curve, curveL, slvK);
    delSleeve.setBounds(right.removeFromTop(26).reduced(4, 2));

    grid.setBounds(r.reduced(2, 2));
}

void SmotherEditor::paint(juce::Graphics &g)
{
    g.fillAll(bg);
    g.setColour(line);
    g.drawRect(getLocalBounds(), 1);
    auto lookF = fontOf(look.bold, 22);
    g.setFont(lookF);
    g.setColour(juce::Colours::white);
    g.drawText("CMPLXTY", 16, 8, 200, 20, juce::Justification::centredLeft);
    g.setFont(fontOf(look.face, 13));
    g.setColour(dim);
    g.drawText("SMOTHER", 16, 28, 200, 16, juce::Justification::centredLeft);
    // Meter
    const float m = juce::jlimit(0.f, 1.f, proc.meter.load());
    auto meter = juce::Rectangle<int>(getWidth() - 288, getHeight() - 36, 70, 8);
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(meter);
    g.setColour(accent);
    g.fillRect(meter.withWidth(int(meter.getWidth() * m)));
}

void SmotherEditor::timerCallback()
{
    grid.repaint();
    drop.repaint();
    if (!midiChip.lastError.isEmpty())
    {
        status.setText(midiChip.lastError, juce::dontSendNotification);
        midiChip.lastError.clear();
    }
    if (!wavChip.lastError.isEmpty())
    {
        status.setText(wavChip.lastError, juce::dontSendNotification);
        wavChip.lastError.clear();
    }
    const int ps = proc.playStep(0);
    const auto bpm = proc.tempo();
    status.setText(juce::String::formatted("  %.1f BPM    step %d    -1 dB limiter    %d lanes", bpm,
                                           ps, proc.pattern().numLanes),
                   juce::dontSendNotification);
    syncInspector();
}

void SmotherEditor::syncInspector()
{
    if (syncing)
        return;
    syncing = true;
    auto p = proc.pattern();
    const int ln = juce::jlimit(0, std::max(0, p.numLanes - 1), proc.selectedLane);
    const auto &lane = p.lanes[(size_t)ln];
    laneTitle.setText("LANE  " + juce::String(smother::drumLabel(lane.drum)) +
                          (lane.hasUser ? "  ·  sample" : ""),
                      juce::dontSendNotification);
    drumBox.setSelectedItemIndex((int)lane.drum, juce::dontSendNotification);
    kitLane.setSelectedItemIndex(smother::clampi(lane.kit, 0, 11), juce::dontSendNotification);
    envBox.setSelectedItemIndex((int)lane.env, juce::dontSendNotification);
    rateBox.setSelectedItemIndex((int)lane.rate, juce::dontSendNotification);
    dbl.setToggleState(lane.doubleLen, juce::dontSendNotification);
    mute.setToggleState(lane.mute, juce::dontSendNotification);
    solo.setToggleState(lane.solo, juce::dontSendNotification);
    length.setValue(lane.length, juce::dontSendNotification);
    midiNote.setValue(lane.midiNote, juce::dontSendNotification);
    tune.setValue(lane.tune, juce::dontSendNotification);
    decay.setValue(lane.decay, juce::dontSendNotification);
    tone.setValue(lane.tone, juce::dontSendNotification);
    pan.setValue(lane.pan, juce::dontSendNotification);
    level.setValue(lane.level, juce::dontSendNotification);
    eqLo.setValue(lane.eqLow, juce::dontSendNotification);
    eqMid.setValue(lane.eqMid, juce::dontSendNotification);
    eqHi.setValue(lane.eqHigh, juce::dontSendNotification);
    hp.setValue(lane.hp, juce::dontSendNotification);
    lp.setValue(lane.lp, juce::dontSendNotification);
    drive.setValue(lane.drive, juce::dontSendNotification);
    crush.setValue(lane.crush, juce::dontSendNotification);
    trans.setValue(lane.transient, juce::dontSendNotification);
    ott.setValue(lane.ott, juce::dontSendNotification);
    chop0.setValue(lane.chopStart, juce::dontSendNotification);
    chop1.setValue(lane.chopEnd, juce::dontSendNotification);

    const smother::Sleeve *sv = nullptr;
    if (proc.selectedAll >= 0 && proc.selectedAll < p.allCount)
        sv = &p.allSleeves[(size_t)proc.selectedAll];
    else if (proc.selectedSleeve >= 0 && proc.selectedSleeve < lane.sleeveCount)
        sv = &lane.sleeves[(size_t)proc.selectedSleeve];
    if (sv)
    {
        inspTitle.setText("SLEEVE  " + juce::String(smother::sleeveNames[(int)sv->type]),
                          juce::dontSendNotification);
        typeBox.setSelectedItemIndex((int)sv->type, juce::dontSendNotification);
        att.setValue(sv->attackMs, juce::dontSendNotification);
        rel.setValue(sv->releaseMs, juce::dontSendNotification);
        depth.setValue(sv->depth, juce::dontSendNotification);
        curve.setValue(sv->curve, juce::dontSendNotification);
    }
    else
        inspTitle.setText("SLEEVE  (draw in SLEEVES mode, or Shift-drag)", juce::dontSendNotification);
    syncing = false;
}

void SmotherEditor::writeInspector()
{
    if (syncing)
        return;
    auto p = proc.pattern();
    const int ln = juce::jlimit(0, std::max(0, p.numLanes - 1), proc.selectedLane);
    auto &lane = p.lanes[(size_t)ln];
    if (drumBox.getSelectedItemIndex() >= 0)
        lane.drum = (smother::DrumType)drumBox.getSelectedItemIndex();
    if (kitLane.getSelectedItemIndex() >= 0)
        lane.kit = kitLane.getSelectedItemIndex();
    if (envBox.getSelectedItemIndex() >= 0)
        lane.env = (smother::EnvShape)envBox.getSelectedItemIndex();
    if (rateBox.getSelectedItemIndex() >= 0)
        lane.rate = (smother::Rate)rateBox.getSelectedItemIndex();
    lane.doubleLen = dbl.getToggleState();
    lane.mute = mute.getToggleState();
    lane.solo = solo.getToggleState();
    lane.length = (int)length.getValue();
    lane.midiNote = (int)midiNote.getValue();
    lane.tune = (float)tune.getValue();
    lane.decay = (float)decay.getValue();
    lane.tone = (float)tone.getValue();
    lane.pan = (float)pan.getValue();
    lane.level = (float)level.getValue();
    lane.eqLow = (float)eqLo.getValue();
    lane.eqMid = (float)eqMid.getValue();
    lane.eqHigh = (float)eqHi.getValue();
    lane.hp = (float)hp.getValue();
    lane.lp = (float)lp.getValue();
    lane.drive = (float)drive.getValue();
    lane.crush = (float)crush.getValue();
    lane.transient = (float)trans.getValue();
    lane.ott = (float)ott.getValue();
    lane.chopStart = (float)chop0.getValue();
    lane.chopEnd = (float)chop1.getValue();

    smother::Sleeve *sv = nullptr;
    if (proc.selectedAll >= 0 && proc.selectedAll < p.allCount)
        sv = &p.allSleeves[(size_t)proc.selectedAll];
    else if (proc.selectedSleeve >= 0 && proc.selectedSleeve < lane.sleeveCount)
        sv = &lane.sleeves[(size_t)proc.selectedSleeve];
    if (sv)
    {
        if (typeBox.getSelectedItemIndex() >= 0)
            sv->type = (smother::SleeveType)typeBox.getSelectedItemIndex();
        sv->attackMs = (float)att.getValue();
        sv->releaseMs = (float)rel.getValue();
        sv->depth = (float)depth.getValue();
        sv->curve = (float)curve.getValue();
    }
    proc.setPattern(p);
    grid.repaint();
}

void SmotherEditor::file(bool saveFile)
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("CMPLXTY SMOTHER");
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(saveFile ? "Save preset" : "Load preset", dir, "*.smother");
    const auto flags = saveFile ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                   juce::FileBrowserComponent::warnAboutOverwriting)
                                : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    chooser->launchAsync(flags, [this, saveFile](const juce::FileChooser &c) {
        auto f = c.getResult();
        if (f == juce::File())
            return;
        if (saveFile)
        {
            if (f.getFileExtension().isEmpty())
                f = f.withFileExtension(".smother");
            juce::MemoryBlock mb;
            proc.getStateInformation(mb);
            f.replaceWithData(mb.getData(), mb.getSize());
        }
        else
        {
            juce::MemoryBlock mb;
            if (f.loadFileAsData(mb))
                proc.setStateInformation(mb.getData(), (int)mb.getSize());
            grid.repaint();
        }
    });
}
