#!/bin/bash
# CMPLXTY installer for Mac, for when double-clicking a downloaded file is blocked by Gatekeeper.
# Usage:  bash install-mac.sh glitch-crusher | growl-saturator | yoink
# Why it works: files downloaded with curl are not marked "from the internet" (no com.apple.quarantine), so macOS does not show the
# "Apple could not verify ... is free of malware" block. Nothing is bypassed system-wide: only this one download is trusted, by you running this script.
# It prints the SHA-256 of what it downloaded so you can compare it with the one on the download page.
set -euo pipefail
BASE="https://cmplxty.music"
case "${1:-}" in
  glitch-crusher) URL="$BASE/plugins/glitch-crusher/CMPLXTY-Glitch-Crusher-Setup-1.0.0-mac.pkg"; KIND=pkg ;;
  growl-saturator) URL="$BASE/plugins/growl-saturator/CMPLXTY-Growl-Setup-1.0.0-mac.pkg"; KIND=pkg ;;
  yoink) URL="$BASE/yoink.dmg"; KIND=dmg ;;
  *) echo "Usage: bash install-mac.sh glitch-crusher | growl-saturator | yoink" >&2; exit 2 ;;
esac
[ "$(uname)" = Darwin ] || { echo "This is for macOS." >&2; exit 1; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FILE="$TMP/$(basename "$URL")"
echo "Downloading $URL ..."
curl -fL --progress-bar -o "$FILE" "$URL"
echo "SHA-256: $(shasum -a 256 "$FILE" | cut -d' ' -f1)"
xattr -c "$FILE" 2>/dev/null || true
if [ "$KIND" = pkg ]; then
  echo "Installing (asks for your Mac password) ..."
  sudo installer -pkg "$FILE" -target /
  echo "Done. Restart your DAW and rescan plugins."
else
  MNT="$TMP/mnt"; mkdir -p "$MNT"
  hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$FILE" >/dev/null
  rm -rf /Applications/Yoink.app
  cp -R "$MNT/Yoink.app" /Applications/
  hdiutil detach "$MNT" >/dev/null
  xattr -dr com.apple.quarantine /Applications/Yoink.app 2>/dev/null || true
  echo "Done. Open Yoink from Applications."
fi
