#!/usr/bin/env bash
# Cross-build Quartz VST3 for Windows on Linux with llvm-mingw, then pack:
#   dist/Quartz-Setup-<version>.exe     NSIS installer (installer/quartz.nsi)
#   dist/Quartz-<version>-VST3.zip      the VST3 bundle, for people who do not want the setup exe
# Quartz is a VST3 plug-in (no CLAP target in this project).
# Needs: cmake, ninja, g++ (host juceaide), git, nsis (makensis), zip, curl, and LLVM_MINGW_ROOT
# (llvm-mingw ucrt, x86_64-w64-mingw32-clang). See quartz/BUILD.txt.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(Quartz VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
[ -n "${LLVM_MINGW_ROOT:-}" ] && [ -x "$LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-clang" ] || { echo "set LLVM_MINGW_ROOT to an llvm-mingw ucrt toolchain" >&2; exit 1; }

if [ ! -d JUCE/.git ]; then
  rm -rf JUCE
  git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git JUCE
fi
if ! grep -q "Quartz patch: llvm-mingw" JUCE/modules/juce_core/system/juce_TargetPlatform.h; then
  git -C JUCE apply ../patches/juce-8.0.9-mingw.patch
fi

JI=$PWD/build/juce-install
if [ ! -f "$JI/lib/cmake/JUCE-8.0.9/JUCEConfig.cmake" ]; then
  cmake -S JUCE -B build/juce-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$JI" \
    -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=ON
  cmake --build build/juce-host --target juceaide --parallel
  cmake --install build/juce-host
fi

B=build/win
cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw.cmake \
  -DQUARTZ_JUCE_DIR="$JI/lib/cmake/JUCE-8.0.9"
cmake --build "$B" --target Quartz_VST3 --parallel

BIN=$(find "$B" -type f -path '*/x86_64-win/Quartz.vst3' | head -1)
BUNDLE=$(find "$B" -type d -name 'Quartz.vst3' | head -1)
[ -n "$BIN" ] && [ -f "$BIN" ] || { echo "Quartz.vst3 binary not found" >&2; exit 1; }
[ -n "$BUNDLE" ] && [ -d "$BUNDLE" ] || { echo "Quartz.vst3 bundle not found" >&2; exit 1; }
"$LLVM_MINGW_ROOT/bin/llvm-strip" --strip-all "$BIN" || true

mkdir -p dist
EXE=$PWD/dist/Quartz-Setup-$VERSION.exe
ZIP=$PWD/dist/Quartz-$VERSION-VST3.zip
rm -f "$EXE" "$ZIP"
( cd installer && makensis -V2 -DVERSION="$VERSION" -DPAYLOAD="$BIN" -DOUTFILE="$EXE" quartz.nsi )
( cd "$(dirname "$BUNDLE")" && zip -r -q "$ZIP" "$(basename "$BUNDLE")" )
( cd dist && sha256sum "$(basename "$EXE")" "$(basename "$ZIP")" | tee SHA256SUMS )
ls -l "$EXE" "$ZIP"
