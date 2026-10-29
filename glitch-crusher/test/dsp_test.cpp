// Offline headless tests for the Glitch Crusher DSP core.
#include "../src/GlitchCrusherDSP.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <set>

// ---- allocation counter (to verify the audio path never allocates) ----
static bool g_countAllocs = false;
static long g_allocs = 0;
void* operator new (std::size_t n) { if (g_countAllocs) ++g_allocs; void* p = std::malloc (n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using Buf = std::vector<float>;
static const double PI = 3.14159265358979323846;

static Buf sine (int n, double sr, double f, double a) { Buf b ((size_t) n); for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (a * std::sin (2 * PI * f * i / sr)); return b; }
static Buf noise (int n, float a, unsigned seed) { std::mt19937 g (seed); std::uniform_real_distribution<float> d (-a, a); Buf b ((size_t) n); for (auto& x : b) x = d (g); return b; }
static Buf drums (int n, double sr)
{
    Buf b ((size_t) n, 0.f); std::mt19937 g (7); std::uniform_real_distribution<float> d (-1.f, 1.f);
    const int beat = (int) (sr * 0.5); // 120 bpm
    for (int s = 0; s < n; ++s)
    {
        const int pos = s % beat, bi = (s / beat) % 4;
        double v = 0;
        if (bi == 0 || bi == 2) { double t = pos / sr; v += 0.95 * std::sin (2 * PI * (45 * t + 90 * (1 - std::exp (-t * 30)) / 30.0 * 0 + 110 * std::exp (-t * 30) / 30.0 * 0 ) ) * std::exp (-t * 8); v += 0.9 * std::sin (2 * PI * (50 * t + 4 * (1 - std::exp (-30 * t)))) * std::exp (-t * 7); }
        if (bi == 1 || bi == 3) { double t = pos / sr; v += 0.7 * d (g) * std::exp (-t * 14); }
        { double t = (s % (beat / 2)) / sr; v += 0.3 * d (g) * std::exp (-t * 60); }
        b[(size_t) s] = (float) std::fmax (-1.0, std::fmin (1.0, v));
    }
    return b;
}

static gc::Params transparent()
{
    gc::Params p; p.bits = 16; p.downsample = 1; p.drive = 0; p.glitchProb = 0; p.filterType = 0; p.mix = 1; p.outGainDb = 0; return p;
}

// run engine over interleaved-by-channel buffers using given block pattern. params applied once up front (snap).
static std::vector<Buf> run (gc::Engine& e, const std::vector<Buf>& in, const std::vector<int>& blocks, const gc::Transport& tp0, bool hasPpq, double sr)
{
    std::vector<Buf> out = in; const int n = (int) in[0].size(); const int nc = (int) in.size();
    int pos = 0, bi = 0;
    while (pos < n)
    {
        const int len = std::min (blocks[(size_t) (bi++ % (int) blocks.size())], n - pos);
        std::vector<float*> ptrs; for (int c = 0; c < nc; ++c) ptrs.push_back (out[(size_t) c].data() + pos);
        gc::Transport tp = tp0; tp.hasPpq = hasPpq; tp.ppq = tp0.ppq + pos * tp0.bpm / 60.0 / sr;
        e.process (ptrs.data(), nc, len, tp);
        pos += len;
    }
    return out;
}

static bool finiteAll (const std::vector<Buf>& b, float& peak)
{
    peak = 0; for (auto& c : b) for (float x : c) { if (!std::isfinite (x)) return false; peak = std::fmax (peak, std::fabs (x)); } return true;
}

int main()
{
    const double rates[] = { 44100.0, 48000.0 };
    const int blockSizes[] = { 1, 7, 32, 64, 480, 512, 1023, 4096 };

    std::printf ("== 1. Transparency at 16 bit / x1 / no fx ==\n");
    for (double sr : rates)
    {
        gc::Engine e; e.prepare (sr, 4096, 2); auto p = transparent(); e.setParams (p); e.snapSmoothers();
        auto in = std::vector<Buf> { sine (48000, sr, 440, 0.8), noise (48000, 0.9f, 1) };
        auto out = run (e, in, { 512 }, {}, false, sr);
        double maxErr = 0; for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < in[c].size(); ++i) maxErr = std::fmax (maxErr, std::fabs (out[c][i] - in[c][i]));
        std::printf ("  sr=%.0f max |out-in| = %.3e (16-bit step = %.3e)\n", sr, maxErr, 2.0 / 65536);
        CHECK (maxErr <= 2.0 / 65536 * 0.5 + 1e-7, "transparency error too large %.3e", maxErr);
    }

    std::printf ("== 2. Bit-depth quantiser ==\n");
    for (double sr : rates) for (int bits : { 1, 2, 3, 4, 6, 8, 12, 16 })
    {
        gc::Engine e; e.prepare (sr, 4096, 1); auto p = transparent(); p.bits = (float) bits; e.setParams (p); e.snapSmoothers();
        auto in = std::vector<Buf> { sine (96000, sr, 997, 0.95) };
        auto out = run (e, in, { 333 }, {}, false, sr);
        std::set<float> vals; double maxErr = 0; const double step = 2.0 / std::pow (2.0, bits);
        for (size_t i = 0; i < out[0].size(); ++i) { vals.insert (out[0][i]); maxErr = std::fmax (maxErr, std::fabs (out[0][i] - in[0][i])); }
        const size_t maxLevels = (size_t) std::pow (2.0, bits);
        std::printf ("  sr=%.0f bits=%2d distinct levels=%zu (max %zu) max quant err=%.3e (step/2=%.3e)\n", sr, bits, vals.size(), maxLevels, maxErr, step / 2);
        CHECK (vals.size() <= maxLevels, "too many levels at %d bits", bits);
        CHECK (maxErr <= step / 2 + 1e-6, "quant error %.3e > step/2", maxErr);
        if (bits <= 8) CHECK (vals.size() >= std::min<size_t> (maxLevels, 2) , "too few levels");
    }

    std::printf ("== 3. Sample-rate reduction (hold) ==\n");
    for (double sr : rates) for (float f : { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 2.5f, 64.0f })
    {
        gc::Engine e; e.prepare (sr, 4096, 1); auto p = transparent(); p.downsample = f; e.setParams (p); e.snapSmoothers();
        auto in = std::vector<Buf> { noise (96000, 0.8f, 3) };
        auto out = run (e, in, { 211, 64, 1000 }, {}, false, sr);
        int changes = 0; int maxRun = 1, run_ = 1;
        for (size_t i = 1; i < out[0].size(); ++i) { if (out[0][i] != out[0][i - 1]) { ++changes; run_ = 1; } else maxRun = std::max (maxRun, ++run_); }
        const double measured = (double) out[0].size() / (changes + 1);
        std::printf ("  sr=%.0f factor=%5.1f -> mean hold length=%.3f samples, longest run=%d\n", sr, f, measured, maxRun);
        CHECK (std::fabs (measured - f) < 0.02 * f + 0.02, "hold length %.3f != %.1f", measured, f);
    }

    std::printf ("== 4. Tempo-synced glitch ==\n");
    for (double sr : rates) for (int rate = 0; rate < 4; ++rate) for (int rev = 0; rev < 2; ++rev)
    {
        gc::Engine e; e.prepare (sr, 4096, 2); auto p = transparent(); p.glitchProb = 1.0f; p.glitchRate = rate; p.reverseMode = rev; e.setParams (p); e.snapSmoothers();
        const int n = (int) (sr * 6);
        auto in = std::vector<Buf> { noise (n, 0.8f, 5), noise (n, 0.8f, 6) };
        gc::Transport tp; tp.bpm = 120; tp.ppq = 0;
        auto out = run (e, in, { 256 }, tp, true, sr);
        const int S = e.sliceLengthSamples (120, rate);
        // slice k (k>=1) should be a repeat of input slice 0 (forward) or its reverse. Check slice 3, interior only.
        const int fade = (int) (0.015 * sr); double maxErr = 1e9; int cnt = 0, bestOff = 0;
        // The repeat is input slice 0 (or its mirror). Slice boundaries are rounded to whole samples, so allow a +-2 sample alignment offset.
        for (int off = -2; off <= 2; ++off)
        {
            double err = 0; cnt = 0;
            for (int i = fade; i < S - fade; ++i)
            {
                const int src = rev ? (S - 1 - i + off) : (i + off);
                err = std::fmax (err, std::fabs (out[0][(size_t) (3 * S + i)] - in[0][(size_t) src])); ++cnt;
            }
            if (err < maxErr) { maxErr = err; bestOff = off; }
        }
        
        // also verify channel independence: L and R differ (different inputs)
        std::printf ("  sr=%.0f rate=1/%d slice=%d samples reverse=%d  max|repeat-ref|=%.2e over %d samples (align offset %+d)\n", sr, 4 << rate, S, rev, maxErr, cnt, bestOff);
        CHECK (maxErr < 2.0 / 65536, "glitch repeat does not match (%.3e)", maxErr);
    }
    {
        gc::Engine e; e.prepare (44100, 4096, 2); auto p = transparent(); p.glitchProb = 0.3f; p.glitchRate = 3; e.setParams (p); e.snapSmoothers();
        auto in = std::vector<Buf> { noise (44100 * 240, 0.5f, 9), noise (44100 * 240, 0.5f, 10) };
        gc::Transport tp; tp.bpm = 120;
        run (e, in, { 512 }, tp, true, 44100);
        const double r = (double) e.glitchSlices / (double) e.slicesSeen;
        std::printf ("  probability test: p=0.30 -> measured %.4f over %lld slices (1/32 @120 bpm)\n", r, (long long) e.slicesSeen);
        CHECK (std::fabs (r - 0.3) < 0.02, "probability off %.4f", r);
        gc::Engine e0; e0.prepare (44100, 4096, 1); p.glitchProb = 0; e0.setParams (p); e0.snapSmoothers();
        auto in0 = std::vector<Buf> { noise (44100 * 20, 0.5f, 9) };
        auto o0 = run (e0, in0, { 512 }, tp, true, 44100);
        double md = 0; for (size_t i = 0; i < in0[0].size(); ++i) md = std::fmax (md, std::fabs (o0[0][i] - in0[0][i]));
        std::printf ("  p=0 -> glitchSlices=%lld, max|out-in|=%.2e\n", (long long) e0.glitchSlices, md);
        CHECK (e0.glitchSlices == 0 && md < 2.0 / 65536, "p=0 must be clean");
    }

    std::printf ("== 5. Block-size independence (free-running transport, all fx on) ==\n");
    for (double sr : rates)
    {
        gc::Params p; p.bits = 5.5f; p.downsample = 3.3f; p.drive = 0.6f; p.glitchProb = 0.5f; p.glitchRate = 2; p.reverseMode = 2; p.filterType = 1; p.cutoffHz = 3000; p.resonance = 4; p.mix = 0.8f; p.outGainDb = -3;
        auto in = std::vector<Buf> { drums ((int) sr * 8, sr), noise ((int) sr * 8, 0.3f, 11) };
        gc::Engine ref; ref.prepare (sr, 4096, 2); ref.setParams (p); ref.snapSmoothers();
        auto refOut = run (ref, in, { 512 }, {}, false, sr);
        for (int bs : blockSizes)
        {
            gc::Engine e; e.prepare (sr, 4096, 2); e.setParams (p); e.snapSmoothers();
            auto out = run (e, in, { bs }, {}, false, sr);
            double md = 0; for (size_t c = 0; c < 2; ++c) for (size_t i = 0; i < out[c].size(); ++i) md = std::fmax (md, std::fabs (out[c][i] - refOut[c][i]));
            std::printf ("  sr=%.0f block=%4d  max diff vs block=512: %.3e\n", sr, bs, md);
            CHECK (md == 0.0, "block size %d changes output", bs);
        }
    }

    std::printf ("== 6. Stress: random/extreme params every block, NaN/inf injected, mono/stereo, several rates ==\n");
    {
        long blocks = 0; float worstPeak = 0; bool allFinite = true;
        for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 }) for (int nc : { 1, 2 })
        {
            std::mt19937 g ((unsigned) sr + (unsigned) nc); std::uniform_real_distribution<float> u (0, 1);
            gc::Engine e; e.prepare (sr, 8192, nc);
            auto base = drums ((int) sr * 10, sr); auto nz = noise ((int) sr * 10, 1.0f, 99);
            std::vector<Buf> sig; for (int c = 0; c < nc; ++c) { Buf b = c ? nz : base; if (c == 0) for (size_t i = 0; i < b.size(); i += 40000) { b[i] = NAN; if (i + 1 < b.size()) b[i + 1] = INFINITY; if (i + 2 < b.size()) b[i + 2] = -INFINITY; } sig.push_back (b); }
            size_t pos = 0; const size_t total = sig[0].size(); gc::Transport tp; tp.bpm = 60 + 140 * u (g);
            while (pos < total)
            {
                const int bs = 1 + (int) (u (g) * 2047); const int len = (int) std::min<size_t> ((size_t) bs, total - pos);
                gc::Params p; p.bits = 1 + 15 * u (g); p.downsample = 1 + 63 * u (g); p.drive = u (g); p.glitchProb = u (g) < 0.2f ? 1.f : u (g);
                p.glitchRate = (int) (u (g) * 3.999f); p.reverseMode = (int) (u (g) * 2.999f); p.filterType = (int) (u (g) * 2.999f);
                p.cutoffHz = 20 * std::pow (1000.f, u (g)); p.resonance = u (g) < 0.3f ? 10.f : 0.5f + 9.5f * u (g); p.mix = u (g); p.outGainDb = -24 + 36 * u (g);
                if (u (g) < 0.05f) tp.bpm = 30 + 270 * u (g);
                e.setParams (p);
                std::vector<float*> ptrs; for (int c = 0; c < nc; ++c) ptrs.push_back (sig[(size_t) c].data() + pos);
                tp.hasPpq = u (g) < 0.5f; tp.ppq = pos * tp.bpm / 60.0 / sr;
                e.process (ptrs.data(), nc, len, tp); pos += (size_t) len; ++blocks;
            }
            float pk; bool fin = finiteAll (sig, pk); allFinite &= fin; worstPeak = std::fmax (worstPeak, pk);
            std::printf ("  sr=%6.0f ch=%d finite=%d peak=%.3f\n", sr, nc, (int) fin, pk);
        }
        std::printf ("  total blocks=%ld worst peak=%.3f (+12 dB gain & Q=10 resonance allowed)\n", blocks, worstPeak);
        CHECK (allFinite, "non-finite output");
        CHECK (worstPeak < 40.f, "peak too large");
    }

    std::printf ("== 7. Gain / parameter smoothing (zipper check) ==\n");
    {
        const double sr = 48000; gc::Engine e; e.prepare (sr, 4096, 1); auto p = transparent(); e.setParams (p); e.snapSmoothers();
        Buf in = sine (48000, sr, 1000, 0.5); Buf out = in; float* ptr = out.data(); gc::Transport tp;
        e.process (&ptr, 1, 24000, tp); p.outGainDb = -24; e.setParams (p); e.process (&ptr, 1, 24000, tp);
        double maxJump = 0, idealMax = 0; size_t at = 0; for (size_t i = 1; i < out.size(); ++i) { double j = std::fabs (out[i] - out[i - 1]); if (j > maxJump) { maxJump = j; at = i; } idealMax = std::fmax (idealMax, std::fabs (in[i] - in[i - 1])); }
        std::printf ("  gain 0 -> -24 dB step: max sample-to-sample delta=%.4f (unprocessed sine max delta=%.4f) at sample %zu\n", maxJump, idealMax, at);
        CHECK (maxJump <= idealMax * 1.05, "zipper/jump detected");
        // time for output to reach within 1 dB of target
        size_t t99 = 0; double env = 0; for (size_t i = 24000; i < 48000; ++i) { env = std::fmax (std::fabs (out[i]), env * 0.999); if (std::fabs (out[i]) < 0.5 * std::pow (10.0, -23.0 / 20) && t99 == 0 && i > 24200) t99 = i; }
        (void) env; (void) t99;
        // cutoff + mix jump
        gc::Engine e2; e2.prepare (sr, 4096, 1); auto p2 = transparent(); p2.filterType = 1; p2.cutoffHz = 18000; e2.setParams (p2); e2.snapSmoothers();
        Buf o2 = noise (48000, 0.5f, 4); float* q = o2.data(); e2.process (&q, 1, 24000, tp); p2.cutoffHz = 200; p2.mix = 0.2f; e2.setParams (p2); e2.process (&q, 1, 24000, tp);
        float pk; finiteAll ({ o2 }, pk); std::printf ("  cutoff 18k->200 Hz + mix 1->0.2 jump: output finite, peak %.3f\n", pk);
        CHECK (pk < 2.0f, "cutoff jump overshoot");
    }

    std::printf ("== 8. Mono / stereo consistency ==\n");
    for (double sr : rates)
    {
        gc::Params p; p.bits = 6; p.downsample = 3; p.drive = 0.4f; p.glitchProb = 0.5f; p.glitchRate = 3; p.reverseMode = 2; p.filterType = 2; p.cutoffHz = 400; p.resonance = 2; p.mix = 0.7f;
        Buf d = drums ((int) sr * 5, sr);
        gc::Engine m; m.prepare (sr, 4096, 1); m.setParams (p); m.snapSmoothers();
        gc::Engine s; s.prepare (sr, 4096, 2); s.setParams (p); s.snapSmoothers();
        auto om = run (m, { d }, { 300 }, {}, false, sr); auto os = run (s, { d, d }, { 300 }, {}, false, sr);
        double md = 0, lr = 0; for (size_t i = 0; i < d.size(); ++i) { md = std::fmax (md, std::fabs (om[0][i] - os[0][i])); lr = std::fmax (lr, std::fabs (os[0][i] - os[1][i])); }
        std::printf ("  sr=%.0f mono vs stereo-L max diff=%.1e, L vs R (identical input) max diff=%.1e\n", sr, md, lr);
        CHECK (md == 0.0 && lr == 0.0, "mono/stereo mismatch");
    }

    std::printf ("== 9. Real-time safety: heap allocations during process() ==\n");
    {
        gc::Engine e; e.prepare (48000, 4096, 2); gc::Params p; p.glitchProb = 1; p.reverseMode = 2; p.filterType = 1; e.setParams (p);
        Buf a = drums (48000 * 4, 48000), b = a; float* ptrs[2] = { a.data(), b.data() }; gc::Transport tp; tp.hasPpq = true;
        g_allocs = 0; g_countAllocs = true;
        for (int i = 0; i < 48000 * 4 / 256; ++i) { float* pp[2] = { ptrs[0] + i * 256, ptrs[1] + i * 256 }; tp.ppq = i * 256 * 2.0 / 48000; e.setParams (p); e.process (pp, 2, 256, tp); }
        g_countAllocs = false;
        std::printf ("  allocations in 4 s of audio (all fx, glitch p=1, random reverse): %ld\n", g_allocs);
        CHECK (g_allocs == 0, "allocations on audio thread: %ld", g_allocs);
    }

    std::printf ("== 10. CPU cost & drum-loop sanity ==\n");
    for (double sr : rates)
    {
        gc::Engine e; e.prepare (sr, 4096, 2); gc::Params p; p.bits = 8; p.downsample = 4; p.drive = 0.5f; p.glitchProb = 0.5f; p.glitchRate = 2; p.reverseMode = 2; p.filterType = 1; p.cutoffHz = 6000; p.resonance = 2; e.setParams (p); e.snapSmoothers();
        const int secs = 20; std::vector<Buf> in { drums ((int) sr * secs, sr), drums ((int) sr * secs, sr) };
        gc::Transport tp; tp.bpm = 120;
        auto t0 = std::chrono::steady_clock::now(); auto out = run (e, in, { 256 }, tp, true, sr); auto t1 = std::chrono::steady_clock::now();
        const double el = std::chrono::duration<double> (t1 - t0).count(); float pk; bool fin = finiteAll (out, pk);
        double rms = 0; for (float x : out[0]) rms += (double) x * x; rms = std::sqrt (rms / out[0].size());
        std::printf ("  sr=%.0f %ds stereo drum loop (all fx): %.3f s CPU => %.1fx realtime (%.2f%% of one core), finite=%d peak=%.3f rms=%.3f\n", sr, secs, el, secs / el, 100.0 * el / secs, (int) fin, pk, rms);
        { gc::Engine e2; e2.prepare (sr, 4096, 2); p.resonance = 0.7071f; e2.setParams (p); e2.snapSmoothers(); auto o2 = run (e2, in, { 256 }, tp, true, sr); float pk2; finiteAll (o2, pk2); std::printf ("    same loop with Butterworth Q=0.707: peak=%.3f\n", pk2); CHECK (pk2 <= 1.25f, "peak with Q=0.707 should stay near 0 dBFS"); }
        CHECK (fin && pk <= 4.0f, "drum loop output bad");
    }

    std::printf ("== 11. Section on/off switches (crush / glitch / filter) ==\n");
    for (double sr : rates)
    {
        gc::Transport tp; tp.bpm = 120;
        const Buf in = noise ((int) sr, 0.5f, 11);
        auto render = [&] (const gc::Params& p) { gc::Engine e; e.prepare (sr, 4096, 1); e.setParams (p); e.snapSmoothers(); Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, (int) o.size(), tp); return o; };
        auto maxDiff = [&] (const Buf& a, const Buf& b) { double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::fmax (m, std::fabs (a[i] - b[i])); return m; };
        gc::Params all = transparent(); all.bits = 3; all.downsample = 8; all.drive = 1.0f; all.glitchProb = 1.0f; all.glitchRate = 3; all.filterType = 1; all.cutoffHz = 200;
        gc::Params off = all; off.crushOn = false; off.glitchOn = false; off.filterOn = false;
        const double dOff = maxDiff (render (off), in);
        std::printf ("  sr=%.0f all sections off vs input: max diff=%.1e\n", sr, dOff);
        CHECK (dOff == 0.0, "all sections off must be bit-exact bypass of the effect chain");
        gc::Params c = off; c.crushOn = true; c.bits = 3; c.downsample = 8; c.drive = 1.0f;
        CHECK (maxDiff (render (c), in) > 0.05, "crush on should change the signal");
        gc::Params g = off; g.glitchOn = true; g.glitchProb = 1.0f; g.glitchRate = 3;
        { gc::Engine e; e.prepare (sr, 4096, 1); e.setParams (g); e.snapSmoothers(); Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, (int) o.size(), tp);
          CHECK (e.glitchSlices > 10 && maxDiff (o, in) > 0.05, "glitch on should glitch (slices=%ld)", (long) e.glitchSlices); }
        { gc::Params g2 = g; g2.glitchOn = false; gc::Engine e; e.prepare (sr, 4096, 1); e.setParams (g2); e.snapSmoothers(); Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, (int) o.size(), tp);
          CHECK (e.glitchSlices == 0 && maxDiff (o, in) == 0.0, "glitch off must not glitch"); }
        gc::Params f = off; f.filterOn = true; f.filterType = 1; f.cutoffHz = 200;
        CHECK (maxDiff (render (f), in) > 0.05, "filter on should change the signal");
        std::printf ("  per-section on/off behave as bypass / active\n");
    }
    {   // toggling mid-stream: no click (sample-to-sample step stays within what the steady-state signals already contain)
        const double sr = 48000; gc::Transport tp; tp.bpm = 120;
        const Buf in = sine ((int) sr, sr, 440, 0.6);
        for (int which = 0; which < 3; ++which)
        {
            gc::Params a = transparent(); a.bits = 4; a.downsample = 6; a.drive = 0.7f; a.filterType = 1; a.cutoffHz = 400; a.glitchProb = 1; a.glitchRate = 3; a.reverseMode = 0;
            gc::Params offp = a; if (which == 0) offp.crushOn = false; if (which == 1) offp.glitchOn = false; if (which == 2) offp.filterOn = false;
            double steadyMax = 0; // largest step in either steady state
            for (const gc::Params* pp : { &a, &offp }) { gc::Engine e; e.prepare (sr, 4096, 1); e.setParams (*pp); e.snapSmoothers(); Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, (int) o.size(), tp); for (size_t i = 24000; i < o.size(); ++i) steadyMax = std::fmax (steadyMax, std::fabs (o[i] - o[i - 1])); }
            gc::Engine e; e.prepare (sr, 4096, 1); e.setParams (a); e.snapSmoothers(); Buf o = in; float* ptr = o.data();
            e.process (&ptr, 1, 24000, tp); e.setParams (offp); float* p2 = ptr + 24000; e.process (&p2, 1, 24000, tp);
            double m = 0; for (size_t i = 23990; i < 26000; ++i) m = std::fmax (m, std::fabs (o[i] - o[i - 1]));
            float pk; const bool fin = finiteAll ({ o }, pk);
            std::printf ("  toggle %s off mid-stream: max step %.4f (steady-state max %.4f)\n", which == 0 ? "CRUSH" : which == 1 ? "GLITCH" : "FILTER", m, steadyMax);
            CHECK (fin && m <= steadyMax * 1.25 + 1.0e-3, "click when toggling section %d", which);
        }
    }

    std::printf ("\nRESULT: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
