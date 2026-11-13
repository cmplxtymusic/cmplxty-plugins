# GLAZE design

Original mastering chain for bass music. Inspired by the usual order of a mastering session (tone, glue, band control, colour, width, final limit) and by how respected tools behave, without recreating any of them.

Signal path, stereo, 32-bit float I/O, double-precision filter state:

1. **Input** gain, then the chain, then a latency-matched dry path for BYPASS.
2. **EQ**: high-pass (12 dB/oct, Butterworth Q), low shelf, two bells, high shelf; RBJ biquads in double precision. Each module has its own ON switch; an off module is skipped entirely (bit-exact).
3. **Glue compressor**: stereo-linked soft-knee feed-forward compressor, peak-envelope detector (25 ms release hold) so the static gain reduction matches the dial, THRESH / RATIO / ATTACK / RELEASE / MAKEUP and a parallel MIX. THRESH at 0 dB does nothing.
4. **Multiband**: two Linkwitz-Riley 4th-order crossovers (TPT state-variable filters) with an all-pass-compensated low band, so the three bands sum flat to within 4e-5 dB. Each band has a relative-level leveller (DYN: evens out loud peaks against the band's own average, 0 does nothing) and a level trim.
5. **Saturation**: TAPE (symmetric tanh), TUBE (asymmetric, even harmonics), HARD (tighter knee). 8x oversampled with IIR half-band filters, mixed in the oversampled domain; TONE tilts the saturated part; MIX blends. Alias products stay below -140 dBFS on a 15 kHz sine at 18 dB drive.
6. **Stereo**: mid/side WIDTH and BASS MONO (a 4th-order Linkwitz-Riley high-pass on the side signal, so everything below the corner is mono).
7. **Limiter**: lookahead true-peak limiter. Required gain per sample comes from an 8x / 32-tap Kaiser-sinc interpolator (plus a parabolic refinement of the largest local maximum) over the 7 points between samples; a sliding minimum over the lookahead window, a release stage and two cascaded boxcar smoothers shape the gain so it never exceeds the requirement at any sample the interpolator can see. A 0.1 dB safety margin below the CEILING covers the residual error of the gain slope against the interpolated peak. PUSH is gain into the limiter.
8. **Meters**: BS.1770-4 K-weighted LUFS (momentary 400 ms, short-term 3 s, histogram-gated integrated) and a true-peak maximum; the same 8x interpolator is used for the peak meter.

Defaults: GLUE threshold 0 dB, DRIVE 0 dB, MULTIBAND dynamics 0, WIDTH 100 %, BASS MONO off, PUSH 0 dB, CEILING -1 dB. A new instance is transparent apart from the limiter.

Parameter changes are smoothed (about 20 ms). Float round-trip noise from the host's normalised values is snapped away at 0 dB so "off" is exactly off.

Latency is fixed: lookahead 2 x 2.5 ms of window plus the interpolator reach, 262 samples at 48 kHz (5.46 ms), reported to the host (scaled with sample rate).

Caller sketch: `engine.prepare(rate, maxBlock); engine.set(settings); engine.snap();` then `engine.process(L, R, n)`.
