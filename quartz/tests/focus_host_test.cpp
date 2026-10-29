// Quartz 1.0.3 keyboard-focus test (Windows VST3 host, cross-compiled with mingw; runs under wine or on real Windows).
// Reproduces "I touched a knob and Space no longer plays/pauses": the host window that has the keyboard focus must STILL have it after
// the plug-in editor was clicked / dragged / right-clicked, and a Space keystroke sent to the focused window must reach the host.
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
using namespace Steinberg; using namespace Steinberg::Vst;


DEF_CLASS_IID(IComponent) DEF_CLASS_IID(IAudioProcessor) DEF_CLASS_IID(IEditController) DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame) DEF_CLASS_IID(IComponentHandler) DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IHostApplication) DEF_CLASS_IID(FUnknown)

static int fails = 0;
#define CHECK(c, ...) do { if(!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while(0)

struct Host : IHostApplication, IComponentHandler {
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) || FUnknownPrivate::iidEqual(_iid, IHostApplication::iid)) { *obj = (IHostApplication*)this; return kResultOk; }
        if (FUnknownPrivate::iidEqual(_iid, IComponentHandler::iid)) { *obj = (IComponentHandler*)this; return kResultOk; }
        *obj = nullptr; return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; } uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getName(String128 name) override { const char* n = "QuartzTestHost"; for (int i = 0; i < 15; ++i) name[i] = (TChar)n[i]; name[15] = 0; return kResultOk; }
    tresult PLUGIN_API createInstance(TUID, TUID, void** obj) override { *obj = nullptr; return kNotImplemented; }
    int begins = 0, ends = 0, performs = 0;
    tresult PLUGIN_API beginEdit(ParamID) override { ++begins; return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { ++performs; return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { ++ends; return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

struct Frame : IPlugFrame {
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override { if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) || FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid)) { *obj = this; return kResultOk; } *obj = nullptr; return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; } uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API resizeView(IPlugView* v, ViewRect* r) override { v->onSize(r); return kResultOk; }
};

typedef IPluginFactory* (*GetFactoryFn)();
typedef bool (*InitFn)();

static bool finiteBuf(const std::vector<float>& b, float& peak) { peak = 0; for (float f : b) { if (!std::isfinite(f)) return false; peak = std::max(peak, std::fabs(f)); } return true; }

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: host Quartz.vst3 [shot.bmp]\n"); return 2; }
    HMODULE m = LoadLibraryA(argv[1]);
    CHECK(m != nullptr, "LoadLibrary (err %lu)", GetLastError());
    if (!m) return 1;
    auto initDll = (InitFn)GetProcAddress(m, "InitDll");
    auto getF = (GetFactoryFn)GetProcAddress(m, "GetPluginFactory");
    CHECK(getF != nullptr, "GetPluginFactory exported");
    CHECK(GetProcAddress(m, "ExitDll") != nullptr, "ExitDll exported");
    if (initDll) CHECK(initDll(), "InitDll");
    IPluginFactory* f = getF();
    PFactoryInfo fi; f->getFactoryInfo(&fi);
    printf("vendor=%s url=%s\n", fi.vendor, fi.url);
    int n = f->countClasses();
    CHECK(n >= 1, "classes=%d", n);
    TUID compCid{}; bool haveComp = false;
    for (int i = 0; i < n; i++) { PClassInfo ci; f->getClassInfo(i, &ci); printf("  class[%d] %s / %s\n", i, ci.category, ci.name); if (!strcmp(ci.category, "Audio Module Class")) { memcpy(compCid, ci.cid, 16); haveComp = true; } }
    CHECK(haveComp, "has Audio Module Class");
    Host host; Frame frame;
    IComponent* comp = nullptr;
    CHECK(f->createInstance(compCid, IComponent::iid, (void**)&comp) == kResultOk && comp, "createInstance IComponent");
    if (!comp) return 1;
    comp->initialize((IHostApplication*)&host);
    IEditController* ctl = nullptr;
    if (comp->queryInterface(IEditController::iid, (void**)&ctl) != kResultOk || !ctl) {
        TUID ctlCid; if (comp->getControllerClassId(ctlCid) == kResultOk) f->createInstance(ctlCid, IEditController::iid, (void**)&ctl);
        if (ctl) { ctl->initialize((IHostApplication*)&host);
            IConnectionPoint *a = nullptr, *b = nullptr; comp->queryInterface(IConnectionPoint::iid, (void**)&a); ctl->queryInterface(IConnectionPoint::iid, (void**)&b); if (a && b) { a->connect(b); b->connect(a); } }
    }
    CHECK(ctl != nullptr, "IEditController");
    ctl->setComponentHandler(&host);

    // the "host": a top-level window holding a transport-ish child control (like FL's own panels). Both count the keys they receive.
    static int hostKeys = 0, ctlKeys = 0, ctlChars = 0;
    WNDCLASSA wc{}; wc.hInstance = GetModuleHandleA(0);
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { if (m == WM_KEYDOWN && w == VK_SPACE) hostKeys++; return DefWindowProcA(h, m, w, l); };
    wc.lpszClassName = "QTHost"; RegisterClassA(&wc);
    WNDCLASSA wc2{}; wc2.hInstance = wc.hInstance; wc2.lpszClassName = "QTHostCtl";
    wc2.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT { if (m == WM_KEYDOWN && w == VK_SPACE) ctlKeys++; if (m == WM_CHAR && w == ' ') ctlChars++; return DefWindowProcA(h, m, w, l); };
    RegisterClassA(&wc2);

    IPlugView* view = ctl->createView("editor");
    CHECK(view != nullptr, "createView(editor)");
    if (!view) return 1;
    ViewRect r{}; view->getSize(&r);
    HWND hw = CreateWindowA("QTHost", "Quartz focus test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, r.right - r.left + 60, r.bottom - r.top + 80, 0, 0, wc.hInstance, 0);
    HWND hostCtl = CreateWindowA("QTHostCtl", "", WS_CHILD | WS_VISIBLE, r.right - r.left + 10, 5, 40, 40, hw, 0, wc.hInstance, 0);
    view->setFrame(&frame);
    CHECK(view->attached(hw, "HWND") == kResultOk, "view attached");
    auto pump = [&](int ms) { DWORD s0 = GetTickCount(); MSG m2; while (GetTickCount() - s0 < (DWORD)ms) { while (PeekMessageA(&m2, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m2); DispatchMessageA(&m2); } Sleep(5); } };
    pump(2000);
    HWND tgt = nullptr; for (HWND c = GetWindow(hw, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) if (c != hostCtl) tgt = c;
    CHECK(tgt != nullptr, "found the plug-in's child window");
    auto name = [](HWND h) { static char b[4][128]; static int i = 0; i = (i + 1) & 3; if (!h) return "(none)"; GetClassNameA(h, b[i], 100); return (const char*) b[i]; };
    auto inPlugin = [&](HWND h) { return h && (h == tgt || IsChild(tgt, h)); };

    // argv[2] present: the host keeps the focus on its own top-level window instead of a child control
    HWND focusWin = argc > 2 ? hw : hostCtl; auto keysSeen = [&]() { return argc > 2 ? hostKeys : ctlKeys; };
    SetForegroundWindow(hw); SetActiveWindow(hw); SetFocus(focusWin); pump(100);
    CHECK(GetFocus() == focusWin, "setup: host control has the keyboard focus (is %s)", name(GetFocus()));
    auto spaceToFocus = [&]() { HWND f = GetFocus(); if (!f) return; SendMessageA(f, WM_KEYDOWN, VK_SPACE, 0x00390001); SendMessageA(f, WM_CHAR, ' ', 0x00390001); SendMessageA(f, WM_KEYUP, VK_SPACE, 0xC0390001); pump(20); };
    // what Windows does with a key: it goes to the focused window; whatever that window does not handle is its own business.
    int expectKeys = 0;
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "setup: Space reaches the host control (%d)", keysSeen());

    // find the GATE knob parameter to check that gestures really reach the host
    ParamID gate = 0; bool haveGate = false;
    for (int p = 0; p < ctl->getParameterCount(); p++) { ParameterInfo pi; ctl->getParameterInfo(p, pi); char nm[64]; for (int i = 0; i < 63; i++) { nm[i] = (char)pi.title[i]; if (!pi.title[i]) break; } nm[63] = 0; if (strstr(nm, "Gate")) { gate = pi.id; haveGate = true; break; } }
    (void) haveGate;
    ParameterInfo gpi{}; for (int p = 0; p < ctl->getParameterCount(); p++) { ParameterInfo pi; ctl->getParameterInfo(p, pi); if (pi.id == gate) gpi = pi; }
    const LPARAM knob = MAKELPARAM(78, 134);

    // 1. plain click on a knob
    SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, knob); pump(30); SendMessageA(tgt, WM_LBUTTONUP, 0, knob); pump(100);
    CHECK(!inPlugin(GetFocus()), "after a knob click the plug-in does NOT hold the keyboard focus (focus: %s)", name(GetFocus()));
    CHECK(GetFocus() == focusWin, "after a knob click the host control still has the focus");
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "after a knob click Space reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 2. knob drag
    int b0 = host.begins;
    SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(78, 134)); pump(30);
    for (int i = 1; i <= 10; i++) { SendMessageA(tgt, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(78, 134 - i * 6)); pump(10); }
    SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(78, 74)); pump(100);
    CHECK(host.begins > b0, "drag reached the host as an edit gesture");
    CHECK(GetFocus() == focusWin, "after a knob drag the host control still has the focus (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "after a knob drag Space reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 3. double click (reset to default) and a click on empty background
    SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, knob); SendMessageA(tgt, WM_LBUTTONUP, 0, knob); SendMessageA(tgt, WM_LBUTTONDBLCLK, MK_LBUTTON, knob); SendMessageA(tgt, WM_LBUTTONUP, 0, knob); pump(100);
    SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(540, 700)); SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(540, 700)); pump(100);
    CHECK(GetFocus() == focusWin, "after double-click and a background click the host control still has the focus (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "Space still reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 4. right-click menu: shortcut letter must still work, and the focus must go back to the host afterwards
    //    first drag the knob away from its default so that "D" (reset to default) visibly does something
    SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, knob); for (int i = 1; i <= 10; i++) { SendMessageA(tgt, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(78, 134 - i * 6)); pump(10); } SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(78, 74)); pump(80);
    double moved = ctl->getParamNormalized(gate);
    SendMessageA(tgt, WM_RBUTTONDOWN, MK_RBUTTON, knob); pump(100);
    printf("  menu open: focus is %s, plug-in holds it: %d\n", name(GetFocus()), (int) inPlugin(GetFocus()));
    CHECK(inPlugin(GetFocus()), "while the menu is open the plug-in holds the keyboard (so its shortcut letters work)");
    { HWND fw = GetFocus(); if (fw) { SendMessageA(fw, WM_KEYDOWN, 'D', 0x00200001); SendMessageA(fw, WM_CHAR, 'd', 0x00200001); SendMessageA(fw, WM_KEYUP, 'D', 0xC0200001); } }
    pump(300);
    SendMessageA(tgt, WM_RBUTTONUP, 0, knob); pump(200);
    double afterD = ctl->getParamNormalized(gate);
    printf("  gate value: moved %.3f, after menu shortcut D %.3f (default %.3f)\n", moved, afterD, gpi.defaultNormalizedValue);
    CHECK(haveGate && std::fabs(afterD - gpi.defaultNormalizedValue) < 1e-3 && std::fabs(moved - afterD) > 1e-3, "menu shortcut letter D (reset to default) worked");
    CHECK(GetFocus() == focusWin, "after the menu closed the host control has the focus again (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "after the right-click menu Space reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 5. right-click menu closed with Escape
    SendMessageA(tgt, WM_RBUTTONDOWN, MK_RBUTTON, knob); SendMessageA(tgt, WM_RBUTTONUP, 0, knob); pump(150);
    { HWND fw = GetFocus(); if (fw) { SendMessageA(fw, WM_KEYDOWN, VK_ESCAPE, 0x00010001); SendMessageA(fw, WM_KEYUP, VK_ESCAPE, 0xC0010001); } }
    pump(300);
    CHECK(GetFocus() == focusWin, "after Escape closes the menu the host control has the focus (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "after Escape Space reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 6. type a value: right-click, E (enter value), type 7, Return
    int bBefore = host.begins;
    SendMessageA(tgt, WM_RBUTTONDOWN, MK_RBUTTON, knob); SendMessageA(tgt, WM_RBUTTONUP, 0, knob); pump(150);
    { HWND fw = GetFocus(); if (fw) { SendMessageA(fw, WM_KEYDOWN, 'E', 0x00120001); SendMessageA(fw, WM_CHAR, 'e', 0x00120001); SendMessageA(fw, WM_KEYUP, 'E', 0xC0120001); } }
    pump(500);
    printf("  value box: focus is %s, plug-in holds it: %d\n", name(GetFocus()), (int) inPlugin(GetFocus()));
    CHECK(inPlugin(GetFocus()), "while typing a value the plug-in holds the keyboard");
    { HWND fw = GetFocus(); if (fw) { SendMessageA(fw, WM_KEYDOWN, '7', 0x00080001); SendMessageA(fw, WM_CHAR, '7', 0x00080001); SendMessageA(fw, WM_KEYUP, '7', 0xC0080001); pump(50);
      SendMessageA(fw, WM_KEYDOWN, VK_RETURN, 0x001C0001); SendMessageA(fw, WM_CHAR, 13, 0x001C0001); SendMessageA(fw, WM_KEYUP, VK_RETURN, 0xC01C0001); } }
    pump(500);
    printf("  typed value: gate now %.3f, host begins +%d\n", ctl->getParamNormalized(gate), host.begins - bBefore);
    CHECK(host.begins > bBefore, "typed value reached the host as an edit gesture");
    CHECK(GetFocus() == focusWin, "after typing a value the host control has the focus again (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "after typing a value Space reaches the host (%d/%d)", keysSeen(), expectKeys);

    // 7. dropdown (Key / Scale style) - click in several header areas, nothing may keep the keyboard
    for (int x : { 200, 420, 700, 900 }) { SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 30)); SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(x, 30)); pump(150); if (GetFocus() != hostCtl) { HWND fw = GetFocus(); if (fw) { SendMessageA(fw, WM_KEYDOWN, VK_ESCAPE, 0x00010001); SendMessageA(fw, WM_KEYUP, VK_ESCAPE, 0xC0010001); pump(200); } } }
    CHECK(GetFocus() == focusWin, "after clicking around the header the host control has the focus (focus: %s)", name(GetFocus()));
    spaceToFocus(); expectKeys++;
    CHECK(keysSeen() == expectKeys, "Space reaches the host at the end (%d/%d)", keysSeen(), expectKeys);

    view->removed(); view->setFrame(nullptr); view->release(); DestroyWindow(hw);
    ctl->setComponentHandler(nullptr); comp->terminate();
    auto exitDll = (InitFn)GetProcAddress(m, "ExitDll"); if (exitDll) exitDll();
    printf("\nRESULT: %s (%d failures)\n", fails ? "FAIL" : "ALL OK", fails);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), fails ? 1 : 0);
    return fails ? 1 : 0;
}
