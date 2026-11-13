#pragma once
#include "Dsp.h"
#include <vector>

namespace glaze
{
// Static response of the EQ section for the UI curve (does not need a running engine).
inline double eqResponseDb(const Settings &s, double hz, double sr)
{
    if (s[EqOn] < 0.5f)
        return 0.0;
    double db = 0;
    if (hpfActive(s[Hpf]))
        db += BiquadCoef::highpass(s[Hpf], 0.7071, sr).magDb(hz, sr);
    db += BiquadCoef::lowShelf(s[LowF], s[LowG], 0.7, sr).magDb(hz, sr);
    db += BiquadCoef::peaking(s[LmF], s[LmG], 0.9, sr).magDb(hz, sr);
    db += BiquadCoef::peaking(s[HmF], s[HmG], 0.9, sr).magDb(hz, sr);
    db += BiquadCoef::highShelf(s[HighF], s[HighG], 0.7, sr).magDb(hz, sr);
    return db;
}

// Level-neutral-at-small-signal shapers used by the saturator, f(u)/1 with slope 1 at the origin.
inline float satShape(int type, float u) noexcept
{
    switch (type)
    {
    case 1: // TUBE: asymmetric soft saturation, even harmonics
    {
        const float v = u + 0.22f * u * u; // the even term makes the curve asymmetric
        return std::tanh(v);
    }
    case 2: // HARD: much squarer knee
    {
        const float a = u * u;
        return u / std::sqrt(std::sqrt(1.f + a * a));
    }
    default: // TAPE
        return std::tanh(u);
    }
}

class Engine
{
  public:
    static constexpr int kScope = 4096;
    static constexpr int kSub = 32;

    // ------------------------------------------------------------------------------------------------ setup
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = std::max(sampleRate, 8000.0);
        smoothA = (float)(1.0 - std::exp(-(double)kSub / (0.020 * sr)));
        modA = onePole(0.008, sr);
        // limiter geometry
        L1 = std::max(4, (int)std::lround(0.0025 * sr));
        W = 2 * L1 - 1;
        // The minimum window is widened by kReach samples each side so that every output sample the true-peak
        // interpolator can see around a peak is also guaranteed to carry at most that peak's gain.
        Wm = W + 2 * kReach;
        D = TruePeakFir::kLead + W - 1 + kReach;
        int p = 1;
        while (p < D + 8)
            p <<= 1;
        dlyMask = p - 1;
        dlyL.assign((size_t)p, 0.f);
        dlyR.assign((size_t)p, 0.f);
        dryL.assign((size_t)p, 0.f);
        dryR.assign((size_t)p, 0.f);
        dryMask = (size_t)p - 1;
        dryW = 0;
        p = 1;
        while (p < Wm + 4)
            p <<= 1;
        dqMask = p - 1;
        dqIdx.assign((size_t)p, 0);
        dqVal.assign((size_t)p, 1.f);
        box1.assign((size_t)L1, 1.f);
        box2.assign((size_t)L1, 1.f);
        meter.prepare(sr);
        meterIn.prepare(sr);
        reset();
        snap();
    }

    int latencySamples() const noexcept { return D; }

    void set(const Settings &s)
    {
        raw = s;
        for (int i = 0; i < Count; ++i)
            sm[(size_t)i].tgt = s[i];
    }
    void snap()
    {
        for (int i = 0; i < Count; ++i)
            sm[(size_t)i].snap(raw[i]);
        updateCoefs();
        for (int i = 0; i < Modules; ++i)
            modGain[(size_t)i] = moduleTarget(i);
        bypassGain = raw[Bypass] >= 0.5f ? 0.f : 1.f;
    }

    void reset()
    {
        for (auto &c : ch)
            c.reset();
        std::fill(dlyL.begin(), dlyL.end(), 0.f);
        std::fill(dlyR.begin(), dlyR.end(), 0.f);
        std::fill(dryL.begin(), dryL.end(), 0.f);
        std::fill(dryR.begin(), dryR.end(), 0.f);
        dryW = 0;
        dlyW = 0;
        histL.fill(0.f);
        histR.fill(0.f);
        histW = 0;
        tpL.fill(0.f);
        tpR.fill(0.f);
        tpW = 0;
        std::fill(dqVal.begin(), dqVal.end(), 1.f);
        dqHead = dqTail = 0;
        n64 = 0;
        relY = 1.f;
        std::fill(box1.begin(), box1.end(), 1.f);
        std::fill(box2.begin(), box2.end(), 1.f);
        sum1 = sum2 = (double)L1;
        pos1 = pos2 = 0;
        glueGr = 0;
        glueDet = 0;
        for (auto &b : band)
            b.reset();
        meter.resetAll();
        meterIn.resetAll();
        tpMaxLin = 0;
    }

    // ------------------------------------------------------------------------------------------------ audio
    void process(float *left, float *right, int n)
    {
        FlushDenormals fd;
        const bool stereo = right != nullptr;
        if (!stereo)
        {
            if (monoScratch.size() < (size_t)n)
                monoScratch.resize((size_t)n);
            std::memcpy(monoScratch.data(), left, sizeof(float) * (size_t)n);
            right = monoScratch.data();
        }
        float grMaxLim = 0, grMaxGlue = 0, grMaxMb = 0, peakOut = 0;
        for (int pos = 0; pos < n; pos += kSub)
        {
            const int k = std::min(kSub, n - pos);
            for (int i = 0; i < Count; ++i)
            {
                const auto &d = definitions[(size_t)i];
                if (d.kind == Kind::Float)
                    sm[(size_t)i].step(smoothA);
                else
                    sm[(size_t)i].snap(raw[i]);
            }
            updateCoefs();
            for (int i = pos; i < pos + k; ++i)
            {
                for (int m = 0; m < Modules; ++m)
                    modGain[(size_t)m] += modA * (moduleTarget(m) - modGain[(size_t)m]);
                bypassGain += modA * ((raw[Bypass] >= 0.5f ? 0.f : 1.f) - bypassGain);

                const float inL = left[i], inR = right[i];
                float yL, yR;
                float glueGrNow = 0, mbGrNow = 0, limGrNow = 0;
                processSample(inL, inR, yL, yR, glueGrNow, mbGrNow, limGrNow);
                grMaxGlue = std::max(grMaxGlue, glueGrNow);
                grMaxMb = std::max(grMaxMb, mbGrNow);
                grMaxLim = std::max(grMaxLim, limGrNow);

                const float outL = clean(yL), outR = clean(yR);
                left[i] = outL;
                right[i] = outR;
                peakOut = std::max(peakOut, std::max(std::abs(outL), std::abs(outR)));

                meter.push(outL, outR);
                meterIn.push(inL, inR);
                truePeakMeter(outL, outR);

                const int w = scopeW.load(std::memory_order_relaxed);
                scopeIn[(size_t)w] = 0.5f * (inL + inR);
                scope[(size_t)w] = 0.5f * (outL + outR);
                scopeW.store((w + 1) & (kScope - 1), std::memory_order_relaxed);
            }
        }
        peakMeter.store(peakOut);
        const float rel = 1.f - std::exp(-(float)n / (0.35f * (float)sr));
        auto hold = [&](std::atomic<float> &a, float v) {
            const float old = a.load();
            a.store(v > old ? v : old + rel * (v - old));
        };
        hold(limGrDb, grMaxLim);
        hold(glueGrDb, grMaxGlue);
        hold(mbGrDb, grMaxMb);
        momentary.store(meter.momentary);
        shortTerm.store(meter.shortTerm);
        integrated.store(meter.integrated);
        shortTermIn.store(meterIn.shortTerm);
        tpMaxDb.store(tpMaxLin > 0 ? gainToDb(tpMaxLin) : -120.f);
        if (resetIntegratedFlag.exchange(false))
        {
            meter.resetIntegrated();
            meterIn.resetIntegrated();
            tpMaxLin = 0;
        }
    }

    // ------------------------------------------------------------------------------------------------ meters (read from the UI thread)
    std::atomic<float> limGrDb{ 0 }, glueGrDb{ 0 }, mbGrDb{ 0 }, peakMeter{ 0 };
    std::atomic<float> momentary{ -120.f }, shortTerm{ -120.f }, integrated{ -120.f }, shortTermIn{ -120.f },
        tpMaxDb{ -120.f };
    std::atomic<bool> resetIntegratedFlag{ false };
    std::array<float, kScope> scope{}, scopeIn{};
    std::atomic<int> scopeW{ 0 };

  private:
    static constexpr int Modules = 6; // eq, glue, multiband, sat, width, limiter
    float moduleTarget(int m) const noexcept
    {
        static constexpr int ids[Modules] = { EqOn, GlueOn, MbOn, SatOn, WidthOn, LimOn };
        return raw[ids[m]] >= 0.5f ? 1.f : 0.f;
    }

    struct Xo
    {
        Svf s1, s2, s3;
        void reset() noexcept
        {
            s1.reset();
            s2.reset();
            s3.reset();
        }
        inline void split(float x, const SvfCoef &c, float &lo, float &hi) noexcept
        {
            float lp, bp, hp, a, b, c2, d, e, f;
            s1.tick(x, c.a1, c.a2, c.a3, c.k, lp, bp, hp);
            s2.tick(lp, c.a1, c.a2, c.a3, c.k, a, b, c2);
            s3.tick(hp, c.a1, c.a2, c.a3, c.k, d, e, f);
            lo = a;
            hi = f;
        }
    };
    struct Band
    {
        Env env, grEnv;
        float slow = 0, gr = 0;
        void reset() noexcept
        {
            env.reset();
            grEnv.reset();
            slow = 0;
            gr = 0;
        }
    };
    struct Chan
    {
        Biquad hpf, low, lm, hm, high, tiltLo, tiltHi;
        Xo xo1, xo2, xoAp;
        Hp1 scHp, dcBlock;
        Oversampler os;
        Svf sideHp, sideHp2;
        void reset() noexcept
        {
            hpf.reset();
            low.reset();
            lm.reset();
            hm.reset();
            high.reset();
            tiltLo.reset();
            tiltHi.reset();
            xo1.reset();
            xo2.reset();
            xoAp.reset();
            scHp.reset();
            dcBlock.reset();
            os.reset();
            sideHp.reset();
            sideHp2.reset();
        }
    };

    void updateCoefs()
    {
        const auto S = [&](int p) { return sm[(size_t)p].cur; };
        inGain = dbToGainSnap(S(Input));
        BiquadCoef ident;
        const BiquadCoef chp = hpfActive(S(Hpf)) ? BiquadCoef::highpass(S(Hpf), 0.7071, sr) : ident;
        const BiquadCoef cl = BiquadCoef::lowShelf(S(LowF), S(LowG), 0.7, sr);
        const BiquadCoef cm = BiquadCoef::peaking(S(LmF), S(LmG), 0.9, sr);
        const BiquadCoef ch2 = BiquadCoef::peaking(S(HmF), S(HmG), 0.9, sr);
        const BiquadCoef chi = BiquadCoef::highShelf(S(HighF), S(HighG), 0.7, sr);
        const float tone = S(Tone) * 4.f * std::min(1.f, S(SatMix) * 2.f);
        const BiquadCoef tlo = BiquadCoef::lowShelf(2500.0, -tone, 0.6, sr);
        const BiquadCoef thi = BiquadCoef::highShelf(2500.0, tone, 0.6, sr);
        xo1c = SvfCoef::make(S(Xo1), 0.70710678f, (float)sr);
        xo2c = SvfCoef::make(S(Xo2), 0.70710678f, (float)sr);
        sideC = SvfCoef::make(std::max(S(BassMono), 25.f), 0.70710678f, (float)sr);
        for (auto &c : ch)
        {
            c.hpf.c = chp;
            c.low.c = cl;
            c.lm.c = cm;
            c.hm.c = ch2;
            c.high.c = chi;
            c.tiltLo.c = tlo;
            c.tiltHi.c = thi;
        }
        const float gt = S(GThr), gr = std::max(S(GRatio), 1.01f);
        glueThr = gt;
        glueSlope = 1.f - 1.f / gr;
        glueDetRel = std::exp(-1.f / (0.025f * (float)sr));
        glueAtk = std::exp(-1.f / (std::max(S(GAtk), 0.05f) * 0.001f * (float)sr));
        glueRel = std::exp(-1.f / (std::max(S(GRel), 5.f) * 0.001f * (float)sr));
        relCoef = onePole(std::max(S(LimRel), 5.f) * 0.001, sr);
        for (auto &c : ch)
            c.scHp.set(90.f, (float)sr);
        for (int b = 0; b < 3; ++b)
        {
            band[(size_t)b].env.set(0.004, 0.05 + 0.03 * (2 - b), sr);
            band[(size_t)b].grEnv.set(0.008 + 0.004 * (2 - b), 0.09 + 0.05 * (2 - b), sr);
        }
        slowA = onePole(1.2, sr);
        satDrive = dbToGain(S(Drive));
        satType = clampi((int)std::lround(S(SatType)), 0, 2);
        ch[0].dcBlock.set(12.f, (float)sr);
        ch[1].dcBlock.set(12.f, (float)sr);
    }

    inline void truePeakMeter(float l, float r)
    {
        tpL[(size_t)tpW] = tpL[(size_t)tpW + TruePeakFir::kTaps] = l;
        tpR[(size_t)tpW] = tpR[(size_t)tpW + TruePeakFir::kTaps] = r;
        tpW = (tpW + 1) & (TruePeakFir::kTaps - 1);
        const float p = std::max(tpFir.peak(&tpL[(size_t)tpW]), tpFir.peak(&tpR[(size_t)tpW]));
        tpMaxLin = std::max(tpMaxLin, p);
    }

    inline void processSample(float inL, float inR, float &outL, float &outR, float &glueGrNow, float &mbGrNow,
                              float &limGrNow)
    {
        const auto S = [&](int p) { return sm[(size_t)p].cur; };
        const double wetBypass = bypassGain;
        // ---- input trim
        double xl = (double)inL * inGain, xr = (double)inR * inGain;

        // ---- EQ
        {
            double a = xl, b = xr;
            a = ch[0].hm.tick(ch[0].lm.tick(ch[0].low.tick(ch[0].hpf.tick(a))));
            b = ch[1].hm.tick(ch[1].lm.tick(ch[1].low.tick(ch[1].hpf.tick(b))));
            a = ch[0].high.tick(a);
            b = ch[1].high.tick(b);
            const double g = modGain[0];
            xl += g * (a - xl);
            xr += g * (b - xr);
        }

        // ---- Multiband (3 bands, Linkwitz-Riley 4th order, sum is flat)
        {
            float lowL, restL, lowR, restR, midL, hiL, midR, hiR;
            ch[0].xo1.split((float)xl, xo1c, lowL, restL);
            ch[1].xo1.split((float)xr, xo1c, lowR, restR);
            ch[0].xo2.split(restL, xo2c, midL, hiL);
            ch[1].xo2.split(restR, xo2c, midR, hiR);
            float ap1L, ap2L, ap1R, ap2R;
            ch[0].xoAp.split(lowL, xo2c, ap1L, ap2L);
            ch[1].xoAp.split(lowR, xo2c, ap1R, ap2R);
            lowL = ap1L + ap2L;
            lowR = ap1R + ap2R;
            float bl[3] = { lowL, midL, hiL }, br[3] = { lowR, midR, hiR };
            const float amt[3] = { S(Mb1), S(Mb2), S(Mb3) }, lvl[3] = { S(Ml1), S(Ml2), S(Ml3) };
            float sumL = 0, sumR = 0, maxGr = 0;
            for (int b = 0; b < 3; ++b)
            {
                auto &B = band[(size_t)b];
                const float det = std::max(std::abs(bl[b]), std::abs(br[b]));
                const float e = B.env.tick(det);
                B.slow += slowA * (e - B.slow);
                float target = 0;
                if (amt[b] > 0.001f && B.slow > 1.0e-4f)
                {
                    const float diff = 20.f * std::log10((e + 1.0e-9f) / (B.slow + 1.0e-9f));
                    target = amt[b] * clampf((diff - 1.0f) * 0.8f, 0.f, 9.f);
                }
                B.gr = B.grEnv.tick(target);
                maxGr = std::max(maxGr, B.gr);
                const float g = dbToGainSnap(lvl[b] - B.gr);
                sumL += bl[b] * g;
                sumR += br[b] * g;
            }
            mbGrNow = maxGr * (float)modGain[2];
            const double g = modGain[2];
            xl += g * ((double)sumL - xl);
            xr += g * ((double)sumR - xr);
        }

        // ---- Glue compressor
        {
            const float sl = ch[0].scHp.tick((float)xl), sr2 = ch[1].scHp.tick((float)xr);
            const float rawDet = std::max(std::abs(sl), std::abs(sr2));
            glueDet = std::max(rawDet, glueDet * glueDetRel);
            const float det = glueDet;
            const float lvl = 20.f * std::log10(std::max(det, 1.0e-6f));
            const float over = lvl - glueThr;
            constexpr float K = 6.f;
            float gr;
            if (2.f * over < -K)
                gr = 0.f;
            else if (2.f * std::abs(over) <= K)
            {
                const float t = over + K * 0.5f;
                gr = glueSlope * t * t / (2.f * K);
            }
            else
                gr = glueSlope * over;
            if (gr > glueGr)
                glueGr = glueAtk * glueGr + (1.f - glueAtk) * gr;
            else
                glueGr = glueRel * glueGr + (1.f - glueRel) * gr;
            const float gain = dbToGainSnap(S(GMakeup) - glueGr);
            const float mix = S(GMix);
            const float wl = (float)xl * gain, wr = (float)xr * gain;
            const double a = modGain[1];
            const double ol = xl * (1.0 - mix) + (double)wl * mix, orr = xr * (1.0 - mix) + (double)wr * mix;
            xl += a * (ol - xl);
            xr += a * (orr - xr);
            glueGrNow = glueGr * (float)a;
        }

        // ---- Saturation (8x oversampled, mixed inside the oversampled domain so dry and wet stay phase aligned)
        const float satAct = clampf(S(Drive) * 2.f, 0.f, 1.f); // drive at 0 dB: the stage is a true pass
        if ((modGain[3] > 1.0e-4 || raw[SatOn] >= 0.5f) && satAct > 0.f)
        {
            float hi[2][8];
            ch[0].os.up((float)xl, hi[0]);
            ch[1].os.up((float)xr, hi[1]);
            const float d = std::max(satDrive, 1.0e-3f), invD = 1.f / d, mix = S(SatMix);
            for (int c = 0; c < 2; ++c)
                for (int j = 0; j < 8; ++j)
                {
                    const float x = hi[c][j];
                    const float w = satShape(satType, x * d) * invD;
                    hi[c][j] = x + mix * (w - x);
                }
            double a = ch[0].os.down(hi[0]), b = ch[1].os.down(hi[1]);
            if (satType == 1)
            {
                a = ch[0].dcBlock.tick((float)a);
                b = ch[1].dcBlock.tick((float)b);
            }
            a = ch[0].tiltHi.tick(ch[0].tiltLo.tick(a));
            b = ch[1].tiltHi.tick(ch[1].tiltLo.tick(b));
            const double g = modGain[3] * satAct;
            xl += g * (a - xl);
            xr += g * (b - xr);
        }

        // ---- Stereo width, bass mono
        {
            float m = (float)(0.5 * (xl + xr)), s = (float)(0.5 * (xl - xr));
            float sh = s;
            if (bassMonoActive(S(BassMono)))
            {
                float lp, bp, hp, hp2;
                ch[0].sideHp.tick(s, sideC.a1, sideC.a2, sideC.a3, sideC.k, lp, bp, hp);
                ch[0].sideHp2.tick(hp, sideC.a1, sideC.a2, sideC.a3, sideC.k, lp, bp, hp2);
                sh = hp2; // 4th-order Linkwitz-Riley high-pass on the side signal
            }
            const float w = S(Width);
            const float s2 = sh * w;
            const double g = modGain[4];
            const double wl = (double)m + s2, wr = (double)m - s2;
            xl += g * (wl - xl);
            xr += g * (wr - xr);
        }

        // ---- True-peak lookahead limiter
        const float push = dbToGainSnap(S(Push) * (float)modGain[5]);
        const float fl = (float)xl * push, fr = (float)xr * push;
        dlyL[(size_t)dlyW & dlyMask] = fl;
        dlyR[(size_t)dlyW & dlyMask] = fr;
        const float ceilLin = dbToGain(S(Ceiling) - 0.1f); // 0.1 dB margin: a time-varying gain makes the interpolated peak differ slightly from the input estimate for the interpolator's residual error
        histL[(size_t)histW] = histL[(size_t)histW + TruePeakFir::kTaps] = fl;
        histR[(size_t)histW] = histR[(size_t)histW + TruePeakFir::kTaps] = fr;
        histW = (histW + 1) & (TruePeakFir::kTaps - 1);
        float gReq = 1.f;
        if (modGain[5] > 1.0e-4)
        {
            const float tp = std::max(tpFir.peak(&histL[(size_t)histW]), tpFir.peak(&histR[(size_t)histW]));
            if (tp > ceilLin)
                gReq = ceilLin / tp;
        }
        // sliding minimum over W samples
        {
            while (dqTail > dqHead && dqVal[(size_t)(dqTail - 1) & dqMask] >= gReq)
                --dqTail;
            dqIdx[(size_t)dqTail & dqMask] = n64;
            dqVal[(size_t)dqTail & dqMask] = gReq;
            ++dqTail;
            while (dqIdx[(size_t)dqHead & dqMask] <= n64 - Wm)
                ++dqHead;
        }
        const float mn = dqVal[(size_t)dqHead & dqMask];
        relY = std::min(mn, relY + (1.f - relY) * relCoef);
        // two cascaded boxcars (triangular smoothing, total support W)
        sum1 += (double)relY - (double)box1[(size_t)pos1];
        box1[(size_t)pos1] = relY;
        pos1 = (pos1 + 1) % L1;
        const float a1 = (float)(sum1 / L1);
        sum2 += (double)a1 - (double)box2[(size_t)pos2];
        box2[(size_t)pos2] = a1;
        pos2 = (pos2 + 1) % L1;
        const float gain = std::min((float)(sum2 / L1), 1.f);
        ++n64;
        const size_t rp = (size_t)(dlyW - D) & dlyMask;
        float ol = dlyL[rp] * gain, orr = dlyR[rp] * gain;
        ++dlyW;
        limGrNow = gain < 1.f ? -gainToDb(gain) : 0.f;

        // ---- bypass: latency-matched dry
        dryL[(size_t)dryW & dryMask] = inL;
        dryR[(size_t)dryW & dryMask] = inR;
        const size_t dp = (size_t)(dryW - D) & dryMask;
        const float dl = dryL[dp], dr = dryR[dp];
        ++dryW;
        const float bg = (float)wetBypass;
        outL = dl + bg * (ol - dl);
        outR = dr + bg * (orr - dr);
    }

    double sr = 48000;
    float smoothA = 0.01f, modA = 0.01f;
    Settings raw;
    std::array<Smoothed, Count> sm{};
    std::array<float, Modules> modGain{ { 1, 1, 1, 1, 1, 1 } };
    float bypassGain = 1.f;
    std::array<Chan, 2> ch{};
    std::array<Band, 3> band{};
    SvfCoef xo1c, xo2c, sideC;
    float inGain = 1.f;
    // glue
    float glueDet = 0, glueDetRel = 0.999f;
    float glueThr = 0, glueSlope = 0.5f, glueAtk = 0.9f, glueRel = 0.99f, glueGr = 0;
    float slowA = 0.0001f;
    // sat
    float satDrive = 1.f;
    int satType = 0;
    // limiter
    static constexpr int kReach = 8;
    int L1 = 58, W = 115, Wm = 131, D = 130;
    std::vector<float> dlyL, dlyR;
    int dlyW = 0;
    size_t dlyMask = 255;
    std::array<float, 2 * TruePeakFir::kTaps> histL{}, histR{}, tpL{}, tpR{};
    int histW = 0, tpW = 0;
    TruePeakFir tpFir;
    std::vector<int64_t> dqIdx;
    std::vector<float> dqVal;
    int64_t dqHead = 0, dqTail = 0, n64 = 0;
    size_t dqMask = 255;
    float relY = 1.f, relCoef = 0.01f;
    std::vector<float> box1, box2;
    double sum1 = 0, sum2 = 0;
    int pos1 = 0, pos2 = 0;
    // bypass dry delay
    std::vector<float> dryL, dryR;
    int dryW = 0;
    size_t dryMask = 255;
    LoudnessMeter meter, meterIn;
    float tpMaxLin = 0;
    std::vector<float> monoScratch;
};
} // namespace glaze
