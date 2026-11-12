#pragma once
#include "Parameters.h"
#include <array>
#include <atomic>
#include <cstring>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <xmmintrin.h>
#define STOKE_HAS_SSE 1
#endif

namespace stoke
{

struct FlushDenormals
{
#if defined(STOKE_HAS_SSE)
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

// Harmonics-only soft clip: tanh with a little even-order asymmetry, linear term removed
// so we add presence harmonics without stacking extra EQ on the fundamental.
inline float shape(float x, float drive, float even) noexcept
{
    const float u = x + even * 0.22f * x * std::fabs(x);
    return std::tanh(drive * u) - drive * x;
}

struct Channel
{
    Svf boost, mud, harsh[kHarshBands], midBp;
    Oversampler os;
    Hp1 dc, harmHp, harmHp2;
    void reset() noexcept
    {
        boost.reset();
        mud.reset();
        midBp.reset();
        for (auto &h : harsh)
            h.reset();
        os.reset();
        dc.reset();
        harmHp.reset();
        harmHp2.reset();
    }
};

class Engine
{
  public:
    static constexpr int kScope = 4096;
    static constexpr int latency = 0;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = std::max(sampleRate, 8000.0);
        midDet.set(0.008, 0.070, sr);
        slowMid.set(0.25, 0.40, sr);
        mudDet.set(0.020, 0.160, sr);
        wideDet.set(0.008, 0.090, sr);
        inEnv.set(0.040, 0.220, sr);
        wetEnv.set(0.040, 0.220, sr);
        for (int i = 0; i < kHarshBands; ++i)
            harshDet[i].set(0.006, 0.180, sr);
        matchCoef = onePole(0.160, sr);
        matchFast = onePole(0.040, sr);
        limRel = onePole(0.180, sr);
        bypassSmooth = onePole(0.010, sr);
        smooth = onePole(0.018, sr);
        coefSmooth = onePole(0.012, sr);
        for (auto &c : ch)
        {
            c.dc.set(18.f, (float)sr);
            c.harmHp.set(400.f, (float)sr);
            c.harmHp2.set(400.f, (float)sr);
        }
        harmHpFc = 400.f;
        reset();
    }

    void reset() noexcept
    {
        for (auto &c : ch)
            c.reset();
        detectMid.reset();
        detectMud.reset();
        detectWide.reset();
        for (auto &s : detectHarsh)
            s.reset();
        midDet.reset();
        slowMid.reset();
        mudDet.reset();
        wideDet.reset();
        inEnv.reset();
        wetEnv.reset();
        for (auto &e : harshDet)
            e.reset();
        makeup = 1;
        limiter = 1;
        bypassGain = 1;
        meter = 0;
        reductionDb = 0;
        makeupDb = 0;
        scope.fill(0);
        scopeIn.fill(0);
        scopeW.store(0);
        boostG = mudG = 1;
        glowMix = 0;
        for (auto &g : harshG)
            g = 1;
        // snap smoothed params to current raw on the next set()/snap()
    }

    void set(const Settings &s) noexcept { raw = s; }

    void snap() noexcept
    {
        sm = raw;
        // Force exact defaults through the smoother so FOCUS = 0 is a true wire immediately.
        bypassGain = raw[Bypass] >= 0.5f ? 0.f : 1.f;
        makeup = 1;
        limiter = 1;
        updateCoefs(true);
    }

    void process(float *left, float *right, int n) noexcept
    {
        if (left == nullptr || n <= 0)
            return;
        FlushDenormals denormGuard;
        const bool stereo = right != nullptr;
        float peak = 0;

        for (int i = 0; i < n; ++i)
        {
            for (int p = 0; p < Count; ++p)
                sm.v[(size_t)p] += smooth * (raw.v[(size_t)p] - sm.v[(size_t)p]);

            const float focus = clampf(sm[Focus], 0.f, 1.f);
            const float glow = clampf(sm[Glow], 0.f, 1.f);
            const float smo = clampf(sm[Smooth], 0.f, 1.f);
            const float midAmt = clampf(sm[MidOnly], 0.f, 1.f);
            const float mix = clampf(sm[Mix], 0.f, 1.f);
            const float outG = dbToGain(sm[Output]);
            const float gmOn = clampf(sm[GainMatch], 0.f, 1.f);
            const float limOn = clampf(sm[LimiterOn], 0.f, 1.f);
            const float centre = centreHz(sm[Centre]);

            updateCoefs(false);

            const float inL = clean(left[i]);
            const float inR = stereo ? clean(right[i]) : inL;
            const float midIn = 0.5f * (inL + inR);
            const float sideIn = 0.5f * (inL - inR);

            tickDetectors(midIn, focus, smo, centre);

            glowDrive = 1.15f + glow * 3.6f;
            glowEven = 0.12f + glow * 0.55f;
            // Zero at FOCUS = 0 (true wire). Default ≈ 0.094; GLOW = 1 at FOCUS 0.7 ≈ 0.49.
            glowMix = glow * focus * 0.70f;

            const float hpFc = std::max(550.f, centre * 0.50f);
            if (std::abs(hpFc - harmHpFc) > 1.f)
            {
                harmHpFc = hpFc;
                ch[0].harmHp.set(hpFc, (float)sr);
                ch[1].harmHp.set(hpFc, (float)sr);
                ch[0].harmHp2.set(hpFc, (float)sr);
                ch[1].harmHp2.set(hpFc, (float)sr);
            }

            // Mid-only blends the input toward the mid, then the original side is put
            // back after processing so the sides stay untouched at midAmt = 1.
            const float xL = inL + midAmt * (midIn - inL);
            const float xR = inR + midAmt * (midIn - inR);
            float yL = processChannel(ch[0], xL);
            float yR = stereo ? processChannel(ch[1], xR) : yL;
            yL += midAmt * sideIn;
            yR -= midAmt * sideIn;

            // Gain match: same loudness as the input, silence stays makeup = 1.
            const float inAbs = std::sqrt(0.5f * (inL * inL + inR * inR));
            const float wetAbs = std::sqrt(0.5f * (yL * yL + yR * yR));
            inEnv.tick(inAbs);
            wetEnv.tick(wetAbs);
            float target = 1.f;
            if (inEnv.y > 1.0e-6f && wetEnv.y > 1.0e-6f)
                target = clampf(inEnv.y / wetEnv.y, 0.25f, 4.f);
            else if (inEnv.y < 1.0e-7f && wetEnv.y < 1.0e-7f)
                target = 1.f;
            const float mcoef = std::abs(target - makeup) > 0.12f ? matchFast : matchCoef;
            makeup += mcoef * (target - makeup);
            const float applied = 1.f + gmOn * (makeup - 1.f);
            yL *= applied;
            yR *= applied;

            // Mix, then output gain. Mix = 0 is a true wire (before output).
            yL = inL + mix * (yL - inL);
            yR = inR + mix * (yR - inR);
            yL *= outG;
            yR *= outG;

            // Safety limiter. A true wire (FOCUS at 0, unity output) is never limited,
            // so Amount 0 stays bit-transparent even with LIMIT on.
            const bool wire = focus < 1.0e-4f && glowMix < 1.0e-6f && std::abs(outG - 1.f) < 1.0e-5f;
            if (limOn > 0.5f && !wire)
            {
                const float pk = std::max(std::abs(yL), std::abs(yR));
                const float need = pk > kCeiling ? kCeiling / std::max(pk, 1.0e-12f) : 1.f;
                limiter = need < limiter ? need : limiter + limRel * (need - limiter);
                yL = clampf(yL * limiter, -kCeiling, kCeiling);
                yR = clampf(yR * limiter, -kCeiling, kCeiling);
                reductionDb = -gainToDb(std::max(limiter, 1.0e-8f));
            }
            else
            {
                limiter = 1.f;
                reductionDb = 0.f;
            }

            const float bypassTarget = raw[Bypass] >= 0.5f ? 0.f : 1.f;
            bypassGain += bypassSmooth * (bypassTarget - bypassGain);
            yL = inL + bypassGain * (yL - inL);
            yR = inR + bypassGain * (yR - inR);

            yL = clean(yL);
            yR = clean(yR);
            left[i] = yL;
            if (stereo)
                right[i] = yR;

            const float mOut = 0.5f * (yL + yR);
            const int w = scopeW.load(std::memory_order_relaxed);
            scopeIn[(size_t)w] = midIn;
            scope[(size_t)w] = mOut;
            scopeW.store((w + 1) & (kScope - 1), std::memory_order_relaxed);
            peak = std::max(peak, std::max(std::abs(yL), std::abs(yR)));
        }

        meter = peak;
        makeupDb = gainToDb(std::max(makeup, 1.0e-8f));
    }

    // Meters and scope rings for the live analyser (mono mix of input and output).
    float meter = 0, reductionDb = 0, makeupDb = 0;
    float focusView = 0.5f, centreView = kCentreDefault, widthView = kWidthDefault;
    std::array<float, kScope> scope{}, scopeIn{};
    std::atomic<int> scopeW{ 0 };

    // Magnitude of the FOCUS EQ chain (boost bell, auto 330 Hz dip, SMOOTH notches) at hz, in dB.
    // Uses the live gains so the curve follows automatic taming as audio runs.
    float curveMagDb(float hz) const noexcept
    {
        hz = std::max(hz, 1.f);
        const float fc = std::max(centreView, 1.f);
        const float qBoost = widthToQ(clampf(widthView, kWidthMin, kWidthMax));
        const float gB = boostG, gM = mudG;
        float gH[kHarshBands];
        for (int i = 0; i < kHarshBands; ++i)
            gH[i] = harshG[i];

        auto applyPeak = [&](float &re, float &im, float f0, float Q, float G) {
            const float numIm = hz * f0 / std::max(Q, 0.05f);
            const float denRe = f0 * f0 - hz * hz;
            const float denIm = numIm;
            const float n2 = denRe * denRe + denIm * denIm + 1.0e-30f;
            const float hRe = numIm * denIm / n2;
            const float hIm = numIm * denRe / n2;
            const float fRe = 1.f + (G - 1.f) * hRe;
            const float fIm = (G - 1.f) * hIm;
            const float nRe = re * fRe - im * fIm;
            const float nIm = re * fIm + im * fRe;
            re = nRe;
            im = nIm;
        };

        float re = 1.f, im = 0.f;
        applyPeak(re, im, fc, qBoost, gB);
        applyPeak(re, im, kMudHz, 0.95f, gM);
        for (int i = 0; i < kHarshBands; ++i)
            applyPeak(re, im, kHarshHz[i], 5.5f, gH[i]);
        const float mag = std::sqrt(re * re + im * im);
        return 20.f * std::log10(std::max(mag, 1.0e-12f));
    }

  private:
    void updateCoefs(bool snapNow) noexcept
    {
        const float centre = centreHz(sm[Centre]);
        const float width = clampf(sm[Width], kWidthMin, kWidthMax);
        const float q = widthToQ(width);
        const auto apply = [&](SvfCoef &dst, const SvfCoef &src) {
            if (snapNow)
            {
                dst = src;
                return;
            }
            dst.a1 += coefSmooth * (src.a1 - dst.a1);
            dst.a2 += coefSmooth * (src.a2 - dst.a2);
            dst.a3 += coefSmooth * (src.a3 - dst.a3);
            dst.k += coefSmooth * (src.k - dst.k);
        };
        apply(cBoost, SvfCoef::make(centre, q, (float)sr));
        apply(cMidBp, SvfCoef::make(centre, q, (float)sr));
        apply(cMud, SvfCoef::make(kMudHz, 0.95f, (float)sr));
        apply(cWide, SvfCoef::make(3200.f, 0.55f, (float)sr));
        apply(cDetMid, SvfCoef::make(centre, q, (float)sr));
        apply(cDetMud, SvfCoef::make(kMudHz, 0.95f, (float)sr));
        for (int i = 0; i < kHarshBands; ++i)
        {
            const float fc = std::min(kHarshHz[i], 0.42f * (float)sr);
            apply(cHarsh[i], SvfCoef::make(fc, 5.5f, (float)sr));
            apply(cDetHarsh[i], SvfCoef::make(fc, 5.5f, (float)sr));
        }
        focusView = sm[Focus];
        centreView = centre;
        widthView = width;
    }

    void tickDetectors(float midIn, float focus, float smo, float centre) noexcept
    {
        float lp, bp, hp;
        detectMid.tick(midIn, cDetMid.a1, cDetMid.a2, cDetMid.a3, cDetMid.k, lp, bp, hp);
        const float midN = std::fabs(bp * cDetMid.k);
        midDet.tick(midN);
        slowMid.tick(midN);

        detectMud.tick(midIn, cDetMud.a1, cDetMud.a2, cDetMud.a3, cDetMud.k, lp, bp, hp);
        mudDet.tick(std::fabs(bp * cDetMud.k));

        detectWide.tick(midIn, cWide.a1, cWide.a2, cWide.a3, cWide.k, lp, bp, hp);
        wideDet.tick(std::fabs(bp * cWide.k));

        for (int i = 0; i < kHarshBands; ++i)
        {
            detectHarsh[i].tick(midIn, cDetHarsh[i].a1, cDetHarsh[i].a2, cDetHarsh[i].a3,
                                cDetHarsh[i].k, lp, bp, hp);
            harshDet[i].tick(std::fabs(bp * cDetHarsh[i].k));
        }

        // Static + dynamic mid boost. Peaks above the slow average get a little less lift
        // (that's SMOOTH), so a loud sibilant or a resonant synth note doesn't get harsh.
        const float boostDb = focusBoostDb(focus);
        float reduction = 0.f;
        if (slowMid.y > 1.0e-8f && midDet.y > slowMid.y)
        {
            const float excessDb = gainToDb(midDet.y / std::max(slowMid.y, 1.0e-12f));
            reduction = smo * 0.55f * std::max(0.f, excessDb - 2.5f);
        }
        const float boostEff = std::max(boostDb * 0.30f, boostDb - reduction);
        boostG = dbToGain(boostEff);

        // De-mud: a shallow dynamic cut around 330 Hz, only when the mid band we're lifting
        // sits well above that region (so Bass Mids can still boost 700 Hz).
        float mudCut = 0.f;
        if (focus > 0.01f && centre > 900.f)
        {
            const float ratio =
                mudDet.y / std::max(midDet.y, 1.0e-8f);
            const float mudFactor = clampf(0.35f + 0.65f * clampf(ratio * 2.f, 0.f, 1.f), 0.f, 1.f);
            mudCut = 3.0f * smo * focus * mudFactor;
        }
        mudG = dbToGain(-mudCut);

        // De-harsh: three narrow downward bands in 2-5 kHz. A band cuts only when it is
        // louder than the wider 2-6 kHz average (a resonance), scaled by SMOOTH and FOCUS.
        for (int i = 0; i < kHarshBands; ++i)
        {
            float cut = 0.f;
            if (focus > 0.01f && smo > 0.01f)
            {
                const float wide = std::max(wideDet.y, 1.0e-8f);
                const float excess = harshDet[i].y / wide;
                cut = smo * focus * 5.5f * std::max(0.f, excess - 1.25f);
                cut = clampf(cut, 0.f, 8.f);
            }
            harshG[i] = dbToGain(-cut);
        }
    }

    float processChannel(Channel &c, float x) noexcept
    {
        const float focus = clampf(sm[Focus], 0.f, 1.f);
        float lp, bp, hp;

        // Always tick every filter so engaging FOCUS is click-free, but at FOCUS≈0 and
        // GLOW≈0 the algebra below adds exactly zero, so the return is bit-identical to x
        // once the smoothers have settled at zero.
        c.boost.tick(x, cBoost.a1, cBoost.a2, cBoost.a3, cBoost.k, lp, bp, hp);
        float y = x + (boostG - 1.f) * (bp * cBoost.k);

        c.mud.tick(y, cMud.a1, cMud.a2, cMud.a3, cMud.k, lp, bp, hp);
        y = y + (mudG - 1.f) * (bp * cMud.k);

        for (int i = 0; i < kHarshBands; ++i)
        {
            c.harsh[i].tick(y, cHarsh[i].a1, cHarsh[i].a2, cHarsh[i].a3, cHarsh[i].k, lp, bp, hp);
            y = y + (harshG[i] - 1.f) * (bp * cHarsh[i].k);
        }

        // Harmonic exciter only on the mid band, 8x oversampled, harmonics-only, high-passed.
        c.midBp.tick(y, cMidBp.a1, cMidBp.a2, cMidBp.a3, cMidBp.k, lp, bp, hp);
        const float mid = bp * cMidBp.k;
        float hi[8];
        c.os.up(mid, hi);
        for (int k = 0; k < 8; ++k)
            hi[k] = shape(hi[k], glowDrive, glowEven);
        float h = c.os.down(hi);
        h = c.dc.tick(h);
        h = c.harmHp.tick(h);
        h = c.harmHp2.tick(h);
        y += glowMix * h;

        // Exact wire once FOCUS and GLOW have settled at zero (boostG=mudG=harshG=1, glowMix=0).
        (void)focus;
        return y;
    }

    double sr = 48000.0;
    Settings raw{}, sm{};
    float smooth = 0.001f, coefSmooth = 0.001f, bypassSmooth = 0.01f;
    float matchCoef = 0.001f, matchFast = 0.01f, limRel = 0.001f;
    float makeup = 1, limiter = 1, bypassGain = 1;
    float boostG = 1, mudG = 1, harshG[kHarshBands] = { 1, 1, 1 };
    float glowDrive = 1.15f, glowEven = 0.12f, glowMix = 0;
    float harmHpFc = 400.f;
    Channel ch[2];
    // Shared (linked) detectors.
    Svf detectMid, detectMud, detectWide, detectHarsh[kHarshBands];
    SvfCoef cBoost, cMud, cMidBp, cWide, cHarsh[kHarshBands];
    SvfCoef cDetMid, cDetMud, cDetHarsh[kHarshBands];
    Env midDet, slowMid, mudDet, wideDet, harshDet[kHarshBands], inEnv, wetEnv;
};

} // namespace stoke
