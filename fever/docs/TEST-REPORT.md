# FEVER 1.0.0 — Local verification report

Built locally from the cmplxty-plugins worktree at RIGGED tip `72643e0a126cf88fe1826fa42f2e2a3387532629`, branch `fever`. Release candidate only: no push, tag, release, catalog mutation or outbound contact. Existing plugins were not edited except for adding a FEVER row to the root README, wiring `.github/workflows/fever.yml` into `build-all.yml`, and extending `mac/install-mac.sh` so `all` and `--list` include `fever`. FEVER is not on the site yet and was added anyway.

This report is measurements from the Linux Release binary driven by `FeverTest`. These tests are not a subjective listening verdict or a substitute for Sam testing in FL Studio.

All processor tests passed (`failures: 0`).

## Real audio, not code-only review

The actual JUCE processor (and, for a few unit checks, the same DSP `Engine` the processor uses) is driven at 48 kHz stereo. Measurements come from rendered buffers with no post-render loudness normalization. The hero knob `fever` is dry at 0.5: love amount is `(0.5 − fever) × 2` and hate amount is `(fever − 0.5) × 2`, both zero at centre.

### Centre null

Dry identity at `fever = 0.5`, mix 1, output 0 dB, heartbeat off. The processed buffer minus the input is silence.

| Condition | Residual |
|---|---|
| Gain match off | **−inf dB** |
| Gain match on | **−inf dB** |

Passes the null test.

### Smooth sweep

A continuous sweep of the hero knob from centre to full love, and from centre to full hate, on a 220 Hz sine. Wet residual energy starts at 0 at the centre and rises monotonically.

| Side | Start energy | End energy |
|---|---:|---:|
| Love (centre → 0) | **0** | **0.2678** |
| Hate (centre → 1) | **0** | **1.527** |

Maximum adjacent-sample jump under automation: **0.0526** (pass < 0.12). No clicks.

### Pitch-cloud partials (love)

A 440 Hz sine at full love, residual after subtracting the dry. Grain pitch ratios are an octave down, a fifth down, a fifth up, an octave up, and an octave-plus-fifth up (0.5, 2/3, 1.5, 2, 3). Peak frequencies must land within **±10 cents**.

| Partial | Measured Hz | Cents error | Limit |
|---|---:|---:|---|
| Octave down | 220.277 | **+2.18** | ±10 |
| Fifth down | 292.049 | **−7.60** | ±10 |
| Fifth up | 659.487 | **−1.35** | ±10 |
| Octave up | 883.297 | **+6.47** | ±10 |
| Octave + fifth | 1318.38 | **−2.12** | ±10 |

Worst error **7.60 cents**.

### Detune spread and stereo correlation

Residual of the pitch cloud at spread 0 versus spread 1. Correlation is on the wet residual so dry energy cannot mask the width.

| Spread | L/R correlation | Spectral sigma (cents) |
|---|---:|---:|
| 0 | **1.000** | 3.70 |
| 1 | **0.775** | 14.13 |

Spread 0 is fully correlated. Spread 1 widens both correlation and spectral detune.

### Spectral peaks and cold tail (hate)

Resonant peaks are designed at 247, 619, 1553 and 3901 Hz. Measured peaks must sit within 5 % of those frequencies. Tail RT60 at freeze = 0.5 is designed as `0.32 + 2.8 × freeze²` = 1.02 s.

| Design Hz | Measured Hz | Relative error |
|---|---:|---:|
| 247 | 246.928 | 0.029 % |
| 619 | 612.871 | 0.990 % |
| 1553 | 1571.05 | 1.162 % |
| 3901 | 3888.53 | 0.320 % |

Max relative error **1.16 %**. Tail RT60 **0.93 s** versus 1.02 s expected (relative 0.088).

### Halo detune

Hilbert single-sideband halo, measured on the residual in a 25-cent window. Design shifts: +9.7506094 Hz (+38 cents at 440) and −12.778852 Hz (−51 cents at 440).

| Side | Measured Hz | Cents error |
|---|---:|---:|
| Up (expect 449.751) | 449.739 | **−0.043** |
| Down (expect 427.221) | 427.180 | **−0.167** |

### Side-knob remapping

The two side knobs change meaning with the hero knob. UI labels follow: Density / Spread on the love side, Freeze / Halo on the hate side. DSP remapping:

| Knob | Low | High | Observation |
|---|---:|---:|---|
| Density (love) | 9 grains | 21 grains | denser cloud at the high setting |
| Freeze (hate) | RT60 0.39 s | RT60 1.41 s | longer cold tail |
| Halo off vs on | residual ratio **0.021** | — | halo energy collapses when the knob is down |

### Tempo sync (heartbeat)

Host tempo 120 BPM, heartbeat rate 1/4. Period of the pulse: **0.500444 s** (expect 0.5 s), 7 wraps observed.

### Gain match

K-weighted (BS.1770) automatic makeup, limiter off for this test (the product default is on). Same input material across the hero-knob positions.

| Quantity | Result | Requirement |
|---|---:|---|
| LUFS min | −18.669 | — |
| LUFS max | −18.607 | — |
| LUFS median | −18.634 | — |
| Max deviation from median | **0.035 dB** | ≤ 1.5 dB |

### Limiter, denormals, bypass, state

- Limiter on: peak **0.891251** (exact −1 dBFS = 10^(−1/20) = 0.891250938). Limiter off, same input: peak **20.93**. Zero-lookahead linked sample-peak clamp, not true-peak.
- Nonfinite samples: **0**. Denormals in the output: **0** (FTZ/DAZ via MXCSR).
- Bypass after the fade, then snap/settle: max abs difference **0**.
- State XML (`FEVER`) recalls the hero knob (`state_fever` = 0.22).

### CPU

Release, x86_64, 48 kHz stereo. Only `processBlock` is timed. Target is under **8 %** of one core (grains and the spectral path cost more than a typical CMPLXTY FX).

| Quantity | Result | Requirement |
|---|---:|---|
| Cloud (love) median | **2.64 %** of one core | < 8 % |
| Rift (hate) median | **1.17 %** | < 8 % |
| Combined min / max | 1.16 % / **2.86 %** | < 8 % |
| Worst observed block | 48.76 ms | first-run / OS spike; not the steady cost |

This is observational wall-time cost, not a cross-machine real-time guarantee. The first-block spike is grain-delay allocation and is not representative of the median.

## Host / packaging checks

- Linux VST3: pluginval strictness 10 **SUCCESS**, including Editor, Open editor whilst processing, Editor Automation, Fuzz parameters, state, thread safety and varying rates/blocks (44.1 / 48 / 96 kHz, 64–1024 samples). Log: `/tmp/fever-tests/pluginval/`.
- Linux CLAP: **44 tests, 32 passed, 0 failed, 1 warning, 11 skipped**. Warning: `state-invalid-random` — the JUCE CLAP wrapper always returns true from `load()`; same as RIGGED / SMOTHER / FALLOUT / HEFT, not a FEVER-only bug.
- Windows x64 VST3 + CLAP + Standalone were cross-built with clang-cl 23.1.4 + xwin and packaged with NSIS as `CMPLXTY-FEVER-Setup-1.0.0-win.exe`. PE ProductName / ProductVersion / CompanyName are `CMPLXTY FEVER` / `1.0.0` / `CMPLXTY` on the standalone, VST3, CLAP (`CMPLXTY FEVER (CLAP)` description) and the installer. The installer is **unsigned**. Native Windows host test and a Mac universal pkg are pending (Mac follows CI after push/tag approval).
- Screenshots: `FEVER-ui.png` is the Still Point preset (hero at 12 o'clock, DRY, DENSITY / SPREAD, RATE 1/4). `FEVER-hate.png` is Full Rift (RIFT 100, FREEZE / HALO). 800×600 editor, Barlow, monochrome. REDUCE MOTION is present.

## What this does not claim

Not a listening test. Not a Windows or Mac host certification. Not a claim that the first-block CPU spike will stay under a given buffer length on every machine. Gain match keeps loudness close; it is not a mastering loudness target.
