# CMPLXTY Visualizer

Free audio-reactive visualizer by CMPLXTY: VST3 and CLAP plug-in plus a standalone app. Put it on any track or on the master. Audio
passes through **unchanged** (bit-exact) and the picture follows the music: level, bass / mid / high bands, beats and the host tempo.
Add your own **JPG / PNG images** and **lyrics**, stack FX, split the screen into a grid of up to 6 x 6 tiles, and play it live from a
pop-out / fullscreen window with a setlist on your laptop keys. Inspired by video-synth style visualizers. It is its own design: no
names, artwork or presets from any other product.

## What's in it
- **45 modes in 12 groups**: SCOPE (Glow, XY, Ring, Shape), LAVA (Lava Lamp, Blob Pulse, Goo Contours), RETRO (Synth Grid, Neon Tunnel,
  Ridge Lines, Copper Bars), SPACE (Starfield, Hyperspace, Nebula, Orbits), CODE (Code Rain, Glyph Storm, Binary Stream), WASH (Color Wash,
  Gradient Sweep, Beat Blocks), STATIC (TV Static, Scanline Roll, Signal Lost), PIXEL (8-Bit Bars, Pixel Fire, Life Grid, Mosaic),
  TEXT (Lyric, Glitch Type, Marquee, Word Wall), PLASMA (Plasma, Liquid Waves, Interference, Warp Swirl), SPECTRUM (Spectrum Bars,
  Mirror Bars, Circle Spectrum, Radial Burst, Waterfall) and IMAGE (Image Pulse, Image Tiles, Image Slices, Image Pixelate).
- **34 palettes**, including 7 mono ones (Mono, Silver, Graphite, Noir, Paper, Mono Red, Mono Blue). The controls themselves are monochrome.
- **Images**: drag JPG / PNG files onto the plug-in or press LOAD IMAGE (up to 8). Images react to the music with **Pulse** (size on the
  beat), **Glow**, **Shake** and **Chroma** split, can spin and cycle on the beat, and sit **Over / Behind** the visuals, **Multiply / Screen**
  into them, cut them out as a **Mask** or become the **Texture** a mode is painted with. Images are saved inside your project and inside
  user presets (large pictures are scaled to 1920 px).
- **Text**: type lyrics or words (one per line), choose a .ttf / .otf font if you like, and animate with Pulse, Glitch, Typewriter, Flash,
  Wave, Zoom or Shake. Lines change on every beat, bar, 2 / 4 bars. Use a TEXT mode or lay the text over any other mode.
- **FX**: **TEXTURE** (Noise, Grain, CRT, Threshold, VHS), **DITHER** (Symbol / ASCII, Star, Hex, Dots, Bayer), **TRACK** (Boxes,
  Crosshair, Brackets, Network: a HUD that locks onto the brightest moving spots) and **GLASS** (Kaleido, Lens, Prism, Ripple, Shards).
- **Multi-grid**: Off, 2x1, 1x2, 2x2, 3x1, 3x2, 3x3, 4x3, 4x4, 5x5, 6x6. Each tile can run its own preset; **Link** sets how tiles
  relate (Free, Same, Mirror, Cascade, Bands, Beat Swap) and **Gap** sets the spacing.
- **87 factory presets** in groups, plus your own (SAVE). User presets are files in your settings folder and include the images.
- **Performance**: POP OUT or FULLSCREEN opens a separate window. Click into it, then: keys **1-0, Q-P, A-L, Z-M** play the 36 setlist
  slots, **LEFT / RIGHT** previous / next preset, **UP / DOWN** mode, hold **ENTER** to strobe (synced 1/4 to 1/32), **BACKSPACE** blackout,
  **F11** fullscreen, **F1** help. These keys only work in the focused pop-out window: the plug-in window never takes keyboard focus,
  so **Space** and your other shortcuts still reach the DAW.
- Every main parameter is automatable. Right-click any control for **Reset / Type in value / Copy / Paste / Automate / Init all**
  (shortcut letters underlined); double-click resets. Undo / redo. Resizable window. Quality (Low / Medium / High / Ultra = 270 / 360 / 540 / 720 lines) and frame cap (24 / 30 / 60).
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/visualizer/version.json` per 24 h on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## How it works (short)
The audio thread only measures: RMS / peak level, bass / mid / high energies from biquad filters, onset detection (log-energy flux)
and the host transport (tempo, beat position, playing). It pushes analysis frames and the raw samples (for scopes and the spectrum)
into lock-free FIFOs and never allocates or locks. The UI thread drains the FIFOs, auto-gains and smooths the bands, runs the FFT and
the beat tracker, and renders with a software renderer (no GPU needed) at the chosen quality: mode, then image and text layers, glow,
trails and the FX stack, scaled to the window. Pixel shaders use several cores; the frame rate is capped.

## Install (users)
Windows: run `CMPLXTY-Visualizer-Setup-<version>-win.exe`. It installs the VST3, the CLAP and the standalone app. Installing over an old
version updates it in place and keeps your settings, presets and images (`%APPDATA%\CMPLXTY\Visualizer`).
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Visualizer_VST3 Visualizer_CLAP Visualizer_Standalone
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/analysis_test.cpp`: level / band / beat / tempo analysis on synthetic signals (82 checks).
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp` (run under Xvfb; the `keys` mode needs a window manager):
  - `proc`: bit-exact passthrough (stereo and mono), FIFO, host tempo, state round trip with images / text / tiles / setlist, huge images, user presets.
  - `presets`: every factory preset loads exactly and renders a visible picture.
  - `params`: every parameter is automatable.
  - `keys`: focus audit. The plug-in window ignores keys and never takes focus; pop-out keys, strobe, blackout.
  - `cpu`: audio-thread cost and per-mode render time.
  - Without a mode it writes screenshots.
- Validated with pluginval (strictness 5) and clap-validator on Linux.

Licence: GPLv3 (see COPYING). UI font: Barlow (SIL OFL 1.1).
