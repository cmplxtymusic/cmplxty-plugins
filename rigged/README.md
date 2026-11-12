# CMPLXTY RIGGED

A randomizable effects rack. Eleven processors sit in a chain you can reorder by dragging, each with its own on/off, a LOCK that keeps it out of a roll, and automatable knobs. RANDOMIZE (also automatable: it rolls when the parameter rises) moves unlocked parameters from where they are now, scaled by AMOUNT, with a smart roll that stays musical and level-safe. Undo and redo walk the roll history. MIX, OUTPUT, an automatic gain match, a safety limiter at −1 dBFS (on by default) and DAW-synced delay and tremolo sit on the top bar. Twenty-five factory presets; your own presets are `.rigged` files.

The eleven processors: Filter (LP / HP / BP state-variable), Drive, Crush (bits and downsample), Ring Mod, Chorus, Phaser, Delay (tempo-sync), Reverb, Comp, Tremolo (tempo-sync) and Width.

An animated dice in the header is decorative only. REDUCE MOTION turns the animation and the pixel scan off.

VST3, CLAP and Standalone. An original “inspired by” take on the idea of a lockable, randomizable rack, not a recreation of any existing product.

## Build

```bash
./scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Rigged_VST3 Rigged_CLAP Rigged_Standalone RiggedTest --parallel
build/linux/RiggedTest_artefacts/Release/RiggedTest test /tmp/rigged-tests
```

Windows (cross from Linux): `./scripts/build-windows-xwin.sh` then `./scripts/build-installer.sh`.
macOS: `./scripts/build-macos-pkg.sh`.

## License

GPL-3.0-or-later. See [COPYING](COPYING) and [installer/LICENSES.txt](installer/LICENSES.txt).
