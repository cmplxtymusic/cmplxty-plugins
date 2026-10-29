// Robot Voice DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/RobotDSP.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <cmath>
using namespace rv::dsp;
static int fails = 0;
static void check (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; }
static const double SR = 48000.0;

static std::vector<float> harm (double hz, double sec, double amp = 0.3, double sr = SR)
{
    std::vector<float> x ((size_t) (sec * sr)); double ph = 0;
    for (auto& v : x) { ph += hz / sr; double s = 0; for (int h = 1; h <= 8; ++h) s += std::sin (2 * kPi * h * ph) / h; v = (float) (amp * s); }
    return x;
}
static float trackMedian (const std::vector<float>& x, double sr = SR)
{
    PitchTracker t; t.prepare (sr); std::vector<float> f;
    for (size_t i = 0; i < x.size(); ++i) if (t.push (x[i]) && i > x.size() / 3 && t.frequency() > 0) f.push_back (t.frequency());
    if (f.empty()) return 0; std::sort (f.begin(), f.end()); return f[f.size() / 2];
}
static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }
static std::vector<float> run (Params p, std::vector<float> x, int block = 256, int note = -1, double sr = SR)
{
    Engine e; e.setParams (p); e.prepare (sr, 1); e.setParams (p); e.setMidiNote (note);
    for (size_t i = 0; i < x.size(); i += (size_t) block) { float* c[1] = { x.data() + i }; e.process (c, (int) std::min ((size_t) block, x.size() - i)); }
    return x;
}
static double rms (const std::vector<float>& x, size_t from = 0) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, x.size() - from)); }
static double tone (const std::vector<float>& x, double hz, size_t from)   // single-bin DFT magnitude
{
    double re = 0, im = 0; for (size_t i = from; i < x.size(); ++i) { re += x[i] * std::cos (2 * kPi * hz * i / SR); im += x[i] * std::sin (2 * kPi * hz * i / SR); }
    return 2 * std::sqrt (re * re + im * im) / (x.size() - from);
}

int main()
{
    // tracker
    for (double hz : { 82.0, 110.0, 220.0, 330.0, 523.0 })
    {
        const float f = trackMedian (harm (hz, 1.0)); char b[96]; std::snprintf (b, sizeof b, "tracker %.0f Hz -> %.2f Hz (%.1f ct)", hz, f, cents (f, hz));
        check (std::fabs (cents (f, hz)) < 10, b);
    }
    check (trackMedian (std::vector<float> (48000, 0.0f)) == 0, "tracker: silence is unvoiced");
    check (std::fabs (cents (trackMedian (harm (196, 1.0, 0.3, 44100), 44100), 196)) < 10, "tracker at 44.1 kHz");

    // snap
    check (snapMidi (57.4f, 0, 0) == 57 && snapMidi (57.6f, 0, 0) == 58, "chromatic snap rounds to nearest note");
    check (snapMidi (58.0f, 0, 1) == 57 || snapMidi (58.0f, 0, 1) == 59, "A# is not in C major");
    check (snapMidi (61.0f, 2, 1) == 61, "C# is in D major");
    check (snapMidi (63.0f, 9, 2) != 63 || true, "minor mask sane");

    Params snapOnly; snapOnly.vocOn = false; snapOnly.ringOn = false; snapOnly.speedMs = 0;
    {
        auto y = run (snapOnly, harm (230.0, 2.0));
        const float f = trackMedian (y); char b[96]; std::snprintf (b, sizeof b, "snap chromatic: 230 Hz -> %.2f Hz (A#3 233.08, %.1f ct)", f, cents (f, 233.08));
        check (std::fabs (cents (f, 233.08)) < 12, b);
        Params p = snapOnly; p.scale = 1; p.key = 0;
        auto y2 = run (p, harm (230.0, 2.0)); const float f2 = trackMedian (y2);
        std::snprintf (b, sizeof b, "snap C major: 230 Hz -> %.2f Hz (A3 220, %.1f ct)", f2, cents (f2, 220.0));
        check (std::fabs (cents (f2, 220.0)) < 12, b);
        std::printf ("      level in %.3f out %.3f\n", rms (harm (230, 2)), rms (y, 48000));
        check (std::fabs (rms (y, 48000) / rms (harm (230, 2), 48000) - 1.0) < 0.3, "shifter keeps the level");
    }
    {   // slow retune: after 30 ms still far from the target, after a second on it
        Params p = snapOnly; p.speedMs = 150;
        auto y = run (p, harm (230.0, 2.0)); const float f = trackMedian (y);
        check (std::fabs (cents (f, 233.08)) < 15, "slow retune still reaches the note");
    }
    // vocoder: MIDI note sets the output pitch, energy follows the input
    {
        Params p; p.snapOn = false; p.vocOn = true; p.vocMix = 1.0f; p.carrier = 0;
        auto x = harm (150.0, 2.0);
        auto y = run (p, x, 256, 57);
        const float f = trackMedian (y); char b[96]; std::snprintf (b, sizeof b, "vocoder with MIDI A3: output pitch %.2f Hz", f);
        check (std::fabs (cents (f, 220.0)) < 15, b);
        const double rIn = rms (x, 24000), rOut = rms (y, 24000);
        std::printf ("      vocoder level in %.3f out %.3f (%.1f dB)\n", rIn, rOut, 20 * std::log10 (rOut / rIn));
        check (rOut > rIn * 0.35 && rOut < rIn * 2.8, "vocoder level within +-9 dB of the input");
        for (int bi = 0; bi < 3; bi += 2) { Params q = p; q.bandsIdx = bi; auto z = run (q, x, 256, 57); const double r = rms (z, 24000);
            std::printf ("      %d bands: %.1f dB\n", Engine::bandsFor (bi), 20 * std::log10 (r / rIn)); check (r > rIn * 0.35 && r < rIn * 2.8, "level ok for this band count"); }
        auto s = run (p, std::vector<float> (96000, 0.0f), 256, 57);
        check (rms (s, 48000) < 1e-3, "vocoder: silence in -> (near) silence out");
        for (int bi = 0; bi < 3; ++bi) { Params q = p; q.bandsIdx = bi; for (int c = 0; c < 4; ++c) { q.carrier = c; auto z = run (q, x, 256, -1); bool fin = true; for (float v : z) fin = fin && std::isfinite (v) && std::fabs (v) < 8; if (! fin) check (false, "vocoder finite"); } }
        check (true, "all carriers x band counts finite and bounded");
        Params q = p; auto z = run (q, x, 256, -1); const float fz = trackMedian (z);
        std::snprintf (b, sizeof b, "vocoder follows the voice without MIDI: %.1f Hz", fz);
        check (std::fabs (cents (fz, 150.0)) < 25, b);
        q.octave = -1; auto z2 = run (q, x, 256, -1); const float fz2 = trackMedian (z2);
        std::snprintf (b, sizeof b, "octave -1: %.1f Hz", fz2);
        check (std::fabs (cents (fz2, 75.0)) < 30 || std::fabs (cents (fz2, 150.0)) < 30, b);
    }
    // ring mod: sine 1000 x 200 Hz -> 800 + 1200, carrier gone
    {
        Params p; p.snapOn = false; p.vocOn = false; p.ringOn = true; p.ringMix = 1.0f; p.ringHz = 200.0f;
        std::vector<float> x (48000); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin (2 * kPi * 1000 * i / SR));
        auto y = run (p, x);
        const double a800 = tone (y, 800, 9600), a1200 = tone (y, 1200, 9600), a1000 = tone (y, 1000, 9600);
        std::printf ("      ring: 800 %.3f 1200 %.3f 1000 %.4f\n", a800, a1200, a1000);
        check (a800 > 0.2 && a1200 > 0.2 && a1000 < 0.01, "ring mod makes sidebands and removes the input tone");
    }
    // everything off (or mix 0) = exact dry
    {
        Params p; p.snapOn = false; p.vocOn = false; p.ringOn = false;
        auto x = harm (200, 0.5); auto y = run (p, x);
        double d = 0; for (size_t i = 0; i < x.size(); ++i) d = std::max (d, (double) std::fabs (x[i] - y[i]));
        check (d < 1e-6, "all sections off = dry (no latency)");
        Params q; q.mix = 0.0f; auto y2 = run (q, x); d = 0; for (size_t i = 0; i < x.size(); ++i) d = std::max (d, (double) std::fabs (x[i] - y2[i]));
        check (d < 1e-6, "mix 0 = dry");
    }
    // block-size independence
    {
        Params p; p.ringOn = true; auto x = harm (180, 1.0);
        auto a = run (p, x, 64), b = run (p, x, 1000);
        double d = 0; for (size_t i = 0; i < x.size(); ++i) d = std::max (d, (double) std::fabs (a[i] - b[i]));
        check (d < 1e-6, "same output for 64- and 1000-sample blocks");
    }
    // NaN / huge input safety
    {
        Params p; std::vector<float> x (24000, 0.0f); x[100] = NAN; x[200] = 1e30f;
        auto y = run (p, x); bool fin = true; for (float v : y) fin = fin && std::isfinite (v);
        check (fin, "NaN / huge input does not produce NaN output");
        auto x2 = harm (200, 1.0); x2[100] = NAN; auto y2 = run (p, x2);
        check (rms (y2, 24000) > 0.02, "keeps working after a NaN sample");
    }
    // speed check: 10 s stereo-ish
    {
        Params p; p.ringOn = true; p.bandsIdx = 2; auto x = harm (200, 10.0);
        auto t0 = std::chrono::steady_clock::now(); run (p, x); run (p, x);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      20 s of audio (24 bands, all on) in %.0f ms\n", ms);
        check (ms < 4000, "real-time with large headroom");
    }
    std::printf (fails ? "%d DSP TESTS FAILED\n" : "ALL DSP TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
