#pragma once
// Synthetic "real-ish" drum loops (kick / snare / hats, straight, swung + room, busy + room) shared by the engine tests and the plug-in tests.
#include "../src/FillEngine.h"
#include <algorithm>
#include <random>

namespace drftest
{
using namespace drf;
inline constexpr double kTwoPi = 6.283185307179586;
// ---------------------------------------------------------------- test material
struct Synth
{
    std::mt19937 rng { 11 };
    float u() { return std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng); }
    // kick: pitch-swept sine (150 -> 48 Hz) + 3 ms click
    void kick (std::vector<float>& x, double sr, double t, float v, float tone = 1.0f)
    {
        double ph = 0; const int s0 = (int) std::llround (t * sr);
        for (int i = 0; i < (int) (0.45 * sr) && s0 + i < (int) x.size(); ++i)
        {
            const double tt = i / sr; ph += (48.0 * tone + 100.0 * std::exp (-tt * 35.0)) / sr;
            const double click = i < (int) (0.003 * sr) ? 0.35 * u() * (1.0 - tt / 0.003) : 0.0;
            const double tail = std::min (1.0, (0.45 - tt) / 0.05);   // fades out over its last 50 ms
            x[(size_t) (s0 + i)] += v * (float) (tail * 0.95 * std::exp (-tt * 7.0) * std::sin (kTwoPi * ph) * std::min (1.0, i / (0.0005 * sr)) + click);
        }
    }
    // snare: band-passed noise + 185 / 330 Hz body
    void snare (std::vector<float>& x, double sr, double t, float v)
    {
        const int s0 = (int) std::llround (t * sr); float lp = 0, hp = 0;
        for (int i = 0; i < (int) (0.3 * sr) && s0 + i < (int) x.size(); ++i)
        {
            const double tt = i / sr; const float n = u(); lp += 0.35f * (n - lp); hp = n - lp;
            const float body = (float) (0.5 * std::sin (kTwoPi * 185.0 * tt) * std::exp (-tt * 30.0) + 0.25 * std::sin (kTwoPi * 330.0 * tt) * std::exp (-tt * 40.0));
            const float tail = (float) std::min (1.0, (0.3 - tt) / 0.04);
            x[(size_t) (s0 + i)] += tail * v * (float) std::min (1.0, i / (0.0004 * sr)) * ((float) std::exp (-tt * 16.0) * 0.55f * (0.6f * hp + 0.4f * n) + body);
        }
    }
    // metallic hat / cymbal: six detuned squares (808-style) high-passed, decay sets closed / open / crash
    // chokeAt: the next hat (a real hi-hat closes), 0 = rings out
    void metal (std::vector<float>& x, double sr, double t, float v, double decay, double chokeAt = 0)
    {
        static const double f[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        const int s0 = (int) std::llround (t * sr); float hp1 = 0, prev = 0, hp2 = 0, prev2 = 0;
        int len = (int) (std::min (3.0, decay * 7.0) * sr);
        const int choke = chokeAt > t ? (int) ((chokeAt - t) * sr) : len;
        len = std::min (len, choke + (int) (0.004 * sr));
        for (int i = 0; i < len && s0 + i < (int) x.size(); ++i)
        {
            const double tt = i / sr; float sq = 0;
            for (double fr : f) sq += std::sin (kTwoPi * fr * 1.0 * tt * 8.1) > 0 ? 1.0f : -1.0f;
            const float in = sq / 6.0f * 0.6f + 0.4f * u();
            hp1 = 0.9f * (hp1 + in - prev); prev = in; hp2 = 0.9f * (hp2 + hp1 - prev2); prev2 = hp1;
            const double fade = std::min ({ 1.0, (len - i) / (0.004 * sr), 1.0 });
            x[(size_t) (s0 + i)] += v * 0.5f * (float) (fade * std::exp (-tt / decay) * std::min (1.0, i / (0.0003 * sr))) * hp2;
        }
    }
};
inline void room (std::vector<float>& x, double sr, float amt)
{   // small diffuse room (4 damped combs + 2 allpasses, ~0.4 s), mixed in, plus a -66 dB noise floor
    std::mt19937 r (5); std::uniform_real_distribution<float> u (-1, 1);
    const double cd[] = { 0.0297, 0.0371, 0.0411, 0.0437 }, ad[] = { 0.005, 0.0017 };
    std::vector<float> y (x.size(), 0.0f);
    for (double d : cd) { std::vector<float> b ((size_t) (d * sr), 0.0f); size_t w = 0; float lp = 0;
        for (size_t i = 0; i < x.size(); ++i) { const float o = b[w]; lp += 0.45f * (o - lp); b[w] = x[i] + 0.72f * lp; w = (w + 1) % b.size(); y[i] += o * 0.25f; } }
    for (double d : ad) { std::vector<float> b ((size_t) (d * sr), 0.0f); size_t w = 0;
        for (size_t i = 0; i < y.size(); ++i) { const float o = b[w], in = y[i]; b[w] = in + 0.6f * o; y[i] = o - 0.6f * b[w]; w = (w + 1) % b.size(); } }
    for (size_t i = 0; i < x.size(); ++i) x[i] += amt * y[i] + 0.0005f * u (r);
}

struct TestLoop { Audio a; std::vector<double> truth; std::vector<int> kinds; };
// 2-bar loops, 4/4, start on the downbeat. variant: 0 straight, 1 swung 16ths + room, 2 busy + room + level changes
inline TestLoop makeLoop (int lane, double bpm, int variant, double sr = 44100.0)
{
    Synth sy; sy.rng.seed ((unsigned) (lane * 100 + variant * 7 + (int) bpm));
    TestLoop t; const double beat = 60.0 / bpm, len = 8 * beat;
    t.a.rate = sr; t.a.L.assign ((size_t) std::llround (len * sr), 0.0f);
    auto sw = [&] (double sixteenth) { const int k = (int) std::llround (sixteenth); double tt = sixteenth * beat / 4; if (variant == 1 && k % 2 == 1) tt += beat / 4 * 0.33; return tt; };
    auto at = [&] (double six, float v, int kind = 0) { t.truth.push_back (sw (six)); t.kinds.push_back (kind); return sw (six); };
    if (lane == kKick)
    {
        const int pat[] = { 0, 6, 10, 16, 22, 26, 29 };
        for (int p : pat) { if (variant == 0 && p == 29) continue; const double tt = at (p, 1); sy.kick (t.a.L, sr, tt, p % 8 == 0 ? 0.95f : 0.8f); }
    }
    else if (lane == kSnare)
    {
        for (int b = 1; b < 8; b += 2) { const double tt = at (b * 4, 1); sy.snare (t.a.L, sr, tt, 0.9f); }
        if (variant >= 1) for (int g : { 7, 14, 23, 30 }) { const double tt = at (g, 0.2f, 1); sy.snare (t.a.L, sr, tt, 0.17f); }   // ghost notes (-15 dB)
    }
    else
    {
        for (int k = 0; k < 32; k += (variant == 2 ? 1 : 2))
        {
            const bool open = k == 14 || k == 30;
            if (variant == 2 && k % 2 == 1 && k != 31) { const double tt = at (k, 0.5f); sy.metal (t.a.L, sr, tt, 0.45f, 0.012); continue; }
            if (variant == 2 && k == 31) continue;
            const double tt = at (k, 1, open ? kOpen : kClosed);
            sy.metal (t.a.L, sr, tt, k % 4 == 0 ? 0.9f : 0.65f, open ? 0.12 : 0.018, open ? sw (k + (variant == 2 ? 1 : 2)) : 0.0);
        }
        if (variant >= 1) { t.a.L.resize (t.a.L.size() + (size_t) (1.2 * sr), 0.0f); }   // a crash at the very end (+ its tail: loop 2 bars + ring)
        if (variant >= 1) { const double tt = at (32 - 0.0001, 1, kCrash); (void) tt; t.truth.back() = 8 * beat; sy.metal (t.a.L, sr, 8 * beat, 0.95f, 0.45); }
    }
    if (variant >= 1) room (t.a.L, sr, variant == 2 ? 0.35f : 0.22f);
    float pk = 0; for (float v : t.a.L) pk = std::max (pk, std::fabs (v));
    for (auto& v : t.a.L) v *= 0.7f / pk;
    if (variant == 2) { t.a.R = t.a.L; for (size_t i = 0; i < t.a.R.size(); ++i) t.a.R[i] *= 0.9f; }
    // keep the truth sorted (crash last)
    std::vector<std::pair<double, int>> tk; for (size_t i = 0; i < t.truth.size(); ++i) tk.push_back ({ t.truth[i], t.kinds[i] });
    std::sort (tk.begin(), tk.end()); t.truth.clear(); t.kinds.clear(); for (auto& p : tk) { t.truth.push_back (p.first); t.kinds.push_back (p.second); }
    return t;
}

} // namespace drftest
