#!/usr/bin/env bash
# Windows NSIS setup exe for the hub. Output: dist/CMPLXTY-Plugin-Hub-Setup-<version>-win.exe
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(PluginHub VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
MAKENSIS=${MAKENSIS:-$(command -v makensis || echo /workspace/tc/sysroot/usr/bin/makensis)}
STAGE=${STAGE:-$PWD/dist/stage-windows-x64}
OUT=${OUT:-$PWD/dist/CMPLXTY-Plugin-Hub-Setup-$VERSION-win.exe}
[ -f "$STAGE/CMPLXTY Plugin Hub.exe" ] || { echo "run scripts/build-windows-xwin.sh first" >&2; exit 1; }
if [ -z "${NSISDIR:-}" ] && [[ "$MAKENSIS" == /workspace/tc/* ]]; then export NSISDIR=/workspace/tc/sysroot/usr/share/nsis; fi
"$MAKENSIS" -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTFILE="$OUT" installer/plugin-hub.nsi
ls -l "$OUT"; sha256sum "$OUT"
