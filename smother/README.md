# CMPLXTY SMOTHER

A sleeve drum machine. Sixteen lanes of sequenced drums, each with its own length and rate, and sleeves you draw over the grid to mute, filter or colour a span of steps. An original CMPLXTY instrument — inspired by the idea of painting processing over a beat, not a copy of any other plugin.

## What it does
- Up to 16 lanes, added and removed as you need them. Each lane has its own length (polymeter, for example a 5-step hat against a 16-step kick), a rate of 1/8, 1/16 or 1/32 including triplets, and a DOUBLE length option.
- Swing and humanize on the steps, plus a separate humanize for sleeves.
- Sleeves: draw across a span of steps, drag the edges to resize. Types are MUTE, LOWPASS, HIGHPASS, CRUSH, DISTORT, FLANGE, DELAY and REVERB. Each has attack, release, depth and curve. An ALL row applies a sleeve to the whole kit. Ramps are continuous so they do not click.
- Twelve original synthesized kits (IRON, VOID, GLASS, DUST, PULSE, SLATE, SPARK, MOSS, GRID, RUST, FLARE, CORE) covering kick, snare, clap, closed hat, open hat, tom, perc and cymbal. Drop your own sample on a lane and chop the start and end.
- Per lane: tune, decay, tone, pan, a simple EQ, envelope shape (SNAP, LINEAR, EXPO, PUNCH) and lane FX (HP, LP, drive, crush, transient, a multiband upward/downward compressor).
- Master chain: HP, LP, the same style of compressor, and a limiter whose ceiling is −1 dBFS.
- Plays in time with the host transport (start FL and the pattern starts; stop and it stops). Lanes also trigger from MIDI notes on a GM-style map.
- Drag the pattern out as MIDI or as a rendered WAV.
- 22 factory patterns and user presets saved as `.smother` files.

## Formats
VST3, CLAP and standalone. Instrument with MIDI input. Windows x64 and Linux x86_64 are built locally. Mac universal arm64/x86_64 packaging is prepared for CI but not built here. Mono or stereo output, no audio input bus, no reported latency, tail declaration 2.5 seconds.

In FL Studio load SMOTHER as a **generator**.

## Build
`./scripts/fetch-deps.sh` fetches pinned JUCE 9.0.4 + clap-juce-extensions with the common CMPLXTY Windows compatibility patches. On this box ext/ points at already-fetched shared copies; they are ignored, not vendored.

Linux: CMake Release + Ninja, targets Smother_VST3 Smother_CLAP Smother_Standalone SmotherTest. Box sysroot environment is in /workspace/tc/env.sh and host compilers in /workspace/tc/hostbin. Windows: scripts/build-windows-xwin.sh then scripts/build-installer.sh, clang-cl/xwin static CRT + NSIS. Mac: scripts/build-macos-pkg.sh on macOS. The workflow builds artifacts only and never publishes a release automatically. Tag pattern: `smother-v*`.

## Verification
`xvfb-run -a build/linux/SmotherTest_artefacts/Release/SmotherTest test /absolute/results`

`python3 test/audio_analysis.py build/linux/SmotherTest_artefacts/Release/SmotherTest /absolute/results`

Audio script requires numpy/scipy/matplotlib and ffmpeg; run under Xvfb on a headless system. It renders factory patterns through the actual processor. Host validation is separate. Optional unsupported CLAP extensions are skipped. Native Windows/FL Studio and macOS testing are still required.

No push, tag, release, site/catalog changes or notifications are authorized by this local build. Sam's listening/DAW tests remain required before Done.

The Plugin Hub picks up a new slug from GitHub Releases automatically (tag `smother-v1.0.0`). No Hub catalog edit is included, matching HEFT and FALLOUT. Without a `typeFor` entry the Hub will label it FX until that map is updated; the product type is Drum Machine / Instrument.
