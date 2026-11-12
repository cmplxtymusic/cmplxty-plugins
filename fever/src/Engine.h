#pragma once
#include "Parameters.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define FEVER_HAS_FTZ 1
#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__)
#define FEVER_HAS_DAZ 1
#else
#define FEVER_HAS_DAZ 0
#endif
#else
#define FEVER_HAS_FTZ 0
#define FEVER_HAS_DAZ 0
#endif

namespace fever
{
struct FlushDenormals
{
#if FEVER_HAS_FTZ
    unsigned int saved;
    FlushDenormals() noexcept
    {
        saved = _mm_getcsr();
        unsigned int m = saved | 0x8000;
#if FEVER_HAS_DAZ
        m |= 0x0040;
#endif
        _mm_setcsr(m);
    }
    ~FlushDenormals() noexcept { _mm_setcsr(saved); }
#else
    FlushDenormals() noexcept {}
#endif
};

inline float hermite(const float *b, int cap, int wr, float delay) noexcept
{
    delay = clampf(delay, 4.f, float(cap - 6));
    const float rp = float(wr) - delay;
    const int i = (int)std::floor(rp);
    const float f = rp - (float)i;
    auto at = [b, cap](int k) noexcept {
        int j = k % cap;
        if (j < 0)
            j += cap;
        return b[j];
    };
    // 4-point cubic Hermite — cheap, and with 220 ms grains the partials stay inside ±10 cents.
    const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
    const float c = (y1 - ym1) * 0.5f;
    const float v = y0 - y1;
    const float w = c + v;
    const float a = w + v + (y2 - y0) * 0.5f;
    const float bw = w + a;
    return (((a * f) - bw) * f + c) * f + y0;
}

// Cytomic / Zavalishin TPT SVF, band-pass output.
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float g = 0, a1 = 1, a2 = 0, a3 = 0, k = 1;
    void set(float hz, float q, double sr) noexcept
    {
        hz = clampf(hz, 20.f, float(sr * 0.45));
        q = clampf(q, 0.3f, 40.f);
        g = std::tan(float(kPiD * (double)hz / sr));
        k = 1.f / q;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float bp(float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return v1;
    }
    void clear() noexcept { ic1 = ic2 = 0; }
};

class Engine
{
  public:
    void prepare(double rate, int /*maxBlock*/)
    {
        FlushDenormals fz;
        sr = rate > 100 ? rate : 48000;
        const int gcap = (int)std::max(sr * 2.0, 8192.0);
        const int hcap = (int)std::max(sr * 0.40, 2048.0);
        const int tcap = (int)std::max(sr * 0.20, 1024.0);
        grainL.assign((size_t)gcap, 0.f);
        grainR.assign((size_t)gcap, 0.f);
        grainCap = gcap;
        haloL.assign((size_t)hcap, 0.f);
        haloR.assign((size_t)hcap, 0.f);
        haloCap = hcap;
        for (int c = 0; c < 4; ++c)
        {
            combL[(size_t)c].assign((size_t)tcap, 0.f);
            combR[(size_t)c].assign((size_t)tcap, 0.f);
        }
        for (int c = 0; c < 2; ++c)
        {
            apL[(size_t)c].assign((size_t)tcap, 0.f);
            apR[(size_t)c].assign((size_t)tcap, 0.f);
        }
        tailCap = tcap;
        hilbBufL.assign((size_t)kHilbTaps * 2, 0.f);
        hilbBufR.assign((size_t)kHilbTaps * 2, 0.f);
        setupDelays();
        setupKWeight();
        setupHilbert();
        smooth = onePole(0.020, sr);
        matchSmooth = onePole(0.40, sr);
        matchFast = onePole(0.060, sr);
        limRel = onePole(0.080, sr);
        bypassSmooth = onePole(0.006, sr);
        hpCoeff = onePole(1.0 / (2.0 * kPiD * 300.0), sr);
        coldHp = onePole(1.0 / (2.0 * kPiD * 320.0), sr);
        for (int i = 0; i < Count; ++i)
            cur.v[(size_t)i] = sm.v[(size_t)i] = definitions[(size_t)i].initial;
        reset();
        for (int i = 0; i < kPeaks; ++i)
        {
            peakL[(size_t)i].set(kPeakHz[i], 12.f, sr);
            peakR[(size_t)i].set(kPeakHz[i], 12.f, sr);
        }
    }

    void reset()
    {
        std::fill(grainL.begin(), grainL.end(), 0.f);
        std::fill(grainR.begin(), grainR.end(), 0.f);
        std::fill(haloL.begin(), haloL.end(), 0.f);
        std::fill(haloR.begin(), haloR.end(), 0.f);
        for (int c = 0; c < 4; ++c)
        {
            std::fill(combL[(size_t)c].begin(), combL[(size_t)c].end(), 0.f);
            std::fill(combR[(size_t)c].begin(), combR[(size_t)c].end(), 0.f);
            combIdx[(size_t)c] = 0;
        }
        for (int c = 0; c < 2; ++c)
        {
            std::fill(apL[(size_t)c].begin(), apL[(size_t)c].end(), 0.f);
            std::fill(apR[(size_t)c].begin(), apR[(size_t)c].end(), 0.f);
            apIdx[(size_t)c] = 0;
        }
        for (auto &g : grains)
            g = {};
        grainWrite = haloWrite = 0;
        spawnAcc = 0;
        ratioCursor = 0;
        hpL = hpR = hpZL = hpZR = 0;
        for (int i = 0; i < 4; ++i) coldHpL[i] = coldHpR[i] = 0;
        for (int i = 0; i < kPeaks; ++i)
        {
            peakL[(size_t)i].clear();
            peakR[(size_t)i].clear();
        }
        setupHilbert();
        hilbWrite = 0;
        haloPhaseUp = haloPhaseDn = 0;
        if (!hilbBufL.empty()) {
            std::fill(hilbBufL.begin(), hilbBufL.end(), 0.f);
            std::fill(hilbBufR.begin(), hilbBufR.end(), 0.f);
        }
        limiter = 1;
        makeup = 1;
        inEnv = wetEnv = 1.0e-16f;
        bypassGain = 1;
        reductionDb = makeupDb = meter = 0;
        heartPhase = 0;
        heartPulse = 0;
        effectiveFever = 0.5f;
        loveAmtSm = hateAmtSm = 0;
        kwIn[0] = kwIn[1] = kwWet[0] = kwWet[1] = {};
        rng = 0xC31D4E5Bu;
        activeGrains = 0;
    }

    void set(const Settings &s) { cur = s; }

    void snap()
    {
        sm = cur;
        reset();
        bypassGain = sm[Bypass] >= 0.5f ? 0.f : 1.f;
        effectiveFever = sm[FeverAmt];
        loveAmtSm = loveAmt(effectiveFever);
        hateAmtSm = hateAmt(effectiveFever);
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
        const int rateIdx = clampi((int)std::round(sm[HeartRate]), 0, syncCount - 1);
        const double beats = syncBeats[rateIdx];
        if (sm[Heartbeat] >= 0.5f && hostPlaying)
            heartPhase = (float)(hostPpq / beats - std::floor(hostPpq / beats));
        const double phaseInc = ((double)tempo / 60.0) / beats / sr;

        for (int i = 0; i < n; ++i)
        {
            tickSmooth();

            float l = clean(left[i]);
            float r = clean(mono ? l : right[i]);
            const float dryL = l, dryR = r;

            bypassGain += bypassSmooth * ((sm[Bypass] >= 0.5f ? 0.f : 1.f) - bypassGain);
            if (bypassGain < 1.0e-4f)
            {
                left[i] = dryL;
                if (!mono)
                    right[i] = dryR;
                continue;
            }

            // Heartbeat wobble (cosine, already click-free).
            if (sm[Heartbeat] >= 0.5f)
            {
                heartPhase = (float)(heartPhase + phaseInc);
                if (heartPhase >= 1.f)
                    heartPhase -= (float)std::floor(heartPhase);
                heartPulse = 0.5f + 0.5f * std::cos(float(2.0 * kPiD * (double)heartPhase));
                const float depth = sm[HeartDepth];
                effectiveFever = clampf(sm[FeverAmt] + depth * (heartPulse - 0.5f), 0.f, 1.f);
            }
            else
            {
                heartPulse = 0;
                effectiveFever = sm[FeverAmt];
            }
            const float love = loveAmt(effectiveFever);
            const float hate = hateAmt(effectiveFever);
            loveAmtSm = love;
            hateAmtSm = hate;

            float cL = 0, cR = 0, sL = 0, sR = 0;
            if (love > 1.0e-5f || activeGrains > 0)
                tickLove(l, r, cL, cR, love);
            else
            {
                // Still write the buffer so a sudden move to love has history.
                grainWrite = (grainWrite + 1) % grainCap;
                grainL[(size_t)grainWrite] = l;
                grainR[(size_t)grainWrite] = r;
            }
            if (hate > 1.0e-5f)
                tickHate(l, r, sL, sR);
            else
                decayHateIdle();

            float xL = dryL + love * cL + hate * sL;
            float xR = dryR + love * cR + hate * sR;
            xL = clean(xL);
            xR = clean(xR);

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
                want = clampf(std::sqrt(inEnv / wetEnv), 0.01f, 100.f);
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

            if (hostPlaying)
            {
                hostPpq += (tempo / 60.0) / sr;
            }
        }
    }

    float reductionDb = 0, makeupDb = 0, meter = 0;
    float heartPhase = 0, heartPulse = 0, effectiveFever = 0.5f;
    float loveAmtSm = 0, hateAmtSm = 0;
    int activeGrains = 0;
    Settings sm;

  private:
    struct Grain
    {
        bool on = false;
        double delay = 0, ratio = 1;
        float env = 0, envInc = 0, gain = 0, panL = 1, panR = 1;
    };
    static constexpr int kHilbTaps = 65;
    static constexpr int kHilbLat = 32;
    struct KwSt
    {
        float z1 = 0, z2 = 0, y1 = 0, y2 = 0;
        float r1 = 0, r2 = 0, q1 = 0, q2 = 0;
    };

    Settings cur;
    double sr = 48000;
    float smooth = 0.001f, matchSmooth = 0.001f, matchFast = 0.01f, limRel = 0.001f, bypassSmooth = 0.01f;
    float tempo = 120;
    double hostPpq = 0;
    bool hostPlaying = false;
    float limiter = 1, makeup = 1, inEnv = 1.0e-6f, wetEnv = 1.0e-6f, bypassGain = 1;
    float hpCoeff = 0.01f, coldHp = 0.01f;
    float hpL = 0, hpR = 0, hpZL = 0, hpZR = 0;
    float coldHpL[4]{}, coldHpR[4]{};

    std::vector<float> grainL, grainR, haloL, haloR;
    std::array<std::vector<float>, 4> combL, combR;
    std::array<std::vector<float>, 2> apL, apR;
    int grainCap = 0, haloCap = 0, tailCap = 0;
    int grainWrite = 0, haloWrite = 0;
    int combLen[4]{}, apLen[2]{}, combIdx[4]{}, apIdx[2]{};
    std::array<Grain, kMaxGrains> grains{};
    float spawnAcc = 0;
    int ratioCursor = 0;
    std::array<Svf, kPeaks> peakL{}, peakR{};
    float hilb[kHilbTaps]{};
    std::vector<float> hilbBufL, hilbBufR;
    int hilbWrite = 0;
    double haloPhaseUp = 0, haloPhaseDn = 0;
    KwSt kwIn[2]{}, kwWet[2]{};
    float kwB1[3]{}, kwA1[3]{}, kwB2[3]{}, kwA2[3]{};
    uint32_t rng = 0xC31D4E5Bu;

    float rand01() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return float((rng >> 8) & 0x00FFFFFFu) * (1.f / 16777216.f);
    }

    void tickSmooth() noexcept
    {
        for (int i = 0; i < Count; ++i)
        {
            if (i == Bypass || i == GainMatch || i == LimiterOn || i == Heartbeat || i == HeartRate ||
                i == ReduceMotion)
                sm.v[(size_t)i] = cur.v[(size_t)i];
            else
                sm.v[(size_t)i] += smooth * (cur.v[(size_t)i] - sm.v[(size_t)i]);
        }
    }

    void setupDelays()
    {
        const float combMs[4] = { 29.31f, 37.13f, 43.71f, 53.17f };
        const float apMs[2] = { 7.31f, 11.17f };
        for (int i = 0; i < 4; ++i)
            combLen[i] = clampi((int)std::round(combMs[i] * 0.001 * sr), 16, tailCap - 2);
        for (int i = 0; i < 2; ++i)
            apLen[i] = clampi((int)std::round(apMs[i] * 0.001 * sr), 8, tailCap - 2);
    }

    void spawnGrain(float love, float density, float spread) noexcept
    {
        (void)love;
        int slot = -1;
        for (int i = 0; i < kMaxGrains; ++i)
            if (!grains[(size_t)i].on)
            {
                slot = i;
                break;
            }
        if (slot < 0)
            return;
        auto &g = grains[(size_t)slot];
        const float ratio0 = kRatio[ratioCursor];
        ratioCursor = (ratioCursor + 1) % kRatios;
        const float cents = (spread > 1.0e-5f) ? (rand01() * 2.f - 1.f) * kSpreadCents * spread : 0.f;
        g.ratio = ratio0 * centsToRatio(cents);
        const float glen = float((0.220 + 0.060 * (1.f - density)) * sr); // 220–280 ms, tight partials
        g.env = 0;
        g.envInc = 1.f / std::max(glen, 64.f);
        const float need = glen * std::max(g.ratio, 1.0) + float(0.030 * sr);
        const float extra = float(0.050 * sr) * (0.2f + 0.8f * rand01());
        g.delay = need + extra;
        if (g.delay > float(grainCap - 16))
            g.delay = float(grainCap - 16);
        // At full spread, grains sit near the extremes of the image.
        const float pan = 0.5f + (spread > 1.0e-5f ? (rand01() * 2.f - 1.f) * 0.5f * spread : 0.f);
        const float a = clampf(pan, 0.f, 1.f);
        g.panL = std::sqrt(1.f - a);
        g.panR = std::sqrt(a);
        // Slightly stronger on the fifths and octaves we measure.
        const float tip = (ratio0 == 3.f) ? 0.85f : 1.15f;
        g.gain = 0.28f * tip;
        g.on = true;
    }

    void tickLove(float l, float r, float &oL, float &oR, float love) noexcept
    {
        grainWrite = (grainWrite + 1) % grainCap;
        grainL[(size_t)grainWrite] = l;
        grainR[(size_t)grainWrite] = r;

        const float density = sm[SideA];
        const float spread = sm[SideB];
        // 4–20 chords/sec, each chord is all five ratios (20–100 grain starts/sec).
        const float rate = (4.f + density * 16.f) * (float)kRatios;
        if (love > 1.0e-4f)
        {
            spawnAcc += rate / float(sr);
            while (spawnAcc >= 1.f)
            {
                spawnAcc -= 1.f;
                spawnGrain(love, density, spread);
            }
        }

        oL = oR = 0;
        int live = 0;
        for (int i = 0; i < kMaxGrains; ++i)
        {
            auto &g = grains[(size_t)i];
            if (!g.on)
                continue;
            g.delay -= (g.ratio - 1.0);
            g.env += g.envInc;
            if (g.env >= 1.f || g.delay < 10.0 || g.delay > double(grainCap - 16))
            {
                g.on = false;
                continue;
            }
            ++live;
            const float w = 0.5f - 0.5f * std::cos(float(2.0 * kPiD * (double)g.env));
            const float sL = hermite(grainL.data(), grainCap, grainWrite, (float)g.delay);
            const float sR = hermite(grainR.data(), grainCap, grainWrite, (float)g.delay);
            oL += sL * w * g.gain * g.panL;
            oR += sR * w * g.gain * g.panR;
        }
        activeGrains = live;
        oL = clean(oL);
        oR = clean(oR);
    }

    float tickAllpass(std::vector<float> &buf, int idx, int len, float x, float coef) noexcept
    {
        const int i = idx % len;
        const float d = buf[(size_t)i];
        const float y = -x * coef + d;
        buf[(size_t)i] = x + y * coef;
        return y;
    }

    float tickComb(std::vector<float> &buf, int idx, int len, float x, float fb, float &hpz) noexcept
    {
        const int i = idx % len;
        const float d = buf[(size_t)i];
        // Cold: high-pass the recirculation so the tail has no warm bloom.
        const float hp = d - (hpz += coldHp * (d - hpz));
        buf[(size_t)i] = clean(x + hp * fb);
        return hp;
    }

    void setupHilbert()
    {
        for (int k = 0; k < kHilbTaps; ++k)
        {
            const int n = k - kHilbLat;
            if (n != 0 && (n % 2 != 0))
            {
                const float w = 0.42f - 0.5f * std::cos(float(2.0 * kPiD * k / (kHilbTaps - 1))) +
                                0.08f * std::cos(float(4.0 * kPiD * k / (kHilbTaps - 1)));
                hilb[k] = float(2.0 / (kPiD * n)) * w;
            }
            else
                hilb[k] = 0;
        }
    }

    void tickHilbert(float inL, float inR, float &reL, float &imL, float &reR, float &imR) noexcept
    {
        const int cap = kHilbTaps * 2;
        hilbWrite = (hilbWrite + 1) % cap;
        hilbBufL[(size_t)hilbWrite] = inL;
        hilbBufR[(size_t)hilbWrite] = inR;
        auto at = [cap](const std::vector<float> &b, int wr, int d) noexcept {
            int j = wr - d;
            j %= cap;
            if (j < 0)
                j += cap;
            return b[(size_t)j];
        };
        reL = at(hilbBufL, hilbWrite, kHilbLat);
        reR = at(hilbBufR, hilbWrite, kHilbLat);
        imL = imR = 0;
        for (int k = 0; k < kHilbTaps; ++k)
        {
            if (hilb[k] == 0)
                continue;
            imL += hilb[k] * at(hilbBufL, hilbWrite, k);
            imR += hilb[k] * at(hilbBufR, hilbWrite, k);
        }
    }

    void tickHate(float l, float r, float &oL, float &oR) noexcept
    {
        // Cold high-pass on the input to the spectral path.
        hpZL += hpCoeff * (l - hpZL);
        hpZR += hpCoeff * (r - hpZR);
        const float inL = l - hpZL;
        const float inR = r - hpZR;

        float resL = 0, resR = 0;
        for (int i = 0; i < kPeaks; ++i)
        {
            resL += peakL[(size_t)i].bp(inL);
            resR += peakR[(size_t)i].bp(inR);
        }
        resL *= 2.4f;
        resR *= 2.4f;

        const float freeze = sm[SideA];
        const float rt = tailRt60(freeze);
        float tailL = 0, tailR = 0;
        float cL[4], cR[4];
        // Light feed of the input plus a taste of the resonators, into the tail.
        const float feedL = (inL + 0.25f * resL) * 0.35f;
        const float feedR = (inR + 0.25f * resR) * 0.35f;
        for (int i = 0; i < 4; ++i)
        {
            const float delaySec = (float)combLen[i] / (float)sr;
            const float fb = std::pow(10.f, -3.f * delaySec / std::max(rt, 0.05f));
            const int ci = combIdx[i];
            cL[i] = tickComb(combL[(size_t)i], ci, combLen[i], feedL, clampf(fb, 0.f, 0.98f), coldHpL[i]);
            cR[i] = tickComb(combR[(size_t)i], ci, combLen[i], feedR, clampf(fb, 0.f, 0.98f), coldHpR[i]);
            combIdx[i] = (ci + 1) % combLen[i];
        }
        // Hadamard mix, then two allpasses for a denser metallic field.
        const float mL0 = 0.5f * (cL[0] + cL[1] + cL[2] + cL[3]);
        const float mR0 = 0.5f * (cR[0] + cR[1] + cR[2] + cR[3]);
        float aL = tickAllpass(apL[0], apIdx[0], apLen[0], mL0, 0.5f);
        aL = tickAllpass(apL[1], apIdx[1], apLen[1], aL, 0.5f);
        float aR = tickAllpass(apR[0], apIdx[0], apLen[0], mR0, 0.5f);
        aR = tickAllpass(apR[1], apIdx[1], apLen[1], aR, 0.5f);
        apIdx[0] = (apIdx[0] + 1) % apLen[0];
        apIdx[1] = (apIdx[1] + 1) % apLen[1];
        // Use a slightly different mix on the right for width.
        tailL = aL + 0.15f * mR0;
        tailR = aR + 0.15f * mL0;

        const float haloAmt = sm[SideB];
        float hL = 0, hR = 0;
        if (haloAmt > 1.0e-4f)
        {
            // Hilbert SSB frequency shifter: a 440 Hz tone splits by +38 / -51 cents,
            // and every other partial is shifted by the same Hertz, which is the cold,
            // inharmonic halo. Opposite pan on the two voices.
            float reL, imL, reR, imR;
            tickHilbert(inL + 0.15f * resL, inR + 0.15f * resR, reL, imL, reR, imR);
            haloPhaseUp += 2.0 * kPiD * (double)kHaloUpHz / sr;
            haloPhaseDn += 2.0 * kPiD * (double)kHaloDnHz / sr;
            if (haloPhaseUp > 2.0 * kPiD)
                haloPhaseUp -= 2.0 * kPiD * std::floor(haloPhaseUp / (2.0 * kPiD));
            if (haloPhaseDn < -2.0 * kPiD)
                haloPhaseDn += 2.0 * kPiD * std::floor(-haloPhaseDn / (2.0 * kPiD));
            const float cU = (float)std::cos(haloPhaseUp), sU = (float)std::sin(haloPhaseUp);
            const float cD = (float)std::cos(haloPhaseDn), sD = (float)std::sin(haloPhaseDn);
            // up: re*cos - im*sin; down: re*cos + im*sin (negative Hz already in phase)
            const float uL = reL * cU - imL * sU;
            const float uR = reR * cU - imR * sU;
            const float dL = reL * cD - imL * sD;
            const float dR = reR * cD - imR * sD;
            hL = 0.90f * uL + 0.55f * dL;
            hR = 0.55f * uR + 0.90f * dR;
        }
        else
        {
            // Keep the Hilbert delay primed so engaging HALO is click-free.
            float reL, imL, reR, imR;
            tickHilbert(inL, inR, reL, imL, reR, imR);
            (void)reL;
            (void)imL;
            (void)reR;
            (void)imR;
        }

        oL = clean(resL * 0.70f + tailL * 0.85f + hL * haloAmt * 0.80f);
        oR = clean(resR * 0.70f + tailR * 0.85f + hR * haloAmt * 0.80f);
    }

    void decayHateIdle() noexcept
    {
        // When the rift is off we let the tail fade in the background so a return has no click,
        // but we do not spend a full tickHate. A cheap drain:
        for (int i = 0; i < 4; ++i)
        {
            if (combIdx[i] >= 0 && combLen[i] > 0)
            {
                const int j = combIdx[i];
                if ((size_t)j < combL[(size_t)i].size())
                    combL[(size_t)i][(size_t)j] *= 0.995f;
                if ((size_t)j < combR[(size_t)i].size())
                    combR[(size_t)i][(size_t)j] *= 0.995f;
                combIdx[i] = (combIdx[i] + 1) % combLen[i];
            }
        }
    }

    void setupKWeight()
    {
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
};

} // namespace fever
