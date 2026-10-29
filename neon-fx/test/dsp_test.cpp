// Neon FX DSP tests (no JUCE): bypass, trance gate + host sync, drive modes, filters + formant + LFO, stutter, tape stop, DJ filter,
// mix / output, click-free switching, fuzz at 3 rates, speed.
#include "../src/NeonDSP.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace cw::dsp;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what); if (! ok) ++fails; }

struct Sig { std::vector<float> L, R; };
static Sig sine (double f, double sr, int n, float a = 0.5f) { Sig s; for (int i = 0; i < n; ++i) { const float v = a * (float) std::sin (2 * kPi * f * i / sr); s.L.push_back (v); s.R.push_back (v); } return s; }
static Sig noise (int n, unsigned seed = 1) { std::mt19937 r (seed); std::uniform_real_distribution<float> u (-0.5f, 0.5f); Sig s; for (int i = 0; i < n; ++i) { s.L.push_back (u (r)); s.R.push_back (u (r)); } return s; }
static Sig run (Engine& e, Sig s, Transport& t, double sr, int block = 256)
{
    const int n = (int) s.L.size();
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        e.render (s.L.data() + i, s.R.data() + i, m, t);
        if (t.playing) t.ppq += m * t.bpm / 60.0 / sr;
    }
    return s;
}
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return b > a ? std::sqrt (s / (double) (b - a)) : 0; }
static float peak (const std::vector<float>& x, size_t a = 0, size_t b = ~(size_t) 0) { float p = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) p = std::max (p, std::fabs (x[i])); return p; }
static double freq (const std::vector<float>& x, size_t a, size_t b, double sr)
{
    int n = 0; double first = -1, last = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (n - 1) * sr / (last - first) : 0.0;
}
static double db (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }

int main()
{
    const double sr = 48000;
    Engine e; e.prepare (sr);
    Transport play { 120.0, 0.0, true };
    {   // bypass
        Params P; e.setParams (P); e.reset(); auto in = noise (48000); Transport t = play; auto o = run (e, in, t, sr);
        float d = 0; for (size_t i = 0; i < in.L.size(); ++i) d = std::max ({ d, std::fabs (o.L[i] - in.L[i]), std::fabs (o.R[i] - in.R[i]) });
        check (d == 0.0f, "everything off = bit-exact bypass");
    }
    {   // trance gate
        Params P; P.gtOn = true; P.gtPattern = 3; P.gtRate = 1; P.gtDepth = 1; P.gtSmooth = 1; e.setParams (P); e.reset();
        Transport t = play; auto o = run (e, sine (440, sr, 48000), t, sr);  // a 1/16 at 120 BPM = 6000 samples. pattern 1011 0110 ...
        const double on0 = rms (o.L, 1000, 4000), off1 = rms (o.L, 7000, 11000), on2 = rms (o.L, 13000, 16000), on3 = rms (o.L, 19000, 22000), off4 = rms (o.L, 25000, 29000);
        std::printf ("        gate rms: %.3f %.4f %.3f %.3f %.4f\n", on0, off1, on2, on3, off4);
        check (on0 > 0.33 && on2 > 0.33 && on3 > 0.33, "gate: on steps pass");
        check (off1 < 0.003 && off4 < 0.003, "gate: off steps are silent at full depth");
        check (rms (o.L, 4700, 5700) < 0.01, "gate: an on step is chopped after 72% (duty)");
        P.gtDepth = 0.5f; e.setParams (P); e.reset(); t = play; o = run (e, sine (440, sr, 24000), t, sr);
        check (std::abs (rms (o.L, 7000, 11000) / on0 - 0.5) < 0.03, "gate depth 50% leaves half the level");
        P.gtDepth = 1; e.setParams (P); e.reset(); t = play; t.ppq = 0.25; o = run (e, sine (440, sr, 4800), t, sr);
        check (rms (o.L, 3000, 4800) < 0.003, "gate follows the host position (starting at step 2 = off)");
        P.intensity = 1; P.gtDepth = 0; e.setParams (P); e.reset(); t = play; t.ppq = 0.25; o = run (e, sine (440, sr, 4800), t, sr);
        check (rms (o.L, 3000, 4800) < 0.003, "intensity macro pushes gate depth to 100%");
        P.intensity = 0; P.gtDepth = 1; e.setParams (P); e.reset(); Transport stop { 120.0, 0.0, false }; o = run (e, sine (440, sr, 24000), stop, sr);
        check (rms (o.L, 7000, 11000) < 0.003 && rms (o.L, 1000, 4000) > 0.33, "gate free-runs at the tempo when the transport is stopped");
    }
    {   // drive
        Params P; P.drOn = true; P.drDb = 24; P.drTone = 20000; e.setParams (P); e.reset(); Transport t = play;
        auto o = run (e, sine (220, sr, 24000), t, sr);
        const double crest = peak (o.L, 4000) / rms (o.L, 4000, 24000);
        check (peak (o.L, 4000) <= 1.0f && crest < 1.25, "soft drive saturates (crest factor drops from 1.41)");
        P.drMode = 1; e.setParams (P); e.reset(); o = run (e, sine (220, sr, 24000), t, sr); check (peak (o.L, 4000) <= 0.4f && peak (o.L, 4000) / rms (o.L, 4000, 24000) < 1.1, "hard clip squares it, with level compensation");
        P.drMode = 2; e.setParams (P); e.reset(); o = run (e, sine (220, sr, 24000), t, sr);
        { bool fin = true; for (float v : o.L) fin = fin && std::isfinite (v); check (fin && peak (o.L) <= 0.4f && freq (o.L, 4000, 24000, sr) > 400, "fold adds overtones (many more zero crossings)"); }
        P.drMode = 3; P.drDb = 36; e.setParams (P); e.reset(); o = run (e, sine (220, sr, 24000), t, sr);
        {   std::vector<float> v (o.L.begin() + 4000, o.L.end()); int changes = 0, holdMax = 0, run1 = 0;
            for (size_t i = 1; i < v.size(); ++i) { if (v[i] != v[i - 1]) { ++changes; holdMax = std::max (holdMax, run1); run1 = 0; } else ++run1; }
            for (auto& z : v) z = std::round (z * 1e4f) / 1e4f; std::sort (v.begin(), v.end()); const auto u = std::unique (v.begin(), v.end()) - v.begin();
            std::printf ("        crush: %d levels, hold up to %d\n", (int) u, holdMax);
            check (u <= 17 && holdMax >= 6, "crush at max = 4 bits + sample-and-hold");
        }
        P.drMode = 0; P.drDb = 24; P.drTone = 1000; e.setParams (P); e.reset(); auto a = run (e, sine (8000, sr, 24000), t, sr);
        P.drTone = 20000; e.setParams (P); e.reset(); auto b = run (e, sine (8000, sr, 24000), t, sr);
        check (rms (a.L, 4000, 24000) < rms (b.L, 4000, 24000) * 0.3, "drive tone darkens");
    }
    {   // filters
        Params P; P.flOn = true; P.flMode = 0; P.flCut = 300; P.flReso = 0.1f; e.setParams (P); e.reset(); Transport t = play;
        auto hi = run (e, sine (6000, sr, 24000), t, sr); e.reset(); auto lo = run (e, sine (60, sr, 24000), t, sr);
        check (db (rms (hi.L, 8000, 24000) / 0.3536) < -40 && std::abs (db (rms (lo.L, 8000, 24000) / 0.3536)) < 1.0, "low-pass: 6k cut > 40 dB, 60 Hz passes");
        P.flMode = 1; e.setParams (P); e.reset(); hi = run (e, sine (6000, sr, 24000), t, sr); e.reset(); lo = run (e, sine (30, sr, 24000), t, sr);
        check (std::abs (db (rms (hi.L, 8000, 24000) / 0.3536)) < 1.0 && db (rms (lo.L, 8000, 24000) / 0.3536) < -15, "high-pass: 6k passes, 30 Hz cut");
        P.flMode = 2; P.flCut = 1000; P.flReso = 0.8f; e.setParams (P); e.reset(); auto c = run (e, sine (1000, sr, 24000), t, sr); e.reset(); auto f = run (e, sine (8000, sr, 24000), t, sr);
        check (rms (c.L, 8000, 24000) > 0.3 && db (rms (f.L, 8000, 24000) / 0.3536) < -20, "band-pass: passes the centre, cuts 3 octaves up");
        P.flMode = 3; P.flReso = 0.4f; P.flVowel = 0.0f; e.setParams (P); e.reset(); auto A = run (e, sine (730, sr, 24000), t, sr);
        P.flVowel = 0.5f; e.setParams (P); e.reset(); auto I = run (e, sine (730, sr, 24000), t, sr);
        std::printf ("        formant 730 Hz: A %.3f  I %.3f\n", rms (A.L, 8000, 24000), rms (I.L, 8000, 24000));
        check (rms (A.L, 8000, 24000) > 3 * rms (I.L, 8000, 24000), "formant: 'A' resonates at 730 Hz, 'I' does not");
        P.flMode = 0; P.flCut = 1000; P.flLfoDepth = 1; P.flLfoRate = 4; e.setParams (P); e.reset(); t = play;   // 1/4 LFO = 24000 samples
        auto m = run (e, sine (2000, sr, 48000), t, sr);
        double mn = 9, mx = 0; for (int k = 0; k < 16; ++k) { const double r = rms (m.L, (size_t) (24000 + k * 1500), (size_t) (24000 + k * 1500 + 1500)); mn = std::min (mn, r); mx = std::max (mx, r); }
        check (mx > 4 * mn, "tempo LFO sweeps the cutoff");
        P.flLfoDepth = 0; P.flCut = 200; P.flReso = 0.1f; P.intensity = 0; e.setParams (P); e.reset(); t = play; auto plain = run (e, sine (800, sr, 24000), t, sr);
        P.flCut = 200; P.flReso = 0.97f; e.setParams (P); e.reset(); auto res = run (e, sine (200, sr, 24000), t, sr);
        check (rms (res.L, 8000, 24000) > 3 * 0.3536 && peak (res.L) < 20.0f, "high resonance rings but stays bounded");
        (void) plain;
    }
    {   // stutter
        Params P; e.setParams (P); e.reset(); Transport t = play;
        auto in = noise (60000, 7); auto o = in;
        o = in; Sig part; part.L.assign (in.L.begin(), in.L.begin() + 4800); part.R.assign (in.R.begin(), in.R.begin() + 4800);
        auto o1 = run (e, part, t, sr);
        P.stOn = true; P.stLen = 3; e.setParams (P);  // 1/16 = 6000 samples, captured from sample 4800
        Sig rest; rest.L.assign (in.L.begin() + 4800, in.L.begin() + 40800); rest.R.assign (in.R.begin() + 4800, in.R.begin() + 40800);
        auto o2 = run (e, rest, t, sr);
        double d1 = 0; for (int i = 0; i < 6000; ++i) d1 = std::max (d1, (double) std::fabs (o2.L[(size_t) i] - in.L[(size_t) (4800 + i)]));
        check (d1 == 0.0, "stutter: live audio passes while the slice is captured");
        double d2 = 0; for (int i = 100; i < 5900; ++i) d2 = std::max (d2, (double) std::fabs (o2.L[(size_t) (6000 + 6000 * 2 + i)] - in.L[(size_t) (4800 + i)]));
        std::printf ("        d1 %.6f d2 %.6f\n", d1, d2); check (d2 < 1e-5, "stutter: the slice repeats exactly (third repeat = captured audio)");
        P.stOn = false; e.setParams (P);
        Sig back; back.L.assign (in.L.begin() + 40800, in.L.begin() + 50000); back.R.assign (in.R.begin() + 40800, in.R.begin() + 50000);
        auto o3 = run (e, back, t, sr);
        double d3 = 0; for (int i = 400; i < 9000; ++i) d3 = std::max (d3, (double) std::fabs (o3.L[(size_t) i] - in.L[(size_t) (40800 + i)]));
        { int fi=-1; for (int i = 0; i < 9000; ++i) if (o3.L[(size_t) i] != in.L[(size_t) (40800 + i)]) fi = i; std::printf ("        d3 %.6f last diff at %d\n", d3, fi); } check (d3 == 0.0, "stutter: release returns to live within 8 ms");
        // decay + gate
        P.stOn = false; P.stDecay = 0.5f; P.stGate = 0.5f; e.setParams (P); e.reset(); t = play;
        auto s = sine (500, sr, 60000); Sig h; h.L.assign (s.L.begin(), s.L.begin() + 256); h.R = h.L; run (e, h, t, sr);
        P.stOn = true; e.setParams (P); Sig r2; r2.L.assign (s.L.begin() + 256, s.L.begin() + 30256); r2.R = r2.L; auto q = run (e, r2, t, sr);
        const double rep1 = rms (q.L, 6000 + 200, 6000 + 2800), rep2 = rms (q.L, 12000 + 200, 12000 + 2800), gap = rms (q.L, 6000 + 3200, 6000 + 5800);
        std::printf ("        stutter decay: %.3f -> %.3f, gate gap %.5f\n", rep1, rep2, gap);
        check (std::abs (rep2 / rep1 - 0.5) < 0.03, "stutter decay 50%: each repeat is half the last");
        check (gap < 1e-4, "stutter gate 50%: second half of each repeat is silent");
        // click-free release on a low sine
        P.stDecay = 0; P.stGate = 1; P.stOn = false; e.setParams (P); e.reset(); t = play;
        auto lo = sine (100, sr, 30000); Sig a; a.L.assign (lo.L.begin(), lo.L.begin() + 3000); a.R = a.L; auto x1 = run (e, a, t, sr);
        P.stOn = true; e.setParams (P); Sig b; b.L.assign (lo.L.begin() + 3000, lo.L.begin() + 20000); b.R = b.L; auto x2 = run (e, b, t, sr);
        P.stOn = false; e.setParams (P); Sig c; c.L.assign (lo.L.begin() + 20000, lo.L.end()); c.R = c.L; auto x3 = run (e, c, t, sr);
        std::vector<float> all = x1.L; all.insert (all.end(), x2.L.begin(), x2.L.end()); all.insert (all.end(), x3.L.begin(), x3.L.end());
        float jump = 0; for (size_t i = 1; i < all.size(); ++i) jump = std::max (jump, std::fabs (all[i] - all[i - 1]));
        std::printf ("        stutter max step %.4f (sine alone 0.0065)\n", jump);
        check (jump < 0.06f, "stutter engage / loop / release do not click");
    }
    {   // tape stop
        Params P; P.tsStop = 500; P.tsStart = 300; e.setParams (P); e.reset(); Transport t = play;
        auto s = sine (1000, sr, 200000, 0.5f);
        Sig a; a.L.assign (s.L.begin(), s.L.begin() + 4800); a.R = a.L; run (e, a, t, sr);
        P.tsOn = true; e.setParams (P);
        Sig b; b.L.assign (s.L.begin() + 4800, s.L.begin() + 52800); b.R = b.L; auto o = run (e, b, t, sr);
        const double f0 = freq (o.L, 0, 2400, sr), f1 = freq (o.L, 12000, 14400, sr);
        std::printf ("        tape stop pitch: %.0f Hz -> %.0f Hz\n", f0, f1);
        check (f0 > 900 && f1 < 0.6 * f0 && f1 > 0, "tape stop: pitch falls as the tape slows");
        check (rms (o.L, 25000, 48000) < 1e-4 && e.speedNow.load() == 0.0f, "tape stop: silent once stopped");
        P.tsOn = false; e.setParams (P);
        Sig c; c.L.assign (s.L.begin() + 52800, s.L.begin() + 92800); c.R = c.L; auto o2 = run (e, c, t, sr);
        const double fu = freq (o2.L, 4000, 6000, sr);
        double d = 0; for (int i = 22000; i < 40000; ++i) d = std::max (d, (double) std::fabs (o2.L[(size_t) i] - s.L[(size_t) (52800 + i)]));
        std::printf ("        spin-up pitch %.0f Hz; live diff after %.4f; lag %.1f\n", fu, d, e.currentLag());
        check (fu > 50 && fu < 900, "tape start: spins up from low pitch");
        check (d < 1e-6 && e.currentLag() == 0.0, "tape start: back to live (no residual delay)");
        // held 15 s: lag capped, restart ok
        P.tsOn = true; e.setParams (P); Sig l; l.L.assign (48000 * 15, 0.3f); l.R = l.L; run (e, l, t, sr);
        P.tsOn = false; P.tsStart = 0; e.setParams (P); auto o3 = run (e, sine (1000, sr, 9600), t, sr); auto ref = sine (1000, sr, 9600);
        double d4 = 0; for (int i = 2000; i < 9600; ++i) d4 = std::max (d4, (double) std::fabs (o3.L[(size_t) i] - ref.L[(size_t) i]));
        check (d4 < 1e-6, "held for 15 s, instant restart (start 0) returns to live");
    }
    {   // DJ filter, mix, output
        Params P; P.dj = -0.8f; e.setParams (P); e.reset(); Transport t = play;
        auto hi = run (e, sine (5000, sr, 24000), t, sr); check (db (rms (hi.L, 8000, 24000) / 0.3536) < -30, "DJ filter left = low-pass");
        P.dj = 0.8f; e.setParams (P); e.reset(); auto lo = run (e, sine (100, sr, 24000), t, sr); check (db (rms (lo.L, 8000, 24000) / 0.3536) < -30, "DJ filter right = high-pass");
        P.dj = 0.0f; e.setParams (P); auto back = run (e, sine (100, sr, 24000), t, sr); auto ref = sine (100, sr, 24000);
        double d = 0; for (int i = 4000; i < 24000; ++i) d = std::max (d, (double) std::fabs (back.L[(size_t) i] - ref.L[(size_t) i]));
        check (d < 1e-6, "DJ filter centre = bypass again (after its fade)");
        P.drOn = true; P.drDb = 30; P.mix = 0; e.setParams (P); e.reset(); auto in = noise (24000); auto o = run (e, in, t, sr);
        d = 0; for (int i = 0; i < 24000; ++i) d = std::max (d, (double) std::fabs (o.L[(size_t) i] - in.L[(size_t) i]));
        check (d < 1e-6, "mix 0 = dry");
        P.drOn = false; P.mix = 1; P.outDb = -6.0206f; e.setParams (P); e.reset(); o = run (e, in, t, sr);
        check (std::abs (o.L[20000] / in.L[20000] - 0.5f) < 1e-3, "output -6 dB = half");
    }
    {   // switching drive / filter on mid-signal does not click
        Params P; e.setParams (P); e.reset(); Transport t = play;
        auto s = sine (100, sr, 24000); Sig a; a.L.assign (s.L.begin(), s.L.begin() + 12000); a.R = a.L; auto x1 = run (e, a, t, sr);
        P.flOn = true; P.flMode = 1; P.flCut = 2000; P.gtOn = true; e.setParams (P); Sig b; b.L.assign (s.L.begin() + 12000, s.L.end()); b.R = b.L; auto x2 = run (e, b, t, sr);
        float j = 0; for (size_t i = 1; i < 3000; ++i) j = std::max (j, std::fabs (x2.L[i] - x2.L[i - 1])); j = std::max (j, std::fabs (x2.L[0] - x1.L.back()));
        check (j < 0.05f, "switching filter + gate on fades in (no click)");
    }
    {   // fuzz
        std::mt19937 rng (42); std::uniform_real_distribution<float> u (0, 1);
        bool fin = true; float pk = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            Engine f; f.prepare (rate); Transport t { 60.0 + 140.0 * u (rng), 0.0, true };
            for (int round = 0; round < 60; ++round)
            {
                Params P; P.drOn = u (rng) < 0.5f; P.drDb = 36 * u (rng); P.drMode = (int) (u (rng) * 4) % 4; P.drTone = 1000 + 19000 * u (rng);
                P.flOn = u (rng) < 0.5f; P.flMode = (int) (u (rng) * 4) % 4; P.flCut = 20 + 19980 * u (rng) * u (rng); P.flReso = u (rng); P.flVowel = u (rng); P.flLfoRate = (int) (u (rng) * 7) % 7; P.flLfoDepth = u (rng);
                P.gtOn = u (rng) < 0.5f; P.gtRate = (int) (u (rng) * 5) % 5; P.gtPattern = (int) (u (rng) * 8) % 8; P.gtDepth = u (rng); P.gtSmooth = 0.5f + 49.5f * u (rng);
                P.stOn = u (rng) < 0.4f; P.stLen = (int) (u (rng) * 8) % 8; P.stGate = 0.05f + 0.95f * u (rng); P.stDecay = 0.9f * u (rng);
                P.tsOn = u (rng) < 0.3f; P.tsStop = 50 + 3950 * u (rng); P.tsStart = 4000 * u (rng);
                P.dj = 2 * u (rng) - 1; P.intensity = u (rng); P.mix = u (rng); P.outDb = -24 + 36 * u (rng);
                f.setParams (P); t.playing = u (rng) < 0.8f; if (u (rng) < 0.1f) t.ppq = 1000 * u (rng);
                const int blk = 1 + (int) (u (rng) * 1024);
                auto s = noise (8192, (unsigned) round); auto o = run (f, s, t, rate, blk);
                for (float v : o.L) { fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
            }
        }
        std::printf ("        fuzz peak %.2f\n", pk);
        check (fin && pk < 40.0f, "fuzz: random settings, toggles, block sizes, 44.1 / 48 / 96 kHz stay finite and bounded");
    }
    {   // speed
        Params P; P.drOn = P.flOn = P.gtOn = true; P.flMode = 3; P.flLfoDepth = 0.5f; P.dj = 0.3f; e.setParams (P); e.reset(); Transport t = play;
        auto s = noise (48000 * 10); const auto t0 = std::chrono::steady_clock::now(); run (e, s, t, sr);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("        10 s of stereo, everything on: %.3f s (%.0fx real time)\n", secs, 10.0 / secs);
        check (secs < 1.0, "faster than 10x real time");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
