#!/usr/bin/env bash
# Cross-build the Windows x64 VST3 + CLAP on Linux with clang-cl/lld-link + xwin (MS CRT/SDK, static /MT CRT).
# One-time setup (no apt needed):
#   LLVM:  https://github.com/llvm/llvm-project/releases  (LLVM-<ver>-Linux-X64.tar.xz) -> ~/.local/llvm   (tested 23.1.4)
#   xwin:  https://github.com/Jake-Shadle/xwin/releases  -> xwin --accept-license --arch x86_64 --variant desktop splat --output ~/.local/xwin-sdk
#          (accepting the license = Microsoft Visual Studio / Windows SDK terms)
#   The case-variant lib symlinks (Windows is case-insensitive, Linux is not) are created below.
# Order: build natively on Linux first (build/linux, see README) - its moduleinfo.json is copied into the VST3 bundle,
#   because the manifest helper is a Windows .exe that cannot run during a cross build.
# JUCE's juceaide helper is compiled for the build host, so a host C++ compiler must be reachable (c++ / cc on PATH, or CC/CXX).
set -euo pipefail
cd "$(dirname "$0")/.."
LLVM_ROOT="${LLVM_ROOT:-$HOME/.local/llvm}"; XWIN_ROOT="${XWIN_ROOT:-$HOME/.local/xwin-sdk}"
export PATH="$LLVM_ROOT/bin:$PATH"
B=build/winclang
L=build/linux/MixTools_artefacts/Release/VST3/"CMPLXTY Mix Tools.vst3"/Contents/Resources/moduleinfo.json
for n in DbgHelp:dbghelp Dwrite:dwrite D2d1:d2d1 DComp:dcomp; do
  ln -sf "${n#*:}.lib" "$XWIN_ROOT/sdk/lib/um/x86_64/${n%%:*}.lib"; done
cmake -S . -B $B -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ROOT="$LLVM_ROOT" -DXWIN_ROOT="$XWIN_ROOT" -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
ninja -C $B MixTools_VST3 MixTools_CLAP MixToolsUpdateTest
A=$B/MixTools_artefacts/Release
# staging tree consumed by installer/mix-tools.nsi (the user-facing deliverable is the NSIS setup .exe, not a zip)
D=dist/stage-windows-x64; rm -rf "$D"; mkdir -p "$D/CMPLXTY Mix Tools.vst3/Contents/Resources"
cp -r "$A/VST3/CMPLXTY Mix Tools.vst3/Contents/x86_64-win" "$D/CMPLXTY Mix Tools.vst3/Contents/"
cp "$L" "$D/CMPLXTY Mix Tools.vst3/Contents/Resources/moduleinfo.json"   # platform independent; helper exe can't run on Linux
cp "$A/CLAP/CMPLXTY Mix Tools.clap" "$D/"
echo "staged: $D   (next: makensis -DVERSION=... installer/mix-tools.nsi, see scripts/build-installer.sh)"
