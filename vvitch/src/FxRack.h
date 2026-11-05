#pragma once
// VVITCH 1.1.0 FX RACK: four insert slots after CHIP / TAPE and before the dark delay + reverb.
// Each slot: a type, two controls (A, B: their meaning depends on the type) and a dry / wet mix. Off slots cost nothing.
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vv { namespace fx
{
enum Type { kOff = 0, kChorus, kPhaser, kFlanger, kDistort, kComp, kTilt, kWiden, kNumTypes };
inline const char* typeName (int t)
{
    static const char* n[] = { "Off", "Chorus", "Phaser", "Flanger", "Distortion", "Compressor", "Tilt EQ", "Widener" };
    return t >= 0 && t < kNumTypes ? n[t] : "?";
}
// labels for the A / B knobs per type (UI)
inline const char* labelA (int t) { static const char* n[] = { "-", "RATE", "RATE", "RATE", "DRIVE", "AMOUNT", "TILT", "WIDTH" }; return t >= 0 && t < kNumTypes ? n[t] : "-"; }
inline const char* labelB (int t) { static const char* n[] = { "-", "DEPTH", "FEEDBACK", "FEEDBACK", "TONE", "SPEED", "LOW CUT", "MONO BASS" }; return t >= 0 && t < kNumTypes ? n[t] : "-"; }
constexpr int kSlots = 4;

struct SlotParams { int type = kOff; float a = 0.5f, b = 0.5f, mix = 0.5f; };

class Slot
{
public:
    void prepare (float sampleRate) { sr = sampleRate; buf[0].assign ((size_t) (sr * 0.05f) + 8, 0.0f); buf[1].assign (buf[0].size(), 0.0f); reset(); }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; ph = 0; env = 0; fbL = fbR = 0; lpL = lpR = 0; hpL = hpR = 0; bassL = bassR = 0; toneL = toneR = 0;
        for (auto& a : ap) a = {};
        lastType = -1;
    }
    void setup (const SlotParams& p)
    {
        P = p;
        if (P.type != lastType) { reset(); lastType = P.type; }
        const float a = std::clamp (P.a, 0.0f, 1.0f), b = std::clamp (P.b, 0.0f, 1.0f);
        switch (P.type)
        {
            case kChorus:  rate = 0.1f + a * a * 4.0f; break;
            case kPhaser:  rate = 0.05f + a * a * 3.0f; break;
            case kFlanger: rate = 0.05f + a * a * 2.0f; break;
            case kDistort: drv = 1.0f + a * a * 30.0f; toneA = coef (800.0f + b * b * 15000.0f); break;
            case kComp:    thr = std::pow (10.0f, (-6.0f - 30.0f * a) / 20.0f); att = coefT (0.03f - 0.027f * b); rel = coefT (0.4f - 0.35f * b); makeup = 1.0f + a * 2.5f; break;
            case kTilt:    tiltA = coef (900.0f); hpA = coef (20.0f + b * b * 600.0f); break;
            case kWiden:   bassA = coef (60.0f + b * 300.0f); break;
            default: break;
        }
    }
    bool active() const { return P.type > kOff && P.type < kNumTypes && P.mix > 0.0001f; }

    void process (float& l, float& r)
    {
        if (! active()) return;
        const float dl = l, dr = r, a = std::clamp (P.a, 0.0f, 1.0f), b = std::clamp (P.b, 0.0f, 1.0f);
        float wl = l, wr = r;
        switch (P.type)
        {
            case kChorus:
            case kFlanger:
            {
                ph += rate / sr; if (ph >= 1) ph -= 1;
                const float s0 = std::sin (6.2831853f * ph), s1 = std::sin (6.2831853f * (ph + 0.25f));
                const bool ch = P.type == kChorus;
                const float base = (ch ? 0.012f : 0.0015f) * sr, depth = (ch ? 0.006f * (0.2f + b) : 0.0013f) * sr;
                const float fb = ch ? 0.0f : (b * 1.8f - 0.9f);
                buf[0][(size_t) w] = clean (l + fbL * fb); buf[1][(size_t) w] = clean (r + fbR * fb);
                wl = read (buf[0], base + depth * s0); wr = read (buf[1], base + depth * (ch ? s1 : s0));
                fbL = wl; fbR = wr; w = (w + 1) % (int) buf[0].size();
                break;
            }
            case kPhaser:
            {
                ph += rate / sr; if (ph >= 1) ph -= 1;
                const float m = 0.5f + 0.5f * std::sin (6.2831853f * ph);
                const float f = 200.0f * std::pow (20.0f, m), t = std::tan (3.14159265f * std::min (f, sr * 0.45f) / sr), c = (t - 1) / (t + 1);
                const float fb = b * 0.85f;
                float xl = l + fbL * fb, xr = r + fbR * fb;
                for (int k = 0; k < 4; ++k) { xl = ap1 (ap[(size_t) k][0], xl, c); xr = ap1 (ap[(size_t) k][1], xr, c * 0.97f); }
                fbL = clean (xl); fbR = clean (xr); wl = (l + xl) * 0.5f; wr = (r + xr) * 0.5f;
                break;
            }
            case kDistort:
            {
                wl = std::tanh (l * drv) / std::sqrt (drv) * 1.3f; wr = std::tanh (r * drv) / std::sqrt (drv) * 1.3f;
                toneL += (wl - toneL) * toneA; toneR += (wr - toneR) * toneA; wl = toneL; wr = toneR;
                break;
            }
            case kComp:
            {
                const float x = std::max (std::fabs (l), std::fabs (r));
                env = x > env ? env + (x - env) * att : env + (x - env) * rel;
                const float g = env > thr ? std::pow (thr / env, 0.75f) : 1.0f;   // 4:1
                wl = l * g * makeup; wr = r * g * makeup;
                break;
            }
            case kTilt:
            {
                hpL += (l - hpL) * hpA; hpR += (r - hpR) * hpA;
                const float xl = l - hpL, xr = r - hpR;
                lpL += (xl - lpL) * tiltA; lpR += (xr - lpR) * tiltA;
                const float t = (a - 0.5f) * 2.0f, lo = std::pow (2.0f, -t * 1.5f), hi = std::pow (2.0f, t * 1.5f);   // +-9 dB
                wl = lpL * lo + (xl - lpL) * hi; wr = lpR * lo + (xr - lpR) * hi;
                break;
            }
            case kWiden:
            {
                bassL += (l - bassL) * bassA; bassR += (r - bassR) * bassA;
                const float bm = 0.5f * (bassL + bassR);
                const float hl = l - bassL, hr = r - bassR, m = 0.5f * (hl + hr), s = 0.5f * (hl - hr) * (a * 2.0f);
                wl = m + s + (b > 0.02f ? bm : bassL); wr = m - s + (b > 0.02f ? bm : bassR);
                break;
            }
            default: break;
        }
        const float mx = std::clamp (P.mix, 0.0f, 1.0f);
        l = clean (dl + (wl - dl) * mx); r = clean (dr + (wr - dr) * mx);
        (void) a;
    }

private:
    static float clean (float x) { return std::fabs (x) < 1e-15f || ! std::isfinite (x) ? 0.0f : x; }
    float coef (float hz) const { return 1.0f - std::exp (-6.2831853f * std::min (hz, sr * 0.45f) / sr); }
    float coefT (float sec) const { return 1.0f - std::exp (-1.0f / (std::max (0.0005f, sec) * sr)); }
    float read (const std::vector<float>& b, float d) const
    {
        const int n = (int) b.size(); float p = (float) w - d; while (p < 0) p += (float) n;
        const int i0 = (int) p; const float f = p - (float) i0; const int i1 = (i0 + 1) % n;
        return b[(size_t) (i0 % n)] * (1 - f) + b[(size_t) i1] * f;
    }
    static float ap1 (float& z, float x, float c) { const float y = c * x + z; z = x - c * y; z = clean (z); return y; }
    SlotParams P; float sr = 48000; int lastType = -1;
    std::array<std::vector<float>, 2> buf; int w = 0; float ph = 0, fbL = 0, fbR = 0;
    std::array<std::array<float, 2>, 4> ap {};
    float rate = 1, drv = 1, toneA = 1, toneL = 0, toneR = 0, thr = 1, att = 1, rel = 1, makeup = 1, env = 0;
    float tiltA = 0.1f, hpA = 0.001f, lpL = 0, lpR = 0, hpL = 0, hpR = 0, bassA = 0.01f, bassL = 0, bassR = 0;
};

class Rack
{
public:
    void prepare (float sr) { for (auto& s : slot) s.prepare (sr); }
    void reset() { for (auto& s : slot) s.reset(); }
    void setup (const std::array<SlotParams, kSlots>& p) { any = false; for (int i = 0; i < kSlots; ++i) { slot[(size_t) i].setup (p[(size_t) i]); any = any || slot[(size_t) i].active(); } }
    bool active() const { return any; }
    void process (float& l, float& r) { for (auto& s : slot) s.process (l, r); }
private:
    std::array<Slot, kSlots> slot; bool any = false;
};
}} // namespace vv::fx
