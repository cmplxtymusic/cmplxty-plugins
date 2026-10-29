// Mix Tools DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/MixToolsDSP.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
using namespace mt::dsp;
static int fails = 0;
static void check (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; }
static const double SR = 48000.0;
using Buf = std::vector<float>;
static void run (Engine& e, Buf& l, Buf& r, int block = 256)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block) { float* c[2] = { l.data() + i, r.data() + i }; e.process (c, (int) std::min ((size_t) block, l.size() - i)); }
}
static Buf sine (double sec, double hz, float amp, double ph = 0) { Buf x ((size_t) (sec * SR)); for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float) std::sin (2 * kPi * hz * (double) i / SR + ph); return x; }
static Buf noise (double sec, float amp, unsigned seed) { std::mt19937 g (seed); std::normal_distribution<float> d (0, amp); Buf x ((size_t) (sec * SR)); for (auto& v : x) v = d (g); return x; }
static double rms (const Buf& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) (b - a) + 1e-30); }
static double peak (const Buf& x, size_t a = 0) { double p = 0; for (size_t i = a; i < x.size(); ++i) p = std::max (p, (double) std::fabs (x[i])); return p; }
static bool finite (const Buf& x) { for (auto v : x) if (! std::isfinite (v)) return false; return true; }
static double truePeak (const Buf& x)   // 16x sinc reference measure
{
    double pk = peak (x);
    for (size_t n = 32; n + 32 < x.size(); ++n)
        for (int f = 1; f < 16; ++f)
        {
            const double t = f / 16.0; double s = 0;
            for (int k = -31; k <= 32; ++k) { const double d = (double) k - t; const double w = 0.5 + 0.5 * std::cos (kPi * d / 33.0); s += x[n + (size_t) k] * std::sin (kPi * d) / (kPi * d) * w; }
            pk = std::max (pk, std::fabs (s));
        }
    return pk;
}
// program-like noise: band-limited to ~16.8 kHz (windowed-sinc lowpass)
static Buf lpNoise (double sec, float amp, unsigned seed)
{
    const Buf w = noise (sec, amp, seed); Buf x (w.size(), 0.0f);
    for (size_t i = 20; i + 20 < x.size(); ++i)
    {
        double s = 0;
        for (int k = -20; k <= 20; ++k) { const double t = k * 0.7 * kPi; s += w[i + (size_t) k] * 0.7 * (k ? std::sin (t) / t : 1.0) * (0.5 + 0.5 * std::cos (kPi * k / 21.0)); }
        x[i] = (float) s * 1.4f;
    }
    return x;
}
static Params off() { Params p; p.wOn = p.tOn = p.lOn = false; return p; }
// drum-like test: decaying 200 Hz hits every 0.25 s
static Buf hits (double sec, float amp, double decayMs = 120)
{
    Buf x ((size_t) (sec * SR));
    for (size_t i = 0; i < x.size(); ++i) { const double t = std::fmod ((double) i / SR, 0.25); x[i] = amp * (float) (std::exp (-t * 1000.0 / decayMs) * std::sin (2 * kPi * 200 * t)); }
    return x;
}

int main()
{
    { Engine e; e.setParams (off()); e.prepare (SR, 2); const int lat = e.latency();
      Buf l = noise (1.0, 0.2f, 1), r = noise (1.0, 0.2f, 2), L0 = l, R0 = r; run (e, l, r);
      double err = 0; for (size_t i = (size_t) lat; i < l.size(); ++i) err = std::max (err, (double) std::max (std::fabs (l[i] - L0[i - (size_t) lat]), std::fabs (r[i] - R0[i - (size_t) lat])));
      std::printf ("      latency %d samples, max err %.2e\n", lat, err);
      check (lat > 0 && lat < 200 && err < 1e-6, "all modules off: exact pass-through delayed by the reported latency"); }
    { Engine e; Params p = off(); p.lOn = true; e.setParams (p); e.prepare (SR, 2);   // also: limiter on but quiet signal is transparent
      Buf l = sine (1.0, 300, 0.3f), r = l, L0 = l; run (e, l, r); const int lat = e.latency();
      double err = 0; for (size_t i = (size_t) lat; i < l.size(); ++i) err = std::max (err, (double) std::fabs (l[i] - L0[i - (size_t) lat]));
      check (err < 1e-5, "limiter under the ceiling is bit-transparent"); }
    { Engine e; Params p = off(); p.wOn = true; p.width = 0.0f; e.setParams (p); e.prepare (SR, 2);
      Buf l = sine (1.0, 440, 0.3f), r = sine (1.0, 660, 0.3f); run (e, l, r);
      double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::fabs (l[i] - r[i]));
      check (d < 1e-6, "width 0 % = mono (L == R)"); }
    { Engine e; Params p = off(); p.wOn = true; p.width = 2.0f; e.setParams (p); e.prepare (SR, 2);
      Buf l = noise (1.0, 0.1f, 3), r = l; for (size_t i = 0; i < l.size(); ++i) r[i] = 0.5f * l[i] + 0.5f * noise (1.0, 0.1f, 4)[0] * 0 + 0.5f * (float) std::sin ((double) i);
      Buf l2 = noise (1.0, 0.1f, 3), r2 = noise (1.0, 0.1f, 5); for (size_t i = 0; i < l2.size(); ++i) r2[i] = 0.7f * l2[i] + 0.3f * r2[i];
      Buf sl = l2, sr2 = r2; double s0 = 0, m0 = 0; for (size_t i = 0; i < l2.size(); ++i) { s0 += std::pow (0.5 * (l2[i] - r2[i]), 2); m0 += std::pow (0.5 * (l2[i] + r2[i]), 2); }
      run (e, sl, sr2); const int lat = e.latency(); double s1 = 0, m1 = 0;
      for (size_t i = (size_t) lat; i < sl.size(); ++i) { s1 += std::pow (0.5 * (sl[i] - sr2[i]), 2); m1 += std::pow (0.5 * (sl[i] + sr2[i]), 2); }
      const double sRatio = std::sqrt (s1 / s0), mRatio = std::sqrt (m1 / m0);
      std::printf ("      width 200%%: side x%.2f mid x%.2f, corr %.2f\n", sRatio, mRatio, e.corr.load());
      check (sRatio > 1.9 && sRatio < 2.1 && mRatio > 0.98 && mRatio < 1.02, "width 200 %: side doubled, mid untouched"); (void) l; (void) r; }
    { Engine e; Params p = off(); p.wOn = true; p.width = 1.5f; p.monoHz = 150; e.setParams (p); e.prepare (SR, 2);
      Buf l = sine (2.0, 50, 0.3f), r = sine (2.0, 50, 0.3f, kPi);   // pure side at 50 Hz
      run (e, l, r); double side = 0; for (size_t i = 48000; i < l.size(); ++i) side = std::max (side, (double) std::fabs (l[i] - r[i]) * 0.5);
      Engine e2; e2.setParams (p); e2.prepare (SR, 2); Buf l2 = sine (2.0, 3000, 0.3f), r2 = sine (2.0, 3000, 0.3f, kPi); run (e2, l2, r2);
      double side2 = 0; for (size_t i = 48000; i < l2.size(); ++i) side2 = std::max (side2, (double) std::fabs (l2[i] - r2[i]) * 0.5);
      std::printf ("      mono bass 150 Hz: 50 Hz side %.3f, 3 kHz side %.3f\n", side, side2);
      check (side < 0.12 && side2 > 0.4, "mono bass: lows below the corner made mono, highs still widened"); }
    { Engine e; Params p = off(); p.wOn = true; e.setParams (p); e.prepare (SR, 2);
      Buf l = sine (1.0, 300, 0.3f), r = sine (1.0, 300, 0.3f, kPi); run (e, l, r); const float c1 = e.corr.load();
      Engine e2; e2.setParams (p); e2.prepare (SR, 2); Buf a = sine (1.0, 300, 0.3f), b = a; run (e2, a, b);
      check (c1 < -0.9f && e2.corr.load() > 0.9f, "correlation meter: +1 for mono, -1 for out of phase"); }
    // transient shaper
    auto transientRun = [] (float att, float sus, double& attackPk, double& tail)
    {
        Engine e; Params p = off(); p.tOn = true; p.attack = att; p.sustain = sus; e.setParams (p); e.prepare (SR, 2);
        Buf l = hits (2.0, 0.4f), r = l; run (e, l, r); const int lat = e.latency();
        attackPk = 0; tail = 0;
        for (int h = 4; h < 8; ++h)
        {
            const size_t s = (size_t) (h * 0.25 * SR) + (size_t) lat;
            for (size_t i = s; i < s + 480; ++i) attackPk = std::max (attackPk, (double) std::fabs (l[i]));
            tail += rms (l, s + 4800, s + 9600);
        }
        return finite (l);
    };
    { double a0, t0, a1, t1, a2, t2, a3, t3;
      bool f = transientRun (0, 0, a0, t0) & transientRun (1, 0, a1, t1) & transientRun (-1, 0, a2, t2) & transientRun (0, 1, a3, t3);
      double a4, t4; f &= transientRun (0, -1, a4, t4);
      std::printf ("      attack pk: 0 %.3f +1 %.3f -1 %.3f | tail: 0 %.4f sus+1 %.4f sus-1 %.4f\n", a0, a1, a2, t0, t3, t4);
      check (f, "transient shaper output finite");
      check (a1 > a0 * 1.4, "attack +100 %: hits get louder at the onset");
      check (a2 < a0 * 0.75, "attack -100 %: hits get softer at the onset");
      check (t3 > t0 * 1.4, "sustain +100 %: tails get louder");
      check (t4 < t0 * 0.75, "sustain -100 %: tails get quieter"); }
    { Engine e; Params p = off(); p.tOn = true; p.attack = 1; p.sustain = 1; e.setParams (p); e.prepare (SR, 2);
      Buf l = sine (2.0, 300, 0.3f), r = l; run (e, l, r);
      check (std::fabs (20 * std::log10 (rms (l, 48000, 96000) / (0.3 / std::sqrt (2.0)))) < 1.5, "steady tone: shaper leaves the level alone (within 1.5 dB)"); }
    // limiter
    for (float ceil : { -1.0f, -0.1f, -6.0f })
    {
        Engine e; Params p = off(); p.lOn = true; p.driveDb = 12; p.ceilingDb = ceil; e.setParams (p); e.prepare (SR, 2);
        Buf l = lpNoise (3.0, 0.3f, 7), r = lpNoise (3.0, 0.3f, 8);
        for (size_t i = 0; i < l.size(); ++i) { l[i] += 0.6f * (float) std::sin (2 * kPi * 11025.0 * (double) i / SR + 0.785); }   // fs/4 + 45 deg: inter-sample overs
        run (e, l, r);
        const double tpk = std::max (truePeak (l), truePeak (r)), limit = std::pow (10.0, ceil / 20.0);
        char msg[160]; std::snprintf (msg, sizeof msg, "true-peak limiter at %.1f dBTP: +12 dB drive, measured %.2f dBTP (sample pk %.2f dB)", ceil, 20 * std::log10 (tpk), 20 * std::log10 (peak (l)));
        check (finite (l) && tpk <= limit * 1.03, msg);   // within 0.25 dB of the ceiling, measured with a 16x reference
        if (ceil == -1.0f) check (e.grDb.load() < -3.0f, "gain-reduction meter shows the reduction");
    }
    { Engine e; Params p = off(); p.lOn = true; p.ceilingDb = -1; p.releaseMs = 50; e.setParams (p); e.prepare (SR, 2);
      Buf l (SR * 2, 0.0f); for (size_t i = 24000; i < 24010; ++i) l[i] = 1.0f; for (size_t i = 24010; i < l.size(); ++i) l[i] = 0.1f * (float) std::sin ((double) i * 0.05);
      Buf r = l; run (e, l, r); const int lat = e.latency();
      const double later = rms (l, 24000 + (size_t) lat + 24000, 24000 + (size_t) lat + 48000);
      check (peak (l) <= std::pow (10.0, -1 / 20.0) * 1.001 && later > 0.1 / std::sqrt (2.0) * 0.95, "spike limited, gain recovers after release"); }
    { // order: shape-first vs widen-first both stable; extremes / denormals / NaN
      for (int o = 0; o < 2; ++o)
      {
          Engine e; Params p; p.order = (Order) o; p.width = 2; p.monoHz = 500; p.attack = 1; p.sustain = 1; p.driveDb = 24; p.gainDb = 12; p.ceilingDb = 0;
          e.setParams (p); e.prepare (96000.0, 2);
          Buf l = noise (1.0, 1.0f, 11), r = noise (1.0, 1.0f, 12);
          l[100] = std::nanf (""); r[200] = INFINITY; l[300] = 1e-40f; for (size_t i = 1000; i < 20000; ++i) l[i] = r[i] = 1e-39f;
          run (e, l, r, 64);
          check (finite (l) && finite (r) && peak (l) < 4.1, o == 0 ? "extreme settings, NaN / inf / denormal input: finite + bounded (widen first)" : "same (shape first)");
      }
    }
    { // CPU
      Engine e; Params p; p.width = 1.5f; p.monoHz = 120; p.attack = 0.5f; p.driveDb = 6; e.setParams (p); e.prepare (SR, 2);
      Buf l = noise (20.0, 0.3f, 21), r = noise (20.0, 0.3f, 22);
      const auto t0 = std::chrono::steady_clock::now(); run (e, l, r, 512);
      const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
      std::printf ("      20 s stereo in %.3f s (%.0fx realtime)\n", s, 20.0 / s);
      check (20.0 / s > 50, "all three modules run > 50x realtime"); }
    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILED\n", fails);
    return fails;
}
