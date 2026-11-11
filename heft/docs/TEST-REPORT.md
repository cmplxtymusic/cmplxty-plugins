# HEFT 1.0.0 . Local verification report

Built locally from GitHub main 6a823be4d47b887aab8aa55438a695b7e043f566, branch heft. Release candidate only: no push, tag, release, catalog mutation or outbound contact. Quartz and Yoink untouched.

## Real audio, not code-only review

Eight-second deterministic synthesized drums, bass and stereo synth at 48 kHz. The actual JUCE processor renders 24-bit processed WAVs; dry inputs are float WAVs. No post-render loudness normalization. The first 64 samples of fixed DSP latency are retained in duration-wide metrics (not cut); onset is at 0.1 s, so this does not omit program material. These tests are not a subjective listening verdict or a substitute for Sam testing in FL Studio.

RMS is full-buffer stereo power in dBFS; crest is sample peak minus RMS. LUFS is integrated ffmpeg EBU R128. Spectrum uses mono Welch PSD (8192 samples); correlation is full-buffer Pearson L/R. True peak is a 4x scipy polyphase estimate, not a certified true-peak meter.

| Signal / preset | RMS dBFS dry → HEFT | LUFS dry → HEFT | Crest dB dry → HEFT | Centroid Hz dry → HEFT | L/R correlation dry → HEFT |
|---|---:|---:|---:|---:|---:|
| drums / Drum weight | -21.93 → -19.28 | -21.6 → -18.7 | 19.08 → 18.28 | 408.6 → 310.0 | 0.999 → 0.999 |
| bass / Bass iron | -11.95 → -9.79 | -12.6 → -10.3 | 9.09 → 8.75 | 83.7 → 97.8 | 1.000 → 1.000 |
| synth / Wide pressure | -16.56 → -12.84 | -14.3 → -10.5 | 13.71 → 11.20 | 324.7 → 335.7 | 0.087 → -0.446 |

Band energy percentages (20–200 Hz / 200–2000 Hz / 2–20 kHz), dry → HEFT:
- drums: 94.35 / 1.18 / 4.47 → 94.60 / 2.16 / 3.24 %.
- bass: 97.45 / 2.30 / 0.25 → 93.91 / 5.73 / 0.36 %.
- synth: 65.92 / 32.83 / 1.25 → 63.86 / 34.85 / 1.30 %.

The wide synth is intentionally anti-correlated (-0.446); width is not correlation protection. A separate unclipped width test nulls the mono sum within 1.49e-8 while creating side signal. The linked final limiter can alter the sum when limiting.

## Behaviour / safety

- 0 processor test failures; 0 nonfinite and 0 subnormal outputs across 44.1/48/96 kHz and blocks 1/17/64/256/1024, with active stereo sidechain and stressed audio. Separate injected NaN/Inf input test passes.
- Stressed sample peak 0.89125100 (approximately -1 dBFS), including +6 dB output. Dry/mix-zero impulse emerges exactly 64 samples later at unity below the ceiling.
- 128 dice states processed, finite and below the sample ceiling. Mix/output preservation tested. This is finite fuzz coverage, not a proof for every possible input or parameter combination.
- External sidechain produces a strong change in the modulated output; summed absolute difference 2741.009 across the test. Sidechain-only input with silent main has exactly zero leakage. Mono processing and state/project recall pass.
- Parameter jumps: first-sample deviation at the step ≤ 0.00021350; largest adjacent-sample step across the automated 80 Hz sine trials 0.138409 (includes intentional fuzz noise). All continuous controls and route-result changes dezippered. This is a measured discontinuity bound, not a claim of inaudibility under arbitrary settings.
- Tail/silence settles below 1e-20. Fuzz/disperser/width and LFO+noise modulation all have nonzero measured output differences; see JSON.
- Isolated transient on drums: RMS -21.93 → -19.33 dBFS. Isolated Tight: RMS → -23.80 dBFS. Analysis caught an initial shared-envelope transient no-op; independent rise/tail detectors corrected it and regression assertions now enforce both effects.
- 1000 Hz shifted ±100 Hz produces 1100/900 Hz. Image rejection 69.2/69.2 dB in this test.

## CPU

Release, x86_64 Intel Xeon box, 8 exposed cores, 48 kHz stereo, 256-sample blocks, all colour paths and LFO/noise routes active. Five 10-second renders; only processBlock is timed, buffers filled outside timing and a checksum consumes output. Median wall-time cost equals 1.957% of one core, range 1.943–2.869%. The worst observed block is 16124.8 µs, above the 5333.3 µs block budget. This is observational wall-time cost, not a cross-machine real-time guarantee or a claimed speedup. Initial runs overlapped compilation. The reported five runs were repeated after build jobs completed, but wall-time spikes remained. Their cause was not profiled, so no hard real-time claim is made. No comparative optimization claim is made.

## Host / packaging checks

- Final Linux VST3: pluginval strictness 10 SUCCESS, including editor while processing, automation, state restoration/thread safety, varying rates/blocks and parameter fuzzing.
- Final Linux CLAP: 44 tests, 32 passed, 0 failed, 1 warning, 11 optional-extension skips. Initial text roundtrip precision failure fixed with defined parameter resolution and canonical strings. Remaining warning: JUCE wrapper reports success for nonempty random state bytes although HEFT ignores invalid data; no crash and state reproducibility passes.
- All Windows PE ProductName/ProductVersion/CompanyName metadata correct. Actual NSIS extraction byte-matches staged VST3, CLAP, standalone and manifest.
- Windows runtime installer smoke NOT completed: isolated Wine initialization fails before running the installer (`wineserver: failed to load l_intl.nls`). No workaround through the shared Wine prefix was attempted. Native Windows/FL Studio testing remains pending.
- macOS package not built on Linux. Universal macOS build/packaging workflow is prepared; it must run in CI after explicit push/tag approval. Installers unsigned.

## Limits

First-order ADAA saturation, not oversampling; finite-band FIR quadrature accuracy, especially at bass frequencies; fixed eight-allpass disperser, not a PHAT circuit recreation. Tight is envelope tail attenuation, not de-reverb separation. Four block-automated mod routes, free-running sine LFO only (no tempo sync), no CLAP polyphonic modulation support. Fixed-width two-height UI; user .heft presets are internal files, not CLAP preset-discovery providers. Safety is a linked zero-lookahead **sample-peak** limiter, not true-peak or acoustic-volume protection; measured intersample estimates can exceed -1 dBFS. No auto loudness matching. Mac/Windows-native and Sam listening tests pending.

## Artifacts

Installer: `CMPLXTY-HEFT-Setup-1.0.0-win.exe` (SHA256 `0eea60f081e9c7c751a15570687c22e48c7c9ae5b4caaed924a9e7c5057b2b73`). Listing: `/workspace/plugins/heft-listing`; shared copy: `/home/box/shared/plugins/heft`; Git bundle: `/workspace/plugins/heft-bundle`. Compact and advanced screenshots are actual editor snapshots, `HEFT-ui.png` and `HEFT-advanced.png`. Measurements/audio/plots and full validator logs are in `measurements/`.

Hub addition is prepared in docs/HUB-RELEASE-NOTES.md, not applied. Hub discovers normal `heft-v1.0.0` GitHub releases and needs the platform installers plus published SHA256SUMS; new slug falls back to FX type, so no Hub code edit is needed.
