#pragma once
// cmx::Oversampler: 2x/4x/8x/16x polyphase IIR halfband up/down sampler (elliptic allpass pairs, de Soras design).
// Minimum-phase-like, no integer latency to report (a few samples of group delay at high frequencies only).
// Use per channel: up (x, hi) fills hi[0..F-1] at F*fs; run the nonlinearity on hi; y = down (hi).
#include <array>
#include <utility>
#include <cstddef>
namespace cmx
{
template <int N>
struct Halfband
{
    std::array<float, N> c {}, x {}, y {};
    void reset() { x.fill (0.0f); y.fill (0.0f); }
    // two paths: even coefs on s0, odd coefs on s1
    inline void stage (float& s0, float& s1)
    {
        int i = 0;
        for (; i + 1 < N; i += 2)
        {
            const float t0 = x[(size_t) i], t1 = x[(size_t) i + 1];
            x[(size_t) i] = s0; x[(size_t) i + 1] = s1;
            s0 = (s0 - y[(size_t) i]) * c[(size_t) i] + t0;
            s1 = (s1 - y[(size_t) i + 1]) * c[(size_t) i + 1] + t1;
            y[(size_t) i] = s0; y[(size_t) i + 1] = s1;
        }
        if (i < N)
        {
            const float t0 = x[(size_t) i]; x[(size_t) i] = s0;
            s0 = (s0 - y[(size_t) i]) * c[(size_t) i] + t0; y[(size_t) i] = s0;
        }
    }
    inline void up (float in, float& o0, float& o1) { float s0 = in, s1 = in; stage (s0, s1); o0 = s0; o1 = s1; }
    inline float down (float a, float b) { float s0 = b, s1 = a; stage (s0, s1); return 0.5f * (s0 + s1); }
};

class Oversampler
{
public:
    // factor 1, 2, 4 or 8
    void setFactor (int f) { F = f >= 16 ? 16 : f >= 8 ? 8 : f >= 4 ? 4 : f >= 2 ? 2 : 1; reset(); }
    int factor() const { return F; }
    void reset() { for (auto* h : { &u1, &d1 }) h->reset(); for (auto* h : { &u2, &d2 }) h->reset(); for (auto* h : { &u3, &d3 }) h->reset(); for (auto* h : { &u4, &d4 }) h->reset(); }
    Oversampler()
    {
        const float a[] = { 0.029281335951567022f, 0.11057561422770555f, 0.22716737191076788f, 0.35889539632252332f, 0.48847433709752414f, 0.60469436668404419f, 0.7024814804315771f, 0.78127749683662318f, 0.84313624835644707f, 0.89125435756077287f, 0.92909106562237864f, 0.95996869059793144f, 0.98699367530182835f }, b[] = { 0.04790845477363561f, 0.17951160525949356f, 0.36780928458752388f, 0.59071801647357025f, 0.84841479122482688f }, e[] = { 0.053379036964928725f, 0.20558897612802121f, 0.44371079776028416f, 0.77747763544575854f };
        for (int i = 0; i < 13; ++i) u1.c[(size_t) i] = d1.c[(size_t) i] = a[i];
        for (int i = 0; i < 5; ++i) u2.c[(size_t) i] = d2.c[(size_t) i] = b[i];
        for (int i = 0; i < 4; ++i) u3.c[(size_t) i] = d3.c[(size_t) i] = e[i];
        const float q[] = { 0.076382693288819331f, 0.30208200300175508f, 0.69878248805529986f };
        for (int i = 0; i < 3; ++i) u4.c[(size_t) i] = d4.c[(size_t) i] = q[i];
    }
    // hi must hold F samples
    inline void up (float x, float* hi)
    {
        if (F == 1) { hi[0] = x; return; }
        float a0, a1; u1.up (x, a0, a1);
        if (F == 2) { hi[0] = a0; hi[1] = a1; return; }
        float b[4]; u2.up (a0, b[0], b[1]); u2.up (a1, b[2], b[3]);
        if (F == 4) { for (int i = 0; i < 4; ++i) hi[i] = b[i]; return; }
        if (F == 8) { for (int i = 0; i < 4; ++i) u3.up (b[i], hi[2 * i], hi[2 * i + 1]); return; }
        float e[8]; for (int i = 0; i < 4; ++i) u3.up (b[i], e[2 * i], e[2 * i + 1]);
        for (int i = 0; i < 8; ++i) u4.up (e[i], hi[2 * i], hi[2 * i + 1]);
    }
    inline float down (const float* hi)
    {
        if (F == 1) return hi[0];
        if (F == 2) return d1.down (hi[0], hi[1]);
        float b[4];
        if (F == 4) { for (int i = 0; i < 4; ++i) b[i] = hi[i]; }
        else if (F == 8) { for (int i = 0; i < 4; ++i) b[i] = d3.down (hi[2 * i], hi[2 * i + 1]); }
        else { float e[8]; for (int i = 0; i < 8; ++i) e[i] = d4.down (hi[2 * i], hi[2 * i + 1]); for (int i = 0; i < 4; ++i) b[i] = d3.down (e[2 * i], e[2 * i + 1]); }
        const float m0 = d2.down (b[0], b[1]);   // sequenced: argument order is unspecified in C++
        const float m1 = d2.down (b[2], b[3]);
        return d1.down (m0, m1);
    }
private:
    int F = 1;
    Halfband<13> u1, d1; Halfband<5> u2, d2; Halfband<4> u3, d3; Halfband<3> u4, d4;
};
} // namespace cmx
