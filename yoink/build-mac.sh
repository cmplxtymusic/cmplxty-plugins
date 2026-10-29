#!/usr/bin/env bash
# Builds the macOS Yoink.dmg (universal: Apple Silicon + Intel). Run this ON A MAC (or in the GitHub Actions macOS job, which calls it).
#   bash build-mac.sh            everything: tools, stage, test, dmg
#   bash build-mac.sh tools      fetch yt-dlp and build ffmpeg into build/tools-mac
#   bash build-mac.sh stage      stage onnxruntime (Clean up) into build/ort-mac
#   bash build-mac.sh dmg        just run electron-builder (tools and stage must be done)
# Output: dist-mac/Yoink.dmg and dist-mac/Yoink.dmg.sha256.
# Signing: ad-hoc by default (Gatekeeper warning on first open). See build/mac-config.js for the optional Apple signing variables.
set -euo pipefail
cd "$(dirname "$0")"
step="${1:-all}"

tools() {
  mkdir -p build/tools-mac
  # yt-dlp: the official universal binary from yt-dlp's GitHub releases (Unlicense), checked against the SHA2-256SUMS file of the same release.
  local base=https://github.com/yt-dlp/yt-dlp/releases/latest/download
  curl -fsSL -o build/tools-mac/yt-dlp "$base/yt-dlp_macos"
  curl -fsSL -o /tmp/yt-dlp-SHA2-256SUMS "$base/SHA2-256SUMS"
  local want got
  want="$(awk '$2=="yt-dlp_macos"{print $1}' /tmp/yt-dlp-SHA2-256SUMS)"
  got="$(shasum -a 256 build/tools-mac/yt-dlp | awk '{print $1}')"
  [ -n "$want" ] && [ "$want" = "$got" ] || { echo "yt-dlp checksum mismatch (want '$want' got '$got')" >&2; exit 1; }
  chmod 755 build/tools-mac/yt-dlp
  echo "yt-dlp ok ($got)"
  bash build-ffmpeg-min-mac.sh
}
stage() { node build/stage-ort-mac.js; }
dmg() {
  [ -x build/tools-mac/yt-dlp ] && [ -x build/tools-mac/ffmpeg ] || { echo "run: bash build-mac.sh tools" >&2; exit 1; }
  [ -d build/ort-mac/arm64 ] || { echo "run: bash build-mac.sh stage" >&2; exit 1; }
  # GitHub Actions turns a missing secret into an empty string. electron-builder treats
  # CSC_LINK="" as a certificate path and then fails with "<workdir> not a file".
  # Drop the empty values so the build stays ad-hoc signed (identity "-").
  local v
  for v in CSC_LINK CSC_KEY_PASSWORD CSC_NAME WIN_CSC_LINK WIN_CSC_KEY_PASSWORD \
           APPLE_ID APPLE_APP_SPECIFIC_PASSWORD APPLE_TEAM_ID \
           APPLE_API_KEY APPLE_API_KEY_ID APPLE_API_ISSUER; do
    if [ -z "${!v:-}" ]; then
      unset "$v"
    fi
  done
  if [ -z "${CSC_LINK:-}${CSC_NAME:-}" ]; then
    export CSC_IDENTITY_AUTO_DISCOVERY=false
  fi
  rm -rf dist-mac
  npx electron-builder --mac --universal --config build/mac-config.js --publish never
  (cd dist-mac && shasum -a 256 Yoink.dmg | tee Yoink.dmg.sha256)
}

case "$step" in
  tools) tools ;;
  stage) stage ;;
  dmg) dmg ;;
  all) npm ci; tools; stage; npm test; dmg ;;
  *) echo "usage: bash build-mac.sh [all|tools|stage|dmg]" >&2; exit 2 ;;
esac
