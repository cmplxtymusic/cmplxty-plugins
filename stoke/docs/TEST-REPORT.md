# STOKE 1.0.0 — test report

Local Linux x86_64 and Windows x64 cross-compile, 12 November 2026. 48 kHz, 512-sample blocks. The actual JUCE processor renders 24-bit processed WAVs. Synthesized drums, vocal, synth lead and a full mix, plus pink noise and test tones. These tests are not a substitute for Sam testing in FL Studio. No push, tag or release.

## Quality gates (all required, all passed)

| Gate | Result |
|---|---|
| True digital silence at idle (default and all 12 presets, 32 s / 4 s) | **−240 dBFS** (harness floor) |
| Bit-transparent null at FOCUS = 0 (MATCH and LIMIT on, pink) | **−240 dBFS** absolute and relative |
| MIX = 0 is a true wire (FOCUS at 1) | **−240 dBFS** |
| Gain at default, MATCH on, vs input | pink **+0.008 dB**, drums **−0.017 dB**, vocal **−0.052 dB** (all within ±0.5 dB) |
| Mud minus gain at default (200–500 Hz) | pink **−1.10 dB**, drums **−0.59 dB**, vocal **−0.08 dB**, bass **+0.10 dB** (all ≤ +0.5 dB) |
| 15 kHz alias at default | **−221 dBFS** (well below −90) |
| THD+N at default, 1 kHz mid-band sine | **−47.2 dB** (mild, intentional GLOW) |
| Mono input stays mono | correlation **1.000** |
| Stereo pink, mono-fold vs dry | **+0.039 dB** (no collapse) |
| MID ONLY on a side-only signal | residual **−240 dBFS** |
| Reported latency | **0 samples** |
| Sweeping every knob on silence | **−240 dBFS** (no zipper) |
| Every continuous knob changes the sound | FOCUS, CENTRE, WIDTH, GLOW, SMOOTH, MIX, OUTPUT all clearly different across their travel |
| No NaNs, no denormals | **0 / 0** |
| State save/restore | residual **−164 dB** |
| CPU, heavy settings, this box | **2.2 %** of one core (rtf 0.022) |

## Mid-band presence, loudness-matched

Wet is gain-matched to the dry RMS, then 1–4 kHz energy is compared. Harshness is 2–5 kHz; low-mid is 200–500 Hz (this last one should not go up).

| Loop | 1–4 kHz lift | 2–5 kHz | 200–500 Hz |
|---|---:|---:|---:|
| Vocal | **+1.57 dB** | +1.36 dB | **−0.27 dB** |
| Synth lead | **+2.43 dB** | +2.19 dB | **−0.62 dB** |
| Drums | **+1.32 dB** | +0.91 dB | **−0.59 dB** |
| Full mix | **+2.58 dB** | +2.09 dB | **−0.57 dB** |

The mids come forward; the low-mids go slightly down (unmasking), which is what we want. Spectra are in the listing folder (`STOKE-pink.png`, `STOKE-ab-vocal.png`, `STOKE-ab-mix.png`, and the other A/B plots).

## Validators and Windows

- Unit / audio suite (`StokeTest`): **0 failures**.
- pluginval, strictness **10**, `--validate-in-process`, under Xvfb: **SUCCESS**.
- clap-validator: **32 passed, 0 failed, 1 warning, 11 skipped**. The warning is the same clap-juce-extensions plugin-id suffix seen on FEVER (`com.cmplxty.stokex1` vs `com.cmplxty.stoke`). An earlier param-conversions failure on MIX (`1 %` parsing as 1.0) was fixed with a matching value-from-string function before this report.
- Windows x64 cross-compile (clang-cl / xwin, static /MT) and NSIS installer: **ok**. ProductName / ProductVersion / CompanyName checked on the VST3, the CLAP and the exe. SHA-256 of `CMPLXTY-STOKE-Setup-1.0.0-win.exe` is recorded with the listing. Mac universal package is prepared for CI and was not built here.

## What this is not

No FL Studio listening session was run. Mac packaging waits on CI. Nothing was pushed, tagged or published, and no site or Gumroad change was made.
