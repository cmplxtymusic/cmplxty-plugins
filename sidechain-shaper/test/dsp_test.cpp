// Sidechain Shaper DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/ShaperDSP.h"
#include <cstdio>
#include <chrono>
#include <random>
#include <vector>
#include <memory>
using namespace sc::dsp;
static int fails = 0;
static void check (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; }
static const double SR = 48000.0;
using Buf = std::vector<float>;

// runs the engine over l / r (in place) with an optional sidechain; the host transport plays from ppq0 (or is stopped)
static void run (Engine& e, Buf& l, Buf& r, const Buf* scl = nullptr, bool playing = true, double bpm = 120, double ppq0 = 0, double barBeats = 4, int block = 256)
{
    for (size_t i = 0; i < l.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - i);
        float* c[2] = { l.data() + i, r.data() + i };
        const float* s[2] = { scl ? scl->data() + i : nullptr, scl ? scl->data() + i : nullptr };
        TimeInfo ti; ti.bpm = bpm; ti.playing = playing; ti.hasPpq = true; ti.ppq = ppq0 + (double) i / SR * bpm / 60.0; ti.barBeats = barBeats;
        e.process (c, 2, n, scl ? s : nullptr, scl ? 2 : 0, ti);
    }
}
static std::unique_ptr<Engine> make (Params p, const Curve& c)
{
    auto e = std::make_unique<Engine>(); e->setParams (p); e->setCurve (c); e->prepare (SR, 2); return e;
}
static Buf dc (size_t n, float v = 0.5f) { return Buf (n, v); }
static Buf kicks (size_t n, size_t every, size_t first = 1000, float amp = 0.8f)
{
    Buf k (n, 0.0f);
    for (size_t s = first; s < n; s += every)
        for (size_t i = 0; i < 2400 && s + i < n; ++i) k[s + i] = amp * (float) (std::exp (-(double) i / 500.0) * std::sin (6.2831853 * 60.0 * (double) i / SR));
    return k;
}
// indices of the local gain minima below `below` (gain = out / in for a DC input)
static std::vector<size_t> minima (const Buf& g, float below)
{
    std::vector<size_t> m; size_t i = 1;
    while (i < g.size())
    {
        if (g[i] < below) { size_t best = i; while (i < g.size() && g[i] < below) { if (g[i] < g[best]) best = i; ++i; } m.push_back (best); }
        ++i;
    }
    return m;
}

int main()
{
    // ---- curve maths
    {
        check (std::fabs (bend (0.5f, 0) - 0.5f) < 1e-6f && bend (0.5f, -0.5f) > 0.5f && bend (0.5f, 0.5f) < 0.5f && std::fabs (bend (1, 0.7f) - 1) < 1e-5f, "segment bend: straight / fast-first / slow-first, ends at 1");
        const auto p = shapeByName ("Pump");
        check (std::fabs (evalCurve (p, 0) - 0) < 1e-4f && std::fabs (evalCurve (p, 1) - 1) < 1e-4f, "Pump: silent at the hit, full at the end");
        bool mono = true; float last = -1; for (int i = 0; i <= 100; ++i) { const float v = evalCurve (p, i / 100.0f); mono = mono && v >= last - 1e-6f; last = v; }
        check (mono, "Pump rises monotonically");
        const auto t = parseCurve (formatCurve (p)); bool same = t.size() == p.size();
        for (size_t i = 0; same && i < t.size(); ++i) same = std::fabs (t[i].x - p[i].x) < 2e-4f && std::fabs (t[i].y - p[i].y) < 2e-4f && std::fabs (t[i].t - p[i].t) < 2e-4f;
        check (same, "curve text round trip (integer format, no locale decimals)");
        const auto g1 = parseCurve (""), g2 = parseCurve ("garbage;;1 2;x y z;99999 -5 77777");
        check (g1.size() >= 2 && g2.size() >= 2 && g2.front().x == 0 && g2.back().x == 1, "garbage curve text gives a valid curve");
        bool inRange = true; for (auto& q : g2) inRange = inRange && q.y >= 0 && q.y <= 1 && q.t >= -1 && q.t <= 1;
        check (inRange, "parsed values clamped");
        Curve many; for (int i = 0; i < 500; ++i) many.push_back ({ i / 500.0f, 0.5f, 0 });
        check ((int) sanitize (many).size() <= kMaxPts + 1, "point count limited");
        check (shapes().size() >= 12, ">= 12 curve shapes");
        bool shapesOk = true; for (auto& s : shapes()) { auto c = sanitize (s.pts); for (int i = 0; i <= 200; ++i) { float v = evalCurve (c, i / 200.0f); shapesOk = shapesOk && std::isfinite (v) && v >= 0 && v <= 1; } }
        check (shapesOk, "every shape stays within 0..1");
        const auto th = shapeByName ("Trance Gate"); int opens = 0; for (int i = 0; i < 16; ++i) opens += evalCurve (th, (i + 0.4f) / 16.0f) > 0.9f ? 1 : 0;
        check (opens == 10, "Trance Gate: its 16-step pattern (10 open steps)");
        check (std::fabs (rateBeats (3) - 1) < 1e-9 && std::fabs (rateBeats (5, 4) - 4) < 1e-9 && std::fabs (rateBeats (5, 3) - 3) < 1e-9 && std::fabs (rateBeats (9) - 1.0 / 3) < 1e-9 && std::fabs (rateBeats (13) - 1.5) < 1e-9, "note lengths: 1/4, bar (4/4 and 3/4), 1/8 T, 1/4 D");
    }
    // ---- flat curve = unity
    {
        Params p; auto e = make (p, shapeByName ("Flat"));
        auto l = dc (48000), r = dc (48000); run (*e, l, r);
        float d = 0; for (size_t i = 100; i < l.size(); ++i) d = std::max (d, std::fabs (l[i] - 0.5f));
        check (d < 1e-5f, "flat curve leaves the audio unchanged");
    }
    // ---- tempo sync
    {
        Params p; p.smoothMs = 0.0f; auto e = make (p, shapeByName ("Pump"));
        const size_t n = 48000 * 3; auto l = dc (n, 1.0f), r = dc (n, 1.0f); run (*e, l, r);
        auto m = minima (l, 0.05f); bool on = m.size() >= 5;
        for (size_t k = 0; on && k < m.size(); ++k) on = (m[k] % 24000) < 30 || (m[k] % 24000) > 23970;
        check (on, "120 BPM, 1/4: the dip lands on every beat (every 24000 samples)");
        check (l[24000 * 2 + 21600] > 0.97f, "full volume late in the beat");
        Params q = p; q.offset = 0.5f; auto e2 = make (q, shapeByName ("Pump")); auto l2 = dc (n, 1.0f), r2 = dc (n, 1.0f); run (*e2, l2, r2);
        auto m2 = minima (l2, 0.05f); bool half = ! m2.empty(); for (auto i : m2) half = half && std::abs ((long) (i % 24000) - 12000) < 30;
        check (half, "Offset 50% moves the dip to the off-beat");
        auto e3 = make (p, shapeByName ("Pump")); auto l3 = dc (4800, 1.0f), r3 = dc (4800, 1.0f); run (*e3, l3, r3, nullptr, true, 120, 10.75);
        check (std::fabs (l3[0] - evalCurve (shapeByName ("Pump"), 0.75f)) < 0.02f, "follows the host position (jump to beat 10.75 -> phase 0.75)");
        Params b = p; b.rate = 5; auto e4 = make (b, shapeByName ("Pump")); auto l4 = dc (48000 * 4, 1.0f), r4 = dc (48000 * 4, 1.0f); run (*e4, l4, r4, nullptr, true, 120, 0, 3);
        auto m4 = minima (l4, 0.05f); check (m4.size() >= 2 && std::abs ((long) (m4[1] - m4[0]) - 72000) < 40, "1 Bar in 3/4 = 3 beats (72000 samples at 120 BPM)");
        Params t = p; t.rate = 9; auto e5 = make (t, shapeByName ("Pump")); auto l5 = dc (48000, 1.0f), r5 = dc (48000, 1.0f); run (*e5, l5, r5, nullptr, true, 150);
        auto m5 = minima (l5, 0.05f); check (m5.size() >= 3 && std::abs ((long) (m5[2] - m5[1]) - 6400) < 20, "1/8 T at 150 BPM = 6400 samples");
        auto e6 = make (p, shapeByName ("Pump")); auto l6 = dc (n, 1.0f), r6 = dc (n, 1.0f); run (*e6, l6, r6, nullptr, false, 90);
        auto m6 = minima (l6, 0.05f); check (m6.size() >= 3 && std::abs ((long) (m6[2] - m6[1]) - 32000) < 30, "transport stopped: free-runs at the host tempo (90 BPM)");
        auto e7 = make (p, shapeByName ("Pump")); auto l7 = dc (n, 1.0f), r7 = dc (n, 1.0f); run (*e7, l7, r7, nullptr, false, 0);
        auto m7 = minima (l7, 0.05f); check (m7.size() >= 3 && std::abs ((long) (m7[2] - m7[1]) - 24000) < 30, "no tempo from the host: 120 BPM");
    }
    // ---- depth / mix / smoothing / gain / stereo
    {
        Params p; p.depth = 0; auto e = make (p, shapeByName ("Pump")); auto l = dc (48000, 1.0f), r = dc (48000, 1.0f); run (*e, l, r);
        float d = 0; for (float v : l) d = std::max (d, std::fabs (v - 1.0f)); check (d < 1e-5f, "depth 0 = no change");
        p.depth = 0.5f; p.smoothMs = 0; auto e2 = make (p, shapeByName ("Pump")); auto l2 = dc (48000, 1.0f), r2 = dc (48000, 1.0f); run (*e2, l2, r2);
        float mn = 1; for (size_t i = 1000; i < l2.size(); ++i) mn = std::min (mn, l2[i]); check (std::fabs (mn - 0.5f) < 0.03f, "depth 50% dips to half volume");
        Params q; q.mix = 0; auto e3 = make (q, shapeByName ("Hard Pump")); auto l3 = dc (48000, 0.3f), r3 = dc (48000, 0.3f); run (*e3, l3, r3);
        d = 0; for (float v : l3) d = std::max (d, std::fabs (v - 0.3f)); check (d < 1e-5f, "mix 0 = dry");
        auto maxStep = [] (float ms) { Params s; s.smoothMs = ms; s.rate = 1; auto e = make (s, shapeByName ("Gate Half")); auto l = dc (48000, 1.0f), r = dc (48000, 1.0f); run (*e, l, r);
                                       float m = 0; for (size_t i = 1; i < l.size(); ++i) m = std::max (m, std::fabs (l[i] - l[i - 1])); return m; };
        const float s0 = maxStep (0.0f), s5 = maxStep (5.0f);
        std::printf ("  gate step: smooth 0 ms %.3f, 5 ms %.4f per sample\n", s0, s5);
        check (s0 > 0.02f && s5 < 0.0045f, "Smooth rounds off the gate edges (no clicks)");
        Params g; g.gainDb = 6.0206f; auto e4 = make (g, shapeByName ("Flat")); auto l4 = dc (48000, 0.25f), r4 = dc (48000, 0.25f); run (*e4, l4, r4);
        check (std::fabs (l4.back() - 0.5f) < 1e-3f, "Output +6 dB doubles the level");
        Params st; st.stereo = 0.25f; st.smoothMs = 0; auto e5 = make (st, shapeByName ("Pump")); auto l5 = dc (48000, 1.0f), r5 = dc (48000, 1.0f); run (*e5, l5, r5);
        auto mL = minima (l5, 0.05f), mR = minima (r5, 0.05f);
        check (! mL.empty() && ! mR.empty() && std::abs ((long) (mR[0] % 24000) - 18000) < 30, "Stereo 25%: the right channel runs a quarter cycle ahead");
        Params st0; auto e6 = make (st0, shapeByName ("Pump")); auto l6 = dc (48000, 1.0f), r6 = dc (48000, 1.0f); run (*e6, l6, r6);
        d = 0; for (size_t i = 0; i < l6.size(); ++i) d = std::max (d, std::fabs (l6[i] - r6[i])); check (d < 1e-6f, "Stereo 0: both channels identical");
    }
    // ---- sidechain mode
    {
        Params p; p.mode = 1; p.rate = 2; p.smoothMs = 0.5f; p.thrDb = -24;   // 1/8 at 120 BPM = 250 ms
        const size_t n = 48000 * 3; const auto k = kicks (n, 28800);              // a kick every 600 ms
        auto e = make (p, shapeByName ("Pump")); auto l = dc (n, 1.0f), r = dc (n, 1.0f); run (*e, l, r, &k);
        const int expect = (int) ((n - 1000 + 28799) / 28800);
        std::printf ("  sidechain: %d triggers for %d kicks, external=%d\n", e->triggers.load(), expect, e->usingExternal.load());
        check (e->triggers.load() == expect && e->usingExternal.load() == 1, "one trigger per kick on the sidechain input");
        bool dips = true; for (size_t s = 1000; s + 26000 < n; s += 28800) { float mn = 1; for (size_t i = s; i < s + 600; ++i) mn = std::min (mn, l[i]); dips = dips && mn < 0.1f && l[s + 26000] > 0.97f; }
        check (dips, "the curve restarts on each kick and holds full volume between kicks");
        check (l[500] > 0.97f, "before the first hit: the curve's end value (full volume)");
        Params hi = p; hi.thrDb = -1.0f; auto e2 = make (hi, shapeByName ("Pump")); auto l2 = dc (n, 1.0f), r2 = dc (n, 1.0f); run (*e2, l2, r2, &k);
        check (e2->triggers.load() == 0 && l2.back() > 0.99f, "threshold above the kicks: no triggers, no ducking");
        auto e3 = make (p, shapeByName ("Pump")); Buf in = k; for (auto& v : in) v *= 1.0f; Buf inR = in; run (*e3, in, inR, nullptr);
        check (e3->usingExternal.load() == 0 && e3->triggers.load() == expect, "no sidechain connected: the plug-in's own input triggers it");
        Params src = p; src.source = 1; auto e4 = make (src, shapeByName ("Pump")); auto l4 = dc (n, 0.01f), r4 = dc (n, 0.01f); run (*e4, l4, r4, &k);
        check (e4->usingExternal.load() == 0 && e4->triggers.load() == 0, "Source = Input ignores the sidechain");
        Params h = p; h.holdMs = 100; const auto fast = kicks (n, 1920);       // a hit every 40 ms
        auto e5 = make (h, shapeByName ("Pump")); auto l5 = dc (n, 1.0f), r5 = dc (n, 1.0f); run (*e5, l5, r5, &fast);
        const int hits = (int) ((n - 1000) / 1920);
        std::printf ("  hold 100 ms: %d triggers for %d hits\n", e5->triggers.load(), hits);
        check (e5->triggers.load() <= hits / 2 + 1 && e5->triggers.load() > 0, "Hold limits the retrigger rate");
        Buf hat (n, 0.0f); std::mt19937 rng (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (size_t s = 1000; s < n; s += 12000) for (size_t i = 0; i < 1200; ++i) hat[s + i] = u (rng) * (float) std::exp (-(double) i / 150.0);
        Params f = p; f.thrDb = -18; f.focusHz = 120; auto e6 = make (f, shapeByName ("Pump")); auto l6 = dc (n, 1.0f), r6 = dc (n, 1.0f); run (*e6, l6, r6, &hat);
        Params f2 = f; f2.focusHz = 20000; auto e7 = make (f2, shapeByName ("Pump")); auto l7 = dc (n, 1.0f), r7 = dc (n, 1.0f); run (*e7, l7, r7, &hat);
        std::printf ("  hats: focus 120 Hz %d triggers, full range %d\n", e6->triggers.load(), e7->triggers.load());
        check (e6->triggers.load() == 0 && e7->triggers.load() >= 10, "Focus (detector lowpass) ignores hi-hats, full range catches them");
    }
    // ---- robustness
    {
        bool fin = true, bounded = true;
        for (auto& s : shapes())
            for (int rate = 0; rate < kNumRates; ++rate)
                for (int mode = 0; mode < 2; ++mode)
                {
                    Params p; p.rate = rate; p.mode = mode; p.stereo = 0.3f; p.smoothMs = 2;
                    auto e = make (p, s.pts); const auto k = kicks (9600, 4000);
                    Buf l (9600), r (9600); std::mt19937 rng (rate); std::uniform_real_distribution<float> u (-1, 1); for (size_t i = 0; i < l.size(); ++i) { l[i] = u (rng); r[i] = u (rng); }
                    l[77] = NAN; r[300] = INFINITY;
                    run (*e, l, r, &k);
                    for (size_t i = 0; i < l.size(); ++i) { fin = fin && std::isfinite (l[i]) && std::isfinite (r[i]); bounded = bounded && std::fabs (l[i]) <= 1.0001f && std::fabs (r[i]) <= 1.0001f; }
                }
        check (fin, "every shape x note length x mode: finite output, NaN / inf input sanitised");
        check (bounded, "the shaper never boosts (output <= input peak at 0 dB)");
        Params p; auto e = make (p, shapeByName ("Pump")); Buf l (480, 0.5f), r (480, 0.5f); float* c[2] = { l.data(), r.data() };
        TimeInfo bad; bad.bpm = NAN; bad.ppq = NAN; bad.playing = true; bad.hasPpq = true; e->process (c, 2, 480, nullptr, 0, bad);
        bool ok = true; for (float v : l) ok = ok && std::isfinite (v); check (ok, "NaN tempo / position from the host handled");
        Buf m (480, 0.5f); float* mc[1] = { m.data() }; TimeInfo ti; e->process (mc, 1, 480, nullptr, 0, ti); ok = true; for (float v : m) ok = ok && std::isfinite (v); check (ok, "mono");
    }
    // ---- speed
    {
        Params p; p.mode = 1; p.focusHz = 150; auto e = make (p, shapeByName ("Trance Gate"));
        const size_t n = 48000 * 20; Buf l (n, 0.3f), r (n, 0.3f); const auto k = kicks (n, 24000);
        const auto t0 = std::chrono::steady_clock::now(); run (*e, l, r, &k, true, 120, 0, 4, 512);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  20 s stereo + sidechain in %.3f s (%.0fx realtime)\n", s, 20.0 / s);
        check (20.0 / s > 100.0, "faster than 100x realtime");
    }
    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILED\n", fails);
    return fails;
}
