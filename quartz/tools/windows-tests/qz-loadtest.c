/* qz-loadtest.exe - loads a VST3 DLL from a process with NO manifest (so Common Controls v5, like a bare host).
   usage: qz-loadtest.exe path\to\Quartz.vst3   -> prints PASS/FAIL, exit code 0 on pass.
   Built static, no manifest, no comctl32 reference of its own. */
#include <windows.h>
#include <stdio.h>
typedef BOOL (__cdecl *EntryFn)(HMODULE); typedef BOOL (__cdecl *ExitFn)(void); typedef void* (__cdecl *FacFn)(void);
typedef struct { void** vt; } Obj;
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { fprintf(stderr, "usage: qz-loadtest <Quartz.vst3>\n"); return 2; }
  HMODULE h = LoadLibraryW(argv[1]);
  if (!h) { DWORD e = GetLastError(); printf("LoadLibraryW FAILED, Win32 error %lu\nFAIL\n", e); return 1; }
  printf("LoadLibraryW OK\n");
  FARPROC pf = GetProcAddress(h, "GetPluginFactory"), pi = GetProcAddress(h, "InitDll"), px = GetProcAddress(h, "ExitDll");
  FARPROC pe = GetProcAddress(h, "ModuleEntry"), pm = GetProcAddress(h, "ModuleExit");
  /* Windows VST3 modules export InitDll/ExitDll (JUCE does); ModuleEntry/ModuleExit are the Linux names, optional here. */
  printf("export GetPluginFactory: %s (required)\nexport InitDll: %s\nexport ExitDll: %s\nexport ModuleEntry: %s\nexport ModuleExit: %s\n",
    pf ? "present" : "MISSING", pi ? "present" : "not present", px ? "present" : "not present", pe ? "present" : "not present", pm ? "present" : "not present");
  int fail = !pf;
  if (!fail) {
    if (pi) printf("InitDll returned %d\n", (int) ((ExitFn) pi)());
    else if (pe) printf("ModuleEntry returned %d\n", (int) ((EntryFn) pe)(h));
    Obj* f = (Obj*) ((FacFn) pf)();
    if (!f) { printf("GetPluginFactory returned NULL\n"); fail = 1; }
    else {
      int n = ((int (__stdcall *)(void*)) f->vt[4])(f);
      printf("GetPluginFactory OK, %d class(es)\n", n); if (n < 1) fail = 1;
      ((unsigned (__stdcall *)(void*)) f->vt[2])(f);
    }
    if (px) { ((ExitFn) px)(); printf("ExitDll called\n"); } else if (pm) { ((ExitFn) pm)(); printf("ModuleExit called\n"); }
  }
  FreeLibrary(h);
  printf(fail ? "FAIL\n" : "PASS\n"); return fail;
}
