#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff0c0c0c), kPanel (0xff181818), kPri (0xffffffff), kSec (0xffb8b8b8), kDim (0xff9f9f9f), kTrack (0xff2d2d2d);
constexpr int kFooterH = 22;

using vzui::uiFont;

float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// Draws "Re&set" as Re<u>s</u>et. Returns the shortcut letter (lower case) or 0.
juce::juce_wchar drawMenuText (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, const juce::Font& font)
{
    const int amp = text.indexOfChar ('&');
    g.setFont (font);
    if (amp < 0 || amp >= text.length() - 1) { g.drawText (text, r, juce::Justification::centredLeft, false); return 0; }
    const auto pre = text.substring (0, amp), key = text.substring (amp + 1, amp + 2), post = text.substring (amp + 2);
    const float wPre = textWidth (font, pre), wKey = textWidth (font, key);
    const float x = (float) r.getX(), cy = (float) r.getCentreY();
    g.drawText (pre, juce::Rectangle<float> (x, (float) r.getY(), wPre + 2, (float) r.getHeight()), juce::Justification::centredLeft, false);
    g.drawText (key, juce::Rectangle<float> (x + wPre, (float) r.getY(), wKey + 2, (float) r.getHeight()), juce::Justification::centredLeft, false);
    const float uy = cy + font.getHeight() * 0.5f - 1.0f;
    g.fillRect (x + wPre, uy, wKey, 1.0f);
    g.drawText (post, juce::Rectangle<float> (x + wPre + wKey, (float) r.getY(), (float) r.getWidth() - wPre - wKey, (float) r.getHeight()), juce::Justification::centredLeft, false);
    return juce::CharacterFunctions::toLowerCase (key[0]);
}
juce::juce_wchar shortcutOf (const juce::String& text)
{
    const int amp = text.indexOfChar ('&');
    return (amp >= 0 && amp < text.length() - 1) ? juce::CharacterFunctions::toLowerCase (text[amp + 1]) : 0;
}
juce::String stripAmp (const juce::String& t) { return t.replace ("&", ""); }
} // namespace

namespace vzui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, (size_t) BinaryData::BarlowRegular_ttfSize);
    static const juce::Typeface::Ptr semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, (size_t) BinaryData::BarlowSemiBold_ttfSize);
    return bold ? semi : regular;
}
} // namespace vzui

//==============================================================================
namespace vzui
{
PopupOverlay::PopupOverlay() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); setVisible (false); setInterceptsMouseClicks (true, true); }

void PopupOverlay::show (juce::Point<int> where, std::vector<Item> newItems)
{
    items = std::move (newItems);
    const auto f = uiFont (13.0f);
    float w = 150.0f;
    for (auto& it : items) w = juce::jmax (w, textWidth (f, stripAmp (it.text.upToFirstOccurrenceOf ("\t", false, false))) + textWidth (uiFont (11.0f), it.text.fromFirstOccurrenceOf ("\t", false, false)) + 52.0f);
    const int h = (int) items.size() * 24 + 8 + (int) std::count_if (items.begin(), items.end(), [] (const Item& i) { return i.separatorAbove; }) * 6;
    panel = { where.x, where.y, (int) w, h };
    panel.setPosition (juce::jlimit (4, juce::jmax (4, getWidth() - panel.getWidth() - 4), panel.getX()),
                       juce::jlimit (4, juce::jmax (4, getHeight() - panel.getHeight() - 4), panel.getY()));
    hover = -1; open = true;
    setVisible (true); toFront (false);
    hostkeys::remember (*this);                          // which host window has the keyboard right now
    setWantsKeyboardFocus (true); grabKeyboardFocus();   // only while the menu is open, so its shortcut letters work
    repaint();
}

void PopupOverlay::dismiss()
{
    const bool wasOpen = open;
    open = false; setWantsKeyboardFocus (false); giveAwayKeyboardFocus(); setVisible (false);
    if (wasOpen) hostkeys::release (*this);   // native focus back to the host window (Space = play / pause keeps working)
}

juce::Rectangle<int> PopupOverlay::rowBounds (int i) const
{
    int y = panel.getY() + 4;
    for (int k = 0; k <= i; ++k)
    {
        if (items[(size_t) k].separatorAbove) y += 6;
        if (k == i) return { panel.getX() + 2, y, panel.getWidth() - 4, 24 };
        y += 24;
    }
    return {};
}
int PopupOverlay::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) items.size(); ++i) if (rowBounds (i).contains (p)) return i;
    return -1;
}

void PopupOverlay::paint (juce::Graphics& g)
{
    if (! open) return;
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (panel.translated (2, 3).toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff202020)); g.fillRoundedRectangle (panel.toFloat(), 6.0f);
    g.setColour (kPri.withAlpha (0.7f)); g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
    const auto f = uiFont (13.0f);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto& it = items[(size_t) i]; auto r = rowBounds (i);
        if (it.separatorAbove) { g.setColour (kDim.withAlpha (0.35f)); g.fillRect (panel.getX() + 8, r.getY() - 4, panel.getWidth() - 16, 1); }
        if (i == hover && it.enabled) { g.setColour (kSec.darker (0.5f)); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
        g.setColour ((it.enabled ? juce::Colours::white : kDim.withAlpha (0.6f)));
        if (it.ticked)
        {
            juce::Path tick; auto t = juce::Rectangle<float> ((float) r.getX() + 6, (float) r.getCentreY() - 5, 10, 10);
            tick.startNewSubPath (t.getX(), t.getCentreY()); tick.lineTo (t.getX() + 3.5f, t.getBottom() - 1); tick.lineTo (t.getRight(), t.getY() + 1);
            g.setColour (kPri); g.strokePath (tick, juce::PathStrokeType (2.0f)); g.setColour (juce::Colours::white);
        }
        const auto label = it.text.upToFirstOccurrenceOf ("\t", false, false), hint = it.text.fromFirstOccurrenceOf ("\t", false, false);
        drawMenuText (g, r.withTrimmedLeft (24), label, f);
        if (hint.isNotEmpty()) { g.setColour (kDim); g.setFont (uiFont (11.0f)); g.drawText (hint, r.withTrimmedRight (8), juce::Justification::centredRight, false); }
    }
}

void PopupOverlay::mouseMove (const juce::MouseEvent& e) { const int h = itemAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
void PopupOverlay::mouseDown (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (i >= 0 && items[(size_t) i].enabled) trigger (i); else dismiss();
}
void PopupOverlay::trigger (int i)
{
    auto fn = items[(size_t) i].action;
    dismiss();
    if (fn) fn();
}
bool PopupOverlay::keyPressed (const juce::KeyPress& k)
{
    if (! open) return false;
    if (k.isKeyCode (juce::KeyPress::escapeKey)) { dismiss(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
    const int n = (int) items.size();
    if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::upKey))
    {
        const int d = k.isKeyCode (juce::KeyPress::downKey) ? 1 : -1;
        int i = hover;
        for (int tries = 0; tries < n; ++tries) { i = (i + d + n) % n; if (items[(size_t) i].enabled) break; }
        hover = i; repaint(); return true;
    }
    if (k.isKeyCode (juce::KeyPress::returnKey) && hover >= 0 && items[(size_t) hover].enabled) { trigger (hover); return true; }
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c != 0)
        for (int i = 0; i < n; ++i)
            if (items[(size_t) i].enabled && shortcutOf (items[(size_t) i].text) == c) { trigger (i); return true; }
    return false; // not ours (Space, transport keys, ...): let it travel on to the host
}

//------------------------------------------------------------------------------
UpdateBanner::UpdateBanner()
{
    for (auto* b : { &update, &later, &skip }) addAndMakeVisible (b);
    update.onClick = [this] { if (onUpdate) onUpdate(); };
    later.onClick = [this] { if (onLater) onLater(); };
    skip.onClick = [this] { if (onSkip) onSkip(); };
    setVisible (false);
}
void UpdateBanner::show (const vz::upd::Info& i)
{
    title = "v" + i.version + " AVAILABLE";
    setVisible (true); toFront (false); repaint();
}
void UpdateBanner::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff202020)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kSec.withAlpha (0.85f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (kSec); g.fillEllipse (10.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (juce::Colours::white); g.setFont (uiFont (12.0f, true));
    g.drawText (title, 22, 0, 110, getHeight(), juce::Justification::centredLeft, false);
}
void UpdateBanner::resized()
{
    int x = getWidth() - 6; const int y = 4, h = getHeight() - 8;
    skip.setBounds (x - 118, y, 118, h); x -= 118 + 4;
    later.setBounds (x - 52, y, 52, h); x -= 52 + 4;
    update.setBounds (x - 60, y, 60, h);
}
} // namespace vzui

//==============================================================================
using Panel = vzui::Panel;

Panel::Look::Look()
{
    setDefaultSansSerifTypeface (vzui::uiTypeface (false));
    setColour (juce::Slider::rotarySliderFillColourId, kPri);
    setColour (juce::Slider::rotarySliderOutlineColourId, kTrack);
    setColour (juce::Slider::thumbColourId, kSec);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kSec.darker (0.3f));
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kPri.withAlpha (0.55f));
    setColour (juce::ComboBox::arrowColourId, kPri);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kSec.darker (0.5f));
    setColour (juce::TextButton::buttonColourId, kTrack);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void Panel::Look::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool down)
{
    g.setColour (kPri.withAlpha (down ? 1.0f : over ? 0.9f : 0.5f));
    for (int i = 1; i <= 3; ++i)
    {
        const float o = (float) i * 4.0f;
        g.drawLine ((float) w - o, (float) h - 1.0f, (float) w - 1.0f, (float) h - o, 1.4f);
    }
}

void Panel::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float d = juce::jmin (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float ring = juce::jmax (3.0f, d * 0.075f), rr = r - ring * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    // knob body
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272727), c.x, c.y - r, juce::Colour (0xff111111), c.x, c.y + r, false));
    g.fillEllipse (c.x - rr + ring, c.y - rr + ring, (rr - ring) * 2.0f, (rr - ring) * 2.0f);
    // track
    juce::Path track; track.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
    g.setColour (kTrack); g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc (bipolar controls grow from the middle)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float from = a0 + zero * (a1 - a0);
    if (std::fabs (ang - from) > 0.01f)
    {
        juce::Path val; val.addCentredArc (c.x, c.y, rr, rr, 0.0f, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (kPri.withAlpha (0.25f)); g.strokePath (val, juce::PathStrokeType (ring + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kPri); g.strokePath (val, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // pointer
    const float pr1 = (rr - ring) * 0.35f, pr2 = (rr - ring) * 0.88f;
    juce::Path p; p.startNewSubPath (c.x + std::sin (ang) * pr1, c.y - std::cos (ang) * pr1); p.lineTo (c.x + std::sin (ang) * pr2, c.y - std::cos (ang) * pr2);
    g.setColour (kSec); g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Panel::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kPri.withAlpha (0.20f) : kTrack); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? kPri : kDim.withAlpha (highlighted ? 0.9f : 0.6f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const float d = r.getHeight() - 6.0f;
    const float cx = on ? r.getRight() - 3.0f - d : r.getX() + 3.0f;
    g.setColour (on ? kPri : kDim); g.fillRoundedRectangle (cx, r.getY() + 3.0f, d, d, 2.0f);
    g.setFont (uiFont (9.0f, true)); g.setColour (on ? kPri : kDim);
    g.drawText (on ? "ON" : "OFF", r.withTrimmedLeft (on ? 5.0f : d + 6.0f).withTrimmedRight (on ? d + 6.0f : 3.0f), juce::Justification::centred, false);
}

void Panel::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.brighter (down ? 0.4f : highlighted ? 0.2f : 0.0f)); g.fillRoundedRectangle (r, 3.0f);
    g.setColour (kPri.withAlpha (0.55f)); g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

//==============================================================================
namespace vzui
{
Panel::KnobBox& Panel::addKnob (const char* id, const char* name)
{
    auto& k = *knobBoxes.emplace_back (std::make_unique<KnobBox>());
    k.slider.setLookAndFeel (&look);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 86, 18);   // read-only: a click on the number must not open an editor (typing is in the right-click menu)
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setLookAndFeel (&look);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    auto* prm = proc.apvts.getParameter (id);
    jassert (prm != nullptr);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));   // double-click resets to the default
    k.slider.textFromValueFunction = [prm] (double v)
    {
        auto t = prm->getText (prm->convertTo0to1 ((float) v), 0);
        const auto u = prm->getLabel();
        return u.isEmpty() ? t : t + " " + u;
    };
    k.slider.valueFromTextFunction = [prm] (const juce::String& s) { return (double) prm->convertFrom0to1 (prm->getValueForText (s)); };
    k.slider.updateText();
    paramOf.push_back ({ &k.slider, id });
    k.slider.onContextMenu = [this, id = juce::String (id)] (vzui::Knob& kn, juce::Point<int> screenPos) { showParamMenu (id, &kn, getLocalPoint (nullptr, screenPos)); };
    return k;
}

Panel::Combo& Panel::addCombo (const char* id, const char* name)
{
    auto& c = *combos.emplace_back (std::make_unique<Combo>());
    c.box.setLookAndFeel (&look);
    c.box.addItemList (proc.apvts.getParameter (id)->getAllValueStrings(), 1);
    addAndMakeVisible (c.box);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setLookAndFeel (&look);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c.box);
    paramOf.push_back ({ &c.box, id }); c.box.addMouseListener (this, true);
    return c;
}

Panel::Tog& Panel::addTog (const char* id, const char* name)
{
    auto& t = *togs.emplace_back (std::make_unique<Tog>());
    t.button.setLookAndFeel (&look);
    addAndMakeVisible (t.button);
    t.label.setText (name, juce::dontSendNotification);
    t.label.setJustificationType (juce::Justification::centredLeft);
    t.label.setLookAndFeel (&look);
    addAndMakeVisible (t.label);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t.button);
    paramOf.push_back ({ &t.button, id }); t.button.addMouseListener (this, false);
    return t;
}

void Panel::applySectionState (Section& s, bool on)
{
    s.on = on;
    s.power.setToggleState (on, juce::dontSendNotification);
    for (auto* c : s.members) c->setAlpha (on ? 1.0f : 0.4f);
    repaint();
}

void Panel::addSection (Section& s, const char* paramId)
{
    s.power.setLookAndFeel (&look);
    s.power.setClickingTogglesState (false);
    addAndMakeVisible (s.power);
    auto* prm = proc.apvts.getParameter (paramId);
    s.att = std::make_unique<juce::ParameterAttachment> (*prm, [this, &s] (float v) { applySectionState (s, v > 0.5f); }, &proc.undoManager);
    s.power.onClick = [&s] { s.att->setValueAsCompleteGesture (s.on ? 0.0f : 1.0f); };
    paramOf.push_back ({ &s.power, paramId }); s.power.addMouseListener (this, false);
    s.att->sendInitialUpdate();
}

Panel::KnobBox* Panel::knobFor (const juce::String& id)
{
    for (auto& pr : paramOf)
        if (pr.second == id)
            for (auto& k : knobBoxes) if (&k->slider == pr.first) return k.get();
    return nullptr;
}

Panel::Panel (VisualizerProcessor& p, juce::AudioProcessorEditor& o) : proc (p), owner (o)
{
    const auto& sp = panelSpec();
    if (! sp.display.isEmpty()) { disp = makeDisplay (proc, *this); if (disp) addAndMakeVisible (*disp); }
    for (auto& ss : sp.secs)
    {
        auto& sec = *sections.emplace_back (std::make_unique<Section>());
        sec.spec = &ss;
        for (auto& c : ss.ctls)
        {
            if (c.kind == 'k')      { auto& k = addKnob (c.id, c.label);  sec.members.insert (sec.members.end(), { &k.slider, &k.label }); }
            else if (c.kind == 'c') { auto& k = addCombo (c.id, c.label); sec.members.insert (sec.members.end(), { &k.box, &k.label }); }
            else                    { auto& k = addTog (c.id, c.label);   sec.members.insert (sec.members.end(), { &k.button, &k.label }); }
        }
        if (ss.powerId != nullptr) addSection (sec, ss.powerId);
    }
    extras = makeTabExtras (proc, *this);
    for (auto& x : extras) if (x.comp) addAndMakeVisible (*x.comp);

    for (auto* b : { &undoBtn, &redoBtn })
    {
        b->setLookAndFeel (&look); addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
    }
    undoBtn.onClick = [this] { proc.undoManager.undo(); };
    redoBtn.onClick = [this] { proc.undoManager.redo(); };

    addChildComponent (banner);
    banner.setLookAndFeel (&look);
    for (auto* b : { &banner.update, &banner.later, &banner.skip }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    banner.onUpdate = [this]
    {
        const auto page = vz::upd::pageForUrl (bannerInfo.url);
        if (vz::upd::isTrustedUrl (page)) juce::URL (page).launchInDefaultBrowser();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    banner.onLater = [this] { banner.setVisible (false); footerNote = {}; repaint(); };
    banner.onSkip = [this]
    {
        auto s = vz::upd::Settings::load(); s.skipVersion = bannerInfo.version; s.save();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    addChildComponent (popup);

    updatesOn = vz::upd::Settings::load().enabled;
    setWantsKeyboardFocus (false);            // never hold keyboard focus: Space & co. must keep driving the host transport
    setMouseClickGrabsKeyboardFocus (false);
    hostkeys::noKeyboardFocus (*this, &popup);   // no child (incl. the sliders' value labels and the display's buttons) may keep keyboard focus
    popup.setWantsKeyboardFocus (false);
    startTimerHz (10);                        // remembers the host's focused window; safety net, see timerCallback
    setSize (VisualizerProcessor::kBaseW, VisualizerProcessor::kBaseH);
    setTab (0);
    // update check: after the editor is up, on a background thread, at most once per 24 h, silent on failure
    juce::MessageManager::callAsync ([sp2 = juce::Component::SafePointer<Panel> (this)] { if (sp2 != nullptr) sp2->startUpdateCheck(); });
}

Panel::~Panel()
{
    stopTimer();
    checker.cancelAndWait();
    disp.reset();
    extras.clear();
    for (auto& k : knobBoxes) { k->slider.setLookAndFeel (nullptr); k->label.setLookAndFeel (nullptr); }
    for (auto& c : combos) { c->box.setLookAndFeel (nullptr); c->label.setLookAndFeel (nullptr); }
    for (auto& t : togs) { t->button.setLookAndFeel (nullptr); t->label.setLookAndFeel (nullptr); }
    for (auto& s : sections) { s->power.setLookAndFeel (nullptr); s->label.setLookAndFeel (nullptr); }
    undoBtn.setLookAndFeel (nullptr); redoBtn.setLookAndFeel (nullptr);
    banner.setLookAndFeel (nullptr);
}

// Only acts when the plug-in window really holds the native keyboard focus and nothing needs it (no popup, no value being typed, no combo menu):
// then the focus goes back to the host. Normally it never fires, because nothing in the editor asks for focus.
void Panel::timerCallback()
{
    hostkeys::remember (*this);
   #if VZ_FOCUS_NET
    if (! popup.isOpen()) hostkeys::release (*this);
   #endif
    undoBtn.setAlpha (proc.undoManager.canUndo() ? 1.0f : 0.4f);
    redoBtn.setAlpha (proc.undoManager.canRedo() ? 1.0f : 0.4f);
}

//==============================================================================
void Panel::startUpdateCheck (bool force)
{
    if (! force && ! vz::upd::Settings::load().enabled) return;
    checker.start ([this, force] (const vz::upd::Result& r)
    {
        if (r.ok && vz::upd::shouldNotify (r, vz::upd::Settings::load(), force)) showBanner (r.info);
        else if (force) { footerNote = r.ok ? juce::String (VisualizerProcessor::kShortName) + " " + VZ_VERSION_STRING + " is up to date" : juce::String ("Could not reach the update server"); repaint(); }
    }, force);
}

void Panel::showBanner (const vz::upd::Info& i)
{
    bannerInfo = i;
    banner.show (i);
    footerNote = i.notes.isNotEmpty() ? "What's new: " + i.notes : juce::String();
    resized(); repaint();
}

juce::Rectangle<int> Panel::updateLinkBounds() const { return footerArea.withTrimmedLeft (footerArea.getWidth() - 190).withTrimmedRight (14).reduced (0, 2); }

void Panel::showUpdateMenu()
{
    const bool on = vz::upd::Settings::load().enabled;
    std::vector<vzui::PopupOverlay::Item> v;
    v.push_back ({ "Check for &updates automatically", [this, on]
                   { auto s = vz::upd::Settings::load(); s.enabled = ! on; s.save(); updatesOn = s.enabled;
                     footerNote = s.enabled ? "Update check on (once a day, nothing is sent about you)" : "Update check off: the plugin never contacts the network"; repaint(); }, true, on });
    v.push_back ({ "Check &now", [this] { footerNote = "Checking..."; repaint(); startUpdateCheck (true); }, true, false });
    auto b = updateLinkBounds();
    popup.show ({ b.getRight() - 230, b.getY() - 70 }, std::move (v));
}

void Panel::resetAll()
{
    for (auto* prm : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
}

juce::String Panel::paramIdOf (juce::Component* c) const
{
    for (; c != nullptr && c != this; c = c->getParentComponent())
        for (auto& pr : paramOf) if (pr.first == c) return pr.second;
    return {};
}

// Right-click menu: our own items (with underlined shortcut letters) plus the host's own parameter menu when the host
// offers one (VST3 IComponentHandler3 -> automation lane, MIDI learn, ...). Without a host menu there is an "Automate" item
// that touches the parameter so the host registers it as the last-touched / automatable one.
void Panel::showParamMenu (const juce::String& id, vzui::Knob* knobPtr, juce::Point<int> pos)
{
    auto* prm = id.isNotEmpty() ? proc.apvts.getParameter (id) : nullptr;
    juce::Component::SafePointer<vzui::Knob> knob (knobPtr);
    std::vector<vzui::PopupOverlay::Item> v;
    if (prm != nullptr)
    {
        v.push_back ({ knob != nullptr ? "&Reset to default\tdouble-click" : "&Reset to default", [prm]
                       { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); } });
        if (knob != nullptr)
        {
            v.push_back ({ "&Type in value...", [knob] { if (knob != nullptr) knob->typeValue(); } });
            v.push_back ({ "&Copy value", [knob] { if (knob != nullptr) juce::SystemClipboard::copyTextToClipboard (knob->getTextFromValue (knob->getValue())); }, true, false, true });
            const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
            const bool pasteOk = clip.isNotEmpty() && clip.length() < 24 && clip.containsAnyOf ("0123456789");
            v.push_back ({ "&Paste value", [knob, clip, prm] { if (knob != nullptr) { prm->beginChangeGesture(); knob->setValue (knob->getValueFromText (clip), juce::sendNotificationSync); prm->endChangeGesture(); } }, pasteOk });
        }
        std::shared_ptr<juce::HostProvidedContextMenu> host;
        if (auto* hc = owner.getHostContext())
            if (auto m = hc->getContextMenuForParameter (prm)) host = std::move (m);
        if (host != nullptr)
        {
            // flatten the host's menu (submenus get a "Name > " prefix); each entry runs the host's own action
            bool first = true;
            std::function<void (const juce::PopupMenu&, const juce::String&)> add = [&] (const juce::PopupMenu& m, const juce::String& prefix)
            {
                juce::PopupMenu::MenuItemIterator it (m);
                while (it.next())
                {
                    auto& item = it.getItem();
                    if (item.isSeparator) { first = true; continue; }
                    if (item.subMenu != nullptr) { add (*item.subMenu, prefix + item.text.replace ("&", "") + " > "); continue; }
                    if (item.text.isEmpty() || v.size() > 14) continue;
                    auto act = item.action;
                    v.push_back ({ prefix + item.text.replace ("&", ""), [host, act] { if (act) act(); }, item.isEnabled && (bool) act, item.isTicked, first });
                    first = false;
                }
            };
            add (host->getEquivalentPopupMenu(), {});
        }
        else
        {
            v.push_back ({ "&Automate", [this, prm]
                           { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getValue()); prm->endChangeGesture();
                             footerNote = "Selected for automation - now use your host's automation command for this parameter"; repaint(); }, true, false, true });
        }
    }
    v.push_back ({ "&Init all controls", [this] { resetAll(); }, true, false, true });
    popup.show (pos, std::move (v));
}

void Panel::demoKnobMenu()
{
    if (auto* k = knobFor (panelSpec().demoKnob)) showParamMenu (panelSpec().demoKnob, &k->slider, k->slider.getBounds().getCentre());
}

juce::Rectangle<int> Panel::tabBounds (int i) const
{
    const auto& sp = panelSpec();
    const int n = (int) sp.tabs.size();
    if (n == 0) return {};
    const int x0 = sp.tabBar.getX() + sp.tabBar.getWidth() * i / n, x1 = sp.tabBar.getX() + sp.tabBar.getWidth() * (i + 1) / n;
    return { x0, sp.tabBar.getY(), x1 - x0, sp.tabBar.getHeight() };
}

void Panel::setTab (int t)
{
    const auto& sp = panelSpec();
    tab = juce::jlimit (0, juce::jmax (0, (int) sp.tabs.size() - 1), t);
    for (auto& s : sections)
    {
        const bool vis = s->spec->tab < 0 || s->spec->tab == tab;
        for (auto* c : s->members) c->setVisible (vis);
        s->power.setVisible (vis && s->spec->powerId != nullptr);
    }
    for (auto& x : extras) if (x.comp) x.comp->setVisible (x.tab < 0 || x.tab == tab);
    repaint();
}

void Panel::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && e.eventComponent == this)
        for (int i = 0; i < (int) panelSpec().tabs.size(); ++i)
            if (tabBounds (i).contains (e.getPosition())) { setTab (i); return; }
    if (e.mods.isPopupMenu())
    {
        if (auto* c = e.eventComponent; c != nullptr && c != this)
            if (const auto id = paramIdOf (c); id.isNotEmpty())
            { showParamMenu (id, dynamic_cast<vzui::Knob*> (c), getLocalPoint (nullptr, e.getScreenPosition())); return; }
    }
    else if (updateLinkBounds().contains (e.getPosition())) showUpdateMenu();
}

// Walks every component: nothing except the open popup / a text editor may want keyboard focus; unhandled keys must not be consumed.
juce::String Panel::auditKeyboard()
{
    juce::String out; int n = 0, bad = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        ++n;
        if (c.getWantsKeyboardFocus() && &c != &popup && dynamic_cast<juce::TextEditor*> (&c) == nullptr)
        { ++bad; out << "  wants focus: " << c.getName() << " (" << typeid (c).name() << ")\n"; }
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    std::function<void (juce::Component&)> walkUp = [&] (juce::Component& c)
    {
        if (&c != this && c.getWantsKeyboardFocus()) { ++bad; out << "  editor-level component wants focus: " << typeid (c).name() << "\n"; }
        for (auto* ch : c.getChildren()) if (ch != this) walkUp (*ch);
    };
    // Slider turns focus back on for its value box on look-and-feel / enablement changes: provoke both and make sure the knobs fix it up again
    for (auto& ks : knobBoxes) { ks->slider.setEnabled (false); ks->slider.setEnabled (true); ks->slider.sendLookAndFeelChange(); }
    {
        juce::Slider stock (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        stock.setBounds (0, 0, 80, 80); int stockWants = 0;
        for (auto* c : stock.getChildren()) stockWants += c->getWantsKeyboardFocus() ? 1 : 0;
        out << "  negative control: stock slider's value box wants focus=" << stockWants << " (expected 1)\n";
        if (stockWants != 1) ++bad;
    }
    walk (*this);
    walkUp (owner);
    if (owner.getWantsKeyboardFocus()) { ++bad; out << "  editor wants focus\n"; }
    out << "  " << n << " components, " << bad << " ask for keyboard focus (expected 0)\n";
    const bool closedWants = popup.getWantsKeyboardFocus();
    out << "  popup closed: wantsFocus=" << (int) closedWants << " (expected 0)\n";
    demoKnobMenu();
    const bool openWants = popup.getWantsKeyboardFocus();
    const bool spaceUsed = popup.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    const bool zUsed     = popup.keyPressed (juce::KeyPress ('z', juce::ModifierKeys(), 'z'));
    const bool rUsed     = popup.isOpen() && popup.keyPressed (juce::KeyPress ('r', juce::ModifierKeys(), 'r'));    // R = Reset, closes the menu
    out << "  menu open: wantsFocus=" << (int) openWants << " (expected 1)  Space consumed=" << (int) spaceUsed << " (expected 0)  unknown letter consumed=" << (int) zUsed
        << " (expected 0)  'R' shortcut consumed+closed=" << (int) (rUsed && ! popup.isOpen()) << " (expected 1)\n";
    demoKnobMenu(); popup.dismiss();
    const bool afterSpace = popup.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    out << "  after dismiss: open=" << (int) popup.isOpen() << " wantsFocus=" << (int) popup.getWantsKeyboardFocus() << " Space consumed=" << (int) afterSpace
        << "  panel wantsFocus=" << (int) getWantsKeyboardFocus() << " panel keyPressed(Space)=" << (int) keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) << " (all expected 0)\n";
    const bool ok = bad == 0 && ! closedWants && openWants && ! spaceUsed && ! zUsed && rUsed && ! popup.getWantsKeyboardFocus() && ! afterSpace && ! getWantsKeyboardFocus();
    out << (ok ? "KEYBOARD AUDIT OK\n" : "KEYBOARD AUDIT FAILED\n");
    return out;
}

//==============================================================================
void Panel::paint (juce::Graphics& g)
{
    const auto& sp = panelSpec();
    g.fillAll (kBg);
    g.setColour (juce::Colours::white.withAlpha (0.015f));
    for (int y = 0; y < getHeight(); y += 4) g.fillRect (0, y, getWidth(), 1);
    g.setColour (kPri);
    g.setFont (uiFont (24.0f, true));
    g.drawText (sp.title, 16, 7, 420, 32, juce::Justification::centredLeft);
    g.setColour (kSec);
    g.fillRect (16, 42, 150, 2);
    g.setColour (kPri); g.fillRect (166, 42, 24, 2);
    g.setColour (kDim); g.setFont (uiFont (10.5f, true));
    g.drawText (sp.subtitle, 200, 36, 300, 12, juce::Justification::centredLeft);

    for (int i = 0; i < (int) sp.tabs.size(); ++i)
    {
        const auto r = tabBounds (i).reduced (1, 0).toFloat();
        const bool sel = i == tab;
        g.setColour (sel ? kPri : kPanel); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kPri.withAlpha (sel ? 1.0f : 0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (sel ? kBg : kSec); g.setFont (uiFont (11.0f, true));
        g.drawText (sp.tabs[(size_t) i], r, juce::Justification::centred, false);
    }
    for (auto& s : sections)
    {
        if (s->spec->tab >= 0 && s->spec->tab != tab) continue;
        const auto r = s->spec->area; const bool on = s->on;
        g.setColour (kPanel);          g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kPri.withAlpha (on ? 0.35f : 0.15f)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (kSec.withAlpha (on ? 0.9f : 0.4f)); g.fillRect (r.getX() + 1, r.getY() + 8, 2, 12);
        g.setColour (on ? kDim : kDim.withAlpha (0.5f)); g.setFont (uiFont (12.0f, true));
        g.drawText (s->spec->title, r.getX() + 10, r.getY() + 4, r.getWidth() - 70, 16, juce::Justification::centredLeft);
    }

    // footer: version, notes, update switch
    g.setFont (uiFont (11.0f));
    g.setColour (kDim.withAlpha (0.8f));
    g.drawText (juce::String ("v") + VZ_VERSION_STRING + "  -  free, GPLv3  -  cmplxty.music", footerArea.getX() + 16, footerArea.getY(), 230, footerArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (footerNote.isNotEmpty() ? kSec.withAlpha (0.9f) : kDim);
    g.drawFittedText (footerNote, footerArea.getX() + 250, footerArea.getY(), footerArea.getWidth() - 250 - 210, footerArea.getHeight(), juce::Justification::centredLeft, 1);
    auto ul = updateLinkBounds();
    g.setColour (updatesOn ? kPri.withAlpha (0.85f) : kDim);
    g.drawText (updatesOn ? "UPDATE CHECK: ON" : "UPDATE CHECK: OFF", ul, juce::Justification::centredRight);
}

void Panel::resized()
{
    const int W = getWidth();
    const auto& sp = panelSpec();
    footerArea = { 0, getHeight() - 24, W, 24 };
    if (disp) disp->setBounds (sp.display);

    size_t ki = 0, ci = 0, ti = 0;
    for (auto& s : sections)
    {
        const auto& ss = *s->spec;
        auto body = ss.area.reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (4);
        if (ss.sideCols > 0)
        {   // grid of combos / toggles, row height 44
            const int cols = ss.sideCols, cw = body.getWidth() / cols;
            int idx = 0;
            for (auto& c : ss.ctls)
            {
                const int cx = body.getX() + (idx % cols) * cw, cy = body.getY() + (idx / cols) * 46; ++idx;
                if (c.kind == 'c')      { auto& k = *combos[ci++]; k.label.setBounds (cx + 2, cy, cw - 6, 15); k.box.setBounds (cx + 2, cy + 16, cw - 6, 24); }
                else if (c.kind == 't') { auto& k = *togs[ti++];   k.label.setBounds (cx + 2, cy, cw - 6, 15); k.button.setBounds (cx + 2, cy + 18, 46, 20); }
                else                    { auto& k = *knobBoxes[ki++]; k.label.setBounds (cx, cy, cw, 14); k.slider.setBounds (cx, cy + 14, cw, 30); }
            }
            if (ss.powerId != nullptr) s->power.setBounds (ss.area.getRight() - 50, ss.area.getY() + 4, 42, 16);
            continue;
        }
        int nk = 0, nside = 0;
        for (auto& c : ss.ctls) (c.kind == 'k' ? nk : nside)++;
        juce::Rectangle<int> col;
        if (nside > 0) col = nk > 0 ? body.removeFromRight (ss.columnW) : body;
        // side column: combos and toggles stacked, vertically centred
        int need = 0; for (auto& c : ss.ctls) need += c.kind == 'c' ? 44 : c.kind == 't' ? 26 : 0;
        int y = col.getY() + juce::jmax (0, (col.getHeight() - need) / 2);
        const int kw = nk > 0 ? body.getWidth() / nk : 0;
        int kx = body.getX();
        for (auto& c : ss.ctls)
        {
            if (c.kind == 'k')
            {
                auto& k = *knobBoxes[ki++];
                juce::Rectangle<int> r (kx, body.getY(), kw, body.getHeight()); kx += kw;
                k.label.setBounds (r.removeFromTop (16)); k.slider.setBounds (r);
            }
            else if (c.kind == 'c')
            {
                auto& k = *combos[ci++];
                k.label.setBounds (col.getX() + 2, y, col.getWidth() - 4, 15); k.box.setBounds (col.getX() + 2, y + 16, col.getWidth() - 4, 24); y += 44;
            }
            else
            {
                auto& k = *togs[ti++];
                k.label.setBounds (col.getX() + 2, y + 2, col.getWidth() - 50, 18); k.button.setBounds (col.getRight() - 44, y + 2, 42, 18); y += 26;
            }
        }
        if (ss.powerId != nullptr) s->power.setBounds (ss.area.getRight() - 50, ss.area.getY() + 4, 42, 16);
    }
    for (auto& x : extras) if (x.comp) x.comp->setBounds (x.area);
    undoBtn.setBounds (W - 128, 12, 56, 24); redoBtn.setBounds (W - 68, 12, 56, 24);
    banner.setBounds (W - 136 - 392, 9, 392, 28);
    popup.setBounds (getLocalBounds());
}

// Test hook: drives the right-click path end to end - with and without a host-provided parameter menu.
namespace
{
struct FakeHostMenu final : juce::HostProvidedContextMenu
{
    std::shared_ptr<int> hits;
    explicit FakeHostMenu (std::shared_ptr<int> h) : hits (std::move (h)) {}
    juce::PopupMenu getEquivalentPopupMenu() const override
    {
        juce::PopupMenu m, sub; auto h = hits;
        m.addItem (juce::PopupMenu::Item ("Show automation lane for parameter").setAction ([h] { ++*h; }));
        sub.addItem (juce::PopupMenu::Item ("Create automation clip").setAction ([h] { *h += 10; }));
        m.addSeparator(); m.addSubMenu ("Host", sub);
        return m;
    }
    void showNativeMenu (juce::Point<int>) const override {}
};
struct FakeHost final : juce::AudioProcessorEditorHostContext
{
    std::shared_ptr<int> hits = std::make_shared<int> (0);
    int asked = 0;
    std::unique_ptr<juce::HostProvidedContextMenu> getContextMenuForParameter (const juce::AudioProcessorParameter*) const override
    { const_cast<FakeHost*> (this)->asked++; return std::make_unique<FakeHostMenu> (hits); }
};
struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};
}

juce::String Panel::auditMenus()
{
    juce::String out; bool ok = true;
    auto check = [&] (bool cond, const juce::String& what) { out << (cond ? "  ok   " : "  FAIL ") << what << "\n"; ok = ok && cond; };
    auto rightClick = [this] (juce::Component& c)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, juce::Point<float> (4.0f, 4.0f), juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, now, juce::Point<float> (4.0f, 4.0f), now, 1, false);
        c.mouseDown (e);
        mouseDown (e);
        c.mouseUp (e);
    };
    const juce::String demoId = panelSpec().demoKnob;
    auto* demo = knobFor (demoId);
    check (demo != nullptr, "demo knob exists");
    if (demo == nullptr) return out + "MENU AUDIT FAILED\n";
    GestureCounter gc;

    // 1) no host menu: custom menu, with an Automate item
    popup.dismiss();
    rightClick (demo->slider);
    auto t = popup.itemTexts();
    out << "  knob menu (no host): " << t.joinIntoString (" | ") << "\n";
    check (popup.isOpen() && t.size() >= 5, "right-click on a knob opens the menu");
    check (t.contains ("&Reset to default\tdouble-click") && t.joinIntoString ("|").contains ("&Automate") && t.contains ("&Type in value..."), "menu has Reset, Type in value and Automate with shortcut letters");
    auto* dp = proc.apvts.getParameter (demoId); dp->addListener (&gc);
    check (popup.keyPressed (juce::KeyPress ('a', juce::ModifierKeys(), 'a')) && ! popup.isOpen() && gc.begins == 1 && gc.ends == 1, "pressing A runs Automate: touches the parameter (begin/end gesture), closes menu");
    dp->removeListener (&gc);

    // 2) switches and combos: menu opens, state unchanged
    for (auto& s : sections)
        if (s->spec->powerId != nullptr)
        {
            auto* pw = proc.apvts.getParameter (s->spec->powerId); const float before = pw->getValue();
            rightClick (s->power);
            check (popup.isOpen() && pw->getValue() == before, juce::String ("right-click on section switch '") + s->spec->powerId + "' opens the menu and does NOT toggle it");
            popup.dismiss();
        }
    for (auto& c : combos)
    {
        rightClick (c->box);
        check (popup.isOpen() && ! popup.itemTexts().contains ("&Type in value..."), "right-click on combo '" + c->label.getText() + "' opens the (no 'type in') menu");
        popup.dismiss();
    }
    for (auto& tg : togs)
    {
        const auto id = paramIdOf (&tg->button); auto* prm = proc.apvts.getParameter (id); const float before = prm->getValue();
        rightClick (tg->button);
        check (popup.isOpen() && prm->getValue() == before, "right-click on toggle '" + id + "' opens the menu without flipping it");
        popup.dismiss();
    }

    // 3) with a host-provided menu
    FakeHost host; owner.setHostContext (&host);
    rightClick (demo->slider);
    t = popup.itemTexts(); out << "  knob menu (host): " << t.joinIntoString (" | ") << "\n";
    check (host.asked >= 1, "asked host context for the parameter menu");
    check (t.joinIntoString ("|").contains ("Show automation lane") && t.joinIntoString ("|").contains ("Host > Create automation clip") && ! t.joinIntoString ("|").contains ("&Automate"),
           "host items shown (submenu flattened), custom Automate replaced");
    check (popup.activate ("Show automation lane") && *host.hits == 1, "clicking a host item runs the host's own action");
    rightClick (demo->slider); popup.activate ("Create automation clip");
    check (*host.hits == 11, "submenu host item runs too");
    popup.dismiss();
    owner.setHostContext (nullptr);

    // 4) every knob: typed text goes through the parameter's own parser (round trip of a non-default value) and double-click resets
    for (auto& k : knobBoxes)
    {
        const auto id = paramIdOf (&k->slider); auto* prm = proc.apvts.getParameter (id);
        const double target = k->slider.getMinimum() + 0.7 * (k->slider.getMaximum() - k->slider.getMinimum());
        const auto txt = k->slider.getTextFromValue (target);
        k->slider.setValue (k->slider.getValueFromText (txt), juce::sendNotificationSync);
        const double got = k->slider.getValue();
        const bool rt = k->slider.getTextFromValue (got) == txt;
        check (rt, "typing \"" + txt + "\" into '" + id + "' gives " + k->slider.getTextFromValue (got));
        prm->setValueNotifyingHost (prm->getDefaultValue());
    }
    out << (ok ? "MENU AUDIT OK\n" : "MENU AUDIT FAILED\n");
    return out;
}
} // namespace vzui

//==============================================================================
VisualizerEditor::VisualizerEditor (VisualizerProcessor& p) : AudioProcessorEditor (&p), proc (p), panel (p, *this)
{
    setLookAndFeel (&panel.look);
    addAndMakeVisible (panel);
    constrainer.setFixedAspectRatio ((double) VisualizerProcessor::kBaseW / (double) VisualizerProcessor::kBaseH);
    constrainer.setMinimumSize (VisualizerProcessor::kBaseW * 6 / 10, VisualizerProcessor::kBaseH * 6 / 10);
    constrainer.setMaximumSize (VisualizerProcessor::kBaseW * 24 / 10, VisualizerProcessor::kBaseH * 24 / 10);
    setConstrainer (&constrainer);
    setResizable (true, true);                 // free resizing (host window frame or the corner handle), fixed aspect ratio
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    vzui::hostkeys::noKeyboardFocus (*this, &panel.popupOverlay());   // includes the resize corner
    addMouseListener (this, true);
    const int w = p.uiWidth.load(), h = p.uiHeight.load();
    if (w >= VisualizerProcessor::kBaseW * 6 / 10 && h >= VisualizerProcessor::kBaseH * 6 / 10) setSize (w, h); else setSize (VisualizerProcessor::kBaseW, VisualizerProcessor::kBaseH);
}

VisualizerEditor::~VisualizerEditor() { removeMouseListener (this); setLookAndFeel (nullptr); }

void VisualizerEditor::applySavedSize()
{
    const int w = proc.uiWidth.load(), h = proc.uiHeight.load();
    if (w >= VisualizerProcessor::kBaseW * 6 / 10 && h >= VisualizerProcessor::kBaseH * 6 / 10 && (w != getWidth() || h != getHeight())) setSize (w, h);
}

void VisualizerEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) VisualizerProcessor::kBaseW, (float) getHeight() / (float) VisualizerProcessor::kBaseH));
    panel.setBounds (0, 0, VisualizerProcessor::kBaseW, VisualizerProcessor::kBaseH);
    proc.uiWidth.store (getWidth()); proc.uiHeight.store (getHeight());
}
