#!/usr/bin/env python3
"""Check Windows PE version metadata is consistent (SignPath requirement: same product name and
product version on every signed file of a build).

usage: check-win-metadata.py <plugin-folder> <file.exe|.vst3|.clap> [...]
Expected values come from <plugin-folder>/CMakeLists.txt:
  project(<X> VERSION a.b.c)  -> ProductVersion must start with a.b.c
  PRODUCT_NAME "CMPLXTY ..."  -> ProductName must equal it
  COMPANY_NAME "CMPLXTY"      -> CompanyName must equal it
Exit 1 on any mismatch or missing VERSIONINFO.
"""
import re, sys, pefile

def expected(folder):
    t = open(f"{folder}/CMakeLists.txt", encoding="utf-8").read()
    ver = re.search(r"^project\(\s*\w+\s+VERSION\s+([0-9.]+)", t, re.M).group(1)
    # the plugin target's PRODUCT_NAME is the first one starting with "CMPLXTY "
    name = re.search(r'PRODUCT_NAME\s+"(CMPLXTY [^"]+)"', t).group(1)
    comp = re.search(r'COMPANY_NAME\s+"([^"]+)"', t).group(1)
    return name, ver, comp

def info(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    out = {}
    for fi in getattr(pe, "FileInfo", []) or []:
        for e in fi:
            for st in getattr(e, "StringTable", []) or []:
                for k, v in st.entries.items():
                    out[k.decode(errors="replace")] = v.decode(errors="replace").strip("\x00 ")
    return out

def main():
    folder, files = sys.argv[1], sys.argv[2:]
    name, ver, comp = expected(folder)
    print(f"expected ProductName={name!r} ProductVersion={ver} CompanyName={comp!r}")
    bad = 0
    for f in files:
        i = info(f)
        pn, pv, cn = i.get("ProductName"), i.get("ProductVersion", ""), i.get("CompanyName")
        ok = pn == name and pv.split(",")[0].strip().startswith(ver) and cn == comp
        # NSIS/JUCE write a.b.c or a.b.c.0; normalise both
        pvn = pv.replace(", ", ".").replace(",", ".")
        ok = pn == name and (pvn == ver or pvn == ver + ".0") and cn == comp
        print(f"{'OK  ' if ok else 'FAIL'} {f}: ProductName={pn!r} ProductVersion={pv!r} CompanyName={cn!r}")
        bad |= not ok
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
