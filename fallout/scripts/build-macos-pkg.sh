#!/usr/bin/env bash
# macOS (run on a Mac): build universal (arm64 + x86_64) VST3 + CLAP + Standalone app and wrap them in ONE installer .pkg.
# Output: dist/CMPLXTY-FALLOUT-Setup-<version>-mac.pkg
#   installs to /Library/Audio/Plug-Ins/VST3, /Library/Audio/Plug-Ins/CLAP, /Applications (app),
#   licence text in /Library/Application Support/CMPLXTY/FALLOUT; all users, admin prompt, upgrades in place.
#   User presets + settings live in ~/Library/Application Support/CMPLXTY/FALLOUT and are never touched by the pkg.
# Needs: Xcode command line tools, CMake >= 3.22, Ninja (brew install cmake ninja), and the deps from scripts/fetch-deps.sh.
# NOT SIGNED / NOTARISED unless you set DEV_ID_APP and DEV_ID_INSTALLER (Apple Developer ID certificates, paid programme).
# STATUS: written for CI, never run on a real Mac by the author.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(Fallout VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
B=build/mac
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.15}" \
  -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build $B --target Fallout_VST3 Fallout_CLAP Fallout_Standalone --parallel
A=$B/Fallout_artefacts/Release
ROOT=dist/stage-macos/root; rm -rf dist/stage-macos; mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/CLAP"
cp -R "$A/VST3/CMPLXTY FALLOUT.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$A/CLAP/CMPLXTY FALLOUT.clap" "$ROOT/Library/Audio/Plug-Ins/CLAP/"
mkdir -p "$ROOT/Applications"; cp -R "$A/Standalone/CMPLXTY FALLOUT.app" "$ROOT/Applications/"
MD="$ROOT/Library/Application Support/CMPLXTY/FALLOUT"; mkdir -p "$MD"
cp installer/LICENSES.txt "$MD/"
if [ -n "${DEV_ID_APP:-}" ]; then
  for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY FALLOUT.app" ; do codesign --force --options runtime --timestamp --sign "$DEV_ID_APP" "$p"; done
else
  for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY FALLOUT.app" ; do codesign --force --deep --timestamp=none --sign - "$p"; done
fi
for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY FALLOUT.app"; do codesign --verify --deep --strict --verbose=2 "$p"; done
lipo -archs "$ROOT/Library/Audio/Plug-Ins/VST3/CMPLXTY FALLOUT.vst3/Contents/MacOS/CMPLXTY FALLOUT"
mkdir -p dist
OUT=dist/CMPLXTY-FALLOUT-Setup-$VERSION-mac.pkg
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  pkgbuild --root "$ROOT" --identifier com.cmplxty.fallout --version "$VERSION" --install-location / --sign "$DEV_ID_INSTALLER" "$OUT"
else
  pkgbuild --root "$ROOT" --identifier com.cmplxty.fallout --version "$VERSION" --install-location / "$OUT"
fi
ls -l "$OUT"; shasum -a 256 "$OUT"
