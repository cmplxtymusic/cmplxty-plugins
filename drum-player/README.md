# CMPLXTY Drum Player

A free 8-pad drum and one-shot sample player (VST3 + CLAP, Windows / macOS / Linux) by CMPLXTY. GPLv3.

## What's new in 1.2.0

- **PLAY / STOP preview**: hear the pattern (with its fill) straight through the plug-in, looping, before you drag anything out. No need to start the DAW: it runs at the host's tempo (the last one it sent, if the transport is stopped) or at the TEMPO knob. It stops by itself when the DAW transport starts or stops, so it never doubles up with your song.
- **Click a loop chip to hear just that part** (FULL LOOP / KICK / SNARE / TOP / TOMS; MIDI = the full loop), drag it to export it. The preview is sample-for-sample the WAV you drag out (measured: identical to -125 dBFS).
- **TEMPO knob**: Host (default) or 20-250 BPM. Sets the preview, the MIDI and the WAV drag-outs, for loops at another tempo than the project.
- **SWING** (50 % straight to 75 %): delays every second 16th of the groove (fills stay straight). Applies to the preview, the MIDI and the WAVs, and the file name says it.
- **SOFT CLIP** (Master, on for new instances): untouched below -3 dBFS, never above -0.3 dBFS. In 1.1, 290 of 324 kit / pattern loops peaked over 0 dBFS (up to +7.6 dBFS) and were hard-clipped in the 24-bit WAV; with Soft Clip they peak at -0.3 dBFS and lose 0.02 dB RMS on average. Projects saved with 1.1 load with it off and sound exactly as before.
- **Seamless WAV loops**: a drag-out is rendered as the loop sounds when it repeats, so a crash or open hat ringing over the loop point is choked and voiced exactly like the next pass (1.1 added a 4 s tail on top, which missed chokes and cut longer tails). A loop that would go over 0 dBFS with Soft Clip off is written as 32-bit float instead of being clipped.
- **PLAY WITH HOST** (was PLAY IN PLUGIN, now under the PLAY button) plays the pattern locked to the song position only while the host is playing; in 1.1 it also free-ran while the project was stopped.
- 64 voices (was 32) with a soft steal: when they run low the quietest voice fades out in 5 ms instead of being cut.

- **GM / FPC note map (default, new in 1.1)**: 36 kick, 38 snare, 39 clap, 42 closed hat (44 pedal), 46 open hat, 41 / 43 / 45 / 47 / 48 / 50 toms (one tom pad, pitched per note), 37 rim, 49 crash, 51 ride (cymbal pad pitched up). Each pad shows its note. FL Studio's FPC and most drum MIDI land on the right sounds.
- **Chromatic map** (the 1.0 behaviour; projects saved in 1.0 keep it): 8 pads from PAD 1 NOTE (C0 / C1 / C2 / C3). Note-offs are ignored: every hit is a one-shot.
- **Patterns and fills (new in 1.1)**: 18 drum patterns (house, tech house, techno, dubstep, trap, boom bap, breakbeat, DnB, funk, reggaeton, afro house, garage, hardstyle ...), a fill generator (Auto, Roll, Build, Toms, Triplets, Flams, Break, Stutter; 1 beat to 2 bars long; NEW FILL / VARIATION gives another take), loop length 1-8 bars. The strip under the pads shows the notes, the fill and a playhead. PLAY / STOP previews it (1.2); WITH HOST plays it in sync with the host transport.
- **Drag-outs (new in 1.1)**: drag the MIDI chip into FL / any DAW for a .mid (GM notes, channel 10, host tempo, exact loop length), or FULL LOOP / KICK / SNARE / TOP / TOMS for 24-bit WAV loops rendered through the current kit (whole bars, the tail wraps to the start so they loop cleanly). Files are also kept in Documents/CMPLXTY Drum Player.
- **Follows host BPM**: the tempo is read from the host and shown in the display (120 BPM with "default" when the host sends none).
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
