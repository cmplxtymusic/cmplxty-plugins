# CMPLXTY Plugin Hub

One window to install, update or reinstall every CMPLXTY VST3/CLAP plug-in (Windows + macOS, GPLv3).

- The list is read live from the per-tag GitHub releases of `cmplxtymusic/cmplxty-plugins` (newest `<slug>-vX.Y.Z` per plug-in).
  `mac-builds-*` releases are never used. Yoink and other standalone apps are left out. `cmplxty.music/plugins/<slug>/version.json`
  (the mac block on macOS) is used only when its download URL matches the release.
- Every download is checked against the release's `SHA256SUMS` / `<asset>.sha256`. A plug-in without a published checksum is shown as
  NO CHECKSUM and can't be installed (currently: Quartz on Windows).
- Installed version: VST3 `moduleinfo.json` (Windows) or `Info.plist` (macOS), in the standard folders
  (`C:\Program Files\Common Files\VST3|CLAP`, `/Library/Audio/Plug-Ins/VST3|CLAP`).
- Presets/settings (`%APPDATA%\CMPLXTY`, `%APPDATA%\Quartz`, `~/Library/CMPLXTY`, `~/Library/Quartz`) are never written to. They are
  backed up to the hub's work folder before every run, hashed before and after, and any missing or changed file is restored.
- Installing runs each plug-in's own installer: Windows `Setup.exe /S /ALLUSERS` (the hub exe requests admin once), macOS
  `installer -pkg` under one admin prompt. Plug-ins you don't tick are never touched. On refresh the hub auto-ticks every
  new or outdated plug-in (never up-to-date or NO CHECKSUM rows). Select-all does the same. An already-current plug-in is
  skipped even if it is ticked ("already up to date, skipped") — no download and no file changes. Right-click or Shift/Alt-click
  a single up-to-date row to force a reinstall/repair. The main button shows only what will change (`Install 2 · Update 1`)
  or `Everything up to date` when there's nothing to do.
- Self-update: `cmplxty.music/plugins/plugin-hub/version.json`, falling back to `plugin-hub-v*` GitHub releases. The banner only opens the page.

Build: `scripts/fetch-deps.sh`, then `cmake -S . -B build/linux -G Ninja && cmake --build build/linux --target PluginHub PluginHubTest`.
Windows: `scripts/build-windows-xwin.sh && scripts/build-installer.sh`. macOS: `scripts/build-macos-pkg.sh` (on a Mac).
Test: `HUB_7Z=7zz PluginHubTest both /tmp/hub-test` (needs network). It unpacks the real installers into a fake Windows and macOS prefix.
GUI test mode: `--prefix <dir> --as win|mac [--screenshot out.png]`.

Not yet verified: real installs on Windows/macOS, the UAC prompt, SmartScreen (unsigned), Gatekeeper on the unsigned/ad-hoc hub app and pkg,
the macOS osascript admin batch, the Quartz Windows installer's silent mode, and the macOS CI job.
