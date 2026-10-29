// Glitch Crusher DSP core - plain C++17, no JUCE dependency.
// Signal flow (per channel):
//   in -> drive (tanh blend) -> sample-rate reduction (hold) -> bit-depth quantiser
//      -> tempo-synced stutter/glitch -> SVF filter (LP/HP/off) -> dry/wet mix -> output gain
// All buffers are allocated in prepare(); process() never allocates.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gc
{
constexpr int kMaxChannels = 8;

struct Params
{
    float bits        = 8.0f;   // 1..16 (fractional allowed, smoothed)
    float downsample  = 4.0f;   // 1..64 hold factor in samples (fractional allowed)
    float drive       = 0.2f;   // 0..1
    float glitchProb  = 0.25f;  // 0..1 probability a grid slice becomes a stutter repeat
    int   glitchRate  = 2;      // 0:1/4  1:1/8  2:1/16  3:1/32
    int   reverseMode = 0;      // 0 forward, 1 reverse, 2 random per glitch
    int   filterType  = 0;      // 0 off, 1 lowpass, 2 highpass
    float cutoffHz    = 8000.f; // 20..20000
    float resonance   = 0.7f;   // Q, 0.5..10
    float mix         = 1.0f;   // 0..1
    float outGainDb   = 0.0f;   // -24..+12
    bool  crushOn     = true;   // section switches (cross-faded, no clicks): CRUSH = drive + downsample + bit depth
    bool  glitchOn    = true;   // GLITCH = tempo-synced stutter
    bool  filterOn    = true;   // FILTER (also needs filterType != 0)
};

struct Transport
{
    double bpm        = 120.0;
    double ppq        = 0.0;     // quarter-note position of first sample of block (valid if hasPpq)
    bool   hasPpq     = false;   // host provides ppq and is playing
};

namespace detail
{
inline float sanitize (float x) noexcept
{
    if (! (std::fabs (x) <= 3.0e38f)) return 0.0f;   // NaN or +/-inf -> silence
    return std::fmax (-32.0f, std::fmin (32.0f, x)); // hard safety rail (~+30 dBFS)
}
struct Smooth
{
    float cur = 0.f, target = 0.f, coef = 1.f;
    void set (float v) noexcept { target = v; }
    void snap (float v) noexcept { cur = target = v; }
    float next() noexcept { cur += (target - cur) * coef; return cur; }
};
} // namespace detail

class Engine
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
    {
        sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
        nch = std::max (1, std::min (numChannels, kMaxChannels));
        const float coef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr)); // 20 ms
        for (auto* s : { &sBits, &sDown, &sDrive, &sCut, &sRes, &sMix, &sGain })
            s->coef = coef;
        const float swCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr)); // 5 ms section on/off cross-fade
        sCrushOn.coef = sFiltOn.coef = swCoef;
        // record buffer long enough for a 1/4 note at 30 BPM (2 s) + margin
        capacity = (int) (2.2 * sr) + 8;
        for (int c = 0; c < nch; ++c)
        {
            rec[(size_t) c].assign ((size_t) capacity, 0.0f);
            frozen[(size_t) c].assign ((size_t) capacity, 0.0f);
        }
        fadeLen = std::max (8, (int) (0.0015 * sr));
        gateCoef = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        reset();
        snapSmoothers();
    }

    void reset()
    {
        for (int c = 0; c < nch; ++c)
        {
            std::fill (rec[(size_t) c].begin(), rec[(size_t) c].end(), 0.0f);
            std::fill (frozen[(size_t) c].begin(), frozen[(size_t) c].end(), 0.0f);
            held[(size_t) c] = 0.f; ic1[(size_t) c] = ic2[(size_t) c] = 0.f;
        }
        writePos = 0; holdCnt = 1.0e9f; glitching = false; reversed = false;
        frozenLen = 1; playPos = 0; gate = 0.f; lastSliceIdx = INT64_MIN; freePpq = 0.0;
        rng = 0x9E3779B97F4A7C15ull; slicesSeen = glitchSlices = 0;
    }

    void setParams (const Params& p)
    {
        params = p;
        sBits.set (std::fmax (1.0f, std::fmin (16.0f, p.bits)));
        sDown.set (std::fmax (1.0f, std::fmin (64.0f, p.downsample)));
        sDrive.set (std::fmax (0.f, std::fmin (1.f, p.drive)));
        sCut.set (std::log (std::fmax (20.f, std::fmin (20000.f, p.cutoffHz))));
        sRes.set (std::fmax (0.5f, std::fmin (10.f, p.resonance)));
        sMix.set (std::fmax (0.f, std::fmin (1.f, p.mix)));
        sGain.set (std::pow (10.0f, std::fmax (-24.f, std::fmin (12.f, p.outGainDb)) / 20.0f));
        sCrushOn.set (p.crushOn ? 1.0f : 0.0f);
        sFiltOn.set (p.filterOn && p.filterType != 0 ? 1.0f : 0.0f);
    }

    void snapSmoothers()
    {
        for (auto* s : { &sBits, &sDown, &sDrive, &sCut, &sRes, &sMix, &sGain, &sCrushOn, &sFiltOn }) s->snap (s->target);
    }

    // Process in place. numChannels may be <= prepared channels.
    void process (float* const* io, int numChannels, int numSamples, const Transport& tp) noexcept
    {
        const int ch = std::min (numChannels, nch);
        const int N = (int) glitchDiv (params.glitchRate);
        const double bpm = std::fmax (30.0, std::fmin (300.0, tp.bpm > 0 ? tp.bpm : 120.0));
        const double beatsPerStep = 4.0 / (double) N;
        const double ppqPerSample = bpm / 60.0 / sr;
        const int sliceSamples = std::max (16, std::min (capacity - 8, (int) std::lround (beatsPerStep * 60.0 / bpm * sr)));
        const float prob = params.glitchOn ? std::fmax (0.f, std::fmin (1.f, params.glitchProb)) : 0.0f;
        double ppq = tp.hasPpq ? tp.ppq : freePpq;

        for (int i = 0; i < numSamples; ++i)
        {
            const float bits = sBits.next(), down = sDown.next(), drv = sDrive.next();
            const float cutL = sCut.next(), q = sRes.next(), mix = sMix.next(), gain = sGain.next();
            const float crushAmt = sCrushOn.next(), filtAmt = sFiltOn.next();
            const bool filtActive = filtAmt > 1.0e-4f || sFiltOn.target > 0.5f;

            // --- slice grid (tempo sync) ---
            const auto sliceIdx = (int64_t) std::floor (ppq / beatsPerStep);
            if (sliceIdx != lastSliceIdx)
            {
                const bool first = (lastSliceIdx == INT64_MIN);
                lastSliceIdx = sliceIdx;
                if (! first) onSliceBoundary (prob, sliceSamples, ch);
            }
            ppq += ppqPerSample;

            // --- crusher params for this sample ---
            holdCnt += 1.0f;
            const bool resample = holdCnt >= down;
            if (resample) { holdCnt -= down; if (holdCnt >= down) holdCnt = 0.f; }
            const float step = 2.0f / std::exp2 (bits);       // 2^bits levels over [-1,1]
            const float invStep = 1.0f / step;
            const float lim = 1.0f - 0.5f * step;
            const float g = 1.0f + 19.0f * drv;
            const float invTanhG = 1.0f / std::tanh (g);

            // --- filter coefficients (TPT SVF) ---
            const float fc = std::fmin (std::exp (cutL), 0.45f * (float) sr);
            const float gg = std::tan (3.14159265358979f * fc / (float) sr);
            const float k = 1.0f / q;
            const float a1 = 1.0f / (1.0f + gg * (gg + k));
            const float a2 = gg * a1, a3 = gg * a2;

            // --- glitch gate (declick) ---
            gate += ((glitching ? 1.0f : 0.0f) - gate) * gateCoef;
            float env = 1.0f;
            {
                const int e = std::min (playPos, frozenLen - 1 - playPos);
                env = e >= fadeLen ? 1.0f : (float) e / (float) fadeLen;
            }
            const int rp = reversed ? (frozenLen - 1 - playPos) : playPos;

            for (int c = 0; c < ch; ++c)
            {
                const float dry = detail::sanitize (io[c][i]);
                // drive (exact bypass at 0)
                float x = dry;
                if (drv > 1.0e-4f) x = dry + drv * (std::tanh (g * dry) * invTanhG - dry);
                // sample-rate reduction
                if (resample) held[(size_t) c] = x;
                float y = held[(size_t) c];
                // bit-depth quantiser (mid-rise)
                y = (std::floor (y * invStep) + 0.5f) * step;
                y = std::fmax (-lim, std::fmin (lim, y));
                // CRUSH section switch: cross-fade to the untouched input
                if (crushAmt < 1.0f) y = dry + crushAmt * (y - dry);
                // record crushed signal, then maybe replace with frozen slice
                rec[(size_t) c][(size_t) writePos] = y;
                if (gate > 1.0e-4f)
                {
                    const float gl = frozen[(size_t) c][(size_t) rp] * env;
                    y += gate * (gl - y);
                }
                // filter (state keeps running while the on/off cross-fade is audible)
                if (filtActive && params.filterType != 0)
                {
                    const float v3 = y - ic2[(size_t) c];
                    const float v1 = a1 * ic1[(size_t) c] + a2 * v3;
                    const float v2 = ic2[(size_t) c] + a2 * ic1[(size_t) c] + a3 * v3;
                    ic1[(size_t) c] = 2.0f * v1 - ic1[(size_t) c];
                    ic2[(size_t) c] = 2.0f * v2 - ic2[(size_t) c];
                    const float f = params.filterType == 1 ? v2 : (y - k * v1 - v2);
                    y = filtAmt >= 1.0f ? f : y + filtAmt * (f - y);
                    if (! (std::fabs (ic1[(size_t) c]) < 1.0e6f && std::fabs (ic2[(size_t) c]) < 1.0e6f))
                        ic1[(size_t) c] = ic2[(size_t) c] = 0.f;
                }
                else { ic1[(size_t) c] = ic2[(size_t) c] = 0.f; }
                const float out = ((1.0f - mix) * dry + mix * y) * gain;
                io[c][i] = detail::sanitize (out);
            }
            if (++writePos >= capacity) writePos = 0;
            if (++playPos >= frozenLen) playPos = 0;
        }
        if (! tp.hasPpq) freePpq = ppq;
    }

    // diagnostics (for tests)
    int64_t slicesSeen = 0, glitchSlices = 0;
    int sliceLengthSamples (double bpm, int rate) const
    {
        return (int) std::lround ((4.0 / glitchDiv (rate)) * 60.0 / bpm * sr);
    }

private:
    static int glitchDiv (int rate) noexcept
    {
        static const int d[4] = { 4, 8, 16, 32 };
        return d[std::max (0, std::min (3, rate))];
    }
    float rand01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (float) ((rng >> 40) & 0xFFFFFF) / 16777216.0f;
    }
    void onSliceBoundary (float prob, int sliceSamples, int ch) noexcept
    {
        ++slicesSeen;
        const bool hit = prob > 0.f && rand01() < prob;
        if (! hit) { glitching = false; return; }
        ++glitchSlices;
        if (glitching && frozenLen == sliceSamples) return; // keep repeating same material
        // freeze the slice that just ended
        frozenLen = sliceSamples;
        for (int c = 0; c < ch; ++c)
        {
            auto& r = rec[(size_t) c]; auto& f = frozen[(size_t) c];
            int src = writePos - sliceSamples; if (src < 0) src += capacity;
            for (int n = 0; n < sliceSamples; ++n)
            {
                f[(size_t) n] = r[(size_t) src];
                if (++src >= capacity) src = 0;
            }
        }
        playPos = 0; glitching = true;
        reversed = params.reverseMode == 1 || (params.reverseMode == 2 && rand01() < 0.5f);
    }

    Params params;
    double sr = 44100.0;
    int nch = 2, capacity = 0, writePos = 0, frozenLen = 1, playPos = 0, fadeLen = 64;
    bool glitching = false, reversed = false;
    float holdCnt = 1.0e9f, gate = 0.f, gateCoef = 0.01f;
    int64_t lastSliceIdx = INT64_MIN;
    double freePpq = 0.0;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    detail::Smooth sBits, sDown, sDrive, sCut, sRes, sMix, sGain, sCrushOn, sFiltOn;
    std::array<std::vector<float>, kMaxChannels> rec, frozen;
    std::array<float, kMaxChannels> held {}, ic1 {}, ic2 {};
};
} // namespace gc
