# CMPLXTY Granular

Free granular effect by CMPLXTY: it chops the incoming audio into grains and sprays them back out as glitchy clouds, smears and
textures. **Freeze** holds the cloud, grains can play **reversed**, and **pitch spread** scatters them freely or quantised to semitones,
fifths or octaves. Inspired by granular texture effects in general; it is its own design and not a clone of any product.

## Use
1. Put it on a track or a send. Zero latency. Mix 100 % on a send, 30-60 % on a track.
2. **GRAINS**: **Size** 10-1000 ms per grain, **Density** 1-80 grains per second, **Shape** of each grain's envelope (Smooth, Triangle,
   Perc = fast attack and decay, Glitch = hard-edged squares).
3. **CLOUD**: **Position** = how far back in the last 4 s the grains read, **Spray** = random extra delay per grain, **Reverse** = chance that a
   grain plays backwards, **Chaos** = timing jitter, **Width** = random stereo placement.
4. **PITCH**: **Pitch** moves every grain ±24 semitones, **Spread** scatters each grain up to ±24 semitones, **Spread Mode**: Free, Semitones,
   Fifths or Octaves.
5. **OUTPUT**: **Freeze** stops recording, so the cloud keeps playing what is in the buffer. **Feedback** sends the cloud back into the
   buffer (it stays under control at any setting), **Tone** lowpass, **Mix** and **Output**.

The display shows the last 4 seconds of audio, the position / spray window and every live grain: left-right = where it reads, up-down = its
pitch, size = its envelope, ring = reversed.

### Presets
16 factory presets (Soft Cloud, Glitch Stutter, Reverse Swell, Octave Shimmer, Freeze Pad, Fifths Choir, Down Under, Bit Rain, Tape Scatter,
Granular Delay, Robot Chatter, Infinite Wash, Perc Sparkle, Dark Matter, Micro Smear and Init). Use the **< >** buttons to step through them or click
the name for the menu: **Save preset as...**, **Load preset file...**, **Open user preset folder** and every factory and user preset.
Your presets are `.cmplxtypreset` files in `%APPDATA%\CMPLXTY\Granular\Presets` (Windows) or `~/Library/Application Support/CMPLXTY/Granular/Presets` (Mac).
Installers and updates never touch that folder.

- Monochrome UI. Undo/redo (preset loads too). Double-click a knob to reset it. Right-click any control for its menu (**Reset**,
  **Type in value**, **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so
  **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/granular/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
A 4 s stereo circular buffer records the input (Freeze crossfades recording off in 8 ms). A scheduler starts grains at Density with Chaos
jitter, up to 64 at a time. Each grain reads the buffer at Position + random Spray with 4-point Hermite interpolation at its own pitch ratio,
forwards or backwards, keeping its read head inside valid audio. It applies one of four windows and an equal-power pan. The sum is normalised
by the square root of the overlap, filtered by two one-pole lowpasses (Tone) and soft-limited. Feedback is mono, normalised by the grain
envelope sum and passed through tanh before it goes back into the buffer, so it cannot run away.

## Install (users)
Windows: run `CMPLXTY-Granular-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings and presets.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Granular_VST3 Granular_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: silence in = silence out, mix 0 = dry, pitch 0 / +12 / -12 measured by frequency, reverse keeps pitch and plays a
  rising ramp backwards, freeze holds and ignores new input, unfreeze dies away, stress peak bound, feedback tail lasts and decays,
  NaN recovery, every shape x spread mode x sample rate x mono / odd block sizes, spread quantisation, display taps, speed.
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus, menu and preset audits), `params`, `proc` and `presets`. `proc` covers processBlock, meters,
  freeze on/off, mono layout, mix 0 = dry, **every factory preset at 44.1 / 48 / 96 kHz (finite, bounded, audible)**, a user preset file round
  trip, and state round trip (including the preset name).
- Validated with pluginval (strictness 5) and clap-validator on Linux.
