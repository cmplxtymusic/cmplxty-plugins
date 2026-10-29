# Loads Quartz.vst3 with LoadLibraryW and checks the VST3 entry points.
#   powershell -ExecutionPolicy Bypass -File test-load.ps1 [path\to\Quartz.vst3]
# Without an argument it tries the per-user and the all-users install and the file next to this script.
# NOTE: powershell.exe itself may carry a Common Controls v6 manifest, which would hide the
# bug this test is about. For a true "no manifest host" test run qz-loadtest.exe (same checks,
# no manifest) - it is in the same folder.
param([string]$Path)
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class Q {
  [DllImport("kernel32", SetLastError=true, CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryW(string p);
  [DllImport("kernel32", CharSet=CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr h, string n);
  [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr h);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate bool BoolFn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate bool ModEntryFn(IntPtr h);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate IntPtr FactoryFn();
  [UnmanagedFunctionPointer(CallingConvention.StdCall)] public delegate int CountFn(IntPtr self);
  [UnmanagedFunctionPointer(CallingConvention.StdCall)] public delegate uint RelFn(IntPtr self);
}
'@
$cands = @()
if ($Path) { $cands += $Path } else {
  $cands += "$env:LOCALAPPDATA\Programs\Common\VST3\Quartz.vst3\Contents\x86_64-win\Quartz.vst3"
  $cands += "$env:CommonProgramFiles\VST3\Quartz.vst3\Contents\x86_64-win\Quartz.vst3"
  $cands += "$PSScriptRoot\Quartz.vst3\Contents\x86_64-win\Quartz.vst3"
  $cands += "$PSScriptRoot\Quartz.vst3"
}
$fail = 0
foreach ($c in $cands) {
  if (-not (Test-Path $c -PathType Leaf)) { if ($Path) { "NOT FOUND: $c"; $fail++ }; continue }
  "== $c"
  $h = [Q]::LoadLibraryW((Resolve-Path $c).Path)
  if ($h -eq [IntPtr]::Zero) { $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error(); "  LoadLibraryW FAILED, Win32 error $e ($((New-Object ComponentModel.Win32Exception $e).Message))"; $fail++; continue }
  "  LoadLibraryW OK"
  foreach ($n in 'GetPluginFactory','InitDll','ExitDll','ModuleEntry','ModuleExit') {
    $p = [Q]::GetProcAddress($h, $n)
    $req = ($n -eq 'GetPluginFactory')
    if ($p -eq [IntPtr]::Zero) { if ($req) { "  export $n : MISSING (required)"; $fail++ } else { "  export $n : not present (optional)" } } else { "  export $n : present" }
  }
  # Windows VST3 modules export InitDll/ExitDll (JUCE does); ModuleEntry/ModuleExit are the Linux names (optional here).
  $pi = [Q]::GetProcAddress($h,'InitDll'); $pe = [Q]::GetProcAddress($h,'ModuleEntry'); $pf = [Q]::GetProcAddress($h,'GetPluginFactory')
  $px = [Q]::GetProcAddress($h,'ExitDll'); if ($px -eq [IntPtr]::Zero) { $px = [Q]::GetProcAddress($h,'ModuleExit') }
  if ($pf -ne [IntPtr]::Zero) {
    if ($pi -ne [IntPtr]::Zero) { $ok = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($pi, [Q+BoolFn]).Invoke(); "  InitDll returned $ok" }
    elseif ($pe -ne [IntPtr]::Zero) { $ok = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($pe, [Q+ModEntryFn]).Invoke($h); "  ModuleEntry returned $ok" }
    $f = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($pf, [Q+FactoryFn]).Invoke()
    if ($f -eq [IntPtr]::Zero) { "  GetPluginFactory returned NULL"; $fail++ }
    else {
      $vt = [Runtime.InteropServices.Marshal]::ReadIntPtr($f)
      $cnt = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer([Runtime.InteropServices.Marshal]::ReadIntPtr($vt, 4*[IntPtr]::Size), [Q+CountFn]).Invoke($f)
      "  GetPluginFactory OK, factory reports $cnt class(es)"
      if ($cnt -lt 1) { $fail++ }
      [void][Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer([Runtime.InteropServices.Marshal]::ReadIntPtr($vt, 2*[IntPtr]::Size), [Q+RelFn]).Invoke($f)
    }
    if ($px -ne [IntPtr]::Zero) { [void][Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($px, [Q+BoolFn]).Invoke(); "  ExitDll/ModuleExit called" }
  } else { $fail++ }
  [void][Q]::FreeLibrary($h)
}
if ($fail -eq 0) { "RESULT: PASS" } else { "RESULT: FAIL ($fail problem(s))" }
