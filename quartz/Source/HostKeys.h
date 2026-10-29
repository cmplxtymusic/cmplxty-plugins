#pragma once
// Quartz 1.0.3: keeps the DAW's own keys (Space = play / pause, ...) working while Quartz is open.
//
// Root cause of "after I touch a knob, Space no longer plays/pauses": Windows delivers keystrokes to the window that has the
// *native* keyboard focus. The old editor asked JUCE for keyboard focus (so Ctrl+Z worked), so the first click on any control made JUCE call
// SetFocus() on the plug-in's own HWND. From then on every key went to Quartz, which handles none of them, and the host never saw them.
//
// Now the editor never asks for keyboard focus. The only times Quartz takes it are the times it must read the keyboard:
//   - a right-click menu is open (its underlined shortcut letters), and
//   - a value is being typed into the inline text box.
// Both hand the focus straight back to the host window that had it before (see release()).
#include <juce_audio_processors/juce_audio_processors.h>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace qzui { namespace hostkeys
{
    inline juce::AudioProcessorEditor* editorOf (juce::Component& c)
    {
        if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (&c)) return e;
        return c.findParentComponentOfClass<juce::AudioProcessorEditor>();
    }

   #if JUCE_WINDOWS
    inline HWND nativeOf (juce::Component& ed) { if (auto* p = ed.getPeer()) return (HWND) p->getNativeHandle(); return nullptr; }

    // true when the plug-in window (or a child of it) owns the native keyboard focus
    inline bool holdsNativeFocus (juce::Component& ed)
    {
        const HWND mine = nativeOf (ed), f = GetFocus();
        return mine != nullptr && f != nullptr && (f == mine || IsChild (mine, f));
    }

    // Remember which host window has the keyboard focus right now. Call it just before Quartz takes the focus.
    inline void remember (juce::Component& c)
    {
        if (auto* ed = editorOf (c))
        {
            const HWND mine = nativeOf (*ed), f = GetFocus();
            if (mine != nullptr && f != nullptr && f != mine && ! IsChild (mine, f))
                ed->getProperties().set ("qzHostFocus", (juce::int64) (intptr_t) f);
        }
    }

    // Give the native keyboard focus back to the host. Does nothing unless Quartz really holds it, and never while the user is typing
    // into one of our text boxes or a modal menu is open. Returns true when the focus was moved.
    inline bool release (juce::Component& c)
    {
        auto* ed = editorOf (c);
        if (ed == nullptr || ! holdsNativeFocus (*ed)) return false;
        if (dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
        if (auto* mm = juce::ModalComponentManager::getInstance()) if (mm->getNumModalComponents() > 0) return false;

        const HWND mine = nativeOf (*ed);
        HWND target = (HWND) (intptr_t) (juce::int64) ed->getProperties().getWithDefault ("qzHostFocus", (juce::int64) 0);
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

    // While a right-click menu is open the editor may hold the keyboard so the menu's shortcut letters work; end() hands it back.
    struct MenuFocus
    {
        juce::Component::SafePointer<juce::Component> editor;
        static MenuFocus begin (juce::Component& owner)
        {
            MenuFocus m;
            if (auto* ed = editorOf (owner))
            {
                m.editor = ed;
                remember (*ed);
                ed->setWantsKeyboardFocus (true);
                ed->grabKeyboardFocus();
            }
            return m;
        }
        void end() const
        {
            if (editor == nullptr) return;
            editor->setWantsKeyboardFocus (false);
            release (*editor);
        }
    };
}}
