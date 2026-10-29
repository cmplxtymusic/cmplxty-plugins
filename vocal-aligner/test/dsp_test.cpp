// Vocal Aligner engine tests: timing correction, pitch correction, edge cases, speed.
#include "../src/AlignDSP.h"
#include <cstdio>
#include <random>
#include <chrono>

using namespace va::dsp;
static int fails = 0;
#define CHECK(c, ...) do { if (! (c)) { ++fails; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Syl { double start, dur, f0, formant; };

// a vowel-ish syllable: harmonics of f0 shaped by one formant, attack/decay envelope, plus a short noise consonant at the onset
static void render (std::vector<float>& out, const std::vector<Syl>& syls, double sr, unsigned seed)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (auto& s : syls)
    {
        const int a = (int) (s.start * sr), n = (int) (s.dur * sr);
        double ph = 0.0;
        for (int i = 0; i < n && a + i < (int) out.size(); ++i)
        {
            const double t = i / sr;
            const double env = std::min (1.0, t / 0.01) * std::min (1.0, (s.dur - t) / 0.04);
            const double f = s.f0 * (1.0 + 0.01 * std::sin (2 * kPi * 5.0 * t));
            ph += f / sr;
            double v = 0.0;
            for (int h = 1; h * s.f0 < 7000.0; ++h)
            {
                const double hf = h * s.f0, w = 1.0 / (1.0 + std::pow ((hf - s.formant) / 300.0, 2.0)) + 0.15 / h;
                v += w * std::sin (2 * kPi * h * ph);
            }
            const double cons = t < 0.03 ? u (rng) * 0.4 * (1.0 - t / 0.03) : 0.0;
            out[(size_t) (a + i)] += (float) (0.12 * env * v + cons);
        }
    }
}

static std::vector<double> onsets (const std::vector<float>& x, double sr)
{
    std::vector<double> o; const int hop = (int) (sr * 0.005); bool on = false;
    for (size_t i = 0; i + hop < x.size(); i += hop)
    {
        double e = 0; for (int k = 0; k < hop; ++k) e += x[i + k] * x[i + k];
        const double db = 10 * std::log10 (1e-12 + e / hop);
        if (! on && db > -30) { o.push_back (i / sr); on = true; }
        else if (on && db < -45) on = false;
    }
    return o;
}

static double onsetError (const std::vector<double>& ref, const std::vector<double>& t)
{
    double sum = 0; int n = 0;
    for (double r : ref) { double b = 1e9; for (double v : t) b = std::min (b, std::fabs (v - r)); if (b < 0.5) { sum += b; ++n; } }
    return n > 0 ? 1000.0 * sum / n : 1e9;
}

static double medianPitchCents (const std::vector<float>& a, const std::vector<float>& b, double sr)
{
    const int hop = (int) (sr * 0.01), fr = (int) (a.size() / hop);
    auto pa = pitchTrack (a.data(), (int) a.size(), sr, hop, fr), pb = pitchTrack (b.data(), (int) b.size(), sr, hop, fr);
    std::vector<double> c;
    for (int i = 0; i < fr; ++i) if (pa[i] > 0 && pb[i] > 0) c.push_back (1200.0 * std::log2 (pb[i] / pa[i]));
    if (c.empty()) return 1e9;
    std::nth_element (c.begin(), c.begin() + c.size() / 2, c.end());
    return c[c.size() / 2];
}

int main()
{
    const double sr = 48000.0;
    std::mt19937 rng (7); std::uniform_real_distribution<double> u (-1, 1);
    std::vector<Syl> g, d;
    double t = 0.3;
    const double notes[] = { 196, 220, 247, 262, 294, 330, 262, 220 };
    for (int i = 0; i < 24; ++i)
    {
        const double dur = 0.18 + 0.12 * (0.5 + 0.5 * u (rng));
        const double f0 = notes[i % 8], fm = 500 + 400 * (i % 3);
        g.push_back ({ t, dur, f0, fm });
        // the dub: early/late by up to 90 ms and 15 % longer/shorter
        d.push_back ({ t + 0.09 * u (rng), dur * (1.0 + 0.15 * u (rng)), f0, fm });
        t += dur + 0.12 + 0.1 * (0.5 + 0.5 * u (rng));
    }
    const int n = (int) ((t + 0.5) * sr);
    std::vector<float> guide ((size_t) n, 0.0f), dub ((size_t) n, 0.0f);
    render (guide, g, sr, 1); render (dub, d, sr, 2);

    const auto og = onsets (guide, sr);
    const double before = onsetError (og, onsets (dub, sr));
    Settings s; s.tightness = 0.8f; s.maxShiftMs = 300.0f;
    auto t0 = std::chrono::steady_clock::now();
    auto R = align (guide.data(), n, dub.data(), n, sr, s);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    CHECK (R.ok && (int) R.audio.size() == n, "align returns a guide-length take");
    const double after = onsetError (og, onsets (R.audio, sr));
    CHECK (before > 25.0, "test dub really is off: mean onset error %.1f ms", before);
    CHECK (after < 12.0, "timing aligned: onset error %.1f ms -> %.1f ms", before, after);
    double pk = 0; for (float v : R.audio) pk = std::max (pk, (double) std::fabs (v));
    CHECK (pk < 1.5 && pk > 0.1, "level kept sane: peak %.2f", pk);
    bool finite = true; for (float v : R.audio) finite = finite && std::isfinite (v);
    CHECK (finite, "output finite");
    CHECK (ms < 4000.0 * (n / sr) / 10.0, "speed: %.0f ms for %.1f s of audio", ms, n / sr);

    // identical takes -> (almost) identity
    auto I = align (guide.data(), n, guide.data(), n, sr, s);
    double maxDev = 0; for (size_t i = 0; i < I.map.size(); ++i) maxDev = std::max (maxDev, (double) std::fabs (I.map[i] - (float) i));
    CHECK (maxDev < 1.5, "same take -> no warp (max %.2f frames)", maxDev);

    // pitch: dub sung 40 cents flat on the same timing
    std::vector<Syl> flat = g; for (auto& sy : flat) sy.f0 *= std::exp2 (-40.0 / 1200.0);
    std::vector<float> fdub ((size_t) n, 0.0f); render (fdub, flat, sr, 3);
    const double c0 = medianPitchCents (guide, fdub, sr);
    Settings p = s; p.pitchAmount = 1.0f;
    auto P = align (guide.data(), n, fdub.data(), n, sr, p);
    const double c1 = medianPitchCents (guide, P.audio, sr);
    CHECK (std::fabs (c0 + 40) < 8, "pitch tracker sees the flat dub: %.1f cents", c0);
    CHECK (std::fabs (c1) < 10, "pitch aligned: %.1f -> %.1f cents", c0, c1);
    Settings h = s; h.pitchAmount = 0.5f;
    auto Hh = align (guide.data(), n, fdub.data(), n, sr, h);
    const double c2 = medianPitchCents (guide, Hh.audio, sr);
    CHECK (std::fabs (c2 + 20) < 10, "pitch amount 50%% -> about half: %.1f cents", c2);
    Settings z = s; z.pitchAmount = 0.0f;
    auto Z = align (guide.data(), n, fdub.data(), n, sr, z);
    CHECK (std::fabs (medianPitchCents (guide, Z.audio, sr) + 40) < 8, "pitch amount 0 leaves pitch alone");

    // tightness 0 still helps but less exactly; different lengths; silence; tiny input; cancel
    Settings loose = s; loose.tightness = 0.0f;
    auto Lr = align (guide.data(), n, dub.data(), n, sr, loose);
    const double afterL = onsetError (og, onsets (Lr.audio, sr));
    CHECK (afterL < before, "low tightness still improves: %.1f ms", afterL);
    auto S = align (guide.data(), n, dub.data(), n / 2, sr, s);
    CHECK (S.ok && (int) S.audio.size() == n, "shorter dub handled");
    std::vector<float> sil ((size_t) n, 0.0f);
    auto Q = align (sil.data(), n, sil.data(), n, sr, s);
    bool qz = Q.ok; for (float v : Q.audio) qz = qz && v == 0.0f;
    CHECK (qz, "silence in -> silence out");
    auto T = align (guide.data(), 10, dub.data(), 10, sr, s);
    CHECK (! T.ok, "too-short input refused");
    std::atomic<bool> cancel { true };
    auto C = align (guide.data(), n, dub.data(), n, sr, s, &cancel);
    CHECK (! C.ok, "cancel stops the job");
    // 44.1k works
    std::vector<float> g44 ((size_t) (n * 44100.0 / sr)), d44 (g44.size());
    render (g44, g, 44100.0, 1); render (d44, d, 44100.0, 2);
    auto R44 = align (g44.data(), (int) g44.size(), d44.data(), (int) d44.size(), 44100.0, s);
    CHECK (onsetError (onsets (g44, 44100.0), onsets (R44.audio, 44100.0)) < 12.0, "44.1 kHz aligned too");

    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
