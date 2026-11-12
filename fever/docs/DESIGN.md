# FEVER design

An original one-knob mood effect. The FEVER parameter is dry at 0.5 (null-test transparent). Values below 0.5 fade in a granular pitch cloud whose grains are pitched to octaves and perfect fifths, with detune and stereo width from SPREAD and a spawn rate from DENSITY. Values above 0.5 fade in a zero-latency spectral path: a bank of high-Q resonators at fixed inharmonic frequencies (247, 619, 1553, 3901 Hz), a cold high-passed feedback-delay tail whose RT60 is a function of FREEZE, and a two-voice dissonant halo (a Hilbert SSB frequency shifter that splits a 440 Hz tone by +38 / −51 cents at full HALO, and shifts every other partial by the same Hertz).

The two side parameters are the same slots on both sides; the UI labels and the DSP mapping change at the centre. Heartbeat, when armed, phase-locks a pulse to the host PPQ (or free-runs at the host BPM) and wobbles the effective FEVER amount by DEPTH at a chosen note division. Gain match uses a K-weighted envelope so a full sweep stays inside a tight loudness window. The safety limiter is a linked, zero-lookahead sample-peak ceiling at −1 dBFS, on by default. All audible parameters are one-pole smoothed (~20 ms) so fast automation does not click. Disabled sides are skipped. REDUCE MOTION is decorative only.

Inspired by the idea of a one-knob lush-to-harsh mood sweep, not a recreation of any existing product.
