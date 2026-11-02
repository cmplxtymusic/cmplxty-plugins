#pragma once
// CMPLXTY Silt - built-in sources for the factory "Sounds" presets (generated, so the installer stays small and
// there are no sample licences). Deterministic: the same audio on every machine.
#include "SiltAnalysis.h"

namespace slt
{
enum BuiltIn { biGlass = 0, biChoir, biBreak, biKeys, biAir, biPluck, kNumBuiltIn };
inline const char* builtInName (int k)
{
    static const char* n[] = { "Silt Glass C", "Silt Choir A", "Silt Break 100", "Silt Keys C", "Silt Air", "Silt Pluck Phrase Cm" };
    return k >= 0 && k < kNumBuiltIn ? n[k] : "";
}

namespace detail
{
struct SRng { uint32_t s; explicit SRng (uint32_t x) : s (x | 1u) {} float sym() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; } };
inline double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
inline void normalise (Audio& a, float peak)
{
    float mx = 1e-9f; for (float v : a.L) mx = std::max (mx, std::fabs (v)); for (float v : a.R) mx = std::max (mx, std::fabs (v));
    const float g = peak / mx; for (auto& v : a.L) v *= g; for (auto& v : a.R) v *= g;
}
}

inline Audio makeBuiltIn (int k)
{
    using namespace detail;
    Audio a; a.rate = 48000.0;
    const double sr = a.rate, tp = 6.283185307179586;
    auto alloc = [&] (double sec, bool st) { a.L.assign ((size_t) (sec * sr), 0.0f); if (st) a.R.assign (a.L.size(), 0.0f); };
    switch (k)
    {
        case biGlass:   // C3: soft additive glass pad, two slightly detuned layers panned apart, slow brightness swell
        {
            alloc (4.0, true); const double f0 = midiHz (48);
            for (int side = 0; side < 2; ++side)
            {
                auto& o = side ? a.R : a.L; const double det = side ? 1.0012 : 0.9988;
                for (int h = 1; h <= 14; ++h)
                {
                    const double f = f0 * h * det; if (f > 18000) break;
                    double ph = h * 0.37 + side;
                    for (size_t i = 0; i < o.size(); ++i)
                    {
                        const double t = (double) i / sr;
                        const double bright = 0.35 + 0.65 * std::sin (3.14159 * std::min (1.0, t / 4.0));
                        const double amp = std::pow ((double) h, -1.3 - 1.2 * (1 - bright)) * (h % 2 ? 1.0 : 0.6);
                        const double env = std::min (1.0, t / 0.25) * std::min (1.0, (4.0 - t) / 0.3);
                        o[i] += (float) (amp * env * std::sin (ph)); ph += tp * f / sr;
                    }
                }
            }
            normalise (a, 0.7f); break;
        }
        case biChoir:   // A2: formant "ah" -> "oh" voice with vibrato and breath
        {
            alloc (5.0, true); const double f0 = midiHz (45); SRng rn (77);
            const double F1a = 800, F2a = 1150, F1o = 450, F2o = 800, F3 = 2800;
            for (int side = 0; side < 2; ++side)
            {
                auto& o = side ? a.R : a.L; double ph[40] {}; double vph = side * 1.3;
                for (size_t i = 0; i < o.size(); ++i)
                {
                    const double t = (double) i / sr, v = std::min (1.0, t / 5.0);
                    vph += tp * 5.1 / sr;
                    const double f = f0 * std::pow (2.0, (0.12 * std::sin (vph) * std::min (1.0, t / 0.8)) / 12.0) * (side ? 1.003 : 0.997);
                    const double F1 = F1a + (F1o - F1a) * v, F2 = F2a + (F2o - F2a) * v;
                    double s = 0;
                    for (int h = 1; h <= 40; ++h)
                    {
                        const double fh = f * h; if (fh > 9000) break;
                        auto res = [&] (double F, double bw, double g) { const double d = (fh - F) / bw; return g / (1 + d * d); };
                        const double amp = (res (F1, 90, 1.0) + res (F2, 120, 0.5) + res (F3, 200, 0.18)) / h * 3;
                        s += amp * std::sin (ph[h - 1]); ph[h - 1] += tp * fh / sr;
                    }
                    const double env = std::min (1.0, t / 0.35) * std::min (1.0, (5.0 - t) / 0.4);
                    o[i] = (float) ((s + 0.02 * rn.sym()) * env);
                }
            }
            normalise (a, 0.7f); break;
        }
        case biBreak:   // 2 bars at 100 BPM: kick, snare, hats
        {
            const double spb = 60.0 / 100.0 * sr; alloc (8 * 60.0 / 100.0, true); SRng rn (5);
            auto kick = [&] (double at, float g)
            {
                double ph = 0;
                for (int i = 0; i < (int) (0.45 * sr); ++i) { const size_t j = (size_t) at + (size_t) i; if (j >= a.L.size()) break; const double t = i / sr;
                    const double f = 45 + 110 * std::exp (-t * 30); ph += tp * f / sr; const double fo = std::min (1.0, (0.45 - t) / 0.04);   // fades out over the last 40 ms (no cut at the end)
                    const float v = (float) (std::sin (ph) * std::exp (-t * 7) * g * fo); a.L[j] += v; a.R[j] += v; }
            };
            auto snare = [&] (double at, float g)
            {
                double ph = 0; float lp = 0;
                for (int i = 0; i < (int) (0.3 * sr); ++i) { const size_t j = (size_t) at + (size_t) i; if (j >= a.L.size()) break; const double t = i / sr;
                    ph += tp * 190 / sr; const float nz = rn.sym(); lp += (nz - lp) * 0.5f; const float hp = nz - lp;
                    const double fo = std::min (1.0, (0.3 - t) / 0.03);
                    const float v = (float) ((0.5 * std::sin (ph) * std::exp (-t * 25) + 0.7 * hp * std::exp (-t * 14)) * g * fo); a.L[j] += v; a.R[j] += v * 0.95f; }
            };
            auto hat = [&] (double at, float g, double dec)
            {
                float lp = 0;
                for (int i = 0; i < (int) (0.2 * sr); ++i) { const size_t j = (size_t) at + (size_t) i; if (j >= a.L.size()) break; const double t = i / sr;
                    const float nz = rn.sym(); lp += (nz - lp) * 0.8f; const double fo = std::min (1.0, (0.2 - t) / 0.02);
                    const float v = (float) ((nz - lp) * std::exp (-t * dec) * g * fo); a.L[j] += v * 0.8f; a.R[j] += v; }
            };
            const int kp[] = { 0, 6, 10, 16, 22, 26, 29 };
            for (int s : kp) kick (s * spb / 4, 0.9f);
            for (int s : { 4, 12, 20, 28 }) snare (s * spb / 4, 0.7f);
            for (int s : { 15, 31 }) snare (s * spb / 4, 0.22f);
            for (int s = 0; s < 32; s += 2) hat (s * spb / 4, s % 4 ? 0.18f : 0.28f, s == 14 || s == 30 ? 8 : 45);
            normalise (a, 0.8f); break;
        }
        case biKeys:    // C4: two-operator FM electric piano, 3.5 s decay
        {
            alloc (3.5, true); const double f0 = midiHz (60);
            for (int side = 0; side < 2; ++side)
            {
                auto& o = side ? a.R : a.L; double pc = 0, pm = 0, pt = side * 0.5;
                for (size_t i = 0; i < o.size(); ++i)
                {
                    const double t = (double) i / sr;
                    const double idx = 1.6 * std::exp (-t * 3.0) + 0.25;
                    pm += tp * f0 / sr; pc += tp * f0 * (side ? 1.0015 : 0.9985) / sr; pt += tp * 4.5 / sr;
                    const double tine = 0.12 * std::sin (tp * f0 * 7.0 * t) * std::exp (-t * 18);
                    const double env = std::min (1.0, t / 0.003) * std::exp (-t * 1.1) * std::min (1.0, (3.5 - t) / 0.2);
                    o[i] = (float) ((std::sin (pc + idx * std::sin (pm)) + tine) * env * (1.0 + 0.15 * std::sin (pt + side * 3.14159)));
                }
            }
            normalise (a, 0.75f); break;
        }
        case biAir:     // unpitched wash: noise through a slowly sweeping resonant band-pass, independent per side
        {
            alloc (6.0, true); SRng rn (99);
            for (int side = 0; side < 2; ++side)
            {
                auto& o = side ? a.R : a.L; float ic1 = 0, ic2 = 0;
                for (size_t i = 0; i < o.size(); ++i)
                {
                    const double t = (double) i / sr;
                    const double fc = 600 * std::pow (2.0, 2.5 * (0.5 + 0.5 * std::sin (tp * t / 6.0 + side))) ;
                    const float g = (float) std::tan (3.14159 * fc / sr), kk = 0.6f;
                    const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
                    const float v0 = rn.sym(), v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
                    const double env = std::min (1.0, t / 0.5) * std::min (1.0, (6.0 - t) / 0.5);
                    o[i] = (float) (v1 * env);
                }
            }
            normalise (a, 0.6f); break;
        }
        default:        // C minor pluck phrase (Karplus-Strong), 8ths at 120 BPM
        {
            alloc (4.0, true); SRng rn (3);
            const int notes[] = { 60, 63, 67, 70, 67, 65, 63, 62, 60, 55, 58, 60, 63, 62, 60, 60 };
            const double step = 0.25 * sr;
            for (int nI = 0; nI < 16; ++nI)
            {
                const double f = midiHz (notes[nI]); const int P = (int) std::lround (sr / f);
                std::vector<float> buf ((size_t) P); for (auto& x : buf) x = rn.sym();
                // fractional delay fix-up: an all-pass tunes the loop exactly
                const double frac = sr / f - P + 0.5; const float ap = (float) ((1 - frac) / (1 + frac)); float apz = 0, apx = 0;
                const size_t at = (size_t) (nI * step); const float pan = (nI % 2 ? 0.35f : -0.35f);
                int w = 0; float last = 0;
                for (int i = 0; i < (int) (0.9 * sr); ++i)
                {
                    const size_t j = at + (size_t) i; if (j >= a.L.size()) break;
                    const float x = buf[(size_t) w];
                    const float avg = 0.5f * (x + last) * 0.996f; last = x;
                    const float y = ap * avg + apx - ap * apz; apx = avg; apz = y;
                    buf[(size_t) w] = y; w = (w + 1) % P;
                    const float env = std::min (1.0f, (float) (0.9 * sr - i) / (float) (0.05 * sr));
                    a.L[j] += x * env * (1 - pan) * 0.5f; a.R[j] += x * env * (1 + pan) * 0.5f;
                }
            }
            normalise (a, 0.75f); break;
        }
    }
    return a;
}
} // namespace slt
