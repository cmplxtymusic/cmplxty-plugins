#pragma once
// CMPLXTY Silt - sample analysis (plain C++17, no JUCE, header only so the DSP tests build on their own).
//  - rootFromName(): the key in a file name ("Pad_C#3", "vox Fmin", "Keys-A") -> pitch class (+ octave if written)
//  - detectPitch(): YIN pitch detection over the loud part of the sample (median of the confident frames) -> fractional MIDI root
//  - pitch marks: the start of every cycle of a pitched sample (period tracking + correlation refinement), used by Cohere
//  - detectOnsets(): transients (log spectral flux + time-domain refinement), used by Magnet and the slicer
//  - RMS envelope + overview for the display, the grain level compensation and the Cross morph
// Note names: MIDI 60 = C3 (the CMPLXTY / Ableton naming; FL Studio calls the same key C5).
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace slt
{
struct Audio
{
    std::vector<float> L, R;   // R empty = mono
    double rate = 44100.0;
    int len() const noexcept { return (int) L.size(); }
    bool stereo() const noexcept { return ! R.empty(); }
    float at (int ch, int i) const noexcept { return (ch == 1 && stereo()) ? R[(size_t) i] : L[(size_t) i]; }
};

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
        // log spectral flux, plus the band under 300 Hz weighted up (a kick inside a snare's noise tail barely moves the
        // broadband flux, the noise already fills the spectrum; it does move the low band)
        float fl = 0, lowF = 0;
        const int lowBins = std::max (2, (int) (300.0 * N / a.rate));
        for (int k = 1; k < bins; ++k)
        {
            cur[(size_t) k] = std::log1p (100.0f * std::abs (buf[(size_t) k]));
            const float d = std::max (0.0f, cur[(size_t) k] - prev[(size_t) k]);
            fl += d; if (k <= lowBins) lowF += d;
        }
        fl += 2.0f * lowF * (float) bins / (float) lowBins / 16.0f;
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
    // refine envelope: max of |x| and 4 |x[n] - x[n-1]| (the low body of a kick and the edge of a hat both count)
    auto ev = [&] (int k) { return std::max (std::fabs (m[(size_t) k]), 4.0f * std::fabs (m[(size_t) k] - m[(size_t) std::max (0, k - 1)])); };
    for (size_t pi = 0; pi < peakFrames.size(); ++pi)
    {
        const int f = peakFrames[pi], c = std::max (0, f * hop - lead);
        const int lo = std::max (pi > 0 ? out.empty() ? 0 : out.back() + blk * 8 : 0, c - (int) (0.024 * a.rate));
        const int hi = std::min (n - 1, c + (int) (0.016 * a.rate));
        if (hi - lo < blk * 4) continue;
        std::vector<float> env;
        for (int i = lo; i < hi; i += blk) { float mx = 0; for (int k = i; k < std::min (hi, i + blk); ++k) mx = std::max (mx, ev (k)); env.push_back (mx); }
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
        for (int k = lo + on * blk; k < std::min (hi, lo + (on + 1) * blk); ++k) if (ev (k) >= th) { sOn = k; break; }
        if (! out.empty() && sOn - out.back() < (int) (0.02 * a.rate)) continue;
        // a real hit rises clearly over what was already sounding (rejects room echoes / flutter and noise)
        {
            // RMS over 13 ms before vs 12 ms after, on the signal or on its first difference (a hat over a kick's tail is
            // only a clear rise in the difference); Sens sets the rise needed: 2.2x (0) .. 1.6x (0.5, default) .. 1.0x (1)
            const double rat = 2.2 - 1.2 * s;
            double before = 0, after = 0, dB = 0, dA = 0; int nb = 0, na = 0;
            const int b0 = std::max (1, sOn - (int) (0.015 * a.rate)), b1 = std::max (1, sOn - (int) (0.002 * a.rate));
            for (int k = b0; k < b1; ++k) { const double x = m[(size_t) k], d = x - m[(size_t) k - 1]; before += x * x; dB += d * d; ++nb; }
            for (int k = std::max (1, sOn); k < std::min (n, sOn + (int) (0.012 * a.rate)); ++k) { const double x = m[(size_t) k], d = x - m[(size_t) k - 1]; after += x * x; dA += d * d; ++na; }
            before /= std::max (1, nb); after /= std::max (1, na); dB /= std::max (1, nb); dA /= std::max (1, na);
            if (! (after > before * rat * rat + 1e-10 || dA > dB * rat * rat + 1e-12)) continue;
        }
        out.push_back (sOn);
        if (strengthOut) strengthOut->push_back (flux[(size_t) f] / fmax);
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ note names
inline std::string noteName (int midi)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midi % 12) + 12) % 12, oct = (int) std::floor (midi / 12.0) - 2;   // 60 = C3
    return std::string (n[pc]) + std::to_string (oct);
}

// Key in a file name. Looks for a note letter (upper case, or lower case followed by #/b/octave) that stands on its own:
// not inside a word ("Dark", "Bass"), optional # / b / "sharp" / "flat", optional octave (-2..8, C3 = 60), optional m/min/maj/minor/major.
// The last match wins (keys are usually written at the end: "Pad_120_Cmin"). Returns the pitch class 0..11 or -1;
// *octaveMidi gets the MIDI note of the written octave, or -1000 when no octave is written.
inline int rootFromName (const std::string& nameIn, int* octaveMidi = nullptr)
{
    std::string s = nameIn;
    { const auto dot = s.find_last_of ('.'); if (dot != std::string::npos && s.size() - dot <= 5) s = s.substr (0, dot); }
    const int n = (int) s.size();
    static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int best = -1, bestOct = -1000;
    auto isAl = [] (char c) { return std::isalpha ((unsigned char) c) != 0; };
    auto lower = [] (char c) { return (char) std::tolower ((unsigned char) c); };
    for (int i = 0; i < n; ++i)
    {
        const char c = s[(size_t) i];
        const char u = (char) std::toupper ((unsigned char) c);
        if (u < 'A' || u > 'G') continue;
        if (i > 0 && isAl (s[(size_t) (i - 1)])) continue;
        int j = i + 1, acc = 0; bool marked = false;
        auto rest = [&] (const char* w) { const int L = (int) std::char_traits<char>::length (w); if (j + L > n) return false; for (int k = 0; k < L; ++k) if (lower (s[(size_t) (j + k)]) != w[k]) return false; return true; };
        if (j < n && s[(size_t) j] == '#') { acc = 1; ++j; marked = true; }
        else if (rest ("sharp")) { acc = 1; j += 5; marked = true; }
        else if (rest ("flat")) { acc = -1; j += 4; marked = true; }
        else if (j < n && s[(size_t) j] == 'b' && (j + 1 >= n || ! isAl (s[(size_t) (j + 1)]) || lower (s[(size_t) (j + 1)]) == 'm')) { acc = -1; ++j; marked = true; }
        int oct = -1000;
        if (j < n && std::isdigit ((unsigned char) s[(size_t) j]))
        {
            // a single digit octave right after the note; more digits = a number (tempo, take), not an octave
            if (j + 1 < n && std::isdigit ((unsigned char) s[(size_t) (j + 1)])) continue;
            oct = s[(size_t) j] - '0'; ++j; marked = true;
        }
        if (rest ("minor")) j += 5; else if (rest ("major")) j += 5; else if (rest ("min")) j += 3; else if (rest ("maj")) j += 3; else if (j < n && (s[(size_t) j] == 'm' || s[(size_t) j] == 'M')) { ++j; marked = true; }
        if (j < n && isAl (s[(size_t) j])) continue;           // part of a word
        if (c != u && ! marked) continue;                       // a lone lower case letter is a word ("a", "e"), not a key
        best = (pcs[u - 'A'] + acc + 12) % 12;
        bestOct = oct == -1000 ? -1000 : (oct + 2) * 12 + best;
    }
    if (octaveMidi) *octaveMidi = bestOct;
    return best;
}

inline std::vector<float> monoOf (const Audio& a)
{
    std::vector<float> m ((size_t) a.len());
    for (int i = 0; i < a.len(); ++i) m[(size_t) i] = a.stereo() ? 0.5f * (a.L[(size_t) i] + a.R[(size_t) i]) : a.L[(size_t) i];
    return m;
}

struct PitchResult { float hz = 0, midi = -1, conf = 0; int frames = 0, used = 0; };

// YIN (de Cheveigne & Kawahara 2002): cumulative mean normalised difference, absolute threshold, parabolic interpolation.
// Runs on up to 32 frames spread over the loud part of the sample; the result is the median of the confident frames,
// refined by averaging the frames within a third of a semitone of it. 35 Hz .. 1500 Hz.
inline PitchResult detectPitch (const std::vector<float>& m, double rate)
{
    PitchResult res;
    const int n = (int) m.size();
    const int tauMin = std::max (2, (int) (rate / 1500.0)), tauMax = (int) (rate / 35.0);
    const int W = std::max (1024, (int) (rate * 0.045));
    if (n < W + tauMax + 2) return res;
    // loudness per candidate frame
    const int hop = std::max (256, (n - W - tauMax) / 200);
    std::vector<std::pair<float, int>> cand;
    float mx = 0;
    for (int s = 0; s + W + tauMax < n; s += hop)
    {
        double e = 0; for (int i = 0; i < W; i += 4) e += (double) m[(size_t) (s + i)] * m[(size_t) (s + i)];
        const float r = (float) std::sqrt (e / (W / 4)); cand.push_back ({ r, s }); mx = std::max (mx, r);
    }
    if (mx < 1e-4f) return res;
    std::vector<int> starts;
    for (auto& c : cand) if (c.first > 0.25f * mx) starts.push_back (c.second);
    if (starts.size() > 32) { std::vector<int> t; for (int k = 0; k < 32; ++k) t.push_back (starts[(size_t) ((size_t) k * starts.size() / 32)]); starts.swap (t); }
    std::vector<float> d ((size_t) tauMax + 2), dn ((size_t) tauMax + 2);
    std::vector<float> mids;
    for (int s : starts)
    {
        ++res.frames;
        const float* x = m.data() + s;
        d[0] = 0;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            double acc = 0;
            for (int i = 0; i < W; ++i) { const float df = x[i] - x[i + tau]; acc += (double) df * df; }
            d[(size_t) tau] = (float) acc;
        }
        double run = 0; dn[0] = 1;
        for (int tau = 1; tau <= tauMax + 1; ++tau) { run += d[(size_t) tau]; dn[(size_t) tau] = run > 0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f; }
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (dn[(size_t) tau] < 0.12f)
            {
                while (tau + 1 <= tauMax && dn[(size_t) (tau + 1)] < dn[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0) continue;
        // parabolic interpolation on the raw difference function (unbiased)
        const float y0 = d[(size_t) (best - 1)], y1 = d[(size_t) best], y2 = d[(size_t) (best + 1)];
        const float den = y0 - 2 * y1 + y2;
        const float off = std::fabs (den) > 1e-12f ? std::clamp (0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0.0f;
        const double T = best + off;
        if (1.0f - dn[(size_t) best] < 0.85f) continue;
        mids.push_back ((float) (69.0 + 12.0 * std::log2 (rate / T / 440.0)));
    }
    res.used = (int) mids.size();
    if (mids.size() < 3 || mids.size() * 3 < (size_t) res.frames) return res;   // fewer than a third of the loud frames are pitched: unpitched
    auto srt = mids; std::nth_element (srt.begin(), srt.begin() + (long) srt.size() / 2, srt.end());
    const float med = srt[srt.size() / 2];
    double sum = 0; int cnt = 0;
    for (float v : mids) if (std::fabs (v - med) < 0.33f) { sum += v; ++cnt; }
    if (cnt < 3) return res;
    res.midi = (float) (sum / cnt);
    res.hz = (float) (440.0 * std::pow (2.0, (res.midi - 69.0) / 12.0));
    res.conf = (float) cnt / (float) res.frames;
    return res;
}

// Cycle starts of a pitched sample: one mark per period. Starts on the biggest peak of the first loud part, then predicts the
// next mark one period on and refines it by correlating the last cycle with the candidates (+-15 % of a period),
// so the period follows slow vibrato / drift. Silent stretches keep the last period.
inline std::vector<int> pitchMarks (const std::vector<float>& m, double rate, float hz)
{
    std::vector<int> marks;
    const int n = (int) m.size();
    if (hz <= 0 || n < 16) return marks;
    const double P0 = rate / hz;
    if (P0 < 2 || P0 * 4 > n) return marks;
    // first loud sample
    float mx = 0; for (float v : m) mx = std::max (mx, std::fabs (v));
    int first = 0; while (first < n && std::fabs (m[(size_t) first]) < 0.1f * mx) ++first;
    int m0 = first; float best = -1e9f;
    for (int i = first; i < std::min (n, first + (int) (P0 * 2)); ++i) if (m[(size_t) i] > best) { best = m[(size_t) i]; m0 = i; }
    // walk back to the first cycle of the sample
    while (m0 - P0 >= 0) m0 -= (int) std::lround (P0);
    double P = P0;
    marks.push_back (m0);
    const int stride = std::max (1, (int) (P0 / 64));
    double cur = m0;
    while (true)
    {
        const double pred = cur + P;
        const int lenC = (int) P;
        if (pred + lenC * 1.2 >= n) break;
        const int c0 = (int) cur;
        int bestD = 0; double bestC = -1e30; double e0 = 0;
        for (int i = 0; i < lenC; i += stride) e0 += (double) m[(size_t) (c0 + i)] * m[(size_t) (c0 + i)];
        const int range = std::max (1, (int) (P * 0.15));
        if (c0 >= 0 && e0 > 1e-9 * lenC)
        {
            for (int dlt = -range; dlt <= range; ++dlt)
            {
                const int b = (int) std::lround (pred) + dlt;
                if (b < 0 || b + lenC >= n) continue;
                double acc = 0, e1 = 0;
                for (int i = 0; i < lenC; i += stride) { const float y = m[(size_t) (b + i)]; acc += (double) m[(size_t) (c0 + i)] * y; e1 += (double) y * y; }
                const double c = acc / std::sqrt (e0 * e1 + 1e-20);
                if (c > bestC) { bestC = c; bestD = dlt; }
            }
        }
        double nx = bestC > 0.6 ? std::lround (pred) + bestD : pred;
        if (bestC > 0.6) P = std::clamp (0.7 * P + 0.3 * (nx - cur), P0 / 1.12, P0 * 1.12);
        cur = nx;
        marks.push_back ((int) std::lround (cur));
    }
    return marks;
}

// A loaded, analysed sample. Immutable once published to the audio thread.
struct Source
{
    std::shared_ptr<const Audio> audio;
    std::string name;
    int namePc = -1, nameOct = -1000;       // key from the file name
    PitchResult pitch;                      // YIN result
    float root = 60;                        // fractional MIDI root used by Tune = Auto ("C plays C")
    bool pitched = false;
    std::string rootHow;                    // "name", "name+detected octave", "detected", "default"
    std::vector<int> marks, onsets;         // source samples
    std::vector<float> onsetStr;
    std::vector<float> rms; int rmsHop = 256; float rmsRef = 0.1f;   // RMS per hop (512 window) and the mean level of the loud part
    std::array<float, 1024> overL {}, overR {};   // peak overview for the display
    float peak = 0;
    float seconds() const { return audio ? (float) (audio->len() / audio->rate) : 0.0f; }
    int len() const { return audio ? audio->len() : 0; }
    float rmsAt (double pos) const
    {
        if (rms.empty()) return rmsRef;
        const double f = pos / rmsHop; const int i = std::clamp ((int) f, 0, (int) rms.size() - 1), j = std::min (i + 1, (int) rms.size() - 1);
        const float t = (float) std::clamp (f - i, 0.0, 1.0);
        return rms[(size_t) i] * (1 - t) + rms[(size_t) j] * t;
    }
    int nearestOnset (double pos) const
    {
        if (onsets.empty()) return -1;
        auto it = std::lower_bound (onsets.begin(), onsets.end(), (int) pos);
        if (it == onsets.end()) return onsets.back();
        if (it == onsets.begin()) return *it;
        const int a = *(it - 1), b = *it; return (pos - a) <= (b - pos) ? a : b;
    }
    int nearestMark (double pos) const
    {
        if (marks.empty()) return -1;
        auto it = std::lower_bound (marks.begin(), marks.end(), (int) pos);
        if (it == marks.end()) return marks.back();
        if (it == marks.begin()) return *it;
        const int a = *(it - 1), b = *it; return (pos - a) <= (b - pos) ? a : b;
    }
};

inline void analyseOnsets (Source& s, float sens)
{
    s.onsetStr.clear();
    s.onsets = s.audio ? detectOnsets (*s.audio, sens, &s.onsetStr) : std::vector<int>();
}

// Full analysis. Root rules: the file name gives the pitch class (most reliable); the octave comes from the name if written,
// otherwise from pitch detection (the octave nearest to the detected pitch), otherwise C3 = 60.
// With no key in the name the detected pitch is used as is (fractional, so a slightly sharp sample still plays in tune).
inline void analyse (Source& s, float onsetSens)
{
    const Audio& a = *s.audio;
    const auto m = monoOf (a);
    s.namePc = rootFromName (s.name, &s.nameOct);
    s.pitch = detectPitch (m, a.rate);
    s.pitched = s.pitch.midi > 0;
    if (s.namePc >= 0)
    {
        if (s.nameOct > -1000) { s.root = (float) s.nameOct; s.rootHow = "name"; }
        else if (s.pitched)
        {
            // nearest note of that pitch class to the detected pitch; keep the detected cents if it agrees with the name
            const float det = s.pitch.midi;
            float bestN = 60; float bd = 1e9f;
            for (int o = -1; o <= 10; ++o) { const int c = o * 12 + s.namePc; if (std::fabs ((float) c - det) < bd) { bd = std::fabs ((float) c - det); bestN = (float) c; } }
            s.root = bd < 0.5f ? det : bestN;
            s.rootHow = bd < 0.5f ? "name + detected" : "name (octave detected)";
        }
        else { s.root = (float) (60 + s.namePc); s.rootHow = "name"; }
    }
    else if (s.pitched) { s.root = s.pitch.midi; s.rootHow = "detected"; }
    else { s.root = 60; s.rootHow = "unpitched (C3)"; }
    const float markHz = s.pitched ? s.pitch.hz : (float) (440.0 * std::pow (2.0, (s.root - 69.0) / 12.0));
    s.marks = s.pitched ? pitchMarks (m, a.rate, markHz) : std::vector<int>();
    analyseOnsets (s, onsetSens);
    // RMS envelope
    const int n = a.len();
    s.rms.clear(); s.rmsHop = 256;
    for (int c = 0; c < n; c += s.rmsHop)
    {
        double e = 0; int k = 0;
        for (int i = std::max (0, c - 256); i < std::min (n, c + 256); ++i) { e += (double) m[(size_t) i] * m[(size_t) i]; ++k; }
        s.rms.push_back ((float) std::sqrt (e / std::max (1, k)));
    }
    float rmx = 0; for (float v : s.rms) rmx = std::max (rmx, v);
    double rs = 0; int rc = 0; for (float v : s.rms) if (v > 0.1f * rmx) { rs += v; ++rc; }
    s.rmsRef = rc > 0 ? (float) (rs / rc) : 0.1f;
    s.peak = 0;
    for (int k = 0; k < 1024; ++k)
    {
        const int i0 = (int) ((int64_t) k * n / 1024), i1 = std::max (i0 + 1, (int) ((int64_t) (k + 1) * n / 1024));
        float pl = 0, pr = 0;
        for (int i = i0; i < std::min (n, i1); ++i) { pl = std::max (pl, std::fabs (a.L[(size_t) i])); pr = std::max (pr, std::fabs (a.at (1, i))); }
        s.overL[(size_t) k] = pl; s.overR[(size_t) k] = pr; s.peak = std::max (s.peak, std::max (pl, pr));
    }
}

// ------------------------------------------------------------------------------------------------ slices
enum SliceSrc { kSlTransients = 0, kSlGrid4, kSlGrid8, kSlGrid16, kSlGrid32, kSlManual, kNumSliceSrc };
constexpr int kMaxSlices = 64;

inline std::vector<int> autoSlices (const Source& s, int src)
{
    std::vector<int> out;
    const int n = s.len();
    if (n <= 0) return out;
    if (src == kSlTransients)
    {
        const int gap = (int) (0.03 * s.audio->rate);
        if (s.onsets.empty() || s.onsets.front() > gap) out.push_back (0);
        for (int o : s.onsets)
        {
            const int st = std::max (0, o - (int) (0.002 * s.audio->rate));   // 2 ms before the transient
            if (out.empty() || st - out.back() > gap) out.push_back (st);
            if ((int) out.size() >= kMaxSlices) break;
        }
        if (out.empty()) out.push_back (0);
    }
    else
    {
        const int k = src == kSlGrid4 ? 4 : src == kSlGrid8 ? 8 : src == kSlGrid16 ? 16 : 32;
        for (int i = 0; i < k; ++i) out.push_back ((int) ((int64_t) i * n / k));
    }
    return out;
}
} // namespace slt
