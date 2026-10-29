// Small real-time-safe DSP building blocks (no allocation in process calls, no JUCE dependency).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace qz
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float sanitize (float x) { return std::isfinite (x) ? x : 0.0f; }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// One-pole smoothing value updated once per chunk.
struct Smooth
{
    float cur = 0, tgt = 0;
    void snap (float v) { cur = tgt = v; }
    float step (float a) { cur += (tgt - cur) * a; if (std::fabs (tgt - cur) < 1e-7f) cur = tgt; return cur; }
};
inline float smoothCoef (int nSamples, float seconds, float fs) { return 1.0f - std::exp (-(float) nSamples / (seconds * fs)); }

// Prevent denormals from slowing the audio thread.
struct DenormalGuard
{
   #if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
    unsigned int old;
    DenormalGuard() { old = __builtin_ia32_stmxcsr(); __builtin_ia32_ldmxcsr (old | 0x8040u); }
    ~DenormalGuard() { __builtin_ia32_ldmxcsr (old); }
   #else
    DenormalGuard() {}
   #endif
};

// Topology-preserving SVF (stable under modulation).
struct SVF
{
    float s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set (float fc, float Q, float fs)
    {
        fc = clampf (fc, 5.0f, fs * 0.47f);
        const float g = std::tan (kPi * fc / fs);
        k = 1.0f / Q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void process (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    inline float lowpass (float x)  { float l, b, h; process (x, l, b, h); return l; }
    inline float highpass (float x) { float l, b, h; process (x, l, b, h); return h; }
    inline float bandpass (float x) { float l, b, h; process (x, l, b, h); return b; }
    void reset() { s1 = s2 = 0; }
};

struct OnePoleLP
{
    float z = 0, a = 0;
    void set (float fc, float fs) { a = 1.0f - std::exp (-kTwoPi * fc / fs); }
    inline float process (float x) { z += a * (x - z); return z; }
    void reset() { z = 0; }
};

struct DCBlock
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void setup (float fs) { r = 1.0f - (kTwoPi * 12.0f / fs); }
    inline float process (float x) { float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};

// Fractional delay line (power-of-two buffer, linear/cubic read).
struct DelayLine
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d >= 1 : samples back from the most recent pushed sample (d=1 -> the sample pushed last)
    inline float read (float d) const
    {
        const int di = (int) d; const float f = d - (float) di;
        const int i0 = (w - di) & mask, i1 = (i0 - 1) & mask;
        return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
    inline float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
};

// Plain allpass (Schroeder), integer delay.
struct Allpass
{
    std::vector<float> buf; int pos = 0, len = 1; float g = 0.5f;
    void init (int n, float gain) { len = std::max (1, n); buf.assign ((size_t) len, 0.0f); pos = 0; g = gain; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline float process (float x)
    {
        const float d = buf[(size_t) pos];
        const float v = x - g * d;
        buf[(size_t) pos] = v;
        if (++pos >= len) pos = 0;
        return d + g * v;
    }
};

// xorshift noise
struct Noise
{
    uint32_t s = 22222u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

// Windowed-sinc half-band FIR for 2x oversampling (linear phase, odd length).
struct HalfBand
{
    static constexpr int kTaps = 63;           // group delay (taps-1)/2 = 31 high-rate samples per filter
    float h[kTaps];
    HalfBand()
    {
        const int c = kTaps / 2;
        float sum = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            const float t = (float) (i - c);
            const float sinc = (i == c) ? 0.5f : std::sin (kPi * 0.5f * t) / (kPi * t);
            const float u = (float) i / (float) (kTaps - 1);
            const float win = 0.42f - 0.5f * std::cos (kTwoPi * u) + 0.08f * std::cos (2 * kTwoPi * u);   // Blackman
            h[i] = sinc * win; sum += h[i];
        }
        for (int i = 0; i < kTaps; ++i) h[i] /= sum;      // unity DC gain
    }
};

struct Oversampler2x
{
    HalfBand hb;
    float upState[HalfBand::kTaps] = {}, dnState[HalfBand::kTaps] = {};
    int upPos = 0, dnPos = 0;
    static constexpr int latencyBaseSamples() { return HalfBand::kTaps - 1; }   // up + down filters = 62 hi-rate samples = 31 base-rate samples
    void reset() { std::memset (upState, 0, sizeof upState); std::memset (dnState, 0, sizeof dnState); upPos = dnPos = 0; }
    inline float fir (float* st, int& pos, float in)
    {
        st[pos] = in;
        float acc = 0; int idx = pos;
        for (int i = 0; i < HalfBand::kTaps; ++i) { acc += hb.h[i] * st[idx]; if (--idx < 0) idx = HalfBand::kTaps - 1; }
        if (++pos >= HalfBand::kTaps) pos = 0;
        return acc;
    }
    // upsample: returns two high-rate samples for one input
    inline void up (float x, float& o0, float& o1) { o0 = 2.0f * fir (upState, upPos, x); o1 = 2.0f * fir (upState, upPos, 0.0f); }
    inline float down (float a, float b) { const float r = fir (dnState, dnPos, a); fir (dnState, dnPos, b); return r; }
};
} // namespace qz
