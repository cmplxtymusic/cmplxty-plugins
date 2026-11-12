# CMPLXTY STOKE

A mid-focus effect. STOKE brings the midrange forward cleanly: a single FOCUS knob lifts a chosen band, GLOW adds warm mid harmonics, and SMOOTH tames harsh peaks and low-mid mud so the lift stays musical. At FOCUS zero the plugin is a true wire. Gain match is on by default, so a new instance sits at about the same loudness as the input.

VST3, CLAP and standalone. Windows x64; Linux x86_64 built locally. Mac universal arm64/x86_64 packaging is prepared for CI. Mono or stereo main bus with matching output. Zero samples of reported latency. Tail declaration 0.2 seconds.

## Knobs

- **FOCUS** — how hard the mids come forward. Zero leaves the sound untouched.
- **CENTRE** — where to lift, 300 Hz (body) to 5 kHz (bite). Default 1.65 kHz.
- **WIDTH** — how wide the lift is, in octaves. Narrow is a tight presence; wide is a broad mid.
- **GLOW** — warm harmonics on the lifted band only, oversampled. Zero is a clean boost.
- **SMOOTH** — automatic taming of harsh 2–5 kHz peaks and low-mid mud.
- **MID ONLY** — lift only the centre of a stereo image; the sides stay as they were.
- **MIX**, **OUTPUT** — wet/dry and output gain.
- **MATCH** — automatic gain match, on by default.
- **LIMIT** — safety limiter at −1 dBFS, on by default.

Twelve factory presets (Init, Subtle, Vocal Forward, Synth Lead, Bass Mids, Snare Crack, Master Glue, Guitar Bite, Podcast Voice, Drum Bus, Piano Body, Air Presence). User presets save as `.stoke` files.

## Build

`scripts/fetch-deps.sh`, then a Linux build (`cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build build/linux --target Stoke_VST3 Stoke_CLAP StokeTest`) or `scripts/build-windows-xwin.sh` plus `scripts/build-installer.sh`. On a Mac, `scripts/build-macos-pkg.sh`.

## License

GPL-3.0. See [COPYING](COPYING). Built with [JUCE](https://juce.com) 9.0.4 (GPLv3) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (MIT). VST is a trademark of Steinberg Media Technologies GmbH.
