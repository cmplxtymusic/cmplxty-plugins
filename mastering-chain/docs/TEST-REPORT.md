# GLAZE 1.0.0 — test report

Local Linux x86_64 and Windows x64 cross-compile, 13 November 2026. 48 kHz unless stated, 512-sample blocks. The real JUCE processor renders every number below (102 checks, 0 failures; raw values in `measurements/results.json`). Reference measurements (true peak, LUFS) come from independent implementations in the test harness (16x / 48-tap interpolator, separate BS.1770-4 code), not from the plugin's own meters. These tests are not a substitute for Sam testing in FL Studio. No push, tag or release.

## Quality gates (all passed)

| Gate | Result |
|---|---|
| True digital silence at idle (default and all 16 presets) | **-240 dBFS** (harness floor) |
| All modules off, limiter off: bit-exact passthrough | max diff **0** |
| BYPASS (latency matched) vs delayed input | max diff **0** |
| Default Init, flatness, 20 Hz to 20 kHz | worst band **0.00328 dB**, RMS change 0.00281 dB |
| Multiband sum flatness (with crossovers moved) | **3.73e-05 dB** (1.63e-05 dB moved) |
| EQ: measured tone gains vs the drawn curve | worst **2.7e-05 dB**; HPF 30 Hz at 15 Hz **-12.3 dB**; high-mid bell +6 dB at its centre |
| Glue static: 0 dB threshold does nothing | change **2.69e-08 dB** |
| Glue timing (10 ms attack / 110 ms release dialled) | 63 % at **10.5 ms** / **110 ms** |
| Multiband leveller, burst-over-bed in the low band | **14 -> 8 dB**, high band untouched (-0.00137 dB) |
| Multiband mid level +6 dB | **+5.95 dB** at 1 kHz, +0.213 dB at 60 Hz, +0.0135 dB at 12 kHz |
| Saturation 2nd harmonic (1 kHz, 12 dB drive) | TAPE **-170 dBc**, TUBE **-25.0 dBc**, HARD -163 dBc |
| Saturation 3rd harmonic | TAPE -26.8, TUBE -26.2, HARD -31.7 dBc; 5th at heavy drive TAPE -21.2, HARD -19.5 dBc |
| Saturation aliasing, 15 kHz sine, 18 dB drive | TAPE **-160**, TUBE **-141**, HARD **-161 dBFS** |
| Width 200 % | side +6.03 dB |
| Bass mono (corner 120 Hz) | side at 30 Hz **-48.2 dB**, 60 Hz -24.6 dB, 120 Hz -6.02 dB, 480 Hz -0.0338 dB |
| Loudness meter vs independent BS.1770 (-23 / -33 LUFS tones) | M **-23.00**, S **-23.00**, I **-23.00** / **-33.00** LUFS; short-term vs reference diff 7.1e-05 dB |
| True-peak meter vs reference on an intersample-peak signal | **-5.92** vs **-6.02 dBTP** |
| Limiter, fs/4 intersample-peak test at -1 dBTP ceiling (44.1 / 48 / 96 kHz) | **-1.10 dBTP** (input true peak -0.18, sample peak -4.11) |
| Limiter, loud bass mix at ceiling -1 / -2 dB (independent 16x true-peak measure) | 44.1k **-1.00 / -2.00**, 48k **-1.04 / -2.04**, 96k **-1.02 / -2.01 dBTP** |
| Limiter, ceiling -0.3 dB | 44.1k -0.31, 48k -0.36, 96k -0.32 dBTP |
| Limiter, +18 dB push into noise bursts and full-scale clicks (ceiling -1) | **-0.94 dBTP** (worst case: +0.06 dB over the ceiling) |
| Limiter recovery after a loud burst | level back to -12.8 dB 300 ms later (steady -12.0) |
| Limiter THD+N at 12 dB push | 1 kHz **-43.1 dB**, 40 Hz **-40.3 dB** |
| Reported latency | **262 samples** (5.46 ms at 48 kHz); impulse alignment exact |
| Robustness | no NaN / infinity / denormals at 44.1, 48, 88.2, 96 and 192 kHz with block sizes 1, 17, 480 and 4096, mono and stereo; random automation of every parameter at extreme input stays finite and bounded; state save / restore round trip on all parameters; garbage state ignored |
| CPU, 30 s of bass mix, 48 kHz stereo | Init **1.8 %**, Bass House Loud **3.2 %**, Cyberpunk Edge **2.9 %** of one core; 96 kHz Bass House Loud 16x real time |

Known limit: the limiter's true-peak guarantee is measured, not proven. A time-varying gain slightly changes the interpolated peak, so the worst measured overshoot above the nominal CEILING is **+0.06 dB** (full-scale clicks at +18 dB push) and **+0.03 dB** in heavy limiting on the bass mix (Bass House Loud, ceiling -0.8 dB reads -0.77 dBTP). A 0.1 dB margin is already built in.

## Before / after on a bass-music mix

Synthetic bass-music test mix (24 s, generated in the test harness), scaled so its true peak is -6 dBTP before mastering (a typical pre-master). Integrated LUFS from the plugin's meter, true peak from the independent reference, crest factor as peak-to-RMS.

Input: **-18.0 LUFS**, **-6.00 dBTP**, crest 12.5 dB.

| Preset | LUFS-I | True peak (dBTP) | Crest (dB) |
|---|---:|---:|---:|
| Init | -18.0 | -6.58 | 11.7 |
| Bass House Loud | -10.4 | -0.77 | 9.9 |
| Bass House Clean | -15.7 | -3.20 | 12.2 |
| Midtempo Weight | -13.3 | -1.55 | 11.4 |
| Midtempo Open | -15.9 | -3.78 | 12.0 |
| Cyberpunk Edge | -13.6 | -1.58 | 12.4 |
| Cyberpunk Wide | -15.7 | -3.48 | 12.1 |
| Heavy Drop | -10.2 | -0.58 | 9.8 |
| Gentle Glue | -17.2 | -5.73 | 11.7 |
| Warm Tape | -15.3 | -5.46 | 10.1 |
| Tight Low End | -17.7 | -6.27 | 11.9 |
| Air and Sparkle | -16.5 | -3.87 | 12.8 |
| Streaming Safe | -14.1 | -2.96 | 11.6 |
| Loud and Punchy | -10.4 | -0.80 | 9.9 |
| Mono Bass, Wide Top | -16.0 | -4.57 | 11.9 |
| Final Check (Limiter Only) | -15.0 | -3.00 | 12.5 |

Spectra and waveforms: `img/GLAZE-ab-bass-house-loud.png`, `GLAZE-ab-cyberpunk.png`, `GLAZE-ab-midtempo.png`. Rendered WAVs are on the box in `/workspace/plugins/mastering-chain-work/t1/wav`.

## Validators and Windows

- pluginval, strictness **10**, `--validate-in-process`, under Xvfb: **SUCCESS** (`measurements/pluginval.txt`).
- clap-validator: **32 passed, 0 failed, 1 warning, 11 skipped**. The warning is the usual clap-juce-extensions plugin-id suffix (`com.cmplxty.glazex1` vs `com.cmplxty.glaze`). An earlier param-conversions failure (HPF showing "12 Hz" for 12.2 Hz and parsing back to OFF) was fixed.
- Keyboard-focus audit: no editor component asks for keyboard focus (negative control passes), so Space and other host keys keep working; the right-click menu takes focus only while open and offers the host's automation menu when the host provides one.
- Windows x64 cross-compile (clang-cl, static CRT): VST3, CLAP, standalone and NSIS installer built; `tools/check-win-metadata.py` OK for VST3, CLAP and installer (ProductName `CMPLXTY GLAZE`, ProductVersion 1.0.0, CompanyName CMPLXTY). Installer unsigned; Mac package built only by CI (never run on a Mac).
