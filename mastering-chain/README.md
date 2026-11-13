# CMPLXTY GLAZE

A mastering chain for the master track, tuned for bass music (bass house, midtempo, cyberpunk). One plugin, six modules in a fixed order:

1. **EQ**: high-pass, low shelf, two bells, high shelf.
2. **Glue compressor**: stereo-linked, soft knee, peak-aware detector, parallel MIX.
3. **Multiband**: three bands (linear-sum Linkwitz-Riley 4th-order crossovers), a relative-level leveller and a level trim per band.
4. **Saturation**: TAPE, TUBE or HARD curves, 8x oversampled, with TONE and MIX.
5. **Stereo**: width (mid/side) and a BASS MONO high-pass on the side signal.
6. **Limiter**: lookahead true-peak limiter (interpolated peak detection) with PUSH, CEILING and RELEASE.

A live analyser shows the input and output spectrum plus the EQ curve, and a BS.1770 loudness meter shows momentary, short-term and integrated LUFS, input short-term and the true-peak maximum. Everything is bypassable per module, and BYPASS is a latency-matched bypass of the whole chain.

With the modules at their defaults, only the limiter is active (ceiling −1 dBTP): the chain is otherwise transparent. Sixteen factory presets for bass music (Bass House Loud / Clean, Midtempo Weight / Open, Cyberpunk Edge / Wide, Heavy Drop, Gentle Glue, Warm Tape, Tight Low End, Air and Sparkle, Streaming Safe, Loud and Punchy, Mono Bass / Wide Top, Final Check) plus user presets saved as `.glaze` files.

VST3, CLAP and standalone. Windows x64; Linux x86_64 built locally. Mac universal arm64/x86_64 packaging is prepared for CI. Mono or stereo main bus with matching output. Reported latency is fixed at 262 samples at 48 kHz (about 5.5 ms; the limiter lookahead plus the true-peak window).

## Build

`scripts/fetch-deps.sh`, then a Linux build (`cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build build/linux --target Glaze_VST3 Glaze_CLAP GlazeTest`) or `scripts/build-windows-xwin.sh` plus `scripts/build-installer.sh`. On a Mac, `scripts/build-macos-pkg.sh`. Tests: `GlazeTest test <folder>` writes `results.json` and before/after WAV files.

## License

GPL-3.0. See [COPYING](COPYING). Built with [JUCE](https://juce.com) 9.0.4 (GPLv3) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (MIT). VST is a trademark of Steinberg Media Technologies GmbH.
