# CMPLXTY Mix Tools

Free mix-bus utility by CMPLXTY: three modules in one plug-in, a **stereo widener**, a **transient shaper** and a **true-peak limiter**.
Switch each on or off and pick the chain order. It is inspired by the usual widener / transient / limiter utilities but is its own design
and not a clone of any product.

### Controls
1. **WIDENER** (power switch): **Width** 0-200 % (0 = mono, 100 = unchanged, 200 = doubled side). **Mono Bass** keeps everything below
   20-500 Hz in mono with a Linkwitz-Riley 24 dB/oct crossover on the side signal (20 Hz = off).
2. **SHAPER** (power switch): **Attack** ±100 % boosts or softens hits. **Sustain** ±100 % brings up or dries out the tail.
   **Speed** is Fast, Medium or Slow. A steady tone passes unchanged.
3. **LIMITER** (power switch): **Drive** 0-24 dB, **Ceiling** -12..0 dBTP (true peak, 8x oversampled), **Release** 1-1000 ms.
   It uses 1.5 ms of lookahead.
4. **OUTPUT**: **Output** gain and **Order** (Widen > Shape > Limit, or Shape > Widen > Limit). The limiter is always last.

The display shows the chain in its current order, with a vectorscope and correlation meter for the widener, the shaper's gain over time,
the limiter's gain reduction and output against the ceiling, and in/out meters. Latency is constant (77 samples at 48 kHz) and reported to
the host, whatever is switched on.

### Presets
18 factory presets (Init, Master Safe -1 dBTP, Loud Master, Streaming -1 dBTP, Wide Pads, Mono Check, Tight Low End, Punchy Drums,
Snappy Snare, Room Boost, Tame Peaks, Dry Up The Room, Plucky Guitar, Vocal Width, Bass Bus, Brickwall Smash, Super Wide Synth, Soft Glue).
Use the **< >** buttons to step through them, or click the name for the menu: **Save preset as...**, **Load preset file...**,
**Open user preset folder** and every factory and user preset.
Your presets are `.cmplxtypreset` files in `%APPDATA%\CMPLXTY\Mix Tools\Presets` (Windows) or `~/Library/Application Support/CMPLXTY/Mix Tools/Presets` (Mac).
Installers and updates never touch that folder.

- Monochrome UI. Undo/redo (preset loads too). Double-click a knob to reset it. Right-click any control for its menu (**Reset**,
  **Type in value**, **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so
  **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/mix-tools/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
Widener: M/S; the side is scaled by Width and, with Mono Bass on, high-passed by two cascaded Butterworth biquads (LR4).
Shaper: a fast envelope and two slower followers of it. Their differences give the attack and sustain gains, which are clamped to -30..+12 dB
and smoothed. Limiter: an 8x polyphase true-peak estimate, a sliding-minimum gain over the lookahead window, release, then a box average over
the same window, applied to the delayed signal, with a final safety clamp at the ceiling.

## Install (users)
Windows: run `CMPLXTY-Mix-Tools-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings and presets.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target MixTools_VST3 MixTools_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: all off = exact delayed passthrough, limiter transparent below the ceiling, width 0 / 200 %, mono bass,
  correlation meter, attack / sustain ±, steady tone unchanged, true-peak ceiling at -1 / -0.1 / -6 dBTP within 0.25 dB (16x reference),
  GR meter, spike recovery, extremes and NaN in both orders, speed.
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus, menu and preset audits), `params`, `proc` and `presets`. `proc` covers processBlock, meters,
  latency reporting, the limiter ceiling, all modules off = delayed dry, width 0 = mono, mono layout, **every factory preset at 44.1 / 48 / 96 kHz (finite, bounded, audible)**, a user preset file round
  trip, and state round trip (including the preset name).
- Validated with pluginval (strictness 5) and clap-validator on Linux.
