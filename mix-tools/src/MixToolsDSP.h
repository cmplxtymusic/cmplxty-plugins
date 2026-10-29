#pragma once
// CMPLXTY Mix Tools DSP: three mix modules in one strip, each switchable, with a selectable order (the limiter is always last).
//  WIDENER   mid/side width 0-200 %, mono-bass below a corner (side high-passed, 2nd order), correlation meter.
//  SHAPER    transient shaper: fast and slow envelope followers (stereo-linked) -> attack / sustain gain in dB, smoothed.
//  LIMITER   drive + true-peak brickwall: 8x polyphase interpolation finds inter-sample peaks, a lookahead sliding-min +
//            release + box-average gain curve reaches the needed gain exactly at the peak, then a safety clip at the ceiling.
// Constant latency (limiter lookahead), whether or not the limiter is on, so switching modules never shifts timing.
// Plain C++ (no JUCE), real-time safe after prepare().
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mt::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kHist = 256;   // display history length

inline float clean (float x) noexcept   // zero NaN / inf / denormals
{
    uint32_t u; std::memcpy (&u, &x, 4);
    const uint32_t e = u & 0x7f800000u;
    return (e == 0u || e == 0x7f800000u) ? 0.0f : x;
}
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1e-9f)); }

enum class Order { widenFirst = 0, shapeFirst = 1 };
enum class Speed { fast = 0, medium = 1, slow = 2 };

struct Params
{
    Order order = Order::widenFirst;
    bool wOn = true;  float width = 1.0f, monoHz = 20.0f;               // monoHz <= 20 = off
    bool tOn = true;  float attack = 0.0f, sustain = 0.0f; Speed speed = Speed::medium;   // attack / sustain -1..1
    bool lOn = true;  float driveDb = 0.0f, ceilingDb = -1.0f, releaseMs = 80.0f;
    float gainDb = 0.0f;
};

// 8x interpolator for true-peak detection (windowed sinc, 12 taps per phase). delay: kTpDelay samples
constexpr int kTpTaps = 12, kTpDelay = kTpTaps / 2, kTpOs = 8;
struct TruePeak
{
    std::array<std::array<float, kTpTaps>, kTpOs> h {};
    TruePeak()
    {
        for (int ph = 0; ph < kTpOs; ++ph)
        {
            float s = 0;
            for (int k = 0; k < kTpTaps; ++k)
            {
                const double t = (double) (k - kTpTaps / 2 + 1) - (double) ph / (double) kTpOs;   // position relative to the interpolated point
                const double x = kPi * t, sinc = std::fabs (t) < 1e-9 ? 1.0 : std::sin (x) / x;
                const double w = 0.5 + 0.5 * std::cos (kPi * t / (kTpTaps / 2 + 0.5));
                h[(size_t) ph][(size_t) k] = (float) (sinc * w); s += h[(size_t) ph][(size_t) k];
            }
            for (auto& v : h[(size_t) ph]) v /= s;
        }
    }
    // peak over the kTpOs phases of the window ending at x[n] (buffer holds the last kTpTaps samples, newest last)
    float peak (const float* last) const noexcept
    {
        float pk = 0;
        for (int ph = 0; ph < kTpOs; ++ph)
        {
            float a = 0; for (int k = 0; k < kTpTaps; ++k) a += h[(size_t) ph][(size_t) k] * last[k];
            pk = std::max (pk, std::fabs (a));
        }
        return pk;
    }
};

class Engine
{
public:
    std::atomic<float> inLevel { 0 }, outLevel { 0 }, grDb { 0 }, tGainDb { 0 }, corr { 1 }, widthNow { 1 };
    // display history (written by the audio thread, read by the UI)
    std::array<std::atomic<float>, kHist> histGr {}, histT {}, histOut {};
    std::atomic<int> histHead { 0 };
    std::array<std::atomic<float>, 128> scopeM {}, scopeS {};   // recent mid/side points (vectorscope)

    void setParams (const Params& np) noexcept { p = np; }
    int latency() const noexcept { return L - 1 + kTpDelay; }
    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0; nch = std::clamp (channels, 1, 2);
        L = std::max (8, (int) std::lround (sr * 0.0015));   // 1.5 ms lookahead
        D = L;
        for (auto& d : delay) d.assign ((size_t) D, 0.0f);
        ring.assign ((size_t) L, 1.0f); boxBuf.assign ((size_t) L, 1.0f);
        reset();
    }
    void reset() noexcept
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (ring.begin(), ring.end(), 1.0f); std::fill (boxBuf.begin(), boxBuf.end(), 1.0f);
        dw = 0; rw = 0; bw = 0; boxSum = (double) L; rel = 1.0f;
        for (auto& t : tp) t.fill (0.0f);
        hp1 = hp2 = {}; fastE = slowE = susE = 0; tg = 1.0f; gainSm = dbToGain (p.gainDb); driveSm = dbToGain (p.driveDb);
        corrNum = corrA = corrB = 0; histCount = 0; scopeW = 0; grMin = 1.0f; tAcc = 0; outPk = 0;
    }

    void process (float* const* ch, int n) noexcept
    {
        if (n <= 0) return;
        float* l = ch[0]; float* r = nch > 1 ? ch[1] : nullptr;
        // coefficients
        const float wc = (float) std::exp (-2.0 * kPi * std::max (20.0, (double) p.monoHz) / sr);
        const bool monoOn = p.monoHz > 20.5f;
        if (monoOn) bq = butterHP (std::min ((double) p.monoHz, sr * 0.45), sr);
        const double fastMs = 0.5, slowAtt = p.speed == Speed::fast ? 10.0 : p.speed == Speed::slow ? 40.0 : 20.0;
        const double susRel = p.speed == Speed::fast ? 80.0 : p.speed == Speed::slow ? 400.0 : 180.0;
        const float aFast = co (fastMs), rFast = co (30.0), aSlow = co (slowAtt), rSlow = co (60.0), aSus = co (2.0), rSus = co (susRel);
        const float tSm = co (0.3);
        const float relC = co (std::max (1.0f, p.releaseMs));
        const float gT = dbToGain (p.gainDb), dT = p.lOn ? dbToGain (p.driveDb) : 1.0f, ceil = dbToGain (p.ceilingDb);
        const float pSm = co (10.0);
        float inPk = 0, oPk = 0;
        for (int i = 0; i < n; ++i)
        {
            float a = clean (l[i]), b = r != nullptr ? clean (r[i]) : a;
            inPk = std::max (inPk, std::max (std::fabs (a), std::fabs (b)));
            if (p.order == Order::widenFirst) { widen (a, b, wc, monoOn); shape (a, b, aFast, rFast, aSlow, rSlow, aSus, rSus, tSm); }
            else                              { shape (a, b, aFast, rFast, aSlow, rSlow, aSus, rSus, tSm); widen (a, b, wc, monoOn); }
            // limiter (always runs for constant latency; gain 1 when off)
            driveSm = dT + (driveSm - dT) * pSm;
            a *= driveSm; b *= driveSm;
            for (int c = 0; c < 2; ++c) { auto& t = tp[(size_t) c]; std::memmove (t.data(), t.data() + 1, sizeof (float) * (kTpTaps - 1)); t[kTpTaps - 1] = c == 0 ? a : b; }
            float g = 1.0f;
            if (p.lOn)
            {
                const float pk = std::max (std::max (tpd.peak (tp[0].data()), tpd.peak (tp[1].data())),
                                           std::max (std::fabs (tp[0][kTpTaps - 1 - kTpDelay]), std::fabs (tp[1][kTpTaps - 1 - kTpDelay])));
                g = pk > ceil ? ceil / pk : 1.0f;
            }
            ring[(size_t) rw] = g; rw = (rw + 1) % L;
            float m = 1.0f; for (float v : ring) m = std::min (m, v);
            rel = m < rel ? m : m + (rel - m) * relC;
            boxSum += (double) rel - (double) boxBuf[(size_t) bw]; boxBuf[(size_t) bw] = rel; bw = (bw + 1) % L;
            const float A = std::min (1.0f, (float) (boxSum / (double) L));
            // delay line: the signal sample kTpDelay back from the newest tp value, delayed by L - 1 more
            auto push = [&] (std::vector<float>& d, float x) { d[(size_t) dw] = x; return d[(size_t) ((dw + 1) % D)]; };
            const float da = push (delay[0], tp[0][kTpTaps - 1 - kTpDelay]), db = push (delay[1], tp[1][kTpTaps - 1 - kTpDelay]);
            dw = (dw + 1) % D;
            float ya = da * A, yb = db * A;
            if (p.lOn) { ya = std::clamp (ya, -ceil, ceil); yb = std::clamp (yb, -ceil, ceil); }
            gainSm = gT + (gainSm - gT) * pSm;
            ya = clean (ya * gainSm); yb = clean (yb * gainSm);
            grMin = std::min (grMin, A);
            oPk = std::max (oPk, std::max (std::fabs (ya), std::fabs (yb)));
            l[i] = ya; if (r != nullptr) r[i] = yb;
        }
        inLevel.store (inPk, std::memory_order_relaxed); outLevel.store (oPk, std::memory_order_relaxed);
        grDb.store (gainToDb (grMin), std::memory_order_relaxed);
        tGainDb.store (tAcc, std::memory_order_relaxed);
        const double den = std::sqrt (corrA * corrB);
        corr.store (den > 1e-9 ? (float) (corrNum / den) : 1.0f, std::memory_order_relaxed);
        histCount += n;
        outPk = std::max (outPk, oPk);
        if (histCount >= (int) (sr / 60.0))
        {
            const int h = (histHead.load (std::memory_order_relaxed) + 1) % kHist;
            histGr[(size_t) h].store (gainToDb (grMin), std::memory_order_relaxed);
            histT[(size_t) h].store (tAcc, std::memory_order_relaxed);
            histOut[(size_t) h].store (outPk, std::memory_order_relaxed);
            histHead.store (h, std::memory_order_relaxed);
            histCount = 0; grMin = 1.0f; outPk = 0;
            corrNum *= 0.5; corrA *= 0.5; corrB *= 0.5;
        }
    }

private:
    float co (double ms) const noexcept { return (float) std::exp (-1.0 / (std::max (0.01, ms) * 0.001 * sr)); }
    struct BQ { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct HP
    {
        float z1 = 0, z2 = 0;
        float run (float x, const BQ& c) noexcept { const float y = c.b0 * x + z1; z1 = clean (c.b1 * x - c.a1 * y + z2); z2 = clean (c.b2 * x - c.a2 * y); return y; }
    };
    static BQ butterHP (double fc, double sr) noexcept
    {
        const double w = 2 * kPi * fc / sr, cw = std::cos (w), al = std::sin (w) / (2 * 0.7071067811865476), a0 = 1 + al;
        BQ c; c.b0 = (float) ((1 + cw) / 2 / a0); c.b1 = (float) (-(1 + cw) / a0); c.b2 = c.b0; c.a1 = (float) (-2 * cw / a0); c.a2 = (float) ((1 - al) / a0);
        return c;
    }
    BQ bq;
    void widen (float& a, float& b, float, bool monoOn) noexcept
    {
        float m = 0.5f * (a + b), s = 0.5f * (a - b);
        if (p.wOn)
        {
            if (monoOn) s = hp2.run (hp1.run (s, bq), bq);   // Linkwitz-Riley 24 dB/oct high-pass on the side = mono lows
            s *= p.width;
            a = m + s; b = m - s;
        }
        corrNum += (double) a * b; corrA += (double) a * a; corrB += (double) b * b;
        if ((++scopeTick & 7) == 0) { scopeM[(size_t) scopeW].store (m, std::memory_order_relaxed); scopeS[(size_t) scopeW].store (0.5f * (a - b), std::memory_order_relaxed); scopeW = (scopeW + 1) % 128; }
    }
    void shape (float& a, float& b, float aF, float rF, float aS, float rS, float aU, float rU, float tSm) noexcept
    {
        if (! p.tOn) { tg = 1.0f + (tg - 1.0f) * tSm; a *= tg; b *= tg; tAcc = gainToDb (tg); return; }
        const float x = std::max (std::fabs (a), std::fabs (b));
        fastE = clean (x > fastE ? x + (fastE - x) * aF : x + (fastE - x) * rF);
        const float f = fastE;   // slow followers track the fast envelope, so a steady tone gives equal envelopes (no change)
        slowE = clean (f > slowE ? f + (slowE - f) * aS : f + (slowE - f) * rS);
        susE  = clean (f > susE  ? f + (susE  - f) * aU : f + (susE  - f) * rU);
        float gDb = 0;
        if (fastE > 1e-5f)
        {
            const float att = std::clamp (gainToDb (fastE) - gainToDb (std::max (slowE, 1e-6f)), 0.0f, 24.0f);   // > 0 during an onset
            const float sus = std::clamp (gainToDb (std::max (susE, 1e-6f)) - gainToDb (fastE), 0.0f, 24.0f);  // > 0 in a decaying tail
            gDb = std::clamp ((p.attack > 0 ? 1.5f : 2.5f) * p.attack * att + (p.sustain > 0 ? 1.0f : 1.5f) * p.sustain * sus, -30.0f, 12.0f);
        }
        const float tgt = dbToGain (gDb);
        tg = tgt + (tg - tgt) * tSm;
        a *= tg; b *= tg; tAcc = gainToDb (tg);
    }

    Params p;
    double sr = 48000.0; int nch = 2, L = 72, D = 75;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> ring, boxBuf;
    int dw = 0, rw = 0, bw = 0; double boxSum = 72; float rel = 1.0f;
    std::array<std::array<float, kTpTaps>, 2> tp {};
    TruePeak tpd;
    HP hp1, hp2;
    float fastE = 0, slowE = 0, susE = 0, tg = 1.0f, gainSm = 1.0f, driveSm = 1.0f, grMin = 1.0f, tAcc = 0, outPk = 0;
    double corrNum = 0, corrA = 0, corrB = 0;
    int histCount = 0, scopeW = 0; unsigned scopeTick = 0;
};
} // namespace mt::dsp
