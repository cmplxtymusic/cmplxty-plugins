# CMPLXTY Dr Fill

Free **drum loops in, one fill out** plug-in by CMPLXTY (VST3 + CLAP + Standalone app, Windows / macOS / Linux, GPLv3).
Drop a kick loop, a snare loop and a hat / cymbal loop into their boxes. Dr Fill finds every hit, slices it, works out the tempo and
writes a drum fill from your own sounds. Drag the full fill, or just the kick, snare or hats part, straight into FL Studio (or any DAW).

## What's new in 1.0.1

- **Five loop lanes:** Kicks, Snares, Hats, Cymbals and an optional Perc lane (shaker, tambourine or conga, played on the off-beats). Hats and cymbals are now separate lanes, and the cymbal lane provides the crash at the end of the fill.
- **Drag-outs are cut to the grid:** each drag-out is a whole number of bars, 4 bars by default. Use the **DRAG** button to choose Fill Only, 1, 2, 4 or 8 bars. The fill sits at the end of the clip.
- **Three combined drag-outs** sit next to the per-lane ones: **Drum Fill Loop** (kicks and snares), **Drum Fill Top Loop** (hats, cymbals and perc) and **Full Drum Fill Loop** (everything).
- **FL Studio fix:** dragging audio in or out no longer makes the plugin window vanish behind FL, even when the window is detached.
- **Tempo:** Dr Fill follows the host BPM and shows it in the header, for example "128.0 BPM (host)".
- **MIDI triggers:** 36 Kick, 38 Snare, 42 Hats, 49 Cymbals, 39 Perc, 48 Drum Fill Loop, 50 Top Loop. Any other note plays the full fill.

## How it works
1. Drag a loop onto **KICKS**, **SNARES** or **HATS / CYMBALS** (or press **LOAD...**). WAV, AIFF, FLAC, OGG and MP3, up to 60 s each.
   Several files dropped at once are sorted by name (kick/kik/bassdrum, snare/snr/clap/rim, hat/hh/cym/crash/ride/shaker/perc), unnamed files fill the empty boxes.
   One box is enough; a snare loop alone already makes a fill.
2. Dr Fill detects the onsets, slices each hit (short fade before the transient, cut just before the next hit) and sorts them
   (main hits, alternates, ghosts; hats into closed / open / crash by how long they ring).
3. It builds a fill with the hits: snare rolls, build-ups, tom-style runs (pitched snares), triplets, flams, break chops and stutters,
   ending on an open hat or a crash if you want.
4. **PREVIEW** plays it (in time with the host, ending on the next bar line while the host is playing), **REGENERATE** / the seed arrows give a new one.
5. Drag **DRAG FULL FILL** or one part onto your playlist. **SAVE WAVS** writes all four WAVs to `Documents/CMPLXTY Dr Fill/`.

### Controls
- Fill: **Length** (1 beat, 2 beats, 1 bar, 2 bars), **Style** (Auto, Snare Roll, Build-Up, Tom Run, Triplets, Flams, Break Chop, Stutter), **Ending** (Off, Open Hat, Crash),
  **Density**, **Complexity**, **Ramp** (how much it builds towards the end).
- Feel: **Swing**, **Humanize**, **Flams**, **Pitch Glide** (-12..+12 semitones across the fill, for tom runs).
- Parts: **Kick** and **Hats** amount. Output: **Level**, **Tempo** (AUTO = host tempo in the plug-in, detected loop tempo in the app; or set it by hand), **Hit Sens** (onset sensitivity).
- **MIDI**: note 36 plays the kick part, 38 the snare part, 42 the hats part, any other note plays the full fill (velocity sets the level). Sample-exact.
- Audio coming in passes through untouched; the fill is added on top. Smooth bypass. Space goes to the host. Right-click a knob for its menu.
- **16 factory presets** plus your own presets (save / load / delete). The loops, the settings and the seed are saved with the project.
- Update check once a day (UPDATE / LATER / SKIP banner); switch it off at the bottom right.

The fill is generated from its own random generator, so the same seed gives the same fill on Windows, Mac and Linux.

## Install (users)
Windows: run `CMPLXTY-Dr-Fill-Setup-<version>-win.exe`. It installs the VST3, the CLAP and the app (Start menu > CMPLXTY > Dr Fill), and updates in place.
In FL Studio: Options > Manage plugins > Find plugins; Dr Fill is under Installed > Generators.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.
Settings and your presets: Windows `%APPDATA%\CMPLXTY\Dr Fill`, macOS `~/Library/Application Support/CMPLXTY/Dr Fill`. Updates never touch them.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target DrFill_VST3 DrFill_CLAP DrFill_Standalone
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows, cross-built on Linux (clang-cl + xwin, NSIS)
scripts/build-macos-pkg.sh                                   # on a Mac
```

### Tests
- `DrFillDspTest` (`test/dsp_test.cpp`, no JUCE): synthetic kick / snare / hat loops with known hit times at 85-174 BPM; onset precision / recall / timing,
  tempo, slicing, every style x length x ending, peak limit, click-free slice edges, chokes, swing, seeds, speed.
- `DrFillScreenshot proc`: passthrough, loading and errors, tempo, MIDI playback, preview, host-tempo re-render, bypass, WAV export, state round trip, presets at 3 sample rates.
- `DrFillUpdateTest`: update checker against a local test server.
- Validated with pluginval (strictness 5) and clap-validator on Linux. Measurements and plots: see NOTES.
