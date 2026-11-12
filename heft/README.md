# CMPLXTY HEFT

**1.0.1:** WIDE keeps the bass mono, so full width no longer collapses in mono. The frequency-shift knob has a gentler taper so small shifts are easier to find (the ±400 Hz ends are the same; values in the middle move a little). Dependent knobs dim when their parent is off.

An original density/motion effect, inspired by the broad concept of distortion/compression with surrounding colour tools. No Stereo Lemon branding, graphics, layout or code are used. GPLv3.

## Controls
- **SLAM** blends antiderivative-antialiased tanh drive and linked compressor reduction. Drive Range, threshold, ratio, attack and release are under the hood.
- **FUZZ** adds input-envelope-gated stereo noise. NOISE TONE controls a broad bandpass (100 Hz–10 kHz centre).
- **WIDE** expands the side and adds a highpassed decorrelated side. Bass below 150 Hz stays mono, so full width stays mono-safe. The mid (centre) is unchanged before the final safety limiter.
- **DISPERSER** crossfades an eight-stage allpass chain. DISP FOCUS moves its phase knee.
- **FREQ SHIFT** is a quadrature/Hilbert single-sideband frequency shifter, ±400 Hz. It is not a pitch shifter. Low-frequency quadrature accuracy is finite.
- **TRANSIENT** boosts attack relative to a slower envelope; **TIGHT** attenuates falling tails relative to that envelope. TIGHT is not AI source separation or reverb removal.
- **MIX** is linear, latency-aligned dry/wet. **OUTPUT** is -24 to +6 dB.
- A non-bypassable linked sample-peak limiter holds output to -1 dBFS. It can distort on extreme input; it is not true-peak/lookahead protection or a loudspeaker-volume guarantee. There is no loudness-matching auto gain.
- **DICE** randomizes the seven sound sections, noise tone, disperser focus, LFO rate and one restrained modulation route. Mix, output and compressor internals stay put.
- **UNDER THE HOOD** opens internals and four mod slots. Envelope and external sidechain envelope are unipolar, sine LFO and smoothed noise bipolar; MOTION is global depth. Seven destinations, including ±250 Hz full route depth for shift. Destinations clamp to their exposed ranges. Routes sum.

Ten factory presets plus user .heft save/load and full DAW project recall. Double-click knobs to reset. All 31 parameters are host automatable (19 continuous controls + four source/destination/depth routes). Automation is block-accurate with 20 ms dezippering, not sample-accurate CLAP modulation. UI is fixed width with a compact and advanced height; no arbitrary scaling.

## Formats / routing
VST3, CLAP and standalone. Windows x64; Linux x86_64 built locally. Mac universal arm64/x86_64 packaging is prepared for CI but not built here. Mono or stereo main bus with matching output; optional mono/stereo external sidechain. Latency 64 samples (1.33 ms at 48 kHz). Tail declaration 0.2 seconds.

In FL Studio load HEFT as an **effect**. For external envelope modulation, route the key track to HEFT's sidechain bus in the wrapper/mixer, then select Sidechain as a mod source. Routing is host-specific. In standalone, hardware I/O is available but no host sidechain routing is supplied.

## Build
`./scripts/fetch-deps.sh` fetches pinned JUCE 9.0.4 + clap-juce-extensions with the common CMPLXTY Windows compatibility patches. On this box ext/ points at already-fetched shared copies; they are ignored, not vendored.

Linux: CMake Release + Ninja, targets Heft_VST3 Heft_CLAP Heft_Standalone HeftTest. Box sysroot environment is in /workspace/tc/env.sh and host compilers in /workspace/tc/hostbin. Windows: scripts/build-windows-xwin.sh then scripts/build-installer.sh, clang-cl/xwin static CRT + NSIS. Mac: scripts/build-macos-pkg.sh on macOS. The workflow builds artifacts only and never publishes a release automatically.

## Verification
`xvfb-run -a build/linux/HeftTest_artefacts/Release/HeftTest test /absolute/results`

`python3 test/audio_analysis.py build/linux/HeftTest_artefacts/Release/HeftTest /absolute/results`

Audio script requires numpy/scipy/matplotlib and ffmpeg; run under Xvfb on a headless system. It renders the actual processor, not a separate DSP imitation. Outputs deterministic synthesized drum, bass and synth tests, isolated modules, loudness/spectrum/correlation data and comparison plots. CPU test times only processBlock in Release, consumes output and records five runs; no speedup is claimed. Host validation is separate. CLAP validation passes with one framework warning: nonempty random state bytes are reported as loaded by the JUCE wrapper, but HEFT ignores them and does not crash. Optional unsupported CLAP extensions are skipped. Windows installer runtime smoke was blocked by the box Wine runtime missing l_intl.nls; PE metadata was checked instead. Native Windows/FL Studio and macOS testing are still required.

No push, tag, release, site/catalog changes or notifications are authorized by this local build. Sam's listening/DAW tests remain required before Done.
