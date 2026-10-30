#pragma once
#include "PluginProcessor.h"
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "HostKeys.h"

namespace gwui
{
// Bundled UI fonts (Barlow Regular / SemiBold, SIL OFL 1.1): no dependence on whatever fonts the host machine has.
juce::Typeface::Ptr uiTypeface (bool bold);
inline juce::Font uiFont (float h, bool bold = false) { return juce::Font (juce::FontOptions (uiTypeface (bold)).withHeight (h * 1.14f)); }

// Slider that asks its owner for a context menu on right-click (Reset / Type in value / Copy / Paste / Init) and resets on double-click.
struct Knob : juce::Slider, private juce::Label::Listener
{
    Knob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); noFocus(); }
    std::function<void (Knob&, juce::Point<int> screenPos)> onContextMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu) { onContextMenu (*this, e.getScreenPosition()); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }

    // The value text box is a juce::Label. Slider switches "wants keyboard focus" back ON for it (it is click-to-type) every time the look-and-feel
    // or the enabled state changes, and a click anywhere near it then moved the native keyboard focus into the plug-in window (Space stopped
    // reaching the DAW). So after each of those changes: no focus for it, and watch it so the focus goes back to the host when typing ends.
    void lookAndFeelChanged() override { juce::Slider::lookAndFeelChanged(); noFocus(); }
    void enablementChanged() override  { juce::Slider::enablementChanged(); noFocus(); }

private:
    void noFocus()
    {
        for (auto* c : getChildren())
        {
            c->setWantsKeyboardFocus (false);
            c->setMouseClickGrabsKeyboardFocus (false);
            if (auto* l = dynamic_cast<juce::Label*> (c)) { l->removeListener (this); l->addListener (this); }
        }
    }
    void labelTextChanged (juce::Label*) override {}
    void editorHidden (juce::Label*, juce::TextEditor&) override { hostkeys::releaseLater (*this); }   // typed value done: Space goes to the host again
};

// Toggle button for the plug-in UI: never takes keyboard focus (so Space and the other transport keys always reach the host),
// and a right-click only opens the parameter menu - it must not flip the switch.
struct Btn : juce::ToggleButton
{
    Btn() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseUp (e); }
};

struct NoFocusCombo : juce::ComboBox
{
    NoFocusCombo() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
};

// Small in-editor popup menu with underlined shortcut letters ("&Reset" -> R is underlined and pressing R runs it).
class PopupOverlay : public juce::Component
{
public:
    struct Item { juce::String text; std::function<void()> action; bool enabled = true, ticked = false, separatorAbove = false; };
    PopupOverlay();
    void show (juce::Point<int> where, std::vector<Item> newItems);
    void dismiss();
    bool isOpen() const { return open; }
    juce::StringArray itemTexts() const { juce::StringArray s; for (auto& i : items) s.add (i.text); return s; }   // test hook
    bool activate (const juce::String& textPart) { for (size_t i = 0; i < items.size(); ++i) if (items[i].enabled && items[i].text.contains (textPart)) { trigger ((int) i); return true; } return false; }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;   // handles only its own shortcut letters / arrows / Enter / Esc; everything else (Space ...) returns false -> host
    void focusLost (FocusChangeType) override {}
    bool hitTest (int, int) override { return open; }

private:
    int itemAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int i) const;
    void trigger (int i);
    std::vector<Item> items;
    juce::Rectangle<int> panel;
    int hover = -1;
    bool open = false;
};

class UpdateBanner : public juce::Component
{
public:
    std::function<void()> onUpdate, onLater, onSkip;
    UpdateBanner();
    void show (const gw::upd::Info&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String title;
    juce::TextButton update { "UPDATE" }, later { "LATER" }, skip { "SKIP THIS VERSION" };
};

// Formant display: the three vowel band-pass resonances on a log frequency axis, the clean low band shaded, live vowel position.
class VowelView final : public juce::Component, private juce::Timer
{
public:
    explicit VowelView (GrowlProcessor&);
    ~VowelView() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
private:
    void timerCallback() override;
    float raw (const char* id) const;
    GrowlProcessor& proc;
    float cached[6] {};
    bool dimmed = false;
};
} // namespace gwui

class GrowlEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GrowlEditor (GrowlProcessor&);
    ~GrowlEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    static constexpr int kBaseW = 840, kBaseH = 540;    // layout units; the window scales them (resizable, fixed aspect)
    float uiScale() const { return juce::jmax (0.1f, (float) getWidth() / (float) kBaseW); }
    juce::Rectangle<int> baseBounds() const { return { 0, 0, kBaseW, kBaseH }; }
    juce::Point<int> toBase (juce::Point<int> p) const { const float s = uiScale(); return { juce::roundToInt ((float) p.x / s), juce::roundToInt ((float) p.y / s) }; }

    // hooks for the screenshot / test harness
    void demoBanner (const gw::upd::Info& i) { showBanner (i); }
    void demoKnobMenu();
    // test hooks (GrowlScreenshot keys): focus / key-forwarding audit
    juce::String auditKeyboard();
    juce::String auditMenus();   // right-click path with a fake host context menu / without one

private:
    struct KnobBox
    {
        gwui::Knob slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        gwui::NoFocusCombo box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Tog
    {
        gwui::Btn button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Section
    {
        gwui::Btn power;
        std::unique_ptr<juce::ParameterAttachment> att;
        std::vector<juce::Component*> members;
        bool on = true;
    };
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
        juce::Font getLabelFont (juce::Label& l) override { return gwui::uiFont (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr ? 12.5f : 10.5f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return gwui::uiFont (12.0f); }
        juce::Font getPopupMenuFont() override { return gwui::uiFont (11.5f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return gwui::uiFont (11.0f, true); }
    } look;

    void addKnob (KnobBox&, const char* id, const char* name);
    void addCombo (Combo&, const char* id, const char* name);
    void addTog (Tog&, const char* id, const char* name);
    void addSection (Section&, const char* paramId);
    void applySectionState (Section&, bool on);
    void showParamMenu (juce::Component& source, juce::Point<int> pos);     // right-click menu for a knob / combo / switch
    void registerParam (juce::Component&, const char* paramId);

    void resetAll();
    void showBanner (const gw::upd::Info&);
    void startUpdateCheck (bool force = false);
    void showUpdateMenu();
    juce::Rectangle<int> updateLinkBounds() const;

    GrowlProcessor& proc;
    KnobBox inGain, xover, subLevel, drive, bias, vowel, shift, focus, fAmt, lfoDepth, postCut, postRes, mix, outGain;
    Combo satType, os, lfoRate, lfoShape, postType;
    Tog subMono, limiter;
    Section driveSec, formSec, postSec;
    gwui::VowelView vowelView { proc };
    juce::Rectangle<int> inArea, driveArea, formArea, lfoArea, postArea, outArea, footerArea;
    gwui::PopupOverlay popup;
    pk::PresetBar presetBar { proc.presets };
    pk::PresetMenu presetMenu;
public:
    pk::PresetBar& getPresetBar() { return presetBar; }
    pk::PresetMenu& getPresetMenu() { return presetMenu; }
private:
    gwui::UpdateBanner banner;
    gw::upd::Checker checker;
    gw::upd::Info bannerInfo;
    juce::String footerNote;
    bool updatesOn = true;
    std::vector<std::pair<juce::Component*, juce::String>> paramOf;   // right-click target -> parameter id
    juce::String paramIdOf (juce::Component*) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrowlEditor)
};
