#pragma once
#include "Parameters.h"
#include <array>
#include <atomic>
#include <cstring>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <xmmintrin.h>
#define GLAZE_HAS_SSE 1
#endif

namespace glaze
{

struct FlushDenormals
{
#if defined(GLAZE_HAS_SSE)
    unsigned old = 0;
    FlushDenormals() noexcept
    {
        old = _mm_getcsr();
        _mm_setcsr(old | 0x8040); // FZ | DAZ
    }
    ~FlushDenormals() { _mm_setcsr(old); }
#else
    FlushDenormals() noexcept = default;
#endif
};

// TPT state-variable filter (Zavalishin / Simper).
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() noexcept { ic1 = ic2 = 0; }
    inline void tick(float x, float a1, float a2, float a3, float k, float &lp, float &bp,
                     float &hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

struct SvfCoef
{
    float a1 = 1, a2 = 0, a3 = 0, k = 1;
    static SvfCoef make(float fc, float q, float sr) noexcept
    {
        fc = clampf(fc, 8.f, 0.45f * sr);
        q = clampf(q, 0.15f, 40.f);
        const float g = std::tan(kPi * fc / sr);
        SvfCoef c;
        c.k = 1.f / q;
        c.a1 = 1.f / (1.f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
};

// Peak-follower with separate attack / release.
struct Env
{
    float y = 0, atk = 0.01f, rel = 0.01f;
    void set(double atkSec, double relSec, double sr) noexcept
    {
        atk = onePole(atkSec, sr);
        rel = onePole(relSec, sr);
    }
    inline float tick(float x) noexcept
    {
        const float a = x > y ? atk : rel;
        y += a * (x - y);
        return y;
    }
    void reset() noexcept { y = 0; }
    void snap(float v) noexcept { y = v; }
};

// 1-pole high-pass (DC blocker / harmonic high-pass).
struct Hp1
{
    float x1 = 0, y1 = 0, a = 0.995f;
    void set(float fc, float sr) noexcept
    {
        a = std::exp(-2.f * kPi * clampf(fc, 5.f, 0.45f * sr) / sr);
    }
    inline float tick(float x) noexcept
    {
        const float y = x - x1 + a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() noexcept { x1 = y1 = 0; }
};

// 8x polyphase IIR half-band oversampler (elliptic allpass pairs, de Soras style).
// Minimum-phase, a few samples of high-frequency group delay, no integer latency to report.
template <int N>
struct Halfband
{
    std::array<float, N> c{}, x{}, y{};
    void reset() noexcept
    {
        x.fill(0);
        y.fill(0);
    }
    inline void stage(float &s0, float &s1) noexcept
    {
        int i = 0;
        for (; i + 1 < N; i += 2)
        {
            const float t0 = x[(size_t)i], t1 = x[(size_t)i + 1];
            x[(size_t)i] = s0;
            x[(size_t)i + 1] = s1;
            s0 = (s0 - y[(size_t)i]) * c[(size_t)i] + t0;
            s1 = (s1 - y[(size_t)i + 1]) * c[(size_t)i + 1] + t1;
            y[(size_t)i] = s0;
            y[(size_t)i + 1] = s1;
        }
        if (i < N)
        {
            const float t0 = x[(size_t)i];
            x[(size_t)i] = s0;
            s0 = (s0 - y[(size_t)i]) * c[(size_t)i] + t0;
            y[(size_t)i] = s0;
        }
    }
    inline void up(float in, float &o0, float &o1) noexcept
    {
        float s0 = in, s1 = in;
        stage(s0, s1);
        o0 = s0;
        o1 = s1;
    }
    inline float down(float a, float b) noexcept
    {
        float s0 = b, s1 = a;
        stage(s0, s1);
        return 0.5f * (s0 + s1);
    }
};

struct Oversampler
{
    Halfband<13> u1, d1;
    Halfband<5> u2, d2;
    Halfband<4> u3, d3;
    Oversampler()
    {
        const float a[] = { 0.029281335951567022f, 0.11057561422770555f,  0.22716737191076788f,
                            0.35889539632252332f,  0.48847433709752414f,  0.60469436668404419f,
                            0.7024814804315771f,   0.78127749683662318f,  0.84313624835644707f,
                            0.89125435756077287f,  0.92909106562237864f,  0.95996869059793144f,
                            0.98699367530182835f };
        const float b[] = { 0.04790845477363561f, 0.17951160525949356f, 0.36780928458752388f,
                            0.59071801647357025f, 0.84841479122482688f };
        const float e[] = { 0.053379036964928725f, 0.20558897612802121f, 0.44371079776028416f,
                            0.77747763544575854f };
        for (int i = 0; i < 13; ++i)
            u1.c[(size_t)i] = d1.c[(size_t)i] = a[i];
        for (int i = 0; i < 5; ++i)
            u2.c[(size_t)i] = d2.c[(size_t)i] = b[i];
        for (int i = 0; i < 4; ++i)
            u3.c[(size_t)i] = d3.c[(size_t)i] = e[i];
    }
    void reset() noexcept
    {
        u1.reset();
        d1.reset();
        u2.reset();
        d2.reset();
        u3.reset();
        d3.reset();
    }
    inline void up(float x, float *hi) noexcept
    {
        float a0, a1;
        u1.up(x, a0, a1);
        float b[4];
        u2.up(a0, b[0], b[1]);
        u2.up(a1, b[2], b[3]);
        for (int i = 0; i < 4; ++i)
            u3.up(b[i], hi[2 * i], hi[2 * i + 1]);
    }
    inline float down(const float *hi) noexcept
    {
        float b[4];
        for (int i = 0; i < 4; ++i)
            b[i] = d3.down(hi[2 * i], hi[2 * i + 1]);
        const float m0 = d2.down(b[0], b[1]);
        const float m1 = d2.down(b[2], b[3]);
        return d1.down(m0, m1);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// RBJ biquads (double precision, transposed direct form II).
struct BiquadCoef
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    static BiquadCoef norm(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        BiquadCoef c;
        c.b0 = b0 / a0;
        c.b1 = b1 / a0;
        c.b2 = b2 / a0;
        c.a1 = a1 / a0;
        c.a2 = a2 / a0;
        return c;
    }
    static BiquadCoef peaking(double f, double gDb, double q, double sr) noexcept
    {
        f = clampd(f, 10.0, 0.45 * sr);
        const double A = std::pow(10.0, gDb / 40.0), w = 2.0 * kPiD * f / sr, cw = std::cos(w),
                     al = std::sin(w) / (2.0 * q);
        return norm(1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    static BiquadCoef lowShelf(double f, double gDb, double q, double sr) noexcept
    {
        f = clampd(f, 10.0, 0.45 * sr);
        const double A = std::pow(10.0, gDb / 40.0), w = 2.0 * kPiD * f / sr, cw = std::cos(w),
                     al = std::sin(w) / (2.0 * q), sq = 2.0 * std::sqrt(A) * al;
        return norm(A * ((A + 1) - (A - 1) * cw + sq), 2 * A * ((A - 1) - (A + 1) * cw),
                    A * ((A + 1) - (A - 1) * cw - sq), (A + 1) + (A - 1) * cw + sq,
                    -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sq);
    }
    static BiquadCoef highShelf(double f, double gDb, double q, double sr) noexcept
    {
        f = clampd(f, 10.0, 0.45 * sr);
        const double A = std::pow(10.0, gDb / 40.0), w = 2.0 * kPiD * f / sr, cw = std::cos(w),
                     al = std::sin(w) / (2.0 * q), sq = 2.0 * std::sqrt(A) * al;
        return norm(A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw),
                    A * ((A + 1) + (A - 1) * cw - sq), (A + 1) - (A - 1) * cw + sq,
                    2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sq);
    }
    static BiquadCoef highpass(double f, double q, double sr) noexcept
    {
        f = clampd(f, 5.0, 0.45 * sr);
        const double w = 2.0 * kPiD * f / sr, cw = std::cos(w), al = std::sin(w) / (2.0 * q);
        return norm((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    // |H| in dB at hz
    double magDb(double hz, double sr) const noexcept
    {
        const double w = 2.0 * kPiD * hz / sr;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        const double m = (nr * nr + ni * ni) / std::max(dr * dr + di * di, 1.0e-30);
        return 10.0 * std::log10(std::max(m, 1.0e-30));
    }
};

struct Biquad
{
    BiquadCoef c;
    double z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0; }
    inline double tick(double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// One-pole smoothed control value, stepped once per sub-block.
struct Smoothed
{
    float cur = 0, tgt = 0;
    void snap(float v) noexcept { cur = tgt = v; }
    inline float step(float a) noexcept
    {
        cur += a * (tgt - cur);
        return cur;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// 8x true-peak interpolator: 32-tap Kaiser-windowed sinc, seven phases (1/8 .. 7/8 between sample s and s+1).
// Window x[s-15 .. s+16] = 32 consecutive samples, oldest first; win[15] is x[s].
struct TruePeakFir
{
    static constexpr int kTaps = 32;
    static constexpr int kPhases = 7;
    static constexpr int kLead = 16; // samples after s that the window needs
    std::array<std::array<float, kTaps>, kPhases> c{};
    static double bessel0(double x) noexcept
    {
        double s = 1, t = 1;
        for (int k = 1; k < 60; ++k)
        {
            t *= (x / (2.0 * k)) * (x / (2.0 * k));
            s += t;
        }
        return s;
    }
    TruePeakFir()
    {
        const double beta = 9.0;
        for (int k = 1; k <= kPhases; ++k)
            for (int j = 0; j < kTaps; ++j)
            {
                const double t = (double)(j - 15) - k / 8.0; // distance from the interpolated point
                const double sinc = std::abs(t) < 1e-9 ? 1.0 : std::sin(kPiD * t) / (kPiD * t);
                const double r = t / 16.5;
                const double w = std::abs(r) >= 1 ? 0.0 : bessel0(beta * std::sqrt(1 - r * r)) / bessel0(beta);
                c[(size_t)k - 1][(size_t)j] = (float)(sinc * w);
            }
    }
    // peak magnitude around x[s]: the sample, the 7 interpolated points after it, and a parabolic refinement of the
    // largest local maximum among them (cuts the 1/8-sample sampling error to well under 0.02 dB). win = 32 samples.
    inline float peak(const float *win) const noexcept
    {
        float v[kPhases + 2];
        v[0] = win[15];
        v[kPhases + 1] = win[16];
        float p = std::abs(v[0]);
        for (int k = 0; k < kPhases; ++k)
        {
            float a = 0;
            const float *cc = c[(size_t)k].data();
            for (int j = 0; j < kTaps; ++j)
                a += cc[j] * win[j];
            v[k + 1] = a;
            p = std::max(p, std::abs(a));
        }
        for (int m = 1; m <= kPhases; ++m)
        {
            const float y0 = std::abs(v[m - 1]), y1 = std::abs(v[m]), y2 = std::abs(v[m + 1]);
            if (y1 >= y0 && y1 >= y2)
            {
                const float den = y0 - 2.f * y1 + y2;
                if (den < -1.0e-12f)
                {
                    const float delta = 0.5f * (y0 - y2) / den;
                    p = std::max(p, y1 - 0.25f * (y0 - y2) * delta);
                }
            }
        }
        return p;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// ITU-R BS.1770-4 / EBU R128 loudness meter (K-weighting, 400 ms momentary, 3 s short-term, gated integrated).
class LoudnessMeter
{
  public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPiD * f0 / sr), Vh = std::pow(10.0, G / 20.0),
                         Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPiD * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            rlb.b0 = 1.0;
            rlb.b1 = -2.0;
            rlb.b2 = 1.0;
            rlb.a1 = 2.0 * (K * K - 1.0) / a0;
            rlb.a2 = (1.0 - K / Q + K * K) / a0;
        }
        blockLen = std::max(1, (int)std::lround(0.1 * sr));
        for (auto &s : st1)
            s.reset();
        for (auto &s : st2)
            s.reset();
        shelfS[0].c = shelfS[1].c = shelf;
        rlbS[0].c = rlbS[1].c = rlb;
        resetAll();
    }
    void resetAll()
    {
        for (auto &s : shelfS)
            s.reset();
        for (auto &s : rlbS)
            s.reset();
        acc = 0;
        count = 0;
        ring.fill(0.0);
        ringW = 0;
        ringN = 0;
        hist.fill({});
        momentary = shortTerm = integrated = -120.f;
        gatedBlocks = 0;
    }
    void resetIntegrated()
    {
        hist.fill({});
        gatedBlocks = 0;
        integrated = -120.f;
    }
    inline void push(float l, float r) noexcept
    {
        double v[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const double y = rlbS[c].tick(shelfS[c].tick(v[c]));
            acc += y * y;
        }
        if (++count >= blockLen)
        {
            blockDone(acc / (double)blockLen);
            acc = 0;
            count = 0;
        }
    }
    float momentary = -120.f, shortTerm = -120.f, integrated = -120.f;

  private:
    struct Bin
    {
        double sum = 0;
        long n = 0;
    };
    static constexpr int kBinsPerDb = 10, kMinDb = -70, kMaxDb = 10, kBins = (kMaxDb - kMinDb) * kBinsPerDb;
    static inline float toLufs(double ms) { return ms <= 1.0e-12 ? -120.f : (float)(-0.691 + 10.0 * std::log10(ms)); }
    void blockDone(double ms)
    {
        ring[(size_t)ringW] = ms;
        ringW = (ringW + 1) % kRing;
        ringN = std::min(ringN + 1, kRing);
        auto avg = [&](int n) {
            n = std::min(n, ringN);
            double s = 0;
            for (int i = 0; i < n; ++i)
                s += ring[(size_t)((ringW - 1 - i + kRing) % kRing)];
            return n > 0 ? s / (double)n : 0.0;
        };
        momentary = toLufs(avg(4));
        shortTerm = toLufs(avg(30));
        if (ringN >= 4)
        {
            const double m = avg(4);
            const float l = toLufs(m);
            if (l >= (float)kMinDb)
            {
                const int b = std::min(kBins - 1, (int)std::floor((l - kMinDb) * kBinsPerDb));
                hist[(size_t)b].sum += m;
                hist[(size_t)b].n += 1;
                ++gatedBlocks;
            }
            if (gatedBlocks > 0)
            {
                double s = 0;
                long n = 0;
                for (const auto &h : hist)
                {
                    s += h.sum;
                    n += h.n;
                }
                const float rel = toLufs(s / (double)n) - 10.f;
                double s2 = 0;
                long n2 = 0;
                for (int b = 0; b < kBins; ++b)
                {
                    const float lo = kMinDb + (float)b / kBinsPerDb;
                    if (lo >= rel && hist[(size_t)b].n > 0)
                    {
                        s2 += hist[(size_t)b].sum;
                        n2 += hist[(size_t)b].n;
                    }
                }
                integrated = n2 > 0 ? toLufs(s2 / (double)n2) : -120.f;
            }
        }
    }
    static constexpr int kRing = 32;
    double sr = 48000;
    BiquadCoef shelf, rlb;
    Biquad shelfS[2], rlbS[2];
    Biquad st1[1], st2[1];
    double acc = 0;
    int count = 0, blockLen = 4800;
    std::array<double, kRing> ring{};
    int ringW = 0, ringN = 0;
    std::array<Bin, kBins> hist{};
    long gatedBlocks = 0;
};
} // namespace glaze
