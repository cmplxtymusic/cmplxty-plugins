# CMPLXTY Drum Player

A free 8-pad drum and one-shot sample player (VST3 + CLAP, Windows / macOS / Linux) by CMPLXTY. GPLv3.

- **8 MIDI pads**, starting at C1 (36) by default (the PAD 1 NOTE menu moves the map to C0 / C2 / C3). Note-offs are ignored: every hit is a one-shot.
- **Built-in kit of 16 synthesised sounds** (Kick 808, Kick Punch, Snare, Clap, Closed / Open Hat, Low / Mid Tom, Rim, Cowbell, Crash, Shaker, Zap, Sub Boom, Snap, Glitch), so it plays with no samples installed.
- **Drag in your own samples**: drop a WAV / AIFF / FLAC / OGG on a pad (several files fill the following pads), or right-click a pad > Load sample. Up to 20 s per sample, mono or stereo, any sample rate.
- **Per pad**: sound, tune (+/-24 st), decay (10 ms to Full), pan, level and choke group (4 groups; pads 4 and 5 choke each other by default, closed / open hat style).
- **Master**: volume, velocity sensitivity, kit tune.
- **Kits are saved in presets and in your project**: the sample audio is embedded (compressed 16-bit), so a kit still loads when the original files move.
- 18 factory kits, a preset browser, and user preset save / load.
- Click a pad to audition it; pads flash when they are hit. Right-click any control for its menu; double-click a control to reset it. The editor never takes keyboard focus, so Space still reaches your DAW.
- Optional update check (at most once a day, can be switched off).

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. See HOW-TO-OPEN-ON-MAC.md for unsigned macOS builds.

Inspired by classic drum machines and one-shot samplers; not a clone of any product.
