#pragma once
// CMPLXTY Dr Fill engine (plain C++17, no JUCE, header only so the DSP tests build on their own).
//  1. analyse(): onset detection on one drum loop (log spectral flux, adaptive threshold, transient refined in the time domain),
//     slices every hit (short fade-in before the transient, ends just before the next hit) and sorts the hits
//     (main / alternates / ghosts; for hats: closed / open / crash by how long they ring).
//  2. estimateTempo(): tempo from the onsets of all loaded loops together (16th / triplet grid fit + loop-length check + file-name hint).
//  3. generate(): a seeded drum fill (rolls, builds, tom-style pitched snare runs, triplets, flams, break chops, stutters)
//     as a list of events in beats. Its own random generator, so a seed gives the same fill on Windows, Mac and Linux.
//  4. render(): plays the slices at the given tempo / sample rate into one stereo buffer per part (kick, snare, hats).
//     Every slice has a raised-cosine fade in and out (no clicks), parts are choked like a real kit, and the whole render is turned down
//     together if the full fill would go over -1 dBFS (the parts still add up exactly to the full fill).
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace drf
{
// 1.1.0: hats and cymbals are separate lanes (Sam), plus an optional percussion lane
enum Lane { kKick = 0, kSnare = 1, kHats = 2, kCym = 3, kPerc = 4, kLanes = 5 };
inline bool isMetal (int l) { return l == kHats || l == kCym; }   // decay-classified lanes (closed / open / crash)
inline const char* laneName (int l) { static const char* n[] = { "Kick", "Snare", "Hats", "Cymbals", "Perc" }; return l >= 0 && l < kLanes ? n[l] : "?"; }

enum Style { kAuto = 0, kRoll, kBuild, kToms, kTriplets, kFlams, kBreak, kStutter, kNumStyles };
inline const char* styleName (int s)
{
    static const char* n[] = { "Auto", "Snare Roll", "Build-Up", "Tom Run", "Triplets", "Flams", "Break Chop", "Stutter" };
    return s >= 0 && s < kNumStyles ? n[s] : "?";
}
enum Ending { kEndOff = 0, kEndOpenHat, kEndCrash };
enum HatKind { kClosed = 0, kOpen = 1, kCrash = 2 };

struct Audio
{
    std::vector<float> L, R;   // R empty = mono
    double rate = 44100.0;
    int len() const noexcept { return (int) L.size(); }
    bool stereo() const noexcept { return ! R.empty(); }
    float at (int ch, int i) const noexcept { return (ch == 1 && stereo()) ? R[(size_t) i] : L[(size_t) i]; }
};

struct Hit
{
    int start = 0, onset = 0, end = 0;   // source samples: slice start (fade-in starts), transient, slice end
    float peak = 0, decayMs = 0, bright = 0;
    int kind = 0;                        // hats lane: HatKind; kick / snare: 0 normal, 1 ghost
};

struct Loop
{
    Audio audio;
    std::string name;
    std::vector<Hit> hits;
    std::vector<float> overview;         // 512 peak points for the UI
    int main = -1;                       // the strongest normal hit
    double lengthSec = 0;
    bool empty() const noexcept { return audio.L.empty() || hits.empty(); }
};
using Kit = std::array<Loop, kLanes>;

// ------------------------------------------------------------------------------------------------ small helpers
namespace detail
{
// portable random numbers (std:: distributions differ between standard libraries)
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { for (int i = 0; i < 4; ++i) next(); }
    uint32_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return (uint32_t) ((s * 0x2545F4914F6CDD1Dull) >> 32); }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }                    // [0, 1)
    float sym() { return uni() * 2.0f - 1.0f; }
    bool chance (float p) { return uni() < p; }
    int pick (int n) { return n <= 1 ? 0 : (int) (uni() * (float) n) % n; }
    float gauss() { const float a = std::max (1e-7f, uni()), b = uni(); return std::sqrt (-2.0f * std::log (a)) * std::cos (6.2831853f * b); }
};

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
        const float ang = -6.28318530718f / (float) len;
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
inline float raisedCos (float x) { return x <= 0.0f ? 0.0f : x >= 1.0f ? 1.0f : 0.5f - 0.5f * std::cos (3.14159265f * x); }
inline float hermite (const std::vector<float>& d, double pos)
{
    const int n = (int) d.size(), i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto s = [&] (int k) { return (k >= 0 && k < n) ? d[(size_t) k] : 0.0f; };
    const float xm = s (i - 1), x0 = s (i), x1 = s (i + 1), x2 = s (i + 2);
    const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
} // namespace detail

// ------------------------------------------------------------------------------------------------ 1. onsets + slicing
// sensitivity 0..1 (0.5 default): higher finds quieter hits (ghost notes).
// Returns the onset sample positions (transient starts) of the loop.
inline std::vector<int> detectOnsets (const Audio& a, float sensitivity = 0.5f, std::vector<float>* strengthOut = nullptr)
{
    std::vector<int> out;
    const int n = a.len();
    if (n < 64) return out;
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i) m[(size_t) i] = a.stereo() ? 0.5f * (a.L[(size_t) i] + a.R[(size_t) i]) : a.L[(size_t) i];

    int N = 512; while (N < a.rate * 0.020) N <<= 1;          // ~21 ms window (1024 at 44.1 / 48 k, 2048 at 96 k)
    const int hop = std::max (32, (int) std::lround (a.rate * 0.0029));   // ~2.9 ms
    const int lead = N / 2;                                    // frames start half a window early: a hit at sample 0 is found too
    const int frames = (n + lead) / hop + 1, bins = N / 2;
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N);
    std::vector<float> prev ((size_t) bins, 0.0f), cur ((size_t) bins), flux ((size_t) frames, 0.0f), energy ((size_t) frames, 0.0f);
    std::vector<std::complex<float>> buf ((size_t) N);
    for (int f = 0; f < frames; ++f)
    {
        const int c = f * hop - lead;                          // frame centred on sample c
        double e = 0;
        for (int i = 0; i < N; ++i)
        {
            const int k = c - N / 2 + i;
            const float x = (k >= 0 && k < n) ? m[(size_t) k] : 0.0f;
            buf[(size_t) i] = { x * win[(size_t) i], 0.0f }; e += (double) x * x;
        }
        energy[(size_t) f] = (float) (e / N);
        detail::fft (buf);
        float fl = 0;
        for (int k = 1; k < bins; ++k)
        {
            cur[(size_t) k] = std::log1p (100.0f * std::abs (buf[(size_t) k]));
            fl += std::max (0.0f, cur[(size_t) k] - prev[(size_t) k]);
        }
        flux[(size_t) f] = fl;
        std::swap (prev, cur);
    }
    float fmax = 1e-9f, emax = 1e-12f;
    for (int f = 0; f < frames; ++f) { fmax = std::max (fmax, flux[(size_t) f]); emax = std::max (emax, energy[(size_t) f]); }
    const float s = std::min (1.0f, std::max (0.0f, sensitivity));
    const float delta = std::max (0.01f, 0.13f - 0.14f * s) * fmax;             // 0.13 (strict) .. -0.01 (finds ghosts) of the loudest onset
    const int w = 3, preAvg = 12, postAvg = 4, minGap = std::max (1, (int) std::lround (0.028 * a.rate / hop));
    int last = -1000000;
    std::vector<int> peakFrames;
    for (int f = 1; f < frames - 1; ++f)
    {
        const float v = flux[(size_t) f];
        bool isMax = true;
        for (int k = std::max (0, f - w); k <= std::min (frames - 1, f + w); ++k) if (flux[(size_t) k] > v) { isMax = false; break; }
        if (! isMax) continue;
        double avg = 0; int cnt = 0;
        for (int k = std::max (0, f - preAvg); k <= std::min (frames - 1, f + postAvg); ++k) { avg += flux[(size_t) k]; ++cnt; }
        avg /= std::max (1, cnt);
        if (v < (float) avg + delta) continue;
        float eNear = 0; for (int k = f; k <= std::min (frames - 1, f + 4); ++k) eNear = std::max (eNear, energy[(size_t) k]);
        if (eNear < emax * 1e-5f) continue;                     // -50 dB below the loudest frame: noise
        if (f - last < minGap) { if (! peakFrames.empty() && v > flux[(size_t) peakFrames.back()]) { peakFrames.back() = f; last = f; } continue; }
        peakFrames.push_back (f); last = f;
    }
    // refine each onset in the time domain on the first difference (pre-emphasis: transients stand out of an earlier hit's tail):
    // envelope = max |d| over 0.25 ms blocks; onset = first block that rises 15 % of the way
    // from the quiet point before the hit to the hit's peak
    const int blk = std::max (4, (int) std::lround (a.rate * 0.00025));
    for (size_t pi = 0; pi < peakFrames.size(); ++pi)
    {
        const int f = peakFrames[pi], c = std::max (0, f * hop - lead);
        const int lo = std::max (pi > 0 ? out.empty() ? 0 : out.back() + blk * 8 : 0, c - (int) (0.024 * a.rate));
        const int hi = std::min (n - 1, c + (int) (0.016 * a.rate));
        if (hi - lo < blk * 4) continue;
        std::vector<float> env;
        for (int i = lo; i < hi; i += blk) { float mx = 0; for (int k = i; k < std::min (hi, i + blk); ++k) mx = std::max (mx, std::fabs (m[(size_t) k])); env.push_back (mx); }
        // the hit's peak: after the flux frame's centre (earlier blocks can still be the louder tail of the hit before)
        const int pk0 = std::min ((int) env.size() - 1, std::max (0, (c - (int) (0.008 * a.rate) - lo) / blk));
        int pk = pk0; for (int i = pk0 + 1; i < (int) env.size(); ++i) if (env[(size_t) i] > env[(size_t) pk]) pk = i;
        // quiet point: walk back from the peak while the envelope keeps falling (stops at the tail of the previous hit)
        // (on a 10 ms trailing max, so the wiggles inside a noisy hat or a low snare body don't stop it)
        std::vector<float> e1 (env.size());
        const size_t sm = (size_t) std::max (1, (int) std::lround (0.010 * a.rate / blk));
        for (size_t i = 0; i < env.size(); ++i) { float mx = 0; for (size_t j = i >= sm ? i - sm : 0; j <= i; ++j) mx = std::max (mx, env[j]); e1[i] = mx; }
        const float top = env[(size_t) pk];
        float quiet = e1[(size_t) pk]; int qi = pk;
        for (int i = pk - 1; i >= 0; --i)
        {
            if (e1[(size_t) i] < quiet) { quiet = e1[(size_t) i]; qi = i; }
            else if (e1[(size_t) i] > quiet + 0.25f * top) break;   // rising again: the previous hit
            if (quiet < 0.04f * top) break;
        }
        const float th = quiet + 0.15f * (env[(size_t) pk] - quiet);
        int on = pk;
        for (int i = qi; i <= pk; ++i) if (env[(size_t) i] >= th) { on = i; break; }
        // the rise into the peak must be continuous: if the envelope drops under the threshold again after 'on'
        // (an earlier hit's tail was still above it), the onset is where that last drop ends
        // (a real gap: 3 ms under the threshold, so the zero crossings of a low kick don't count)
        {
            const int gw = std::max (1, (int) std::lround (0.003 * a.rate / blk));
            for (int i = pk; i > on + gw; --i)
            {
                bool gap = true; for (int j = i - gw; j < i && gap; ++j) gap = env[(size_t) j] < th;
                if (gap) { on = i; break; }
            }
        }
        // inside the block: first sample over the threshold
        int sOn = lo + on * blk;
        for (int k = lo + on * blk; k < std::min (hi, lo + (on + 1) * blk); ++k) if (std::fabs (m[(size_t) k]) >= th) { sOn = k; break; }
        if (! out.empty() && sOn - out.back() < (int) (0.02 * a.rate)) continue;
        // a real hit rises clearly over what was already sounding (rejects room echoes / flutter and noise)
        {
            float before = 0, after = 0;
            const int b0 = std::max (0, sOn - (int) (0.015 * a.rate)), b1 = std::max (0, sOn - (int) (0.002 * a.rate));
            for (int k = b0; k < b1; ++k) before = std::max (before, std::fabs (m[(size_t) k]));
            for (int k = sOn; k < std::min (n, sOn + (int) (0.012 * a.rate)); ++k) after = std::max (after, std::fabs (m[(size_t) k]));
            if (after < before * 1.25f + 1e-5f) continue;
        }
        out.push_back (sOn);
        if (strengthOut) strengthOut->push_back (flux[(size_t) f] / fmax);
    }
    return out;
}

// Slices one loop and classifies its hits. lane decides the classification (kick / snare: main + ghosts; hats: closed / open / crash).
inline void analyse (Loop& lp, int lane, float sensitivity = 0.5f)
{
    lp.hits.clear(); lp.main = -1; lp.overview.assign (512, 0.0f);
    const auto& a = lp.audio;
    const int n = a.len();
    lp.lengthSec = n / a.rate;
    if (n == 0) return;
    for (int i = 0; i < n; ++i)
    {
        const size_t b = (size_t) ((int64_t) i * 512 / n);
        lp.overview[b] = std::max (lp.overview[b], std::max (std::fabs (a.L[(size_t) i]), a.stereo() ? std::fabs (a.R[(size_t) i]) : 0.0f));
    }
    const auto on = detectOnsets (a, sensitivity);
    const int pre = (int) std::lround (0.003 * a.rate), maxLen = (int) (3.0 * a.rate);
    auto absAt = [&] (int i) { return std::max (std::fabs (a.L[(size_t) i]), a.stereo() ? std::fabs (a.R[(size_t) i]) : 0.0f); };
    for (size_t i = 0; i < on.size(); ++i)
    {
        Hit h;
        h.onset = on[i];
        h.start = std::max (i > 0 ? on[i - 1] + (on[i] - on[i - 1]) / 2 : 0, on[i] - pre);
        h.end = i + 1 < on.size() ? std::max (h.onset + 16, on[i + 1] - pre) : n;
        h.end = std::min ({ h.end, n, h.onset + maxLen });
        // peak + brightness (zero-crossing rate) over the first 30 ms
        const int a30 = std::min (h.end, h.onset + (int) (0.03 * a.rate));
        int pkAt = h.onset; int zc = 0; float prevS = 0;
        for (int k = h.onset; k < a30; ++k)
        {
            const float v = absAt (k);
            if (v > h.peak) { h.peak = v; pkAt = k; }
            const float sv = a.L[(size_t) k];
            if ((sv >= 0) != (prevS >= 0)) ++zc; prevS = sv;
        }
        h.bright = (float) zc / std::max (1, a30 - h.onset) * (float) a.rate * 0.5f;
        // decay to -30 dB (extrapolated from the slope when the next hit cuts it off first)
        const int blk = std::max (8, (int) (0.005 * a.rate));
        float envEnd = h.peak; int k = pkAt, kMin = pkAt; bool reached = false;
        for (; k + blk <= h.end; k += blk)
        {
            float mx = 0; for (int j = k; j < k + blk; ++j) mx = std::max (mx, absAt (j));
            if (mx < envEnd) { envEnd = mx; kMin = k; }
            if (mx < h.peak * 0.0316f) { reached = true; break; }
        }
        if (! reached) k = kMin;
        const double ms = (k - pkAt) * 1000.0 / a.rate;
        if (reached || h.peak <= 0) h.decayMs = (float) ms;
        else
        {
            const double dropDb = 20.0 * std::log10 (std::max (1e-6f, envEnd) / h.peak);   // negative
            h.decayMs = dropDb < -1.0 ? (float) (ms * 30.0 / -dropDb) : 2000.0f;
        }
        if (h.peak > 1e-4f) lp.hits.push_back (h);
    }
    if (lp.hits.empty()) return;
    float top = 0; for (auto& h : lp.hits) top = std::max (top, h.peak);
    for (auto& h : lp.hits)
    {
        if (isMetal (lane)) h.kind = h.decayMs > 650.0f ? kCrash : h.decayMs > 140.0f ? kOpen : kClosed;
        else h.kind = h.peak < top * 0.4f ? 1 : 0;              // more than 8 dB under the loudest: ghost note
    }
    // main hit: the loudest normal one (hats: the loudest closed hat when there is one)
    for (size_t i = 0; i < lp.hits.size(); ++i)
    {
        const auto& h = lp.hits[i];
        const bool want = lane == kHats ? h.kind == kClosed : lane == kCym ? true : h.kind == 0;
        if (want && (lp.main < 0 || h.peak > lp.hits[(size_t) lp.main].peak)) lp.main = (int) i;
    }
    if (lp.main < 0) { lp.main = 0; for (size_t i = 1; i < lp.hits.size(); ++i) if (lp.hits[i].peak > lp.hits[(size_t) lp.main].peak) lp.main = (int) i; }
}

// ------------------------------------------------------------------------------------------------ 2. tempo
// BPM from a file name ("Snare Loop 140 BPM.wav", "hats_92bpm", "kick 174"); 0 when it says nothing.
inline double tempoFromName (const std::string& name)
{
    std::string s; for (char c : name) s += (char) std::tolower ((unsigned char) c);
    double best = 0; bool bestBpm = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (! std::isdigit ((unsigned char) s[i]) || (i > 0 && (std::isdigit ((unsigned char) s[i - 1]) || s[i - 1] == '.'))) continue;
        size_t j = i; while (j < s.size() && (std::isdigit ((unsigned char) s[j]) || s[j] == '.')) ++j;
        const double v = std::atof (s.substr (i, j - i).c_str());
        size_t k = j; while (k < s.size() && s[k] == ' ') ++k;
        const bool bpm = s.compare (k, 3, "bpm") == 0;
        const bool alone = (i == 0 || ! std::isalpha ((unsigned char) s[i - 1])) && (j >= s.size() || ! std::isalpha ((unsigned char) s[j]) || bpm);
        if (v >= 60.0 && v <= 200.0 && (bpm || alone) && (bpm || ! bestBpm)) { best = v; bestBpm = bpm; }
    }
    return best;
}

struct TempoResult { double bpm = 0; float confidence = 0; bool fromName = false; };

// Scans 60..200 BPM: how well the onsets sit on a 16th (or triplet) grid that starts at the first hit, how close the loop length is
// to a whole number of bars, plus a soft preference for 80..160 BPM. All loaded loops vote together.
inline TempoResult estimateTempo (const Kit& kit)
{
    TempoResult r;
    double nameBpm = 0;
    for (auto& l : kit) if (! l.audio.L.empty()) { const double t = tempoFromName (l.name); if (t > 0) { nameBpm = t; break; } }
    struct L { std::vector<double> t; std::vector<float> w, strong; double len; int lane; };
    std::vector<L> ls;
    for (auto& l : kit)
    {
        if (l.empty()) continue;
        L x; x.len = l.lengthSec; x.lane = (int) (&l - &kit[0]);
        float top = 0; for (auto& h : l.hits) top = std::max (top, h.peak);
        for (auto& h : l.hits)
        {
            x.t.push_back (h.onset / l.audio.rate); x.w.push_back (std::sqrt (std::max (1e-6f, h.peak)));
            x.strong.push_back (h.kind == 0 && h.peak >= 0.5f * top ? h.peak : 0.0f);   // accents: kicks / snares on the beat, hats on 8ths
        }
        ls.push_back (x);
    }
    if (ls.empty()) { r.bpm = nameBpm; r.fromName = nameBpm > 0; return r; }
    auto score = [&] (double bpm, float* fitOut)
    {
        double total = 0, fitSum = 0, wsum = 0;
        for (auto& x : ls)
        {
            const double t0 = x.t.empty() ? 0.0 : (x.t[0] < 60.0 / bpm / 2 ? x.t[0] : 0.0);
            const double beat = 60.0 / bpm, sig = 0.007;
            double f16 = 0, f12 = 0, w = 0, fb = 0, wb = 0;
            const double bstep = x.lane >= kHats ? beat / 2 : beat;
            for (size_t i = 0; i < x.t.size(); ++i)
            {
                const double p = x.t[i] - t0;
                for (int g : { 4, 3 })
                {
                    const double step = beat / g, d = p - std::round (p / step) * step;
                    (g == 4 ? f16 : f12) += x.w[i] * std::exp (-0.5 * (d / sig) * (d / sig));
                }
                w += x.w[i];
                if (x.strong[i] > 0) { const double d = p - std::round (p / bstep) * bstep; fb += x.strong[i] * std::exp (-0.5 * (d / 0.010) * (d / 0.010)); wb += x.strong[i]; }
            }
            const double fit = std::max (f16, 0.85 * f12) / std::max (1e-9, w);
            const double beatFit = wb > 0 ? fb / wb : 1.0;
            // loop length: whole bars (2^k beats) score best, other whole beat counts a bit
            const double beats = (x.len - t0) * bpm / 60.0, nb = std::round (beats);
            double lenScore = 0.0;
            if (nb >= 2)
            {
                const double err = std::fabs (beats - nb) * beat;   // seconds off
                const int ib = (int) nb; const bool pow2 = (ib & (ib - 1)) == 0, bar = ib % 4 == 0;
                lenScore = std::exp (-0.5 * (err / 0.012) * (err / 0.012)) * (pow2 ? 1.0 : bar ? 0.6 : 0.3);
            }
            total += x.w.size() * (fit * fit * (0.45 + 0.55 * lenScore) * (0.3 + 0.7 * beatFit));
            fitSum += fit * x.w.size(); wsum += x.w.size();
        }
        if (fitOut) *fitOut = (float) (fitSum / std::max (1.0, wsum));
        const double lg = std::log2 (bpm / 115.0);
        double prior = std::exp (-0.5 * (lg / 0.55) * (lg / 0.55));
        if (nameBpm > 0 && std::fabs (bpm - nameBpm) < 0.6) prior *= 1.6;
        return total * prior;
    };
    double best = 120, bestS = -1;
    for (double b = 60.0; b <= 200.0; b += 0.1) { const double s = score (b, nullptr); if (s > bestS) { bestS = s; best = b; } }
    for (double b = best - 0.1; b <= best + 0.1; b += 0.005) { const double s = score (b, nullptr); if (s > bestS) { bestS = s; best = b; } }
    float fit = 0; score (best, &fit);
    r.bpm = std::round (best * 100.0) / 100.0;
    // snap to the name / a round tempo when it is that close
    if (nameBpm > 0 && std::fabs (r.bpm - nameBpm) < 0.6) { r.bpm = nameBpm; r.fromName = true; }
    else if (std::fabs (r.bpm - std::round (r.bpm)) < 0.06) r.bpm = std::round (r.bpm);
    r.confidence = fit;
    return r;
}

// ------------------------------------------------------------------------------------------------ 3. fill generator
struct FillSettings
{
    int lengthBeats = 4;       // 1, 2, 4 (one bar), 8 (two bars)
    int style = kAuto;
    float density = 0.55f, complexity = 0.4f, swing = 0.0f, humanize = 0.1f, ramp = 0.6f;
    float flam = 0.2f, pitch = 0.0f /* semitones over the fill */, kick = 0.5f, hats = 0.35f, perc = 0.4f, level = 1.0f /* linear */;
    int ending = kEndCrash;
    uint32_t seed = 1;
};

struct Event
{
    int lane = kSnare, hit = -1;          // hit index in the lane's loop (resolved by generate)
    double beat = 0;                      // time in beats from the fill start (after swing; humanize lives in offsetMs)
    double gridBeat = 0;                  // straight grid time (before swing / humanize / flam offset) - for the timing tests
    float offsetMs = 0;                   // humanize + flam grace offset
    float vel = 0.8f, semis = 0;
    bool ring = false;                    // not choked by the next hit (crash)
    bool grace = false;
};

struct Fill { std::vector<Event> events; int style = kAuto; int lengthBeats = 4; };

inline int resolveStyle (const FillSettings& s)
{
    if (s.style > kAuto && s.style < kNumStyles) return s.style;
    detail::Rng r (s.seed * 7919u + 13u);
    // weights per length: { roll, build, toms, triplets, flams, break, stutter }
    static const float w1[] = { 30, 0, 5, 15, 20, 20, 10 }, w2[] = { 20, 10, 20, 15, 12, 18, 5 }, w4[] = { 15, 20, 25, 10, 5, 15, 10 };
    const float* w = s.lengthBeats <= 1 ? w1 : s.lengthBeats <= 2 ? w2 : w4;
    float tot = 0; for (int i = 0; i < 7; ++i) tot += w[i];
    float x = r.uni() * tot;
    for (int i = 0; i < 7; ++i) { if (x < w[i]) return i + 1; x -= w[i]; }
    return kRoll;
}

inline Fill generate (const Kit& kit, const FillSettings& s)
{
    Fill F;
    const int Lb = std::max (1, s.lengthBeats);
    const double L = Lb;
    F.lengthBeats = Lb;
    F.style = resolveStyle (s);
    detail::Rng rng (s.seed * 2654435761u + (uint32_t) F.style * 97u + (uint32_t) Lb);
    const float d = std::clamp (s.density, 0.0f, 1.0f), cx = std::clamp (s.complexity, 0.0f, 1.0f);
    std::vector<Event> ev;
    auto ramp = [&] (double b) { return (float) ((1.0 - s.ramp * 0.7) + s.ramp * 0.7 * (b / L)); };
    auto add = [&] (int lane, double b, float vel, float semis = 0.0f) { Event e; e.lane = lane; e.beat = e.gridBeat = b; e.vel = vel; e.semis = semis; ev.push_back (e); return &ev.back(); };
    auto glide = [&] (double b) { return (float) (s.pitch * (b / L)); };
    auto accentOf = [&] (double b, int perBeat, int k) { (void) b; return (k % perBeat) == 0 ? 1.0f : 0.82f; };
    auto steps = [&] (double from, double to, int perBeat, auto&& fn)
    {
        const int a = (int) std::ceil (from * perBeat - 1e-6), z = (int) std::ceil (to * perBeat - 1e-6);
        for (int k = a; k < z; ++k) fn ((double) k / perBeat, k);
    };
    // complexity: swap some hits for kicks / ghosts / rests (never the last two steps of the fill)
    auto vary = [&] (int lane, double b, float v, float semis, bool protect)
    {
        if (! protect && rng.chance (cx * 0.18f)) return;                       // rest
        if (! protect && lane == kSnare && rng.chance (cx * 0.28f)) { add (kKick, b, v * 0.95f); return; }
        if (! protect && lane == kSnare && rng.chance (cx * 0.22f)) { add (kSnare, b, v * 0.5f, semis); return; }   // ghost
        add (lane, b, v, semis);
    };

    switch (F.style)
    {
        case kRoll: default:
        {
            const int per = d < 0.3f ? 2 : 4;
            const double fast = d >= 0.72f ? std::min (1.0, L / 2) : 0.0;     // last part in 32nds
            steps (0, L - fast, per, [&] (double b, int k) { vary (kSnare, b, ramp (b) * accentOf (b, per, k), glide (b), b >= L - 2.0 / per); });
            if (fast > 0) steps (L - fast, L, 8, [&] (double b, int) { add (kSnare, b, ramp (b) * 0.9f, glide (b)); });
            break;
        }
        case kBuild:
        {
            const int rates[] = { d < 0.3f ? 1 : 2, d < 0.3f ? 2 : 4, d < 0.3f ? 4 : 8 };
            const double seg = L / 3.0, e1 = std::round (seg * 4.0) / 4.0, e2 = std::round (2.0 * seg * 4.0) / 4.0;
            const double edges[] = { 0.0, e1, e2, L };
            for (int i = 0; i < 3; ++i)
                steps (edges[i], edges[i + 1], rates[i], [&] (double b, int k)
                { vary (kSnare, b, (float) (0.35 + 0.65 * (b / L) * s.ramp + (1.0 - s.ramp) * 0.5) * accentOf (b, rates[i], k), glide (b), b >= L - 0.5); });
            break;
        }
        case kToms:
        {
            const int per = d < 0.4f ? 2 : d < 0.8f ? 4 : 6;
            const int nt = Lb == 1 ? 3 : 4;
            const float span = std::max (7.0f, std::fabs (s.pitch)) * (s.pitch > 0 ? -1.0f : 1.0f);   // + pitch = low to high run
            for (int t = 0; t < nt; ++t)
            {
                const float semis = span * (0.5f - (float) t / (float) (nt - 1));
                const double a = std::round (L * t / nt * per) / per, z = std::round (L * (t + 1) / nt * per) / per;
                steps (a, z, per, [&] (double b, int k)
                {
                    const bool first = std::fabs (b - a) < 1e-9;
                    if (first && rng.chance (cx * 0.4f)) add (kSnare, b, ramp (b), 0.0f);   // unpitched accent opens the group
                    else vary (kSnare, b, ramp (b) * (first ? 1.0f : 0.85f) * accentOf (b, per, k), semis, b >= L - 2.0 / per);
                });
            }
            break;
        }
        case kTriplets:
        {
            const int per = d < 0.5f ? 3 : 6;
            const bool shifted = rng.chance (cx);
            steps (0, L, per, [&] (double b, int k)
            {
                const int pos = k % 3;
                const bool kick = shifted ? pos == 1 : pos == 2;
                const float st = (float) (s.pitch * std::floor (b) / std::max (1.0, L - 1.0));
                if (kick && b < L - 1.0 / per) add (kKick, b, ramp (b) * 0.95f);
                else vary (kSnare, b, ramp (b) * (pos == 0 ? 1.0f : 0.8f), st, b >= L - 2.0 / per);
            });
            break;
        }
        case kFlams:
        {
            const int per = d < 0.5f ? 2 : 4;
            steps (0, L, per, [&] (double b, int k)
            {
                const bool acc = k % per == 0 || b >= L - 1.0;
                vary (kSnare, b, ramp (b) * (acc ? 1.0f : 0.75f), glide (b), b >= L - 2.0 / per);
                if (! ev.empty() && ev.back().lane == kSnare && std::fabs (ev.back().gridBeat - b) < 1e-9 && rng.chance (acc ? std::max (0.65f, s.flam) : s.flam * 0.5f))
                {
                    Event g = ev.back(); g.grace = true; g.vel *= 0.42f; g.offsetMs = -(18.0f + 10.0f * rng.uni()); ev.push_back (g);
                }
            });
            break;
        }
        case kBreak:
        {
            static const char* pats[] = { "k.s..sk.s.ks.sss", "s.ks.s.ks.ssksss", "k.s.ks.sk.s.ssss", "ks.sk.s.k.sskss.", "s..ss.s.k.s.sksk" };
            const char* pat = pats[rng.pick (5)];
            steps (0, L, 4, [&] (double b, int k)
            {
                const int ci = (int) (k % 16);
                char c = pat[ci];
                if (b >= L - 1.0 && d > 0.45f) c = 's';                            // last beat: roll into the next bar
                if (c == '.' && rng.chance (d * 0.6f)) c = rng.chance (0.6f) ? 'g' : 'k';
                if (c != '.' && rng.chance (cx * 0.25f) && b < L - 1.0) c = c == 's' ? 'k' : 's';
                const float v = ramp (b) * (k % 4 == 0 ? 1.0f : 0.85f);
                if (c == 'k') add (kKick, b, v);
                else if (c == 's') add (kSnare, b, v, glide (b));
                else if (c == 'g') add (kSnare, b, v * 0.45f, glide (b));
                if (c == 's' && rng.chance (cx * 0.3f) && b < L - 0.25) add (kSnare, b + 0.125, v * 0.55f, glide (b));   // 32nd double
            });
            break;
        }
        case kStutter:
        {
            // repeats of the main snare: 8ths, then 16ths, 32nds and 64ths over the last part
            double b = 0; int rate = d < 0.35f ? 1 : 2; double segEnd = L / 2;
            while (b < L - 1e-9)
            {
                steps (b, segEnd, rate, [&] (double t, int) { add (kSnare, t, ramp (t) * 0.92f, glide (t)); });
                b = segEnd; segEnd = b + (L - b) / 2; rate *= 2;
                if (rate >= 16 || L - b <= 1.0 / rate * 2 + 1e-9) { steps (b, L, rate, [&] (double t, int) { add (kSnare, t, ramp (t) * 0.92f, glide (t)); }); break; }
            }
            break;
        }
    }

    // kick under the fill
    if (s.kick > 0.01f)
    {
        auto near = [&] (int lane, double b, double tol) { for (auto& e : ev) if (e.lane == lane && std::fabs (e.gridBeat - b) < tol) return true; return false; };
        for (int b = 0; b < Lb; ++b)
        {
            if ((b == 0 || rng.chance (s.kick)) && ! near (kKick, b, 0.13)) add (kKick, b, b == 0 ? 1.0f : 0.9f);
            if (s.kick > 0.65f && rng.chance ((s.kick - 0.65f) * 2.2f) && ! near (kKick, b + 0.75, 0.13)) add (kKick, b + 0.75, 0.8f);
        }
    }
    // hats keep time at the start of the fill and drop out under the roll
    if (s.hats > 0.01f)
    {
        const int per = s.hats > 0.65f ? 4 : 2;
        const double until = std::max (0.5, L * (0.35 + 0.5 * (1.0 - d)));
        steps (0, std::min (L, until), per, [&] (double b, int k) { if (k == 0 || rng.chance (0.5f + 0.5f * s.hats)) add (kHats, b, (k % per == 0 ? 0.8f : 0.6f) * (float) (1.0 - 0.4 * b / L)); });
    }
    // percussion (optional lane): off-beat 8ths / 16ths through the first part of the fill, a hit on the last off-beat
    if (s.perc > 0.01f && ! kit[(size_t) kPerc].empty())
    {
        const int per = s.perc > 0.7f ? 4 : 2;
        const double until = std::max (0.5, L * (0.4 + 0.5 * (1.0 - d)));
        steps (0, std::min (L, until), per, [&] (double b, int k) { if (k % per != 0 && rng.chance (0.35f + 0.6f * s.perc)) add (kPerc, b, 0.7f * (float) (1.0 - 0.3 * b / L)); });
        if (L >= 1.0 && rng.chance (s.perc)) add (kPerc, L - 0.5, 0.8f);
    }
    // ending: the crash comes from the cymbal lane when one is loaded, else from the hats loop (as in 1.0)
    const int crashLane = kit[(size_t) kCym].empty() ? kHats : kCym;
    if (s.ending == kEndCrash) { auto* e = add (crashLane, L, 1.0f); e->ring = true; e->hit = -2; if (s.kick > 0.01f) add (kKick, L, 1.0f); }
    else if (s.ending == kEndOpenHat) { auto* e = add (kHats, L - 0.5, 0.95f); e->hit = -3; }

    // resolve hits: main / alternates / ghosts / closed / open / crash
    for (auto& e : ev)
    {
        const auto& lp = kit[(size_t) e.lane];
        if (lp.empty()) { e.hit = -1; continue; }
        std::vector<int> pool;
        const float mainPk = lp.hits[(size_t) lp.main].peak;
        auto collect = [&] (auto&& pred) { pool.clear(); for (int i = 0; i < (int) lp.hits.size(); ++i) if (pred (lp.hits[(size_t) i])) pool.push_back (i); };
        if (isMetal (e.lane))
        {
            const int want = e.hit == -2 ? kCrash : e.hit == -3 ? kOpen : kClosed;
            collect ([&] (const Hit& h) { return h.kind == want; });
            if (pool.empty() && want == kCrash) collect ([&] (const Hit& h) { return h.kind == kOpen; });
            if (pool.empty() && want != kClosed)
            {   // no ringing hit in the loop: the longest one
                int li = 0; for (int i = 1; i < (int) lp.hits.size(); ++i) if (lp.hits[(size_t) i].decayMs > lp.hits[(size_t) li].decayMs) li = i;
                pool = { li };
            }
            if (pool.empty()) pool = { lp.main };
            if (want == kClosed && pool.size() > 1 && ! rng.chance (cx)) pool = { lp.main };
        }
        else
        {
            const bool ghost = e.vel < 0.55f && ! e.grace;
            if (ghost) collect ([&] (const Hit& h) { return h.kind == 1; });
            if (pool.empty()) collect ([&] (const Hit& h) { return h.kind == 0 && h.peak > mainPk * 0.63f; });   // within 4 dB of the main hit
            if (pool.empty() || ! rng.chance (0.25f + cx * 0.5f)) pool = { lp.main };
        }
        e.hit = pool[(size_t) rng.pick ((int) pool.size())];
    }
    ev.erase (std::remove_if (ev.begin(), ev.end(), [] (const Event& e) { return e.hit < 0; }), ev.end());

    // swing (straight grids only: positions on the 16th / 32nd grid) and humanize
    for (auto& e : ev)
    {
        const double p8 = e.gridBeat * 2.0, base = std::floor (p8 + 1e-9), f = p8 - base;   // position inside its 8th, 0..1
        const bool straight = std::fabs (e.gridBeat * 8.0 - std::round (e.gridBeat * 8.0)) < 1e-6;
        if (straight && s.swing > 0.001f && f > 1e-9)
        {
            const double w = 0.5 + 0.25 * s.swing;   // where the off 16th lands (0.5 straight .. 0.75 hard swing)
            const double g = f < 0.5 ? f / 0.5 * w : w + (f - 0.5) / 0.5 * (1.0 - w);
            e.beat = (base + g) / 2.0;
        }
        const bool anchor = e.gridBeat < 1e-9 || e.ring || e.gridBeat >= L - 1e-9;
        if (s.humanize > 0.001f && ! anchor && ! e.grace)
            e.offsetMs += std::clamp (rng.gauss() * s.humanize * 5.0f, -12.0f, 12.0f);
        if (s.humanize > 0.001f) e.vel *= 1.0f + rng.sym() * s.humanize * 0.15f;
        e.vel = std::clamp (e.vel, 0.05f, 1.0f);
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.beat + a.offsetMs / 60000.0 < b.beat + b.offsetMs / 60000.0; });
    F.events = std::move (ev);
    return F;
}

// ------------------------------------------------------------------------------------------------ 4. render
struct Render
{
    std::array<std::vector<float>, kLanes> L, R;   // one stereo buffer per part, all the same length
    double rate = 48000, bpm = 120;
    int fillLen = 0;          // samples up to the end of the fill (the next downbeat)
    int total = 0;            // fillLen + ringing tail
    float gain = 1.0f;        // safety gain that kept the full fill under -1 dBFS (1 = none)
    float peak = 0;           // full-fill peak after that
    float maxEdge = 0;        // largest |sample| at any slice's first / last rendered sample (click check; ~0)
    int events = 0;
    std::vector<double> eventTimes;   // seconds, per rendered event (same order as the fill's events)
};

inline double eventSeconds (const Event& e, double bpm) { return e.beat * 60.0 / bpm + e.offsetMs * 0.001; }

inline Render render (const Kit& kit, const Fill& fill, double bpm, double rate, const FillSettings& s)
{
    Render out; out.rate = rate; out.bpm = bpm = std::clamp (bpm, 20.0, 400.0);
    out.fillLen = (int) std::llround (fill.lengthBeats * 60.0 / bpm * rate);
    const int maxTotal = out.fillLen + (int) (4.0 * rate);
    struct Play { int lane, hit; double outStart; double rho; int len; float gain; const Event* e; };
    std::vector<Play> plays;
    for (auto& e : fill.events)
    {
        const auto& lp = kit[(size_t) e.lane];
        if (lp.empty() || e.hit < 0 || e.hit >= (int) lp.hits.size()) continue;
        const auto& h = lp.hits[(size_t) e.hit];
        Play p; p.lane = e.lane; p.hit = e.hit; p.e = &e;
        p.rho = lp.audio.rate / rate * std::pow (2.0, e.semis / 12.0);
        const double tOut = eventSeconds (e, bpm) * rate;
        p.outStart = tOut - (h.onset - h.start) / p.rho;               // transient lands exactly on tOut
        p.len = (int) std::floor ((h.end - h.start) / p.rho);
        const float norm = std::min (4.0f, lp.hits[(size_t) lp.main].peak / std::max (1e-5f, h.peak));
        p.gain = s.level * norm * std::pow (e.vel, 1.6f);
        plays.push_back (p);
        out.eventTimes.push_back (tOut / rate);
    }
    out.events = (int) plays.size();
    int total = out.fillLen;
    // choke: a hit stops where the next hit of the same part starts (crash rings over everything; a flam grace note is choked by its main hit)
    std::vector<int> stopAt (plays.size());
    for (size_t i = 0; i < plays.size(); ++i)
    {
        const int naturalEnd = (int) std::floor (plays[i].outStart) + plays[i].len;
        int stop = naturalEnd;
        if (! plays[i].e->ring)
            for (size_t j = i + 1; j < plays.size(); ++j)
                if (plays[j].lane == plays[i].lane && ! plays[j].e->ring)
                {
                    const auto& hj = kit[(size_t) plays[j].lane].hits[(size_t) plays[j].hit];
                    stop = std::min (stop, (int) std::floor (plays[j].outStart + (hj.onset - hj.start) / plays[j].rho));   // up to the next transient
                    break;
                }
        stopAt[i] = std::min (stop, maxTotal);
        total = std::max (total, stopAt[i]);
    }
    out.total = total;
    for (int l = 0; l < kLanes; ++l) { out.L[(size_t) l].assign ((size_t) total, 0.0f); out.R[(size_t) l].assign ((size_t) total, 0.0f); }
    const int fadeIn = std::max (8, (int) std::lround (0.0015 * rate)), fadeOutChoke = std::max (16, (int) std::lround (0.004 * rate)),
              fadeOutEnd = std::max (16, (int) std::lround (0.008 * rate));
    for (size_t i = 0; i < plays.size(); ++i)
    {
        const auto& p = plays[i];
        const auto& lp = kit[(size_t) p.lane];
        const auto& h = lp.hits[(size_t) p.hit];
        const int s0 = (int) std::ceil (p.outStart);
        const int a = std::max (0, s0), z = stopAt[i];
        if (z <= a) continue;
        const bool choked = z < (int) std::floor (p.outStart) + p.len;
        const int fo = std::min (choked ? fadeOutChoke : fadeOutEnd, std::max (4, (z - a) / 3));
        // a hit that starts before 0 (pre-roll of the first hit) gets a fresh short fade from sample 0
        const int fiStart = a, fi = s0 < 0 ? std::max (4, fadeIn / 3) : fadeIn;
        std::vector<float> chL, chR;
        const bool st = lp.audio.stereo();
        float* oL = out.L[(size_t) p.lane].data(); float* oR = out.R[(size_t) p.lane].data();
        for (int k = a; k < z; ++k)
        {
            const double src = h.start + (k - p.outStart) * p.rho;
            if (src >= h.end) break;
            const float env = detail::raisedCos ((float) (k - fiStart) / (float) fi) * detail::raisedCos ((float) (z - 1 - k) / (float) fo);
            const float g = p.gain * env;
            const float xl = detail::hermite (lp.audio.L, src), xr = st ? detail::hermite (lp.audio.R, src) : xl;
            oL[k] += g * xl; oR[k] += g * xr;
            if (k == a || k == z - 1) out.maxEdge = std::max (out.maxEdge, std::max (std::fabs (g * xl), std::fabs (g * xr)));
        }
    }
    // trim the tail after the last sound (keeps at least the fill length)
    int last = out.fillLen;
    for (int k = total - 1; k >= out.fillLen; --k)
    {
        float m = 0; for (int l = 0; l < kLanes; ++l) m = std::max ({ m, std::fabs (out.L[(size_t) l][(size_t) k]), std::fabs (out.R[(size_t) l][(size_t) k]) });
        if (m > 3e-5f) { last = k + 1; break; }
    }
    out.total = last;
    for (int l = 0; l < kLanes; ++l) { out.L[(size_t) l].resize ((size_t) last); out.R[(size_t) l].resize ((size_t) last); }
    float pk = 0;
    for (int k = 0; k < last; ++k)
    {
        float sl = 0, sr = 0; for (int l = 0; l < kLanes; ++l) { sl += out.L[(size_t) l][(size_t) k]; sr += out.R[(size_t) l][(size_t) k]; }
        pk = std::max ({ pk, std::fabs (sl), std::fabs (sr) });
    }
    const float ceiling = 0.891f;   // -1 dBFS
    if (pk > ceiling)
    {
        out.gain = ceiling / pk;
        for (int l = 0; l < kLanes; ++l) for (int k = 0; k < last; ++k) { out.L[(size_t) l][(size_t) k] *= out.gain; out.R[(size_t) l][(size_t) k] *= out.gain; }
        pk = ceiling; out.maxEdge *= out.gain;
    }
    out.peak = pk;
    return out;
}
} // namespace drf
