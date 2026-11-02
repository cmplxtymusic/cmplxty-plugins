// Silt engine tests (plain C++17, no JUCE). Real measurements on rendered audio:
//  root from file names, YIN pitch accuracy, "C plays C" on the rendered output (cents), Cohere tonality, Magnet, scale lock,
//  layer B key match, MPE bend, slices (transient accuracy, key per chop = bit-exact slice audio), sequencer on the host grid,
//  beat repeat period, clicks on every event type, NaN / denormal fuzzing, CPU at 16 voices x 200 grains/s x 500 ms.
// SLT_MEASURE_DIR=<dir> writes the renders as WAVs there (spectrum / waveform plots are made from them by tools/plot_measurements.py).
#include "../src/SiltDSP.h"
#include "../src/SiltSources.h"
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

using namespace slt;
static int fails = 0;
static FILE* report = nullptr;
#define CHECK(c, ...) do { const bool _ok = (c); char _b[512]; std::snprintf (_b, sizeof _b, __VA_ARGS__); if (! _ok) ++fails; std::printf ("%s%s\n", _ok ? "ok:   " : "FAIL: ", _b); if (report) std::fprintf (report, "%s%s\n", _ok ? "ok:   " : "FAIL: ", _b); } while (0)
#define NOTE(...) do { char _b[512]; std::snprintf (_b, sizeof _b, __VA_ARGS__); std::printf ("      %s\n", _b); if (report) std::fprintf (report, "      %s\n", _b); } while (0)

static std::string mdir;
static void writeWav (const std::string& name, const std::vector<float>& L, const std::vector<float>& R, double sr)
{
    if (mdir.empty()) return;
    FILE* f = std::fopen ((mdir + "/" + name).c_str(), "wb"); if (! f) return;
    const int ch = 2; const uint32_t n = (uint32_t) L.size(), bytes = n * ch * 4, srI = (uint32_t) sr, br = srI * ch * 4;
    const uint16_t fmt = 3, chs = (uint16_t) ch, ba = (uint16_t) (ch * 4), bits = 32; const uint32_t sz = 36 + bytes, f16 = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&sz, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&f16, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&chs, 2, 1, f); std::fwrite (&srI, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

static std::shared_ptr<Source> makeSource (Audio a, const std::string& name, float sens = 0.5f)
{
    auto s = std::make_shared<Source>(); s->audio = std::make_shared<Audio> (std::move (a)); s->name = name; analyse (*s, sens); return s;
}
static Audio tone (double midi, double sec, int wave, double sr = 48000.0)   // 0 sine, 1 band-limited saw, 2 square-ish
{
    Audio a; a.rate = sr; a.L.assign ((size_t) (sec * sr), 0.0f);
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    for (size_t i = 0; i < a.L.size(); ++i)
    {
        const double t = i / sr; double v = 0;
        if (wave == 0) v = std::sin (6.283185307 * f * t);
        else for (int h = 1; h * f < sr * 0.45; ++h) { if (wave == 2 && h % 2 == 0) continue; v += std::sin (6.283185307 * f * h * t) / h * 0.6; }
        const double env = std::min (1.0, t / 0.01) * std::min (1.0, (sec - t) / 0.01);
        a.L[i] = (float) (v * 0.5 * env);
    }
    return a;
}

struct Ev { double t; int type; int note; float val; int ch = 0; };   // type 0 on, 1 off, 2 bend, 3 cc
struct Out { std::vector<float> L, R; };

static Params quiet()   // measurement settings: no reverb, nothing random unless the test sets it
{
    Params p; p.rvOn = false; p.spray = 0.05f; p.spread = 0.3f; p.sizeRand = 0.1f; p.velSens = 0; p.aA = 0.01f; p.aR = 0.2f; return p;
}

static Out render (Engine& e, double sec, std::vector<Ev> evs, Transport tr = {}, int block = 256, std::function<void (int)> perBlock = nullptr)
{
    // the engine's 1 ms look-ahead limiter delays the output by latency() samples; like a host's delay compensation, render that
    // much further and drop it from the front, so output sample i belongs to timeline sample i
    const int lat = e.latency();
    const int n = (int) (sec * e.sr) + lat; Out o; o.L.assign ((size_t) n, 0.0f); o.R.assign ((size_t) n, 0.0f);
    std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });
    size_t ei = 0; int pos = 0, blk = 0;
    while (pos < n)
    {
        int m = std::min (block, n - pos);
        while (ei < evs.size() && (int) (evs[ei].t * e.sr) <= pos)
        {
            auto& v = evs[ei++];
            if (v.type == 0) e.noteOn (v.ch, v.note, v.val); else if (v.type == 1) e.noteOff (v.ch, v.note);
            else if (v.type == 2) e.pitchBend (v.ch, v.val); else e.controller (v.ch, v.note, v.val);
        }
        if (ei < evs.size()) m = std::max (1, std::min (m, (int) (evs[ei].t * e.sr) - pos));
        if (perBlock) perBlock (blk);
        Transport t = tr; if (tr.playing) t.ppq = tr.ppq + pos / (60.0 / tr.bpm * e.sr);
        e.render (o.L.data() + pos, o.R.data() + pos, m, t);
        pos += m; ++blk;
    }
    o.L.erase (o.L.begin(), o.L.begin() + lat); o.R.erase (o.R.begin(), o.R.begin() + lat);
    return o;
}

static std::vector<float> mono (const Out& o, double t0, double t1, double sr)
{
    const int a = (int) (t0 * sr), b = std::min ((int) o.L.size(), (int) (t1 * sr));
    std::vector<float> m; for (int i = a; i < b; ++i) m.push_back (0.5f * (o.L[(size_t) i] + o.R[(size_t) i]));
    return m;
}
static double rms (const std::vector<float>& m) { double e = 0; for (float v : m) e += (double) v * v; return std::sqrt (e / std::max<size_t> (1, m.size())); }

// fraction of the spectral energy within +-1.5 % of the harmonics of f0 (a "tonality" figure: 1 = pure harmonic tone, noise ~ 0.1)
static double harmonicity (const std::vector<float>& m, double sr, double f0)
{
    int N = 1; while (N * 2 <= (int) m.size() && N < 65536) N <<= 1;
    std::vector<std::complex<float>> b ((size_t) N);
    for (int i = 0; i < N; ++i) b[(size_t) i] = { m[(size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * i / N)), 0.0f };
    detail::fft (b);
    double tot = 0, harm = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * sr / N, p = std::norm (b[(size_t) k]);
        if (f > 8000) break;
        tot += p;
        const double h = std::round (f / f0);
        if (h >= 1 && std::fabs (f - h * f0) <= 0.015 * h * f0 + 2 * sr / N) harm += p;
    }
    return tot > 0 ? harm / tot : 0;
}

// clicks: peak of the output through a 4th-order 2.5 kHz high-pass, in dBFS. The test tone is a -6 dBFS 110 Hz sine (nothing above 2.5 kHz),
// so anything there is splatter from an edge; a hard cut of the same tone measures about -9 dBFS here.
static int hpPeakAt = 0;
static double hpPeakDb (const Out& o, int a = 0, int b = -1, double sr = 48000)
{
    if (b < 0) b = (int) o.L.size();
    const double w = std::tan (3.14159265 * 2500.0 / sr), q[2] = { 0.5411961, 1.3065630 };
    double pk = 0;
    for (const auto* c : { &o.L, &o.R })
    {
        double z[2][2] = {};
        for (int i = 0; i < b; ++i)
        {
            double x = (*c)[(size_t) i];
            for (int s = 0; s < 2; ++s)
            {   // TPT SVF high-pass, Butterworth pair
                const double k = q[s], a1 = 1 / (1 + w * (w + k)), v3 = x - z[s][1], v1 = a1 * z[s][0] + w * a1 * v3, v2 = z[s][1] + w * v1;
                z[s][0] = 2 * v1 - z[s][0]; z[s][1] = 2 * v2 - z[s][1]; x = x - k * v1 - v2;
            }
            if (i >= a && std::fabs (x) > pk) { pk = std::fabs (x); hpPeakAt = i; }
        }
    }
    return 20 * std::log10 (pk + 1e-12);
}
// worst 2nd difference
static double maxD2 (const Out& o, int a = 2, int b = -1)
{
    if (b < 0) b = (int) o.L.size();
    double mx = 0;
    for (int i = std::max (2, a); i < b; ++i)
        for (const auto* c : { &o.L, &o.R })
            mx = std::max (mx, (double) std::fabs ((*c)[(size_t) i] - 2 * (*c)[(size_t) (i - 1)] + (*c)[(size_t) (i - 2)]));
    return mx;
}
static bool finiteAndSafe (const Out& o, int* subnormals = nullptr, float* peak = nullptr)
{
    bool ok = true; int sub = 0; float pk = 0;
    for (const auto* c : { &o.L, &o.R }) for (float v : *c) { if (! std::isfinite (v)) ok = false; if (v != 0 && std::fpclassify (v) == FP_SUBNORMAL) ++sub; pk = std::max (pk, std::fabs (v)); }
    if (subnormals) *subnormals = sub; if (peak) *peak = pk;
    return ok && pk <= 0.95f;
}

static std::unique_ptr<Engine> engine (const Params& p, const Source* a, const Source* b = nullptr, double sr = 48000)
{
    auto e = std::make_unique<Engine>(); e->prepare (sr); e->setParams (p); e->setSources (a, b); return e;
}
static float outMidi (const Out& o, double t0, double t1, double sr = 48000) { auto m = mono (o, t0, t1, sr); return detectPitch (m, sr).midi; }
// long-FFT pitch: the power-weighted centre of each harmonic's lobe (Hann window, zero-padded to 2^18), lobes searched within
// +-50 cents of h * expected for h = 1..4, combined by power. A grain cloud with random phases has a speckled spectrum: each harmonic
// is a lobe about 1 / grain size wide (7 Hz for 150 ms grains, ~90 cents at C2) with random bumps, so the single highest bin wanders
// by +-15 cents from render to render while the lobe's centre of mass stays on the pitch the grains are played at.
static float specMidi (const Out& o, double t0, double t1, float expect, double sr = 48000, int hMax = 4)
{
    auto m = mono (o, t0, t1, sr);
    const int N = 1 << 18; std::vector<std::complex<float>> b ((size_t) N);
    const int L = std::min ((int) m.size(), N);
    for (int i = 0; i < L; ++i) b[(size_t) i] = { m[(size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * i / (L - 1))), 0.0f };
    detail::fft (b);
    const double f0 = 440.0 * std::pow (2.0, (expect - 69.0) / 12.0);
    double sw = 0, sf = 0;
    for (int h = 1; h <= hMax; ++h)
    {
        const int k0 = (int) std::ceil (h * f0 * std::pow (2.0, -50.0 / 1200) * N / sr), k1 = (int) (h * f0 * std::pow (2.0, 50.0 / 1200) * N / sr);
        if (k1 >= N / 2) break;
        double pw = 0, pf = 0;
        for (int k = k0; k <= k1; ++k) { const double q = std::norm (b[(size_t) k]); pw += q; pf += q * k; }
        if (pw <= 0) continue;
        const double fc = pf / pw * sr / N / h;
        sw += pw; sf += pw * fc;
    }
    if (sw <= 0) return -1;
    return (float) (69.0 + 12.0 * std::log2 (sf / sw / 440.0));
}

int main()
{
    if (const char* d = std::getenv ("SLT_MEASURE_DIR")) { mdir = d; report = std::fopen ((mdir + "/dsp_report.txt").c_str(), "w"); }
    const double SR = 48000;

    // ------------------------------------------------------------------ 1. root from file names
    {
        struct C { const char* n; int pc; int oct; };
        const C cs[] = { { "Pad_C#3.wav", 1, 61 }, { "vox Fmin.wav", 5, -1000 }, { "Keys-A.wav", 9, -1000 }, { "Dark Bass Loop 120.wav", -1, -1000 },
                         { "bell_Eb4_soft.aif", 3, 75 }, { "Choir - a3.flac", 9, 69 }, { "lead_F#m_140bpm.wav", 6, -1000 }, { "Strings Gmaj.wav", 7, -1000 },
                         { "a loop.wav", -1, -1000 }, { "Bb2 sub.wav", 10, 58 }, { "chop 01.wav", -1, -1000 }, { "Silt Glass C", 0, -1000 }, { "Break 100", -1, -1000 },
                         { "Gsharp.wav", 8, -1000 }, { "Pluck_D.wav", 2, -1000 }, { "E-Piano 90 Dm.wav", 2, -1000 } };
        int good = 0;
        for (auto& c : cs)
        {
            int oct = 0; const int pc = rootFromName (c.n, &oct);
            const bool ok = pc == c.pc && (c.pc < 0 || oct == c.oct);
            if (! ok) NOTE ("name '%s' -> pc %d oct %d (want %d %d)", c.n, pc, oct, c.pc, c.oct);
            good += ok;
        }
        CHECK (good == (int) (sizeof cs / sizeof cs[0]), "key from file name: %d / %d names parsed right", good, (int) (sizeof cs / sizeof cs[0]));
    }

    // ------------------------------------------------------------------ 2. YIN accuracy on tones without a key in the name
    {
        double worst = 0; int bad = 0, cnt = 0;
        for (double m = 31; m <= 88; m += 3.37)
            for (int w = 0; w < 3; ++w)
            {
                const auto a = tone (m, 1.5, w);
                const auto r = detectPitch (monoOf (a), a.rate);
                const double c = std::fabs (r.midi - m) * 100; ++cnt;
                if (r.midi < 0 || c > 3) { ++bad; NOTE ("tone %.2f wave %d -> %.3f", m, w, r.midi); }
                worst = std::max (worst, r.midi < 0 ? 9999 : c);
            }
        CHECK (bad == 0, "YIN pitch detection on %d tones (MIDI 31..88, sine / saw / square): worst error %.2f cents", cnt, worst);
    }

    // ------------------------------------------------------------------ 3. built-in sources: analysis
    std::vector<std::shared_ptr<Source>> bi;
    for (int k = 0; k < kNumBuiltIn; ++k) bi.push_back (makeSource (makeBuiltIn (k), builtInName (k)));
    {
        const float want[] = { 48, 45, -1, 60, -1, 60 };
        for (int k = 0; k < kNumBuiltIn; ++k)
        {
            auto& s = *bi[(size_t) k];
            NOTE ("%-22s %.2fs  root %.2f (%s)  detected %.2f conf %.2f  marks %d onsets %d", s.name.c_str(), s.seconds(), s.root, s.rootHow.c_str(), s.pitch.midi, s.pitch.conf, (int) s.marks.size(), (int) s.onsets.size());
            if (want[k] > 0) CHECK (std::fabs (s.root - want[k]) < 0.1f, "built-in '%s' root %.2f = %s (want %s)", s.name.c_str(), s.root, noteName ((int) std::lround (s.root)).c_str(), noteName ((int) want[k]).c_str());
            else CHECK (! s.pitched || k == biBreak ? true : false, "built-in '%s' read as unpitched (root C3)", s.name.c_str());
        }
    }

    // ------------------------------------------------------------------ 4. C plays C (rendered output, cents)
    {
        struct T { std::shared_ptr<Source> s; const char* what; };
        auto off = makeSource (tone (57.37, 3.0, 1), "lead take 2.wav");   // a slightly sharp A2 saw with no key in the name: detection only
        auto nameOnly = makeSource (tone (64.0, 3.0, 2), "Synth E.wav");    // key in the name, octave detected
        std::vector<T> ts = { { bi[biGlass], "Glass" }, { bi[biKeys], "Keys" }, { bi[biChoir], "Choir" }, { off, "untagged saw +37 ct" }, { nameOnly, "'Synth E' square" } };
        double worst[2] = { 0, 0 }, worstY[2] = { 0, 0 }; int n = 0;
        for (auto& t : ts)
            for (int ci = 0; ci < 2; ++ci)
                for (int note : { 48, 60, 67 })
                {
                    auto p = quiet(); p.cohere = (float) ci; p.pos = 0.3f; p.size = 0.15f; p.density = 30;
                    auto e = engine (p, t.s.get());
                    auto o = render (*e, 1.6, { { 0.0, 0, note, 1.0f } });
                    const float got = specMidi (o, 0.4, 1.55, (float) note), yin = outMidi (o, 0.4, 1.55);
                    const double c = std::fabs (got - note) * 100, cy = yin < 0 ? 9999 : std::fabs (yin - note) * 100;
                    worst[ci] = std::max (worst[ci], c); worstY[ci] = std::max (worstY[ci], cy); ++n;
                    if (std::getenv ("SLT_VERBOSE")) NOTE ("%s note %d cohere %d -> spectrum %.3f  YIN %.3f", t.what, note, ci, got, yin);
                    if (note == 60 && ci == 0 && t.s == bi[biGlass]) writeWav ("c_plays_c_glass_C3.wav", o.L, o.R, SR);
                }
        CHECK (worst[1] <= 1.0 && worstY[1] <= 1.0, "C plays C, Cohere 1 (grains phase-locked): %d renders (5 sources incl. an untagged +37 ct sample, notes C2 C3 G3): worst %.2f cents (spectrum), %.2f cents (YIN)", n / 2, worst[1], worstY[1]);
        CHECK (worst[0] <= 8.0 && worstY[0] <= 8.0, "C plays C, Cohere 0 (free cloud, random grain phases): worst %.2f cents (spectrum), %.2f cents (YIN); the two meters scatter in opposite directions on the same render (speckle of a random-phase cloud)", worst[0], worstY[0]);
        // ground truth for the free cloud: isolated grains (2 per second, 200 ms, no overlap) measured one by one
        {
            double wg = 0; int ng = 0;
            for (auto& t : ts)
                if (t.s != bi[biChoir] && t.s != bi[biKeys])   // the choir has a 12 ct vibrato and the keys a 5 ct L/R detune: a single grain catches part of it
                for (int note : { 48, 60, 67 })
                {
                    auto p = quiet(); p.cohere = 0; p.pos = 0.3f; p.size = 0.2f; p.density = 2; p.spray = 0; p.sizeRand = 0;
                    auto e = engine (p, t.s.get());
                    auto o = render (*e, 2.2, { { 0.0, 0, note, 1.0f } });
                    // find each grain: runs of non-silence
                    const auto m = mono (o, 0.0, 2.2, SR);
                    for (size_t i = 0; i < m.size(); )
                    {
                        if (std::fabs (m[i]) < 1e-6f) { ++i; continue; }
                        size_t j = i; size_t quietRun = 0;
                        while (j < m.size() && quietRun < 200) { quietRun = std::fabs (m[j]) < 1e-6f ? quietRun + 1 : 0; ++j; }
                        const size_t len = j - i;
                        if (len > (size_t) (0.15 * SR))
                        {
                            std::vector<float> g (m.begin() + (long) (i + len / 4), m.begin() + (long) (i + len * 3 / 4));
                            const auto r = detectPitch (g, SR);
                            if (r.midi > 0) { wg = std::max (wg, std::fabs (r.midi - note) * 100.0); ++ng; if (std::getenv ("SLT_VERBOSE") && std::fabs (r.midi - note) * 100 > 2) NOTE ("grain %s note %d at %.2f s: %.3f", t.what, note, i / SR, r.midi); }
                        }
                        i = j;
                    }
                }
            CHECK (ng >= 25 && wg <= 2.0, "C plays C, single grains of a free cloud (no overlap, Cohere 0; Glass, untagged saw, 'Synth E'): %d grains measured one by one, worst %.2f cents", ng, wg);
        }
        // Tune Off: a sample is played at its own pitch for C3 only when it is a C3 (sanity check that the mapping is what makes it work)
        auto p = quiet(); p.tune = tuneOff; auto e = engine (p, bi[biChoir].get());
        auto o = render (*e, 1.5, { { 0.0, 0, 60, 1.0f } });
        CHECK (std::fabs (outMidi (o, 0.5, 1.4) - 45) < 0.1f, "Tune Off: the A2 choir plays its own pitch (A2) on C3 (%.2f)", outMidi (o, 0.5, 1.4));
        p.tune = tuneManual; p.root = 57; e = engine (p, bi[biChoir].get());
        o = render (*e, 1.5, { { 0.0, 0, 60, 1.0f } });
        CHECK (std::fabs (outMidi (o, 0.5, 1.4) - 48) < 0.1f, "Tune Manual root A3 on an A2 sample: C3 plays C2 (%.2f)", outMidi (o, 0.5, 1.4));
    }

    // ------------------------------------------------------------------ 5. Cohere: a jittery cloud firms up into a tone
    {
        auto p = quiet(); p.pitchJit = 0.6f; p.spray = 0.5f; p.sizeRand = 0.6f; p.size = 0.08f; p.density = 60; p.pos = 0.4f; p.spread = 0.4f;
        double h[3]; const float cs[] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            p.cohere = cs[i]; auto e = engine (p, bi[biChoir].get());
            auto o = render (*e, 2.2, { { 0.0, 0, 57, 1.0f } });
            h[i] = harmonicity (mono (o, 0.4, 2.1, SR), SR, 220.0);
            writeWav (std::string ("cohere_") + (i == 0 ? "0" : i == 1 ? "50" : "100") + ".wav", o.L, o.R, SR);
        }
        CHECK (h[2] > h[0] + 0.1 && h[1] > h[0], "Cohere: harmonic energy %.1f %% (0) -> %.1f %% (0.5) -> %.1f %% (1) with 0.6 st jitter, 50 %% spray", h[0] * 100, h[1] * 100, h[2] * 100);
        // and on a plain sine sample: cohere 1 keeps a short-grain cloud phase-coherent (no comb notches / beating)
        auto sine = makeSource (tone (57, 3.0, 0), "sine A2.wav");
        p = quiet(); p.size = 0.03f; p.density = 120; p.spray = 0.3f;
        double hs[2];
        for (int i = 0; i < 2; ++i) { p.cohere = (float) i; auto e = engine (p, sine.get()); auto o = render (*e, 2.0, { { 0.0, 0, 57, 1.0f } }); hs[i] = harmonicity (mono (o, 0.4, 1.9, SR), SR, 220.0); }
        CHECK (hs[1] > 0.97 && hs[1] >= hs[0], "Cohere on 30 ms grains of a sine: %.1f %% -> %.1f %% of the energy on the pitch", hs[0] * 100, hs[1] * 100);
    }

    // ------------------------------------------------------------------ 6. Magnet pulls grain starts onto transients
    {
        auto& br = *bi[biBreak];
        double dist[2];
        for (int i = 0; i < 2; ++i)
        {
            auto p = quiet(); p.magnet = (float) i; p.spray = 1.0f; p.density = 40; p.size = 0.06f;
            auto e = engine (p, &br);
            std::vector<float> starts;
            render (*e, 3.0, { { 0.0, 0, 60, 1.0f } }, {}, 256, [&] (int) {});
            for (auto& t : e->taps) if (t.stamp.load() > 0) starts.push_back (t.pos.load());
            double s = 0; for (float x : starts) { const int pos = (int) (x * br.len()); s += std::fabs (pos - br.nearestOnset (pos)) / br.audio->rate * 1000.0; }
            dist[i] = s / std::max<size_t> (1, starts.size());
        }
        CHECK (dist[1] < 4.0 && dist[0] > 20.0, "Magnet: mean distance from a grain start to the nearest transient %.1f ms (off) -> %.1f ms (full)", dist[0], dist[1]);
    }

    // ------------------------------------------------------------------ 7. scale lock, glide, layer B key match, MPE
    {
        auto p = quiet(); p.scale = scMajor; p.key = 0;
        auto e = engine (p, bi[biGlass].get());
        auto o = render (*e, 1.5, { { 0.0, 0, 61, 1.0f } });
        const float g1 = outMidi (o, 0.4, 1.4);
        p.scale = scPentMin; p.key = 9; e = engine (p, bi[biGlass].get());   // A minor pentatonic: A C D E G; F (65) -> E (64)
        o = render (*e, 1.5, { { 0.0, 0, 65, 1.0f } });
        const float g2 = outMidi (o, 0.4, 1.4);
        CHECK (std::fabs (g1 - 60) < 0.05f && std::fabs (g2 - 64) < 0.05f, "scale lock: C#3 in C major plays %.2f (C3), F3 in A minor pentatonic plays %.2f (E3)", g1, g2);

        p = quiet(); p.voices = 1; p.glide = 0.3f; p.cohere = 1;
        e = engine (p, bi[biGlass].get());
        float mid = -1; int nv = 0;
        o = render (*e, 2.4, { { 0.0, 0, 48, 1.0f }, { 1.0, 0, 60, 1.0f }, { 1.02, 1, 48, 0 } }, {}, 256,
                    [&] (int blk) { if (blk == (int) (1.1 * 48000 / 256)) mid = e->newestPitch (&nv); });
        const float a = specMidi (o, 0.3, 0.95, 48), b = specMidi (o, 1.6, 2.35, 60);
        CHECK (std::fabs (a - 48) < 0.03f && std::fabs (b - 60) < 0.03f && mid > 49 && mid < 59 && nv == 1, "mono glide 300 ms: C2 %.2f -> %.2f at 100 ms -> C3 %.2f (one legato voice)", a, mid, b);

        p = quiet(); p.bOn = true; p.morphMode = mmSwap; p.morph = 1.0f; p.keyMatch = true; p.cohere = 1;
        e = engine (p, bi[biGlass].get(), bi[biKeys].get());
        o = render (*e, 1.4, { { 0.0, 0, 55, 1.0f } });
        const float km = specMidi (o, 0.15, 1.3, 55);
        p.keyMatch = false; e = engine (p, bi[biGlass].get(), bi[biKeys].get());
        o = render (*e, 1.4, { { 0.0, 0, 55, 1.0f } });
        const float nk = specMidi (o, 0.15, 1.3, 67);
        CHECK (std::fabs (km - 55) < 0.03f && std::fabs (nk - 67) < 0.03f && std::fabs (outMidi (o, 0.15, 1.3) - 67) < 0.3f, "layer B key match: Keys (C4 root) behind Glass (C3) plays G3 %.2f matched, %.2f (an octave up) unmatched", km, nk);
        p.morphMode = mmBlend; p.morph = 0.5f; p.keyMatch = true; e = engine (p, bi[biGlass].get(), bi[biKeys].get());
        o = render (*e, 1.4, { { 0.0, 0, 60, 1.0f } });
        CHECK (std::fabs (specMidi (o, 0.15, 1.3, 60) - 60) < 0.03f, "layer B blend 50 %%: still C3 (%.3f)", specMidi (o, 0.15, 1.3, 60));
        for (int mm : { mmRing, mmCross }) { p.morphMode = mm; e = engine (p, bi[biGlass].get(), bi[biAir].get()); o = render (*e, 1.4, { { 0.0, 0, 60, 1.0f } }); float pk = 0; CHECK (finiteAndSafe (o, nullptr, &pk) && rms (mono (o, 0.2, 1.2, SR)) > 0.01, "morph %s with an unpitched B: level %.3f rms, peak %.2f", mm == mmRing ? "Ring" : "Cross", rms (mono (o, 0.2, 1.2, SR)), pk); }

        p = quiet(); p.mpe = true; e = engine (p, bi[biGlass].get());
        o = render (*e, 1.6, { { 0.0, 2, 0, 0.5f, 1 }, { 0.0, 0, 60, 1.0f, 1 }, { 0.0, 2, 0, 0.0f, 2 }, { 0.0, 0, 60, 1.0f, 2 }, { 0.8, 1, 60, 0, 2 } });
        const float up = outMidi (o, 1.0, 1.5);
        CHECK (std::fabs (up - 84) < 0.05f, "MPE: member-channel bend +0.5 (48 st range) lifts only that voice: %.2f (C5) once the unbent voice is released", up);
    }

    // ------------------------------------------------------------------ 8. slices
    {
        auto& br = *bi[biBreak];
        const double spb = 0.6 * br.audio->rate;
        std::vector<double> ref;
        for (int s = 0; s < 32; ++s) { const bool hit = s % 2 == 0 || s == 15 || s == 31 || s == 29; if (hit) ref.push_back (s * spb / 4); }
        auto sl = autoSlices (br, kSlTransients);
        int m = 0; double worst = 0;
        int missKS = 0, missHat = 0;
        for (double r : ref)
        {
            double bd = 1e9; for (int x : sl) bd = std::min (bd, std::fabs (std::max (0.0, x + 0.002 * br.audio->rate) - r));
            const int st = (int) std::lround (r / (spb / 4));
            const bool hatOnly = st % 2 == 0 && st % 4 != 0 && st != 6 && st != 10 && st != 22 && st != 26 && st != 0 && st != 16;   // even 16ths without kick / snare
            if (bd < 0.005 * br.audio->rate || (r == 0 && bd < 0.0025 * br.audio->rate)) { ++m; worst = std::max (worst, r == 0 ? 0 : bd); }
            else { NOTE ("missed hit at 16th %d (%s)", st, hatOnly ? "hat only" : "kick / snare"); (hatOnly ? missHat : missKS)++; }
        }
        CHECK (missKS == 0 && missHat <= 1 && (int) sl.size() <= (int) ref.size() + 1, "transient slices on the break: %d / %d hits found (every kick and snare; %d quiet hat missed), %d slices, worst %.2f ms off", m, (int) ref.size(), missHat, (int) sl.size(), worst / br.audio->rate * 1000);
        // key per chop: C1 + k plays chop k bit-exactly (One Shot, 0 st, centre pan, no fx)
        auto p = quiet(); p.mode = modeSlice; p.slPlay = slOneShot; p.levelDb = 0; p.aA = 0;
        auto e = engine (p, &br); e->setSlices (sl.data(), (int) sl.size());
        auto o = render (*e, 0.4, { { 0.0, 0, 36 + 3, 1.0f } });
        const int s0 = sl[3], s1 = sl[4], fade = (int) (0.008 * SR);
        double err = 0; for (int i = fade + 2; i < s1 - s0 - fade - 2; ++i) err = std::max (err, (double) std::fabs (o.L[(size_t) i] - br.audio->L[(size_t) (s0 + i)]));
        double tail = 0; for (int i = s1 - s0 + 2; i < (int) o.L.size(); ++i) tail = std::max (tail, (double) std::fabs (o.L[(size_t) i]));
        CHECK (err < 1e-5 && tail == 0, "key per chop: D#1 (C1 + 3) plays chop 4 sample-exact after the 8 ms fade-in (max error %.2g), silent after it ends", err);
        const int lastIdx = (int) sl.size();
        e->noteOn (0, 36 + lastIdx, 1.0f); CHECK (e->activeVoices() == 0, "a key past the last chop (C1 + %d) plays nothing", lastIdx);
        // grid slices
        CHECK ((int) autoSlices (br, kSlGrid16).size() == 16 && autoSlices (br, kSlGrid16)[4] == br.len() * 4 / 16, "grid 16: 16 equal chops");
    }

    // ------------------------------------------------------------------ 9. sequencer on the host grid
    {
        auto& br = *bi[biBreak];
        auto sl = autoSlices (br, kSlGrid16);
        auto p = quiet(); p.mode = modeSlice; p.seq = true; p.seqRate = 2; p.seqTrig = 1; p.slPlay = slGate; p.seqGate = 0.5f; p.aR = 0.01f;
        auto e = engine (p, &br); e->setSlices (sl.data(), (int) sl.size());
        int pat[16]; for (int i = 0; i < 16; ++i) pat[i] = 0; e->setPattern (pat);   // every step = chop 1 (the kick): clean onsets
        Transport tr; tr.playing = true; tr.bpm = 120; tr.ppq = 0.37;              // starts mid-beat
        auto o = render (*e, 2.0, {}, tr, 511);
        Audio a; a.rate = SR; a.L = mono (o, 0, 2.0, SR);
        auto on = detectOnsets (a, 0.5f);
        const double stepS = 0.25 * 0.5 * SR;   // 1/16 at 120 = 6000 samples
        double worst = 0; int n = 0;
        for (int x : on) { const double beatPos = 0.37 + x / (0.5 * SR); const double k = std::round (beatPos / 0.25); worst = std::max (worst, std::fabs (beatPos - k * 0.25) * 0.5 * SR); ++n; }
        CHECK (n >= 13 && worst < 0.0015 * SR, "sequencer 1/16 at 120 BPM from ppq 0.37: %d steps, onsets within %.2f ms of the host grid", n, worst / SR * 1000);
        writeWav ("seq_host_grid.wav", o.L, o.R, SR);
        (void) stepS;
        // hold key: transposes, stops when released
        p.seqTrig = 0; p.slPlay = slGranular; e = engine (p, &br); e->setSlices (sl.data(), (int) sl.size());
        for (int i = 0; i < 16; ++i) pat[i] = i % 4; e->setPattern (pat);
        o = render (*e, 1.5, { { 0.1, 0, 60, 1.0f }, { 0.9, 1, 60, 0 } });
        const double after = rms (mono (o, 1.2, 1.5, SR)), during = rms (mono (o, 0.2, 0.8, SR));
        CHECK (during > 0.02 && after < during * 0.01, "sequencer (hold key): runs while the key is held (%.3f rms), stops after release (%.5f)", during, after);
    }

    // ------------------------------------------------------------------ 10. beat repeat
    {
        auto p = quiet(); p.rpOn = true; p.rpChance = 1.0f; p.rpLen = 2; p.rpMix = 1.0f; p.spray = 0.6f; p.pitchJit = 0.5f;
        auto e = engine (p, bi[biAir].get());
        Transport tr; tr.playing = true; tr.bpm = 120; tr.ppq = 0;
        auto o = render (*e, 2.2, { { 0.0, 0, 60, 1.0f } }, tr);
        const int beat = (int) (0.5 * SR), loop = beat / 4;
        double err = 0, lvl = 0;
        for (int b = 1; b < 4; ++b)
            for (int i = b * beat + loop + 400; i < b * beat + 2 * loop - 400; ++i)
            { err = std::max (err, (double) std::fabs (o.L[(size_t) (i + loop)] - o.L[(size_t) i])); lvl = std::max (lvl, (double) std::fabs (o.L[(size_t) i])); }
        CHECK (err < 1e-6 && lvl > 0.01, "beat repeat 1/16 at 120 BPM: the rest of each beat repeats its first 16th (period error %.2g, level %.3f)", err, lvl);
        writeWav ("beat_repeat.wav", o.L, o.R, SR);
    }

    // ------------------------------------------------------------------ 11. clicks: every kind of event, on a sine sample at its root
    {
        auto sine = makeSource (tone (45, 4.0, 0), "sine A1.wav");
        auto base = quiet(); base.spray = 0; base.spread = 0; base.size = 0.2f; base.density = 20; base.sizeRand = 0; base.cohere = 1.0f;
        auto e = engine (base, sine.get());
        auto steady = render (*e, 1.6, { { 0.0, 0, 45, 1.0f } });
        const double ref = hpPeakDb (steady, (int) (0.5 * SR), (int) (1.5 * SR));
        { Out cut = steady; for (size_t i = (size_t) (0.8 * SR) + 37; i < cut.L.size(); ++i) cut.L[i] = cut.R[i] = 0; NOTE ("click meter: steady tone %.1f dBFS, the same tone hard-cut %.1f dBFS", ref, hpPeakDb (cut)); }
        for (int nn : { 57, 69 }) { auto e2 = engine (base, sine.get()); auto st2 = render (*e2, 1.6, { { 0.0, 0, nn, 1.0f } }); NOTE ("click meter: steady note %d %.1f dBFS", nn, hpPeakDb (st2, (int) (0.5 * SR), (int) (1.5 * SR))); }
        struct Case { const char* what; Params p; std::vector<Ev> ev; bool swap; };
        std::vector<Case> cs;
        { auto p = base; p.aA = 0.0f; p.aR = 0.0f; cs.push_back ({ "zero attack / release", p, { { 0.1, 0, 45, 1.0f }, { 0.6, 1, 45, 0 } }, false }); }
        { auto p = base; p.voices = 2; cs.push_back ({ "voice stealing (12 notes, 2 voices)", p, {}, false });
          for (int i = 0; i < 12; ++i) cs.back().ev.push_back ({ 0.05 + i * 0.09, 0, 45 + (i % 3) * 12, 1.0f }); }
        { auto p = base; p.mode = modeSlice; p.slPlay = slOneShot; cs.push_back ({ "slice one shots cut mid-cycle", p, { { 0.1, 0, 36, 1 }, { 0.2, 0, 37, 1 }, { 0.25, 0, 38, 1 }, { 0.4, 0, 39, 1 } }, false }); }
        { auto p = base; p.mode = modeSlice; p.slPlay = slGate; p.aR = 0.003f; cs.push_back ({ "slice gate, short release", p, { { 0.1, 0, 37, 1 }, { 0.25, 1, 37, 0 } }, false }); }
        { auto p = base; p.rpOn = true; p.rpChance = 0.7f; p.rpLen = 3; cs.push_back ({ "beat repeat in and out", p, { { 0.0, 0, 45, 1 } }, false }); }
        { auto p = base; cs.push_back ({ "sample swapped while playing", p, { { 0.0, 0, 45, 1 } }, true }); }
        { auto p = base; p.mode = modeSlice; p.seq = true; p.seqTrig = 1; p.slPlay = slGate; cs.push_back ({ "sequencer steps (choke)", p, {}, false }); }
        { auto p = base; p.reverse = 1.0f; p.cohere = 0; cs.push_back ({ "reverse grains", p, { { 0.0, 0, 45, 1 } }, false }); }
        auto sine2 = makeSource (tone (45, 4.0, 0), "sine A1 b.wav");
        auto sl = autoSlices (*sine, kSlGrid8);
        for (auto& c : cs)
        {
            auto ce = engine (c.p, sine.get()); ce->setSlices (sl.data(), (int) sl.size());
            Transport tr; tr.playing = c.p.seq || c.p.rpOn; tr.bpm = 120;
            auto o = render (*ce, 1.6, c.ev, tr, 128, [&] (int blk) { if (c.swap && blk == 300) ce->setSources (sine2.get(), nullptr); });
            const double hp = hpPeakDb (o);
            if (std::getenv ("SLT_VERBOSE")) { float pk = 0; finiteAndSafe (o, nullptr, &pk); NOTE ("%s: peak at %.4f s, output peak %.3f", c.what, hpPeakAt / SR, pk); }
            CHECK (hp < -50.0, "no clicks - %s: high-pass peak %.1f dBFS (limit -50)", c.what, hp);
        }
    }

    // ------------------------------------------------------------------ 12. NaN / denormal fuzz
    {
        FastRng r; r.s = 1234;
        int bad = 0, sub = 0; float worstPk = 0;
        for (int it = 0; it < 120; ++it)
        {
            Params p;
            p.pos = r.uni(); p.scan = r.sym() * 2; p.density = 0.5f + r.uni() * 199.5f; p.size = 0.01f + r.uni() * 0.49f; p.sizeRand = r.uni(); p.pitch = r.sym() * 24;
            p.pitchJit = r.uni() * 24; p.spray = r.uni(); p.spread = r.uni(); p.shape = r.uni(); p.reverse = r.uni(); p.magnet = r.uni(); p.gcomp = r.uni();
            p.cohere = r.uni(); p.scale = (int) (r.uni() * kNumScales); p.voices = 1 + (int) (r.uni() * 16); p.glide = r.uni() * 0.5f;
            p.bOn = r.uni() < 0.5f; p.morphMode = (int) (r.uni() * 4); p.morph = r.uni(); p.bPitch = r.sym() * 12;
            p.fOn = r.uni() < 0.6f; p.fType = (int) (r.uni() * 4); p.cutoff = 20 + r.uni() * 19000; p.res = r.uni(); p.fRand = r.uni(); p.fKey = r.uni();
            p.mode = r.uni() < 0.3f ? modeSlice : modeGranular; p.slPlay = (int) (r.uni() * 3); p.seq = r.uni() < 0.3f;
            p.rpOn = r.uni() < 0.5f; p.rpChance = r.uni(); p.cpOn = r.uni() < 0.5f; p.cpThr = -40 + r.uni() * 40; p.cpRatio = 1 + r.uni() * 19; p.cpGain = r.uni() * 18;
            p.dlOn = r.uni() < 0.5f; p.dlFb = r.uni() * 0.95f; p.dlMix = r.uni(); p.rvOn = r.uni() < 0.6f; p.rvSize = r.uni(); p.rvMix = r.uni(); p.levelDb = r.sym() * 12;
            p.fxOrder = (int) (r.uni() * 24);
            for (int k = 0; k < kModSlots; ++k) { p.modSrc[k] = (int) (r.uni() * kNumModSrc); p.modDst[k] = (int) (r.uni() * kNumModDst); p.modAmt[k] = r.sym(); }
            p.l1Sync = (int) (r.uni() * 16); p.l1Rate = r.uni() * 20; p.l1Shape = (int) (r.uni() * 6); p.l2Shape = (int) (r.uni() * 6);
            auto e = engine (p, bi[(size_t) (it % kNumBuiltIn)].get(), bi[(size_t) ((it + 2) % kNumBuiltIn)].get());
            auto sl = autoSlices (*bi[(size_t) (it % kNumBuiltIn)], kSlTransients); e->setSlices (sl.data(), (int) sl.size());
            Transport tr; tr.playing = it % 2; tr.bpm = 60 + r.uni() * 120;
            std::vector<Ev> ev; for (int k = 0; k < 6; ++k) { const double t = r.uni() * 0.6; const int n = 24 + (int) (r.uni() * 72); ev.push_back ({ t, 0, n, r.uni() }); ev.push_back ({ t + r.uni() * 0.5, 1, n, 0 }); }
            ev.push_back ({ 0.3, 2, 0, r.sym() }); ev.push_back ({ 0.35, 3, 1, r.uni() }); ev.push_back ({ 0.4, 3, 74, r.uni() });
            auto o = render (*e, 1.0, ev, tr, 1 + (int) (r.uni() * 700));
            int s = 0; float pk = 0;
            if (! finiteAndSafe (o, &s, &pk)) ++bad;
            sub += s; worstPk = std::max (worstPk, pk);
        }
        CHECK (bad == 0 && sub == 0, "fuzz: 120 random patches (every mode, mod matrix, fx order, odd block sizes): %d with NaN / over -0.5 dBFS, %d denormal samples, peak %.3f", bad, sub, worstPk);
        // long tail with a big reverb + delay: no denormal slow-down, output reaches exact silence
        Params p; p.rvOn = true; p.rvSize = 1; p.rvMix = 1; p.dlOn = true; p.dlFb = 0.9f; p.dlMix = 1; p.fOn = true; p.res = 1;
        auto e = engine (p, bi[biGlass].get());
        auto t0 = std::chrono::steady_clock::now();
        auto o = render (*e, 2.0, { { 0.0, 0, 60, 1.0f }, { 0.5, 1, 60, 0 } });
        auto t1 = std::chrono::steady_clock::now();
        auto tailO = render (*e, 60.0, {});
        auto t2 = std::chrono::steady_clock::now();
        float lastPk = 0; for (size_t i = tailO.L.size() - (size_t) SR; i < tailO.L.size(); ++i) lastPk = std::max (lastPk, std::fabs (tailO.L[i]));
        int s = 0; finiteAndSafe (tailO, &s);
        const double msPerSecTail = std::chrono::duration<double, std::milli> (t2 - t1).count() / 60.0;
        CHECK (s == 0 && lastPk < 1e-6f, "60 s reverb / 90 %% delay tail with FTZ off: %d denormal samples, last second peak %.2g, %.2f ms CPU per second of tail", s, lastPk, msPerSecTail);
        (void) t0; (void) o;
    }

    // ------------------------------------------------------------------ 13. CPU
    {
        auto run = [&] (const char* what, Params p, const Source* a, const Source* b, int notes, double* rtOut)
        {
            auto e = engine (p, a, b);
            std::vector<Ev> ev; for (int i = 0; i < notes; ++i) ev.push_back ({ 0.0, 0, 40 + i * 3, 0.9f });
            render (*e, 0.6, ev);   // warm up: every voice at its full grain count
            const int g = e->activeGrains(), v = e->activeVoices();
            // best of 3 runs of 4 s (the box is shared with other jobs; the fastest run is the closest to the real cost)
            Out o; double ms = 1e30;
            for (int k = 0; k < 3; ++k)
            {
                auto t0 = std::chrono::steady_clock::now();
                o = render (*e, 4.0, {}, {}, 256);
                ms = std::min (ms, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() * 2.5);
            }
            const double rt = ms / 10000.0 * 100.0;
            *rtOut = rt;
            NOTE ("CPU %-58s %2d voices, %4d grains: %.1f %% of one core (%.2f ms per 256-sample block at 48 kHz)", what, v, g, rt, ms / (10.0 * SR / 256));
            int s = 0; float pk = 0; const bool ok = finiteAndSafe (o, &s, &pk);
            if (std::string (what).find ("16 voices") != std::string::npos) writeWav ("cpu_stress_16v.wav", o.L, o.R, SR);
            return ok;
        };
        Params st; st.voices = 16; st.density = 200; st.size = 0.5f; st.sizeRand = 0; st.aS = 1; st.fOn = true; st.bOn = true; st.morphMode = mmBlend; st.pitchJit = 0.3f;
        st.rpOn = true; st.cpOn = true; st.dlOn = true; st.rvOn = true; st.spray = 0.3f;
        double worst, typ, sl;
        const bool ok1 = run ("16 voices x 200 grains/s x 500 ms, filter + layer B + all fx", st, bi[biChoir].get(), bi[biGlass].get(), 16, &worst);
        Params ty; ty.voices = 8; const bool ok2 = run ("typical pad: 6 notes, 24 grains/s x 120 ms, reverb", ty, bi[biGlass].get(), nullptr, 6, &typ);
        Params sp = st; sp.bOn = false; sp.fOn = false; const bool ok3 = run ("16 voices x 200/s x 500 ms, A only, no filter", sp, bi[biChoir].get(), nullptr, 16, &sl);
        CHECK (ok1 && ok2 && ok3 && worst < 60.0, "CPU: worst case %.1f %%, plain stress %.1f %%, typical %.1f %% of one core (best of 3 runs; budget: worst case < 60 %%)", worst, sl, typ);
    }

    std::printf ("\n%s (%d failures)\n", fails ? "DSP TESTS FAILED" : "ALL DSP TESTS PASSED", fails);
    if (report) { std::fprintf (report, "\n%s (%d failures)\n", fails ? "DSP TESTS FAILED" : "ALL DSP TESTS PASSED", fails); std::fclose (report); }
    return fails ? 1 : 0;
}
