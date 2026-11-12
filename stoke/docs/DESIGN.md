# STOKE design

Original mid-focus effect. Inspired by how respected mid and presence tools work (dynamic mid lift, Pultec-style boost-and-unmask interplay, band-limited harmonic exciters, mid/side mid focus, and automatic resonance taming) without recreating any of them.

Signal, per sample, stereo-linked:

1. Optional mid/side blend (MID ONLY): process toward the mid, put the original side back after.
2. Linked detectors on the mid: a mid-band envelope, a slow average, a 330 Hz mud envelope, and three narrow 2–5 kHz bands against a wider 2–6 kHz average.
3. A peaking mid lift at CENTRE / WIDTH, amount from FOCUS (0–9 dB). Loud mid peaks get a little less lift, scaled by SMOOTH, so a sibilant or a resonant synth note does not get harsh.
4. A shallow dynamic cut around 330 Hz (de-mud), only when CENTRE is well above that region, so a Bass Mids setting can still boost 700 Hz.
5. Three narrow downward bands at 2.2, 3.4 and 5.1 kHz that cut only when a band is louder than the wider high-mid average (de-harsh).
6. A harmonics-only soft clip on the mid band, 8× oversampled, even-order weighted, then high-passed so even products do not fill 200–500 Hz. GLOW is the mix; at zero this path adds nothing.
7. Gain match (on by default), wet/dry MIX, OUTPUT, and a −1 dBFS safety limiter. The limiter does not run on a true wire (FOCUS at 0, unity output), so Amount 0 stays bit-transparent.

At FOCUS = 0 the added gains are identically 1 and GLOW’s mix is 0, so the output is the input, bit for bit. Filters stay primed so engaging FOCUS is click-free. All continuous parameters are one-pole smoothed (~18 ms). Stereo detectors are linked, so a mono input stays mono and width does not collapse in a mono fold.

Caller sketch: `engine.prepare(rate); engine.set(settings); engine.snap();` then `engine.process(L, R, n)`. Processor reports 0 samples of latency. Mono or stereo main bus, matching output. No sidechain.
