#pragma once
// CMPLXTY Vocal Aligner - offline alignment engine (no JUCE, so the test builds with a plain compiler).
//
//   1. features:  10 ms frames, 24 log-spaced band energies (spectral shape, mean-removed) + loudness + loudness change
//   2. timing:    dynamic time warping of dub frames onto guide frames inside a +/- "max shift" band, then the path is smoothed
//                 ("tightness" = how closely it follows every little difference; low = only the broad timing moves)
//   3. pitch:     optional - YIN pitch of both takes, per-frame ratio guide/dub (limited to +/- 7 semitones), "pitch amount" scales it
//   4. render:    WSOLA (waveform-similarity overlap-add) reads the dub along the warp map; with pitch, each grain is resampled
//
// Everything runs off the audio thread. Cancel by setting the atomic flag; progress 0..1 is reported through a callback.
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include <atomic>
#include <functional>
#include <cstdint>
#include <limits>

namespace va::dsp
{
constexpr float kPi = 3.14159265358979f;

struct Settings
{
    float tightness = 0.7f;     // 0..1
    float maxShiftMs = 300.0f;  // DTW band half-width
    float pitchAmount = 0.0f;   // 0..1
};

// ---------------------------------------------------------------------------------------------------- small radix-2 FFT
inline void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * kPi / (float) len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

constexpr int kBands = 24;
constexpr int kFeat = kBands + 2;

struct Features
{
    int frames = 0, hop = 0;
    std::vector<float> f;        // frames * kFeat
    std::vector<float> loud;     // frame loudness in dB
    const float* at (int i) const { return f.data() + (size_t) i * kFeat; }
};

inline Features features (const float* x, int n, double sr)
{
    Features F;
    F.hop = std::max (1, (int) std::lround (sr * 0.01));
    const int win = 2 * F.hop;
    int N = 1; while (N < win) N <<= 1;
    F.frames = std::max (1, (n + F.hop - 1) / F.hop);
    F.f.assign ((size_t) F.frames * kFeat, 0.0f);
    F.loud.assign ((size_t) F.frames, -120.0f);
    std::vector<float> w ((size_t) win);
    for (int i = 0; i < win; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) win);
    // band edges (bins), log spaced 100 Hz .. 8 kHz (or Nyquist)
    int edge[kBands + 1];
    const double fHi = std::min (8000.0, sr * 0.45);
    for (int b = 0; b <= kBands; ++b)
    {
        const double hz = 100.0 * std::pow (fHi / 100.0, (double) b / kBands);
        edge[b] = std::max (1, (int) std::lround (hz * N / sr));
    }
    for (int b = 1; b <= kBands; ++b) edge[b] = std::max (edge[b], edge[b - 1] + 1);
    std::vector<std::complex<float>> buf ((size_t) N);
    float prevLoud = -80.0f;
    for (int fr = 0; fr < F.frames; ++fr)
    {
        const int start = fr * F.hop - F.hop / 2;
        double e = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const int k = start + i;
            const float s = (i < win && k >= 0 && k < n) ? x[k] * w[(size_t) i] : 0.0f;
            buf[(size_t) i] = { s, 0.0f }; e += (double) s * s;
        }
        fft (buf);
        float* o = F.f.data() + (size_t) fr * kFeat;
        float mean = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            double sum = 0.0;
            for (int k = edge[b]; k < edge[b + 1] && k < N / 2; ++k) sum += (double) std::norm (buf[(size_t) k]);
            o[b] = (float) std::log10 (1e-9 + sum / std::max (1, edge[b + 1] - edge[b]));
            mean += o[b];
        }
        mean /= kBands;
        const float loud = (float) (10.0 * std::log10 (1e-12 + e / win));
        F.loud[(size_t) fr] = loud;
        // in quiet frames the spectral shape is noise: fade it out so silence matches silence
        const float shapeW = std::clamp ((loud + 60.0f) / 20.0f, 0.0f, 1.0f);
        for (int b = 0; b < kBands; ++b) o[b] = (o[b] - mean) * shapeW;
        o[kBands] = std::max (loud, -70.0f) / 12.0f;
        o[kBands + 1] = std::clamp (std::max (loud, -70.0f) - std::max (prevLoud, -70.0f), -30.0f, 30.0f) / 6.0f;
        prevLoud = loud;
    }
    return F;
}

inline float dist (const float* a, const float* b)
{
    float s = 0.0f;
    for (int k = 0; k < kFeat; ++k) { const float d = a[k] - b[k]; s += d * d; }
    return std::sqrt (s);
}

// ---------------------------------------------------------------------------------------------------- DTW -> warp map
// Returns, for every guide frame, the (fractional) dub frame that should sound there.
inline std::vector<float> warpMap (const Features& g, const Features& d, const Settings& s, const std::atomic<bool>* cancel = nullptr,
                                   const std::function<void (float)>& progress = {})
{
    const int N = g.frames, M = d.frames;
    const int B = std::max (2, (int) std::lround (s.maxShiftMs / 10.0f));
    const int W = 2 * B + 1;
    const float inf = std::numeric_limits<float>::max() / 4.0f;
    const float pen = 0.15f;                         // small cost for non-diagonal steps (keeps the path from wandering in steady sounds)
    std::vector<uint8_t> dir ((size_t) N * W, 0);   // 0 diag, 1 from (i-1, j) [dub held], 2 from (i, j-1) [dub skipped]
    std::vector<float> prev ((size_t) W, inf), cur ((size_t) W, inf);
    auto lo = [&] (int i) { return i - B; };          // band: j in [i-B, i+B] (both takes share one timeline)
    for (int i = 0; i < N; ++i)
    {
        if (cancel != nullptr && cancel->load()) return {};
        if (progress && (i & 1023) == 0) progress (0.5f * (float) i / (float) N);
        std::fill (cur.begin(), cur.end(), inf);
        for (int k = 0; k < W; ++k)
        {
            const int j = lo (i) + k;
            if (j < 0 || j >= M) continue;
            const float c = dist (g.at (i), d.at (j));
            float best = inf; uint8_t bd = 0;
            if (i == 0 && j <= B) { best = (float) j * pen; bd = 2; if (j == 0) bd = 0; }
            if (i > 0)
            {
                // previous row index of (i-1, j-1) is k (band shifts by one), (i-1, j) is k+1
                if (k < W && j - 1 >= 0) { const float v = prev[(size_t) k]; if (v < best) { best = v; bd = 0; } }
                if (k + 1 < W) { const float v = prev[(size_t) k + 1] + pen; if (v < best) { best = v; bd = 1; } }
            }
            if (k > 0 && j - 1 >= 0) { const float v = cur[(size_t) k - 1] + pen; if (v < best) { best = v; bd = 2; } }
            if (best >= inf) continue;
            cur[(size_t) k] = best + c;
            dir[(size_t) i * W + (size_t) k] = bd;
        }
        std::swap (prev, cur);
    }
    // end: best cell in the last row (open end inside the band)
    int bk = -1; float bv = inf;
    for (int k = 0; k < W; ++k) { const int j = lo (N - 1) + k; if (j >= 0 && j < M && prev[(size_t) k] < bv) { bv = prev[(size_t) k]; bk = k; } }
    std::vector<float> sum ((size_t) N, 0.0f), cnt ((size_t) N, 0.0f);
    if (bk < 0) { std::vector<float> id ((size_t) N); for (int i = 0; i < N; ++i) id[(size_t) i] = (float) i; return id; }
    int i = N - 1, k = bk;
    while (i >= 0)
    {
        const int j = lo (i) + k;
        sum[(size_t) i] += (float) j; cnt[(size_t) i] += 1.0f;
        const uint8_t bd = dir[(size_t) i * W + (size_t) k];
        if (i == 0 && bd != 2) break;
        if (bd == 0) { --i; }                 // (i-1, j-1): same k
        else if (bd == 1) { --i; ++k; }       // (i-1, j)
        else { --k; if (k < 0) break; }       // (i, j-1)
    }
    std::vector<float> map ((size_t) N);
    for (int n = 0; n < N; ++n) map[(size_t) n] = cnt[(size_t) n] > 0.0f ? sum[(size_t) n] / cnt[(size_t) n] : (float) n;
    // smooth: tightness 1 -> 3 frames, 0 -> 41 frames (centred moving average on the offset map-i), then keep it monotonic
    const int half = 1 + (int) std::lround ((1.0f - std::clamp (s.tightness, 0.0f, 1.0f)) * 20.0f);
    std::vector<double> off ((size_t) N + 1, 0.0);
    for (int n = 0; n < N; ++n) off[(size_t) n + 1] = off[(size_t) n] + (map[(size_t) n] - (float) n);
    std::vector<float> sm ((size_t) N);
    for (int n = 0; n < N; ++n)
    {
        const int a = std::max (0, n - half), b = std::min (N, n + half + 1);
        sm[(size_t) n] = (float) n + (float) ((off[(size_t) b] - off[(size_t) a]) / (b - a));
    }
    for (int n = 1; n < N; ++n) sm[(size_t) n] = std::max (sm[(size_t) n], sm[(size_t) n - 1] + 0.05f);   // never runs backwards
    return sm;
}

// ---------------------------------------------------------------------------------------------------- pitch (YIN on a 4x decimated copy)
inline std::vector<float> pitchTrack (const float* x, int n, double sr, int hop, int frames)
{
    const int dec = std::max (1, (int) std::lround (sr / 12000.0));
    const double dsr = sr / dec;
    const int nd = n / dec;
    std::vector<float> y ((size_t) std::max (1, nd));
    float lp1 = 0.0f, lp2 = 0.0f; const float a = std::exp (-2.0f * kPi * 4000.0f / (float) sr);
    for (int i = 0, o = 0; i < n && o < nd; ++i)
    {
        lp1 = x[i] + a * (lp1 - x[i]); lp2 = lp1 + a * (lp2 - lp1);
        if (i % dec == dec - 1) y[(size_t) o++] = lp2;
    }
    const int W = (int) (dsr * 0.03);
    const int tMin = (int) (dsr / 1000.0), tMax = (int) (dsr / 70.0);
    std::vector<float> f0 ((size_t) frames, 0.0f), dd ((size_t) tMax + 2);
    for (int fr = 0; fr < frames; ++fr)
    {
        const int c = (int) ((double) fr * hop / dec) - W / 2;
        if (c < 0 || c + W + tMax >= nd) continue;
        double e = 0.0; for (int i = 0; i < W; ++i) e += (double) y[(size_t) (c + i)] * y[(size_t) (c + i)];
        if (e / W < 1e-6) continue;   // below about -60 dBFS: unvoiced
        dd[0] = 1.0f; double run = 0.0; int best = -1;
        for (int t = 1; t <= tMax; ++t)
        {
            double s = 0.0;
            for (int i = 0; i < W; ++i) { const float df = y[(size_t) (c + i)] - y[(size_t) (c + i + t)]; s += (double) df * df; }
            run += s;
            dd[(size_t) t] = (float) (run > 0.0 ? s * t / run : 1.0);
            if (t >= tMin + 1 && best < 0 && dd[(size_t) t - 1] < 0.15f && dd[(size_t) t - 1] <= dd[(size_t) t]) best = t - 1;
        }
        if (best < 0) continue;
        float tt = (float) best;
        if (best > 1 && best < tMax) { const float s0 = dd[(size_t) best - 1], s1 = dd[(size_t) best], s2 = dd[(size_t) best + 1]; const float den = s0 - 2 * s1 + s2; if (std::fabs (den) > 1e-9f) tt += 0.5f * (s0 - s2) / den; }
        f0[(size_t) fr] = (float) (dsr / tt);
    }
    return f0;
}

// ---------------------------------------------------------------------------------------------------- render
struct Result
{
    std::vector<float> audio;   // aligned dub, on the guide's timeline (same start, guide length)
    std::vector<float> map;     // guide frame -> dub frame
    std::vector<float> ratio;   // per guide frame pitch ratio applied (1 = none)
    int hop = 0;
    bool ok = false;
};

inline float readLinear (const float* x, int n, double pos)
{
    const int i = (int) std::floor (pos);
    if (i < 0 || i + 1 >= n) return (i >= 0 && i < n) ? x[i] : 0.0f;
    const float fr = (float) (pos - i);
    return x[i] + fr * (x[i + 1] - x[i]);
}

inline Result align (const float* guide, int ng, const float* dub, int nd, double sr, const Settings& s,
                     const std::atomic<bool>* cancel = nullptr, const std::function<void (float)>& progress = {})
{
    Result R;
    if (ng < 64 || nd < 64 || sr < 8000.0) return R;
    const auto G = features (guide, ng, sr), D = features (dub, nd, sr);
    if (progress) progress (0.1f);
    R.map = warpMap (G, D, s, cancel, [&] (float p) { if (progress) progress (0.1f + p); });
    if (R.map.empty()) return R;
    R.hop = G.hop;
    const int N = G.frames;
    R.ratio.assign ((size_t) N, 1.0f);
    if (s.pitchAmount > 0.001f)
    {
        const auto fg = pitchTrack (guide, ng, sr, G.hop, G.frames);
        const auto fd = pitchTrack (dub, nd, sr, D.hop, D.frames);
        std::vector<float> cents ((size_t) N, 0.0f); std::vector<uint8_t> v ((size_t) N, 0);
        for (int i = 0; i < N; ++i)
        {
            const int j = std::clamp ((int) std::lround (R.map[(size_t) i]), 0, D.frames - 1);
            if (fg[(size_t) i] > 0.0f && fd[(size_t) j] > 0.0f)
            {
                float c = 1200.0f * std::log2 (fg[(size_t) i] / fd[(size_t) j]);
                while (c > 600.0f) c -= 1200.0f;  while (c < -600.0f) c += 1200.0f;   // octave errors of either tracker
                cents[(size_t) i] = c; v[(size_t) i] = 1;
            }
        }
        // median of 5 over voiced frames, then a short glide so the correction never steps
        std::vector<float> med = cents;
        for (int i = 0; i < N; ++i)
        {
            if (! v[(size_t) i]) continue;
            float w[5]; int m = 0;
            for (int k = -2; k <= 2; ++k) { const int t = i + k; if (t >= 0 && t < N && v[(size_t) t]) w[m++] = cents[(size_t) t]; }
            std::nth_element (w, w + m / 2, w + m); med[(size_t) i] = w[m / 2];
        }
        float st = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float target = v[(size_t) i] ? std::clamp (med[(size_t) i], -700.0f, 700.0f) * s.pitchAmount : 0.0f;
            st += 0.35f * (target - st);
            R.ratio[(size_t) i] = std::exp2 (st / 1200.0f);
        }
    }
    if (progress) progress (0.65f);

    // WSOLA
    const int L = std::max (64, ((int) (sr * 0.032)) & ~1), Ha = L / 2, tol = L / 4;
    std::vector<float> win ((size_t) L);
    for (int i = 0; i < L; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) L);   // periodic Hann: sums to 1 at 50 %
    R.audio.assign ((size_t) ng, 0.0f);
    std::vector<float> grain ((size_t) L);
    double prevSrc = -1.0; float prevRatio = 1.0f;
    const int frames = (ng + Ha - 1) / Ha + 1;
    auto mapAt = [&] (double outSample)
    {
        const double fpos = outSample / G.hop;
        const int i = (int) std::floor (fpos);
        if (i < 0) return (double) R.map[0] * D.hop + outSample;
        if (i >= N - 1) return (double) R.map[(size_t) N - 1] * D.hop + (outSample - (double) (N - 1) * G.hop);
        const double fr = fpos - i;
        return ((1.0 - fr) * R.map[(size_t) i] + fr * R.map[(size_t) i + 1]) * D.hop;
    };
    for (int k = 0; k < frames; ++k)
    {
        if (cancel != nullptr && cancel->load()) { R.audio.clear(); return R; }
        if (progress && (k & 511) == 0) progress (0.65f + 0.35f * (float) k / (float) frames);
        const int outStart = k * Ha - Ha;   // grain centre = k * Ha
        const int fi = std::clamp ((int) ((double) (outStart + Ha) / G.hop), 0, N - 1);
        const float r = R.ratio[(size_t) fi];
        const double span = (double) L * r;                       // source samples read for this grain
        const double nominal = mapAt (outStart + Ha) - span * 0.5; // grain start in the dub
        double best = nominal;
        if (prevSrc >= 0.0)
        {
            // natural continuation of the previous grain; pick the offset whose start looks most like it (coarse then fine search)
            const double cont = prevSrc + (double) Ha * prevRatio;
            const int cmpLen = Ha;
            auto score = [&] (double st)
            {
                double s2 = 0.0;
                for (int i = 0; i < cmpLen; i += 2) s2 += (double) readLinear (dub, nd, cont + i * r) * readLinear (dub, nd, st + i * r);
                return s2;
            };
            double bs = -1e300; int bdl = 0;
            for (int dl = -tol; dl <= tol; dl += 4) { const double v = score (nominal + dl); if (v > bs) { bs = v; bdl = dl; } }
            for (int dl = bdl - 3; dl <= bdl + 3; ++dl) { const double v = score (nominal + dl); if (v > bs) { bs = v; best = nominal + dl; } }
            if (bs <= 0.0 && bdl == 0) best = nominal;
        }
        for (int i = 0; i < L; ++i) grain[(size_t) i] = readLinear (dub, nd, best + i * r) * win[(size_t) i];
        for (int i = 0; i < L; ++i) { const int o = outStart + i; if (o >= 0 && o < ng) R.audio[(size_t) o] += grain[(size_t) i]; }
        prevSrc = best; prevRatio = r;
    }
    if (progress) progress (1.0f);
    R.ok = true;
    return R;
}
} // namespace va::dsp
