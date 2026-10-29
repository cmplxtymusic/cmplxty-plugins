#pragma once
#include "PluginProcessor.h"
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "HostKeys.h"

// 1.0.1: when 1, a 10 Hz check hands the native keyboard focus back to the host if the plug-in ever ends up holding it (e.g. after a combo box popup).
// The real fix is that nothing in the editor wants keyboard focus; set to 0 to test that on its own.
#ifndef DF_FOCUS_NET
 #define DF_FOCUS_NET 1
#endif

namespace dfui
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
    void show (const df::upd::Info&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String title;
    juce::TextButton update { "UPDATE" }, later { "LATER" }, skip { "SKIP THIS VERSION" };
};

// Spectrum display: input (dim) and processed (bright) on a log frequency axis, the three bands shaded, draggable crossovers.
class SpectrumView final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (DriftProcessor&);
    ~SpectrumView() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hot = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override { return hot >= 0 || drag >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor; }
    std::function<void (const juce::String& paramId, juce::Point<int> screenPos)> onMenu;   // right-click on a crossover handle
    bool ecoMode() const { return eco; }
    juce::Rectangle<float> plotArea() const;
    float xOfHz (float hz) const;
    float hzOfX (float x) const;
private:
    void timerCallback() override;
    float raw (const char* id) const;
    int handleAt (float x) const;       // 0 = low crossover, 1 = high crossover, -1 = none
    void setCrossover (int which, float hz);
    DriftProcessor& proc;
    df::SpectrumAnalyzer an;
    std::vector<float> inDb, outDb, inShown, outShown, tmp;
    std::vector<float> samples;
    int order = 12;
    bool eco = false;
    float cache[12] {};
    int hot = -1, drag = -1;
};

// The whole interface, laid out at a fixed 900 x 590 and scaled by the editor (so the window can be resized freely).
class DriftPanel final : public juce::Component, private juce::Timer
{
public:
    DriftPanel (DriftProcessor&, juce::AudioProcessorEditor& owner);
    ~DriftPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // hooks for the screenshot / test harness
    void demoBanner (const df::upd::Info& i) { showBanner (i); }
    void demoKnobMenu();
    juce::String auditKeyboard();
    juce::String auditMenus();   // right-click path with a fake host context menu / without one
    dfui::PopupOverlay& popupOverlay() { return popup; }

    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool mouseOver, bool mouseDown) override;
        juce::Font getLabelFont (juce::Label& l) override { return dfui::uiFont (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr ? 12.5f : 10.5f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return dfui::uiFont (12.0f); }
        juce::Font getPopupMenuFont() override { return dfui::uiFont (11.5f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return dfui::uiFont (11.0f, true); }
    } look;

private:
    void timerCallback() override;
    struct KnobBox
    {
        dfui::Knob slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        dfui::NoFocusCombo box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Tog
    {
        dfui::Btn button;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Section
    {
        dfui::Btn power;
        juce::Label label;
        std::unique_ptr<juce::ParameterAttachment> att;
        std::vector<juce::Component*> members;
        bool on = true;
    };

    void addKnob (KnobBox&, const char* id, const char* name);
    void addCombo (Combo&, const char* id, const char* name);
    void addTog (Tog&, const char* id, const char* name);
    void addSection (Section&, const char* paramId, const char* name);
    void applySectionState (Section&, bool on);
    void showParamMenu (const juce::String& paramId, dfui::Knob* knob, juce::Point<int> pos);   // right-click menu for a knob / combo / switch / handle

    void resetAll();
    void showBanner (const df::upd::Info&);
    void startUpdateCheck (bool force = false);
    void showUpdateMenu();
    juce::Rectangle<int> updateLinkBounds() const;
    juce::String paramIdOf (juce::Component*) const;
    std::vector<KnobBox*> knobs() { return { &coarse, &fine, &spread, &mix, &feedback, &keyAmt, &xoLow, &xoHigh }; }

    DriftProcessor& proc;
    juce::AudioProcessorEditor& owner;
    dfui::SpectrumView spec { proc };
    KnobBox coarse, fine, spread, mix, feedback, keyAmt, xoLow, xoHigh;
    Combo slope;
    Tog lowBand, highBand, soloLow, soloMid, soloHigh, eco;
    Section shiftSec, wideSec, keySec;
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" };
    juce::Rectangle<int> bandArea[3], shiftArea, stereoArea, outArea, keyArea, footerArea;
    dfui::PopupOverlay popup;
    pk::PresetBar presetBar { proc.presets };
    pk::PresetMenu presetMenu;
public:
    pk::PresetBar& getPresetBar() { return presetBar; }
    pk::PresetMenu& getPresetMenu() { return presetMenu; }
private:
    dfui::UpdateBanner banner;
    df::upd::Checker checker;
    df::upd::Info bannerInfo;
    juce::String footerNote, noteText;
    int shownNote = -2;
    bool updatesOn = true;
    std::vector<std::pair<juce::Component*, juce::String>> paramOf;   // right-click target -> parameter id

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftPanel)
};
} // namespace dfui

class DriftEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DriftEditor (DriftProcessor&);
    ~DriftEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    void applySavedSize();
    dfui::DriftPanel& getPanel() { return panel; }
    // Sees every click in the editor (listener on all children): if the click left the native keyboard focus in the plug-in window, it goes straight
    // back to the host before the button-up is finished, so Space never lands in the plug-in, not even for the 100 ms until the safety-net timer.
    void mouseDown (const juce::MouseEvent&) override { handBack(); }
    void mouseUp (const juce::MouseEvent&) override   { handBack(); }

private:
    void handBack() { if (! panel.popupOverlay().isOpen()) dfui::hostkeys::release (*this); }
    DriftProcessor& proc;
    dfui::DriftPanel panel;
    juce::ComponentBoundsConstrainer constrainer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftEditor)
};
