# CMPLXTY Silt

Free **smart granular synth** by CMPLXTY (VST3 + CLAP + Standalone app, Windows / macOS / Linux, GPLv3). Type: **Synth**.
Drop any sample on it and play it from your keyboard as a grain cloud, or chop it into slices and play / sequence the chops.
Inspired by smart granular instruments like Murmur; Silt is its own design and code.

## How it works
1. Drag any sample onto the big display (layer **A**) or onto the thin **B** strip (layer **B**), or press **LOAD...** / **BUILT-IN**.
   WAV, AIFF, FLAC, OGG and MP3, up to 60 s. Two files dropped at once go to A, then B. Six built-in samples are included.
2. **Tune**: Silt reads the root key from the file name ("Pad A2", "vox_F#3", "Keys-C") and finds it by ear when the name has none,
   so **C plays C**. Tune Off plays the sample at its own pitch, Manual uses the ROOT KEY knob; **USE DETECTED ROOT** copies the detected key.
   **Scale Lock** snaps every played note to a scale (13 scales, any key). **Cohere** pulls each grain onto the played pitch so even noisy
   material sings in tune.
3. **Grain**: Position, Scan (playhead speed), Density (up to 200 grains / s), Size (10-500 ms), Size Random, Spray, Pitch, Fine, Pitch Jitter,
   Glide, Spread (stereo), Shape (window), Reverse, Level Comp, and **Magnet**, which pulls grain starts onto detected transients.
   The display shows every live grain as a dot in the grain cloud.
4. **Slice mode**: the sample is chopped at its transients (or an equal 4 / 8 / 16 / 32 grid). One key per chop starting at FIRST KEY (C1 by default).
   Drag a slice line to move it, double-click to add one, right-click to delete (the grid then switches to Manual). Play: One Shot, Gate or Granular.
   The 16-step **sequencer** picks a chop per step, syncs to the host tempo, and runs while you hold a key or while the DAW plays.
5. **Layer B**: a second sample with Morph (Blend, Swap, Ring, Cross), offset, pitch and **Key Match** (B is tuned to A's key).
6. **Mod**: 2 LFOs (tempo sync), a mod envelope, 4 macros, per-grain random, velocity, note, mod wheel, MPE pressure and timbre,
   8 mod slots. Drag a MOD chip onto any knob to assign it (right-click a knob for its menu).
7. **FX** (drag the blocks to reorder): tempo-synced beat repeat, compressor, tempo-synced delay, reverb. Every grain also has its own filter
   (low / band / high pass, notch) with random cutoff per grain and key tracking.
8. Up to 16 voices, glide, pitch bend range, MPE.

### Good to know
- **20 factory presets** plus your own presets (save / load / delete). Dropped samples are stored inside your presets and inside the project
  (24-bit FLAC), so a project still sounds right when the original file is gone. Built-in samples are stored by name only.
- **Keep My Sample** (on by default): a preset changes the sound, not your dropped sample. Switch it off to let presets load their own sample.
- Smooth bypass. Space and every key Silt does not use go to the host.
- Update check when Silt opens (at most once a day; UPDATE / LATER / SKIP banner), on Windows and Mac; switch it off at the bottom right.

## Install (users)
Windows: run `CMPLXTY-Silt-Setup-<version>-win.exe`. It installs the VST3, the CLAP and the app (Start menu > CMPLXTY > Silt), and updates in place.
In FL Studio: Options > Manage plugins > Find plugins; Silt is under Installed > Generators. MIDI note 60 is C3 in Silt (FL Studio calls it C5).
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.
Settings and your presets: Windows `%APPDATA%\CMPLXTY\Silt`, macOS `~/Library/Application Support/CMPLXTY/Silt`. Updates never touch them.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Silt_VST3 Silt_CLAP Silt_Standalone
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows, cross-built on Linux (clang-cl + xwin, NSIS)
scripts/build-macos-pkg.sh                                   # on a Mac
```

### Tests
- `SiltDspTest` (`test/dsp_test.cpp`, no JUCE): root from file names, pitch detection, C plays C (spectrum + YIN), Cohere, Magnet,
  scale lock, glide, key match, MPE, transient slicing, key per chop, sequencer timing, beat repeat, clicks, fuzzing, denormals, CPU.
- `SiltScreenshot proc`: loading and errors, root key, C plays C through the plug-in, voices, slices, sequencer with the host,
  modulation, bypass, state round trip with the files deleted, presets with samples, every factory preset at 44.1 / 48 / 96 kHz.
  Other modes: `cpu`, `keys`, `drop`, `params` and the screenshots.
- `SiltUpdateTest`: update checker against a local test server (Windows and Mac blocks).
- Validated with pluginval (strictness 5) and clap-validator on Linux. Measurements and plots: see NOTES.
