// Headless tests for the Drift DSP core.  g++ -std=c++17 -O2 test/dsp_test.cpp -o dsp_test && ./dsp_test
#include "../src/DriftDSP.h"
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <random>
#include <string>
#include <memory>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FAIL %s:%d  %s   ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace df;

// ---- helpers ----
static std::vector<float> sine (double f, double sr, int n, double amp = 0.5, double ph = 0.0)
{ std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / sr + ph)); return v; }
static std::vector<float> sum (std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; }

// run a mono / stereo signal through an engine in blocks of `block` samples
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r, int block)
{
    const int n = (int) l.size();
    for (int i = 0; i < n; i += block)
    {
        float* c[2] = { l.data() + i, r.data() + i };
        e.process (c, 2, std::min (block, n - i));
    }
}
// magnitude (dB, 0 dB = full-scale sine) of the strongest bin within +-tol Hz of f, in the last `fftN` samples
static double levelAt (const std::vector<float>& x, double sr, double f, double tol = 6.0, int fftN = 1 << 15)
{
    SpectrumAnalyzer a; a.init (15); (void) fftN;
    std::vector<float> db ((size_t) a.bins());
    a.analyse (x.data() + (x.size() - (size_t) a.size()), db.data());
    const double binHz = sr / a.size();
    double best = -300;
    for (int b = (int) ((f - tol) / binHz); b <= (int) ((f + tol) / binHz) + 1; ++b) if (b >= 0 && b < a.bins()) best = std::max (best, (double) db[(size_t) b]);
    return best;
}
static double peakFreq (const std::vector<float>& x, double sr, double lo = 20.0, double hi = 20000.0)
{
    SpectrumAnalyzer a; a.init (15); std::vector<float> db ((size_t) a.bins());
    a.analyse (x.data() + (x.size() - (size_t) a.size()), db.data());
    const double binHz = sr / a.size(); int bi = 0; float bv = -400;
    for (int b = std::max (1, (int) (lo / binHz)); b < std::min (a.bins() - 1, (int) (hi / binHz)); ++b) if (db[(size_t) b] > bv) { bv = db[(size_t) b]; bi = b; }
    // parabolic interpolation
    const double y0 = db[(size_t) bi - 1], y1 = db[(size_t) bi], y2 = db[(size_t) bi + 1];
    const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
    return (bi + d) * binHz;
}
static double rms (const std::vector<float>& x, size_t from = 0) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) (x.size() - from)); }
// frequency that sits exactly in the centre of an analysis bin (no leakage issues)
static double binFreq (double sr, int bin, int log2n = 15) { return sr * bin / (double) (1 << log2n); }

static double snapHz (double sr, double hz) { return std::round (hz * (1 << 15) / sr) * sr / (double) (1 << 15); }   // nearest analysis-bin centre

static Engine* make (double sr, const Params& p, int nch = 2) { auto* e = new Engine; e->prepare (sr, nch); e->setParams (p); return e; }

// ---------------------------------------------------------------------------------------------------------------------------------
static void testHilbert()
{
    std::printf ("Hilbert pair: 90 degree phase difference, flat magnitudes\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        HilbertChain a, b; a.init (kHilbertI, true); b.init (kHilbertQ, false);
        double worst = 0, worstMag = 0;
        for (double f0 : { 30.0, 60.0, 120.0, 300.0, 1000.0, 3000.0, 8000.0, 15000.0, 20000.0 })
        {
            const double f = std::round (f0 * 16384 / sr) * sr / 16384.0;   // whole number of cycles in the 16384-sample measurement window
            if (f > sr * 0.45 || f < 0.0006 * sr) continue;
            a.reset(); b.reset();
            const int n = 1 << 16; double sa = 0, sb = 0, ca = 0, cb = 0, ea = 0, eb = 0;   // correlate with sin/cos over the last 16384 samples
            for (int i = 0; i < n; ++i)
            {
                const double x = std::sin (2 * kPi * f * i / sr); const double ya = a.tick (x), yb = b.tick (x);
                if (i >= n - 16384) { const double s = std::sin (2 * kPi * f * i / sr), c = std::cos (2 * kPi * f * i / sr); sa += ya * s; ca += ya * c; sb += yb * s; cb += yb * c; ea += ya * ya; eb += yb * yb; }
            }
            const double pa = std::atan2 (ca, sa), pb = std::atan2 (cb, sb);
            double d = pa - pb; while (d > kPi) d -= 2 * kPi; while (d < -kPi) d += 2 * kPi;
            worst = std::max (worst, std::fabs (std::fabs (d) - kPi / 2) * 180 / kPi);
            worstMag = std::max (worstMag, std::fabs (20 * std::log10 (std::sqrt (ea / eb))));
            const double ma = std::sqrt (2 * ea / 16384);
            CHECK (std::fabs (ma - 1.0) < 1e-3, "allpass magnitude at %g Hz = %g", f, ma);
        }
        std::printf ("  sr %.0f: worst phase error %.4f deg, worst I/Q magnitude mismatch %.5f dB\n", sr, worst, worstMag);
        CHECK (worst < 0.3, "phase error %.3f deg", worst);
        CHECK (worstMag < 1e-3, "mag mismatch");
    }
}

static void testShiftAccuracy()
{
    std::printf ("Shift accuracy and sideband rejection\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double shiftIn : { 500.0, -500.0, 3000.0, -3000.0, 10000.0, -10000.0, 5.0 })
        {
            double shift = shiftIn;
            const int bin = (int) std::round (2000.0 * (1 << 15) / sr);     // ~2 kHz, bin centred
            const double f0 = binFreq (sr, bin);
            shift = snapHz (sr, shift);
            const int n = 1 << 16;
            auto l = sine (f0, sr, n), r = l;
            Params p; p.xoLowHz = 40; p.lowBand = false; p.highBand = false;   // single band => full signal
            p.coarseHz = (float) shift; p.fineHz = 0;
            std::unique_ptr<Engine> e (make (sr, p));
            run (*e, l, r, 512);
            const double wantF = f0 + shift;
            if (wantF < 30 || wantF > sr * 0.47) continue;
            const double pf = peakFreq (l, sr, 20, sr * 0.49);
            const double main = levelAt (l, sr, wantF, 3.0);
            const double image = levelAt (l, sr, f0 - shift, 3.0);
            const double sepOk = std::fabs (shift) > 100.0;
            std::printf ("  sr %6.0f shift %+7.1f: peak %.2f Hz (want %.2f), level %.2f dB, image %.1f dB\n", sr, shift, pf, wantF, main, image);
            CHECK (std::fabs (pf - wantF) < 1.5 + sr / (1 << 15), "peak %f want %f", pf, wantF);
            CHECK (main > -6.5 && main < -5.5, "level %.2f dB", main);   // 0.5 amplitude = -6.02 dB
            if (sepOk && (f0 - shift) > 60 && (f0 - shift) < sr * 0.45) CHECK (image < main - 55.0, "image %.1f vs %.1f", image, main);
        }
}

static void testTransparent()
{
    std::printf ("Zero shift is an allpass (flat gain at every frequency), at any mix / band layout\n");
    const double sr = 48000.0; const int n = 1 << 16;
    for (int layout = 0; layout < 4; ++layout)
        for (int slope = 0; slope < 3; ++slope)
        {
            double worst = 0;
            for (double f0 : { 45.0, 150.0, 290.0, 420.0, 900.0, 2000.0, 3200.0, 6000.0, 12000.0, 20000.0 })
            {
                const double f = snapHz (sr, f0);
                Params p; p.lowBand = layout & 1; p.highBand = layout & 2; p.slope = slope; p.mix = 0.6f; p.xoLowHz = 300; p.xoHighHz = 3000;
                std::unique_ptr<Engine> e (make (sr, p)); auto l = sine (f, sr, n), r = l; run (*e, l, r, 300);
                worst = std::max (worst, std::fabs (levelAt (l, sr, f, 3.0) + 6.0206));
            }
            CHECK (worst < 0.02, "layout %d slope %d: gain deviates %.4f dB", layout, slope, worst);
        }
}

static void testBands()
{
    std::printf ("Only the middle band is shifted; solo / remove behave\n");
    const double sr = 48000.0; const int n = 1 << 16;
    const double fl = binFreq (sr, 137), fm = binFreq (sr, 1365), fh = binFreq (sr, 8192);   // ~100 Hz, ~2 kHz, ~12 kHz
    auto make3 = [&] { return sum (sum (sine (fl, sr, n, 0.2), sine (fm, sr, n, 0.2)), sine (fh, sr, n, 0.2)); };
    auto lvl = [&] (const std::vector<float>& x, double f) { return levelAt (x, sr, f, 3.0); };
    const double ref = 20 * std::log10 (0.2);
    const double dS = snapHz (sr, 300.0);
    {
        Params p; p.xoLowHz = 400; p.xoHighHz = 6000; p.coarseHz = (float) dS;
        std::unique_ptr<Engine> e (make (sr, p)); auto l = make3(), r = l; run (*e, l, r, 256);
        std::printf ("  default layout: low %.1f  mid(unshifted) %.1f  mid(shifted) %.1f  high %.1f dB (ref %.1f)\n", lvl (l, fl), lvl (l, fm), lvl (l, fm + dS), lvl (l, fh), ref);
        CHECK (std::fabs (lvl (l, fl) - ref) < 0.3, "low untouched"); CHECK (std::fabs (lvl (l, fh) - ref) < 0.3, "high untouched");
        CHECK (std::fabs (lvl (l, fm + dS) - ref) < 0.3, "mid shifted"); CHECK (lvl (l, fm) < ref - 55, "mid original gone");
        CHECK (lvl (l, fl + dS) < ref - 40 && lvl (l, fh + dS) < ref - 40, "no stray shifts: %.1f %.1f", lvl (l, fl + dS), lvl (l, fh + dS));
    }
    {   // solo each band
        for (int which = 0; which < 3; ++which)
        {
            Params p; p.xoLowHz = 400; p.xoHighHz = 6000; p.coarseHz = (float) dS; p.soloLow = which == 0; p.soloMid = which == 1; p.soloHigh = which == 2;
            std::unique_ptr<Engine> e (make (sr, p)); auto l = make3(), r = l; run (*e, l, r, 256);
            const double f[3] = { fl, fm + dS, fh };
            for (int k = 0; k < 3; ++k)
            {
                if (k == which) CHECK (std::fabs (lvl (l, f[k]) - ref) < 0.3, "solo %d band %d level %.2f", which, k, lvl (l, f[k]));
                else CHECK (lvl (l, f[k]) < ref - 60, "solo %d leaks band %d: %.1f dB", which, k, lvl (l, f[k]));
            }
        }
    }
    {   // remove low band => 100 Hz tone gets shifted; remove both => everything shifted
        Params p; p.xoLowHz = 400; p.xoHighHz = 6000; p.coarseHz = (float) dS; p.lowBand = false;
        std::unique_ptr<Engine> e (make (sr, p)); auto l = make3(), r = l; run (*e, l, r, 256);
        CHECK (std::fabs (lvl (l, fl + dS) - ref) < 0.3 && lvl (l, fl) < ref - 50, "low removed -> shifted with the middle");
        CHECK (std::fabs (lvl (l, fh) - ref) < 0.3, "high still untouched");
        p.highBand = false; e.reset (make (sr, p)); l = make3(); r = l; run (*e, l, r, 256);
        CHECK (std::fabs (lvl (l, fh + dS) - ref) < 0.3 && lvl (l, fh) < ref - 50 && std::fabs (lvl (l, fl + dS) - ref) < 0.3, "one band left shifts the full signal");
        // solo of a removed band is ignored
        p.soloLow = true; e.reset (make (sr, p)); l = make3(); r = l; run (*e, l, r, 256);
        CHECK (std::fabs (lvl (l, fh + dS) - ref) < 0.3, "solo on removed band ignored");
    }
    {   // slope: leakage of the shifted band one octave above / below the crossover gets smaller with a steeper slope
        double leak[3];
        for (int s = 0; s < 3; ++s)
        {
            Params p; p.xoLowHz = 1000; p.xoHighHz = 8000; p.slope = s; p.coarseHz = (float) snapHz (sr, 400.0); p.soloMid = true;
            const double f = binFreq (sr, 512);      // 750 Hz: just under the 1k crossover => mostly low band
            std::unique_ptr<Engine> e (make (sr, p)); auto l = sine (f, sr, n, 0.5), r = l; run (*e, l, r, 256);
            leak[s] = levelAt (l, sr, f + snapHz (sr, 400.0), 3.0);
        }
        std::printf ("  leak into the shifted band at 0.75x crossover: 24 dB %.1f, 48 dB %.1f, 96 dB %.1f\n", leak[0], leak[1], leak[2]);
        CHECK (leak[0] > leak[1] && leak[1] > leak[2], "steeper slope should leak less");
    }
}

static void testMixSpreadKey()
{
    std::printf ("Mix, spread, keytrack, feedback\n");
    const double sr = 48000.0; const int n = 1 << 16; const double f0 = binFreq (sr, 1365);
    Params base; base.lowBand = false; base.highBand = false;
    {   // mix
        Params p = base; p.coarseHz = (float) snapHz (sr, 250.0); p.mix = 0.5f; std::unique_ptr<Engine> e (make (sr, p)); auto l = sine (f0, sr, n), r = l; run (*e, l, r, 200);
        const double a = levelAt (l, sr, f0, 3), b = levelAt (l, sr, f0 + snapHz (sr, 250.0), 3); std::printf ("  mix 50%%: dry %.2f dB shifted %.2f dB\n", a, b);
        CHECK (std::fabs (a - b) < 0.2 && std::fabs (a - (-6.02 - 6.02)) < 0.3, "mix 50%%");
        p.mix = 0.0f; e.reset (make (sr, p)); l = sine (f0, sr, n); r = l; run (*e, l, r, 200);
        CHECK (levelAt (l, sr, f0 + snapHz (sr, 250.0), 3) < -100 && std::fabs (levelAt (l, sr, f0, 3) + 6.02) < 0.1, "mix 0 = dry");
        p.mix = 1.0f; p.shiftOn = false; e.reset (make (sr, p)); l = sine (f0, sr, n); r = l; run (*e, l, r, 200);
        CHECK (levelAt (l, sr, f0 + snapHz (sr, 250.0), 3) < -100 && std::fabs (levelAt (l, sr, f0, 3) + 6.02) < 0.1, "shift off = dry");
    }
    {   // spread (wide)
        Params p = base; p.spreadHz = 200; p.wide = true; std::unique_ptr<Engine> e (make (sr, p)); auto l = sine (f0, sr, n), r = l; run (*e, l, r, 200);
        const double pl = peakFreq (l, sr), pr = peakFreq (r, sr);
        std::printf ("  spread 200 Hz: L %.2f R %.2f (in %.2f)\n", pl, pr, f0);
        CHECK (std::fabs (pl - (f0 - 100)) < 1.5 && std::fabs (pr - (f0 + 100)) < 1.5, "L %f R %f", pl, pr);
        p.wide = false; e.reset (make (sr, p)); l = sine (f0, sr, n); r = l; run (*e, l, r, 200);
        CHECK (std::fabs (peakFreq (l, sr) - f0) < 1.5 && std::fabs (peakFreq (r, sr) - f0) < 1.5, "wide off: no spread");
        // mono instance: spread is ignored (centre)
        p.wide = true; e.reset (make (sr, p, 1)); l = sine (f0, sr, n);
        for (int i = 0; i < n; i += 256) { float* c[1] = { l.data() + i }; e->process (c, 1, 256); }
        CHECK (std::fabs (peakFreq (l, sr) - f0) < 1.5, "mono instance");
    }
    {   // keytrack: 10 Hz/semitone, note 72 -> +120 Hz, release -> back to 0
        Params p = base; p.keyOn = true; p.keyHzPerSemi = 10; p.mix = 1.0f; std::unique_ptr<Engine> e (make (sr, p));
        auto l = sine (f0, sr, n), r = l; e->noteOn (72); run (*e, l, r, 128);
        CHECK (std::fabs (peakFreq (l, sr) - (f0 + 120)) < 1.5, "key 72: %f", peakFreq (l, sr));
        e->noteOn (48); l = sine (f0, sr, n); r = l; run (*e, l, r, 128);
        CHECK (std::fabs (peakFreq (l, sr) - (f0 - 120)) < 1.5, "key 48 (last note wins): %f", peakFreq (l, sr));
        e->noteOff (48); l = sine (f0, sr, n); r = l; run (*e, l, r, 128);
        CHECK (std::fabs (peakFreq (l, sr) - (f0 + 120)) < 1.5, "back to held key 72: %f", peakFreq (l, sr));
        e->noteOff (72); l = sine (f0, sr, n); r = l; run (*e, l, r, 128);
        CHECK (std::fabs (peakFreq (l, sr) - f0) < 1.5, "released: %f", peakFreq (l, sr));
        p.keyOn = false; e->setParams (p); e->noteOn (84); l = sine (f0, sr, n); r = l; run (*e, l, r, 128);
        CHECK (std::fabs (peakFreq (l, sr) - f0) < 1.5, "keytrack off ignores notes");
    }
    {   // feedback: tone repeats at +k*shift, loud noise stays bounded
        Params p = base; p.coarseHz = 200; p.feedback = 0.6f; std::unique_ptr<Engine> e (make (sr, p));
        auto l = sine (f0, sr, n, 0.2), r = l; run (*e, l, r, 256);
        const double l1 = levelAt (l, sr, f0 + 200, 3), l2 = levelAt (l, sr, f0 + 400, 3), l3 = levelAt (l, sr, f0 + 600, 3);
        std::printf ("  feedback 0.6: +200 %.1f, +400 %.1f, +600 %.1f dB\n", l1, l2, l3);
        CHECK (l2 > l1 - 8.0 - 1 && l2 < l1 && l3 < l2 && l2 > -60, "feedback chain");
        p.feedback = 0.95f; e.reset (make (sr, p)); std::mt19937 rng (3); std::uniform_real_distribution<float> ud (-1.f, 1.f);
        std::vector<float> a ((size_t) sr * 6); for (auto& v : a) v = ud (rng); auto b = a; run (*e, a, b, 512);
        float mx = 0; bool finite = true; for (float v : a) { mx = std::max (mx, std::fabs (v)); finite &= std::isfinite (v); }
        std::printf ("  feedback 0.95 on full-scale noise: max |y| %.2f\n", mx);
        CHECK (finite && mx < 12.0f, "stable");
    }
}

static void testRobust()
{
    std::printf ("Robustness: block sizes, random automation, extreme settings\n");
    const double sr = 48000.0;
    {   // block size independence
        Params p; p.coarseHz = 777; p.xoLowHz = 300; p.xoHighHz = 3000; p.feedback = 0.3f; p.keyOn = true;
        std::mt19937 rng (5); std::normal_distribution<float> nd (0.f, 0.2f); std::vector<float> in (20000); for (auto& v : in) v = nd (rng);
        std::vector<float> ref;
        for (int block : { 1, 7, 64, 480, 4096, 20000 })
        {
            std::unique_ptr<Engine> e (make (sr, p)); e->noteOn (65); auto l = in, r = in; run (*e, l, r, block);
            if (ref.empty()) ref = l; double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::fabs (l[i] - ref[i]));
            CHECK (d < 1e-5, "block %d differs by %g", block, d);
        }
    }
    {   // random automation never produces NaN / explosions and never huge jumps
        std::mt19937 rng (9); std::uniform_real_distribution<float> u (0.f, 1.f);
        std::unique_ptr<Engine> e (make (sr, Params())); e->noteOn (60);
        std::normal_distribution<float> nd (0.f, 0.3f); float mx = 0; bool finite = true;
        for (int blk = 0; blk < 3000; ++blk)
        {
            Params p; p.coarseHz = -10000 + 20000 * u (rng); p.fineHz = -500 + 1000 * u (rng); p.shiftOn = u (rng) > 0.1; p.spreadHz = -500 + 1000 * u (rng); p.wide = u (rng) > 0.5;
            p.mix = u (rng); p.feedback = 0.95f * u (rng); p.keyOn = u (rng) > 0.5; p.keyHzPerSemi = -100 + 200 * u (rng);
            p.lowBand = u (rng) > 0.3; p.highBand = u (rng) > 0.3; p.xoLowHz = 40 + 7960 * u (rng); p.xoHighHz = 80 + 15920 * u (rng); p.slope = (int) (u (rng) * 2.999f);
            p.soloLow = u (rng) > 0.8; p.soloMid = u (rng) > 0.8; p.soloHigh = u (rng) > 0.8; e->setParams (p);
            if (u (rng) > 0.9) e->noteOn ((int) (u (rng) * 127)); if (u (rng) > 0.9) e->noteOff ((int) (u (rng) * 127));
            std::vector<float> a (64), b (64); for (auto& v : a) v = nd (rng); b = a; float* c[2] = { a.data(), b.data() }; e->process (c, 2, 64);
            for (float v : a) { finite &= std::isfinite (v); mx = std::max (mx, std::fabs (v)); }
        }
        CHECK (finite && mx < 20.f, "random automation: finite %d max %g", (int) finite, mx);
    }
    {   // no clicks when a band is switched / shift toggled while a tone is playing
        const double f = binFreq (sr, 1365); Params p; p.xoLowHz = 400; p.xoHighHz = 6000; p.coarseHz = 0; std::unique_ptr<Engine> e (make (sr, p));
        std::vector<float> l = sine (f, sr, 48000, 0.5), r = l; double maxStep = 0; float prev = 0;
        for (int i = 0; i < 48000; i += 64)
        {
            if (i == 12800) { p.lowBand = false; e->setParams (p); } if (i == 19200) { p.shiftOn = false; e->setParams (p); } if (i == 25600) { p.shiftOn = true; p.coarseHz = 100; e->setParams (p); }
            if (i == 32000) { p.highBand = false; e->setParams (p); } if (i == 38400) { p.soloMid = true; e->setParams (p); }
            float* c[2] = { l.data() + i, r.data() + i }; e->process (c, 2, 64);
        }
        prev = l[3999]; for (size_t i = 4000; i < l.size(); ++i) { maxStep = std::max (maxStep, (double) std::fabs (l[i] - prev)); prev = l[i]; }
        std::printf ("  largest sample-to-sample step during automation of a 0.5 tone at 2 kHz: %.3f (a clean sine: %.3f)\n", maxStep, 0.5 * 2 * kPi * f / sr);
        CHECK (maxStep < 0.5 * 2 * kPi * f / sr * 1.8, "click detected: %g", maxStep);
    }
    {   // silence in, silence out; denormals die
        std::unique_ptr<Engine> e (make (sr, Params())); std::vector<float> a (48000, 0.0f), b = a; run (*e, a, b, 256);
        CHECK (rms (a) == 0.0, "silence");
    }
}

static void testSpectrum()
{
    std::printf ("Spectrum analyser and sample tap\n");
    const double sr = 48000.0; SpectrumAnalyzer a; a.init (12);
    auto x = sine (binFreq (sr, 300, 12), sr, 4096, 1.0); std::vector<float> db ((size_t) a.bins()); a.analyse (x.data(), db.data());
    int pk = 0; for (int i = 1; i < a.bins(); ++i) if (db[(size_t) i] > db[(size_t) pk]) pk = i;
    CHECK (pk == 300, "bin %d", pk); CHECK (std::fabs (db[300] - 0.0f) < 0.6f, "full scale sine = 0 dB, got %f", db[300]);
    Engine e; e.prepare (sr, 2); e.setDisplayActive (true); auto l = sine (1000, sr, 4096), r = l; run (e, l, r, 256);
    std::vector<float> t (2048); CHECK (e.inTap().latest (t.data(), 2048), "tap fills"); CHECK (std::fabs (t[100] - (float) (0.5 * std::sin (2 * kPi * 1000 * (4096 - 2048 + 100) / sr))) < 1e-5f, "tap content");
    Engine e2; e2.prepare (sr, 2); auto l2 = sine (1000, sr, 4096), r2 = l2; run (e2, l2, r2, 256); CHECK (! e2.inTap().latest (t.data(), 16), "tap idle when no display");
}

static void testCpu()
{
    const double sr = 48000.0; Params p; p.coarseHz = 300; p.slope = Slope96; p.feedback = 0.4f; Engine e; e.prepare (sr, 2); e.setParams (p); e.setDisplayActive (true);
    std::vector<float> a (512, 0.1f), b (512, 0.1f); const int blocks = (int) (sr * 20 / 512);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) { for (int k = 0; k < 512; ++k) a[(size_t) k] = b[(size_t) k] = 0.3f * std::sin (0.05f * (float) (i * 512 + k)); float* c[2] = { a.data(), b.data() }; e.process (c, 2, 512); }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("CPU (this box, -O2, stereo, 96 dB slope, feedback, display tap on, 48 kHz): %.0fx real time (%.2f %% of one core)\n", 20.0 / sec, 100.0 * sec / 20.0);
    CHECK (sec < 20.0 / 20.0, "too slow");
}

int main()
{
    testHilbert(); testShiftAccuracy(); testTransparent(); testBands(); testMixSpreadKey(); testRobust(); testSpectrum(); testCpu();
    std::printf ("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
