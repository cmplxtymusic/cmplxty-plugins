#pragma once
#include "PluginProcessor.h"
#include "UpdateCheck.h"

namespace gcui
{
// Bundled UI fonts (Barlow Regular / SemiBold, SIL OFL 1.1): no dependence on whatever fonts the host machine has.
juce::Typeface::Ptr uiTypeface (bool bold);
inline juce::Font uiFont (float h, bool bold = false) { return juce::Font (juce::FontOptions (uiTypeface (bold)).withHeight (h * 1.14f)); }

// Slider that asks its owner for a context menu on right-click (Reset / Type in value / Copy / Paste / Init) and resets on double-click.
struct Knob : juce::Slider
{
    Knob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    std::function<void (Knob&, juce::Point<int> screenPos)> onContextMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu) { onContextMenu (*this, e.getScreenPosition()); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
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
    bool keyPressed (const juce::KeyPress&) override;   // only its own shortcut letters / arrows / Enter / Esc; everything else (Space ...) returns false -> host
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
    void show (const gc::upd::Info&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String title;
    juce::TextButton update { "UPDATE" }, later { "LATER" }, skip { "SKIP THIS VERSION" };
};
} // namespace gcui

class GlitchCrusherEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GlitchCrusherEditor (GlitchCrusherProcessor&);
    ~GlitchCrusherEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // hooks for the screenshot / test harness
    void demoBanner (const gc::upd::Info& i) { showBanner (i); }
    void demoKnobMenu();
    juce::String auditKeyboard();   // test hooks: focus / key forwarding
    juce::String auditMenus();      // right-click path with a fake host context menu / without one

private:
    struct KnobBox
    {
        gcui::Knob slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        gcui::NoFocusCombo box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Section
    {
        gcui::Btn power;
        std::unique_ptr<juce::ParameterAttachment> att;
        std::vector<juce::Component*> members;
        bool on = true;
    };
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        juce::Font getLabelFont (juce::Label& l) override { return gcui::uiFont (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr ? 12.5f : 10.5f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return gcui::uiFont (12.0f); }
        juce::Font getPopupMenuFont() override { return gcui::uiFont (11.5f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return gcui::uiFont (11.0f, true); }
    } look;

    void addKnob (KnobBox&, const char* id, const char* name);
    void addCombo (Combo&, const char* id, const char* name);
    void addSection (Section&, const char* paramId, const char* tip);
    void applySectionState (Section&, bool on);
    void showParamMenu (juce::Component& source, juce::Point<int> pos);   // right-click menu for a knob / combo / switch
    juce::String paramIdOf (juce::Component*) const;
    void resetAll();
    void showBanner (const gc::upd::Info&);
    void startUpdateCheck (bool force = false);
    void showUpdateMenu();
    juce::Rectangle<int> updateLinkBounds() const;

    GlitchCrusherProcessor& proc;
    KnobBox bits, down, drive, prob, cutoff, res, mix, outGain;
    Combo rate, rev, ftype;
    Section crushSec, glitchSec, filterSec;
    juce::Rectangle<int> crushArea, glitchArea, filterArea, outArea, footerArea;
    gcui::PopupOverlay popup;
    gcui::UpdateBanner banner;
    gc::upd::Checker checker;
    gc::upd::Info bannerInfo;
    juce::String footerNote;
    bool updatesOn = true;
    std::vector<std::pair<juce::Component*, juce::String>> paramOf;   // right-click target -> parameter id

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchCrusherEditor)
};
