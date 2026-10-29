#!/usr/bin/env bash
# macOS (run on a Mac / GitHub Actions macos-14): universal (arm64 + x86_64) Quartz.vst3, ad-hoc signed, wrapped in one .pkg.
# Output: dist/CMPLXTY-Quartz-Setup-<version>-mac.pkg  (installs to /Library/Audio/Plug-Ins/VST3, upgrades in place; presets live in the
# user's data folder and are not touched).
# Needs: Xcode command line tools, CMake >= 3.22, Ninja, git. Fetches JUCE 8.0.9 into ./JUCE and installs it to build/juce-install.
# NOT SIGNED with a Developer ID / not notarised (no paid Apple programme): ad-hoc signature only.
# STATUS: written for CI, never run on a real Mac by the author.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(Quartz VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
ARCHS="arm64;x86_64"; DT="${MACOSX_DEPLOYMENT_TARGET:-10.15}"
[ -d JUCE ] || git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git JUCE
JI=$PWD/build/juce-install
if [ ! -f "$JI/lib/cmake/JUCE-8.0.9/JUCEConfig.cmake" ]; then
  cmake -S JUCE -B build/juce -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$JI" -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DT" -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF
  cmake --build build/juce --parallel && cmake --install build/juce
fi
B=build/mac
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$DT" \
  -DQUARTZ_JUCE_DIR="$JI/lib/cmake/JUCE-8.0.9"
cmake --build $B --target Quartz_VST3 QuartzUpdateTest --parallel
"$B/QuartzUpdateTest_artefacts/Release/QuartzUpdateTest"
A=$B/Quartz_artefacts/Release
ROOT=dist/stage-macos/root; rm -rf dist/stage-macos; mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3"
cp -R "$A/VST3/Quartz.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
P="$ROOT/Library/Audio/Plug-Ins/VST3/Quartz.vst3"
# ad-hoc seal (Apple Silicon hosts refuse a missing/broken signature); does NOT remove the Gatekeeper warning on a downloaded .pkg
codesign --force --deep --timestamp=none --sign - "$P"
codesign --verify --deep --strict --verbose=2 "$P"
lipo -archs "$P/Contents/MacOS/Quartz"
mkdir -p dist
OUT=dist/CMPLXTY-Quartz-Setup-$VERSION-mac.pkg
pkgbuild --root "$ROOT" --identifier com.cmplxty.quartz --version "$VERSION" --install-location / "$OUT"
ls -l "$OUT"; shasum -a 256 "$OUT"
