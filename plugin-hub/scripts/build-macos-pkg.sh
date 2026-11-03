#!/usr/bin/env bash
# macOS (run on a Mac): universal hub app, ad-hoc signed unless DEV_ID_APP/DEV_ID_INSTALLER are set, wrapped in a pkg -> /Applications.
# STATUS: written for CI, never run on a real Mac.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(PluginHub VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
B=build/mac
cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-10.15}" \
  -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF
cmake --build $B --target PluginHub --parallel
APP="$B/PluginHub_artefacts/Release/CMPLXTY Plugin Hub.app"
ROOT=dist/stage-macos/root; rm -rf dist/stage-macos; mkdir -p "$ROOT/Applications"
cp -R "$APP" "$ROOT/Applications/"
A="$ROOT/Applications/CMPLXTY Plugin Hub.app"
if [ -n "${DEV_ID_APP:-}" ]; then codesign --force --options runtime --timestamp --sign "$DEV_ID_APP" "$A"
else codesign --force --deep --timestamp=none --sign - "$A"; fi
codesign --verify --deep --strict --verbose=2 "$A"
lipo -archs "$A/Contents/MacOS/CMPLXTY Plugin Hub"
mkdir -p dist; OUT=dist/CMPLXTY-Plugin-Hub-Setup-$VERSION-mac.pkg
if [ -n "${DEV_ID_INSTALLER:-}" ]; then
  pkgbuild --root "$ROOT" --identifier com.cmplxty.pluginhub --version "$VERSION" --install-location / --sign "$DEV_ID_INSTALLER" "$OUT"
else
  pkgbuild --root "$ROOT" --identifier com.cmplxty.pluginhub --version "$VERSION" --install-location / "$OUT"
fi
ls -l "$OUT"; shasum -a 256 "$OUT"
