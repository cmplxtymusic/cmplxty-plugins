// Quartz 1.0.3 UI + host-interaction tests (Linux, run under xvfb). Real PluginProcessor, real QuartzEditor on a real (X11) window, synthetic mouse / key events.
//   covers: host-automation-then-UI-edit (item 6, all 63 params), double-click reset, right-click menus + underlined letters + keyboard shortcuts,
//   shared clipboard, fake host context (create automation clip), section diamonds, Key/Scale "--" default and old-state load, banner layout.
#include "PluginEditor.h"
#include <cstdio>
#include <map>

namespace qzui { struct TestAccess
{
    static juce::Rectangle<int> valueRect (KnobComp& k) { return k.valueRect(); }
    static bool editing (KnobComp& k) { return k.editing; }
    static bool dragging (KnobComp& k) { return k.dragging; }
    static juce::String valueText (KnobComp& k) { return k.valueText(); }
}; }
using namespace qz;
using qzui::TestAccess;

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static juce::MouseEvent mk (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys m, int clicks = 1, bool dragged = false)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, pos, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, clicks, dragged);
}
static const juce::ModifierKeys L = juce::ModifierKeys::leftButtonModifier, R = juce::ModifierKeys::rightButtonModifier;

template <class T> static void collect (juce::Component& c, std::vector<T*>& out)
{
    for (auto* ch : c.getChildren()) { if (auto* t = dynamic_cast<T*> (ch)) out.push_back (t); collect<T> (*ch, out); }
}

// counts gestures and value changes of every parameter
struct Rec : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};

// host automation write, as the VST3 wrapper does it for a value coming from the host
static void hostWrite (juce::AudioProcessorParameter& p, float norm) { p.setValue (norm); p.sendValueChangedMessageToListeners (norm); }

struct FakeMenu : juce::HostProvidedContextMenu
{
    std::function<void()> onAutomation; mutable int nativeShown = 0;
    juce::PopupMenu getEquivalentPopupMenu() const override
    {
        juce::PopupMenu m;
        m.addItem ("Show in browser", [] {});
        m.addItem ("Create automation clip", [this] { if (onAutomation) onAutomation(); });
        m.addItem ("Link to controller...", [] {});
        return m;
    }
    void showNativeMenu (juce::Point<int>) const override { ++nativeShown; }
};
struct FakeHost : juce::AudioProcessorEditorHostContext
{
    int automationFor = -1; int lastAsked = -1; juce::AudioProcessor* proc = nullptr; bool provide = true;
    std::unique_ptr<juce::HostProvidedContextMenu> getContextMenuForParameter (const juce::AudioProcessorParameter* p) const override
    {
        if (! provide) return nullptr;
        auto fm = std::make_unique<FakeMenu>();
        const int idx = p->getParameterIndex();
        auto* self = const_cast<FakeHost*> (this);
        fm->onAutomation = [self, idx] { self->automationFor = idx; };
        self->lastAsked = idx;
        return fm;
    }
};

static bool menuOpen() { return juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0; }
static void closeMenus() { juce::PopupMenu::dismissAllActiveMenus(); pump (80); }
static void pressKey (int ch)
{
    auto* mw = juce::ModalComponentManager::getInstance()->getModalComponent (0);
    if (mw == nullptr) return;
    if (auto* pe = mw->getPeer()) pe->handleKeyPress (juce::KeyPress (ch)); else mw->keyPressed (juce::KeyPress (ch));
}

#include <execinfo.h>
#include <csignal>
static void segv (int) { void* b[40]; int n = backtrace (b, 40); backtrace_symbols_fd (b, n, 2); _exit (139); }
int main()
{
    std::signal (SIGSEGV, segv);
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    QuartzProcessor proc; proc.setRateAndBufferSizeDetails (48000.0, 512); proc.prepareToPlay (48000.0, 512);

    // ======================= item 4: defaults / "--" ====================================================
    CHECK (juce::roundToInt (proc.getRaw (pKey)) == kKeyOff && juce::roundToInt (proc.getRaw (pScale)) == kScaleOff);
    CHECK (proc.param (pKey)->getCurrentValueAsText() == "--" && proc.param (pScale)->getCurrentValueAsText() == "--");
    CHECK (juce::roundToInt (kParams[pKey].def) == kKeyOff && juce::roundToInt (kParams[pScale].def) == kScaleOff);
    CHECK (juce::String (kParams[pKey].choices).startsWith ("C|C#|D|D#|E|F|F#|G|G#|A|A#|B") && juce::String (kParams[pKey].choices).endsWith ("|--"));
    {   // old 1.0.1 state (key = F#, scale = Minor) must still load unchanged
        QuartzProcessor a; a.setRateAndBufferSizeDetails (48000.0, 512);
        a.param (pKey)->setValueNotifyingHost (a.param (pKey)->convertTo0to1 (6.0f));
        a.param (pScale)->setValueNotifyingHost (a.param (pScale)->convertTo0to1 (2.0f));
        a.param (pPitch)->setValueNotifyingHost (a.param (pPitch)->convertTo0to1 (4.0f));
        juce::MemoryBlock mb; a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        CHECK (xml != nullptr);
        if (xml) { xml->setAttribute ("qzVersion", "1.0.1"); }
        juce::MemoryBlock old; if (xml) juce::AudioProcessor::copyXmlToBinary (*xml, old);
        QuartzProcessor b; b.setRateAndBufferSizeDetails (48000.0, 512);
        b.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (juce::roundToInt (b.getRaw (pKey)) == 6 && juce::roundToInt (b.getRaw (pScale)) == 2 && std::abs (b.getRaw (pPitch) - 4.0f) < 1e-3f);
    }
    {   // a 1.0.1 file written with the OLD 12-key/7-scale tables never has index 12/7; a saved key=0/scale=0 keeps quantising (as 1.0.1 did)
        QuartzProcessor a; a.param (pKey)->setValueNotifyingHost (a.param (pKey)->convertTo0to1 (0.0f)); a.param (pScale)->setValueNotifyingHost (a.param (pScale)->convertTo0to1 (0.0f));
        juce::MemoryBlock mb; a.getStateInformation (mb);
        QuartzProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (juce::roundToInt (b.getRaw (pKey)) == 0 && juce::roundToInt (b.getRaw (pScale)) == 0);
    }
    // presets: those meant to auto-tune keep key/scale, all others use "--"
    {
        int tuned = 0, off = 0;
        for (int i = 0; i < proc.presets.numTotal (1); ++i)
        {
            proc.loadPreset (1, i);
            const bool quantOff = juce::roundToInt (proc.getRaw (pKey)) == kKeyOff || juce::roundToInt (proc.getRaw (pScale)) == kScaleOff;
            const int mode = juce::roundToInt (proc.getRaw (pMode));
            (quantOff ? off : tuned)++;
            if (mode == 1 || mode == 2) CHECKM (! quantOff, "preset %d mode %d lost its key/scale", i, mode);
        }
        std::printf ("  ALTER presets: %d tune (key+scale set), %d use \"--\"\n", tuned, off);
        CHECK (tuned > 0 && off > 0);
    }

    // ======================= item 6: host automation write, then UI edit, on EVERY parameter ================
    {
        QuartzProcessor p2; p2.setRateAndBufferSizeDetails (48000.0, 512); p2.prepareToPlay (48000.0, 512);
        qzui::Ctx c { p2, [] (const juce::String&) {}, {}, {} };
        int bad = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            auto& p = *p2.param (i);
            Rec rec; p.addListener (&rec);
            const float def = p.getDefaultValue(), initLevel = def < 0.5f ? 0.0f : 1.0f;     // "initial value" the automation clip started with
            hostWrite (p, initLevel); hostWrite (p, initLevel);                               // clip played / FL re-applies the init value
            const float target = p.getNumSteps() == 2 ? 1.0f - initLevel : (initLevel < 0.5f ? 0.75f : 0.25f);
            // knob drag as the editor does it
            juce::ParameterAttachment att (p, [] (float) {}); att.beginGesture(); att.setValueAsPartOfGesture (p.convertFrom0to1 (target)); att.endGesture();
            const float got = p.getValue();
            const float want = p.convertTo0to1 (p.convertFrom0to1 (target));    // discrete params snap
            if (std::abs (got - want) > 1e-4f || std::abs (p2.getRaw (i) - p.convertFrom0to1 (got)) > 1e-3f) { ++bad; std::printf ("  not followed: %s got %.4f want %.4f raw %.3f\n", kParams[i].id, got, want, p2.getRaw (i)); }
            if (rec.begins != 1 || rec.ends != 1) { ++bad; std::printf ("  gesture imbalance on %s: %d/%d\n", kParams[i].id, rec.begins, rec.ends); }
            // double-click reset + menu reset + paste are single begin/end gestures too
            rec = {}; c.resetGesture (i);
            if (rec.begins != 1 || rec.ends != 1 || std::abs (p.getValue() - def) > 1e-4f) { ++bad; std::printf ("  reset failed on %s\n", kParams[i].id); }
            // host writes AGAIN (new playback start), then a UI toggle must still take effect, and the plugin must not touch the value by itself
            hostWrite (p, initLevel);
            juce::AudioBuffer<float> buf (2, 512); buf.clear(); juce::MidiBuffer mb; p2.processBlock (buf, mb);
            if (std::abs (p.getValue() - initLevel) > 1e-4f) { ++bad; std::printf ("  plugin changed %s during processBlock\n", kParams[i].id); }
            c.setNormGesture (i, target);
            if (std::abs (p.getValue() - want) > 1e-4f) { ++bad; std::printf ("  UI edit after host write ignored on %s\n", kParams[i].id); }
            p.removeListener (&rec);
        }
        CHECKM (bad == 0, "%d parameter problems", bad);
        std::printf ("  host-write -> UI-edit checked on %d parameters\n", (int) kNumParams);
    }

    // ======================= editor on a real window ====================================================
    std::unique_ptr<QuartzEditor> ed (static_cast<QuartzEditor*> (proc.createEditor()));
    ed->enableDemo (true);                                   // no network from the test editor
    FakeHost host; ed->setHostContext (&host);
    ed->addToDesktop (0); ed->setVisible (true); ed->setTopLeftPosition (20, 20);
    pump (200);
    std::vector<qzui::KnobComp*> knobs; collect (*ed, knobs);
    auto knobFor = [&] (int pid) -> qzui::KnobComp* { for (auto* k : knobs) if (k->paramId() == pid) return k; return nullptr; };
    CHECK (knobFor (pGate) && knobFor (pGender) && knobFor (pPitch) && knobFor (pCNote) && knobFor (pIntensity));
    std::vector<qzui::SectionTitle*> titles; collect (*ed, titles);
    std::vector<qzui::DropComp*> drops; collect (*ed, drops);
    CHECK (titles.size() == 2 && drops.size() == 2);
    // section titles: the whole glow / diamond must be inside the component (item 5)
    for (auto* t : titles) { auto pb = t->getBoundsInParent(); CHECK (pb.getX() >= -16); CHECK (qzui::SectionTitle::ox >= 16.0f); }

    auto dblClick = [&] (juce::Component& k, juce::Point<float> pos)
    {
        k.mouseDown (mk (k, pos, L, 1)); k.mouseUp (mk (k, pos, L, 1));
        k.mouseDown (mk (k, pos, L, 2)); k.mouseUp (mk (k, pos, L, 2)); k.mouseDoubleClick (mk (k, pos, L, 2));
    };

    // ======================= item 1: double-click resets ===============================================
    for (int pid : { pGate, pDrive, pPitch, pGender, pADrive, pMix, pIntensity })
    {
        auto* k = knobFor (pid); auto& p = *proc.param (pid);
        p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p.getDefaultValue() < 0.5f ? 0.8f : 0.2f));
        Rec rec; p.addListener (&rec);
        const auto pos = pid == pIntensity ? juce::Point<float> (196.0f, 120.0f) : juce::Point<float> ((float) k->getWidth() * 0.5f, 10.0f);
        dblClick (*k, pos);
        CHECKM (std::abs (p.getValue() - p.getDefaultValue()) < 1e-4f, "double-click did not reset %s (%.3f)", kParams[pid].id, p.getValue());
        CHECKM (rec.begins == rec.ends && rec.begins >= 1, "gestures %d/%d for %s", rec.begins, rec.ends, kParams[pid].id);
        CHECK (! TestAccess::dragging (*k)); CHECK (! TestAccess::editing (*k));
        // and the knob still drags afterwards
        k->mouseDown (mk (*k, pos, L)); const float dy = p.getDefaultValue() > 0.5f ? 40.0f : -40.0f; k->mouseDrag (mk (*k, pos + juce::Point<float> (0, dy), L, 1, true)); k->mouseUp (mk (*k, pos + juce::Point<float> (0, dy), L, 1, true));
        CHECKM (std::abs (p.getValue() - p.getDefaultValue()) > 0.05f, "drag after reset did nothing on %s", kParams[pid].id);
        CHECK (TestAccess::valueText (*k).isNotEmpty());
        p.removeListener (&rec);
    }
    std::printf ("  [step] double-click reset done\n");
    {   // double-click on the VALUE types a number (does not reset)
        auto* k = knobFor (pGate); auto& p = *proc.param (pGate);
        p.setValueNotifyingHost (0.6f);
        const auto vr = TestAccess::valueRect (*k).getCentre().toFloat();
        dblClick (*k, vr);
        CHECK (TestAccess::editing (*k)); CHECKM (std::abs (p.getValue() - 0.6f) < 1e-4f, "value double-click changed the value");
        std::vector<juce::TextEditor*> eds; collect (*k, eds); CHECK (eds.size() == 1);
        if (! eds.empty()) { eds[0]->setText ("-37", false); eds[0]->onReturnKey(); }
        pump (100);
        CHECK (! TestAccess::editing (*k));
        CHECKM (std::abs (p.convertFrom0to1 (p.getValue()) - -37.0f) < 0.6f, "typed value gave %f", p.convertFrom0to1 (p.getValue()));
    }
    std::printf ("  [step] value typing done\n");
    {   // carrier note: double-click on its value is ignored (single click cycles the shape)
        auto* k = knobFor (pCNote); const auto before = proc.param (pCNote)->getValue();
        dblClick (*k, TestAccess::valueRect (*k).getCentre().toFloat());
        CHECK (std::abs (proc.param (pCNote)->getValue() - before) < 1e-5f);
    }
    std::printf ("  [step] carrier dblclick done\n");
    {   // ctrl-click still resets, bypass icon not hijacked by double-click
        auto* k = knobFor (pTone); auto& p = *proc.param (pTone); p.setValueNotifyingHost (0.9f);
        k->mouseDown (mk (*k, { 40, 30 }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier)));
        CHECK (std::abs (p.getValue() - p.getDefaultValue()) < 1e-4f);
    }

    // ======================= item 2: right-click menu, underlined letters, keys, shared clipboard =======
    std::printf ("  [step] ctrl-click done\n");
    auto openMenu = [&] (juce::Component& c, juce::Point<float> pos) { c.mouseDown (mk (c, pos, R)); pump (250); };
    {
        auto* k = knobFor (pGate); auto& pg = *proc.param (pGate);
        qzui::ValueClip::valid = false;
        pg.setValueNotifyingHost (0.35f);
        openMenu (*k, { 40, 30 });
        CHECK (menuOpen());
        pressKey ('c'); pump (250);
        CHECK (! menuOpen());
        CHECK (qzui::ValueClip::valid && std::abs (qzui::ValueClip::norm - 0.35f) < 1e-4f);
        std::printf ("  [step] copy ok\n");
        // paste onto a different knob
        auto* k2 = knobFor (pDrive); auto& pd = *proc.param (pDrive); pd.setValueNotifyingHost (0.9f);
        Rec rec; pd.addListener (&rec);
        openMenu (*k2, { 40, 30 }); CHECK (menuOpen());
        pressKey ('V'); pump (250);
        CHECKM (std::abs (pd.getValue() - 0.35f) < 1e-4f, "paste gave %.3f", pd.getValue()); CHECK (rec.begins == 1 && rec.ends == 1);
        std::printf ("  [step] paste ok\n");
        // reset with D
        openMenu (*k2, { 40, 30 }); pressKey ('d'); pump (250);
        CHECK (std::abs (pd.getValue() - pd.getDefaultValue()) < 1e-4f);
        std::printf ("  [step] reset ok\n");
        // Enter value with E opens the editor
        openMenu (*k2, { 40, 30 }); pressKey ('e'); pump (250);
        CHECK (TestAccess::editing (*k2)); closeMenus();
        if (TestAccess::editing (*k2)) { std::vector<juce::TextEditor*> eds; collect (*k2, eds); if (! eds.empty()) eds[0]->onEscapeKey(); pump (100); }
        std::printf ("  [step] enter ok\n");
        // Bypass this knob with B
        auto& bp = *proc.param (bDrive); bp.setValueNotifyingHost (0.0f);
        openMenu (*k2, { 40, 30 }); pressKey ('b'); pump (250);
        CHECK (bp.getValue() > 0.5f);
        openMenu (*k2, { 40, 30 }); pressKey ('b'); pump (250);
        CHECK (bp.getValue() < 0.5f);
        // an unrelated letter does nothing and leaves the menu open; Escape closes
        openMenu (*k2, { 40, 30 }); pressKey ('z'); pump (100); CHECK (menuOpen()); closeMenus(); CHECK (! menuOpen());
        // clipboard is shared with another editor instance (process-wide static)
        CHECK (qzui::ValueClip::valid.load());
        pd.removeListener (&rec);
        // CARRIER: shape entries by letter
        auto* kc = knobFor (pCNote); proc.param (pCShape)->setValueNotifyingHost (proc.param (pCShape)->convertTo0to1 (0.0f));
        openMenu (*kc, { 40, 30 }); pressKey ('N'); pump (250);
        CHECK (juce::roundToInt (proc.getRaw (pCShape)) == 2);
        openMenu (*kc, { 40, 30 }); pressKey ('q'); pump (250);
        CHECK (juce::roundToInt (proc.getRaw (pCShape)) == 1);
    }

    // ---- create automation clip via the host's context menu
    {
        auto* k = knobFor (pGate); host.automationFor = -1; host.lastAsked = -1;
        openMenu (*k, { 40, 30 }); CHECK (host.lastAsked == proc.param (pGate)->getParameterIndex());
        pressKey ('a'); pump (300);
        CHECKM (host.automationFor == proc.param (pGate)->getParameterIndex(), "host automation item not triggered (%d)", host.automationFor);
        // via mouse selection too: the menu item exists and is enabled
        // section diamonds: right-click -> same
        for (int t = 0; t < 2; ++t)
        {
            host.automationFor = -1; auto* st = titles[(size_t) t];
            openMenu (*st, { 20, 20 }); CHECK (menuOpen());
            pressKey ('A'); pump (300);
            CHECKM (host.automationFor == proc.param (t == 0 ? pQon : pAon)->getParameterIndex(), "diamond %d automation", t);
            // right-click must NOT toggle the section
            CHECK (proc.getRaw (t == 0 ? pQon : pAon) > 0.5f);
            openMenu (*st, { 20, 20 }); pressKey ('o'); pump (250);          // "Switch &off"
            CHECK (proc.getRaw (t == 0 ? pQon : pAon) < 0.5f);
            openMenu (*st, { 20, 20 }); pressKey ('o'); pump (250);          // "Switch &on"
            CHECK (proc.getRaw (t == 0 ? pQon : pAon) > 0.5f);
            // left click toggles, as before
            st->mouseDown (mk (*st, { 20, 20 }, L)); st->mouseUp (mk (*st, { 20, 20 }, L)); CHECK (proc.getRaw (t == 0 ? pQon : pAon) < 0.5f);
            st->mouseDown (mk (*st, { 20, 20 }, L)); st->mouseUp (mk (*st, { 20, 20 }, L)); CHECK (proc.getRaw (t == 0 ? pQon : pAon) > 0.5f);
        }
        // toggle params are ordinary host-visible 0/1 params
        for (int pid : { pQon, pAon, pBypass, pLink })
        {
            auto* p = proc.param (pid);
            CHECK (p->isAutomatable() && ! p->isMetaParameter() && p->getNumSteps() == 2);
            CHECK (p->getParameterIndex() >= 0);
        }
        // dropdown + LINK + segment groups
        for (auto* d : drops) { host.automationFor = -1; openMenu (*d, { 20, 10 }); pressKey ('a'); pump (300); CHECK (host.automationFor >= 0); }
        // no host menu available -> entry disabled, nothing crashes
        host.provide = false; host.automationFor = -1;
        openMenu (*knobFor (pGate), { 40, 30 }); pressKey ('a'); pump (200); CHECK (host.automationFor == -1); CHECK (menuOpen()); closeMenus();
        host.provide = true;
    }

    // ---- Key / Scale dropdown shows "--" first and it is the default
    {
        auto names = juce::StringArray::fromTokens (juce::String (kParams[pKey].choices), "|", "");
        CHECK (names[kKeyOff] == "--" && names.size() == 13);
        auto sn = juce::StringArray::fromTokens (juce::String (kParams[pScale].choices), "|", "");
        CHECK (sn[kScaleOff] == "--" && sn.size() == 8);
    }

    // ---- banner layout / URL safety
    {
        auto* b = ed->updateBanner(); CHECK (b != nullptr && ! b->isVisible());
        qz::upd::Result r; r.ok = true; r.info = { "9.9.9", "https://cmplxty.music/quartz-engine/", "https://cmplxty.music/quartz-engine/Quartz-Setup.exe", "notes" };
        juce::String opened; b->openUrl = [&] (const juce::String& u) { opened = u; };
        ed->showUpdateNotice (r); CHECK (b->isVisible());
        const auto bb = b->getBounds(); CHECK (bb.getX() > 330 && bb.getRight() <= 850 && bb.getHeight() == qzui::UpdateBanner::kH);
        b->update.onClick(); CHECK (opened == "https://cmplxty.music/quartz-engine/"); CHECK (! b->isVisible());
        b->openUrl = [&] (const juce::String& u) { opened = u; }; opened = {};
        auto bad = r; bad.info.url = "https://evil.example/x"; // parseInfo would already reject it; the button must refuse too
        ed->showUpdateNotice (bad); b->update.onClick(); CHECK (opened.isEmpty());
        std::printf ("  banner bounds x=%d..%d w=%d\n", bb.getX(), bb.getRight(), bb.getWidth());
    }

    // ======================= 1.0.3: the DAW keeps its keyboard (Space = play / pause) ==================
    {
        auto* pe = ed->getPeer(); CHECK (pe != nullptr);
        // 1. nothing in the editor asks for the keyboard (the inline value box is a TextEditor that only exists while typing)
        std::vector<juce::Component*> all; std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* ch : c.getChildren()) { all.push_back (ch); walk (*ch); } };
        walk (*ed); int wanting = 0; for (auto* c : all) if (c->getWantsKeyboardFocus() && dynamic_cast<juce::TextEditor*> (c) == nullptr) ++wanting;
        CHECKM (wanting == 0 && ! ed->getWantsKeyboardFocus(), "%d components still want keyboard focus", wanting);
        // 2. editor swallows none of the host's transport keys
        for (int key : { (int) juce::KeyPress::spaceKey, (int) juce::KeyPress::returnKey, (int) juce::KeyPress::tabKey, (int) juce::KeyPress::escapeKey, (int) juce::KeyPress::F1Key,
                         (int) juce::KeyPress::leftKey, (int) juce::KeyPress::homeKey, (int) 'r', (int) 'l', (int) '1' })
            CHECKM (! ed->keyPressed (juce::KeyPress (key)), "editor consumed key %d", key);
        // 3. a real click / drag / right-click through JUCE's own mouse + focus code must not leave keyboard focus in the editor
        auto* k = knobFor (pGate); const auto centre = pe->globalToLocal (k->localPointToGlobal (juce::Point<float> (40.0f, 30.0f)));
        auto mouse = [&] (juce::ModifierKeys m, juce::Point<float> pos) { pe->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, m, 1.0f, 0.0f, juce::Time::currentTimeMillis()); pump (30); };
        const auto before = proc.param (pGate)->getValue();
        mouse (L, centre); mouse (L, centre - juce::Point<float> (0, 25)); mouse ({}, centre - juce::Point<float> (0, 25)); pump (80);
        CHECK (std::abs (proc.param (pGate)->getValue() - before) > 1e-3f);                       // the click really reached the knob
        CHECKM (juce::Component::getCurrentlyFocusedComponent() == nullptr, "a knob drag left keyboard focus on a component");
        // control: the old behaviour (editor wants focus) does grab it on the same click, so this test would have caught 1.0.2
        ed->setWantsKeyboardFocus (true); ed->setMouseClickGrabsKeyboardFocus (true); mouse (L, centre); mouse ({}, centre); pump (80);
        const bool controlFocused = juce::Component::getCurrentlyFocusedComponent() != nullptr;
        ed->setWantsKeyboardFocus (false); ed->setMouseClickGrabsKeyboardFocus (false); juce::Component::unfocusAllComponents(); pump (50);
        CHECKM (controlFocused, "test control: click on a focus-wanting editor should take focus");
        CHECK (juce::Component::getCurrentlyFocusedComponent() == nullptr);
        // 4. right-click menu takes the keyboard only while open, shortcut letters work, then nothing keeps focus
        proc.param (pGate)->setValueNotifyingHost (0.5f);
        openMenu (*k, { 40, 30 });
        CHECK (menuOpen()); CHECK (ed->getWantsKeyboardFocus());
        pressKey ('d'); pump (300);
        CHECK (! menuOpen()); CHECK (std::abs (proc.param (pGate)->getValue() - proc.param (pGate)->getDefaultValue()) < 1e-4f);
        CHECKM (! ed->getWantsKeyboardFocus() && juce::Component::getCurrentlyFocusedComponent() == nullptr, "menu left keyboard focus behind (shortcut path)");
        openMenu (*k, { 40, 30 }); CHECK (menuOpen()); closeMenus(); pump (100);
        CHECKM (! ed->getWantsKeyboardFocus() && juce::Component::getCurrentlyFocusedComponent() == nullptr, "menu left keyboard focus behind (dismiss path)");
        // 5. "Enter value": text box has focus while typing, none after Return
        openMenu (*k, { 40, 30 }); pressKey ('e'); pump (300);
        CHECK (dynamic_cast<juce::TextInputTarget*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr);
        if (auto* te = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent())) { te->setText ("-30"); te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); }
        pump (300);
        CHECKM (juce::Component::getCurrentlyFocusedComponent() == nullptr && ! ed->getWantsKeyboardFocus(), "typed value left keyboard focus behind");
        // 6. the Key / Scale dropdowns (plain click opens a menu)
        std::vector<qzui::DropComp*> dd; collect (*ed, dd);
        for (auto* d : dd) { d->mouseDown (mk (*d, { 20, 10 }, L)); pump (200); const bool open = menuOpen(); closeMenus(); pump (100);
            CHECKM (open && ! ed->getWantsKeyboardFocus() && juce::Component::getCurrentlyFocusedComponent() == nullptr, "dropdown left keyboard focus behind"); }
        std::printf ("  keyboard audit: %d components checked, %d dropdowns\n", (int) all.size(), (int) dd.size());
    }

    ed.reset(); pump (50);
    std::printf ("ui_test: %d checks, %d failures\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
