// Minimal Windows VST3 test host (cross-compiled with mingw, runs under wine or on Windows).
// Loads Quartz.vst3, enumerates classes, runs audio blocks, sweeps every parameter, creates the editor in a real HWND.
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
DEF_CLASS_IID(IHostApplication) 

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
    CHECK(comp->initialize((IHostApplication*)&host) == kResultOk, "component initialize");
    IAudioProcessor* proc = nullptr;
    CHECK(comp->queryInterface(IAudioProcessor::iid, (void**)&proc) == kResultOk && proc, "IAudioProcessor");
    IEditController* ctl = nullptr;
    if (comp->queryInterface(IEditController::iid, (void**)&ctl) != kResultOk || !ctl) {
        TUID ctlCid; if (comp->getControllerClassId(ctlCid) == kResultOk) f->createInstance(ctlCid, IEditController::iid, (void**)&ctl);
        if (ctl) { ctl->initialize((IHostApplication*)&host);
            IConnectionPoint *a = nullptr, *b = nullptr; comp->queryInterface(IConnectionPoint::iid, (void**)&a); ctl->queryInterface(IConnectionPoint::iid, (void**)&b); if (a && b) { a->connect(b); b->connect(a); } }
    }
    CHECK(ctl != nullptr, "IEditController");
    ctl->setComponentHandler(&host);

    SpeakerArrangement st = SpeakerArr::kStereo;
    CHECK(proc->setBusArrangements(&st, 1, &st, 1) == kResultOk, "setBusArrangements stereo");
    comp->activateBus(kAudio, kInput, 0, true); comp->activateBus(kAudio, kOutput, 0, true);
    int ok = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 }) {
        ProcessSetup ps{ kRealtime, kSample32, 512, fs };
        CHECK(proc->setupProcessing(ps) == kResultOk, "setupProcessing %.0f", fs);
        comp->setActive(true); proc->setProcessing(true);
        uint32 lat = proc->getLatencySamples();
        printf("  latency %u samples (%.2f ms)\n", lat, 1000.0 * lat / fs);
        CHECK(lat > 0 && lat < fs * 0.2, "latency sane");
        std::vector<float> inL(512), inR(512), oL(512), oR(512);
        float* ins[2] = { inL.data(), inR.data() }; float* outs[2] = { oL.data(), oR.data() };
        AudioBusBuffers ib{}, ob{}; ib.numChannels = 2; ib.channelBuffers32 = ins; ob.numChannels = 2; ob.channelBuffers32 = outs;
        ProcessData pd{}; pd.processMode = kRealtime; pd.symbolicSampleSize = kSample32; pd.numSamples = 512; pd.numInputs = 1; pd.numOutputs = 1; pd.inputs = &ib; pd.outputs = &ob;
        double ph = 0; bool finite = true; float maxPk = 0; double energyTail = 0;
        int sweepN = ctl->getParameterCount();
        for (int blk = 0; blk < 400; blk++) {
            for (int i = 0; i < 512; i++) { float s = 0.4f * (float)std::sin(ph) * (blk % 80 < 60 ? 1.f : 0.f); ph += 2 * 3.14159265358979 * 220.0 * (1.0 + 0.2 * std::sin(blk * 0.05)) / fs; inL[i] = s; inR[i] = s * 0.9f; }
            if (blk > 40) { // automate every parameter with a pseudo-random walk
                for (int p = 0; p < sweepN; p++) { ParameterInfo pi; ctl->getParameterInfo(p, pi); if (pi.flags & ParameterInfo::kIsReadOnly) continue;
                    double v = 0.5 + 0.5 * std::sin(blk * 0.13 + p * 1.7); if (blk % 97 == 0) v = (p & 1); ctl->setParamNormalized(pi.id, v); }
            }
            CHECK_BLOCK:
            if (proc->process(pd) != kResultOk) { finite = false; break; }
            float pk; if (!finiteBuf(oL, pk) || !finiteBuf(oR, pk)) { finite = false; break; } maxPk = std::max(maxPk, pk);
            if (blk >= 380) for (float v : oL) energyTail += v * v;
        }
        CHECK(finite, "%.0f Hz: 400 blocks with all-param automation: no NaN/Inf (peak %.2f)", fs, maxPk);
        CHECK(maxPk < 8.0f, "%.0f Hz: peak bounded", fs);
        proc->setProcessing(false); comp->setActive(false);
        ok++;
    }
    // restore sane defaults then rerun silence test
    for (int p = 0; p < ctl->getParameterCount(); p++) { ParameterInfo pi; ctl->getParameterInfo(p, pi); ctl->setParamNormalized(pi.id, pi.defaultNormalizedValue); }
    printf("params=%d\n", ctl->getParameterCount());
    CHECK(ctl->getParameterCount() > 60, "parameter count");

    // Editor in a real window
    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "QTHost"; RegisterClassA(&wc);
    IPlugView* view = ctl->createView("editor");
    CHECK(view != nullptr, "createView(editor)");
    if (view) {
        CHECK(view->isPlatformTypeSupported("HWND") == kResultTrue, "platform HWND supported");
        ViewRect r{}; view->getSize(&r); printf("  editor size %dx%d\n", r.right - r.left, r.bottom - r.top);
        CHECK(r.right - r.left > 400 && r.bottom - r.top > 300, "editor size sane");
        HWND hw = CreateWindowA("QTHost", "Quartz", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, r.right - r.left + 20, r.bottom - r.top + 40, 0, 0, wc.hInstance, 0);
        view->setFrame(&frame);
        CHECK(view->attached(hw, "HWND") == kResultOk, "view attached");
        DWORD t0 = GetTickCount(); MSG msg; while (GetTickCount() - t0 < 2500) { while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); } Sleep(10); }
        if (argc > 2) { // capture child window contents via PrintWindow
            HWND ch = GetWindow(hw, GW_CHILD); HWND tgt = ch ? ch : hw; RECT cr; GetClientRect(tgt, &cr); int W = cr.right, H = cr.bottom;
            HDC sdc = GetDC(0); HDC mdc = CreateCompatibleDC(sdc); BITMAPINFO bi{}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
            void* bits = nullptr; HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, 0, 0); SelectObject(mdc, bmp);
            { HDC wdc = GetDC(tgt); BitBlt(mdc, 0, 0, W, H, wdc, 0, 0, SRCCOPY); ReleaseDC(tgt, wdc); }
            BITMAPFILEHEADER bf{}; bf.bfType = 'MB'; bf.bfOffBits = sizeof(bf) + sizeof(BITMAPINFOHEADER); bf.bfSize = bf.bfOffBits + W * H * 4;
            BITMAPINFOHEADER bh = { sizeof(bh), W, -H, 1, 32, BI_RGB };
            FILE* fp = fopen(argv[2], "wb"); fwrite(&bf, sizeof bf, 1, fp); fwrite(&bh, sizeof bh, 1, fp); fwrite(bits, 4, (size_t)W * H, fp); fclose(fp);
            long nz = 0; for (long i = 0; i < (long)W * H; i++) if (((unsigned*)bits)[i] & 0xffffff) nz++;
            printf("  captured %dx%d nonblack px=%ld\n", W, H, nz); CHECK(nz > W * H / 10, "editor painted");
        }
        { // synthetic mouse input: drag the GATE knob, click the QUARTZ section diamond; expect host gestures
            HWND ch = GetWindow(hw, GW_CHILD); HWND tgt = ch ? ch : hw;
            auto pump = [&](int ms) { DWORD s0 = GetTickCount(); MSG m2; while (GetTickCount() - s0 < (DWORD)ms) { while (PeekMessageA(&m2, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m2); DispatchMessageA(&m2); } Sleep(5); } };
            ParamID qonId = 0; double before = 0; for (int p = 0; p < ctl->getParameterCount(); p++) { ParameterInfo pi; ctl->getParameterInfo(p, pi); char nm[64]; for (int i = 0; i < 63; i++) { nm[i] = (char)pi.title[i]; if (!pi.title[i]) break; } nm[63] = 0; if (!strcmp(nm, "Quartz On")) { qonId = pi.id; before = ctl->getParamNormalized(pi.id); } }
            int bx = host.begins, ex = host.ends, px = host.performs;
            SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(78, 134)); pump(30);
            for (int i = 1; i <= 12; i++) { SendMessageA(tgt, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(78, 134 - i * 6)); pump(15); }
            SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(78, 134 - 72)); pump(60);
            printf("  knob drag: begins+%d ends+%d performs+%d\n", host.begins - bx, host.ends - ex, host.performs - px);
            CHECK(host.begins > bx && host.ends == host.begins && host.performs > px, "knob drag produces begin/perform/end edit gestures");
            int b2 = host.begins;
            SendMessageA(tgt, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 62)); pump(30); SendMessageA(tgt, WM_LBUTTONUP, 0, MAKELPARAM(30, 62)); pump(80);
            double after = qonId ? ctl->getParamNormalized(qonId) : before;
            printf("  quartz diamond: %.0f -> %.0f (begins+%d)\n", before, after, host.begins - b2);
            CHECK(qonId && before != after, "QUARTZ diamond click toggles the section on/off param");
        }
        ViewRect nr{ 0, 0, 900, 640 }; view->checkSizeConstraint(&nr); view->onSize(&nr);
        t0 = GetTickCount(); while (GetTickCount() - t0 < 500) { while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); } Sleep(10); }
        printf("removing\n"); CHECK(view->removed() == kResultOk, "view removed");
        printf("setFrame null\n"); view->setFrame(nullptr); printf("release\n"); view->release(); printf("destroywindow\n"); DestroyWindow(hw); printf("destroyed\n");
    }
    printf("terminate\n"); ctl->setComponentHandler(nullptr);
    comp->terminate(); printf("terminated\n"); proc->release();
    printf("host begins=%d ends=%d performs=%d\n", host.begins, host.ends, host.performs);
    auto exitDll = (InitFn)GetProcAddress(m, "ExitDll"); if (exitDll) exitDll();
    printf("\nRESULT: %s (%d failures)\n", fails ? "FAIL" : "ALL OK", fails);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), fails ? 1 : 0); // skip static dtors
    return fails ? 1 : 0;
}
