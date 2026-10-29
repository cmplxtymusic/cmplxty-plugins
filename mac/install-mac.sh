#!/bin/bash
# CMPLXTY installer for Mac. Downloads the official installer with curl and installs it.
#
# Usage:
#   bash install-mac.sh <slug> [<slug> ...]   install one or more plugins (e.g. granular, neon-fx, yoink)
#   bash install-mac.sh all                   install every plugin in the list below
#   bash install-mac.sh --print <slug|all>    only print what would be downloaded (URL, version, sha256)
#   bash install-mac.sh --download <slug|all> download and verify into ./cmplxty-downloads (or $CMPLXTY_DL_DIR), do not install
#   bash install-mac.sh --list                list known slugs
#
# Why it works: files fetched with curl are not marked "downloaded from the internet" (no com.apple.quarantine),
# so macOS does not show "Apple could not verify ... is free of malware". Nothing is disabled system-wide;
# only the file this script downloads is trusted, and only after its SHA-256 matches the one published in
# https://cmplxty.music/plugins/<slug>/version.json. A mismatch aborts before anything is installed.
# Everything is fetched over HTTPS only (curl --proto =https, redirects included).
set -euo pipefail

BASE="https://cmplxty.music"
SLUGS=(glitch-crusher growl-saturator drift-shifter vocal-aligner robot-voice dark-reverb granular sidechain-shaper
       mix-tools cyber-wavetable drum-player neon-fx visualizer)
# Not in "all": yoink (an app, not a plugin; install it by name). quartz has no Mac build listed yet.

CURL=(curl --proto '=https' --proto-redir '=https' --tlsv1.2 -fsSL --retry 2)
MODE=install
die() { echo "ERROR: $*" >&2; exit 1; }

json_get() { # json_get <file> <key.path>  -> prints value or empty
  if command -v plutil >/dev/null 2>&1; then plutil -extract "$2" raw -o - "$1" 2>/dev/null || true
  else python3 - "$1" "$2" <<'PY' 2>/dev/null || true
import json,sys
v=json.load(open(sys.argv[1]))
for k in sys.argv[2].split('.'):
    v=v.get(k) if isinstance(v,dict) else None
print(v if v is not None else "", end="")
PY
  fi
}

meta_url() { [ "$1" = yoink ] && echo "$BASE/yoink-version.json" || echo "$BASE/plugins/$1/version.json"; }

resolve() { # resolve <slug> -> sets URL VER SHA
  local slug="$1" j
  [[ "$slug" =~ ^[a-z0-9-]+$ ]] || die "bad slug '$slug'"
  j="$TMP/$slug.json"
  "${CURL[@]}" -o "$j" "$(meta_url "$slug")" || die "no version.json for '$slug' (try --list)"
  URL=$(json_get "$j" mac.url); [ -n "$URL" ] || URL=$(json_get "$j" mac.installer)
  VER=$(json_get "$j" mac.version); SHA=$(json_get "$j" mac.sha256 | tr 'A-F' 'a-f')
  [ -n "$URL" ] || die "'$slug' has no Mac download listed yet"
  [[ "$URL" == https://* ]] || die "'$slug' Mac URL is not HTTPS: $URL"
  [[ "$SHA" =~ ^[0-9a-f]{64}$ ]] || die "'$slug' has no valid sha256 in version.json"
}

fetch_verify() { # fetch_verify <dest>
  echo "Downloading $URL ..."
  "${CURL[@]}" --progress-bar -o "$1" "$URL" || die "download failed"
  local got; got=$(shasum -a 256 "$1" 2>/dev/null | cut -d' ' -f1 || sha256sum "$1" | cut -d' ' -f1)
  if [ "$got" != "$SHA" ]; then rm -f "$1"; die "SHA-256 mismatch for $(basename "$URL"): expected $SHA, got $got. Nothing was installed."; fi
  echo "SHA-256 OK: $got"
}

install_one() {
  local slug="$1"; resolve "$slug"
  local name; name=$(basename "$URL")
  case "$MODE" in
    print) printf '%-18s %-8s %s\n%-18s sha256 %s\n' "$slug" "$VER" "$URL" "" "$SHA"; return ;;
    download) mkdir -p "$DL"; fetch_verify "$DL/$name"; return ;;
  esac
  local f="$TMP/$name"; fetch_verify "$f"
  xattr -c "$f" 2>/dev/null || true
  case "$name" in
    *.pkg)
      echo "Installing $slug $VER (asks for your Mac password) ..."
      sudo installer -pkg "$f" -target / ;;
    *.dmg)
      local mnt="$TMP/mnt-$slug"; mkdir -p "$mnt"
      hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$f" >/dev/null
      local app; app=$(find "$mnt" -maxdepth 1 -name '*.app' | head -1); [ -n "$app" ] || { hdiutil detach "$mnt" >/dev/null; die "no .app in $name"; }
      rm -rf "/Applications/$(basename "$app")"; cp -R "$app" /Applications/
      hdiutil detach "$mnt" >/dev/null
      xattr -dr com.apple.quarantine "/Applications/$(basename "$app")" 2>/dev/null || true
      echo "Installed /Applications/$(basename "$app")" ;;
    *) die "unknown installer type: $name" ;;
  esac
}

ARGS=()
for a in "$@"; do case "$a" in
  --print|-n) MODE=print ;;
  --download|-d) MODE=download ;;
  --list) printf '%s\n' "${SLUGS[@]}" yoink; exit 0 ;;
  -h|--help) sed -n '2,10p' "$0" 2>/dev/null || echo "usage: bash install-mac.sh [--print|--download] <slug|all>"; exit 0 ;;
  -*) die "unknown option $a" ;;
  *) ARGS+=("$a") ;;
esac; done
[ ${#ARGS[@]} -gt 0 ] || die "usage: bash install-mac.sh [--print|--download] <slug ...|all>   (--list shows slugs)"
EXP=(); for s in "${ARGS[@]}"; do if [ "$s" = all ]; then EXP+=("${SLUGS[@]}"); else EXP+=("$s"); fi; done; ARGS=("${EXP[@]}")
if [ "$MODE" = install ]; then [ "$(uname)" = Darwin ] || die "installing only works on macOS (try --print or --download)"; fi
DL="${CMPLXTY_DL_DIR:-$PWD/cmplxty-downloads}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
for s in "${ARGS[@]}"; do install_one "$s"; done
[ "$MODE" = install ] && echo "Done. Restart your DAW and rescan plugins."
exit 0
