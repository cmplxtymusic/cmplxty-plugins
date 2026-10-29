# CMPLXTY Dark Reverb

Free reverb by CMPLXTY for huge, dark, metallic spaces: up to 30 s of decay, a **shimmer** loop that pitch-shifts the tail upward
(or down), and **freeze** for endless pads. Inspired by big ambient / cinematic reverbs. It is its own design and not a clone of any product.

## Use
1. Put it on a send/return (Mix 100 %) or straight on a track (Mix 20-40 %). Zero latency.
2. **SPACE**: **Pre-Delay** 0-500 ms, **Size** (room scale), **Decay** 0.3-30 s (time to fall 60 dB), **Diffusion** (smear of the early echoes).
3. **TONE**: **Dark** is the lowpass inside the tail (lower = darker, the tail gets darker as it decays), **Low Cut** removes rumble going in,
   **Metal** adds tuned combs and allpasses for a ringing, metallic plate/pipe colour, **Mod** gently moves the delay lines to avoid ringing.
4. **SHIMMER**: switch it on, pick **Pitch** (+12, +7, +19, +24 or -12 semitones) and **Amount**. The shifted tail is fed back, so it climbs.
5. **OUTPUT**: **Freeze** holds the current tail forever and stops new input entering. **Width** (mono to full stereo), **Mix**, **Output** gain.

The display shows the decay envelope (flat when frozen), the recent tail level and input / wet meters.

- Monochrome UI. Undo/redo. Double-click a knob to reset it. Right-click any control for its menu (**Reset**, **Type in value**,
  **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/dark-reverb/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
Low cut and pre-delay, then an optional metallic comb, four series allpass diffusers and an 8-line feedback delay network with a
Householder matrix. Each line has a damping lowpass at Dark, optional metal allpasses and slow Hermite-interpolated modulation; line gains
are set from Decay so the tail hits -60 dB on time at any Size. Shimmer is a two-head grain pitch shifter in a feedback path with a limiter, so
it can never run away. Freeze sets the loop gain to 1, bypasses damping and closes the input.

## Install (users)
Windows: run `CMPLXTY-Dark-Reverb-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target DarkReverb_VST3 DarkReverb_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: measured T60 vs Decay, Size invariance, pre-delay timing, mix 0 = dry, wet level calibration, Dark and Metal
  spectra, shimmer octave energy, freeze hold / input gate / release, block-size independence, mono, NaN safety, stability at max settings, speed.
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus and menu audit), `params`, and `proc`. `proc` covers
  processBlock: tail after input stops, meters, tail length, decay, freeze on/off, mono layout, mix 0 = dry, and state round trip.
- Validated with pluginval (strictness 5) and clap-validator on Linux.
