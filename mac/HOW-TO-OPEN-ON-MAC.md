# Opening CMPLXTY downloads on a Mac

Our Mac downloads are not yet signed with an Apple Developer ID (that costs $99 a year), so macOS shows
"Apple could not verify ... is free of malware". The files are the same ones built in public on GitHub; the download page shows the SHA-256.

## Easiest: one command (no warning at all)
Open Terminal (Spotlight > Terminal) and paste ONE line:

    curl -fsSL https://cmplxty.music/install-mac.sh | bash -s glitch-crusher
    (replace glitch-crusher with the plugin name from its page address, e.g. granular or neon-fx; "all" installs every plugin. Add --print first to see exactly what it would download.)

Files fetched by curl are not flagged by Gatekeeper. The plugins ask for your Mac password (installs to /Library/Audio/Plug-Ins).

## Already downloaded the file in your browser?
Option A, Terminal (works on macOS 13, 14, 15 and 26):

    xattr -dr com.apple.quarantine ~/Downloads/CMPLXTY-Growl-Setup-1.0.0-mac.pkg

then double-click it. For Yoink: `xattr -dr com.apple.quarantine /Applications/Yoink.app` after dragging it to Applications.

Option B, Settings (macOS 13/14/15/26):
1. Double-click the file. In the warning click **Done** (NOT Move to Trash).
2. Apple menu > System Settings > Privacy & Security, scroll down to **Security**.
3. Next to "<file> was blocked to protect your Mac" click **Open Anyway**, enter your Mac password, confirm **Open**.
   The button is only there for about an hour after the warning. On macOS 14 and earlier you can also right-click the file > Open.
   If there is no Open Anyway button (this happens with some unsigned apps on macOS 15/26), use Option A.
