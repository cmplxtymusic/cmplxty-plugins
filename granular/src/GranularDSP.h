#pragma once
// CMPLXTY Granular DSP (plain C++, no JUCE): the input is recorded into a 4-second circular buffer and played back as a
// cloud of short grains read from behind the write head. Each grain gets its own position (spray), length (chaos), pitch
// (pitch + spread, optionally quantised to semitones / fifths / octaves), direction (reverse chance), pan and window.
// Freeze stops recording so the cloud keeps playing the frozen moment. Feedback writes the cloud back into the buffer.
// Zero latency (grains always read material that is already recorded). Real-time safe after prepare().
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gr::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxGrains = 64;
constexpr int kPeakBins = 256;          // waveform overview of the whole buffer for the display
constexpr double kBufSeconds = 4.0;

enum class Shape { smooth = 0, triangle, perc, glitch };
enum class SpreadMode { free = 0, semis, fifths, octaves };

struct Params
{
    float sizeMs = 120.0f, density = 16.0f, positionMs = 150.0f, sprayMs = 80.0f;
    float pitch = 0.0f, spread = 0.0f; SpreadMode spreadMode = SpreadMode::free;
    float reverse = 0.0f, chaos = 0.2f; Shape shape = Shape::smooth;
    float width = 0.7f, feedback = 0.0f, toneHz = 16000.0f;
    bool freeze = false; float mix = 0.5f, gainDb = 0.0f;
};

inline float sanitize (float x) noexcept { return std::isfinite (x) ? std::clamp (x, -8.0f, 8.0f) : 0.0f; }

struct Rng   // xorshift32: cheap, deterministic, real-time safe
{
    uint32_t s = 0x9E3779B9u;
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bi() noexcept { return uni() * 2.0f - 1.0f; }                               // [-1, 1)
};

// grain window, t in [0, 1]
inline float window (Shape s, float t) noexcept
{
    switch (s)
    {
        case Shape::triangle: return 1.0f - std::fabs (2.0f * t - 1.0f);
        case Shape::perc:     return t < 0.04f ? t / 0.04f : std::exp (-5.0f * (t - 0.04f) / 0.96f) * (1.0f - std::pow ((t - 0.04f) / 0.96f, 8.0f));
        case Shape::glitch:   return std::min (1.0f, std::min (t, 1.0f - t) * 40.0f);   // hard-edged block with 2.5 % fades
        case Shape::smooth: default: { const float c = std::sin ((float) kPi * t); return c * c; }
    }
}

// random pitch offset in [-spread, +spread] semitones, quantised by mode
inline float spreadOffset (Rng& r, float spread, SpreadMode m) noexcept
{
    if (spread <= 0.001f) return 0.0f;
    const float u = r.bi() * spread;
    switch (m)
    {
        case SpreadMode::semis: return std::round (u);
        case SpreadMode::octaves:
        {
            const int n = (int) std::floor (spread / 12.0f + 1e-4f);
            if (n <= 0) return 0.0f;
            return 12.0f * (float) ((int) (r.uni() * (float) (2 * n + 1)) - n);
        }
        case SpreadMode::fifths:
        {
            static const float steps[] = { -24, -19, -17, -12, -7, -5, 0, 5, 7, 12, 17, 19, 24 };
            float pick[13]; int k = 0;
            for (float s : steps) if (std::fabs (s) <= spread + 1e-4f) pick[k++] = s;
            return k > 0 ? pick[std::min (k - 1, (int) (r.uni() * (float) k))] : 0.0f;
        }
        case SpreadMode::free: default: return u;
    }
}

struct Grain
{
    bool active = false;
    double pos = 0.0;      // absolute (unwrapped) read position in buffer samples
    double inc = 1.0;      // samples per output sample (negative = reverse)
    int len = 1, t = 0;
    float gl = 0.7f, gr = 0.7f; Shape shape = Shape::smooth;
    float pitchSt = 0.0f; bool rev = false;
};

class Engine
{
public:
    // display taps (written by the audio thread, read by the UI; benign races)
    std::atomic<float> inLevel { 0 }, outLevel { 0 }, freezeAmt { 0 };
    std::atomic<int> activeGrains { 0 };
    struct GrainTap { std::atomic<float> age { -1 }, pitch { 0 }, amp { 0 }, pan { 0 }; std::atomic<int> rev { 0 }; };
    std::array<GrainTap, kMaxGrains> taps;
    std::array<std::atomic<float>, kPeakBins> peaks {};
    std::atomic<int> peakHead { 0 };      // index of the newest bin

    void setParams (const Params& np) noexcept { p = np; }
    const Params& params() const noexcept { return p; }

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        nch = std::clamp (numChannels, 1, 2);
        int n = 1; while (n < (int) (sr * kBufSeconds) + 8) n <<= 1;
        len = n; mask = n - 1;
        for (auto& b : buf) b.assign ((size_t) n, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& g : grains) g.active = false;
        w = 0; untilNext = 0.0; normSm = 1.0f; mixS = -1.0f; fbL = fbR = 0.0f; lpL1 = lpL2 = lpR1 = lpR2 = 0.0f; frz = p.freeze ? 1.0f : 0.0f;
        for (auto& pk : peaks) pk.store (0.0f, std::memory_order_relaxed);
        binAcc = 0; binPeak = 0.0f; binLen = std::max (1, (int) (sr * kBufSeconds / kPeakBins));
        inPk = outPk = 0.0f; rng.s = 0x9E3779B9u;
    }

    int bufferLength() const noexcept { return len; }
    double sampleRate() const noexcept { return sr; }

    void process (float* const* ch, int n) noexcept
    {
        if (len == 0 || n <= 0) return;
        float* L = ch[0]; float* R = nch > 1 ? ch[1] : nullptr;
        const float mixT = std::clamp (p.mix, 0.0f, 1.0f), gainT = std::pow (10.0f, p.gainDb / 20.0f);
        const float fbT = std::clamp (p.feedback, 0.0f, 0.95f);
        const float interval = (float) sr / std::clamp (p.density, 0.5f, 200.0f);
        const float grainLen = std::clamp (p.sizeMs, 2.0f, 2000.0f) * 0.001f * (float) sr;
        // expected overlap -> loudness normalisation (smoothed per block)
        const float overlap = std::max (1.0f, grainLen / interval * windowPower (p.shape));
        const float normTarget = 1.0f / std::sqrt (overlap);
        // tone: 2-pole low-pass (two cascaded one-poles, cheap and stable at any setting)
        const float aT = 1.0f - std::exp (-2.0f * (float) kPi * std::clamp (p.toneHz, 200.0f, 20000.0f) / (float) sr);
        const bool freezing = p.freeze;
        const float ks = smoothK();
        float inPkB = 0.0f, outPkB = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // mix, gain, feedback and tone glide (20 ms one-pole) instead of jumping: no clicks when they are moved or automated
            if (mixS < 0.0f) { mixS = mixT; gainS = gainT; fbS = fbT; aS = aT; }
            mixS += ks * (mixT - mixS); gainS += ks * (gainT - gainS); fbS += ks * (fbT - fbS); aS += ks * (aT - aS);
            const float mix = mixS, gain = gainS, fb = fbS, a = aS;
            float xl = sanitize (L[i]), xr = R != nullptr ? sanitize (R[i]) : xl;
            inPkB = std::max (inPkB, std::max (std::fabs (xl), std::fabs (xr)));
            // freeze is smoothed (8 ms) so the recording fades out / in instead of cutting
            frz += ((freezing ? 1.0f : 0.0f) - frz) * frzCoef();
            const float rec = 1.0f - frz;
            if (rec > 1e-4f)
            {
                const size_t wi = (size_t) (w & mask);
                const float nl = xl + fb * std::tanh (fbL), nr = xr + fb * std::tanh (fbR);
                buf[0][wi] = buf[0][wi] * frz + nl * rec;
                buf[1][wi] = buf[1][wi] * frz + nr * rec;
                binPeak = std::max (binPeak, std::max (std::fabs (nl), std::fabs (nr)));
                ++w;
                if (++binAcc >= binLen)
                {
                    const int h = (peakHead.load (std::memory_order_relaxed) + 1) % kPeakBins;
                    peaks[(size_t) h].store (binPeak, std::memory_order_relaxed); peakHead.store (h, std::memory_order_relaxed);
                    binAcc = 0; binPeak = 0.0f;
                }
            }

            // schedule grains
            untilNext -= 1.0;
            if (untilNext <= 0.0)
            {
                spawn (grainLen);
                const float jitter = 1.0f + std::clamp (p.chaos, 0.0f, 1.0f) * rng.bi() * 0.9f;
                untilNext += std::max (1.0f, interval * jitter);
            }

            // render grains
            float sl = 0.0f, sr2 = 0.0f, envSum = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float env = window (g.shape, (float) g.t / (float) g.len);
                envSum += env;
                const float vl = read (0, g.pos), vr = read (1, g.pos);
                const float m = 0.5f * (vl + vr), sd = 0.5f * (vl - vr);
                // pan the grain: keeps the source's stereo image partly (side), moves its centre
                sl += env * (m * g.gl * 1.4142f + sd);
                sr2 += env * (m * g.gr * 1.4142f - sd);
                g.pos += g.inc;
                if (++g.t >= g.len) g.active = false;
            }
            normSm += (normTarget - normSm) * 0.002f;
            float wl = sl * normSm, wr = sr2 * normSm;
            lpL1 += a * (wl - lpL1); lpL2 += a * (lpL1 - lpL2);
            lpR1 += a * (wr - lpR1); lpR2 += a * (lpR1 - lpR2);
            wl = lpL2; wr = lpR2;
            // feedback uses the envelope-normalised grain sum (a weighted average of what was read), so the loop gain stays below
            // 'feedback' (< 1) for dense clouds (whose grains add up mostly incoherently): echoes die away unless Freeze is on
            // (mono sum: equal-power pans give (gl + gr) / sqrt 2 <= 1, so nothing in the loop is above unity)
            { const float k = 0.5f / std::pow (std::max (1.0f, envSum), 0.8f); fbL = fbR = sanitize ((sl + sr2) * k); }
            float ol = (xl * (1.0f - mix) + wl * mix) * gain, orr = (xr * (1.0f - mix) + wr * mix) * gain;
            ol = softLimit (ol); orr = softLimit (orr);
            outPkB = std::max (outPkB, std::max (std::fabs (ol), std::fabs (orr)));
            if (R != nullptr) { L[i] = ol; R[i] = orr; }
            else L[i] = 0.5f * (ol + orr);
        }
        // denormal / runaway guards
        if (! std::isfinite (lpL2) || ! std::isfinite (lpR2)) { lpL1 = lpL2 = lpR1 = lpR2 = 0.0f; fbL = fbR = 0.0f; }
        inPk = std::max (inPkB, inPk * 0.9f); outPk = std::max (outPkB, outPk * 0.9f);
        inLevel.store (inPk, std::memory_order_relaxed); outLevel.store (outPk, std::memory_order_relaxed);
        freezeAmt.store (frz, std::memory_order_relaxed);
        publishTaps();
    }

private:
    static float windowPower (Shape s) noexcept   // mean of window^2-ish energy weight used for overlap normalisation
    {
        switch (s) { case Shape::smooth: return 0.5f; case Shape::triangle: return 0.5f; case Shape::perc: return 0.3f; case Shape::glitch: return 0.95f; }
        return 0.5f;
    }
    float frzCoef() const noexcept { return 1.0f - std::exp (-1.0f / (0.008f * (float) sr)); }
    static float softLimit (float x) noexcept { const float ax = std::fabs (x); return ax < 1.0f ? x : std::copysign (1.0f + std::tanh (ax - 1.0f) * 0.5f, x); }

    float read (int c, double pos) const noexcept   // 4-point Hermite on the circular buffer
    {
        const double fl = std::floor (pos); const float f = (float) (pos - fl); const int64_t i = (int64_t) fl;
        const auto& b = buf[(size_t) c];
        const float y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)], y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void spawn (float baseLen) noexcept
    {
        Grain* g = nullptr;
        for (auto& c : grains) if (! c.active) { g = &c; break; }
        if (g == nullptr)   // all busy: replace the one closest to its end
        {
            g = &grains[0];
            for (auto& c : grains) if ((c.len - c.t) < (g->len - g->t)) g = &c;
        }
        const float chaos = std::clamp (p.chaos, 0.0f, 1.0f);
        const int gl = std::max (16, (int) (baseLen * (1.0f + chaos * rng.bi() * 0.75f)));
        const float st = std::clamp (p.pitch + spreadOffset (rng, std::clamp (p.spread, 0.0f, 24.0f), p.spreadMode), -36.0f, 36.0f);
        const double ratio = std::pow (2.0, (double) st / 12.0);
        const bool rev = rng.uni() < std::clamp (p.reverse, 0.0f, 1.0f);
        const bool frozen = frz > 0.5f;
        // age = how far behind the write head the grain's first sample is
        double age = (double) (std::max (0.0f, p.positionMs) + std::max (0.0f, p.sprayMs) * rng.uni()) * 0.001 * sr;
        const double travel = (double) gl * ratio;
        const double maxAge = (double) len - 8.0;
        double lo, hi;
        if (! rev) { lo = frozen ? travel + 4.0 : std::max (4.0, (double) gl * (ratio - 1.0) + 4.0); hi = maxAge - (frozen ? 0.0 : std::max (0.0, (double) gl * (1.0 - ratio))); }
        else       { lo = 4.0; hi = maxAge - travel - (frozen ? 0.0 : (double) gl); }
        if (hi < lo) hi = lo;
        age = std::clamp (age, lo, hi);
        const double start = (double) w - age;
        g->active = true; g->t = 0; g->len = gl; g->shape = p.shape; g->rev = rev; g->pitchSt = st;
        g->inc = rev ? -ratio : ratio;
        g->pos = start;   // reverse grains start at their newest sample and run backwards
        const float pan = std::clamp (p.width, 0.0f, 1.0f) * rng.bi();
        const float ang = (pan + 1.0f) * 0.25f * (float) kPi;
        g->gl = std::cos (ang); g->gr = std::sin (ang);
    }

    void publishTaps() noexcept
    {
        int k = 0, act = 0;
        for (auto& g : grains)
        {
            auto& t = taps[(size_t) k++];
            if (! g.active) { t.age.store (-1.0f, std::memory_order_relaxed); continue; }
            ++act;
            const double age = ((double) w - g.pos) / (double) len;   // 0 = newest, 1 = oldest
            t.age.store ((float) std::clamp (age, 0.0, 1.0), std::memory_order_relaxed);
            t.pitch.store (g.pitchSt, std::memory_order_relaxed);
            t.amp.store (window (g.shape, (float) g.t / (float) g.len), std::memory_order_relaxed);
            t.pan.store (g.gr - g.gl, std::memory_order_relaxed);
            t.rev.store (g.rev ? 1 : 0, std::memory_order_relaxed);
        }
        activeGrains.store (act, std::memory_order_relaxed);
    }

    Params p;
    double sr = 48000.0; int nch = 2;
    std::array<std::vector<float>, 2> buf;
    int len = 0; int64_t mask = 0; int64_t w = 0;
    std::array<Grain, kMaxGrains> grains;
    double untilNext = 0.0;
    float mixS = -1.0f, gainS = 1.0f, fbS = 0.0f, aS = 0.5f;
    float smoothK() const noexcept { return 1.0f - std::exp (-1.0f / (0.02f * (float) sr)); }
    float normSm = 1.0f, fbL = 0, fbR = 0, lpL1 = 0, lpL2 = 0, lpR1 = 0, lpR2 = 0, frz = 0;
    int binAcc = 0, binLen = 1; float binPeak = 0;
    float inPk = 0, outPk = 0;
    Rng rng;
};
} // namespace gr::dsp
