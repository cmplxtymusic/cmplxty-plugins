#!/usr/bin/env bash
# Cross-build the Windows x64 hub exe on Linux with clang-cl/lld-link + xwin (same toolchain setup as the plug-ins, see silt/scripts).
set -euo pipefail
cd "$(dirname "$0")/.."
LLVM_ROOT="${LLVM_ROOT:-$HOME/.local/llvm}"; XWIN_ROOT="${XWIN_ROOT:-$HOME/.local/xwin-sdk}"
export PATH="$LLVM_ROOT/bin:$PATH"
B=build/winclang
for n in DbgHelp:dbghelp Dwrite:dwrite D2d1:d2d1 DComp:dcomp; do ln -sf "${n#*:}.lib" "$XWIN_ROOT/sdk/lib/um/x86_64/${n%%:*}.lib"; done
cmake -S . -B $B -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ROOT="$LLVM_ROOT" -DXWIN_ROOT="$XWIN_ROOT" -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF
ninja -C $B PluginHub
D=dist/stage-windows-x64; rm -rf "$D"; mkdir -p "$D"
cp "$B/PluginHub_artefacts/Release/CMPLXTY Plugin Hub.exe" "$D/"
echo "staged: $D   (next: scripts/build-installer.sh)"
