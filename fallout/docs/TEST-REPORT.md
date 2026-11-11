# FALLOUT 1.0.0 — Local verification report

Built locally from the cmplxty-plugins worktree at HEFT tip 58a14493fbb6f4bbb4cb1c32f46bcfcfca42f8c5, branch `fallout`. Release candidate only: no push, tag, release, catalog mutation or outbound contact. Quartz, Yoink and HEFT untouched.

## Real audio, not code-only review

The actual JUCE processor is driven by FalloutTest and by `test/audio_analysis.py`. Factory patterns are rendered to 24-bit WAVs at 48 kHz; there is no post-render loudness normalization. These tests are not a subjective listening verdict or a substitute for Sam testing in FL Studio.

### Behaviour measured on the live processor

- 0 processor test failures; 0 nonfinite and 0 subnormal outputs across 44.1/48/96 kHz and blocks 1/17/64/256/1024, with stressed resonance, drive, delay and MIDI. Parameter jumps stay finite.
- Stressed sample peak 0.48190, under the linked −1 dBFS ceiling (0.89125). The limiter is a zero-lookahead sample-peak stage, not true-peak.
- Filter: a C2 EDGE saw at cutoff 300 Hz vs 1800 Hz (resonance 0.88, envelope amount 0) raises the 1–2.5 kHz / 150–450 Hz band-energy ratio from 0.011 to 0.051. Spectral peak in the 150–6000 Hz search band moves from 140.6 Hz to 785.2 Hz. Changing resonance at a fixed 800 Hz cutoff also changes the spectrum (low-resonance output is darker/quieter).
- Accent at full ACCENT raises RMS of an otherwise identical C3 note by 3.42 dB (0.15561 → 0.23081).
- Slide/glide at GLIDE 0.25: a legato move from C2 to C3 reaches halfway in 80 ms (3840 samples at 48 kHz) and 90% of the interval in 265.3 ms.
- Aliasing: a C7 EDGE saw has a 14–20 kHz / fundamental energy ratio of 1.02×10⁻⁵.
- Tempo-synced delay (1/4 note at 120 BPM, mix 0.5, feedback 0): echo spacing error 0.83 ms.
- Rubato: a +0.4 step nudge at 1/16 and 120 BPM, global RUBATO 1, shifts onset by 50.0 ms (exactly the requested 0.4 × 125 ms).
- State save/restore recalls parameters, the pattern including micro and rubato, the ash toggle and the grid octave. Invalid/random state bytes are ignored and do not replace a good state.
- MIDI export of an 8-step test pattern writes three notes at 960 ppq, accent velocity 116 vs plain 80, and 0.2500 s spacing for a two-step gap at 1/16 / 120 BPM. The exported 24-bit WAV is readable and non-silent. Randomize, shift and clear change the pattern as expected.

Eight-second factory-pattern renders (48 kHz, integrated ffmpeg EBU R128, Welch PSD). True peak is a 4× scipy polyphase estimate, not a certified true-peak meter.

| Preset | RMS dBFS | LUFS | Crest dB | Centroid Hz | 20–200 / 200–2k / 2–20k % |
|---|---:|---:|---:|---:|---|
| 01 Ash Init | −16.00 | −13.9 | 10.31 | 319.6 | 63.2 / 35.5 / 1.3 |
| 02 Residue Line | −14.95 | −12.8 | 10.75 | 348.7 | 62.2 / 36.2 / 1.6 |
| 03 Sump Crawler | −10.22 | −8.6 | 6.30 | 235.7 | 83.5 / 15.5 / 1.0 |
| 09 Ember Horde | −21.32 | −19.3 | 14.56 | 405.5 | 51.8 / 46.1 / 2.1 |
| 10 Folded Waste | −11.98 | −9.3 | 7.95 | 771.1 | 5.5 / 94.4 / 0.1 |
| 11 Snarl Pit | −24.56 | −19.8 | 19.39 | 3457.6 | 3.0 / 37.7 / 59.4 |
| 12 Half-Life | −11.06 | −9.4 | 9.57 | 279.5 | 73.9 / 24.3 / 1.8 |
| 26 Fallout Shelter | −14.39 | −12.1 | 10.11 | 401.8 | 58.3 / 39.2 / 2.5 |

All eight renders had 0 nonfinite samples and stayed under the sample-peak ceiling (Half-Life came closest at −1.49 dBFS sample peak, −1.46 dBFS 4× true-peak estimate).

## CPU

Release, x86_64, 8 exposed cores, 48 kHz stereo, 256-sample blocks, factory preset 09 (Horde) with delay mix 0.3 and the sequencer running. Five 10-second renders; only processBlock is timed, buffers filled outside timing and a checksum consumes output. Median wall-time cost equals 1.744% of one core, range 1.741–1.748%. Worst observed block 267.7 µs, under the 5333.3 µs block budget. This is observational wall-time cost, not a cross-machine real-time guarantee. Ash particles are UI-thread only (48 frames: 98.1 ms with ash on, 98.7 ms with ash off); they do not change processBlock cost. No comparative optimization claim is made.

## Host / packaging checks

- Final Linux VST3: pluginval strictness 10 SUCCESS, including editor, editor while processing, editor automation, state restoration, parameter thread safety, varying rates/blocks and parameter fuzzing.
- Final Linux CLAP: 44 tests, 33 passed, 0 failed, 1 warning, 10 optional-extension skips. Remaining warning: JUCE wrapper reports success for nonempty random state bytes although FALLOUT ignores invalid data; no crash and state reproducibility passes.
- All Windows PE ProductName/ProductVersion/CompanyName metadata correct on VST3, CLAP, standalone and the NSIS installer.
- Windows runtime installer smoke NOT completed: isolated Wine initialization has previously failed on this box (`wineserver: failed to load l_intl.nls`). Native Windows/FL Studio testing remains pending.
- macOS package not built on Linux. Universal macOS build/packaging workflow is prepared; it must run in CI after explicit push/tag approval. Installers unsigned.

## Limits

The ladder is a 4-pole tanh design with 2x oversampling of the nonlinear path, not a circuit-level 303 or NUKE model. Oscillators use polyBLEP (and a sine-fold / noise-ring), not wavetable oversampling. The limiter is a linked zero-lookahead **sample-peak** ceiling, not true-peak. Sequencer timing follows host PPQ and is sample-accurate within a block; it is not a guaranteed sample-accurate CLAP transport implementation. MIDI export writes pitch-bend for micro offsets, not MPE. The UI is a fixed 1100×820 layout. User `.fallout` presets are internal files, not CLAP preset-discovery providers. Mac/Windows-native and Sam listening tests pending.

## Artifacts

Installer: `CMPLXTY-FALLOUT-Setup-1.0.0-win.exe`. Listing: `/workspace/plugins/fallout-listing`; shared copy: `/home/box/shared/plugins/fallout`; Git bundle: `/workspace/plugins/fallout-bundle`. Screenshots are actual editor snapshots, `FALLOUT-ui.png` (preset 02 Residue Line) and `FALLOUT-horde.png` (preset 09 Ember Horde). Measurements, audio, plots and validator logs are in `measurements/`.

Hub addition is prepared in docs/HUB-RELEASE-NOTES.md, not applied. Hub discovers normal `fallout-v1.0.0` GitHub releases and needs the platform installers plus published SHA256SUMS. FALLOUT is an instrument; the site listing type should be Synth/Instrument, not FX.
