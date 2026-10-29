// Engine: ALTER -> QUARTZ with section on/off, per-knob bypass, INTENSITY macro, master bypass.
// Fixed total latency = alter latency + quartz latency, independent of every switch.
#pragma once
#include "Alter.h"
#include "QuartzFx.h"
#include "ParamTable.h"

namespace qz
{
// Raw parameter values (natural units; choices/bools as 0..N floats), indexed by ParamIndex.
struct ParamValues { float v[kNumParams]; ParamValues() { for (int i = 0; i < kNumParams; ++i) v[i] = kParams[i].def; } };

// Resolve the knob settings the DSP actually uses (INTENSITY macro + per-knob bypass). Exposed for tests/UI.
inline QuartzParams resolveQuartz (const ParamValues& pv)
{
    const float* v = pv.v;
    auto byp = [&] (int p) { const int b = bypassFor (p); return b >= 0 && v[b] > 0.5f; };
    const float I = byp (pIntensity) ? 1.0f : clampf (v[pIntensity] * 0.01f, 0.0f, 1.0f);
    const float cI = I, cI15 = std::pow (I, 1.5f);
    QuartzParams q;
    q.inDb   = byp (pIn) ? 0.0f : v[pIn];
    q.gateDb = byp (pGate) ? -80.0f : v[pGate];
    q.bits   = byp (pBits) ? 24.0f : 24.0f + cI * (v[pBits] - 24.0f);
    {
        const float k = byp (pRate) ? 48000.0f : v[pRate];
        const float ln = std::log (48000.0f) + cI * (std::log (std::max (1000.0f, k)) - std::log (48000.0f));
        q.rateHz = std::exp (ln);
    }
    q.driveDb = byp (pDrive) ? 0.0f : cI15 * v[pDrive];
    q.grit    = byp (pGrit) ? 0.0f : cI15 * v[pGrit];
    q.dtype   = (int) std::lround (v[pDType]);
    q.toneDb  = byp (pTone) ? 0.0f : v[pTone];
    q.vocode  = (byp (pVocode) || byp (pCNote)) ? 0.0f : cI * v[pVocode] * 0.01f;
    q.cshape  = (int) std::lround (v[pCShape]);
    q.cnote   = v[pCNote];
    q.lowcut  = byp (pLowCut) ? 20.0f : v[pLowCut];
    q.highcut = byp (pHighCut) ? 20000.0f : v[pHighCut];
    q.doubleCt = byp (pDouble) ? 0.0f : cI * v[pDouble];
    q.widen   = byp (pWiden) ? 0.0f : v[pWiden] * 0.01f;
    q.delayMs = byp (pDelay) ? 0.0f : v[pDelay];
    q.fb      = byp (pFb) ? 0.0f : v[pFb] * 0.01f;
    q.reverb  = byp (pReverb) ? 0.0f : cI * v[pReverb] * 0.01f;
    q.decay   = byp (pDecay) ? 1.5f : v[pDecay];
    {
        const float knob = byp (pMix) ? 1.0f : v[pMix] * 0.01f;
        q.mix = knob * (0.2f + 0.8f * cI);
        if (byp (pMix)) q.mix = 1.0f;
    }
    q.outDb   = byp (pOut) ? 0.0f : v[pOut];
    return q;
}

class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        alter.prepare (sampleRate);
        quartz.prepare (sampleRate);
        da = alter.latency(); dq = quartz.latency();
        for (auto& d : aDry) d.init (da + 8);
        for (auto& d : qDry) d.init (dq + 8);
        for (auto& d : mDry) d.init (da + dq + 8);
        monoBuf.assign (kMaxBlock, 0.0f);
        reset();
    }
    int latency() const { return da + dq; }
    int alterLatency() const { return da; }

    void reset()
    {
        alter.reset(); quartz.reset();
        for (auto& d : aDry) d.reset();
        for (auto& d : qDry) d.reset();
        for (auto& d : mDry) d.reset();
        sA.snap (1); sQ.snap (1); sB.snap (0); first = true;
        aMixS.snap (1); aOutS.snap (1);
        wasAOff = wasQOff = false;
    }

    void process (float* L, float* R, int n, const ParamValues& pv)
    {
        DenormalGuard dg;
        int pos = 0;
        while (pos < n) { const int c = std::min (kMaxBlock, n - pos); block (L + pos, R + pos, c, pv); pos += c; }
    }

private:
    static constexpr int kMaxBlock = 32;
    float fs = 48000; int da = 0, dq = 0;
    AlterEngine alter; QuartzFx quartz;
    DelayLine aDry[2], qDry[2], mDry[2];
    std::vector<float> monoBuf;
    Smooth sA, sQ, sB, aMixS, aOutS, pitchS, formS, genS, speedS, adS;
    bool first = true, wasAOff = false, wasQOff = false;

    void block (float* L, float* R, int n, const ParamValues& pv)
    {
        const float* v = pv.v;
        auto byp = [&] (int p) { const int b = bypassFor (p); return b >= 0 && v[b] > 0.5f; };
        const float aSec = clampf (0.0f, 0.0f, 1.0f); (void) aSec;
        const float a10 = smoothCoef (n, 0.010f, fs), a15 = smoothCoef (n, 0.015f, fs);
        sA.tgt = v[pAon] > 0.5f ? 1.0f : 0.0f; sQ.tgt = v[pQon] > 0.5f ? 1.0f : 0.0f; sB.tgt = v[pBypass] > 0.5f ? 1.0f : 0.0f;
        aMixS.tgt = byp (pAMix) ? 1.0f : v[pAMix] * 0.01f;
        aOutS.tgt = dbToGain (byp (pAOut) ? 0.0f : v[pAOut]);
        pitchS.tgt = byp (pPitch) ? 0.0f : v[pPitch];
        formS.tgt = byp (pFormant) ? 0.0f : v[pFormant];
        genS.tgt = byp (pGender) ? 0.0f : v[pGender];
        speedS.tgt = byp (pSpeed) ? 12.0f : v[pSpeed];
        adS.tgt = byp (pADrive) ? 0.0f : v[pADrive];
        if (first) { sA.snap (sA.tgt); sQ.snap (sQ.tgt); sB.snap (sB.tgt); aMixS.snap (aMixS.tgt); aOutS.snap (aOutS.tgt);
                     pitchS.snap (pitchS.tgt); formS.snap (formS.tgt); genS.snap (genS.tgt); speedS.snap (speedS.tgt); adS.snap (adS.tgt); first = false; }
        const float sA0 = sA.cur, sQ0 = sQ.cur, sB0 = sB.cur, mix0 = aMixS.cur, out0 = aOutS.cur;
        sA.step (a10); sQ.step (a10); sB.step (a10); aMixS.step (a15); aOutS.step (a15);
        pitchS.step (a15); formS.step (a15); genS.step (a15); speedS.step (a15); adS.step (a15);

        AlterControls ac;
        ac.mode = (int) std::lround (v[pMode]); ac.pitchSt = pitchS.cur; ac.formantSt = formS.cur; ac.link = v[pLink] > 0.5f;
        ac.gender = genS.cur; ac.speedMs = speedS.cur; ac.key = (int) std::lround (v[pKey]); ac.scale = (int) std::lround (v[pScale]);
        alter.setControls (ac);

        const bool alterActive = true;   // always tracking: re-enabling must not produce a hole (engine needs ~44 ms of history)
        const bool quartzActive = sQ.cur > 0.0005f || sQ.tgt > 0.5f;
        if (! quartzActive && ! wasQOff) { quartz.reset(); }
        wasQOff = ! quartzActive;

        float* ch[2] = { L, R };
        const float invN = 1.0f / (float) n;
        // master-bypass dry path (aligned to total latency)
        static thread_local float dryBuf[2][kMaxBlock];
        for (int i = 0; i < n; ++i) { mDry[0].push (L[i]); mDry[1].push (R[i]); dryBuf[0][i] = mDry[0].readInt (da + dq + 1); dryBuf[1][i] = mDry[1].readInt (da + dq + 1); }

        // ---- ALTER stage (output at latency da)
        const float ad = adS.cur;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * invN;
            const float l = L[i], r = R[i];
            aDry[0].push (l); aDry[1].push (r);
            const float dl = aDry[0].readInt (da + 1), dr = aDry[1].readInt (da + 1);
            float wet = 0;
            if (alterActive) { wet = alter.process (0.5f * (l + r)); wet = alter.drive (wet, ad); }
            const float mx = lerp (mix0, aMixS.cur, t), g = lerp (out0, aOutS.cur, t), sa = lerp (sA0, sA.cur, t);
            const float al = (dl * (1.0f - mx) + wet * mx) * g, ar = (dr * (1.0f - mx) + wet * mx) * g;
            L[i] = dl + sa * (al - dl);
            R[i] = dr + sa * (ar - dr);
        }
        // ---- QUARTZ stage
        float qa[2][kMaxBlock];
        for (int i = 0; i < n; ++i) { qDry[0].push (L[i]); qDry[1].push (R[i]); qa[0][i] = qDry[0].readInt (dq + 1); qa[1][i] = qDry[1].readInt (dq + 1); }
        if (quartzActive) quartz.process (L, R, n, resolveQuartz (pv));
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * invN;
            const float sq = lerp (sQ0, sQ.cur, t), sb = lerp (sB0, sB.cur, t);
            for (int c2 = 0; c2 < 2; ++c2)
            {
                float y = qa[c2][i] + sq * ((quartzActive ? ch[c2][i] : qa[c2][i]) - qa[c2][i]);
                y = y + sb * (dryBuf[c2][i] - y);
                ch[c2][i] = clampf (sanitize (y), -4.0f, 4.0f);
            }
        }
    }
};
} // namespace qz
