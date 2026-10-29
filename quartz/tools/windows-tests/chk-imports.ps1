# Checks that every function Quartz.vst3 imports exists on THIS Windows install.
# Reads imports.json (made by dumpimp.py) next to this script.
# Prints "DLL MISSING: x.dll" / "MISSING: x.dll Function" for problems, then "done".
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class N{[DllImport("kernel32",SetLastError=true,CharSet=CharSet.Ansi)]public static extern IntPtr LoadLibraryA(string p);[DllImport("kernel32",CharSet=CharSet.Ansi)]public static extern IntPtr GetProcAddress(IntPtr h,string n);}'
$j = Get-Content "$PSScriptRoot\imports.json" -Raw | ConvertFrom-Json
$bad = 0; $n = 0
foreach($p in $j.PSObject.Properties){
  $h=[N]::LoadLibraryA($p.Name)
  if($h -eq [IntPtr]::Zero){ "DLL MISSING: $($p.Name)"; $bad++; continue }
  foreach($fn in $p.Value){ $n++; if([N]::GetProcAddress($h,$fn) -eq [IntPtr]::Zero){ "MISSING: $($p.Name) $fn"; $bad++ } }
}
"checked $n imports in $(@($j.PSObject.Properties).Count) DLLs, problems: $bad"
"done"
