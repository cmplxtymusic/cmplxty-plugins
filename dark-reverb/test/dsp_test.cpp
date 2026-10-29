// Dark Reverb DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/ReverbDSP.h"
#include <cstdio>
#include <chrono>
#include <complex>
#include <random>
#include <vector>
using namespace dr::dsp;
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
static Params wetOnly (Params p) { p.mix = 1.0f; return p; }
static Buf noise (double sec, float amp, unsigned seed) { std::mt19937 g (seed); std::normal_distribution<float> d (0, amp); Buf x ((size_t) (sec * SR)); for (auto& v : x) v = d (g); return x; }
static double rms (const Buf& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) (b - a) + 1e-30); }
static double db (double v) { return 20.0 * std::log10 (v + 1e-12); }

// T60 from the Schroeder backward integral (fit -5..-25 dB, extrapolate)
static double t60 (const Buf& ir)
{
    std::vector<double> e (ir.size()); double acc = 0;
    for (size_t i = ir.size(); i-- > 0;) { acc += (double) ir[i] * ir[i]; e[i] = acc; }
    const double tot = e[0]; double t5 = -1, t25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double d = 10 * std::log10 (e[i] / tot);
        if (t5 < 0 && d <= -5) t5 = (double) i / SR;
        if (t25 < 0 && d <= -25) { t25 = (double) i / SR; break; }
    }
    return t25 > 0 ? (t25 - t5) * 3.0 : 1e9;
}
static Buf impulse (Params p, double sec)
{
    Engine e; p = wetOnly (p); e.setParams (p); e.prepare (SR, 2);
    Buf l ((size_t) (sec * SR), 0.0f), r = l; l[0] = r[0] = 1.0f; run (e, l, r);
    for (size_t i = 0; i < l.size(); ++i) l[i] = 0.5f * (l[i] + r[i]);
    return l;
}
static double goertzel (const Buf& x, size_t a, size_t b, double hz)
{
    const double w = 2 * kPi * hz / SR, c = 2 * std::cos (w); double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) (b - a);
}
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto wl = std::polar (1.0, -2 * kPi / (double) len);
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
// spectral flatness (geometric / arithmetic mean of power) between 200 Hz and 6 kHz: lower = more resonant / metallic
static double flatness (const Buf& x)
{
    const size_t n = 1 << 15; std::vector<std::complex<double>> a (n); for (size_t i = 0; i < n && i < x.size(); ++i) a[i] = x[i]; fft (a);
    double lg = 0, ar = 0; int k = 0;
    for (size_t i = (size_t) (200.0 * n / SR); i < (size_t) (6000.0 * n / SR); ++i) { const double pw = std::norm (a[i]) + 1e-20; lg += std::log (pw); ar += pw; ++k; }
    return std::exp (lg / k) / (ar / k);
}
static double bandEnergy (const Buf& x, double lo, double hi)
{
    const size_t n = 1 << 16; std::vector<std::complex<double>> a (n); for (size_t i = 0; i < n && i < x.size(); ++i) a[i] = x[i]; fft (a);
    double s = 0; for (size_t i = (size_t) (lo * n / SR); i < (size_t) (hi * n / SR); ++i) s += std::norm (a[i]); return s;
}

int main()
{
    Params base; base.darkHz = 16000.0f; base.metal = 0.0f; base.mod = 0.0f;
    // 1) decay time follows the Decay parameter
    for (float T : { 1.5f, 4.0f })
    {
        Params p = base; p.decayS = T; auto ir = impulse (p, T * 1.6 + 1.0);
        const double m = t60 (ir);
        std::printf ("    decay %.1f s -> measured T60 %.2f s\n", T, m);
        check (m > T * 0.75 && m < T * 1.3, "measured decay time within 25 % of the Decay setting");
    }
    { Params p = base; p.decayS = 4.0f; p.size = 0.1f; const double a = t60 (impulse (p, 7.5)); p.size = 1.0f; const double b = t60 (impulse (p, 7.5));
      std::printf ("    size 10 %% T60 %.2f, size 100 %% T60 %.2f\n", a, b);
      check (std::fabs (a / b - 1.0) < 0.3, "decay time stays put when Size changes"); }

    // 2) pre-delay
    { Params p = base; p.preDelayMs = 200.0f; p.diffusion = 0.0f; auto ir = impulse (p, 0.6);
      size_t first = 0; for (size_t i = 0; i < ir.size(); ++i) if (std::fabs (ir[i]) > 1e-5f) { first = i; break; }
      std::printf ("    first wet sample at %.1f ms\n", first * 1000.0 / SR);
      check (first >= (size_t) (0.2 * SR) && first < (size_t) (0.2 * SR + 0.1 * SR), "pre-delay delays the tail by 200 ms"); }

    // 3) mix 0 = exact dry; mix 1 = no dry
    { Params p; p.mix = 0.0f; Engine e; e.setParams (p); e.prepare (SR, 2); Buf l = noise (1.0, 0.1f, 1), r = noise (1.0, 0.1f, 2), l0 = l, r0 = r; run (e, l, r);
      double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::max (std::fabs (l[i] - l0[i]), std::fabs (r[i] - r0[i])));
      check (d < 1e-7, "mix 0 passes the input unchanged"); }

    // 4) level: steady noise in, wet RMS in a sane range for short and very long decays
    for (float T : { 2.0f, 30.0f })
    {
        Params p = wetOnly (base); p.decayS = T; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = noise (8.0, 0.1f, 3), r = noise (8.0, 0.1f, 4); const double in = rms (l, 0, l.size()); run (e, l, r);
        const double out = rms (l, l.size() / 2, l.size()); std::printf ("    decay %4.1f s: wet %+.1f dB re input\n", T, db (out / in));
        check (db (out / in) > -12 && db (out / in) < 9, "wet level within -12..+9 dB of the input");
    }

    // 5) darkness: the Dark control removes highs from the tail
    { Params p = base; p.decayS = 3.0f; const auto br = impulse (p, 3.0); p.darkHz = 1200.0f; const auto dk = impulse (p, 3.0);
      const double rb = bandEnergy (br, 5000, 12000) / bandEnergy (br, 100, 1000), rd = bandEnergy (dk, 5000, 12000) / bandEnergy (dk, 100, 1000);
      std::printf ("    high/low energy: bright %.3g, dark %.3g\n", rb, rd);
      check (rd < rb * 0.1, "Dark 1.2 kHz cuts the tail's highs by more than 10 dB"); }

    // 6) metal: makes the tail resonant (lower spectral flatness)
    { Params p = base; p.decayS = 2.0f; const double f0 = flatness (impulse (p, 1.0)); p.metal = 1.0f; const double f1 = flatness (impulse (p, 1.0));
      std::printf ("    spectral flatness: metal 0 %.3f, metal 1 %.3f\n", f0, f1);
      check (f1 < f0 * 0.75, "Metal makes the tail more resonant"); }

    // 7) shimmer: an octave up appears in the tail
    {
        auto run440 = [] (bool shim) {
            Params p = wetOnly (Params {}); p.darkHz = 16000; p.decayS = 4; p.shimOn = shim; p.shimAmt = 0.7f; p.shimSemis = 12; p.mod = 0.5f; p.metal = 0;
            Engine e; e.setParams (p); e.prepare (SR, 2); Buf l ((size_t) (SR * 4)), r;
            for (size_t i = 0; i < l.size(); ++i) l[i] = i < (size_t) (SR * 1.5) ? 0.2f * (float) std::sin (2 * kPi * 440.0 * i / SR) : 0.0f;
            r = l; run (e, l, r); Buf t (l.begin() + (long) (SR * 1.6), l.end()); return bandEnergy (t, 820, 940) / bandEnergy (t, 410, 470); };
        const double a = run440 (false), b = run440 (true);
        std::printf ("    880/440 Hz in the tail: shimmer off %.3f, on %.3f\n", a, b);
        check (b > a * 100.0 && b > 0.1, "shimmer adds the octave above to the tail (energy ratio)");
    }

    // 8) freeze: holds the tail, ignores new input, releases afterwards
    {
        Params p = wetOnly (base); p.decayS = 1.0f; Engine e; e.setParams (p); e.prepare (SR, 2);
        Buf l = noise (1.0, 0.1f, 5), r = noise (1.0, 0.1f, 6); run (e, l, r);
        const double before = rms (l, (size_t) (0.8 * SR), l.size());
        p.freeze = true; e.setParams (p);
        Buf fl = noise (6.0, 0.4f, 7), fr = noise (6.0, 0.4f, 8); run (e, fl, fr);   // loud new input while frozen
        const double h1 = rms (fl, (size_t) (1.0 * SR), (size_t) (2.0 * SR)), h2 = rms (fl, (size_t) (5.0 * SR), (size_t) (6.0 * SR));
        Engine e2; Params q = wetOnly (base); q.decayS = 1.0f; e2.setParams (q); e2.prepare (SR, 2); Buf l2 = noise (1.0, 0.1f, 5), r2 = noise (1.0, 0.1f, 6); run (e2, l2, r2);
        q.freeze = true; e2.setParams (q); Buf sl ((size_t) (SR * 2), 0.0f), sr2 = sl; run (e2, sl, sr2);   // same tail, silence while frozen
        const double hs = rms (sl, (size_t) (1.0 * SR), (size_t) (2.0 * SR));
        std::printf ("    before %.1f dB, frozen at 1-2 s %.1f dB, at 5-6 s %.1f dB\n", db (before), db (h1), db (h2));
        check (std::fabs (db (h2 / h1)) < 1.5, "frozen tail holds its level (1-2 s vs 5-6 s within 1.5 dB) with decay 1 s");
        std::printf ("    frozen with silence %.1f dB vs with loud input %.1f dB\n", db (hs), db (h1));
        check (std::fabs (db (h1 / hs)) < 2.0, "frozen tail does not take in new input (within 2 dB of freezing over silence)");
        p.freeze = false; e.setParams (p); Buf z ((size_t) (SR * 4), 0.0f), z2 = z; run (e, z, z2);
        check (db (rms (z, (size_t) (3.5 * SR), z.size()) / h2) < -40, "unfreezing lets the tail decay again");
    }

    // 9) stability: everything at maximum for 20 s
    {
        Params p; p.decayS = 30; p.size = 1; p.metal = 1; p.mod = 1; p.diffusion = 1; p.shimOn = true; p.shimAmt = 1; p.mix = 1; p.darkHz = 20000; p.lowCutHz = 10;
        Engine e; e.setParams (p); e.prepare (SR, 2); Buf l = noise (20.0, 0.3f, 9), r = noise (20.0, 0.3f, 10);
        auto t0 = std::chrono::steady_clock::now(); run (e, l, r); const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        float pk = 0; bool fin = true; for (size_t i = 0; i < l.size(); ++i) { fin = fin && std::isfinite (l[i]) && std::isfinite (r[i]); pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i]))); }
        std::printf ("    max settings: peak %.2f, 20 s stereo in %.0f ms\n", pk, ms);
        check (fin && pk < 8.0f, "max settings stay bounded and finite");
        check (ms < 4000, "faster than 5x real time");
    }

    // 10) block-size independence, mono, NaN
    {
        Params p; p.shimOn = true; p.mod = 0.8f; p.metal = 0.5f;
        auto go = [&] (int bs) { Engine e; e.setParams (p); e.prepare (SR, 2); Buf l = noise (2.0, 0.1f, 11), r = noise (2.0, 0.1f, 12); run (e, l, r, bs); return l; };
        const auto a = go (64), b = go (480); double d = 0; for (size_t i = 0; i < a.size(); ++i) d = std::max (d, (double) std::fabs (a[i] - b[i]));
        check (d < 1e-5, "output does not depend on the block size");
        Engine e; e.setParams (p); e.prepare (SR, 1); Buf m = noise (1.0, 0.1f, 13);
        for (size_t i = 0; i < m.size(); i += 256) { float* c[1] = { m.data() + i }; e.process (c, (int) std::min<size_t> (256, m.size() - i)); }
        bool fin = true; for (float v : m) fin = fin && std::isfinite (v); check (fin && rms (m, 0, m.size()) > 0.01, "mono processing works");
        Engine n; n.setParams (p); n.prepare (SR, 2); Buf l = noise (1.0, 0.1f, 14), r = l; l[100] = NAN; r[200] = INFINITY; run (n, l, r);
        fin = true; for (size_t i = 0; i < l.size(); ++i) fin = fin && std::isfinite (l[i]) && std::isfinite (r[i]);
        check (fin && rms (l, l.size() / 2, l.size()) > 0.01, "NaN / Inf input is ignored and processing carries on");
    }
    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails;
}
