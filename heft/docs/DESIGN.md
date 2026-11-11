# HEFT architecture

Task phases: ground / sketch / implement / verify / package. No cloud/pstack runners are available in this executor; alternatives were compared locally, not attributed to external reviewers.

Ground: repository plugins are independent JUCE CMake projects, with CLAP via clap-juce-extensions, static-CRT clang-cl/xwin Windows builds and NSIS. Hub discovers releases rather than a checked-in static catalog. HEFT owns only heft/ and its own workflow.

Candidates: (A) a JUCE effect graph with per-module allocations/adapters; (B) one allocation-free, fixed-state sample engine with one settings snapshot per host block. B chosen: fixed routing is sufficient, deterministic offline and host tests exercise the identical DSP, and no audio-thread ownership handoffs are needed. Parameters.h is the single authority for continuous controls; APVTS owns automation/state, editor owns widgets and file choosers, Engine owns DSP history. No locks, file/network activity or allocations in the render loop.

Caller sketch: engine.prepare(rate); engine.set(settings); for each sample engine.tick(L,R,sidechain). Processor registers 64 samples of latency for Hilbert real-path alignment and dry path. Mono input/output is supported, optional mono/stereo sidechain. Sidechain is detector-only, never mixed into audio.

Signal: linked transient/tail shaping -> drive with first-order antiderivative antialiasing -> linked macro compression -> input-gated bandpass noise -> eight stable allpasses -> 129-tap windowed Hilbert frequency shifter -> centre-preserving M/S width -> latency-aligned dry blend -> output gain -> linked sample-peak safety limiter. The zero-shift path crossfades to the delayed real signal. Widening adds side only, with the generated side highpassed at 180 Hz.

Four mod routes use main envelope, sine LFO, smoothed random noise or external envelope. Sources and depths never directly overwrite base APVTS values. Continuous parameters and the resulting modulated values use 20ms dezippering; route switching is therefore ramped too. Fixed FIR/allpass history remains live across changes.

Safety: finite input sanitization, no-denormal scope, bounded mod destinations, non-randomized output/mix and compressor internals, -1dBFS linked instantaneous sample-peak limiting. This is not a lookahead true-peak limiter and cannot guarantee arbitrary intersample peaks or a safe acoustic volume.
