# SMOTHER 1.0.0 — Local verification report

Built locally from the cmplxty-plugins worktree at FALLOUT tip `4f68be80185313481b54c6eb85628f8407687bde`, branch `smother`. Release candidate only: no push, tag, release, catalog mutation or outbound contact. Existing plugins were not edited except for adding a SMOTHER row to the root README.

This report includes a quality pass after the first candidate: CPU reduction, 2-pole TPT/SVF sleeve filters, and small cleanups. All numbers below are from the final binary.

## Real audio, not code-only review

The actual JUCE processor is driven by `SmotherTest`. Measurements are taken from rendered 48 kHz stereo buffers (and from exported MIDI/WAV), with no post-render loudness normalization. These tests are not a subjective listening verdict or a substitute for Sam testing in FL Studio.

All processor tests passed (`failures: 0`, `TEST_EXIT: 0`).

### MUTE sleeve (full depth)

A continuous 220 Hz sine is loaded as a user sample on one lane. Two otherwise identical renders are compared: dry (no sleeve) vs wet (MUTE sleeve covering the whole span, depth 1, attack 50 ms, release 80 ms).

| Quantity | First candidate | Final | Requirement |
|---|---:|---:|---|
| Mid-sleeve attenuation | −114.47 dB | **−180.00 dB** | ≥ 60 dB |
| Attack (10–30 % of the MUTE dB-law envelope, scaled) | 47.5 ms | **52.5 ms** | 50 ms ± 15 ms |
| Release (90–10 % of the MUTE dB-law envelope, scaled) | 79.375 ms | **80.625 ms** | 80 ms ± 20 ms |
| Maximum adjacent-sample jump through the ramps | 0.00789 | **0.00815** | no clicks (< 0.08) |

MUTE gain law is \(10^{-9 \cdot e \cdot \mathrm{depth}}\). That is **−180 dB** at full depth, not −90 dB (a 20·log₁₀ conversion). The first candidate's −114 dB was a residual-floor reading; with FTZ/DAZ the mid-span wet RMS is a hard zero, so the reported figure is −180 dB. Attack is still measured on the 10–30 % portion of that envelope because 90 % is already at the noise floor. The ramps remain continuous.

### LOWPASS / HIGHPASS sleeves (spectrum)

Sleeves now use a **2-pole (12 dB/oct) TPT SVF** at Butterworth Q = 1/√2, so the −3 dB point sits at the mapped cutoff. The first candidate used a one-pole section; HIGHPASS measured 2809 Hz against a 2000 Hz design. Full-depth product mapping is unchanged: LOWPASS 18 kHz → 500 Hz, HIGHPASS 20 Hz → 2000 Hz. Additional cutoffs are measured by setting those full-depth extents to the target.

Sine probes, binary search of the 0.707 amplitude-ratio point, 48 kHz. Lane and master filters are bypassed at their transparent defaults so they do not shift the measurement.

| Setting | LOWPASS −3 dB | HIGHPASS −3 dB | ±5 % window |
|---|---:|---:|---|
| 500 Hz / 2000 Hz (product defaults) | **499.18 Hz** | **2001.46 Hz** | 475–525 / 1900–2100 |
| 200 Hz | **200.38 Hz** | **199.97 Hz** | 190–210 |
| 1000 Hz | **999.85 Hz** | **999.85 Hz** | 950–1050 |
| 5000 Hz | **4999.27 Hz** | **4999.27 Hz** | 4750–5250 |

First candidate, product defaults only: LP 499.18 Hz (already good), HP **2808.89 Hz** (outside any tight window). The 2-pole SVF is within 0.2 % at every probed cutoff.

### Polymeter timing vs host PPQ

Two lanes at 120 BPM, 48 kHz, block 64: a 5-step lane and a 16-step lane, both at 1/16. Onsets are recorded from the live processor and compared to host PPQ converted to samples.

- Lane 0 hits: 32; lane 1 hits: 32
- **Maximum error: 1 sample** (unchanged; sample-accurate within one-sample quantization)

Step firing is now planned once per block (same windows as the old per-sample loop) so CPU drops without changing onset times.

### Swing

Swing 0.5 at 120 BPM, 1/16 (step = 125 ms). Odd (0-based) steps are delayed by `swing * 0.5 * stepBeats`.

- Expected offset: 31.25 ms
- **Measured offset: 31.25 ms** (exact, unchanged)

### MIDI drag-out

`exportMidi` writes 960 ppq over a 16-beat window, looping each lane's own length (polymeter).

- **Maximum error: 0.0 ms** (unchanged)

### WAV render vs realtime

A controlled sine pattern is rendered through `processBlock` (realtime path) and through `exportWav` / `renderPattern` (offline path). Both paths now call the same `Engine::process()`, including the 48-sample limiter lookahead.

- Correlation: **0.999936** (same as the first candidate)
- Maximum absolute sample difference: **0.007194**
- The two outputs match for the purpose of this test.

### Limiter ceiling

A 16-lane full-velocity stress pattern is driven through the master chain.

- Limiter ceiling: 0.891250938 (−1 dBFS)
- **Measured sample peak: 0.891251** (holds the ceiling, −1.0000 dBFS)
- Stress peak: 0.891251
- Nonfinite samples: 0; denormals: 0

The limiter is a 48-sample lookahead sample-peak stage with a hard clip at the ceiling. Peak tracking is an O(1) running max that is equivalent to a full-window scan. It is not a true-peak meter and is not loudspeaker-volume protection.

### CPU, 16 busy lanes — before and after

Process-block wall time versus a 512-sample period at 48 kHz, one core, 16 lanes all firing with sleeves, drive, per-lane OTT and lane FX active, plus a master OTT. Same pattern before and after.

| | First candidate | Final |
|---|---:|---:|
| Median, % of one core | 35.82 % | **7.23 %** |
| Min | 35.09 % | 6.80 % |
| Max | 47.69 % | 9.52 % |
| Worst block | 5782 µs | **1638 µs** |

Target was under 10 % (stretch under 6 %). The final median is **about 5× lower** and under the 10 % gate. The stretch goal was not reached; 16 active OTT stages and drive `tanh` on every lane are the remaining cost.

What changed in the audio loop:

- Per-sample `tan` / `exp` coefficient work was moved to block rate or to `prepare`. Lane HP/LP, master filters and EQ are bypassed at transparent defaults. OTT coefficients are constant for a given sample rate.
- Sleeve LP/HP coefficients update at most every 16 samples (exact once settled).
- Idle sleeve FX are skipped; delay, flange and reverb are **one shared send** each, ticked once per sample, with tails still ringing after the sleeve ends. The first candidate ticked the shared reverb once per lane, which also advanced its combs 16× too fast when many lanes had a REVERB sleeve.
- Step firing is planned once per block from precomputed onsets. Voices are mixed in a single pass. Envelope values use a precomputed multiply instead of `exp` every sample.
- FTZ/DAZ is set in the engine (the processor already used `juce::ScopedNoDenormals`). The pattern is no longer copied into the engine on every block.

### Host validators

- **pluginval 1.0.4**, Linux VST3 (`CMPLXTY SMOTHER.vst3`), **strictness 10**, including the editor, editor-while-processing, editor automation, state, thread safety, rates/blocks and parameter fuzzing, run under xvfb: **SUCCESS**, exit 0. Log: `measurements/pluginval/pluginval.log`.
- **clap-validator**, Linux CLAP (`CMPLXTY SMOTHER.clap`), in-process: **44 tests run, 33 passed, 0 failed, 1 warning, 10 skipped**. The warning is `state-invalid-random`: the plugin did not crash, but the JUCE CLAP wrapper's `clap_plugin_state::load()` is `void` on the processor side and always returns true, so random bytes are reported as a successful load. Fixing that means patching the shared `clap-juce-extensions` wrapper (used by every plugin on this box), which is not a simple SMOTHER-only change. Log: `measurements/clap/clap-validator.log`.

### Other checks

- 22 factory presets all produce unique, non-silent audio.
- Editor snapshot (Velvet Grid and Wide Curtain) renders at 1180×740; PNG hashes match the first candidate (the UI did not change).
- User-sample voices keep a stable buffer across `setPattern` copies.
- Closed-hat voices fade open-hat voices via an internal `hatCut()` (renamed from `choke()`). The product itself does not use CHOKE as a brand or feature name.
- The unused `stepTriggerBeats` helper was removed.

## Whether the sound changed

- **Kit hits, step timing, swing, MUTE ramps, drive, lane OTT, and the limiter** are the same character as the first candidate. WAV-vs-realtime correlation is unchanged at 0.999936. Timing tests still measure 1 sample / 0 ms / 31.25 ms.
- **LOWPASS and HIGHPASS sleeves are steeper** (12 dB/oct instead of 6) and their cutoff now matches the setting. A full-depth HIGHPASS that used to sit near 2.8 kHz now sits at 2 kHz, as designed. This is an intentional sound change for those two sleeve types.
- **REVERB, DELAY and FLANGE sleeves** are one shared send for the whole kit. A single sleeve on one lane is close to the old sound. Several REVERB sleeves at once used to tick the same comb set once per lane per sample (the tail ran fast and piled up); they now share one correctly-timed reverb, which is the intended behaviour and will sound different on those patterns.
- **Lane EQ** is bypassed when all bands are at 0 dB, so a default lane is more transparent (the old 0 dB crossover did not perfectly reconstruct).
- Factory kit synthesis is unchanged.

## Limits and things not done

- The Mac universal pkg is produced by CI after an approved tag; it is not in this local bundle.
- No native Windows host or FL Studio test was run on this box (Wine historically lacks `l_intl.nls` in isolation).
- Plugin Hub `typeFor` has no `smother` entry, same as HEFT and FALLOUT. The Hub will label a GitHub release as **FX** until that map is updated. No HubCore edit was made. The intended listing type is **Drum Machine / Instrument**.
- Median CPU is 7.23 % of one core, under the 10 % target, above the 6 % stretch goal.
- The clap-validator `state-invalid-random` warning is a JUCE wrapper limitation, as noted above. 0 failures.
- This report is not a substitute for Sam's FL Studio listening test. The Trello card should stay open until that happens.
- No push, tag, release publication, Hub/Trello/Bob/Wolf contact, or edit to Quartz, Yoink, HEFT or FALLOUT.
