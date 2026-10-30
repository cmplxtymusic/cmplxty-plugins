// QUARTZ effect chain (stereo). Effective (post-INTENSITY / post-bypass) parameters in, audio out.
// Chain: Input -> Gate -> Crush -> Drive(Clip/Fold/Fuzz + Grit, 2x OS) -> Tone -> Vocoder -> Low/High cut
//        -> Double -> Widen -> Delay -> Reverb -> Mix(dry delayed by latency) -> Output
#pragma once
#include "Dsp.h"

namespace qz
{
struct QuartzParams
{
    float inDb = 0, gateDb = -80, bits = 24, rateHz = 48000, driveDb = 0, grit = 0;
    int   dtype = 0;               // 0 Clip, 1 Fold, 2 Fuzz
    float toneDb = 0, vocode = 0;  // vocode 0..1
    int   cshape = 0;              // 0 Saw, 1 Square, 2 Noise
    float cnote = 48;              // MIDI
    float lowcut = 20, highcut = 20000;
    float doubleCt = 0, widen = 0; // widen 0..1
    float delayMs = 0, fb = 0;     // fb 0..1
    float reverb = 0, decay = 1.5f;// reverb 0..1
    float mix = 1, outDb = 0;      // mix 0..1
};

class QuartzFx
{
public:
    static constexpr int kChunk = 32;
    static constexpr int kBands = 16;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        latencySamples = Oversampler2x::latencyBaseSamples() / 2;
        dryDelay[0].init (latencySamples + 8); dryDelay[1].init (latencySamples + 8);
        // vocoder band centres
        for (int i = 0; i < kBands; ++i) bandFreq[i] = 120.0f * std::pow (8000.0f / 120.0f, (float) i / (float) (kBands - 1));
        // double
        grainLen = 0.045f * fs;
        for (auto& d : dblLine) d.init ((int) (0.12f * fs));
        // widen allpasses
        const int apLen[4] = { 113, 181, 263, 331 };
        for (int i = 0; i < 4; ++i) widenAp[i].init ((int) (apLen[i] * fs / 48000.0f), 0.55f);
        // delay
        for (auto& d : echo) d.init ((int) (1.05f * fs));
        // reverb
        static const float rl[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.9f };
        for (int i = 0; i < 8; ++i) { revLen[i] = rl[i] * 0.001f * fs; revLine[i].init ((int) (revLen[i] * 1.2f) + 16); }
        static const float al[4] = { 5.1f, 7.7f, 11.3f, 14.9f };
        for (int i = 0; i < 4; ++i) revAp[i].init ((int) (al[i] * 0.001f * fs), 0.6f);
        preDelay.init ((int) (0.012f * fs) + 4);
        for (int b = 0; b < kBands; ++b)
        {
            SVF t; t.set (bandFreq[b], 4.5f, fs);
            for (int ch = 0; ch < 2; ++ch) for (int k2 = 0; k2 < 2; ++k2)
            {
                for (SVF* f : { &vAn[ch][b][k2], &vSy[ch][b][k2] }) { f->k = t.k; f->a1 = t.a1; f->a2 = t.a2; f->a3 = t.a3; }
            }
        }
        for (auto& d : outDcL) d.setup (fs);
        for (auto& d : outDcR) d.setup (fs);
        for (auto& d : drvDc) d.setup (fs);
        reset();
    }

    int latency() const { return latencySamples; }

    void reset()
    {
        for (auto& d : dryDelay) d.reset();
        for (auto& o : os) o.reset();
        for (auto& d : drvDc) d.reset();
        toneLp[0].reset(); toneLp[1].reset();
        voc_reset();
        for (auto& f : lcut) for (auto& s : f) s.reset();
        for (auto& f : hcut) for (auto& s : f) s.reset();
        for (auto& d : dblLine) d.reset();
        dblPhase = 0; dblSmoothCt = 0;
        for (auto& a : widenAp) a.reset();
        for (auto& d : echo) d.reset();
        echoDamp[0] = echoDamp[1] = 0;
        for (auto& d : revLine) d.reset();
        for (auto& a : revAp) a.reset();
        preDelay.reset();
        for (auto& z : revDamp) z = 0;
        revLfo = 0;
        gateEnv = 0; gateOpen = false; gateGain = 1;
        holdVal[0] = holdVal[1] = 0; srPhase = 0;
        for (auto& d : outDcL) d.reset();
        for (auto& d : outDcR) d.reset();
        carPhA = carPhB = 0.0f;
        first = true;
    }

    void process (float* L, float* R, int n, const QuartzParams& p)
    {
        int pos = 0;
        while (pos < n) { const int c = std::min (kChunk, n - pos); chunk (L + pos, R + pos, c, p); pos += c; }
    }

private:
    float fs = 48000;
    int latencySamples = 31;

    // ---- smoothed control values
    struct Ctl
    {
        Smooth in, gate, bits, rateLog, drive, grit, tone, voc, cnote, low, high, dbl, widen, delay, fb, rev, decay, mix, out, out2;   // out2: second smoothing stage on the output gain (no slope step)
        Smooth wClip, wFold, wFuzz, wSaw, wSq, wNoise, echoOn, driveWet;
    } c;
    bool first = true;

    DelayLine dryDelay[2];

    // gate
    float gateEnv = 0, gateGain = 1; bool gateOpen = false;
    // crush
    float holdVal[2] = {}, srPhase = 0;
    // drive
    Oversampler2x os[2]; DCBlock drvDc[2];
    // tone
    OnePoleLP toneLp[2];
    // vocoder
    float bandFreq[kBands];
    SVF vAn[2][kBands][2], vSy[2][kBands][2];
    float vEnv[2][kBands] = {};
    float carPhA = 0, carPhB = 0; Noise carNoise, nz2;
    float vocPrevOn = 0;
    void voc_reset()
    {
        for (int ch = 0; ch < 2; ++ch) for (int b = 0; b < kBands; ++b) { vAn[ch][b][0].reset(); vAn[ch][b][1].reset(); vSy[ch][b][0].reset(); vSy[ch][b][1].reset(); vEnv[ch][b] = 0; }
    }
    // cuts
    SVF lcut[2][2], hcut[2][2];
    // double
    DelayLine dblLine[2]; float dblPhase = 0, dblSmoothCt = 0, grainLen = 2000; // [0]=voice A input line, [1]=voice B input line
    // widen
    Allpass widenAp[4];
    // echo
    DelayLine echo[2]; float echoDamp[2] = {}; float echoT = 0; 
    // reverb
    float revLen[8]; DelayLine revLine[8]; Allpass revAp[4]; DelayLine preDelay; float revDamp[8] = {}; float revLfo = 0;
    DCBlock outDcL[1], outDcR[1];

    static inline float polyblep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    static inline float shClip (float x) { const float xc = clampf (x, -1.5f, 1.5f); return xc - 0.148148f * xc * xc * xc; }
    static inline float shFold (float x) { float p = x * 0.25f; p -= std::floor (p + 0.5f); return p > 0.25f ? 2.0f - 4.0f * p : (p < -0.25f ? -2.0f - 4.0f * p : 4.0f * p); }
    static inline float shFuzz (float x) { return x >= 0 ? 1.0f - std::exp (-2.2f * x) : -(1.0f - std::exp (1.5f * x)) * 0.85f; }

    void setT (Smooth& s, float v) { s.tgt = v; if (first) s.cur = v; }

    void chunk (float* L, float* R, int n, const QuartzParams& p)
    {
        const float a15 = smoothCoef (n, 0.015f, fs), a60 = smoothCoef (n, 0.06f, fs), a120 = smoothCoef (n, 0.12f, fs), a30 = smoothCoef (n, 0.03f, fs);
        setT (c.in, p.inDb); setT (c.gate, p.gateDb); setT (c.bits, p.bits); setT (c.rateLog, std::log (clampf (p.rateHz, 500.0f, 48000.0f)));
        setT (c.drive, p.driveDb); setT (c.grit, p.grit); setT (c.tone, p.toneDb); setT (c.voc, p.vocode); setT (c.cnote, p.cnote);
        setT (c.low, std::log (p.lowcut)); setT (c.high, std::log (p.highcut)); setT (c.dbl, p.doubleCt); setT (c.widen, p.widen);
        setT (c.delay, p.delayMs); setT (c.fb, p.fb); setT (c.rev, p.reverb); setT (c.decay, p.decay); setT (c.mix, p.mix); setT (c.out, p.outDb); setT (c.out2, c.out.cur);
        setT (c.wClip, p.dtype == 0 ? 1.0f : 0.0f); setT (c.wFold, p.dtype == 1 ? 1.0f : 0.0f); setT (c.wFuzz, p.dtype == 2 ? 1.0f : 0.0f);
        setT (c.wSaw, p.cshape == 0 ? 1.0f : 0.0f); setT (c.wSq, p.cshape == 1 ? 1.0f : 0.0f); setT (c.wNoise, p.cshape == 2 ? 1.0f : 0.0f);
        setT (c.echoOn, p.delayMs >= 1.0f ? 1.0f : 0.0f);
        setT (c.driveWet, clampf ((p.driveDb + p.grit * 0.3f) * (1.0f / 1.5f), 0.0f, 1.0f));
        const bool wasFirst = first; first = false;

        // previous values for ramps
        const float inPrev = dbToGain (c.in.cur), outPrev = dbToGain (c.out2.cur), mixPrev = c.mix.cur, widenPrev = c.widen.cur;
        const float vocPrev = c.voc.cur, driveWetPrev = c.driveWet.cur;
        (void) wasFirst;
        c.in.step (a15); c.gate.step (a15); c.bits.step (a15); c.rateLog.step (a15); c.drive.step (a15); c.grit.step (a15); c.tone.step (a15);
        c.voc.step (a15); c.cnote.step (a30); c.low.step (a30); c.high.step (a30); c.dbl.step (a30); c.widen.step (a15);
        c.delay.step (a120); c.fb.step (a15); c.rev.step (a15); c.decay.step (a60); c.mix.step (a15); c.out.step (a15); c.out2.tgt = c.out.cur; c.out2.step (a15);
        c.wClip.step (a15); c.wFold.step (a15); c.wFuzz.step (a15); c.wSaw.step (a15); c.wSq.step (a15); c.wNoise.step (a15);
        c.echoOn.step (a30); c.driveWet.step (a15);

        const float inNow = dbToGain (c.in.cur), outNow = dbToGain (c.out2.cur);
        const float invN = 1.0f / (float) n;

        // ---- coefficient prep
        const float gateThr = dbToGain (c.gate.cur);
        const bool gateActive = c.gate.cur > -78.0f;
        const float aEnvA = 1.0f - std::exp (-1.0f / (0.001f * fs)), aEnvR = 1.0f - std::exp (-1.0f / (0.080f * fs));
        const float aGateUp = 1.0f - std::exp (-1.0f / (0.002f * fs)), aGateDn = 1.0f - std::exp (-1.0f / (0.030f * fs));

        const float bitsNow = c.bits.cur; const bool bitsOn = bitsNow < 23.5f;
        const float levels = std::exp2 (bitsNow - 1.0f), invLevels = 1.0f / levels;
        const float rateNow = std::exp (c.rateLog.cur); const bool rateOn = rateNow < 47000.0f;
        const float srInc = rateNow / fs;

        const float driveG = dbToGain (c.drive.cur);
        const float gritN = c.grit.cur * 0.01f;
        const float bias = 0.30f * gritN;
        const float driveComp = dbToGain (-0.5f * c.drive.cur);
        const float wsum = std::max (1e-3f, c.wClip.cur + c.wFold.cur + c.wFuzz.cur);
        const float wC = c.wClip.cur / wsum, wF = c.wFold.cur / wsum, wZ = c.wFuzz.cur / wsum;
        const float biasDC = wC * shClip (bias) + wF * shFold (bias) + wZ * shFuzz (bias);

        toneLp[0].set (720.0f, fs); toneLp[1].a = toneLp[0].a;
        const float gHi = dbToGain (c.tone.cur * 0.5f), gLo = dbToGain (-c.tone.cur * 0.5f);

        // vocoder prep
        const float vocNow = c.voc.cur;
        const bool vocOn = vocNow > 0.0008f || vocPrev > 0.0008f || p.vocode > 0.0008f;
        if (! vocOn) voc_reset();
        const float carF = 440.0f * std::exp2 ((c.cnote.cur - 69.0f) / 12.0f);
        const float dtA = carF * 1.004f / fs, dtB = carF * 0.996f / fs;     // +-~7 cents
        const float wS = c.wSaw.cur, wQ = c.wSq.cur, wN = c.wNoise.cur;
        const float wnorm = 1.0f / std::max (1e-3f, wS + wQ + wN);
        const float aEnvVA = 1.0f - std::exp (-1.0f / (0.002f * fs)), aEnvVR = 1.0f - std::exp (-1.0f / (0.014f * fs));
        float bandNoise[kBands];
        for (int b = 0; b < kBands; ++b) bandNoise[b] = clampf ((bandFreq[b] - 3000.0f) / 3500.0f, 0.0f, 0.85f);

        // cuts
        {
            const float hp = std::exp (c.low.cur), lp = std::exp (c.high.cur);
            for (int ch = 0; ch < 2; ++ch)
            {
                lcut[ch][0].set (hp, 0.5412f, fs); lcut[ch][1].set (hp, 1.3066f, fs);
                hcut[ch][0].set (lp, 0.5412f, fs); hcut[ch][1].set (lp, 1.3066f, fs);
            }
        }

        // double prep
        const float dblCt = c.dbl.cur;
        const float dblWet = 0.65f * std::min (1.0f, dblCt * 0.25f);
        const float rateA = std::exp2 (dblCt / 1200.0f), rateB = std::exp2 (-dblCt / 1200.0f);
        const float phIncA = (1.0f - rateA) / grainLen, phIncB = (1.0f - rateB) / grainLen;
        const float baseA = 0.008f * fs, baseB = 0.014f * fs;
        // widen
        const float widenA = c.widen.cur;
        // delay
        const float echoTarget = clampf (c.delay.cur * 0.001f * fs, 2.0f, 1.0f * fs);
        const float fbG = clampf (c.fb.cur, 0.0f, 0.95f);
        const float dampA = 1.0f - std::exp (-kTwoPi * 5500.0f / fs);
        const float echoWet = c.echoOn.cur * 0.8f;
        if (echoT < 1.0f) echoT = echoTarget;
        // reverb
        const float revAmt = c.rev.cur;
        const float rt60 = std::max (0.2f, c.decay.cur);
        float revG[8];
        for (int i = 0; i < 8; ++i) revG[i] = std::pow (10.0f, -3.0f * (revLen[i] / fs) / rt60);
        const float revDampA = 1.0f - std::exp (-kTwoPi * (rt60 > 4.0f ? 3500.0f : 5500.0f) / fs);
        const float lfoInc = kTwoPi * 0.35f / fs;

        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * invN;
            float dryL = L[i], dryR = R[i];
            dryDelay[0].push (dryL); dryDelay[1].push (dryR);
            const float gIn = lerp (inPrev, inNow, t);
            float x[2] = { dryL * gIn, dryR * gIn };

            // ---- gate (linked)
            if (gateActive)
            {
                const float lvl = std::max (std::fabs (x[0]), std::fabs (x[1]));
                gateEnv += (lvl > gateEnv ? aEnvA : aEnvR) * (lvl - gateEnv);
                if (!gateOpen && gateEnv > gateThr) gateOpen = true;
                else if (gateOpen && gateEnv < gateThr * 0.63f) gateOpen = false;
                const float tg = gateOpen ? 1.0f : 0.0f;
                gateGain += (tg - gateGain) * (tg > gateGain ? aGateUp : aGateDn);
                x[0] *= gateGain; x[1] *= gateGain;
            }
            else { gateGain += (1.0f - gateGain) * aGateUp; x[0] *= gateGain; x[1] *= gateGain; gateEnv = 0; gateOpen = false; }

            // ---- crush
            if (rateOn)
            {
                srPhase += srInc;
                if (srPhase >= 1.0f) { srPhase -= 1.0f; holdVal[0] = x[0]; holdVal[1] = x[1]; }
                x[0] = holdVal[0]; x[1] = holdVal[1];
            }
            else { srPhase = 0; holdVal[0] = x[0]; holdVal[1] = x[1]; }
            if (bitsOn) for (int ch = 0; ch < 2; ++ch) x[ch] = std::floor (x[ch] * levels + 0.5f) * invLevels;

            // ---- drive (2x OS)
            const float wet = lerp (driveWetPrev, c.driveWet.cur, t);
            for (int ch = 0; ch < 2; ++ch)
            {
                float h0, h1; os[ch].up (x[ch], h0, h1);
                float o[2] = { h0, h1 };
                for (int k = 0; k < 2; ++k)
                {
                    const float pre = o[k] * driveG + bias;
                    float y = wC * shClip (pre) + wF * shFold (pre) + wZ * shFuzz (pre) - biasDC;
                    if (gritN > 0.001f) y += gritN * 0.5f * (shFold (y * 2.4f) - y);
                    y *= driveComp;
                    o[k] = o[k] + wet * (y - o[k]);
                }
                float yy = os[ch].down (o[0], o[1]);
                x[ch] = drvDc[ch].process (yy);
            }

            // ---- tone
            for (int ch = 0; ch < 2; ++ch) { const float lo = toneLp[ch].process (x[ch]); x[ch] = lo * gLo + (x[ch] - lo) * gHi; }

            // ---- vocoder
            if (vocOn)
            {
                const float v = lerp (vocPrev, vocNow, t);
                carPhA += dtA; if (carPhA >= 1.0f) carPhA -= 1.0f;
                carPhB += dtB; if (carPhB >= 1.0f) carPhB -= 1.0f;
                float sawA = 2.0f * carPhA - 1.0f - polyblep (carPhA, dtA), sawB = 2.0f * carPhB - 1.0f - polyblep (carPhB, dtB);
                float pa2 = carPhA + 0.5f; if (pa2 >= 1.0f) pa2 -= 1.0f; float pb2 = carPhB + 0.5f; if (pb2 >= 1.0f) pb2 -= 1.0f;
                const float sqA = (carPhA < 0.5f ? 1.0f : -1.0f) + polyblep (carPhA, dtA) - polyblep (pa2, dtA);
                const float sqB = (carPhB < 0.5f ? 1.0f : -1.0f) + polyblep (carPhB, dtB) - polyblep (pb2, dtB);
                const float nzA = carNoise.next(), nzB = nz2.next();
                const float oscA = (wS * sawA + wQ * sqA * 0.8f + wN * nzA) * wnorm;
                const float oscB = (wS * sawB + wQ * sqB * 0.8f + wN * nzB) * wnorm;
                const float carL = 0.7f * oscA + 0.3f * oscB, carR = 0.3f * oscA + 0.7f * oscB;
                const float nL = nzA * 0.7f, nR = nzB * 0.7f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float car = ch == 0 ? carL : carR, nzc = ch == 0 ? nL : nR;
                    float acc = 0;
                    for (int b = 0; b < kBands; ++b)
                    {
                        float l, bp, h, bp2;
                        vAn[ch][b][0].process (x[ch], l, bp, h);
                        vAn[ch][b][1].process (bp, l, bp2, h);
                        const float rect = std::fabs (bp2);
                        float& e = vEnv[ch][b];
                        e += (rect > e ? aEnvVA : aEnvVR) * (rect - e);
                        const float cin = lerp (car, nzc, bandNoise[b]);
                        float s1, s2;
                        vSy[ch][b][0].process (cin, l, s1, h);
                        vSy[ch][b][1].process (s1, l, s2, h);
                        acc += e * s2;
                    }
                    const float voc = acc * kVocGain;
                    x[ch] = x[ch] * (1.0f - v) + voc * v;
                }
            }

            // ---- cuts
            for (int ch = 0; ch < 2; ++ch)
            {
                float y = x[ch];
                { float l, b, h; lcut[ch][0].process (y, l, b, h); y = h; lcut[ch][1].process (y, l, b, h); y = h; }
                { float l, b, h; hcut[ch][0].process (y, l, b, h); y = l; hcut[ch][1].process (y, l, b, h); y = l; }
                x[ch] = y;
            }

            // ---- double
            {
                const float m = 0.5f * (x[0] + x[1]);
                dblLine[0].push (m);
                // voice A and B each use two crossfading taps on the shared line
                float vA = 0, vB = 0;
                for (int v = 0; v < 2; ++v)
                {
                    float& ph = (v == 0) ? phA : phB; const float inc = (v == 0) ? phIncA : phIncB; const float base = (v == 0) ? baseA : baseB;
                    ph += inc; ph -= std::floor (ph);
                    float ph2 = ph + 0.5f; ph2 -= std::floor (ph2);
                    const float d1 = base + grainLen * ph + 1.0f, d2 = base + grainLen * ph2 + 1.0f;
                    const float w1 = std::sin (kPi * ph), w2 = std::sin (kPi * ph2);
                    const float s = dblLine[0].read (d1) * w1 * w1 + dblLine[0].read (d2) * w2 * w2;
                    if (v == 0) vA = s; else vB = s;
                }
                x[0] += dblWet * (0.8f * vA + 0.3f * vB);
                x[1] += dblWet * (0.3f * vA + 0.8f * vB);
            }

            // ---- widen
            {
                const float w = lerp (widenPrev, widenA, t);
                if (w > 0.0005f)
                {
                    const float M = 0.5f * (x[0] + x[1]), S = 0.5f * (x[0] - x[1]);
                    float d = M;
                    for (int k2 = 0; k2 < 4; ++k2) d = widenAp[k2].process (d);
                    const float S2 = S * (1.0f + 0.6f * w) + w * 0.75f * d;
                    x[0] = M + S2; x[1] = M - S2;
                }
            }

            // ---- delay (ping-pong)
            {
                echoT += (echoTarget - echoT) * 0.0006f * (fs > 60000.0f ? 0.5f : 1.0f);
                const float dL = echo[0].read (std::max (2.0f, echoT)), dR = echo[1].read (std::max (2.0f, echoT));
                echoDamp[0] += dampA * (dL - echoDamp[0]); echoDamp[1] += dampA * (dR - echoDamp[1]);
                const float m = 0.5f * (x[0] + x[1]);
                echo[0].push (std::tanh (m + fbG * echoDamp[1]));
                echo[1].push (std::tanh (fbG * echoDamp[0]));
                x[0] += echoWet * dL; x[1] += echoWet * dR;
            }

            // ---- reverb (FDN)
            {
                const float rm = 0.5f * (x[0] + x[1]);
                preDelay.push (rm);
                float d = preDelay.readInt (std::max (1, (int) (0.010f * fs)));
                for (int k2 = 0; k2 < 4; ++k2) d = revAp[k2].process (d);
                revLfo += lfoInc; if (revLfo > kTwoPi) revLfo -= kTwoPi;
                float o[8];
                for (int k2 = 0; k2 < 8; ++k2)
                {
                    const float mod = 0.00035f * fs * std::sin (revLfo * (1.0f + 0.13f * (float) k2) + (float) k2);
                    float v = revLine[k2].read (std::max (2.0f, revLen[k2] + mod));
                    revDamp[k2] += revDampA * (v - revDamp[k2]);
                    o[k2] = revDamp[k2] * revG[k2];
                }
                // 8x8 Hadamard
                float h[8];
                for (int k2 = 0; k2 < 8; ++k2) h[k2] = o[k2];
                for (int len = 1; len < 8; len <<= 1)
                    for (int a = 0; a < 8; a += len << 1)
                        for (int b2 = a; b2 < a + len; ++b2) { const float u = h[b2], w2 = h[b2 + len]; h[b2] = u + w2; h[b2 + len] = u - w2; }
                const float norm = 0.35355339f;
                for (int k2 = 0; k2 < 8; ++k2) revLine[k2].push (h[k2] * norm + d * 0.5f);
                const float oL = (o[0] - o[2] + o[4] - o[6]) * 0.5f, oR = (o[1] - o[3] + o[5] - o[7]) * 0.5f;
                x[0] += revAmt * 0.9f * oL; x[1] += revAmt * 0.9f * oR;
            }

            // ---- mix + output
            const float mx = lerp (mixPrev, c.mix.cur, t);
            const float g = lerp (outPrev, outNow, t);
            const float dl = dryDelay[0].readInt (latencySamples + 1), dr = dryDelay[1].readInt (latencySamples + 1);
            float yl = (dl * (1.0f - mx) + x[0] * mx) * g, yr = (dr * (1.0f - mx) + x[1] * mx) * g;
            yl = clampf (sanitize (yl), -4.0f, 4.0f); yr = clampf (sanitize (yr), -4.0f, 4.0f);
            L[i] = yl; R[i] = yr;
        }
        // NaN guard on internal state (cheap): reset if exploded
        if (! std::isfinite (echoDamp[0] + echoDamp[1] + revDamp[0] + gateEnv)) reset();
    }

    float phA = 0, phB = 0.37f;
public:
    static constexpr float kVocGain = 0.070f;
};
} // namespace qz
