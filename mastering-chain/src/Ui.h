#pragma once
#include "HostKeys.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

// Shared UI pieces (same behaviour as the other CMPLXTY plugins): knobs and buttons that never take keyboard focus,
// so the host's own keys (Space = play / pause) keep working; a right-click popup menu with shortcut letters.
namespace gzui
{
juce::Typeface::Ptr uiTypeface(bool bold);
inline juce::Font uiFont(float h, bool bold = false)
{
    return juce::Font(juce::FontOptions(uiTypeface(bold)).withHeight(h * 1.14f));
}

// Slider that asks its owner for a context menu on right-click and resets on double-click.
struct Knob : juce::Slider, private juce::Label::Listener
{
    Knob() : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTextBoxIsEditable(false);
        noFocus();
    }
    // Typing a value is only offered from the right-click menu: a plain click on the value text must never open an editor.
    void typeValue()
    {
        setTextBoxIsEditable(true);
        noFocus();
        showTextBox();
    }
    std::function<void(Knob &, juce::Point<int> screenPos)> onContextMenu;
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu)
        {
            onContextMenu(*this, e.getScreenPosition());
            return;
        }
        juce::Slider::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!e.mods.isPopupMenu())
            juce::Slider::mouseUp(e);
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!e.mods.isPopupMenu())
            juce::Slider::mouseDrag(e);
    }
    // Slider switches "wants keyboard focus" back on for its value box whenever the look-and-feel or enabled state changes.
    void lookAndFeelChanged() override
    {
        juce::Slider::lookAndFeelChanged();
        noFocus();
    }
    void enablementChanged() override
    {
        juce::Slider::enablementChanged();
        noFocus();
    }

  private:
    void noFocus()
    {
        for (auto *c : getChildren())
        {
            c->setWantsKeyboardFocus(false);
            c->setMouseClickGrabsKeyboardFocus(false);
            if (auto *l = dynamic_cast<juce::Label *>(c))
            {
                l->removeListener(this);
                l->addListener(this);
            }
        }
    }
    void labelTextChanged(juce::Label *) override {}
    void editorHidden(juce::Label *, juce::TextEditor &) override
    {
        setTextBoxIsEditable(false);
        noFocus();
        hostkeys::releaseLater(*this);
    }
};

// Toggle button: never takes keyboard focus; a right-click only opens the parameter menu, it does not flip the switch.
struct Btn : juce::ToggleButton
{
    Btn()
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    std::function<void(Btn &, juce::Point<int> screenPos)> onContextMenu;
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu(*this, e.getScreenPosition());
            return;
        }
        juce::ToggleButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!e.mods.isPopupMenu())
            juce::ToggleButton::mouseUp(e);
    }
};

struct NoFocusCombo : juce::ComboBox
{
    NoFocusCombo()
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
};

// Small in-editor popup menu with underlined shortcut letters ("&Reset" -> R is underlined and pressing R runs it).
class PopupOverlay : public juce::Component
{
  public:
    struct Item
    {
        juce::String text;
        std::function<void()> action;
        bool enabled = true, ticked = false, separatorAbove = false;
    };
    PopupOverlay();
    void show(juce::Point<int> where, std::vector<Item> newItems);
    void dismiss();
    bool isOpen() const { return open; }
    juce::StringArray itemTexts() const
    {
        juce::StringArray s;
        for (auto &i : items)
            s.add(i.text);
        return s;
    }
    bool activate(const juce::String &textPart)
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].enabled && items[i].text.contains(textPart))
            {
                trigger((int)i);
                return true;
            }
        return false;
    }
    void paint(juce::Graphics &) override;
    void mouseMove(const juce::MouseEvent &) override;
    void mouseDown(const juce::MouseEvent &) override;
    bool keyPressed(const juce::KeyPress &) override; // only its own shortcut letters / arrows / Enter / Esc
    void focusLost(FocusChangeType) override {}
    bool hitTest(int, int) override { return open; }

  private:
    int itemAt(juce::Point<int>) const;
    juce::Rectangle<int> rowBounds(int i) const;
    void trigger(int i);
    std::vector<Item> items;
    juce::Rectangle<int> panel;
    int hover = -1;
    bool open = false;
};
} // namespace gzui
