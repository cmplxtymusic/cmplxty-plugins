#!/usr/bin/env bash
# Build the Windows NSIS setup .exe from the staged build (run scripts/build-windows-xwin.sh first).
# Output: dist/CMPLXTY-Glitch-Crusher-Setup-<version>-win.exe   (version is read from CMakeLists.txt project(... VERSION x.y.z))
# makensis: set MAKENSIS or have it on PATH (NSIS 3; on Debian/Ubuntu: apt install nsis).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(GlitchCrusher VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
MAKENSIS=${MAKENSIS:-$(command -v makensis)}
STAGE=${STAGE:-$PWD/dist/stage-windows-x64}
OUT=${OUT:-$PWD/dist/CMPLXTY-Glitch-Crusher-Setup-$VERSION-win.exe}
[ -d "$STAGE/CMPLXTY Glitch Crusher.vst3" ] || { echo "missing $STAGE - run scripts/build-windows-xwin.sh first" >&2; exit 1; }
"$MAKENSIS" -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTFILE="$OUT" installer/glitch-crusher.nsi
ls -l "$OUT"; sha256sum "$OUT"
