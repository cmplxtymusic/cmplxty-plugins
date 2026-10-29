# CMPLXTY Plugins

Free, open-source audio plugins by Sam Arnstein (CMPLXTY), for Windows and macOS, in VST3 and CLAP formats. They're built with JUCE and work in FL Studio, Ableton Live, Reaper, Bitwig and other DAWs. Each plugin has presets, automatable knobs, double-click reset and a right-click menu.

| Plugin | Folder | What it does |
|---|---|---|
| Glitch Crusher | [`glitch-crusher/`](glitch-crusher/) | Bitcrusher / sample-rate reducer / glitch |
| Growl | [`growl-saturator/`](growl-saturator/) | Saturator / distortion |
| Drift | [`drift-shifter/`](drift-shifter/) | Frequency shifter |
| Vocal Aligner | [`vocal-aligner/`](vocal-aligner/) | Offline vocal timing aligner |
| Robot Voice | [`robot-voice/`](robot-voice/) | Vocoder-style robot voice |
| Dark Reverb | [`dark-reverb/`](dark-reverb/) | Dark, long reverb |
| Granular | [`granular/`](granular/) | Granular texture effect |
| Sidechain Shaper | [`sidechain-shaper/`](sidechain-shaper/) | Volume-shape (sidechain) tool |
| Mix Tools | [`mix-tools/`](mix-tools/) | Stereo widener, transient shaper, limiter |
| Cyber Wavetable | [`cyber-wavetable/`](cyber-wavetable/) | Wavetable synth |
| Drum Player | [`drum-player/`](drum-player/) | 8-pad drum sampler |
| Neon FX | [`neon-fx/`](neon-fx/) | Stutter / tape-stop / filter / drive performance effect |
| VVITCH | [`vvitch/`](vvitch/) | Witch house polysynth (chip, tape, choral vox, DRAG) |
| Visualizer | [`visualizer/`](visualizer/) | Music-reactive visuals with your own JPG/PNG images |
| Quartz | [`quartz/`](quartz/) | Saturation / distortion engine (VST3, AGPLv3) |
| Yoink | [`yoink/`](yoink/) | Find YouTube clips by phrase and drag the audio into your DAW (MIT) |

## Download
Installers for every plugin: **https://cmplxty.music/plugins/**. Each page shows the file's SHA-256 hash.

## Build from source
Each plugin folder has its own scripts (`scripts/fetch-deps.sh`, then `scripts/build-windows-xwin.sh` + `scripts/build-installer.sh` for Windows, or `scripts/build-macos-pkg.sh` on a Mac). Requirements: CMake 3.22+, Ninja, a C++17/20 compiler (clang-cl on Windows, Xcode on macOS), Git. Quartz is built as described in [`quartz/BUILD.txt`](quartz/BUILD.txt).
Release builds come from the workflows in `.github/workflows/`, which run on GitHub-hosted runners.

## License
GPL-3.0 (Quartz: AGPL-3.0, Yoink: MIT). See [LICENSE](LICENSE). Built with [JUCE](https://juce.com), used under its GPLv3/AGPLv3 open-source license, and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (MIT). VST is a trademark of Steinberg Media Technologies GmbH.

## Code signing policy
Windows installers are currently unsigned; see the download page for how to open them ("More info > Run anyway"). We have applied to the SignPath Foundation free program; our policy (team roles, privacy statement) is in [CODE-SIGNING-POLICY.md](CODE-SIGNING-POLICY.md). Contact: contact@cmplxty.music.
