#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
const juce::Colour kBg (0xff0b0d17), kPanel (0xff141829), kCyan (0xff00e5ff), kMagenta (0xffff2bd6), kDim (0xff8a93b8);
constexpr int kFooterH = 22;

using gcui::uiFont;

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

namespace gcui
{
juce::Typeface::Ptr uiTypeface (bool bold)
{
    static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, (size_t) BinaryData::BarlowRegular_ttfSize);
    static const juce::Typeface::Ptr semi    = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, (size_t) BinaryData::BarlowSemiBold_ttfSize);
    return bold ? semi : regular;
}
} // namespace gcui

//==============================================================================
namespace gcui
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
    g.setColour (juce::Colour (0xff1a2038)); g.fillRoundedRectangle (panel.toFloat(), 6.0f);
    g.setColour (kCyan.withAlpha (0.7f)); g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 6.0f, 1.0f);
    const auto f = uiFont (13.0f);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto& it = items[(size_t) i]; auto r = rowBounds (i);
        if (it.separatorAbove) { g.setColour (kDim.withAlpha (0.35f)); g.fillRect (panel.getX() + 8, r.getY() - 4, panel.getWidth() - 16, 1); }
        if (i == hover && it.enabled) { g.setColour (kMagenta.darker (0.4f)); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
        g.setColour ((it.enabled ? juce::Colours::white : kDim.withAlpha (0.6f)));
        if (it.ticked)
        {
            juce::Path tick; auto t = juce::Rectangle<float> ((float) r.getX() + 6, (float) r.getCentreY() - 5, 10, 10);
            tick.startNewSubPath (t.getX(), t.getCentreY()); tick.lineTo (t.getX() + 3.5f, t.getBottom() - 1); tick.lineTo (t.getRight(), t.getY() + 1);
            g.setColour (kCyan); g.strokePath (tick, juce::PathStrokeType (2.0f)); g.setColour (juce::Colours::white);
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
void UpdateBanner::show (const gc::upd::Info& i)
{
    title = "v" + i.version + " AVAILABLE";
    setVisible (true); toFront (false); repaint();
}
void UpdateBanner::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1a2038)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kMagenta.withAlpha (0.85f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (kMagenta); g.fillEllipse (10.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
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
} // namespace gcui

//==============================================================================
GlitchCrusherEditor::Look::Look()
{
    setDefaultSansSerifTypeface (gcui::uiTypeface (false));
    setColour (juce::Slider::rotarySliderFillColourId, kCyan);
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff262c47));
    setColour (juce::Slider::thumbColourId, kMagenta);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kCyan.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, kCyan);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kMagenta.darker (0.4f));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262c47));
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void GlitchCrusherEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kCyan.withAlpha (0.20f) : juce::Colour (0xff262c47)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? kCyan : kDim.withAlpha (highlighted ? 0.9f : 0.6f)); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    const float d = r.getHeight() - 6.0f;
    const float cx = on ? r.getRight() - 3.0f - d : r.getX() + 3.0f;
    g.setColour (on ? kCyan : kDim); g.fillEllipse (cx, r.getY() + 3.0f, d, d);
    g.setFont (uiFont (9.0f, true)); g.setColour (on ? kCyan : kDim);
    g.drawText (on ? "ON" : "OFF", r.withTrimmedLeft (on ? 5.0f : d + 6.0f).withTrimmedRight (on ? d + 6.0f : 3.0f), juce::Justification::centred, false);
}

void GlitchCrusherEditor::Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (c.brighter (down ? 0.4f : highlighted ? 0.2f : 0.0f)); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (kCyan.withAlpha (0.55f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void GlitchCrusherEditor::addKnob (KnobBox& k, const char* id, const char* name)
{
    k.slider.setLookAndFeel (&look);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setLookAndFeel (&look);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    // double-click resets to the parameter's default (host-visible change, one undo-able gesture)
    auto* prm = proc.apvts.getParameter (id);
    k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    paramOf.push_back ({ &k.slider, id });
    k.slider.onContextMenu = [this] (gcui::Knob& kn, juce::Point<int> screenPos) { showParamMenu (kn, getLocalPoint (nullptr, screenPos)); };
}

void GlitchCrusherEditor::addCombo (Combo& c, const char* id, const char* name)
{
    c.box.setLookAndFeel (&look);
    c.box.addItemList (proc.apvts.getParameter (id)->getAllValueStrings(), 1);
    addAndMakeVisible (c.box);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setLookAndFeel (&look);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c.box);
    paramOf.push_back ({ &c.box, id }); c.box.addMouseListener (this, true);
}

void GlitchCrusherEditor::applySectionState (Section& s, bool on)
{
    s.on = on;
    s.power.setToggleState (on, juce::dontSendNotification);
    for (auto* c : s.members) c->setAlpha (on ? 1.0f : 0.4f);
    repaint();
}

void GlitchCrusherEditor::addSection (Section& s, const char* paramId, const char*)
{
    s.power.setLookAndFeel (&look);
    s.power.setClickingTogglesState (false);
    addAndMakeVisible (s.power);
    auto* prm = proc.apvts.getParameter (paramId);
    s.att = std::make_unique<juce::ParameterAttachment> (*prm, [this, &s] (float v) { applySectionState (s, v > 0.5f); }, nullptr);
    s.power.onClick = [&s] { s.att->setValueAsCompleteGesture (s.on ? 0.0f : 1.0f); };
    paramOf.push_back ({ &s.power, paramId }); s.power.addMouseListener (this, false);
    s.att->sendInitialUpdate();
}

GlitchCrusherEditor::GlitchCrusherEditor (GlitchCrusherProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob (bits, "bits", "BIT DEPTH");  addKnob (down, "downsample", "DOWNSAMPLE"); addKnob (drive, "drive", "DRIVE");
    addKnob (prob, "glitchProb", "PROBABILITY");
    addKnob (cutoff, "cutoff", "CUTOFF");  addKnob (res, "resonance", "RESONANCE");
    addKnob (mix, "mix", "MIX");           addKnob (outGain, "outGain", "OUTPUT");
    addCombo (rate, "glitchRate", "SLICE");  addCombo (rev, "reverse", "REVERSE");  addCombo (ftype, "filterType", "FILTER");

    crushSec.members  = { &bits.slider, &bits.label, &down.slider, &down.label, &drive.slider, &drive.label };
    glitchSec.members = { &prob.slider, &prob.label, &rate.box, &rate.label, &rev.box, &rev.label };
    filterSec.members = { &ftype.box, &ftype.label, &cutoff.slider, &cutoff.label, &res.slider, &res.label };
    addSection (crushSec, "crushOn", "Crush"); addSection (glitchSec, "glitchOn", "Glitch"); addSection (filterSec, "filterOn", "Filter");

    addChildComponent (banner);
    banner.setLookAndFeel (&look);
    banner.onUpdate = [this]
    {
        const auto page = gc::upd::pageForUrl (bannerInfo.url);
        if (gc::upd::isTrustedUrl (page)) juce::URL (page).launchInDefaultBrowser();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    banner.onLater = [this] { banner.setVisible (false); footerNote = {}; repaint(); };
    banner.onSkip = [this]
    {
        auto s = gc::upd::Settings::load(); s.skipVersion = bannerInfo.version; s.save();
        banner.setVisible (false); footerNote = {}; repaint();
    };
    addChildComponent (popup);

    updatesOn = gc::upd::Settings::load().enabled;
    setWantsKeyboardFocus (false);            // never hold keyboard focus: Space & co. must keep driving the host transport
    setMouseClickGrabsKeyboardFocus (false);
    for (auto* b : { &banner.update, &banner.later, &banner.skip }) { b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
    // belt and braces: no child (incl. the sliders' value labels) may keep keyboard focus - only the popup while open, and text editors
    gcui::hostkeys::noKeyboardFocus (*this, &popup);
    popup.setWantsKeyboardFocus (false);
    startTimerHz (10);                        // remembers the host's focused window; safety net, see timerCallback
    setSize (720, 384);
    // update check: after the editor is up, on a background thread, at most once per 24 h, silent on failure
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GlitchCrusherEditor> (this)] { if (sp != nullptr) sp->startUpdateCheck(); });
}

GlitchCrusherEditor::~GlitchCrusherEditor()
{
    stopTimer();
    checker.cancelAndWait();
    for (auto* k : { &bits, &down, &drive, &prob, &cutoff, &res, &mix, &outGain })
    { k->slider.setLookAndFeel (nullptr); k->label.setLookAndFeel (nullptr); }
    for (auto* c : { &rate, &rev, &ftype }) { c->box.setLookAndFeel (nullptr); c->label.setLookAndFeel (nullptr); }
    for (auto* s : { &crushSec, &glitchSec, &filterSec }) s->power.setLookAndFeel (nullptr);
    banner.setLookAndFeel (nullptr);
}

// Only acts when the plug-in window really holds the native keyboard focus and nothing needs it (no popup, no value being typed, no combo menu):
// then the focus goes back to the host. Normally it never fires, because nothing in the editor asks for focus.
void GlitchCrusherEditor::timerCallback()
{
    gcui::hostkeys::remember (*this);
   #if GC_FOCUS_NET
    if (! popup.isOpen()) gcui::hostkeys::release (*this);
   #endif
}

//==============================================================================
void GlitchCrusherEditor::startUpdateCheck (bool force)
{
    if (! force && ! gc::upd::Settings::load().enabled) return;
    checker.start ([this, force] (const gc::upd::Result& r)
    {
        if (r.ok && gc::upd::shouldNotify (r, gc::upd::Settings::load(), force)) showBanner (r.info);
        else if (force) { footerNote = r.ok ? juce::String ("Glitch Crusher ") + GC_VERSION_STRING + " is up to date" : juce::String ("Could not reach the update server"); repaint(); }
    }, force);
}

void GlitchCrusherEditor::showBanner (const gc::upd::Info& i)
{
    bannerInfo = i;
    banner.show (i);
    footerNote = i.notes.isNotEmpty() ? "What's new: " + i.notes : juce::String();
    resized(); repaint();
}

juce::Rectangle<int> GlitchCrusherEditor::updateLinkBounds() const { return footerArea.withTrimmedLeft (footerArea.getWidth() - 190).withTrimmedRight (14).reduced (0, 2); }

void GlitchCrusherEditor::showUpdateMenu()
{
    const bool on = gc::upd::Settings::load().enabled;
    std::vector<gcui::PopupOverlay::Item> v;
    v.push_back ({ "Check for &updates automatically", [this, on]
                   { auto s = gc::upd::Settings::load(); s.enabled = ! on; s.save(); updatesOn = s.enabled;
                     footerNote = s.enabled ? "Update check on (once a day, nothing is sent about you)" : "Update check off: the plugin never contacts the network"; repaint(); }, true, on });
    v.push_back ({ "Check &now", [this] { footerNote = "Checking..."; repaint(); startUpdateCheck (true); }, true, false });
    auto b = updateLinkBounds();
    popup.show ({ b.getRight() - 230, b.getY() - 70 }, std::move (v));
}

void GlitchCrusherEditor::resetAll()
{
    for (auto* prm : proc.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
        }
}

juce::String GlitchCrusherEditor::paramIdOf (juce::Component* c) const
{
    for (; c != nullptr && c != this; c = c->getParentComponent())
        for (auto& pr : paramOf) if (pr.first == c) return pr.second;
    return {};
}

// Right-click menu: our own items (with underlined shortcut letters) plus the host's own parameter menu when the host
// offers one (VST3 IComponentHandler3 -> automation lane, MIDI learn, ...). Without a host menu there is an "Automate" item
// that touches the parameter so the host registers it as the last-touched / automatable one.
void GlitchCrusherEditor::showParamMenu (juce::Component& source, juce::Point<int> pos)
{
    const auto id = paramIdOf (&source);
    auto* prm = id.isNotEmpty() ? proc.apvts.getParameter (id) : nullptr;
    juce::Component::SafePointer<gcui::Knob> knob (dynamic_cast<gcui::Knob*> (&source));
    std::vector<gcui::PopupOverlay::Item> v;
    if (prm != nullptr)
    {
        v.push_back ({ knob != nullptr ? "&Reset to default\tdouble-click" : "&Reset to default", [prm]
                       { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); } });
        if (knob != nullptr)
        {
            v.push_back ({ "&Type in value...", [knob] { if (knob != nullptr) knob->showTextBox(); } });
            v.push_back ({ "&Copy value", [knob] { if (knob != nullptr) juce::SystemClipboard::copyTextToClipboard (knob->getTextFromValue (knob->getValue())); }, true, false, true });
            const auto clip = juce::SystemClipboard::getTextFromClipboard().trim();
            const bool pasteOk = clip.isNotEmpty() && clip.length() < 24 && clip.containsAnyOf ("0123456789");
            v.push_back ({ "&Paste value", [knob, clip] { if (knob != nullptr) knob->setValue (knob->getValueFromText (clip), juce::sendNotificationSync); }, pasteOk });
        }
        std::shared_ptr<juce::HostProvidedContextMenu> host;
        if (auto* hc = getHostContext())
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

void GlitchCrusherEditor::demoKnobMenu() { showParamMenu (cutoff.slider, cutoff.slider.getBounds().getCentre()); }

void GlitchCrusherEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (auto* c = e.eventComponent; c != nullptr && c != this && paramIdOf (c).isNotEmpty())
        { showParamMenu (*c, getLocalPoint (nullptr, e.getScreenPosition())); return; }
    }
    else if (updateLinkBounds().contains (e.getPosition())) showUpdateMenu();
}

// Walks every component: nothing except the open popup / a text editor may want keyboard focus; unhandled keys must not be consumed.
juce::String GlitchCrusherEditor::auditKeyboard()
{
    juce::String out; int n = 0, bad = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        ++n;
        if (c.getWantsKeyboardFocus() && &c != &popup && dynamic_cast<juce::TextEditor*> (&c) == nullptr)
        { ++bad; out << "  wants focus: " << c.getName() << " (" << typeid (c).name() << ")\n"; }
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    // Slider turns focus back on for its value box on look-and-feel / enablement changes: provoke both and make sure the knobs fix it up again
    for (auto* ks : { &bits, &down, &drive, &prob, &cutoff, &res, &mix, &outGain }) { ks->slider.setEnabled (false); ks->slider.setEnabled (true); ks->slider.sendLookAndFeelChange(); }
    // negative control: a stock juce::Slider really does ask for keyboard focus (so the walk below could fail)
    {
        juce::Slider stock (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        stock.setBounds (0, 0, 80, 80); int stockWants = 0;
        for (auto* c : stock.getChildren()) stockWants += c->getWantsKeyboardFocus() ? 1 : 0;
        out << "  negative control: stock slider's value box wants focus=" << stockWants << " (expected 1)\n";
        if (stockWants != 1) ++bad;
    }
    walk (*this);
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
        << "  editor wantsFocus=" << (int) getWantsKeyboardFocus() << " editor keyPressed(Space)=" << (int) keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) << " (all expected 0)\n";
    const bool ok = bad == 0 && ! closedWants && openWants && ! spaceUsed && ! zUsed && rUsed && ! popup.getWantsKeyboardFocus() && ! afterSpace && ! getWantsKeyboardFocus();
    out << (ok ? "KEYBOARD AUDIT OK\n" : "KEYBOARD AUDIT FAILED\n");
    return out;
}

//==============================================================================
void GlitchCrusherEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kCyan);
    g.setFont (uiFont (22.0f, true));
    g.drawText ("CMPLXTY // GLITCH CRUSHER", 16, 8, 330, 30, juce::Justification::centredLeft);
    g.setColour (kMagenta);
    g.fillRect (16, 40, 120, 2);
    auto panel = [&] (juce::Rectangle<int> r, const char* t, bool on)
    {
        g.setColour (kPanel);          g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (kCyan.withAlpha (on ? 0.35f : 0.15f)); g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.0f);
        g.setColour (on ? kDim : kDim.withAlpha (0.5f)); g.setFont (uiFont (12.0f, true));
        g.drawText (t, r.getX() + 10, r.getY() + 4, 100, 16, juce::Justification::centredLeft);
    };
    panel (crushArea, "CRUSH", crushSec.on); panel (glitchArea, "GLITCH", glitchSec.on); panel (filterArea, "FILTER", filterSec.on); panel (outArea, "OUT", true);

    // footer: version, notes, update switch
    g.setFont (uiFont (11.0f));
    g.setColour (kDim.withAlpha (0.8f));
    g.drawText (juce::String ("v") + GC_VERSION_STRING + "  -  free, GPLv3  -  cmplxty.music", footerArea.getX() + 16, footerArea.getY(), 230, footerArea.getHeight(), juce::Justification::centredLeft);
    g.setColour (footerNote.isNotEmpty() ? kMagenta.withAlpha (0.9f) : kDim);
    g.drawFittedText (footerNote, footerArea.getX() + 250, footerArea.getY(), footerArea.getWidth() - 250 - 210, footerArea.getHeight(), juce::Justification::centredLeft, 1);
    auto ul = updateLinkBounds();
    g.setColour (updatesOn ? kCyan.withAlpha (0.85f) : kDim);
    g.drawText (updatesOn ? "UPDATE CHECK: ON" : "UPDATE CHECK: OFF", ul, juce::Justification::centredRight);
}

void GlitchCrusherEditor::resized()
{
    auto full = getLocalBounds();
    footerArea = full.removeFromBottom (kFooterH);
    auto area = full.reduced (12);
    area.removeFromTop (38);
    area.removeFromBottom (0);
    auto top = area.removeFromTop (area.getHeight() / 2 + 5).withTrimmedBottom (6);
    auto bot = area.withTrimmedTop (0);

    crushArea  = top.removeFromLeft (top.getWidth() * 3 / 5).withTrimmedRight (6);
    glitchArea = top;
    filterArea = bot.removeFromLeft (bot.getWidth() * 3 / 5).withTrimmedRight (6);
    outArea    = bot;

    auto knobRow = [] (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, std::initializer_list<juce::Component*> labels)
    {
        r.removeFromTop (18);
        const int w = r.getWidth() / (int) comps.size(); int x = r.getX();
        auto li = labels.begin();
        for (auto* c : comps) { c->setBounds (x, r.getY() + 14, w, r.getHeight() - 14); (*li++)->setBounds (x, r.getY() - 2, w, 16); x += w; }
    };
    knobRow (crushArea, { &bits.slider, &down.slider, &drive.slider }, { &bits.label, &down.label, &drive.label });

    { // glitch: knob + 2 combos
        auto r = glitchArea.reduced (8); r.removeFromTop (14);
        auto kn = r.removeFromLeft (r.getWidth() / 2);
        prob.label.setBounds (kn.getX(), kn.getY(), kn.getWidth(), 16); prob.slider.setBounds (kn.getX(), kn.getY() + 14, kn.getWidth(), kn.getHeight() - 14);
        r.reduce (4, 6);
        rate.label.setBounds (r.removeFromTop (16)); rate.box.setBounds (r.removeFromTop (26)); r.removeFromTop (8);
        rev.label.setBounds (r.removeFromTop (16)); rev.box.setBounds (r.removeFromTop (26));
    }
    { // filter: combo + 2 knobs
        auto r = filterArea.reduced (8); r.removeFromTop (14);
        auto cb = r.removeFromLeft (r.getWidth() / 4).reduced (0, 6);
        ftype.label.setBounds (cb.removeFromTop (16)); ftype.box.setBounds (cb.removeFromTop (26));
        auto k1 = r.removeFromLeft (r.getWidth() / 2);
        cutoff.label.setBounds (k1.getX(), k1.getY(), k1.getWidth(), 16); cutoff.slider.setBounds (k1.getX(), k1.getY() + 14, k1.getWidth(), k1.getHeight() - 14);
        res.label.setBounds (r.getX(), r.getY(), r.getWidth(), 16); res.slider.setBounds (r.getX(), r.getY() + 14, r.getWidth(), r.getHeight() - 14);
    }
    knobRow (outArea, { &mix.slider, &outGain.slider }, { &mix.label, &outGain.label });

    for (auto [sec, area2] : { std::pair<Section*, juce::Rectangle<int>> { &crushSec, crushArea }, { &glitchSec, glitchArea }, { &filterSec, filterArea } })
        sec->power.setBounds (area2.getRight() - 50, area2.getY() + 4, 42, 16);

    banner.setBounds (getWidth() - 12 - 392, 9, 392, 28);
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

juce::String GlitchCrusherEditor::auditMenus()
{
    juce::String out; bool ok = true;
    auto check = [&] (bool cond, const juce::String& what) { out << (cond ? "  ok   " : "  FAIL ") << what << "\n"; ok = ok && cond; };
    auto rightClick = [this] (juce::Component& c)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e (src, juce::Point<float> (4.0f, 4.0f), juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, now, juce::Point<float> (4.0f, 4.0f), now, 1, false);
        c.mouseDown (e);                         // the component's own handler (Knob menu hook / Btn ignores right-click) ...
        for (auto* p = &c; p != nullptr && p != this; p = p->getParentComponent()) {}
        mouseDown (e);                            // ... and the editor's MouseListener path used for combos / switches
        c.mouseUp (e);
    };
    auto* pw = proc.apvts.getParameter ("filterOn");
    GestureCounter gc;

    // 1) no host menu: custom menu, with an Automate item
    popup.dismiss();
    rightClick (cutoff.slider);
    auto t = popup.itemTexts();
    out << "  knob menu (no host): " << t.joinIntoString (" | ") << "\n";
    check (popup.isOpen() && t.size() >= 5, "right-click on a knob opens the menu");
    check (t.contains ("&Reset to default\tdouble-click") && t.joinIntoString ("|").contains ("&Automate"), "menu has Reset and Automate with shortcut letters");
    auto* cutP = proc.apvts.getParameter ("cutoff"); cutP->addListener (&gc);
    check (popup.keyPressed (juce::KeyPress ('a', juce::ModifierKeys(), 'a')) && ! popup.isOpen() && gc.begins == 1 && gc.ends == 1, "pressing A runs Automate: touches the parameter (begin/end gesture), closes menu");
    cutP->removeListener (&gc);

    // 2) switch right-click: menu opens, state unchanged, host item present
    const float before = pw->getValue();
    rightClick (filterSec.power);
    check (popup.isOpen() && pw->getValue() == before, "right-click on a section switch opens the menu and does NOT toggle it");
    t = popup.itemTexts(); out << "  switch menu: " << t.joinIntoString (" | ") << "\n";
    popup.dismiss();
    rightClick (rev.box);
    check (popup.isOpen() && ! popup.itemTexts().contains ("&Type in value..."), "right-click on a combo opens the (no 'type in') menu");
    popup.dismiss();

    // 3) with a host-provided menu
    FakeHost host; setHostContext (&host);
    rightClick (cutoff.slider);
    t = popup.itemTexts(); out << "  knob menu (host): " << t.joinIntoString (" | ") << "\n";
    check (host.asked >= 1, "asked host context for the parameter menu");
    check (t.joinIntoString ("|").contains ("Show automation lane") && t.joinIntoString ("|").contains ("Host > Create automation clip") && ! t.joinIntoString ("|").contains ("&Automate"),
           "host items shown (submenu flattened), custom Automate replaced");
    check (popup.activate ("Show automation lane") && *host.hits == 1, "clicking a host item runs the host's own action");
    rightClick (cutoff.slider); popup.activate ("Create automation clip");
    check (*host.hits == 11, "submenu host item runs too");
    rightClick (filterSec.power);
    check (popup.isOpen() && popup.itemTexts().joinIntoString ("|").contains ("Show automation lane"), "host menu also offered on section switch");
    popup.dismiss();
    setHostContext (nullptr);
    out << (ok ? "MENU AUDIT OK\n" : "MENU AUDIT FAILED\n");
    return out;
}
