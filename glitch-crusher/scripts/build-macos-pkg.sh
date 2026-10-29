#!/usr/bin/env bash
# macOS (run on a Mac): build universal (arm64 + x86_64) VST3 + CLAP and wrap them in ONE installer .pkg.
# Output: dist/CMPLXTY-Glitch-Crusher-Setup-<version>-mac.pkg
#   installs to /Library/Audio/Plug-Ins/VST3 and /Library/Audio/Plug-Ins/CLAP (all users, admin prompt), upgrades in place.
# Needs: Xcode command line tools, CMake >= 3.22, Ninja (brew install cmake ninja), and the deps from scripts/fetch-deps.sh.
# NOT SIGNED / NOTARISED unless you set DEV_ID_APP and DEV_ID_INSTALLER (Apple Developer ID certificates, paid programme).
# STATUS: written for CI, never run on a real Mac by the author.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(GlitchCrusher VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
B=build/mac
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.15}" \
  -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build $B --target GlitchCrusher_VST3 GlitchCrusher_CLAP --parallel
A=$B/GlitchCrusher_artefacts/Release
ROOT=dist/stage-macos/root; rm -rf dist/stage-macos; mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/CLAP"
cp -R "$A/VST3/CMPLXTY Glitch Crusher.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$A/CLAP/CMPLXTY Glitch Crusher.clap" "$ROOT/Library/Audio/Plug-Ins/CLAP/"
if [ -n "${DEV_ID_APP:-}" ]; then
  for p in "$ROOT"/Library/Audio/Plug-Ins/*/*; do codesign --force --options runtime --timestamp --sign "$DEV_ID_APP" "$p"; done
fi
lipo -archs "$ROOT/Library/Audio/Plug-Ins/VST3/CMPLXTY Glitch Crusher.vst3/Contents/MacOS/CMPLXTY Glitch Crusher"
mkdir -p dist
OUT=dist/CMPLXTY-Glitch-Crusher-Setup-$VERSION-mac.pkg
# macOS /bin/bash is 3.2: an empty array under `set -u` is an unbound variable, so do not expand SIGN[@] when unsigned.
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  pkgbuild --root "$ROOT" --identifier com.cmplxty.glitch-crusher --version "$VERSION" --install-location / --sign "$DEV_ID_INSTALLER" "$OUT"
else
  pkgbuild --root "$ROOT" --identifier com.cmplxty.glitch-crusher --version "$VERSION" --install-location / "$OUT"
fi
ls -l "$OUT"; shasum -a 256 "$OUT"
