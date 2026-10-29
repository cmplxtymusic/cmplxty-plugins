#!/usr/bin/env bash
# Build the Windows NSIS setup .exe from the staged build (run scripts/build-windows-xwin.sh first).
# Output: dist/CMPLXTY-Robot-Voice-Setup-<version>-win.exe   (version is read from CMakeLists.txt project(... VERSION x.y.z))
# makensis: set MAKENSIS or have it on PATH (NSIS 3; on Debian/Ubuntu: apt install nsis).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project *(RobotVoice VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
MAKENSIS=${MAKENSIS:-$(command -v makensis || echo /workspace/tc/sysroot/usr/bin/makensis)}
STAGE=${STAGE:-$PWD/dist/stage-windows-x64}
OUT=${OUT:-$PWD/dist/CMPLXTY-Robot-Voice-Setup-$VERSION-win.exe}
[ -d "$STAGE/CMPLXTY Robot Voice.vst3" ] || { echo "missing $STAGE - run scripts/build-windows-xwin.sh first" >&2; exit 1; }
# NSIS needs its own data dir when installed outside the usual prefix
if [ -z "${NSISDIR:-}" ] && [ -d /workspace/tc/sysroot/usr/share/nsis ] && [[ "$MAKENSIS" == /workspace/tc/* ]]; then export NSISDIR=/workspace/tc/sysroot/usr/share/nsis; fi
"$MAKENSIS" -V2 -DVERSION="$VERSION" -DSTAGE="$STAGE" -DOUTFILE="$OUT" installer/robot-voice.nsi
ls -l "$OUT"; sha256sum "$OUT"
