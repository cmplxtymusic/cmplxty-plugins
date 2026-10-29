// Quartz UI components. All coordinates are in design pixels (1100 x 720); the editor scales the root.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "Ui.h"
#include "HostKeys.h"

namespace qzui
{
using namespace qz;
using ui::Fonts; using juce::Graphics; using juce::Colour; using juce::Rectangle;

struct Ctx
{
    QuartzProcessor& proc;
    std::function<void (const juce::String&)> hint;
    float spec[48] {};
    bool demo = false;
    juce::RangedAudioParameter& P (int i) const { return *proc.param (i); }
    bool sectionOn (int sec) const { return sec < 0 || proc.getRaw (sec == 0 ? pQon : pAon) > 0.5f; }
    float norm (int i) const { return P (i).getValue(); }
    void setNormGesture (int i, float n) { auto& p = P (i); p.beginChangeGesture(); p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n)); p.endChangeGesture(); }
    void toggleGesture (int i) { setNormGesture (i, norm (i) > 0.5f ? 0.0f : 1.0f); }
    void touched (int sec) { proc.markEdited (sec); }
    // every user edit of a parameter is one begin / set / end gesture (host records it, FL undo works)
    void resetGesture (int i) { auto& p = P (i); proc.pushUndo(); setNormGesture (i, p.getDefaultValue()); touched (sectionOf (i)); }
};

// ---------------------------------------------------------------- shared value clipboard (Copy value / Paste value)
// One clipboard for every knob of every Quartz instance in the host process (static in this module).
// It holds the NORMALISED value (0..1), like FL Studio's copy/paste value. The OS clipboard is never written.
struct ValueClip
{
    static inline std::atomic<bool> valid { false };
    static inline std::atomic<float> norm { 0.0f };
};
inline void copyValue (Ctx& ctx, int pid) { ValueClip::norm = ctx.P (pid).getValue(); ValueClip::valid = true; ctx.hint ("Copied " + ctx.P (pid).getCurrentValueAsText()); }
// number-ish text on the OS clipboard ("12", "-3.5 st", "40%") is also accepted by Paste when nothing was copied inside Quartz
inline bool textLooksNumeric (const juce::String& t0)
{
    const auto t = t0.trim();
    if (t.isEmpty() || t.length() > 24) return false;
    return juce::String ("+-.0123456789").containsChar (t[0]) && t.containsAnyOf ("0123456789");
}
inline bool pasteNorm (Ctx& ctx, int pid, float& out)
{
    if (ValueClip::valid) { out = ValueClip::norm; return true; }
    const auto txt = juce::SystemClipboard::getTextFromClipboard();
    if (! textLooksNumeric (txt)) return false;
    out = juce::jlimit (0.0f, 1.0f, ctx.P (pid).getValueForText (txt.trim())); return true;
}
inline bool canPaste (Ctx& ctx, int pid) { float n; return pasteNorm (ctx, pid, n); }
inline void pasteValue (Ctx& ctx, int pid)
{
    float n; if (! pasteNorm (ctx, pid, n)) return;
    ctx.proc.pushUndo(); ctx.setNormGesture (pid, n); ctx.touched (sectionOf (pid));
}

// ---------------------------------------------------------------- right-click menus with underlined keyboard shortcuts
struct MenuEntry
{
    juce::String text;                 // "&Copy value": the letter after & is the shortcut
    std::function<void()> run;
    bool enabled = true, ticked = false, separator = false;
    static MenuEntry sep() { MenuEntry e; e.separator = true; return e; }
    juce::juce_wchar key() const { const int i = text.indexOfChar ('&'); return (i >= 0 && i + 1 < text.length()) ? juce::CharacterFunctions::toUpperCase (text[i + 1]) : 0; }
};

// Listens for plain letters on the modal menu window (JUCE menus only handle arrows / return / escape themselves).
class MenuKeys : public juce::KeyListener
{
public:
    static MenuKeys& get() { static MenuKeys k; return k; }
    void arm (std::shared_ptr<std::vector<MenuEntry>> l, juce::Component* menuWindow)
    {
        list = std::move (l); win = menuWindow;
        if (win != nullptr) win->addKeyListener (this);
    }
    void disarm (const std::shared_ptr<std::vector<MenuEntry>>& l)
    {
        if (list != l) return;
        if (win != nullptr) win->removeKeyListener (this);
        win = nullptr; list.reset();
    }
    // exposed for the offline UI test
    bool trigger (juce::juce_wchar letter)
    {
        if (! list) return false;
        const auto up = juce::CharacterFunctions::toUpperCase (letter);
        for (auto& e : *list)
            if (! e.separator && e.enabled && e.key() == up && e.run)
            {
                auto fn = e.run; auto keep = list;
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::callAsync (std::move (fn));
                return true;
            }
        return false;
    }
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        if (k.getModifiers().isAltDown()) return false;
        const int code = k.getKeyCode();
        if (code < 'A' || code > 'Z') { if (code < 'a' || code > 'z') return false; }
        return trigger ((juce::juce_wchar) code);
    }
private:
    std::shared_ptr<std::vector<MenuEntry>> list; juce::Component::SafePointer<juce::Component> win;
};

inline void showEntryMenu (juce::Component& owner, std::vector<MenuEntry> entries, juce::PopupMenu extra = {}, const juce::String& extraTitle = {})
{
    auto list = std::make_shared<std::vector<MenuEntry>> (std::move (entries));
    juce::PopupMenu m;
    for (size_t i = 0; i < list->size(); ++i)
    {
        auto& e = (*list)[i];
        if (e.separator) m.addSeparator(); else m.addItem ((int) i + 1, e.text, e.enabled, e.ticked);
    }
    if (extra.getNumItems() > 0) { m.addSeparator(); m.addSubMenu (extraTitle, extra); }
    auto focus = hostkeys::MenuFocus::begin (owner);   // the menu needs the keyboard for its shortcut letters; handed back below
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner), [list, focus] (int r)
    {
        MenuKeys::get().disarm (list);
        focus.end();
        if (r >= 1 && r <= (int) list->size() && (*list)[(size_t) r - 1].run) (*list)[(size_t) r - 1].run();
    });
    juce::Component* mw = nullptr;
    if (auto* mcm = juce::ModalComponentManager::getInstance()) mw = mcm->getModalComponent (0);
    MenuKeys::get().arm (list, mw);
}

// The host's own menu for a parameter (VST3 IComponentHandler3::createContextMenu): FL puts "Create automation clip" etc. in it.
struct HostMenu
{
    std::shared_ptr<juce::HostProvidedContextMenu> host;   // null when the host has no context menu
    juce::PopupMenu all;                                                                   // every host item (actions stay valid on their own)
    std::function<void()> automation;                                                       // the host item whose text mentions "automation"
    bool automationEnabled = false;
};
// The host's menu object must outlive the popup (the actions inside it point back at it). Keep exactly one alive; the editor drops it on close.
inline std::shared_ptr<juce::HostProvidedContextMenu>& hostMenuKeepAlive() { static std::shared_ptr<juce::HostProvidedContextMenu> k; return k; }

inline HostMenu getHostMenu (juce::Component& owner, Ctx& ctx, int pid)
{
    HostMenu h;
    if (auto* ed = owner.findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* hc = ed->getHostContext())
            if (auto up = hc->getContextMenuForParameter (&ctx.P (pid)))
            {
                h.host = std::shared_ptr<juce::HostProvidedContextMenu> (std::move (up));
                hostMenuKeepAlive() = h.host;
                h.all = h.host->getEquivalentPopupMenu();
                juce::PopupMenu::MenuItemIterator it (h.all, true);
                while (it.next())
                {
                    auto& item = it.getItem();
                    if (item.text.containsIgnoreCase ("automation") && item.action != nullptr && ! item.isSeparator)
                    {
                        if (item.text.containsIgnoreCase ("create") || ! h.automation) { h.automation = [keep = h.host, act = item.action] { act(); }; h.automationEnabled = item.isEnabled; }
                    }
                }
            }
    return h;
}

// Copy / Paste / Reset / Enter value / Create automation clip - shared by knobs, the section diamonds, LINK and the dropdowns.
inline std::vector<MenuEntry> standardEntries (juce::Component& owner, Ctx& ctx, int pid, std::function<void()> enterValue, const HostMenu& hm)
{
    using SP = juce::Component::SafePointer<juce::Component>;
    SP sp (&owner); Ctx* c = &ctx;
    std::vector<MenuEntry> v;
    v.push_back ({ "&Copy value", [sp, c, pid] { if (sp != nullptr) copyValue (*c, pid); } });
    { MenuEntry e { "Paste &value", [sp, c, pid] { if (sp != nullptr) pasteValue (*c, pid); } }; e.enabled = canPaste (ctx, pid); v.push_back (e); }
    v.push_back ({ "Reset to &default", [sp, c, pid] { if (sp != nullptr) c->resetGesture (pid); } });
    if (enterValue) v.push_back ({ "&Enter value...", std::move (enterValue) });
    v.push_back (MenuEntry::sep());
    {
        MenuEntry e { "Create &automation clip", {} };
        if (hm.automation) { e.run = hm.automation; e.enabled = hm.automationEnabled; }
        else if (hm.host) { auto host = hm.host; e.run = [host, sp] { if (sp != nullptr) if (auto* ed = sp->findParentComponentOfClass<juce::AudioProcessorEditor>()) host->showNativeMenu (ed->getMouseXYRelative()); }; }
        else { e.text = "Create &automation clip (host only)"; e.enabled = false; }
        v.push_back (e);
    }
    return v;
}

// ---------------------------------------------------------------- inline text editor
class InlineEdit : public juce::TextEditor
{
public:
    static void open (juce::Component& owner, Rectangle<int> r, const juce::String& init, float fontH, std::function<void (const juce::String&, bool)> commit)
    {
        if (current != nullptr) delete current.getComponent();
        auto* e = new InlineEdit (std::move (commit));
        current = e;
        e->owner = &owner;
        hostkeys::remember (owner);        // the text box takes the keyboard; finish() gives it back to the host
        owner.addAndMakeVisible (e);
        e->setBounds (r);
        e->setFont (Fonts::monoF (fontH));
        e->setJustification (juce::Justification::centred);
        e->setIndents (2, 0);
        e->setText (init, false);
        e->selectAll();
        e->grabKeyboardFocus();
    }
private:
    explicit InlineEdit (std::function<void (const juce::String&, bool)> c) : commitFn (std::move (c))
    {
        setMultiLine (false); setReturnKeyStartsNewLine (false); setScrollbarsShown (false); setSelectAllWhenFocused (true);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff000000));
        onReturnKey = [this] { finish (true); };
        onEscapeKey = [this] { finish (false); };
        onFocusLost = [this] { finish (true); };
    }
    void finish (bool commit)
    {
        if (done) return; done = true;
        const auto t = getText(); auto fn = commitFn;
        setVisible (false);
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<juce::Component> (this)] { delete sp.getComponent(); });
        if (fn) fn (t, commit);
        // after the box is gone (queued behind the delete above) the host gets its keyboard back, so Space plays / pauses again
        juce::MessageManager::callAsync ([o = owner] { if (o != nullptr) hostkeys::release (*o); });
    }
    std::function<void (const juce::String&, bool)> commitFn; bool done = false; juce::Component::SafePointer<juce::Component> owner;
    static inline juce::Component::SafePointer<juce::Component> current;
};

inline juce::String noteName (int midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
inline float parseNote (juce::String t, float fallback)
{
    t = t.trim().toUpperCase();
    if (t.isEmpty()) return fallback;
    const juce::juce_wchar c0 = t[0];
    if (c0 >= 'A' && c0 <= 'G')
    {
        static const int semi[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int s = semi[c0 - 'A']; int i = 1;
        if (t[i] == '#') { ++s; ++i; } else if (t[i] == 'B' && t.length() > i + 1) { --s; ++i; }
        const int oct = t.substring (i).containsAnyOf ("0123456789") ? t.substring (i).getIntValue() : 3;
        return (float) ((oct + 1) * 12 + s);
    }
    return t.getFloatValue();
}

// ---------------------------------------------------------------- knobs
enum class KnobStyle { Facet, Gender, Gem };

class KnobComp : public juce::Component
{
public:
    KnobComp (Ctx& c, int pid_, const juce::String& lbl, int size, int width, KnobStyle st = KnobStyle::Facet)
        : ctx (c), pid (pid_), label (lbl), s (size), w (width), style (st), sec (sectionOf (pid_)),
          attMain (c.P (pid_), [this] (float) { repaint(); }),
          byp (bypassFor (pid_) >= 0 ? new juce::ParameterAttachment (c.P (bypassFor (pid_)), [this] (float) { repaint(); }) : nullptr)
    {
        auto& spec = kParams[pid];
        bipolar = spec.mn < 0 && spec.mx > 0 && pid != pGate;
        zeroNorm = bipolar ? juce::jlimit (0.0f, 1.0f, (0.0f - spec.mn) / (spec.mx - spec.mn)) : -1.0f;
        if (spec.skewMid > spec.mn && spec.skewMid < spec.mx && bipolar) zeroNorm = ctx.P (pid).convertTo0to1 (0.0f);
        attMain.sendInitialUpdate(); if (byp) byp->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        height = style == KnobStyle::Gem ? 366 : s + 29;
    }
    ~KnobComp() override { if (dragging) attMain.endGesture(); }
    int totalHeight() const { return height; }
    int paramId() const { return pid; }
    friend struct TestAccess;     // tests/ui_test.cpp

    void paint (Graphics& g) override
    {
        const bool bypassed = byp != nullptr && ctx.norm (bypassFor (pid)) > 0.5f;
        const float alpha = bypassed ? 0.32f : 1.0f;
        const float v = ctx.norm (pid);
        if (style == KnobStyle::Gem) { paintGem (g, v, alpha, bypassed); return; }
        ui::drawFacetKnob (g, (float) (w - s) * 0.5f, 0.0f, (float) s, v, zeroNorm, alpha);
        g.beginTransparencyLayer (alpha);
        if (style == KnobStyle::Gender)
        {
            const auto small = Fonts::ui (8.5f, 1.2f);
            ui::text (g, "MASC", { 0, (float) s + 1, 34, 13 }, small, juce::Colour (0xff8a8a8a), juce::Justification::centredLeft);
            ui::text (g, "FEM", { (float) w - 34, (float) s + 1, 34, 13 }, small, juce::Colour (0xff8a8a8a), juce::Justification::centredRight);
            ui::text (g, label, { 0, (float) s + 1, (float) w, 13 }, Fonts::ui (10.5f, 1.4f), juce::Colours::white);
        }
        else ui::text (g, label, { 0, (float) s + 1, (float) w, 13 }, Fonts::ui (10.5f, 1.4f), juce::Colour (0xffbdbdbd));
        if (! editing) ui::text (g, valueText(), valueRect().toFloat(), Fonts::monoF (10.5f), juce::Colours::white);
        g.endTransparencyLayer();
        if (byp) ui::powerIcon (g, (float) bypassRect().getX(), (float) bypassRect().getY(), bypassed ? juce::Colour (0xff7a7a7a) : juce::Colour (0xfff2f2f2));
    }

    bool hitTest (int x, int y) override
    {
        if (style != KnobStyle::Gem) return true;
        const float dx = (float) x - cx, dy = (float) y - cy;
        return dx * dx + dy * dy <= (GR + 16) * (GR + 16) || valueRect().expanded (6).contains (x, y) || bypassRect().expanded (4).contains (x, y);
    }

    void mouseEnter (const juce::MouseEvent&) override { ctx.hint (juce::String (kParams[pid].name) + " - drag to change, Shift = fine, double-click = reset, double-click the value = type, right-click = copy / paste / automation"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (dragging) { attMain.endGesture(); }   // a lost mouse-up must never leave a gesture open
        dragging = false; downOnValue = false;
        if (byp && bypassRect().expanded (4).contains (e.getPosition()))
        {
            ctx.proc.pushUndo(); ctx.toggleGesture (bypassFor (pid)); ctx.touched (sec); return;
        }
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (pid == pCNote && valueRect().expanded (3).contains (e.getPosition()) && style != KnobStyle::Gem) { downOnValue = true; return; }
        if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isAltDown()) { resetToDefault(); return; }
        ctx.proc.pushUndo();
        attMain.beginGesture(); dragging = true; cur = ctx.norm (pid); lastY = e.position.y;
        ctx.touched (sec);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float per = style == KnobStyle::Gem ? 170.0f : 190.0f;
        const float k = e.mods.isShiftDown() ? 1.0f / (per * 10.0f) : 1.0f / per;
        cur = juce::jlimit (0.0f, 1.0f, cur + (lastY - e.position.y) * k); lastY = e.position.y;
        attMain.setValueAsPartOfGesture (ctx.P (pid).convertFrom0to1 (cur));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging) { attMain.endGesture(); dragging = false; }
        if (downOnValue && ! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isPopupMenu() && valueRect().expanded (3).contains (e.getPosition()))
        {
            ctx.proc.pushUndo(); auto& p = ctx.P (pCShape); p.beginChangeGesture(); p.setValueNotifyingHost (p.convertTo0to1 (std::fmod (p.convertFrom0to1 (p.getValue()) + 1.0f, 3.0f))); p.endChangeGesture(); ctx.touched (sec);
        }
        downOnValue = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        if (byp && bypassRect().expanded (4).contains (e.getPosition())) return;
        const bool onValue = valueRect().expanded (3).contains (e.getPosition());
        if (pid == pCNote && onValue && style != KnobStyle::Gem) return;     // a click on this value cycles the carrier shape
        if (onValue) beginTyping();            // double-click the number: type a value
        else resetToDefault();                 // double-click the knob: back to default (one undoable gesture)
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        const float d = (wd.isReversed ? -1.0f : 1.0f) * wd.deltaY;
        if (d == 0.0f) return;
        ctx.proc.pushUndo();
        auto& p = ctx.P (pid);
        const bool discrete = kParams[pid].step > 0 && (kParams[pid].mx - kParams[pid].mn) <= 60;
        float n = p.getValue();
        if (discrete) n += (d > 0 ? 1.0f : -1.0f) / (kParams[pid].mx - kParams[pid].mn);
        else n += d * (e.mods.isShiftDown() ? 0.04f : 0.4f) * 0.1f;
        attMain.beginGesture(); attMain.setValueAsPartOfGesture (p.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); attMain.endGesture();
        ctx.touched (sec);
    }

private:
    static constexpr float cx = 196.0f, cy = 158.0f, GR = 94.0f;
    Rectangle<int> valueRect() const
    {
        if (style == KnobStyle::Gem) return { (int) cx - 40, (int) (cy + GR + 28), 80, 22 };
        return { 0, s + 16, w, 13 };
    }
    Rectangle<int> bypassRect() const
    {
        if (style == KnobStyle::Gem) return { (int) cx + 46, (int) (cy + GR + 51), 12, 12 };
        const int r = juce::roundToInt ((float) (w - s) * 0.5f) - 3;
        return { w - r - 12, 0, 12, 12 };
    }
    juce::String valueText() const
    {
        if (pid == pCNote)
        {
            const int shape = juce::roundToInt (ctx.P (pCShape).convertFrom0to1 (ctx.norm (pCShape)));
            if (shape == 2) return "Noise";
            return juce::String (shape == 0 ? "Saw " : "Sqr ") + noteName (juce::roundToInt (ctx.P (pid).convertFrom0to1 (ctx.norm (pid))));
        }
        if (pid == pIntensity) return juce::String (juce::roundToInt (ctx.P (pid).convertFrom0to1 (ctx.norm (pid))));
        return ctx.P (pid).getCurrentValueAsText();
    }
    void resetToDefault()
    {
        ctx.proc.pushUndo(); attMain.beginGesture(); attMain.setValueAsPartOfGesture (kParams[pid].def); attMain.endGesture(); ctx.touched (sec);
    }
    void beginTyping()
    {
        editing = true; repaint();
        auto r = valueRect().expanded (4, 2);
        if (style == KnobStyle::Gem) r = { (int) cx - 44, (int) (cy + GR + 24), 88, 26 };
        juce::String init;
        if (pid == pCNote) init = noteName (juce::roundToInt (ctx.P (pid).convertFrom0to1 (ctx.norm (pid))));
        else if (pid == pIntensity) init = juce::String (juce::roundToInt (ctx.P (pid).convertFrom0to1 (ctx.norm (pid))));
        else init = valueText();
        InlineEdit::open (*this, r, init, style == KnobStyle::Gem ? 17.0f : 11.0f, [sp = SafePointer<KnobComp> (this)] (const juce::String& t, bool ok)
        {
            if (sp == nullptr) return;
            sp->editing = false; sp->repaint();
            if (! ok) return;
            float actual;
            if (sp->pid == pCNote) actual = parseNote (t, kParams[pCNote].def);
            else actual = sp->ctx.P (sp->pid).convertFrom0to1 (sp->ctx.P (sp->pid).getValueForText (t));
            sp->ctx.proc.pushUndo(); sp->attMain.beginGesture(); sp->attMain.setValueAsPartOfGesture (juce::jlimit (kParams[sp->pid].mn, kParams[sp->pid].mx, actual)); sp->attMain.endGesture();
            sp->ctx.touched (sp->sec);
        });
    }
    void showMenu()
    {
        auto hm = getHostMenu (*this, ctx, pid);
        auto v = standardEntries (*this, ctx, pid, [sp = SafePointer<KnobComp> (this)] { if (sp != nullptr) sp->beginTyping(); }, hm);
        using SP = SafePointer<KnobComp>; SP sp (this);
        if (byp)
        {
            const bool on = ctx.norm (bypassFor (pid)) > 0.5f;
            v.push_back ({ on ? "Remove &bypass" : "&Bypass this knob", [sp] { if (sp != nullptr) { sp->ctx.proc.pushUndo(); sp->ctx.toggleGesture (bypassFor (sp->pid)); sp->ctx.touched (sp->sec); } } });
        }
        if (pid == pCNote)
        {
            v.push_back (MenuEntry::sep());
            const char* names[3] = { "Carrier: &Saw", "Carrier: S&quare", "Carrier: &Noise" };
            const int cur = juce::roundToInt (ctx.P (pCShape).convertFrom0to1 (ctx.norm (pCShape)));
            for (int k = 0; k < 3; ++k)
            {
                MenuEntry e { names[k], [sp, k] { if (sp != nullptr) { sp->ctx.proc.pushUndo(); sp->ctx.setNormGesture (pCShape, sp->ctx.P (pCShape).convertTo0to1 ((float) k)); sp->ctx.touched (sp->sec); } } };
                e.ticked = k == cur; v.push_back (e);
            }
        }
        showEntryMenu (*this, std::move (v), hm.all, "Host menu");
    }

    void paintGem (Graphics& g, float v, float alpha, bool bypassed)
    {
        const float I = v;
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.02f + 0.2f * I * I), cx, cy, juce::Colours::white.withAlpha (0.0f), cx + 165.0f, cy, true));
        g.fillEllipse (cx - 165, cy - 165, 330, 330);
        const float rr = GR + 10.0f, a0 = -135.0f, a1 = 135.0f, av = a0 + (a1 - a0) * I;
        // spectrum rays hidden in the bottom gap
        for (int i = 0; i < 64; ++i)
        {
            const float a = (float) i * 360.0f / 64.0f;
            if (std::abs (a - 180.0f) < 46.0f) continue;
            const float sv = ctx.spec[(int) std::floor (std::abs (32.0f - (float) i) * 48.0f / 32.0f) % 48] * (0.8f + 0.2f * std::sin ((float) i)) * (0.25f + 0.75f * I);
            const float len = 3.0f + sv * 20.0f;
            const float c = std::cos (ui::rad (a - 90.0f)), sn = std::sin (ui::rad (a - 90.0f));
            g.setColour (ui::grey (70.0f + sv * 185.0f * (0.4f + 0.6f * I)));
            g.drawLine (cx + c * (GR + 24), cy + sn * (GR + 24), cx + c * (GR + 24 + len), cy + sn * (GR + 24 + len), 3.0f);
        }
        for (int i = 0; i <= 20; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 20.0f;
            g.setColour (a <= av + 0.1f ? juce::Colour (0xffdddddd) : juce::Colour (0xff444444));
            g.drawLine (juce::Line<float> (ui::P (cx, cy, rr + 4, a), ui::P (cx, cy, rr + (i % 5 == 0 ? 10 : 7), a)), i % 5 == 0 ? 1.8f : 1.0f);
        }
        juce::Path t; ui::arcPath (t, cx, cy, rr, a0, a1);
        g.setColour (juce::Colour (0xff3a3a3a)); g.strokePath (t, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (I > 0.0f) { juce::Path a; ui::arcPath (a, cx, cy, rr, a0, av); g.setColour (juce::Colour (0xfff2f2f2)); g.strokePath (a, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        ui::drawGem (g, cx, cy, GR, 9, ctx.spec, 48, I);
        const auto th = ui::P (cx, cy, rr, av);
        g.setColour (juce::Colours::white); g.fillEllipse (th.x - 5.5f, th.y - 5.5f, 11, 11);
        g.setColour (juce::Colours::black); g.drawEllipse (th.x - 5.5f, th.y - 5.5f, 11, 11, 1.2f);
        const float fa = bypassed ? 0.4f : 1.0f; (void) alpha;
        if (! editing) ui::text (g, juce::String (juce::roundToInt (I * 100.0f)), { cx - 50, cy + GR + 28, 100, 22 }, Fonts::monoF (21.0f), juce::Colours::white.withAlpha (fa));
        ui::text (g, "INTENSITY", { cx - 60, cy + GR + 51, 120, 14 }, Fonts::ui (10.5f, 2.0f), juce::Colour (0xffbdbdbd).withAlpha (fa));
        ui::powerIcon (g, (float) bypassRect().getX(), (float) bypassRect().getY(), bypassed ? juce::Colour (0xff7a7a7a) : juce::Colour (0xfff2f2f2));
    }

    Ctx& ctx; int pid; juce::String label; int s, w; KnobStyle style; int sec; int height = 0;
    juce::ParameterAttachment attMain; std::unique_ptr<juce::ParameterAttachment> byp;
    bool bipolar = false; float zeroNorm = -1.0f;
    bool dragging = false, downOnValue = false, editing = false; float cur = 0, lastY = 0;
};

// ---------------------------------------------------------------- meters
class MeterComp : public juce::Component
{
public:
    void setLevels (float l, float r) { lv[0] = l; lv[1] = r; repaint(); }
    void paint (Graphics& g) override
    {
        ui::text (g, label, { 0, 0, 28, (float) getHeight() }, Fonts::ui (9.0f, 1.5f), juce::Colour (0xffcfcfcf), juce::Justification::centredRight);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 10; ++i)
            {
                const bool on = (float) i / 10.0f < lv[c];
                g.setColour (on ? juce::Colour (0xffd8d8d8) : juce::Colour (0xff262626));
                g.fillRect (34.0f + (float) i * 5.0f, (float) c * 7.0f, 3.4f, 4.0f);
            }
    }
    juce::String label;
    float lv[2] { 0, 0 };
};

// ---------------------------------------------------------------- segmented choice buttons (Mode, Drive Type)
class SegGroup : public juce::Component
{
public:
    SegGroup (Ctx& c, int pid_, int itemW, int itemH, int gapY, int pad, int gapX, float spacing)
        : ctx (c), pid (pid_), iw (itemW), ih (itemH), gy (gapY), padX (pad), gx (gapX), sp (spacing), sec (sectionOf (pid_)),
          att (c.P (pid_), [this] (float v) { value = juce::roundToInt (v); repaint(); })
    {
        names = juce::StringArray::fromTokens (juce::String (kParams[pid].choices), "|", "");
        att.sendInitialUpdate();
    }
    int totalHeight() const { return names.size() * ih + (names.size() - 1) * gy; }
    void paint (Graphics& g) override
    {
        for (int i = 0; i < names.size(); ++i)
        {
            const bool on = i == value;
            Rectangle<float> r (0.0f, (float) (i * (ih + gy)), (float) iw, (float) ih);
            if (on) { g.setColour (juce::Colour (0xffe9e9e9)); g.fillRect (r); g.setColour (juce::Colours::white); g.drawRect (r, 1.0f); }
            else { g.setColour (juce::Colour (0xff3a3a3a)); g.drawRect (r, 1.0f); }
            const float dx = (float) padX + 4.0f;
            ui::diamond (g, dx, r.getCentreY(), 8.0f, juce::Colours::black, on ? juce::Colours::black : juce::Colour (0xff777777), 1.0f, on);
            ui::text (g, names[i].toUpperCase(), { (float) (padX + 8 + gx), r.getY(), (float) iw - (float) (padX + 8 + gx), (float) ih }, Fonts::ui (10.5f, sp), on ? juce::Colours::black : juce::Colour (0xff999999), juce::Justification::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            auto hm = getHostMenu (*this, ctx, pid);
            showEntryMenu (*this, standardEntries (*this, ctx, pid, {}, hm), hm.all, "Host menu"); return;
        }
        const int i = e.y / (ih + gy);
        if (i < 0 || i >= names.size()) return;
        ctx.proc.pushUndo(); att.setValueAsCompleteGesture ((float) i); ctx.touched (sec);
    }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hint (juce::String (kParams[pid].name) + " - click to choose"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
private:
    Ctx& ctx; int pid, iw, ih, gy, padX, gx; float sp; int sec; int value = 0; juce::StringArray names; juce::ParameterAttachment att;
};

// ---------------------------------------------------------------- dropdown (Key / Scale)
class DropComp : public juce::Component
{
public:
    DropComp (Ctx& c, int pid_) : ctx (c), pid (pid_), att (c.P (pid_), [this] (float v) { value = juce::roundToInt (v); repaint(); })
    {
        names = juce::StringArray::fromTokens (juce::String (kParams[pid].choices), "|", "");
        // "--" (= off) is listed first, but keeps its stored index (last), so old saved states stay valid
        for (int i = 0; i < names.size(); ++i) if (names[i] == "--") order.push_back (i);
        for (int i = 0; i < names.size(); ++i) if (names[i] != "--") order.push_back (i);
        att.sendInitialUpdate();
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0a0a)); g.fillRect (r);
        g.setColour (juce::Colour (0xff555555)); g.drawRect (r, 1.0f);
        const auto f = Fonts::ui (12.0f);
        const auto txt = names[value];
        ui::text (g, txt, r.withTrimmedLeft (9), f, juce::Colour (0xffeeeeee), juce::Justification::centredLeft);
        const float x = 9.0f + ui::textWidth (f, txt) + 8.0f, y = r.getCentreY();
        juce::Path p; p.addTriangle (x, y - 2, x + 7, y - 2, x + 3.5f, y + 2.5f);
        g.setColour (juce::Colour (0xffeeeeee)); g.fillPath (p);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            auto hm = getHostMenu (*this, ctx, pid);
            showEntryMenu (*this, standardEntries (*this, ctx, pid, {}, hm), hm.all, "Host menu"); return;
        }
        juce::PopupMenu m;
        for (int k = 0; k < (int) order.size(); ++k) m.addItem (order[(size_t) k] + 1, names[order[(size_t) k]], true, order[(size_t) k] == value);
        auto focus = hostkeys::MenuFocus::begin (*this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()), [sp = SafePointer<DropComp> (this), focus] (int r)
        {
            focus.end();
            if (sp == nullptr || r <= 0) return;
            sp->ctx.proc.pushUndo(); sp->att.setValueAsCompleteGesture ((float) (r - 1)); sp->ctx.touched (1);
        });
    }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hint (juce::String (kParams[pid].name) + " - used by Quantize and Robot modes (-- = no auto-tune / no snapping)"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
private:
    Ctx& ctx; int pid; int value = 0; juce::StringArray names; std::vector<int> order; juce::ParameterAttachment att;
};

// ---------------------------------------------------------------- LINK toggle
class LinkComp : public juce::Component
{
public:
    explicit LinkComp (Ctx& c) : ctx (c), att (c.P (pLink), [this] (float v) { on = v > 0.5f; repaint(); }) { att.sendInitialUpdate(); }
    void paint (Graphics& g) override
    {
        const Rectangle<float> b ((float) (getWidth() - 44) * 0.5f, 0.0f, 44.0f, 44.0f);
        if (on) { g.setColour (juce::Colour (0xffeeeeee)); g.fillRect (b); g.setColour (juce::Colours::white); g.drawRect (b, 1.0f); }
        else { g.setColour (juce::Colour (0xff666666)); g.drawRect (b, 1.0f); }
        const auto col = on ? juce::Colours::black : juce::Colour (0xff888888);
        g.setColour (col);
        const float ox = b.getX() + 9.0f, oy = 9.0f;
        g.drawLine (ox + 10, oy + 16, ox + 16, oy + 10, 2.0f);
        for (int k = 0; k < 2; ++k)
        {
            juce::Path p; const float x = k == 0 ? 2.0f : 14.0f, y = k == 0 ? 12.0f : 7.0f;
            p.addRoundedRectangle (x, y, 10.0f, 7.0f, 3.5f);
            p.applyTransform (juce::AffineTransform::rotation (-ui::kPi / 4, x + 5, y + 3.5f).translated (ox, oy));
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
        ui::text (g, "LINK", { 0, 47, (float) getWidth(), 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffaaaaaa));
        ui::text (g, on ? "ON" : "OFF", { 0, 61, (float) getWidth(), 12 }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xff777777));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            auto hm = getHostMenu (*this, ctx, pLink);
            showEntryMenu (*this, standardEntries (*this, ctx, pLink, {}, hm), hm.all, "Host menu"); return;
        }
        ctx.proc.pushUndo(); att.setValueAsCompleteGesture (on ? 0.0f : 1.0f); ctx.touched (1);
    }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hint ("Link - when on, pitch changes move the formant with it (like a speed change)"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
private:
    Ctx& ctx; bool on = false; juce::ParameterAttachment att;
};

// ---------------------------------------------------------------- pitch x formant pad
class XYPad : public juce::Component
{
public:
    explicit XYPad (Ctx& c) : ctx (c), ap (c.P (pPitch), [this] (float) { repaint(); }), af (c.P (pFormant), [this] (float) { repaint(); })
    { ap.sendInitialUpdate(); af.sendInitialUpdate();
      ui::Lcg r (11); const float W = 116, H = 100;
      for (int i = 0; i < 14; ++i) { const float x = r() * W, y = r() * H, x2 = r() * W, y2 = r() * H; const float x3 = r() * W, y3 = r() * H; const float a = 0.02f + r() * 0.05f; tris.push_back ({ { x, y }, { x2, y2 }, { x3, y3 }, a }); }
    }
    void paint (Graphics& g) override
    {
        const float W = (float) getWidth(), H = (float) getHeight();
        const float px = ctx.norm (pPitch) * W, py = (1.0f - ctx.norm (pFormant)) * H;
        g.setColour (juce::Colour (0xff444444)); g.drawLine (W / 2, 0, W / 2, H); g.drawLine (0, H / 2, W, H / 2);
        for (auto& t : tris)
        {
            juce::Path p; p.addTriangle (t.a, t.b, t.c);
            g.setColour (juce::Colours::white.withAlpha (t.alpha)); g.fillPath (p);
            g.setColour (juce::Colours::white.withAlpha (0.06f)); g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        { juce::Path d; d.startNewSubPath (0, H); d.lineTo (W, 0); juce::Path dd; const float dash[2] = { 4, 4 };
          juce::PathStrokeType (1.0f).createDashedStroke (dd, d, dash, 2); g.setColour (juce::Colour (0xff666666)); g.fillPath (dd); }
        g.setColour (juce::Colour (0xffdddddd)); g.drawLine (px, 0, px, H); g.drawLine (0, py, W, py);
        g.setColour (juce::Colours::white); g.fillEllipse (px - 7, py - 7, 14, 14);
        g.setColour (juce::Colours::white.withAlpha (0.6f)); g.drawEllipse (px - 12, py - 12, 24, 24, 1.0f);
        const auto f = Fonts::monoF (9.0f);
        ui::text (g, "FORMANT +", { 6, 2, 80, 12 }, f, juce::Colour (0xff999999), juce::Justification::centredLeft);
        ui::text (g, "PITCH +", { W - 86, H - 14, 80, 12 }, f, juce::Colour (0xff999999), juce::Justification::centredRight);
    }
    ~XYPad() override { if (down) { ap.endGesture(); af.endGesture(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        if (down) { ap.endGesture(); af.endGesture(); down = false; }
        ctx.proc.pushUndo(); ap.beginGesture(); af.beginGesture(); down = true; ctx.touched (1); mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! down) return;
        const float nx = juce::jlimit (0.0f, 1.0f, (float) e.x / (float) getWidth()), ny = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) getHeight());
        ap.setValueAsPartOfGesture (ctx.P (pPitch).convertFrom0to1 (nx)); af.setValueAsPartOfGesture (ctx.P (pFormant).convertFrom0to1 (ny));
    }
    void mouseUp (const juce::MouseEvent&) override { if (down) { ap.endGesture(); af.endGesture(); down = false; } }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ctx.proc.pushUndo(); ap.beginGesture(); af.beginGesture(); ap.setValueAsPartOfGesture (0); af.setValueAsPartOfGesture (0); ap.endGesture(); af.endGesture(); ctx.touched (1);
    }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hint ("Pitch x Formant pad - drag to set both, double-click to reset"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
private:
    struct Tri { juce::Point<float> a, b, c; float alpha; };
    std::vector<Tri> tris; Ctx& ctx; juce::ParameterAttachment ap, af; bool down = false;
};

// ---------------------------------------------------------------- section title with on/off diamond
class SectionTitle : public juce::Component
{
public:
    SectionTitle (Ctx& c, int onPid_, const juce::String& t, const juce::String& sub_, int sec_)
        : ctx (c), onPid (onPid_), title (t), sub (sub_), sec (sec_), att (c.P (onPid_), [this] (float v) { on = v > 0.5f; repaint(); })
    { att.sendInitialUpdate(); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        const float cy = (float) getHeight() * 0.5f;
        const float a = on ? 1.0f : 0.45f;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), ox + 4.5f, cy, juce::Colours::white.withAlpha (0.0f), ox + 17.0f, cy, true));
            g.fillEllipse (ox - 12.5f, cy - 17.0f, 34.0f, 34.0f);
            ui::diamond (g, ox + 4.5f, cy, 9.0f, juce::Colours::white, juce::Colours::white, 0.0f, true);
        }
        else ui::diamond (g, ox + 4.5f, cy, 9.0f, juce::Colours::black, juce::Colour (0xff8a8a8a), 1.2f, false);
        const auto f = Fonts::title (13.0f, 5.0f);
        ui::text (g, title, { ox + 19.0f, 0, 300, (float) getHeight() }, f, juce::Colours::white.withAlpha (a), juce::Justification::centredLeft);
        if (sub.isNotEmpty())
            ui::text (g, sub, { ox + 19.0f + ui::textWidth (f, title) + 5.0f + 6.0f, 0, 300, (float) getHeight() }, Fonts::ui (9.0f, 2.0f), juce::Colour (0xff777777).withAlpha (a), juce::Justification::centredLeft);
    }
    bool hitTest (int x, int) override { return x < (int) ox + 24; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            auto hm = getHostMenu (*this, ctx, onPid);
            auto v = standardEntries (*this, ctx, onPid, {}, hm);
            v.insert (v.begin(), MenuEntry { on ? "Switch &off  (0)" : "Switch &on  (100)", [sp = SafePointer<SectionTitle> (this)] { if (sp != nullptr) { sp->ctx.proc.pushUndo(); sp->ctx.toggleGesture (sp->onPid); } } });
            v.insert (v.begin() + 1, MenuEntry::sep());
            showEntryMenu (*this, std::move (v), hm.all, "Host menu"); return;
        }
        ctx.proc.pushUndo(); ctx.toggleGesture (onPid);
    }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hint (juce::String ("Click the diamond to switch ") + title.toUpperCase() + " on / off (smooth crossfade), right-click = automation clip (0 = off, 100 = on)"); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
    int sec;
    static constexpr float ox = 16.0f;    // left padding inside the component so the glow / diamond are never clipped (the editor shifts the bounds left by this)
private:
    Ctx& ctx; int onPid; juce::String title, sub; bool on = true; juce::ParameterAttachment att;
};

// darkens a whole section when its diamond is off (never intercepts the mouse)
class OffOverlay : public juce::Component
{
public:
    OffOverlay (Ctx& c, int onPid_) : att (c.P (onPid_), [this] (float v) { off = v < 0.5f; setVisible (off); repaint(); }) { setInterceptsMouseClicks (false, false); att.sendInitialUpdate(); }
    void paint (Graphics& g) override { if (off) { g.setColour (juce::Colour (0xff050505).withAlpha (0.58f)); g.fillRect (getLocalBounds()); } }
private:
    bool off = false; juce::ParameterAttachment att;
};

// ---------------------------------------------------------------- small text button
class TextButtonComp : public juce::Component
{
public:
    std::function<void()> onClick;
    juce::String text; bool on = false; bool enabled = true; juce::Colour bg { 0xff161616 }; float font = 11.0f, spacing = 1.0f; juce::String hint; Ctx* ctx = nullptr;
    bool glyphLeft = false, glyphRight = false, dots = false;
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (on ? juce::Colour (0xffeeeeee) : bg); g.fillRect (r);
        g.setColour (on ? juce::Colours::white : juce::Colour (isMouseOver() && enabled ? 0xff888888 : 0xff555555)); g.drawRect (r, 1.0f);
        const auto col = (on ? juce::Colours::black : juce::Colour (0xffcccccc)).withAlpha (enabled ? 1.0f : 0.4f);
        if (glyphLeft || glyphRight)
        {
            const float cx = r.getCentreX(), cy = r.getCentreY();
            juce::Path p; if (glyphLeft) p.addTriangle (cx + 3, cy - 5, cx + 3, cy + 5, cx - 4, cy); else p.addTriangle (cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy);
            g.setColour (col); g.fillPath (p);
        }
        else if (dots) { g.setColour (col); for (int i = -1; i <= 1; ++i) g.fillEllipse (r.getCentreX() + (float) i * 5.0f - 1.3f, r.getCentreY() - 1.3f, 2.6f, 2.6f); }
        else ui::text (g, text, r, Fonts::ui (font, spacing), col);
    }
    void mouseDown (const juce::MouseEvent&) override { if (enabled && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); if (ctx && hint.isNotEmpty()) ctx->hint (hint); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); if (ctx) ctx->hint ({}); }
};

// ---------------------------------------------------------------- preset bar
class PresetBar : public juce::Component, private juce::ChangeListener
{
public:
    PresetBar (Ctx& c, int section_, bool popUp) : ctx (c), sec (section_), up (popUp)
    {
        ctx.proc.presetChanged.addChangeListener (this);
        for (auto* b : { &prev, &next, &save, &more })
        {
            addAndMakeVisible (b); b->ctx = &ctx;
        }
        prev.glyphLeft = true; next.glyphRight = true; save.text = "SAVE"; more.dots = true;
        save.spacing = 1.0f; more.font = 14.0f; save.hint = "Save the current knob settings of this section as a user preset"; more.hint = "More: reset section, delete / reveal user presets";
        prev.hint = "Previous preset"; next.hint = "Next preset";
        prev.onClick = [this] { ctx.proc.stepPreset (sec, -1); };
        next.onClick = [this] { ctx.proc.stepPreset (sec, +1); };
        save.onClick = [this] { beginSave(); };
        more.onClick = [this] { showMore(); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        layout();
    }
    ~PresetBar() override { ctx.proc.presetChanged.removeChangeListener (this); }
    // widths: label right edge | prev 29 | name 250 | next 29 | save 50 | more 30 ; gaps 6
    static constexpr int totalW = 84 + 29 + 6 + 250 + 6 + 29 + 6 + 50 + 6 + 30;
    void resized() override { layout(); }
    void paint (Graphics& g) override
    {
        ui::text (g, "PRESET", { 0, 0, 74, 26 }, Fonts::ui (10.0f, 2.0f), juce::Colour (0xff9a9a9a), juce::Justification::centredRight);
        const auto n = nameBox.toFloat();
        juce::Path p; p.startNewSubPath (n.getX() + 8, n.getY()); p.lineTo (n.getRight(), n.getY()); p.lineTo (n.getRight() - 8, n.getBottom()); p.lineTo (n.getX(), n.getBottom()); p.closeSubPath();
        g.setColour (juce::Colour (0xff050505)); g.fillPath (p);
        g.setColour (juce::Colour (0xff777777)); g.strokePath (p, juce::PathStrokeType (1.0f));
        const int idx = ctx.proc.presetIndex[sec], tot = ctx.proc.presets.numTotal (sec);
        const auto idxTxt = idx >= 0 ? juce::String::formatted ("%02d/%d", idx + 1, tot) : juce::String ("--/") + juce::String (tot);
        ui::text (g, idxTxt, { n.getX() + 16, n.getY(), 48, n.getHeight() }, Fonts::monoF (10.0f), juce::Colour (0xff888888), juce::Justification::centredLeft);
        auto nm = ctx.proc.presetName[sec]; if (nm.isEmpty()) nm = "Custom";
        if (ctx.proc.presetDirty[sec]) nm << " *";
        if (! saving) ui::text (g, nm, { n.getX() + 66, n.getY(), n.getWidth() - 96, n.getHeight() }, Fonts::ui (13.0f), juce::Colours::white, juce::Justification::centredLeft);
        const float cx = n.getRight() - 18, cy = n.getCentreY();
        juce::Path t; if (up) t.addTriangle (cx - 4, cy + 2.5f, cx + 4, cy + 2.5f, cx, cy - 2.5f); else t.addTriangle (cx - 4, cy - 2.5f, cx + 4, cy - 2.5f, cx, cy + 2.5f);
        g.setColour (juce::Colour (0xffaaaaaa)); g.fillPath (t);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (nameBox.contains (e.getPosition())) showList(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (nameBox.contains (e.getPosition()) && w.deltaY != 0) ctx.proc.stepPreset (sec, w.deltaY > 0 ? -1 : 1);
    }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void layout()
    {
        int x = getWidth();
        auto place = [&] (juce::Component& c, int wd) { x -= wd; c.setBounds (x, 0, wd, 26); x -= 6; };
        place (more, 30); place (save, 50); place (next, 29);
        x -= 250; nameBox = { x, 0, 250, 26 }; x -= 6;
        place (prev, 29);
    }
    void showList()
    {
        auto& P = ctx.proc;
        juce::PopupMenu m;
        const int nf = P.presets.numFactory (sec), nt = P.presets.numTotal (sec);
        for (int i = 0; i < nf; ++i) m.addItem (i + 1, juce::String::formatted ("%02d    ", i + 1) + P.presets.nameAt (sec, i), true, i == P.presetIndex[sec]);
        if (nt > nf) { m.addSeparator(); m.addSectionHeader ("USER"); for (int i = nf; i < nt; ++i) m.addItem (i + 1, juce::String::formatted ("%02d    ", i + 1) + P.presets.nameAt (sec, i), true, i == P.presetIndex[sec]); }
        auto o = juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (nameBox)).withMinimumWidth (nameBox.getWidth()).withMaximumNumColumns (3)
                     .withPreferredPopupDirection (up ? juce::PopupMenu::Options::PopupDirection::upwards : juce::PopupMenu::Options::PopupDirection::downwards);
        m.showMenuAsync (o, [sp = SafePointer<PresetBar> (this)] (int r) { if (sp != nullptr && r > 0) sp->ctx.proc.loadPreset (sp->sec, r - 1); });
    }
    void beginSave()
    {
        saving = true; repaint();
        auto init = ctx.proc.presetName[sec]; if (init.isEmpty() || ! ctx.proc.presets.isUser (sec, ctx.proc.presetIndex[sec])) init = "My " + juce::String (sec == 0 ? "Quartz" : "Alter") + " Preset";
        InlineEdit::open (*this, nameBox.reduced (2), init, 13.0f, [sp = SafePointer<PresetBar> (this)] (const juce::String& t, bool ok)
        {
            if (sp == nullptr) return;
            sp->saving = false; sp->repaint();
            if (ok && t.trim().isNotEmpty()) sp->ctx.proc.saveUserPreset (sp->sec, t);
        });
    }
    void showMore()
    {
        auto& P = ctx.proc;
        juce::PopupMenu m;
        m.addItem (1, "Reset section to Init");
        m.addItem (2, "Delete this user preset", P.presets.isUser (sec, P.presetIndex[sec]));
        m.addSeparator();
        m.addItem (3, "Show user presets folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more), [sp = SafePointer<PresetBar> (this)] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) sp->ctx.proc.resetSection (sp->sec);
            else if (r == 2) sp->ctx.proc.deleteCurrentUserPreset (sp->sec);
            else if (r == 3) { auto d = qz::PresetStore::sectionDir (sp->sec); d.createDirectory(); d.revealToUser(); }
        });
    }
    Ctx& ctx; int sec; bool up; bool saving = false;
    TextButtonComp prev, next, save, more; Rectangle<int> nameBox;
};
} // namespace qzui
