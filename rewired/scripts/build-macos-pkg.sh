#!/usr/bin/env bash
# macOS (run on a Mac): build universal (arm64 + x86_64) VST3 + CLAP + Standalone app and wrap them in ONE installer .pkg.
# Output: dist/CMPLXTY-Rewired-Setup-<version>-mac.pkg
#   installs to /Library/Audio/Plug-Ins/VST3, /Library/Audio/Plug-Ins/CLAP, /Applications (app) and
#   /Library/Application Support/CMPLXTY/Rewired (models + ONNX Runtime dylib, shared by all formats); all users, admin prompt, upgrades in place.
#   Models: run scripts/fetch-models.sh first (or point RW_MODELS / RW_ORT_MAC at existing files).
# Needs: Xcode command line tools, CMake >= 3.22, Ninja (brew install cmake ninja), and the deps from scripts/fetch-deps.sh.
# NOT SIGNED / NOTARISED unless you set DEV_ID_APP and DEV_ID_INSTALLER (Apple Developer ID certificates, paid programme).
# STATUS: written for CI, never run on a real Mac by the author.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(Rewired VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
B=build/mac
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.15}" \
  -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build $B --target Rewired_VST3 Rewired_CLAP Rewired_Standalone --parallel
A=$B/Rewired_artefacts/Release
ROOT=dist/stage-macos/root; rm -rf dist/stage-macos; mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/CLAP"
cp -R "$A/VST3/CMPLXTY Rewired.vst3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$A/CLAP/CMPLXTY Rewired.clap" "$ROOT/Library/Audio/Plug-Ins/CLAP/"
mkdir -p "$ROOT/Applications"; cp -R "$A/Standalone/CMPLXTY Rewired.app" "$ROOT/Applications/"
RW_MODELS=${RW_MODELS:-$PWD/build/models}; RW_ORT_MAC=${RW_ORT_MAC:-$RW_MODELS/ort/osx/libonnxruntime.1.23.2.dylib}
MD="$ROOT/Library/Application Support/CMPLXTY/Rewired"; mkdir -p "$MD"
cp "$RW_MODELS/spleeter4.onnx" "$RW_MODELS/nmp.onnx" "$RW_ORT_MAC" "$MD/"
cp installer/LICENSES.txt installer/THIRD-PARTY-LICENSES.txt "$MD/"
if [ -n "${DEV_ID_APP:-}" ]; then
  for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY Rewired.app" "$MD/libonnxruntime.1.23.2.dylib"; do codesign --force --options runtime --timestamp --sign "$DEV_ID_APP" "$p"; done
else
  # No Apple certificate: still seal each bundle with an ad-hoc signature ("-"). The compiler only signs the main binary; this also seals
  # Info.plist + Resources so the bundle is a valid signed unit (Apple Silicon hosts refuse code with a missing/broken signature).
  # It does NOT remove the Gatekeeper warning on the downloaded .pkg: only Developer ID signing + notarization does.
  for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY Rewired.app" "$MD/libonnxruntime.1.23.2.dylib"; do codesign --force --deep --timestamp=none --sign - "$p"; done
fi
for p in "$ROOT"/Library/Audio/Plug-Ins/*/* "$ROOT/Applications/CMPLXTY Rewired.app"; do codesign --verify --deep --strict --verbose=2 "$p"; done
lipo -archs "$ROOT/Library/Audio/Plug-Ins/VST3/CMPLXTY Rewired.vst3/Contents/MacOS/CMPLXTY Rewired"
lipo -archs "$MD/libonnxruntime.1.23.2.dylib"
mkdir -p dist
OUT=dist/CMPLXTY-Rewired-Setup-$VERSION-mac.pkg
# macOS /bin/bash is 3.2: an empty array under `set -u` is an unbound variable, so do not expand SIGN[@] when unsigned.
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  pkgbuild --root "$ROOT" --identifier com.cmplxty.rewired --version "$VERSION" --install-location / --sign "$DEV_ID_INSTALLER" "$OUT"
else
  pkgbuild --root "$ROOT" --identifier com.cmplxty.rewired --version "$VERSION" --install-location / "$OUT"
fi
ls -l "$OUT"; shasum -a 256 "$OUT"
