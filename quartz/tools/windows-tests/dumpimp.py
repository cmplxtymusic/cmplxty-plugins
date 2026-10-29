#!/usr/bin/env python3
"""Dump the import table (normal + delay-load) of a PE file to imports.json.
Usage: python dumpimp.py Quartz.vst3 imports.json      (needs: pip install pefile)
Output format: {"DLL.dll": ["Function", ...], ...}; delay-loaded DLLs are marked on stdout
and ALSO included in the json (chk-imports.ps1 checks them too)."""
import pefile, sys, json
pe = pefile.PE(sys.argv[1]); out = {}; delay = []
print("machine", hex(pe.FILE_HEADER.Machine))
for e in pe.DIRECTORY_ENTRY_IMPORT:
    out.setdefault(e.dll.decode(), []).extend(i.name.decode() for i in e.imports if i.name)
    for i in e.imports:
        if not i.name: print("ORDINAL IMPORT", e.dll.decode(), i.ordinal)
for e in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
    d = e.dll.decode(); delay.append(d)
    out.setdefault(d, []).extend(i.name.decode() for i in e.imports if i.name)
for k in out: out[k] = sorted(set(out[k]))
json.dump(out, open(sys.argv[2], "w"), indent=0)
for k, v in sorted(out.items(), key=lambda kv: kv[0].lower()):
    print("%-40s %4d%s" % (k, len(v), "   (DELAY-LOADED)" if k in delay else ""))
