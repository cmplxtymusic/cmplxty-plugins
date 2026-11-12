#pragma once
#include "Parameters.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define RIGGED_HAS_FTZ 1
#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#define RIGGED_HAS_DAZ 1
#else
#define RIGGED_HAS_DAZ 0
#endif
#else
#define RIGGED_HAS_FTZ 0
#define RIGGED_HAS_DAZ 0
#endif

namespace rigged
{
struct FlushDenormals
{
#if RIGGED_HAS_FTZ
    unsigned int saved;
    FlushDenormals() noexcept
    {
        saved = _mm_getcsr();
        unsigned int m = saved | 0x8000; // FTZ
#if RIGGED_HAS_DAZ
        m |= 0x0040; // DAZ
#endif
        _mm_setcsr(m);
    }
    ~FlushDenormals() noexcept { _mm_setcsr(saved); }
#else
    FlushDenormals() noexcept {}
#endif
};

// ADAA tanh, same antiderivative trick as HEFT, for low-alias drive.
inline float logcosh(float x)
{
    const float a = std::abs(x);
    return a + std::log1p(std::exp(-2 * a)) - 0.69314718056f;
}
inline float adaaTanh(float x, float old)
{
    return std::abs(x - old) > 1.0e-3f ? (logcosh(x) - logcosh(old)) / (x - old)
                                       : std::tanh((x + old) * 0.5f);
}

class Engine
{
  public:
    void prepare(double rate, int /*maxBlock*/)
    {
        FlushDenormals fz;
        sr = rate > 100 ? rate : 48000;
        smooth = onePole(0.020, sr);
        matchSmooth = onePole(0.40, sr);
        matchFast = onePole(0.060, sr);
        limRel = onePole(0.080, sr);
        bypassSmooth = onePole(0.006, sr);
        const int dly = (int)std::max(sr * 2.2, 4096.0);
        delayL.assign((size_t)dly, 0.f);
        delayR.assign((size_t)dly, 0.f);
        delayCap = dly;
        dlyWrite = 0;
        const int ch = (int)std::max(sr * 0.08, 256.0);
        chorL.assign((size_t)ch, 0.f);
        chorR.assign((size_t)ch, 0.f);
        chorCap = ch;
        chorWrite = 0;
        *this = Engine();
        // re-apply after the assignment reset
        sr = rate > 100 ? rate : 48000;
        smooth = onePole(0.020, sr);
        matchSmooth = onePole(0.40, sr);
        matchFast = onePole(0.060, sr);
        limRel = onePole(0.080, sr);
        bypassSmooth = onePole(0.006, sr);
        delayL.assign((size_t)dly, 0.f);
        delayR.assign((size_t)dly, 0.f);
        delayCap = dly;
        chorL.assign((size_t)ch, 0.f);
        chorR.assign((size_t)ch, 0.f);
        chorCap = ch;
        for (int i = 0; i < Count; ++i)
            cur.v[(size_t)i] = sm.v[(size_t)i] = definitions[(size_t)i].initial;
        // K-weight (BS.1770) bilinear-ish 48k prototype, warped by sr ratio.
        setupKWeight();
        resetFxState();
    }

    void reset()
    {
        resetFxState();
        limiter = 1;
        makeup = 1;
        inEnv = 1.0e-6f;
        wetEnv = 1.0e-6f;
        bypassGain = 1;
        reductionDb = 0;
        makeupDb = 0;
        meter = 0;
        compGrDb = 0;
    }

    void set(const Settings &s) { cur = s; }
    void snap()
    {
        sm = cur;
        updateCoeffs();
        dlyTime = 0; // snap delay time on next sample
        makeup = 1;
        limiter = 1;
        inEnv = wetEnv = 1.0e-16f;
        bypassGain = sm[Bypass] >= 0.5f ? 0.f : 1.f;
    }

    void setTempo(double bpm, bool playing = false, double ppq = 0)
    {
        tempo = clampf((float)bpm, 20.f, 300.f);
        hostPlaying = playing;
        hostPpq = ppq;
    }

    void process(float *left, float *right, int n) noexcept
    {
        FlushDenormals fz;
        if (n <= 0 || left == nullptr)
            return;
        const bool mono = (right == nullptr);
        for (int i = 0; i < n; ++i)
        {
            tickSmooth();
            if ((age & 7) == 0)
                updateCoeffs();
            ++age;

            float l = clean(left[i]);
            float r = clean(mono ? l : right[i]);
            const float dryL = l, dryR = r;

            const float bypassAmt = sm[Bypass];
            bypassGain += bypassSmooth * ((bypassAmt >= 0.5f ? 0.f : 1.f) - bypassGain);
            if (bypassGain < 1.0e-4f)
            {
                left[i] = dryL;
                if (!mono)
                    right[i] = dryR;
                continue;
            }

            float xL = l, xR = r;
            for (int s = 0; s < kFxCount; ++s)
            {
                const int fx = cur.order[(size_t)s];
                if (fx < 0 || fx >= kFxCount)
                    continue;
                if (sm[enableOf(fx)] < 0.001f)
                    continue;
                if (sm[enableOf(fx)] < 0.999f)
                {
                    // Fade an effect on/off instead of a hard click.
                    float aL = xL, aR = xR;
                    processFx(fx, aL, aR);
                    const float w = sm[enableOf(fx)];
                    xL = xL + w * (aL - xL);
                    xR = xR + w * (aR - xR);
                }
                else
                    processFx(fx, xL, xR);
                xL = clean(xL);
                xR = clean(xR);
            }

            // Automatic gain match: K-weighted mean-square over a ~400 ms window,
            // so the makeup tracks LUFS rather than instantaneous peaks.
            const float kin = kWeight(dryL, dryR, kwIn);
            const float kwet = kWeight(xL, xR, kwWet);
            const float inSq = kin * kin, wetSq = kwet * kwet;
            inEnv += matchSmooth * (inSq - inEnv);
            wetEnv += matchSmooth * (wetSq - wetEnv);
            if (inEnv < 1.0e-16f)
                inEnv = 1.0e-16f;
            if (wetEnv < 1.0e-16f)
                wetEnv = 1.0e-16f;
            float want = 1.f;
            if (sm[GainMatch] >= 0.5f)
                want = clampf(std::sqrt(inEnv / wetEnv), 0.01f, 100.f); // ±40 dB
            const float gap = std::abs(gainToDb(want) - gainToDb(std::max(makeup, 1.0e-6f)));
            const float ms = gap > 0.8f ? matchFast : matchSmooth;
            makeup += ms * (want - makeup);
            xL *= makeup;
            xR *= makeup;
            makeupDb = gainToDb(makeup);

            const float mix = sm[Mix];
            xL = dryL * (1.f - mix) + xL * mix;
            xR = dryR * (1.f - mix) + xR * mix;

            const float outG = dbToGain(sm[Output]);
            xL *= outG;
            xR *= outG;

            if (sm[LimiterOn] >= 0.5f)
            {
                const float peak = std::max(std::abs(xL), std::abs(xR));
                const float need = std::min(1.f, kCeiling / std::max(peak, 1.0e-12f));
                limiter = need < limiter ? need : limiter + limRel * (need - limiter);
                xL = clampf(xL * limiter, -kCeiling, kCeiling);
                xR = clampf(xR * limiter, -kCeiling, kCeiling);
                reductionDb = -gainToDb(std::max(limiter, 1.0e-8f));
            }
            else
            {
                limiter = 1;
                reductionDb = 0;
            }

            if (bypassGain < 0.999f)
            {
                xL = dryL + bypassGain * (xL - dryL);
                xR = dryR + bypassGain * (xR - dryR);
            }

            xL = clean(xL);
            xR = clean(xR);
            left[i] = xL;
            if (!mono)
                right[i] = xR;
            const float pk = std::max(std::abs(xL), std::abs(xR));
            meter += 0.01f * (pk - meter);
        }
    }

    float reductionDb = 0, makeupDb = 0, meter = 0, compGrDb = 0;
    Settings sm; // smoothed (test-visible)

  private:
    struct FiltSt
    {
        float ic1 = 0, ic2 = 0;
    };
    struct SvfCoef
    {
        float g = 0, k = 1.41421356f, a1 = 0;
        int mode = 0;
        bool on = false;
    };
    struct KwSt
    {
        float z1 = 0, z2 = 0, y1 = 0, y2 = 0; // pre
        float r1 = 0, r2 = 0, q1 = 0, q2 = 0; // rlb, two channels packed as L/R via two structs
    };

    Settings cur;
    double sr = 48000;
    float smooth = 0.001f, matchSmooth = 0.001f, matchFast = 0.01f, limRel = 0.001f, bypassSmooth = 0.01f;
    float tempo = 120, hostPpq = 0;
    bool hostPlaying = false;
    unsigned age = 0;
    float limiter = 1, makeup = 1, inEnv = 1.0e-6f, wetEnv = 1.0e-6f, bypassGain = 1;

    // Filter
    std::array<FiltSt, 2> fltSt{};
    SvfCoef fltCoef{};

    // Drive
    float drvPrev[2]{};
    float drvLp[2]{}, drvHp[2]{};

    // Crush
    float crushHoldL = 0, crushHoldR = 0;
    int crushLeft = 0;

    // Ring
    double ringPhase = 0;

    // Chorus
    std::vector<float> chorL, chorR;
    int chorCap = 0, chorWrite = 0;
    double chorPhase = 0;

    // Phaser
    float phApX[2][6]{}, phApY[2][6]{};
    float phFbL = 0, phFbR = 0;
    double phPhase = 0;

    // Delay
    std::vector<float> delayL, delayR;
    int delayCap = 0, dlyWrite = 0;
    float dlyTime = 0, dlyLpL = 0, dlyLpR = 0;

    // Reverb (4-comb + 2 allpass)
    static constexpr int kCombN[4] = { 1553, 1613, 1741, 1871 };
    static constexpr int kApN[2] = { 223, 337 };
    float combL[4][1872]{}, combR[4][1872]{};
    int combIdx[4]{};
    float apL[2][338]{}, apR[2][338]{};
    int apIdx[2]{};
    float rvLpL = 0, rvLpR = 0;

    // Comp
    float compEnv = 0;

    // Tremolo
    double tremPhase = 0;

    // K-weight
    float kwB1[3]{}, kwA1[3]{}, kwB2[3]{}, kwA2[3]{};
    KwSt kwIn[2]{}, kwWet[2]{};

    void resetFxState()
    {
        fltSt = {};
        fltCoef = {};
        drvPrev[0] = drvPrev[1] = drvLp[0] = drvLp[1] = drvHp[0] = drvHp[1] = 0;
        crushHoldL = crushHoldR = 0;
        crushLeft = 0;
        ringPhase = chorPhase = phPhase = tremPhase = 0;
        phFbL = phFbR = 0;
        std::memset(phApX, 0, sizeof(phApX));
        std::memset(phApY, 0, sizeof(phApY));
        if (!delayL.empty())
        {
            std::fill(delayL.begin(), delayL.end(), 0.f);
            std::fill(delayR.begin(), delayR.end(), 0.f);
        }
        if (!chorL.empty())
        {
            std::fill(chorL.begin(), chorL.end(), 0.f);
            std::fill(chorR.begin(), chorR.end(), 0.f);
        }
        dlyWrite = chorWrite = 0;
        dlyTime = 0;
        dlyLpL = dlyLpR = 0;
        std::memset(combL, 0, sizeof(combL));
        std::memset(combR, 0, sizeof(combR));
        std::memset(apL, 0, sizeof(apL));
        std::memset(apR, 0, sizeof(apR));
        std::memset(combIdx, 0, sizeof(combIdx));
        std::memset(apIdx, 0, sizeof(apIdx));
        rvLpL = rvLpR = 0;
        compEnv = 0;
        kwIn[0] = kwIn[1] = kwWet[0] = kwWet[1] = {};
    }

    void setupKWeight()
    {
        // Prototype at 48 kHz (ITU-R BS.1770-4). Close enough at other rates for matching.
        const double s = 48000.0 / sr;
        // We just use the 48k coefficients; at 44.1/96 the match is still relative and tight.
        (void)s;
        kwB1[0] = 1.53512485958697f;
        kwB1[1] = -2.69169618940638f;
        kwB1[2] = 1.19839281085285f;
        kwA1[0] = 1;
        kwA1[1] = -1.69065929318241f;
        kwA1[2] = 0.73248077421585f;
        kwB2[0] = 1;
        kwB2[1] = -2;
        kwB2[2] = 1;
        kwA2[0] = 1;
        kwA2[1] = -1.99004745483398f;
        kwA2[2] = 0.99007225036621f;
        if (std::abs(sr - 48000) > 1)
        {
            // Mild fallback: a 38 Hz HPF + 1.5 kHz high shelf approximation via one-poles at this sr.
            // Keep 48k coeffs; relative matching still holds for a single session's sample rate.
        }
    }

    float kWeight(float l, float r, KwSt st[2]) noexcept
    {
        auto go = [&](KwSt &s, float x) -> float {
            const float p = kwB1[0] * x + kwB1[1] * s.z1 + kwB1[2] * s.z2 - kwA1[1] * s.y1 - kwA1[2] * s.y2;
            s.z2 = s.z1;
            s.z1 = x;
            s.y2 = s.y1;
            s.y1 = p;
            const float q = kwB2[0] * p + kwB2[1] * s.r1 + kwB2[2] * s.r2 - kwA2[1] * s.q1 - kwA2[2] * s.q2;
            s.r2 = s.r1;
            s.r1 = p;
            s.q2 = s.q1;
            s.q1 = q;
            return q;
        };
        const float a = go(st[0], l);
        const float b = go(st[1], r);
        return std::sqrt(0.5f * (a * a + b * b) + 1.0e-16f);
    }

    void tickSmooth() noexcept
    {
        for (int i = 0; i < Count; ++i)
        {
            // Locks and the randomize trigger snap. Everything audible is ramped so a
            // roll cannot introduce a click (choice params are interpolated and decoded
            // at use, or blended — see processFilter).
            if (isLockParam(i) || i == Randomize || i == ReduceMotion)
                sm.v[(size_t)i] = cur.v[(size_t)i];
            else
                sm.v[(size_t)i] += smooth * (cur.v[(size_t)i] - sm.v[(size_t)i]);
        }
        sm.order = cur.order;
    }

    void updateCoeffs() noexcept
    {
        const float hz = clampf(sm[FltCut], 20.f, float(sr * 0.45));
        const float q = clampf(sm[FltRes], 0.3f, 12.f);
        makeSvf(fltCoef, hz, q, clampi((int)std::round(sm[FltMode]), 0, 2));
    }

    void makeSvf(SvfCoef &c, float hz, float q, int mode) noexcept
    {
        const float g = std::tan(float(kPiD * (double)hz / sr));
        const float k = 1.f / q;
        c.g = g;
        c.k = k;
        c.a1 = 1.f / (1.f + g * (g + k));
        c.mode = mode;
        c.on = true;
    }

    void svf(std::array<FiltSt, 2> &st, const SvfCoef &c, float &l, float &r) noexcept
    {
        if (!c.on)
            return;
        const float g = c.g, k = c.k, a1 = c.a1;
        const int mode = c.mode;
        auto go = [&](FiltSt &s, float x) -> float {
            const float v1 = (s.ic1 + g * (x - s.ic2)) * a1;
            const float v2 = s.ic2 + g * v1;
            s.ic1 = 2.f * v1 - s.ic1;
            s.ic2 = 2.f * v2 - s.ic2;
            if (mode == 2)
                return k * v1; // band
            if (mode == 1)
                return x - k * v1 - v2; // high
            return v2;                  // low
        };
        l = go(st[0], l);
        r = go(st[1], r);
    }

    static float readDelay(const std::vector<float> &buf, int cap, int write, float samples) noexcept
    {
        if (cap < 8)
            return 0;
        samples = clampf(samples, 1.f, float(cap - 4));
        float idx = float(write) - samples;
        while (idx < 0)
            idx += cap;
        const int i0 = int(idx) % cap;
        const int i1 = (i0 + 1) % cap;
        const float f = idx - float(int(idx));
        return buf[(size_t)i0] + f * (buf[(size_t)i1] - buf[(size_t)i0]);
    }

    void processFx(int fx, float &l, float &r) noexcept
    {
        switch ((Fx)fx)
        {
        case Fx::Filter:
            processFilter(l, r);
            break;
        case Fx::Drive:
            processDrive(l, r);
            break;
        case Fx::Crush:
            processCrush(l, r);
            break;
        case Fx::Ring:
            processRing(l, r);
            break;
        case Fx::Chorus:
            processChorus(l, r);
            break;
        case Fx::Phaser:
            processPhaser(l, r);
            break;
        case Fx::Delay:
            processDelay(l, r);
            break;
        case Fx::Reverb:
            processReverb(l, r);
            break;
        case Fx::Comp:
            processComp(l, r);
            break;
        case Fx::Tremolo:
            processTremolo(l, r);
            break;
        case Fx::Width:
            processWidth(l, r);
            break;
        default:
            break;
        }
    }

    void processFilter(float &l, float &r) noexcept
    {
        const float modeF = clampf(sm[FltMode], 0, 2);
        const int mode = clampi((int)std::round(cur[FltMode]), 0, 2);
        // Always run the target mode's coefficients (updated at block rate from sm cutoff/res).
        // If the mode is mid-ramp, run a second SVF path and crossfade — used only briefly.
        float a = l, b = r;
        svf(fltSt, fltCoef, a, b);
        const float frac = std::abs(modeF - (float)mode);
        if (frac > 0.02f)
        {
            // Crossfade dry-ish during a mode change so the hand-off is not a pop.
            // The destination mode already sits in `a,b`. Blend with the dry input
            // weighted by how far the choice still has to travel (max 0.5 at the midpoint).
            const float w = 1.f - 2.f * std::min(frac, 0.5f);
            a = l + w * (a - l);
            b = r + w * (b - r);
        }
        const float m = sm[FltMix];
        l = l + m * (a - l);
        r = r + m * (b - r);
    }

    void processDrive(float &l, float &r) noexcept
    {
        const float amt = sm[DrvAmt];
        const float gain = dbToGain(amt * 24.f); // 0..24 dB
        const float tone = sm[DrvTone];
        // Tone: 0 = darker (LP ~ 2 kHz), 1 = brighter (HP tilt).
        const float lpC = onePole(1.0 / (2.0 * kPiD * (800.0 + tone * 9000.0)), sr);
        const float hpC = onePole(1.0 / (2.0 * kPiD * (80.0 + (1.0 - tone) * 400.0)), sr);
        auto go = [&](int c, float x) {
            const float driven = x * gain;
            float y = adaaTanh(driven, drvPrev[c]);
            drvPrev[c] = driven;
            drvLp[c] += lpC * (y - drvLp[c]);
            drvHp[c] += hpC * (y - drvHp[c]);
            const float dark = drvLp[c];
            const float bright = y - drvHp[c];
            return dark * (1.f - tone) + (y * 0.45f + bright * 0.55f) * tone;
        };
        float a = go(0, l), b = go(1, r);
        // Compensate the 24 dB gain roughly so drive is colour, not just loudness.
        const float comp = dbToGain(-amt * 12.f);
        a *= comp;
        b *= comp;
        const float m = sm[DrvMix];
        l = l + m * (a - l);
        r = r + m * (b - r);
    }

    void processCrush(float &l, float &r) noexcept
    {
        const int bits = clampi((int)std::round(sm[CrshBits]), 1, 16);
        const int holdN = clampi((int)std::round(sm[CrshRate]), 1, 64);
        if (--crushLeft <= 0)
        {
            crushLeft = holdN;
            const float levels = float((1 << bits) - 1);
            const float half = levels * 0.5f;
            auto q = [&](float x) { return std::round(clampf(x, -1, 1) * half) / std::max(half, 1.f); };
            crushHoldL = q(l);
            crushHoldR = q(r);
        }
        const float m = sm[CrshMix];
        l = l + m * (crushHoldL - l);
        r = r + m * (crushHoldR - r);
    }

    void processRing(float &l, float &r) noexcept
    {
        const float hz = clampf(sm[RingFreq], 1.f, float(sr * 0.45));
        ringPhase += 2.0 * kPiD * (double)hz / sr;
        if (ringPhase > 2.0 * kPiD)
            ringPhase -= 2.0 * kPiD * std::floor(ringPhase / (2.0 * kPiD));
        const float s = float(std::sin(ringPhase));
        const float m = sm[RingMix];
        l = l * (1.f - m) + (l * s) * m;
        r = r * (1.f - m) + (r * s) * m;
    }

    void processChorus(float &l, float &r) noexcept
    {
        if (chorCap < 16)
            return;
        chorWrite = (chorWrite + 1) % chorCap;
        chorL[(size_t)chorWrite] = l;
        chorR[(size_t)chorWrite] = r;
        const float rate = clampf(sm[ChorRate], 0.01f, 20.f);
        chorPhase += 2.0 * kPiD * (double)rate / sr;
        if (chorPhase > 2.0 * kPiD)
            chorPhase -= 2.0 * kPiD;
        const float depth = sm[ChorDepth];
        const float base = float(0.012 * sr); // 12 ms
        const float span = float(0.010 * sr) * depth;
        const float d1 = base + span * (0.5f + 0.5f * float(std::sin(chorPhase)));
        const float d2 = base * 1.47f + span * 0.85f * (0.5f + 0.5f * float(std::sin(chorPhase + 2.094)));
        const float wL = 0.5f * (readDelay(chorL, chorCap, chorWrite, d1) +
                                 readDelay(chorL, chorCap, chorWrite, d2));
        const float wR = 0.5f * (readDelay(chorR, chorCap, chorWrite, d2) +
                                 readDelay(chorR, chorCap, chorWrite, d1 * 1.07f));
        const float m = sm[ChorMix];
        l = l + m * (wL - l);
        r = r + m * (wR - r);
    }

    void processPhaser(float &l, float &r) noexcept
    {
        const float rate = clampf(sm[PhasRate], 0.01f, 20.f);
        phPhase += 2.0 * kPiD * (double)rate / sr;
        if (phPhase > 2.0 * kPiD)
            phPhase -= 2.0 * kPiD;
        const float depth = sm[PhasDepth];
        const float fc = 220.f + (1650.f * depth) * (0.5f + 0.5f * float(std::sin(phPhase)));
        const float ap =
            (std::tan(float(kPiD * std::min(fc, float(sr * 0.2)) / sr)) - 1.f) /
            (std::tan(float(kPiD * std::min(fc, float(sr * 0.2)) / sr)) + 1.f);
        const float fb = sm[PhasFb];
        auto go = [&](int c, float x, float &fbs) {
            float u = x + fbs * fb;
            for (int j = 0; j < 6; ++j)
            {
                float y = ap * u + phApX[c][j] - ap * phApY[c][j];
                phApX[c][j] = u;
                phApY[c][j] = y;
                u = y;
            }
            fbs = u;
            return u;
        };
        float a = go(0, l, phFbL), b = go(1, r, phFbR);
        const float m = sm[PhasMix];
        l = l + m * (a - l);
        r = r + m * (b - r);
    }

    void processDelay(float &l, float &r) noexcept
    {
        if (delayCap < 64)
            return;
        const int sync = clampi((int)std::round(sm[DlaySync]), 0, syncCount - 1);
        const float want = float(syncBeats[sync] * 60.0 / (double)tempo * sr);
        if (dlyTime < 1.f)
            dlyTime = want;
        else
            dlyTime += onePole(0.18, sr) * (want - dlyTime);
        dlyWrite = (dlyWrite + 1) % delayCap;
        const float fb = clampf(sm[DlayFb], 0, 0.95f);
        const float oL = readDelay(delayL, delayCap, dlyWrite, dlyTime);
        const float oR = readDelay(delayR, delayCap, dlyWrite, dlyTime);
        // Light damping in the feedback path so repeats stay level-safe.
        dlyLpL += 0.25f * (oL - dlyLpL);
        dlyLpR += 0.25f * (oR - dlyLpR);
        delayL[(size_t)dlyWrite] = l + fb * dlyLpR; // ping-pong
        delayR[(size_t)dlyWrite] = r + fb * dlyLpL;
        const float m = sm[DlayMix];
        l = l * (1.f - m) + oL * m;
        r = r * (1.f - m) + oR * m;
    }

    void processReverb(float &l, float &r) noexcept
    {
        const float size = sm[RvrbSize];
        const float damp = sm[RvrbDamp];
        const float fb = 0.55f + 0.42f * size; // 0.55..0.97; length is fixed so a roll cannot click
        const float lpC = 0.12f + 0.75f * damp;
        float accL = 0, accR = 0;
        const float inL = l * 0.25f, inR = r * 0.25f;
        for (int i = 0; i < 4; ++i)
        {
            const int nn = kCombN[i];
            combIdx[i] = (combIdx[i] + 1) % nn;
            const int j = combIdx[i];
            float &bL = combL[i][j];
            float &bR = combR[i][j];
            const float oL = bL, oR = bR;
            bL = inL + oL * fb;
            bR = inR + oR * fb;
            accL += oL;
            accR += oR;
        }
        accL *= 0.25f;
        accR *= 0.25f;
        // Two all-pass diffusers.
        for (int i = 0; i < 2; ++i)
        {
            const int nn = kApN[i];
            apIdx[i] = (apIdx[i] + 1) % nn;
            const int j = apIdx[i];
            const float g = 0.6f;
            float &aL = apL[i][j];
            float &aR = apR[i][j];
            const float tL = accL + g * aL;
            const float tR = accR + g * aR;
            const float oL = aL - g * tL;
            const float oR = aR - g * tR;
            aL = tL;
            aR = tR;
            accL = oL;
            accR = oR;
        }
        rvLpL += lpC * (accL - rvLpL);
        rvLpR += lpC * (accR - rvLpR);
        const float m = sm[RvrbMix];
        l = l * (1.f - m) + rvLpL * m;
        r = r * (1.f - m) + rvLpR * m;
    }

    void processComp(float &l, float &r) noexcept
    {
        const float th = sm[CompThres];
        const float ratio = std::max(1.f, sm[CompRatio]);
        const float atk = onePole(sm[CompAtk] * 0.001, sr);
        const float rel = onePole(sm[CompRel] * 0.001, sr);
        const float peak = std::max(std::abs(l), std::abs(r));
        const float coef = peak > compEnv ? atk : rel;
        compEnv += coef * (peak - compEnv);
        if (compEnv < 1.0e-12f)
            compEnv = 0;
        const float inDb = gainToDb(std::max(compEnv, 1.0e-12f));
        float gr = 0;
        if (inDb > th)
            gr = (inDb - th) * (1.f - 1.f / ratio);
        compGrDb = gr;
        const float g = dbToGain(-gr);
        l *= g;
        r *= g;
    }

    void processTremolo(float &l, float &r) noexcept
    {
        const int sync = clampi((int)std::round(sm[TremSync]), 0, syncCount - 1);
        const double beats = syncBeats[sync];
        const double hz = (double)tempo / (60.0 * beats);
        if (hostPlaying && hostPpq > 0)
        {
            const double cyc = hostPpq / beats;
            tremPhase = 2.0 * kPiD * (cyc - std::floor(cyc));
        }
        else
        {
            tremPhase += 2.0 * kPiD * hz / sr;
            if (tremPhase > 2.0 * kPiD)
                tremPhase -= 2.0 * kPiD * std::floor(tremPhase / (2.0 * kPiD));
        }
        const int shape = clampi((int)std::round(sm[TremShape]), 0, 2);
        float wave = 0;
        const float ph = float(tremPhase);
        if (shape == 1)
        {
            // Triangle 0..1..0 over a cycle, starting at 1 (phase 0 = peak).
            const float t = float(tremPhase / (2.0 * kPiD));
            wave = 1.f - 4.f * std::abs(t - 0.5f + (t < 0.25f || t >= 0.75f ? 0.f : 0.f));
            // Standard triangle: 0 at phase 0 is less useful; peak at 0:
            wave = t < 0.5f ? (1.f - 4.f * t) : (4.f * t - 3.f);
            // t=0 -> 1, t=0.25 -> 0, t=0.5 -> -1, t=0.75 -> 0, t=1 -> 1. That's a cosine-like triangle.
            wave = 1.f - 4.f * std::abs(t - std::floor(t + 0.5f)); // 1 at 0, -1 at 0.5
        }
        else if (shape == 2)
            wave = std::sin(ph) >= 0.f ? 1.f : -1.f;
        else
            wave = float(std::sin(tremPhase));
        const float depth = sm[TremDepth];
        const float g = 1.f - depth * (0.5f + 0.5f * wave);
        l *= g;
        r *= g;
    }

    void processWidth(float &l, float &r) noexcept
    {
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r);
        const float w = sm[WdthAmt];
        l = mid + side * w;
        r = mid - side * w;
    }
};

} // namespace rigged
