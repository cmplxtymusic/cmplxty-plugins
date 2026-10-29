// Unit tests for src/Analysis.h (no JUCE). Build: g++ -O2 -std=c++17 -Isrc test/analysis_test.cpp -o build/analysis_test
#include "../src/Analysis.h"
#include <cstdio>
#include <random>
#include <thread>

using namespace vz::ana;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL %s:%d  ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Sig { std::vector<float> L, R; };

// Runs a signal through Analyzer in host-sized blocks, collecting frames; optional host transport.
struct Run
{
    std::vector<Frame> frames;
    std::vector<double> onsetTimes;
    double sr;
    explicit Run (double s) : sr (s) {}
    void go (const Sig& s, int block = 512, Host host = {})
    {
        auto an = std::make_unique<Analyzer>();
        an->prepare (sr);
        double t = 0;
        for (size_t i = 0; i < s.L.size(); i += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, s.L.size() - i);
            const float* ch[2] = { s.L.data() + i, s.R.data() + i };
            Host h = host;
            if (h.valid && h.playing) h.ppq = host.ppq + (double) i / sr * host.bpm / 60.0;
            an->process (ch, 2, n, h);
            Frame f; Stereo st;
            while (an->samples.pop (st)) {}
            while (an->frames.pop (f)) { t += f.hopSec; frames.push_back (f); if (f.onset) onsetTimes.push_back (t); }
        }
    }
};

static Sig sine (double sr, double sec, double hz, float amp)
{
    Sig s; const size_t n = (size_t) (sr * sec); s.L.resize (n); s.R.resize (n);
    for (size_t i = 0; i < n; ++i) s.L[i] = s.R[i] = amp * (float) std::sin (2 * M_PI * hz * (double) i / sr);
    return s;
}

// kick-like bursts (pitch-dropping sine, 120 ms decay) at the given bpm, on top of an optional noise bed
static Sig kicks (double sr, double sec, double bpm, float amp, float noiseAmp = 0, double offset = 0.05)
{
    Sig s; const size_t n = (size_t) (sr * sec); s.L.assign (n, 0); s.R.assign (n, 0);
    std::mt19937 rng (7); std::uniform_real_distribution<float> u (-1, 1);
    for (size_t i = 0; i < n; ++i) s.L[i] = noiseAmp * u (rng);
    const double per = 60.0 / bpm;
    for (double t0 = offset; t0 < sec; t0 += per)
    {
        const size_t a = (size_t) (t0 * sr);
        double ph = 0;
        for (size_t k = 0; k < (size_t) (0.25 * sr) && a + k < n; ++k)
        {
            const double tt = (double) k / sr;
            const double f = 50 + 120 * std::exp (-tt * 30);
            ph += 2 * M_PI * f / sr;
            s.L[a + k] += amp * (float) (std::sin (ph) * std::exp (-tt * 9));
        }
    }
    s.R = s.L;
    return s;
}

static void testFifo()
{
    auto q = std::make_unique<Spsc<int, 8>>();
    for (int i = 0; i < 8; ++i) CHECK (q->push (i), "push %d", i);
    CHECK (! q->push (99), "overflow must fail");
    int v = -1;
    for (int i = 0; i < 8; ++i) { CHECK (q->pop (v) && v == i, "pop order %d got %d", i, v); }
    CHECK (! q->pop (v), "empty pop");
    int arr[20]; for (int i = 0; i < 20; ++i) arr[i] = i;
    CHECK (q->pushMany (arr, 20) == 8, "pushMany clamps to capacity");
    // threaded stress: 2M items, order must be preserved, no loss when the consumer keeps up
    auto big = std::make_unique<Spsc<uint32_t, 1024>>();
    const uint32_t N = 2000000; bool ok = true;
    std::thread prod ([&] { for (uint32_t i = 0; i < N;) if (big->push (i)) ++i; });
    uint32_t expect = 0, x;
    while (expect < N) if (big->pop (x)) { if (x != expect) ok = false; ++expect; }
    prod.join();
    CHECK (ok, "threaded FIFO order");
}

static void testBands()
{
    const double sr = 48000;
    struct C { double hz; int dom; const char* name; } cs[] = { { 60, 0, "60 Hz" }, { 1000, 1, "1 kHz" }, { 8000, 2, "8 kHz" } };
    for (auto& c : cs)
    {
        Run r (sr); r.go (sine (sr, 1.0, c.hz, 0.5f));
        double e[3] = { 0, 0, 0 };
        for (size_t i = r.frames.size() / 2; i < r.frames.size(); ++i) { e[0] += r.frames[i].bass; e[1] += r.frames[i].mid; e[2] += r.frames[i].high; }
        const int dom = e[0] > e[1] && e[0] > e[2] ? 0 : e[1] > e[2] ? 1 : 2;
        CHECK (dom == c.dom, "%s dominant band %d (b %.3f m %.3f h %.3f)", c.name, dom, e[0], e[1], e[2]);
        const double other = (e[0] + e[1] + e[2] - e[dom]) / 2;
        CHECK (e[dom] > 4 * other, "%s band separation %.1fx", c.name, e[dom] / std::max (1e-9, other));
        std::printf ("  bands %-6s bass %.3f mid %.3f high %.3f\n", c.name, e[0] / (r.frames.size() / 2), e[1] / (r.frames.size() / 2), e[2] / (r.frames.size() / 2));
    }
    // rms of a full-scale-ish sine ~ amp/sqrt2
    Run r (sr); r.go (sine (sr, 0.5, 440, 0.5f));
    CHECK (std::abs (r.frames.back().rms - 0.3536f) < 0.02f, "rms %.3f", r.frames.back().rms);
    CHECK (std::abs (r.frames.back().peak - 0.5f) < 0.01f, "peak %.3f", r.frames.back().peak);
}

static void testOnsets()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 64, 512, 2048 })
        {
            Run r (sr); r.go (kicks (sr, 8.0, 120, 0.7f), block);
            int hits = 0; double maxErr = 0;
            for (double t : r.onsetTimes)
            {
                const double k = std::round ((t - 0.05) / 0.5);
                const double err = std::abs (t - (0.05 + k * 0.5));
                maxErr = std::max (maxErr, err); ++hits;
            }
            CHECK (hits == 16, "sr %.0f block %d: 16 kicks -> %d onsets", sr, block, hits);
            CHECK (maxErr < 0.02, "sr %.0f block %d onset timing err %.1f ms", sr, block, maxErr * 1000);
        }
    }
    const double sr = 48000;
    { Run r (sr); r.go (kicks (sr, 8.0, 128, 0.5f, 0.03f)); CHECK (std::abs ((int) r.onsetTimes.size() - 17) <= 1, "kicks over a noise bed: %d onsets (expect 17)", (int) r.onsetTimes.size()); }
    { Run r (sr); Sig s; s.L.assign ((size_t) (sr * 4), 0); s.R = s.L; r.go (s); CHECK (r.onsetTimes.empty(), "silence -> %d onsets", (int) r.onsetTimes.size()); }
    { Run r (sr); r.go (sine (sr, 4.0, 220, 0.5f)); int late = 0; for (double t : r.onsetTimes) if (t > 0.3) ++late; CHECK (late == 0, "steady tone -> %d onsets after start", late); }
    { // steady noise: no (or almost no) onsets
        Run r (sr); Sig s; s.L.resize ((size_t) (sr * 4)); std::mt19937 g (3); std::uniform_real_distribution<float> u (-0.3f, 0.3f);
        for (auto& v : s.L) { v = u (g); }
        s.R = s.L; r.go (s);
        int late = 0; for (double t : r.onsetTimes) if (t > 0.3) ++late;
        CHECK (late <= 1, "steady noise -> %d onsets", late);
    }
    { Run r (sr); r.go (kicks (sr, 4.0, 120, 0.0005f)); CHECK (r.onsetTimes.empty(), "very quiet (-66 dB) kicks ignored: %d", (int) r.onsetTimes.size()); }
    { Run r (sr); r.go (kicks (sr, 8.0, 120, 0.01f)); CHECK (r.onsetTimes.size() == 16, "quiet (-40 dB) kicks still detected: %d", (int) r.onsetTimes.size()); }
    { // kicks plus hats on the off-beats (a busier pattern): every kick found, hats may add onsets but never > 2 per beat
        Sig s = kicks (sr, 8.0, 120, 0.6f, 0.0f);
        std::mt19937 g (5); std::uniform_real_distribution<float> u (-1, 1);
        for (double t0 = 0.3; t0 < 8.0; t0 += 0.5) for (size_t k = 0; k < (size_t) (0.04 * sr); ++k) { const size_t i = (size_t) (t0 * sr) + k; if (i < s.L.size()) s.L[i] += 0.15f * u (g) * (float) std::exp (-(double) k / sr * 80); }
        s.R = s.L;
        Run r (sr); r.go (s);
        int kickHits = 0; for (double t : r.onsetTimes) { const double ph = std::fmod (t - 0.05 + 10.0, 0.5); if (ph < 0.02 || ph > 0.48) ++kickHits; }
        CHECK (kickHits == 16, "kicks+hats: %d kick onsets of 16 (%d total)", kickHits, (int) r.onsetTimes.size());
        CHECK ((int) r.onsetTimes.size() <= 32, "kicks+hats: %d onsets", (int) r.onsetTimes.size());
    }
}

static void testTempo()
{
    const double sr = 48000;
    for (double bpm : { 90.0, 120.0, 128.0, 140.0, 100.0 })
    {
        auto an = std::make_unique<Analyzer>(); an->prepare (sr);
        Features fe; fe.setSampleRate (sr);
        Features::Settings st; st.sync = false;
        Sig s = kicks (sr, 12.0, bpm, 0.6f, 0.01f);
        int beats = 0;
        for (size_t i = 0; i < s.L.size(); i += 800)   // one "render frame" every 800 samples (60 fps)
        {
            const int n = (int) std::min ((size_t) 800, s.L.size() - i);
            const float* ch[2] = { s.L.data() + i, s.R.data() + i };
            an->process (ch, 2, n, {});
            const auto& sn = fe.consume (an->frames, an->samples, n / sr, st);
            if (sn.onset) ++beats;
        }
        const float got = fe.detectedBpm();
        CHECK (std::abs (got - bpm) < 1.5, "tempo %.0f -> %.2f", bpm, got);
        CHECK (std::abs (fe.snapshot().bpm - bpm) < 1.5, "snapshot bpm %.0f -> %.2f", bpm, fe.snapshot().bpm);
        const int expect = (int) std::ceil ((12.0 - 0.05) / (60.0 / bpm));
        CHECK (std::abs (beats - expect) <= 1, "bpm %.0f beat pulses %d expect %d", bpm, beats, expect);
        std::printf ("  tempo %.0f bpm -> detected %.2f, %d beats\n", bpm, got, beats);
    }
}

static void testHostClock()
{
    const double sr = 48000;
    auto an = std::make_unique<Analyzer>(); an->prepare (sr);
    Features fe; fe.setSampleRate (sr);
    Features::Settings st; st.sync = true; st.beatMode = 2;
    Host h; h.valid = true; h.playing = true; h.hasTempo = true; h.bpm = 100; h.ppq = 8.0;
    std::vector<float> z (480, 0.0f);
    int ticks = 0;
    for (int f = 0; f < 600; ++f)   // 6 s
    {
        const float* ch[2] = { z.data(), z.data() };
        Host hh = h; hh.ppq = 8.0 + f * 480 / sr * 100.0 / 60.0;
        an->process (ch, 2, 480, hh);
        const auto& sn = fe.consume (an->frames, an->samples, 0.01, st);
        if (sn.onset) ++ticks;
        if (f == 599)
        {
            const double expectPos = 8.0 + 600 * 480 / sr * 100.0 / 60.0;
            CHECK (std::abs (sn.beatPos - expectPos) < 0.02, "beatPos %.3f expect %.3f", sn.beatPos, expectPos);
            CHECK (sn.hostClock && std::abs (sn.bpm - 100) < 0.01, "host bpm %.2f clock %d", sn.bpm, (int) sn.hostClock);
        }
    }
    CHECK (ticks == 10, "100 bpm for 6 s -> %d beat ticks (expect 10)", ticks);
    // sync off: host ignored
    st.sync = false;
    const float* ch[2] = { z.data(), z.data() };
    an->process (ch, 2, 480, h);
    CHECK (! fe.consume (an->frames, an->samples, 0.01, st).hostClock, "sync off ignores host");
}

static void testLevelsAndSpectrum()
{
    const double sr = 48000;
    auto an = std::make_unique<Analyzer>(); an->prepare (sr);
    Features fe; fe.setSampleRate (sr);
    Features::Settings st;
    Sig q = sine (sr, 3.0, 1000, 0.01f);   // -40 dBFS
    Snapshot sn;
    for (size_t i = 0; i < q.L.size(); i += 800)
    {
        const float* ch[2] = { q.L.data() + i, q.R.data() + i };
        an->process (ch, 2, 800, {});
        sn = fe.consume (an->frames, an->samples, 800 / sr, st);
    }
    CHECK (sn.mid > 0.6f, "auto-gain: -40 dB 1 kHz -> mid %.2f", sn.mid);
    CHECK (sn.mid > sn.bass * 2 && sn.mid > sn.high, "1 kHz mid %.2f bass %.2f high %.2f", sn.mid, sn.bass, sn.high);
    int pk = 0; for (int b = 1; b < Snapshot::kBands; ++b) if (sn.spec[(size_t) b] > sn.spec[(size_t) pk]) pk = b;
    const double fc = 30.0 * std::pow (16000.0 / 30.0, (pk + 0.5) / Snapshot::kBands);
    CHECK (fc > 800 && fc < 1250, "spectrum peak band %d ~ %.0f Hz", pk, fc);
    CHECK (std::abs (sn.waveL[1023]) <= 0.0101f, "wave history in range");
    // silence afterwards: levels fall to ~0
    std::vector<float> z (800, 0.0f);
    for (int f = 0; f < 120; ++f) { const float* ch[2] = { z.data(), z.data() }; an->process (ch, 2, 800, {}); sn = fe.consume (an->frames, an->samples, 800 / sr, st); }
    CHECK (sn.level < 0.05f && sn.mid < 0.05f, "silence -> level %.3f mid %.3f", sn.level, sn.mid);
    // react scales output
    st.react = 0; const float* ch[2] = { q.L.data(), q.R.data() }; an->process (ch, 2, 800, {});
    CHECK (fe.consume (an->frames, an->samples, 800 / sr, st).mid == 0.0f, "react 0 -> 0");
}

static void testOverflowSafe()
{
    // consumer absent for a long time (editor closed): producer must not block or crash; data afterwards is current
    const double sr = 48000;
    auto an = std::make_unique<Analyzer>(); an->prepare (sr);
    Sig s = sine (sr, 20.0, 440, 0.2f);
    for (size_t i = 0; i < s.L.size(); i += 512) { const float* ch[2] = { s.L.data() + i, s.R.data() + i }; an->process (ch, 2, (int) std::min ((size_t) 512, s.L.size() - i), {}); }
    CHECK (an->frames.size() == Analyzer::kFrameFifo, "frame FIFO full, not overrun (%d)", an->frames.size());
    CHECK (an->samples.size() == Analyzer::kSampleFifo, "sample FIFO full (%d)", an->samples.size());
    Features fe; fe.setSampleRate (sr); Features::Settings st;
    fe.consume (an->frames, an->samples, 0.016, st);
    CHECK (an->frames.size() == 0 && an->samples.size() == 0, "drained");
}

int main()
{
    testFifo(); testBands(); testOnsets(); testTempo(); testHostClock(); testLevelsAndSpectrum(); testOverflowSafe();
    std::printf ("analysis_test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
