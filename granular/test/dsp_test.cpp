// Granular DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/GranularDSP.h"
#include <cstdio>
#include <chrono>
#include <random>
#include <vector>
using namespace gr::dsp;
static int fails = 0;
static void check (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; }
static const double SR = 48000.0;
using Buf = std::vector<float>;

static void run (Engine& e, Buf& l, Buf& r, int block = 256)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block)
    {
        float* c[2] = { l.data() + i, r.data() + i };
        e.process (c, (int) std::min ((size_t) block, l.size() - i));
    }
}
static Buf sine (double sec, double hz, float amp) { Buf x ((size_t) (sec * SR)); for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float) std::sin (2 * kPi * hz * (double) i / SR); return x; }
static Buf noise (double sec, float amp, unsigned seed) { std::mt19937 g (seed); std::normal_distribution<float> d (0, amp); Buf x ((size_t) (sec * SR)); for (auto& v : x) v = d (g); return x; }
static double rms (const Buf& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) (b - a) + 1e-30); }
static double peak (const Buf& x) { double p = 0; for (auto v : x) p = std::max (p, (double) std::fabs (v)); return p; }
static bool finite (const Buf& x) { for (auto v : x) if (! std::isfinite (v)) return false; return true; }
static double goertzel (const Buf& x, size_t a, size_t b, double hz)
{
    const double w = 2 * kPi * hz / SR, c = 2 * std::cos (w); double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) (b - a);
}
static Params wet (Params p) { p.mix = 1.0f; p.chaos = 0.0f; p.sprayMs = 0; return p; }

int main()
{
    Params base; base.sprayMs = 0; base.chaos = 0;

    { // silence in -> silence out
        Engine e; e.setParams (wet (base)); e.prepare (SR, 2);
        Buf l ((size_t) SR, 0.0f), r = l; run (e, l, r);
        check (peak (l) < 1e-6 && peak (r) < 1e-6, "silence in, silence out");
    }
    { // mix 0 -> dry passthrough
        Params p = base; p.mix = 0.0f; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (1.0, 440, 0.3f), r = l, ref = l; run (e, l, r);
        double err = 0; for (size_t i = 0; i < l.size(); ++i) err = std::max (err, (double) std::fabs (l[i] - ref[i]));
        check (err < 1e-5, "mix 0 = dry signal, bit-near exact");
    }
    { // wet output of a steady sine keeps its pitch (pitch 0) and level is sane
        Params p = wet (base); p.sizeMs = 80; p.density = 30; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (2.0, 440, 0.3f), r = l; run (e, l, r);
        const size_t a = (size_t) (1.0 * SR), b = (size_t) (2.0 * SR);
        const double g440 = goertzel (l, a, b, 440), g880 = goertzel (l, a, b, 880);
        std::printf ("      pitch 0: 440=%.4f 880=%.4f rms=%.3f\n", g440, g880, rms (l, a, b));
        check (g440 > 4 * g880 && rms (l, a, b) > 0.05 && rms (l, a, b) < 0.6, "pitch 0: grains keep 440 Hz, level in range");
    }
    { // pitch +12 -> dominant 880
        Params p = wet (base); p.pitch = 12; p.sizeMs = 80; p.density = 30; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (2.0, 440, 0.3f), r = l; run (e, l, r);
        const size_t a = (size_t) (1.0 * SR), b = (size_t) (2.0 * SR);
        const double g440 = goertzel (l, a, b, 440), g880 = goertzel (l, a, b, 880);
        std::printf ("      pitch +12: 440=%.4f 880=%.4f\n", g440, g880);
        check (g880 > 4 * g440, "pitch +12 st: grains play an octave up (880 Hz dominates)");
    }
    { // pitch -12 -> 220
        Params p = wet (base); p.pitch = -12; p.sizeMs = 80; p.density = 30; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (2.0, 440, 0.3f), r = l; run (e, l, r);
        const size_t a = (size_t) (1.0 * SR), b = (size_t) (2.0 * SR);
        check (goertzel (l, a, b, 220) > 4 * goertzel (l, a, b, 440), "pitch -12 st: 220 Hz dominates");
    }
    { // reverse 100 %: pitch unchanged on a steady tone
        Params p = wet (base); p.reverse = 1.0f; p.sizeMs = 80; p.density = 30; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (2.0, 440, 0.3f), r = l; run (e, l, r);
        const size_t a = (size_t) (1.0 * SR), b = (size_t) (2.0 * SR);
        check (goertzel (l, a, b, 440) > 4 * goertzel (l, a, b, 880) && rms (l, a, b) > 0.05, "reverse grains: steady 440 Hz stays 440 Hz");
    }
    { // reverse of a rising ramp-envelope: reverse grains read backwards -> output envelope trend inside grains decreases
        Params p = wet (base); p.reverse = 1.0f; p.sizeMs = 200; p.density = 2; p.shape = Shape::glitch; p.positionMs = 0; p.toneHz = 20000;
        Engine e; e.setParams (p); e.prepare (SR, 1);
        // input: sawtooth envelope noise-free DC ramp 0..1 repeating every 10 ms
        Buf l ((size_t) (1.0 * SR)), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = 0.5f * (float) ((i % 480) / 480.0);
        Buf in = l; r = l;
        float* c[1] = { l.data() }; Engine e1; e1.setParams (p); e1.prepare (SR, 1);
        for (size_t i = 0; i < l.size(); i += 256) { c[0] = l.data() + i; e1.process (c, (int) std::min ((size_t) 256, l.size() - i)); }
        // count rising vs falling steps in the steady part of the wet signal
        int up = 0, dn = 0; for (size_t i = (size_t) (0.6 * SR); i < l.size(); ++i) { const float d = l[i] - l[i - 1]; if (d > 1e-5f && d < 0.01f) ++up; else if (d < -1e-5f && d > -0.01f) ++dn; }
        std::printf ("      reverse ramp: up=%d down=%d\n", up, dn);
        check (dn > 3 * up, "reverse 100 %: a rising ramp plays back falling");
    }
    { // freeze holds the sound after input stops; unfreeze lets it fade (old material is replaced by silence)
        Params p = wet (base); p.sizeMs = 100; p.density = 25; p.positionMs = 300; p.sprayMs = 500;
        Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (1.0, 330, 0.3f), r = l; run (e, l, r);
        p.freeze = true; e.setParams (p);
        Buf l2 ((size_t) (3.0 * SR), 0.0f), r2 = l2; run (e, l2, r2);
        const double held = rms (l2, (size_t) (2.0 * SR), (size_t) (3.0 * SR));
        p.freeze = false; e.setParams (p);
        Buf l3 ((size_t) (5.0 * SR), 0.0f), r3 = l3; run (e, l3, r3);
        const double after = rms (l3, (size_t) (4.5 * SR), (size_t) (5.0 * SR));
        std::printf ("      freeze: held=%.4f after unfreeze=%.6f\n", held, after);
        check (held > 0.05, "freeze: cloud keeps playing 2-3 s after the input stopped");
        check (after < 1e-4, "unfreeze: buffer refills with silence, cloud dies out");
    }
    { // freeze ignores new input
        Params p = wet (base); p.sizeMs = 100; p.density = 25; p.positionMs = 200;
        Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (1.0, 330, 0.3f), r = l; run (e, l, r);
        p.freeze = true; e.setParams (p);
        Buf l2 = sine (2.0, 1500, 0.3f), r2 = l2; run (e, l2, r2);
        const size_t a = (size_t) (1.0 * SR), b = (size_t) (2.0 * SR);
        check (goertzel (l2, a, b, 330) > 5 * goertzel (l2, a, b, 1500), "freeze: new input (1500 Hz) is not recorded, frozen 330 Hz plays");
    }
    { // worst case: dense, long, max spread, feedback 0.95, loud noise -> bounded, finite
        Params p; p.mix = 1.0f; p.sizeMs = 1000; p.density = 80; p.spread = 24; p.reverse = 0.5f; p.chaos = 1.0f; p.feedback = 0.95f; p.sprayMs = 1000; p.gainDb = 12;
        Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = noise (10.0, 0.7f, 3), r = noise (10.0, 0.7f, 4); run (e, l, r);
        std::printf ("      stress peak=%.3f\n", peak (l));
        check (finite (l) && finite (r) && peak (l) < 2.0 && peak (r) < 2.0, "dense + feedback 0.95 + gain +12: finite and soft-limited (< 2)");
    }
    { // feedback lengthens the tail (vs none), stays bounded and still decays
        auto tailOf = [] (float fb, double from, double to)
        {
            Params p = wet (Params {}); p.sprayMs = 0; p.feedback = fb; p.sizeMs = 150; p.density = 20; p.positionMs = 100;
            Engine e; e.setParams (p); e.prepare (SR, 2);
            Buf l = sine (0.5, 500, 0.3f); l.resize ((size_t) (10 * SR), 0.0f); Buf r = l; run (e, l, r);
            return std::make_pair (rms (l, (size_t) (from * SR), (size_t) (to * SR)), peak (l));
        };
        const auto t0 = tailOf (0.0f, 1.5, 2.5), t9 = tailOf (0.9f, 1.5, 2.5), late = tailOf (0.9f, 9.0, 10.0), t95 = tailOf (0.95f, 1.5, 2.5);
        std::printf ("      tail 1.5-2.5 s: fb0=%.6f fb0.9=%.5f fb0.95=%.5f; fb0.9 at 9-10 s=%.6f\n", t0.first, t9.first, t95.first, late.first);
        check (t9.first > 0.005 && t9.first > 20 * t0.first, "feedback 0.9: an echoing tail continues long after the input (none without feedback)");
        check (late.first < 0.25 * t9.first && t9.second < 2.0 && t95.second < 2.0, "feedback: bounded and decays over time (loop gain < 1)");
    }
    { // NaN / inf input recovers
        Params p = base; p.mix = 0.5f; p.feedback = 0.5f; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (1.0, 440, 0.3f), r = l; l[1000] = NAN; r[2000] = INFINITY; l[3000] = -INFINITY;
        run (e, l, r);
        Buf l2 = sine (1.0, 440, 0.3f), r2 = l2; run (e, l2, r2);
        check (finite (l) && finite (r) && finite (l2) && rms (l2, 0, l2.size()) > 0.05, "NaN / inf input: output stays finite and keeps working");
    }
    { // every shape / spread mode / mono / odd block sizes / sample rates
        bool ok = true;
        for (double sr : { 44100.0, 96000.0, 192000.0 })
            for (int sh = 0; sh < 4; ++sh)
                for (int sm = 0; sm < 4; ++sm)
                {
                    Params p; p.shape = (Shape) sh; p.spreadMode = (SpreadMode) sm; p.spread = 19; p.reverse = 0.3f; p.mix = 1;
                    Engine e; e.setParams (p); e.prepare (sr, sm == 3 ? 1 : 2);
                    Buf l ((size_t) (sr * 0.6)); for (size_t i = 0; i < l.size(); ++i) l[i] = 0.3f * (float) std::sin (i * 0.05);
                    Buf r = l;
                    for (size_t i = 0, k = 1; i < l.size(); i += k, k = k % 1031 + 37) { float* c[2] = { l.data() + i, r.data() + i }; e.process (c, (int) std::min (k, l.size() - i)); }
                    ok = ok && finite (l) && peak (l) < 2.0;
                }
        check (ok, "all shapes x spread modes x rates (44.1-192k) x mono/stereo x odd block sizes: finite, bounded");
    }
    { // spread quantisation
        Rng g; bool semis = true, fifths = true, oct = true;
        for (int i = 0; i < 2000; ++i)
        {
            const float s = spreadOffset (g, 7.5f, SpreadMode::semis); semis = semis && std::fabs (s - std::round (s)) < 1e-6f && std::fabs (s) <= 8;
            const float f = spreadOffset (g, 12.0f, SpreadMode::fifths); fifths = fifths && (f == 0 || std::fabs (f) == 5 || std::fabs (f) == 7 || std::fabs (f) == 12);
            const float o = spreadOffset (g, 24.0f, SpreadMode::octaves); oct = oct && std::fmod (std::fabs (o), 12.0f) == 0 && std::fabs (o) <= 24;
        }
        check (semis && fifths && oct, "spread modes quantise to semitones / fifths+fourths+octaves / octaves");
    }
    { // display taps are populated
        Params p = wet (base); p.density = 40; p.sizeMs = 200; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = sine (1.0, 440, 0.3f), r = l; run (e, l, r);
        check (e.activeGrains.load() > 2 && e.outLevel.load() > 0.05f, "display taps: active grains and output level published");
    }
    { // performance: worst case 64 grains at 48k must run far faster than real time
        Params p; p.mix = 1.0f; p.sizeMs = 1000; p.density = 80; p.spread = 12; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = noise (10.0, 0.3f, 9), r = l;
        const auto t0 = std::chrono::steady_clock::now(); run (e, l, r);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      10 s of audio at max load in %.3f s (%.0fx real time)\n", s, 10.0 / s);
        check (s < 2.5, "max load processes at > 4x real time on one core");
    }
    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
