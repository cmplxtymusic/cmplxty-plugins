# CMPLXTY Neon FX

A free performance multi-FX (VST3 + CLAP, Windows / macOS / Linux) by CMPLXTY: stutter, trance gate, tape stop, filter and drive in one tempo-synced strip. GPLv3.

- **Drive**: Soft, Hard, Fold and Crush (bit depth 16 to 4 bits plus sample-and-hold), with a tone control.
- **Filter**: LP / HP / BP (stable SVF) and a 3-band **Formant** (vowels A E I O U), with a tempo-synced LFO on cutoff or vowel.
- **Trance gate**: 8 16-step patterns, 1/8 to 1/32 and triplets, depth and smoothing, locked to the host song position.
- **Stutter**: hold to capture and repeat a 1/2 to 1/32 slice (triplets too) with gate and decay; click-free fades in and out.
- **Tape stop**: hold to brake to a stop (stop time) and release to spin back up (start time).
- **Macros**: bipolar DJ filter (left = low-pass, right = high-pass, centre = off), Intensity (more drive, gate and resonance), Mix and Output.
- **HOLD pads** in the display for Stutter and Tape Stop: press and hold, or right-click to latch. Both are also automatable parameters.
- Free-runs at 120 BPM when the transport is stopped. Every module off = bit-exact passthrough.
- 18 factory presets, a preset browser, and user preset save / load.
- Right-click any control for its menu; double-click a control to reset it. The editor never takes keyboard focus, so Space still reaches your DAW.
- Optional update check (at most once a day, can be switched off).

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. See HOW-TO-OPEN-ON-MAC.md for unsigned macOS builds.

Inspired by classic performance effects (stutter, gate and tape-stop units); not a clone of any product.
