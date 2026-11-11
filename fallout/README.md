# CMPLXTY FALLOUT 1.0.0

An original 303-style monophonic acid bass instrument with a step grid sequencer. Inspired by the broad idea of a grid-based acid bass, not a clone: no 22method or NUKE branding, graphics, layout or code are used. GPLv3.

## Voice
- Six original oscillator types, all anti-aliased with polyBLEP where that applies: **EDGE** (saw), **SLAB** (square), **DUTY** (pulse/PWM), **HORDE** (five detuned saws), **FOLD** (sine-fold), **SNARL** (noise-ring).
- A 4-pole resonant tanh ladder filter (cutoff, resonance, envelope amount, decay, accent amount) with 2x oversampling of the nonlinear path, plus drive (ADAA tanh) and glide for slides.
- **TUNE** is ±24 semitones. Accented notes are louder and get more filter-envelope depth.
- MIDI input can play the voice live (legato notes slide) or transpose the running pattern from a held key relative to C3. Sequencer and live playing can be switched from the MIDI control.

## Grid
- 16 steps by default, length 1–32, host-tempo sync, musical rates from 1/32 to 1/2 including triplets.
- Click a cell to place or remove a note on a two-octave pitch row. **OCT − / +** shifts the visible window. Drag up or down on a note to toggle accent (gold), left or right to toggle slide (teal tail).
- **MICRO** and **RUBATO** modes edit per-step pitch offset in cents and timing nudge as a fraction of a step. Hold Shift for fine adjustment. **RUBATO** (the knob) is a global humanize amount applied to those nudges.
- Randomize, mutate, clear, shift left/right, copy and paste. The pattern is stored in plugin/project state and in user presets.
- In a DAW the sequencer follows the host transport. In the standalone app it runs on an internal clock while SEQ is on.

## Delay, output, drag-out
- Tempo-synced delay: TIME, FEEDBACK, DLY MIX, DLY TONE.
- OUTPUT −24 to +6 dB, plus a non-bypassable linked sample-peak limiter at −1 dBFS. It is not true-peak or loudspeaker-volume protection.
- **DRAG MIDI** and **DRAG WAV** pull the current pattern out as a 960-ppq MIDI file or a 24-bit WAV render (same approach as Drum Player / Dr Fill, including FL Studio). Accents become higher velocities; slides hold into the next step; micro offsets are written as pitch-bend.

## Look and presets
- Dark CMPLXTY interface, Barlow type, fixed 1100×820 layout. A lightweight falling-ash particle layer can be toggled with **ASH** (on by default). Particles run on the UI thread only and do not touch the audio path.
- 26 factory presets (sound + pattern). User presets save/load as `.fallout` files in the standard CMPLXTY/FALLOUT/Presets folder. Double-click knobs to reset. All 22 parameters are host-automatable.

## Formats
VST3, CLAP and standalone. Instrument with MIDI input. Windows x64 and Linux x86_64 are built locally. Mac universal arm64/x86_64 packaging is prepared for CI but not built here. Mono or stereo output, no audio input bus, no reported latency, tail declaration 2.5 seconds.

In FL Studio load FALLOUT as a **generator**.

## Build
`./scripts/fetch-deps.sh` fetches pinned JUCE 9.0.4 + clap-juce-extensions with the common CMPLXTY Windows compatibility patches. On this box ext/ points at already-fetched shared copies; they are ignored, not vendored.

Linux: CMake Release + Ninja, targets Fallout_VST3 Fallout_CLAP Fallout_Standalone FalloutTest. Box sysroot environment is in /workspace/tc/env.sh and host compilers in /workspace/tc/hostbin. Windows: scripts/build-windows-xwin.sh then scripts/build-installer.sh, clang-cl/xwin static CRT + NSIS. Mac: scripts/build-macos-pkg.sh on macOS. The workflow builds artifacts only and never publishes a release automatically. Tag pattern: `fallout-v*`.

## Verification
`xvfb-run -a build/linux/FalloutTest_artefacts/Release/FalloutTest test /absolute/results`

`python3 test/audio_analysis.py build/linux/FalloutTest_artefacts/Release/FalloutTest /absolute/results`

Audio script requires numpy/scipy/matplotlib and ffmpeg; run under Xvfb on a headless system. It renders factory patterns through the actual processor. Host validation is separate. CLAP validation passes with one framework warning: nonempty random state bytes are reported as loaded by the JUCE wrapper, but FALLOUT ignores them and does not crash. Optional unsupported CLAP extensions are skipped. Native Windows/FL Studio and macOS testing are still required.

No push, tag, release, site/catalog changes or notifications are authorized by this local build. Sam's listening/DAW tests remain required before Done.
