# Opening CMPLXTY Visualizer on a Mac

Our Mac downloads are not yet signed with an Apple Developer ID (that costs $99 a year), so macOS shows
"Apple could not verify ... is free of malware". The files are the same ones built in public on GitHub; the download page shows the SHA-256.

## Option A: Terminal (works on macOS 13, 14, 15 and 26)
After downloading, open Terminal (Spotlight > Terminal) and paste:

    xattr -dr com.apple.quarantine ~/Downloads/CMPLXTY-Visualizer-Setup-1.0.0-mac.pkg

then double-click the file. Or skip the browser and download with curl, because files fetched by curl are not flagged:

    curl -fL -o /tmp/vz.pkg https://cmplxty.music/plugins/visualizer/CMPLXTY-Visualizer-Setup-1.0.0-mac.pkg && sudo installer -pkg /tmp/vz.pkg -target /

## Option B: System Settings (macOS 13/14/15/26)
1. Double-click the file. In the warning click **Done** (NOT Move to Trash).
2. Apple menu > System Settings > Privacy & Security, scroll down to **Security**.
3. Next to "CMPLXTY-Visualizer-Setup... was blocked to protect your Mac", click **Open Anyway**. Enter your Mac password and confirm **Open**.
   The button is only there for about an hour after the warning. On macOS 14 and earlier you can also right-click the file and choose Open.
   If there is no Open Anyway button (this happens with some unsigned apps on macOS 15/26), use Option A.

## What gets installed
- `/Library/Audio/Plug-Ins/VST3/CMPLXTY Visualizer.vst3` and `/Library/Audio/Plug-Ins/CLAP/CMPLXTY Visualizer.clap`
- `/Applications/CMPLXTY Visualizer.app` (standalone: pick your audio input in its Options)

Installing a newer version replaces these in place. Your presets and images in `~/Library/Application Support/CMPLXTY/Visualizer` are kept.
Logic Pro and GarageBand use Audio Units only, so they won't show the plug-in; use the standalone app next to them, or a VST3 / CLAP host
(Ableton Live, FL Studio, Reaper, Bitwig, Studio One, Cubase).
