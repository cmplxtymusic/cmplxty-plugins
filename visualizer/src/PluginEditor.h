#pragma once
#include "PluginProcessor.h"
#include "UpdateCheck.h"
#include "HostKeys.h"

// 1.0.1: when 1, a 10 Hz check hands the native keyboard focus back to the host if the plug-in ever ends up holding it (e.g. after a combo box popup).
// The real fix is that nothing in the editor wants keyboard focus; set to 0 to test that on its own.
#ifndef VZ_FOCUS_NET
 #define VZ_FOCUS_NET 1
#endif

namespace vzui
{
// Bundled UI fonts (Barlow Regular / SemiBold, SIL OFL 1.1): no dependence on whatever fonts the host machine has.
juce::Typeface::Ptr uiTypeface (bool bold);
inline juce::Font uiFont (float h, bool bold = false) { return juce::Font (juce::FontOptions (uiTypeface (bold)).withHeight (h * 1.14f)); }

// Slider that asks its owner for a context menu on right-click (Reset / Type in value / Copy / Paste / Init) and resets on double-click.
struct Knob : juce::Slider, private juce::Label::Listener
{
    Knob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); setTextBoxIsEditable (false); noFocus(); }
    // Typing a value is only offered from the right-click menu: a plain click on the value text must never open an editor (it grabbed the focus).
    void typeValue() { setTextBoxIsEditable (true); noFocus(); showTextBox(); }
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
    void editorHidden (juce::Label*, juce::TextEditor&) override { setTextBoxIsEditable (false); noFocus(); hostkeys::releaseLater (*this); }   // typed value done: Space goes to the host again
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
    void show (const vz::upd::Info&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String title;
    juce::TextButton update { "UPDATE" }, later { "LATER" }, skip { "SKIP THIS VERSION" };
};

// ---- layout description: every plug-in fills this in (Layout.h) and the generic panel builds the controls from it
struct CtlSpec { char kind; const char* id; const char* label; };   // kind: 'k' knob, 'c' combo, 't' toggle
// tab: -1 = always shown, else index into PanelSpec::tabs. sideCols > 0: no knob row, every control in a grid with that many columns.
struct SecSpec { const char* title; const char* powerId; juce::Rectangle<int> area; std::vector<CtlSpec> ctls; int columnW = 132; int tab = -1; int sideCols = 0; };
struct PanelSpec
{
    const char* title; const char* subtitle;
    juce::Rectangle<int> display;                 // area handed to the plug-in's own display component (empty = none)
    std::vector<SecSpec> secs;
    const char* demoKnob;                         // knob used by the screenshot "menu" mode and the audits
    std::vector<const char*> tabs {};             // optional tab bar (right column)
    juce::Rectangle<int> tabBar {};
};
const PanelSpec& panelSpec();
class Panel;
std::unique_ptr<juce::Component> makeDisplay (VisualizerProcessor&, Panel&);
void demoDisplay (juce::Component*);   // screenshot helper: fill the display with demo data
// Plug-in specific components placed on a tab (tab -1 = always visible), e.g. the preset bar, image strip, setlist.
struct TabExtra { int tab; juce::Rectangle<int> area; std::unique_ptr<juce::Component> comp; };
std::vector<TabExtra> makeTabExtras (VisualizerProcessor&, Panel&);

// The whole interface, laid out at a fixed base size and scaled by the editor (so the window can be resized freely).
class Panel final : public juce::Component, private juce::Timer
{
public:
    Panel (VisualizerProcessor&, juce::AudioProcessorEditor& owner);
    ~Panel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // hooks for the screenshot / test harness and for the plug-in's display
    void demoBanner (const vz::upd::Info& i) { showBanner (i); }
    void demoKnobMenu();
    juce::String auditKeyboard();
    juce::String auditMenus();   // right-click path with a fake host context menu / without one
    vzui::PopupOverlay& popupOverlay() { return popup; }
    void setNote (const juce::String& s) { footerNote = s; repaint (footerArea); }
    juce::Component* display() { return disp.get(); }
    void setTab (int t);
    int currentTab() const { return tab; }
    juce::AudioProcessorEditor& editor() { return owner; }
    void showParamMenu (const juce::String& paramId, vzui::Knob* knob, juce::Point<int> pos);   // right-click menu for a knob / combo / switch

    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool mouseOver, bool mouseDown) override;
        juce::Font getLabelFont (juce::Label& l) override { return vzui::uiFont (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr ? 12.5f : 10.5f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return vzui::uiFont (12.0f); }
        juce::Font getPopupMenuFont() override { return vzui::uiFont (11.5f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return vzui::uiFont (11.0f, true); }
    } look;

private:
    void timerCallback() override;
    struct KnobBox { vzui::Knob slider; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    struct Combo   { vzui::NoFocusCombo box; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att; };
    struct Tog     { vzui::Btn button; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    struct Section
    {
        const SecSpec* spec = nullptr;
        vzui::Btn power; juce::Label label;
        std::unique_ptr<juce::ParameterAttachment> att;
        std::vector<juce::Component*> members;
        bool on = true;
    };

    KnobBox& addKnob (const char* id, const char* name);
    Combo& addCombo (const char* id, const char* name);
    Tog& addTog (const char* id, const char* name);
    void addSection (Section&, const char* paramId);
    void applySectionState (Section&, bool on);
    KnobBox* knobFor (const juce::String& id);

    void resetAll();
    void showBanner (const vz::upd::Info&);
    void startUpdateCheck (bool force = false);
    void showUpdateMenu();
    juce::Rectangle<int> updateLinkBounds() const;
    juce::String paramIdOf (juce::Component*) const;

    VisualizerProcessor& proc;
    juce::AudioProcessorEditor& owner;
    std::unique_ptr<juce::Component> disp;
    std::vector<std::unique_ptr<KnobBox>> knobBoxes;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Tog>> togs;
    std::vector<std::unique_ptr<Section>> sections;
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" };
    juce::Rectangle<int> footerArea;
    vzui::PopupOverlay popup;
    vzui::UpdateBanner banner;
    vz::upd::Checker checker;
    vz::upd::Info bannerInfo;
    juce::String footerNote;
    bool updatesOn = true;
    int tab = 0;
    std::vector<TabExtra> extras;
    juce::Rectangle<int> tabBounds (int i) const;
    std::vector<std::pair<juce::Component*, juce::String>> paramOf;   // right-click target -> parameter id

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};
} // namespace vzui

class VisualizerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VisualizerEditor (VisualizerProcessor&);
    ~VisualizerEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    void applySavedSize();
    vzui::Panel& getPanel() { return panel; }
    // Sees every click in the editor (listener on all children): if the click left the native keyboard focus in the plug-in window, it goes straight
    // back to the host before the button-up is finished, so Space never lands in the plug-in.
    void mouseDown (const juce::MouseEvent&) override { handBack(); }
    void mouseUp (const juce::MouseEvent&) override   { handBack(); }

private:
    void handBack() { if (! panel.popupOverlay().isOpen()) vzui::hostkeys::release (*this); }
    VisualizerProcessor& proc;
    vzui::Panel panel;
    juce::ComponentBoundsConstrainer constrainer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerEditor)
};
