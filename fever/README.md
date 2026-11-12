# CMPLXTY FEVER

A one-knob mood effect. The hero knob, FEVER, sits at dry in the centre. To the left it opens a granular pitch cloud of octaves and fifths, detuned and wide. To the right it opens a spectral engine: resonant peaks, a cold decaying tail, and a detuned dissonant halo. An original CMPLXTY effect, inspired by the idea of a single-knob sweep from lush to harsh, not a recreation of any existing product.

Two side knobs change their meaning with the main knob: DENSITY and SPREAD on the cloud side, FREEZE and HALO on the rift side. MIX, OUTPUT, automatic gain match and a safety limiter at −1 dBFS (on by default) sit on the bar. A heartbeat option wobbles the knob in time with the DAW tempo (rate and depth). Twelve factory presets; your own presets are `.fever` files. REDUCE MOTION turns the mood animation and the heartbeat visual off.

VST3, CLAP and Standalone.

## Build

```bash
./scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Fever_VST3 Fever_CLAP Fever_Standalone FeverTest --parallel
build/linux/FeverTest_artefacts/Release/FeverTest test /tmp/fever-tests
```

Windows (cross from Linux): `./scripts/build-windows-xwin.sh` then `./scripts/build-installer.sh`.
macOS: `./scripts/build-macos-pkg.sh`.

## License

GPL-3.0-or-later. See [COPYING](COPYING) and [installer/LICENSES.txt](installer/LICENSES.txt).
