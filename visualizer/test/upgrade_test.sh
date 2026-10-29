#!/bin/bash
# Visualizer upgrade test: 0.9.9 -> 1.0.0 (all users, silent). Checks VST3, CLAP, standalone exe, uninstaller,
# that settings / Presets / Images survive the upgrade and the uninstall, then cleans up. Based on tools/upgrade_test.sh.
source /workspace/tc/env.sh
export WINEPREFIX=/workspace/qbuild/wineprefix WINEDEBUG=-all WINEDLLPATH=$SR/usr/lib/x86_64-linux-gnu/wine
W="$SR/usr/lib/wine/wine64"; C=$WINEPREFIX/drive_c; N="CMPLXTY Visualizer"; OLD=$1; NEW=$2; STAGE=$3
SD="$C/users/$USER/AppData/Roaming/CMPLXTY/Visualizer"
VST="$C/Program Files/Common Files/VST3/$N.vst3"; CL="$C/Program Files/Common Files/CLAP/$N.clap"; APP="$C/Program Files/CMPLXTY/Visualizer"
ok=1; chk() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; ok=0; fi; }
timeout 300 xvfb-run -a $W "$OLD" /S /ALLUSERS
chk "0.9.9 installed (vst3)" '[ -f "$VST/Contents/x86_64-win/$N.vst3" ]'
mkdir -p "$SD/Presets" "$SD/Images"; echo '{"marker":"keep-me"}' > "$SD/settings.json"; echo p > "$SD/Presets/mine.vzpreset"; echo i > "$SD/Images/cover.png"
timeout 300 xvfb-run -a $W "$NEW" /S /ALLUSERS
chk "vst3 is the new build" 'cmp -s "$VST/Contents/x86_64-win/$N.vst3" "$STAGE/$N.vst3/Contents/x86_64-win/$N.vst3"'
chk "moduleinfo.json present" '[ -f "$VST/Contents/Resources/moduleinfo.json" ]'
chk "clap is the new build" 'cmp -s "$CL" "$STAGE/$N.clap"'
chk "standalone exe is the new build" 'cmp -s "$APP/$N.exe" "$STAGE/$N.exe"'
chk "uninstaller present" '[ -f "$APP/Uninstall.exe" ]'
chk "settings kept" 'grep -q keep-me "$SD/settings.json"'
chk "user presets kept" '[ -f "$SD/Presets/mine.vzpreset" ]'
chk "user images kept" '[ -f "$SD/Images/cover.png" ]'
chk "no stale .old files" '[ -z "$(find "$VST" "$APP" -name "*.old" 2>/dev/null)" ]'
chk "version in registry is 1.0.0" '$W reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B5206C41-A0B6-426F-A2B8-2424C562B8C1}" /v DisplayVersion 2>/dev/null | grep -q 1.0.0'
timeout 300 xvfb-run -a $W "$APP/Uninstall.exe" /S _?="Z:$(echo "$APP" | tr / '\\')"; sleep 2
chk "uninstall removed vst3" '[ ! -e "$VST" ]'
chk "uninstall removed clap" '[ ! -e "$CL" ]'
chk "uninstall removed exe" '[ ! -e "$APP/$N.exe" ]'
chk "uninstall kept presets/images (silent)" '[ -f "$SD/Presets/mine.vzpreset" ] && [ -f "$SD/Images/cover.png" ]'
rm -rf "$VST" "$CL" "$SD" "$APP"; rmdir "$C/Program Files/CMPLXTY" "$C/users/$USER/AppData/Roaming/CMPLXTY" 2>/dev/null
$W reg delete "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B5206C41-A0B6-426F-A2B8-2424C562B8C1}" /f >/dev/null 2>&1; $W reg delete "HKLM\\Software\\CMPLXTY\\Visualizer" /f >/dev/null 2>&1
[ $ok = 1 ] && echo "UPGRADE TEST PASS" || echo "UPGRADE TEST FAIL"
