#pragma once
// 1.0.1: keeps the DAW's own keys (Space = play / pause, ...) working while the editor is open.
//
// Why "after I touch a knob, Space no longer plays / pauses": Windows delivers a keystroke to the window that has the *native* keyboard focus.
// Whenever a JUCE component that wants keyboard focus is clicked (or any click lands on a component whose search for a focusable child finds one),
// JUCE calls SetFocus() on the plug-in's own HWND. From then on every key goes to the plug-in, which handles none of them, and the host never sees them.
// The offender was the knobs' value text boxes (juce::Label): Slider turns "wants keyboard focus" back ON for them every time its look-and-feel or its
// enabled state changes (after the constructor had switched it off), so even a click on the background ended up focusing one.
//
// Rules now: nothing in the editor wants keyboard focus, except (a) the right-click popup while it is open (its underlined shortcut letters) and
// (b) the value text editor while a number is being typed. When either ends, the native focus goes straight back to the host window that had it.
#include <juce_audio_processors/juce_audio_processors.h>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace drui { namespace hostkeys
{
    inline juce::AudioProcessorEditor* editorOf (juce::Component& c)
    {
        if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (&c)) return e;
        return c.findParentComponentOfClass<juce::AudioProcessorEditor>();
    }

    // no component below (and including) root may take keyboard focus; `except` (the popup) is skipped with its children
    inline void noKeyboardFocus (juce::Component& root, const juce::Component* except = nullptr)
    {
        if (&root == except) return;
        root.setWantsKeyboardFocus (false);
        root.setMouseClickGrabsKeyboardFocus (false);
        for (auto* ch : root.getChildren()) noKeyboardFocus (*ch, except);
    }

   #if JUCE_WINDOWS
    inline HWND nativeOf (juce::Component& ed) { if (auto* p = ed.getPeer()) return (HWND) p->getNativeHandle(); return nullptr; }

    // true when the plug-in window (or a child of it) owns the native keyboard focus
    inline bool holdsNativeFocus (juce::Component& ed)
    {
        const HWND mine = nativeOf (ed), f = GetFocus();
        return mine != nullptr && f != nullptr && (f == mine || IsChild (mine, f));
    }

    // Remember which host window has the keyboard focus right now. Called often (a timer) and just before the plug-in takes the focus.
    inline void remember (juce::Component& c)
    {
        if (auto* ed = editorOf (c))
        {
            const HWND mine = nativeOf (*ed), f = GetFocus();
            if (mine != nullptr && f != nullptr && f != mine && ! IsChild (mine, f))
                ed->getProperties().set ("hostFocus", (juce::int64) (intptr_t) f);
        }
    }

    // Give the native keyboard focus back to the host. Does nothing unless the plug-in really holds it, and never while the user is typing into
    // one of our text boxes or a modal menu is open. Returns true when the focus was moved.
    inline bool release (juce::Component& c)
    {
        auto* ed = editorOf (c);
        if (ed == nullptr || ! holdsNativeFocus (*ed)) return false;
        if (dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
        if (auto* mm = juce::ModalComponentManager::getInstance()) if (mm->getNumModalComponents() > 0) return false;

        const HWND mine = nativeOf (*ed);
        HWND target = (HWND) (intptr_t) (juce::int64) ed->getProperties().getWithDefault ("hostFocus", (juce::int64) 0);
        if (target == nullptr || ! IsWindow (target) || target == mine || IsChild (mine, target) || ! IsWindowVisible (target))
        {
            target = GetAncestor (mine, GA_ROOT);        // fall back to the host's top-level window that contains the plug-in
            if (target == mine) target = nullptr;
        }
        if (target == nullptr || GetWindowThreadProcessId (target, nullptr) != GetCurrentThreadId()) return false;
        juce::Component::unfocusAllComponents();
        SetFocus (target);
        return true;
    }
   #else
    inline void remember (juce::Component&) {}
    inline bool release (juce::Component& c) { if (auto* ed = editorOf (c)) { ed->giveAwayKeyboardFocus(); } return false; }
   #endif

    // after the current message (e.g. after a text editor has finished closing)
    inline void releaseLater (juce::Component& c)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (&c)] { if (sp != nullptr) release (*sp); });
    }
}}
