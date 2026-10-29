#pragma once
// CMPLXTY Dark Reverb DSP (plain C++, no JUCE): pre-delay -> metallic comb + input diffusion -> 8-line modulated feedback
// delay network (Householder mix, in-loop damping and tuned all-passes) -> octave shimmer fed back into the network -> freeze,
// width, dry/wet and output gain. Zero latency. Real-time safe after prepare().
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dr::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kLines = 8;

struct Params
{
    float preDelayMs = 20.0f, size = 0.6f, decayS = 6.0f, diffusion = 0.7f;
    float darkHz = 3500.0f, lowCutHz = 80.0f, metal = 0.25f, mod = 0.3f;
    bool shimOn = false; float shimSemis = 12.0f, shimAmt = 0.3f;
    bool freeze = false; float width = 1.0f, mix = 0.35f, gainDb = 0.0f;
};

inline float sanitize (float x) noexcept { return std::isfinite (x) ? std::clamp (x, -4.0f, 4.0f) : 0.0f; }

// power-of-two circular delay with fractional (linear) reads
struct Delay
{
    std::vector<float> b; int mask = 0, w = 0;
    void prepare (int maxLen) { int n = 1; while (n < maxLen + 4) n <<= 1; b.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
    void push (float x) noexcept { b[(size_t) w] = x; w = (w + 1) & mask; }
    float at (int d) const noexcept { return b[(size_t) ((w - 1 - d) & mask)]; }            // d = 0: newest
    float frac (float d) const noexcept
    {
        const int i = (int) d; const float f = d - (float) i;
        return at (i) + f * (at (i + 1) - at (i));
    }
    float hermite (float d) const noexcept   // 4-point, 3rd order: far less high-frequency loss than linear inside a feedback loop
    {
        const int i = (int) d; const float f = d - (float) i;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

struct Allpass   // Schroeder all-pass on a fixed integer delay
{
    Delay d; int len = 1;
    void prepare (int n) { len = std::max (1, n); d.prepare (len + 1); }
    float process (float x, float g) noexcept
    {
        const float v = d.at (len - 1);
        const float u = x + g * v;
        d.push (u);
        return v - g * u;
    }
};

struct OnePole
{
    float z = 0;
    float lp (float x, float a) noexcept { z += a * (x - z); return z; }
    static float coef (float hz, double fs) { return (float) (1.0 - std::exp (-2.0 * kPi * hz / fs)); }
};

// granular two-head pitch shifter with a fixed ratio (used inside the shimmer loop, so a little latency does not matter)
struct GrainShifter
{
    Delay d; float win = 2048, ph = 0;
    void prepare (double fs) { win = (float) std::round (0.07 * fs); d.prepare ((int) win * 2 + 8); ph = 0; }
    void clear() { d.clear(); ph = 0; }
    float process (float x, float ratio) noexcept
    {
        d.push (x);
        ph += (1.0f - ratio) / win;
        ph -= std::floor (ph);
        float p2 = ph + 0.5f; p2 -= std::floor (p2);
        const float a = std::sin ((float) kPi * ph), b = std::sin ((float) kPi * p2);
        return d.frac (ph * win + 1.0f) * a * a + d.frac (p2 * win + 1.0f) * b * b;
    }
};

class Engine
{
public:
    // display taps
    std::atomic<float> inLevel { 0 }, wetLevel { 0 }, freezeAmt { 0 };

    void setParams (const Params& np) noexcept { p = np; dirty = true; }

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate > 0 ? sampleRate : 48000.0; nch = std::clamp (numChannels, 1, 2);
        pre.fill ({}); for (auto& d : pre) d.prepare ((int) (0.5 * fs) + 4);
        for (int c = 0; c < 2; ++c)
        {
            static const float apMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
            for (int i = 0; i < 4; ++i) diff[c][i].prepare ((int) std::round (apMs[i] * (1.0f + 0.13f * c) * 1e-3 * fs));
            comb[c].prepare ((int) (0.02 * fs)); combLen[c] = (float) ((c == 0 ? 3.3 : 4.7) * 1e-3 * fs); combZ[c] = 0;
            shift[c].prepare (fs); hpZ[c] = 0; shimFb[c] = 0; shimEnv[c] = 0;
        }
        static const float lineMs[kLines] = { 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.1f, 73.7f, 79.3f };
        static const float metMs[kLines] = { 2.1f, 2.9f, 3.7f, 4.3f, 5.3f, 6.1f, 6.7f, 7.9f };
        static const float lfoHz[kLines] = { 0.11f, 0.23f, 0.31f, 0.43f, 0.53f, 0.67f, 0.79f, 0.91f };
        for (int i = 0; i < kLines; ++i)
        {
            baseLen[i] = (float) (lineMs[i] * 1e-3 * fs);
            line[i].prepare ((int) (baseLen[i] * 2.0f + 0.004 * fs) + 8);
            metAp[i].prepare ((int) std::round (metMs[i] * 1e-3 * fs));
            damp[i].z = 0; lfoPh[i] = (float) i / kLines; lfoInc[i] = (float) (lfoHz[i] / fs);
        }
        sizeS = sizeScale (p.size); gate = p.freeze ? 0.0f : 1.0f; frz = p.freeze ? 1.0f : 0.0f;
        mixS = p.mix; gainS = dbToGain (p.gainDb); widthS = p.width;
        dirty = true; update();
    }

    void reset()
    {
        for (auto& d : pre) d.clear();
        for (int c = 0; c < 2; ++c) { for (auto& a : diff[c]) a.d.clear(); comb[c].clear(); combZ[c] = 0; shift[c].clear(); hpZ[c] = 0; shimFb[c] = 0; shimEnv[c] = 0; }
        for (int i = 0; i < kLines; ++i) { line[i].clear(); metAp[i].d.clear(); damp[i].z = 0; }
        for (auto& o : outLp) o.z = 0;
    }

    // ch[0..nch-1] in place
    void process (float* const* ch, int n) noexcept
    {
        if (dirty) update();
        float pkIn = 0, pkWet = 0;
        const float k8 = 2.0f / kLines;
        for (int s = 0; s < n; ++s)
        {
            float in[2];
            in[0] = sanitize (ch[0][s]); in[1] = nch > 1 ? sanitize (ch[1][s]) : in[0];
            pkIn = std::max (pkIn, std::max (std::fabs (in[0]), std::fabs (in[1])));

            // smoothed controls (per sample, so the result is block-size independent)
            sizeS += 0.0005f * (sizeTarget - sizeS);
            gate += smF * ((p.freeze ? 0.0f : 1.0f) - gate);
            frz += sm * ((p.freeze ? 1.0f : 0.0f) - frz);
            mixS += sm * (p.mix - mixS); gainS += sm * (gainTarget - gainS); widthS += sm * (p.width - widthS);

            // input: low cut, pre-delay, metallic comb, diffusion
            float x[2];
            for (int c = 0; c < 2; ++c)
            {
                hpZ[c] += hpA * (in[c] - hpZ[c]);
                pre[c].push ((in[c] - hpZ[c]) * gate + shimFb[c]);
                float v = pre[c].frac (preLen);
                const float cv = comb[c].frac (combLen[c]);
                combZ[c] += 0.8f * (cv - combZ[c]);
                const float u = v + combG * combZ[c];
                comb[c].push (u);
                v = v * (1.0f - combMix) + u * combMix * combNorm;   // level-normalised comb crossfade: resonance without a level jump
                for (auto& a : diff[c]) v = a.process (v, diffG);
                x[c] = v;
            }

            // feedback delay network
            float o[kLines], sum = 0;
            for (int i = 0; i < kLines; ++i)
            {
                lfoPh[i] += lfoInc[i]; if (lfoPh[i] >= 1.0f) lfoPh[i] -= 1.0f;
                // integer length for the size (lossless), fractional only for the modulation, which fades out while frozen
                const float md = modDepth * (1.0f - frz);
                const int L = (int) (baseLen[i] * sizeS);
                o[i] = md > 1e-4f ? line[i].hermite ((float) L + md * (1.0f + std::sin (2.0f * (float) kPi * lfoPh[i]))) : line[i].at (L);
                sum += o[i];
            }
            float wl = 0, wr = 0;
            for (int i = 0; i < kLines; ++i)
            {
                wl += (i & 1) ? -o[i] : o[i];
                wr += (i & 2) ? -o[i] : o[i];
                float y = o[i] - k8 * sum;                                   // Householder: lossless mixing
                const float lpA = dampA + (1.0f - dampA) * frz;               // freeze: no damping
                y = damp[i].lp (y, lpA);
                y = metAp[i].process (y, metalG * (1.0f - frz));
                const float g = gLine[i] + (1.0f - gLine[i]) * frz;
                const float inj = (i & 1) ? x[1] : x[0];
                line[i].push (y * g + inj * ((i & 4) ? -0.5f : 0.5f));
            }
            wl = outLp[1].lp (outLp[0].lp (wl * wetScale, outA), outA);
            wr = outLp[3].lp (outLp[2].lp (wr * wetScale, outA), outA);
            if (! std::isfinite (wl) || ! std::isfinite (wr)) { reset(); wl = wr = 0; }

            // shimmer: pitch-shifted tail fed back into the input (energy-normalised, soft limited, off while frozen)
            for (int c = 0; c < 2; ++c)
            {
                const float w = c == 0 ? wl : wr;
                const float sh = shimOn ? shift[c].process (w, shimRatio) : 0.0f;
                const float v = sh * shimGain * (1.0f - frz);
                const float a = std::fabs (v);
                shimEnv[c] += (a > shimEnv[c] ? envAtk : envRel) * (a - shimEnv[c]);
                shimFb[c] = v * std::min (1.0f, 0.12f / (shimEnv[c] + 1e-9f));   // feedback limiter: the shimmer can never run away
            }

            // width (M/S) and output
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * widthS;
            wl = m + sd; wr = m - sd;
            pkWet = std::max (pkWet, std::max (std::fabs (wl), std::fabs (wr)));
            if (nch > 1)
            {
                ch[0][s] = (in[0] * (1.0f - mixS) + wl * mixS) * gainS;
                ch[1][s] = (in[1] * (1.0f - mixS) + wr * mixS) * gainS;
            }
            else ch[0][s] = (in[0] * (1.0f - mixS) + 0.5f * (wl + wr) * mixS) * gainS;
        }
        inLevel.store (pkIn, std::memory_order_relaxed);
        wetLevel.store (pkWet, std::memory_order_relaxed);
        freezeAmt.store (frz, std::memory_order_relaxed);
    }

    static float sizeScale (float size01) noexcept { return 0.25f + 1.75f * std::clamp (size01, 0.0f, 1.0f); }
    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

private:
    void update() noexcept
    {
        dirty = false;
        sm = (float) (1.0 - std::exp (-1.0 / (0.03 * fs)));
        envAtk = (float) (1.0 - std::exp (-1.0 / (0.003 * fs))); envRel = (float) (1.0 - std::exp (-1.0 / (0.25 * fs)));
        smF = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));
        sizeTarget = sizeScale (p.size);
        const float T = std::clamp (p.decayS, 0.1f, 120.0f);
        float gsum = 0;
        for (int i = 0; i < kLines; ++i)
        {
            const float L = baseLen[i] * sizeTarget;
            gLine[i] = std::pow (10.0f, -3.0f * L / (T * (float) fs));
            gsum += gLine[i];
        }
        const float ga = gsum / kLines;
        wetScale = 0.55f * std::pow (std::max (1e-4f, 1.0f - ga * ga), 0.25f);
        preLen = (float) (std::clamp (p.preDelayMs, 0.0f, 500.0f) * 1e-3 * fs);
        hpA = OnePole::coef (std::clamp (p.lowCutHz, 10.0f, 2000.0f), fs);
        dampA = OnePole::coef (std::clamp (p.darkHz, 200.0f, 20000.0f), fs);
        outA = OnePole::coef (std::clamp (p.darkHz * 1.5f, 300.0f, 24000.0f), fs);
        diffG = 0.75f * std::clamp (p.diffusion, 0.0f, 1.0f);
        const float mt = std::clamp (p.metal, 0.0f, 1.0f);
        metalG = 0.8f * mt; combG = 0.9f * mt; combMix = 0.75f * mt; combNorm = (1.0f - combG) * 2.6f;
        modDepth = (float) (0.0015 * fs) * std::clamp (p.mod, 0.0f, 1.0f);
        shimOn = p.shimOn && p.shimAmt > 0.0f;
        shimRatio = std::pow (2.0f, std::clamp (p.shimSemis, -24.0f, 24.0f) / 12.0f);
        shimGain = 1.0f * std::clamp (p.shimAmt, 0.0f, 1.0f) * std::sqrt (std::max (0.0f, 1.0f - ga * ga)) / std::max (1e-3f, wetScale);
        gainTarget = dbToGain (p.gainDb);
    }

    Params p; bool dirty = true; double fs = 48000.0; int nch = 2;
    std::array<Delay, 2> pre; Allpass diff[2][4]; Delay comb[2]; float combLen[2] {}, combZ[2] {}, combNorm = 1;
    GrainShifter shift[2]; float hpZ[2] {}, shimFb[2] {}, shimEnv[2] {}, envAtk = 0.004f, envRel = 0.0001f;
    Delay line[kLines]; Allpass metAp[kLines]; OnePole damp[kLines]; OnePole outLp[4]; float outA = 1;
    float baseLen[kLines] {}, gLine[kLines] {}, lfoPh[kLines] {}, lfoInc[kLines] {};
    float sm = 0.001f, smF = 0.005f, sizeS = 1, sizeTarget = 1, gate = 1, frz = 0, mixS = 0, gainS = 1, gainTarget = 1, widthS = 1;
    float wetScale = 0.3f, preLen = 0, hpA = 0, dampA = 1, diffG = 0, metalG = 0, combG = 0, combMix = 0, modDepth = 0;
    bool shimOn = false; float shimRatio = 2, shimGain = 0;
};
} // namespace dr::dsp
