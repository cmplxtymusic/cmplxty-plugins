#!/usr/bin/env bash
# Cross-build the Windows installers on Linux with electron-builder (NSIS runs under wine32 for the uninstaller stub).
# Needs: node/npm (npm install first), wine + wine32:i386, build/electron-dist (unzipped electron-v45.0.0-win32-x64.zip),
#        build/tools/{yt-dlp.exe,ffmpeg.exe,ffmpeg-LICENSE.txt}  (ffmpeg.exe: see build-ffmpeg-min.sh).
# Output: dist/Yoink-Setup.exe (full, offline) and dist-lite/Yoink-Setup-Lite.exe (downloads yt-dlp on first search).
set -euo pipefail
cd "$(dirname "$0")"
export WINEDEBUG=-all
for f in build/tools/yt-dlp.exe build/tools/ffmpeg.exe build/electron-dist/electron.exe; do [ -e "$f" ] || { echo "missing $f"; exit 1; }; done
mkdir -p build/tools-lite && cp build/tools/ffmpeg.exe build/tools/ffmpeg-LICENSE.txt build/tools-lite/
node build/stage-ort.js
npm test
npx electron-builder --win nsis --x64
[ "${LITE:-1}" = 1 ] && npx electron-builder --win nsis --x64 --config build/lite-config.js
OUT="${OUT:-/workspace/yoink-win/v2}"; mkdir -p "$OUT"
for f in dist/Yoink-Setup.exe dist-lite/Yoink-Setup-Lite.exe; do
  [ -e "$f" ] || continue; cp "$f" "$OUT/"; (cd "$OUT" && sha256sum "$(basename "$f")" && stat -c '%s bytes' "$(basename "$f")")
done
