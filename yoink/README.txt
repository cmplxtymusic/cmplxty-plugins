Yoink 2.1.0 (free, by CMPLXTY)
==============================
Type a phrase, Yoink searches YouTube captions and gives you the matching audio clips, ready to drag into your DAW.

INSTALL
  Run Yoink-Setup.exe (downloaded from https://cmplxty.music/yoink.exe). One click, no admin rights.
  It installs to %LOCALAPPDATA%\Programs\yoink-cmplxty and adds Desktop + Start Menu shortcuts.
  If an older Yoink is installed and you run the installer yourself it asks: "An older version of Yoink is installed. Update it by installing this version
  and removing the old one?"  Yes = new version installed, old one removed.
  If the old portable v1 folder (Desktop\Yoink\Yoink-CMPLXTY-win64 or %LOCALAPPDATA%\Yoink) is found, it asks before removing it.
  Your favorites, history, settings and clips are kept (they live in %APPDATA%\Yoink and your clips folder).
  Silent install: Yoink-Setup.exe /S  (updates an existing install, no prompts, leaves any v1 folder alone).
  Windows SmartScreen may warn because the installer is not code-signed: More info -> Run anyway.

FEATURES
  Filters: Creative Commons, Manual subtitles, Language, Category, Search region, Channel, Title, Views min/max, Uploaded (YYYY/MM).
  Cards: category, heart (favorites), open on YouTube, waveform with trim flags (drag the flags, the clip is re-cut on release),
         Play preview, and a drag handle to drag the clip (just the excerpt) into your DAW or a folder.
  Found N matches + Load more, History, Favorites, Settings (WAV/MP3/M4A, output folder, padding, default filters, volume).
  Check for updates button.

NEW IN 2.1
  Update notice: Yoink checks https://cmplxty.music/yoink-version.json at start (at most once a day, nothing about you is sent).
    If there is a newer version a banner offers [Update now] (downloads, checks the SHA-256, runs the installer, which keeps your data),
    [Later] or [Skip this version]. Turn the check off in Settings.
  Load more: fixed. It now always comes back after a search, Stop or an error, and "No more results" shows only when the scan really ran out.
  Clean up (vocal isolation): on each clip and in its menu. First use downloads a 166 MB model (MIT licensed, HT-Demucs) once, then it works offline.
    Needs the free Microsoft Visual C++ Redistributable (x64) that most PCs already have. About 3 s of CPU per 8 s of audio; ~1 GB RAM while it runs.
  Advanced filters: Channel, Title, Views and Uploaded are in a drop down with a badge that counts the ones in use.
  Optional sign in (guest by default, asked once). Real cross-PC sync needs a Google client id in resources/sync-config.json, see SIGNIN-PLAN.md.
  Also: key detection (estimate, Camelot), normalize and fade-out on export, favorite tags, keyboard shortcuts
    (Space play, J/K move, F favorite, C clean up, S save, / or Ctrl+K search, Ctrl+, settings).

LITE BUILD (Yoink-Setup-Lite.exe)
  Same app without the bundled yt-dlp (~25 MB smaller). It downloads yt-dlp from GitHub on the first search, so it needs internet then.

BUILD (Linux)
  npm install; place build/electron-dist, build/tools/{yt-dlp.exe,ffmpeg.exe}; run ./build-win.sh
  ffmpeg.exe is a minimal audio-only LGPL build (see build-ffmpeg-min.sh and build/tools/ffmpeg-LICENSE.txt).
  Only download clips you have the right to use.
