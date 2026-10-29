# CMPLXTY Robot Voice

Free robot-voice effect by CMPLXTY: hard **pitch snap** to a key and scale, a **channel vocoder** whose built-in carrier follows the
snapped pitch (or the MIDI notes you play), and a **ring modulator**, in one chain. Inspired by the classic robot / talk-box vocal sounds.
It is its own design and not a clone of any product.

## Use
1. Put it on a vocal (or any monophonic) track. Everything works in real time with **zero latency**.
2. **SNAP**: pick **Key** and **Scale** (Chromatic, Major, Minor, Pentatonic, Blues). **Retune** 0 ms is the hard, stepped robot snap;
   higher values glide.
3. **VOCODER**: **Blend** mixes the vocoded sound with the snapped voice. **Carrier**: Saw, Square, Buzz or Whisper. **Bands**: 8, 16 or 24.
   **Octave** moves the carrier -2..+1 octaves. Send MIDI notes to the plug-in to play the carrier yourself (last held note wins);
   without MIDI it follows the snapped vocal pitch.
4. **RING**: **Freq** 20 Hz - 2 kHz and **Amount** for metallic, Dalek-style tones.
5. **OUTPUT**: dry/wet **Mix** and **Output** gain.

The display shows the detected (grey) and snapped (white) pitch, the current note, and the vocoder band levels.

- Monochrome UI. Undo/redo. Double-click a knob to reset it. Right-click any control for its menu (**Reset**, **Type in value**,
  **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/robot-voice/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
YIN pitch tracking on a decimated copy (64-sample hop), snapped to the scale and smoothed by Retune; a two-tap crossfading delay-line
shifter moves the voice. The vocoder splits the voice and the carrier into 8/16/24 log-spaced bands (two cascaded band-passes each),
follows each band's envelope and imposes it on the carrier, with noise added on unvoiced consonants. The ring modulator multiplies by a sine.

## Install (users)
Windows: run `CMPLXTY-Robot-Voice-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target RobotVoice_VST3 RobotVoice_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: tracker within 1 cent, scale snapping, MIDI carrier pitch, vocoder level calibration per band count,
  ring-mod sidebands, all-off = dry, block-size independence, NaN safety, speed.
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus and menu audit), `params`, and `proc`. `proc` covers pitch tracking and snapping through
  processBlock, scale changes, MIDI carrier note on/off, band meters, mono layout, all-off = dry, and state round trip.
- Validated with pluginval (strictness 5) and clap-validator on Linux.
