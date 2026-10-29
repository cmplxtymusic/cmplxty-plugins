# CMPLXTY Sidechain Shaper

Free volume shaper by CMPLXTY. Draw a gain curve and it plays in time with your project (pumping, ducking, trance gates, chops, tremolo,
swells), or let a kick on the **sidechain input** restart it every hit. Zero latency. Inspired by curve-based volume shaping tools in
general; it is its own design and not a clone of any product.

## Use
1. Put it on the track you want to pump (bass, pads, a bus). It starts with the **Pump** curve at 1/4 notes.
2. **Draw the curve** in the display: click to add a point, drag to move it (it snaps to the grid; hold **Shift** for free movement),
   drag a segment's **diamond** (or **Alt**-drag anywhere on a segment) to bend it, double-click a point to delete it or a diamond to
   straighten it. Edits can be undone. Right-click the display for **15 curve shapes** (Pump, Hard Pump, Soft Duck, Short Duck, Long
   Release, Gate Half, Chop 8, Trance Gate, Stutter, Ramp Up, Ramp Down, Triangle, Sine, Swell, Flat), **Reverse**, **Mirror**,
   **Straighten Bends**, **Copy / Paste Curve** and the **grid** (off, 4, 8, 16, 32).
3. **TIMING**: **Mode** Tempo = the curve loops in sync with the host (it follows the play position, so it lines up with bars; when the
   transport is stopped it free-runs at the host tempo). Mode Sidechain = the curve plays once per trigger and then holds its last value.
   **Length** 1/32 to 4 bars, including triplets and dotted values. **Offset** shifts the curve against the beat.
4. **SHAPE**: **Depth** scales the effect (the dashed line shows the actual gain), **Smooth** removes clicks from hard edges, **Stereo
   Offset** plays the right channel later in the curve for movement and auto-pan.
5. **SIDECHAIN** (Mode Sidechain): **Trigger** = the sidechain input (route a kick to it in your DAW; if nothing is connected the plug-in
   listens to its own input and says so) or always the plug-in input. **Threshold** sets how loud a hit must be (the DET meter shows the
   detector and a tick for the threshold), **Focus** low-passes the detector so only the kick triggers, **Hold** is the shortest time between
   triggers.
6. **OUTPUT**: **Mix** and **Output** gain.

The display shows the length, mode, host tempo / sidechain status, the curve, the playhead (dashed second playhead for the right channel),
a flash on every trigger, and IN / GAIN / DET / OUT meters.

Sidechain routing: FL Studio = route the kick's mixer track to the shaper's track with "Sidechain to this track" and pick it in the plug-in's
wrapper input. Ableton Live / Reaper / Bitwig / Cubase = enable the plug-in's sidechain input and choose the kick track.

### Presets
20 factory presets (Init, Classic Pump, Hard EDM Pump, Soft Duck, Half-Time Pump, Subtle Glue, Dotted Pump, 16th Chop, Trance Gate, Triplet
Gate, Stutter Bar, Wobble Sine, Stereo Tremolo, Auto-Pan Triangle, Riser Bar, Swell 2 Bars, Kick Duck (SC), Bass Ducker (SC), Ghost Duck (SC)
and Auto Swell (Input)). Presets store the curve. Use the **< >** buttons to step through them or click the name for the menu: **Save preset
as...**, **Load preset file...**, **Open user preset folder** and every factory and user preset.
Your presets are `.cmplxtypreset` files in `%APPDATA%\CMPLXTY\Sidechain Shaper\Presets` (Windows) or `~/Library/Application Support/CMPLXTY/Sidechain Shaper/Presets` (Mac).
Installers and updates never touch that folder.

- Monochrome UI. Undo/redo (curve edits and preset loads too). Double-click a knob to reset it. Right-click any control for its menu (**Reset**,
  **Type in value**, **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so
  **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/sidechain-shaper/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
The curve (up to 64 points with a per-segment exponential bend) is rendered to a 1024-point table. In Tempo mode the phase comes from the
host's PPQ position (bar-aligned, with the time signature) and free-runs from the host tempo when stopped. In Sidechain mode a detector
(two one-pole low-passes at Focus, peak follower with 30 ms release) fires when it crosses Threshold, re-arms below half the threshold and
honours Hold; each trigger restarts the curve. Gain = 1 - Depth x (1 - curve), smoothed by a one-pole (Smooth), mixed with the dry signal,
then the output gain (smoothed over 20 ms). No latency, no lookahead.

## Install (users)
Windows: run `CMPLXTY-Sidechain-Shaper-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings and presets.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target SidechainShaper_VST3 SidechainShaper_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: curve maths (bends, sanitising, text round trip), tempo sync from PPQ, offset, position jumps, 3/4 bars, triplets,
  free-running when stopped, depth / mix / smoothing / output gain / stereo offset, sidechain triggering, fallback to the input when no
  sidechain is connected, trigger source, hold, focus, NaN robustness, speed.
- `test/update_test.cpp`: the update notice.
- `test/screenshot.cpp`: modes `keys` (focus, menu, preset and **curve editor** audits: add / move / bend / delete points through the mouse
  code, undo / redo, the curve menu), `params`, `proc`, `presets` and `curvemenu`. `proc` covers tempo sync from a play head, the sidechain bus
  (kicks duck the signal, silent sidechain does nothing), mono layout, mix / depth 0 = dry, **every factory preset at 44.1 / 48 / 96 kHz
  (finite, bounded, audible)**, presets carrying curves, a user preset file round trip with a drawn curve, curve undo, and state round trip.
- Validated with pluginval (strictness 5) and clap-validator on Linux.
