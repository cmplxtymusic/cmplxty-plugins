// Growl DSP core - plain C++17, no JUCE dependency.
//
// Signal flow (per channel, stereo max):
//   in -> input gain -> Linkwitz-Riley 8 crossover (48 dB/oct) (60..200 Hz)
//     low  band : optional mono sum, level            (stays clean - never saturated)
//     high band : drive (tanh / tube / fold / hard / diode, 1x/2x/4x oversampled, bias)
//                 -> vowel formant bank (3 band-pass formants, morph + shift + focus, tempo-synced LFO on the morph)
//                 -> post filter (LP/HP SVF)
//   low + high -> dry/wet mix -> output gain -> soft safety limiter
//
// The whole plug-in has one fixed latency (kLatency samples) in every oversampling mode, so dry/wet is phase aligned
// and the host can compensate it. All buffers are fixed-size members: process() never allocates.
// Everything is block-size independent: smoothers run per sample, filter coefficients are refreshed every kCtlInterval
// samples on a counter that survives block boundaries.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace gw
{
constexpr int    kMaxChannels = 2;
constexpr int    kCtlInterval = 8;                    // coefficient refresh period (samples)
constexpr int    kM1          = 31;                   // half-band half-length, stage 1 (63 taps, 1x<->2x)
constexpr int    kM2          = 11;                   // half-band half-length, stage 2 (23 taps, 2x<->4x)
constexpr int    kLatency     = kM1 + (kM2 + 1) / 2;  // 37 samples, constant for every oversampling mode
constexpr double kPi          = 3.14159265358979323846;

enum SatType   { SatTanh = 0, SatTube, SatFold, SatHard, SatDiode, NumSatTypes };
enum OsMode    { OsOff = 0, Os2x, Os4x };
enum LfoShape  { LfoSine = 0, LfoTri, LfoSawUp, LfoSawDown, LfoSquare, NumLfoShapes };
// LFO rate choices -> beats (quarter notes) per cycle: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 note
constexpr double kLfoBeats[6] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };

struct Params
{
    // input / split
    float inGainDb    = 0.0f;     // -24..+24
    float crossoverHz = 100.0f;   // 60..200
    float subLevelDb  = 0.0f;     // -48..+6 (-48 = off)
    bool  subMono     = true;     // sum the low band to mono
    // drive
    bool  driveOn     = true;
    float drive       = 0.5f;     // 0..1  (0 = bypass, up to +42 dB of pre-gain)
    int   satType     = 0;        // SatType
    float bias        = 0.0f;     // -1..1 asymmetry (DC offset into the shaper, removed afterwards)
    int   oversampling = 2;       // OsMode
    // formant
    bool  formantOn   = true;
    float vowel       = 0.4f;     // 0..1 morph  OO - OH - AH - EH - EE
    float formantShift = 0.0f;    // semitones, -12..+12
    float focus       = 0.5f;     // 0..1 formant sharpness
    float formantAmount = 1.0f;   // 0..1 blend dry high band -> formant output
    int   lfoRate     = 3;        // index into kLfoBeats
    int   lfoShape    = 0;
    float lfoDepth    = 0.0f;     // 0..1 (bipolar swing around the vowel knob; 0 = no modulation)
    // post filter
    bool  postOn      = false;
    int   postType    = 0;        // 0 lowpass, 1 highpass
    float postCutHz   = 9000.0f;  // 20..20000
    float postRes     = 0.707f;   // Q 0.5..10
    // out
    float mix         = 1.0f;     // 0..1
    float outGainDb   = 0.0f;     // -24..+12
    bool  limiter     = true;     // soft safety limiter (transparent below -1 dBFS)
};

struct Transport
{
    double bpm    = 120.0;
    double ppq    = 0.0;     // quarter-note position of first sample of block (valid if hasPpq)
    bool   hasPpq = false;   // host provides ppq and is playing
};

namespace detail
{
inline float sanitize (float x) noexcept
{
    if (! (std::fabs (x) <= 3.0e38f)) return 0.0f;   // NaN or +/-inf -> silence
    return std::fmax (-32.0f, std::fmin (32.0f, x)); // hard safety rail (~+30 dBFS)
}
inline float flush (float x) noexcept { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }

struct Smooth
{
    float cur = 0.f, target = 0.f, coef = 1.f;
    void set (float v) noexcept { target = v; }
    void snap (float v) noexcept { cur = target = v; }
    float next() noexcept { cur += (target - cur) * coef; return cur; }
};

// ---- TPT state-variable filter (Zavalishin) ----
struct Svf { float ic1 = 0.f, ic2 = 0.f; };
struct SvfCoef { float a1 = 0.f, a2 = 0.f, a3 = 0.f, k = 1.f; };
inline SvfCoef svfCoef (float fc, float q, float sr) noexcept
{
    fc = std::fmax (10.0f, std::fmin (fc, 0.45f * sr));
    const float g = std::tan ((float) kPi * fc / sr);
    SvfCoef c; c.k = 1.0f / q;
    c.a1 = 1.0f / (1.0f + g * (g + c.k)); c.a2 = g * c.a1; c.a3 = g * c.a2;
    return c;
}
inline void svfTick (Svf& s, const SvfCoef& c, float x, float& lp, float& bp, float& hp) noexcept
{
    const float v3 = x - s.ic2;
    const float v1 = c.a1 * s.ic1 + c.a2 * v3;
    const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
    lp = v2; bp = v1; hp = x - c.k * v1 - v2;
}

// ---- half-band polyphase oversampling (linear phase) ----
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0; const double q = x * x * 0.25;
    for (int k = 1; k < 60; ++k) { term *= q / ((double) k * k); sum += term; if (term < 1e-18 * sum) break; }
    return sum;
}
// c[i] = h[2i-M], i = 0..M : the non-zero (odd-m) taps of a Kaiser-windowed half band filter whose odd taps sum to 0.5
template <int M> struct HalfBand
{
    float c[M + 1] {};
    HalfBand()
    {
        const double beta = 7.9;   // ~80 dB stop band
        double h[M + 1], sum = 0.0;
        for (int i = 0; i <= M; ++i)
        {
            const double m = (double) (2 * i - M);
            const double r = m / (double) (M + 1);
            const double w = besselI0 (beta * std::sqrt (std::fmax (0.0, 1.0 - r * r))) / besselI0 (beta);
            h[i] = std::sin (kPi * m * 0.5) / (kPi * m) * w;
            sum += h[i];
        }
        for (int i = 0; i <= M; ++i) c[i] = (float) (h[i] * 0.5 / sum);
    }
};
template <int M> const HalfBand<M>& halfBand() { static const HalfBand<M> hb; return hb; }

template <int M> struct Up2      // 1 in -> 2 out (even, odd). Delay = M samples at the output rate.
{
    float x[M + 1] {};
    void reset() noexcept { std::memset (x, 0, sizeof (x)); }
    void process (float in, float& even, float& odd) noexcept
    {
        std::memmove (x + 1, x, sizeof (float) * M);
        x[0] = in;
        const float* c = halfBand<M>().c;
        float s = 0.f;
        for (int i = 0; i <= M; ++i) s += c[i] * x[i];
        even = 2.0f * s;
        odd  = x[(M - 1) / 2];
    }
};
template <int M> struct Down2    // 2 in (even, odd) -> 1 out
{
    float v[2 * M + 2] {};
    void reset() noexcept { std::memset (v, 0, sizeof (v)); }
    float process (float even, float odd) noexcept
    {
        std::memmove (v + 1, v, sizeof (float) * (2 * M + 1));
        v[0] = even;
        const float* c = halfBand<M>().c;
        float s = 0.5f * v[M];
        for (int i = 0; i <= M; ++i) s += c[i] * v[2 * i];
        std::memmove (v + 1, v, sizeof (float) * (2 * M + 1));
        v[0] = odd;
        return s;
    }
};

// One channel's oversampler. Latency (base-rate samples): off 0, 2x kM1, 4x kM1 + (kM2+1)/2.
struct Oversampler
{
    Up2<kM1> up1; Down2<kM1> dn1; Up2<kM2> up2; Down2<kM2> dn2; float d2 = 0.f;
    void reset() noexcept { up1.reset(); dn1.reset(); up2.reset(); dn2.reset(); d2 = 0.f; }
    static int latency (int mode) noexcept { return mode == Os2x ? kM1 : mode == Os4x ? kM1 + (kM2 + 1) / 2 : 0; }
    template <class F> float process (float x, int mode, F&& shape) noexcept
    {
        if (mode == OsOff) return shape (x);
        float e, o; up1.process (x, e, o);
        if (mode == Os2x) return dn1.process (shape (e), shape (o));
        float r[2], t[2] = { d2, e };           // one 2x-rate sample of extra delay keeps the total latency an integer
        d2 = o;
        for (int k = 0; k < 2; ++k)
        {
            float a, b; up2.process (t[k], a, b);
            r[k] = dn2.process (shape (a), shape (b));
        }
        return dn1.process (r[0], r[1]);
    }
};

struct Ring    // fixed delay line: push() then read (d) = sample pushed d calls ago
{
    float b[128] {}; int w = 0;
    void reset() noexcept { std::memset (b, 0, sizeof (b)); w = 0; }
    void push (float v) noexcept { b[w] = v; w = (w + 1) & 127; }
    float read (int d) const noexcept { return b[(w - 1 - d) & 127]; }
};

// ---- waveshapers (input pre-gained, unity slope at 0, output within about +/-1) ----
inline float shapeOne (int t, float x) noexcept
{
    switch (t)
    {
        case SatTanh:  return std::tanh (x);
        case SatTube:  return x >= 0.0f ? std::tanh (x) : std::tanh (1.5f * x) * (1.0f / 1.5f);          // asymmetric: even harmonics
        case SatFold:  return std::sin (x);                                                               // smooth wavefolder
        case SatHard:  return std::fmax (-1.0f, std::fmin (1.0f, x));
        default:       return x >= 0.0f ? 1.0f - std::exp (-x) : -0.5f * (1.0f - std::exp (2.0f * x));    // diode-like, strongly asymmetric
    }
}

inline float softLimit (float x) noexcept
{
    constexpr float t = 0.891f;   // -1 dBFS knee; exactly transparent below
    const float a = std::fabs (x);
    if (a <= t) return x;
    return std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), x);
}

constexpr float kFormantMakeup = 9.0f;

// vowel morph -> three formant centre frequencies / Q.  pos 0..1 : OO - OH - AH - EH - EE
inline void vowelFormants (float pos, float shiftSemi, float focus, float sr, float F[3], float Q[3]) noexcept
{
    static const float tab[5][3] = { { 300, 870, 2240 }, { 450, 800, 2830 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
    static const float qbase[3] = { 7.0f, 9.0f, 11.0f };
    pos = std::fmax (0.0f, std::fmin (1.0f, pos)) * 4.0f;
    const int i = std::min ((int) pos, 3); const float f = pos - (float) i;
    const float sh = std::exp2 (shiftSemi / 12.0f), qf = std::pow (3.0f, 2.0f * focus - 1.0f);
    for (int b = 0; b < 3; ++b)
    {
        const float fr = std::exp ((1.0f - f) * std::log (tab[i][b]) + f * std::log (tab[i + 1][b])) * sh;
        F[b] = std::fmax (50.0f, std::fmin (fr, 0.45f * sr));
        Q[b] = qbase[b] * qf;
    }
}
constexpr float kFormantWeight[3] = { 1.0f, 0.8f, 0.5f };
} // namespace detail
using detail::Svf; using detail::SvfCoef; using detail::Oversampler; using detail::Ring;

class Engine
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/, int numChannels)
    {
        sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
        nch = std::max (1, std::min (numChannels, kMaxChannels));
        const float coef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));    // 20 ms
        for (auto* s : { &sIn, &sXover, &sSub, &sDrive, &sBias, &sVowel, &sShift, &sFocus, &sFAmt, &sDepth, &sPostCut, &sPostRes, &sMix, &sOut })
            s->coef = coef;
        const float swCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr)); // 5 ms: section switches and type cross-fades
        for (auto* s : { &sMono, &sDriveOn, &sFormOn, &sPostOn, &sPostHp, &sLim, &sLfoOut })
            s->coef = swCoef;
        sLfoOut.coef = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));      // 1.5 ms LFO de-zipper
        gateCoef = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
        typeStep = 1.0f / std::fmax (1.0f, 0.008f * (float) sr);
        dcR = 1.0f - 2.0f * (float) kPi * 12.0f / (float) sr;
        holdLen = std::max (64, (int) (0.004 * sr));
        reset();
        snapSmoothers();
    }

    void reset()
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            xo[c] = {}; ch[c] = {};
            ch[c].os.reset(); ch[c].dry.reset(); ch[c].low.reset(); ch[c].hd.reset(); ch[c].pad.reset();
        }
        ctl = 0; freePpq = 0.0; osCur = osReq; gate = 1.0f; gateTarget = 1.0f; hold = 0;
        vowelNow = P.vowel;
    }

    void setParams (const Params& p)
    {
        P = p;
        P.satType = std::max (0, std::min (P.satType, (int) NumSatTypes - 1));
        P.oversampling = std::max (0, std::min (P.oversampling, 2));
        P.lfoRate = std::max (0, std::min (P.lfoRate, 5));
        P.lfoShape = std::max (0, std::min (P.lfoShape, (int) NumLfoShapes - 1));
        osReq = P.oversampling;
        if (P.satType != typeTo) { typeFrom = typeTo; typeTo = P.satType; xf = 0.0f; xfading = true; }
        sIn.set (std::pow (10.0f, std::fmax (-24.f, std::fmin (24.f, P.inGainDb)) / 20.0f));
        sXover.set (std::log (std::fmax (60.f, std::fmin (200.f, P.crossoverHz))));
        sSub.set (P.subLevelDb <= -47.9f ? 0.0f : std::pow (10.0f, std::fmin (6.f, P.subLevelDb) / 20.0f));
        sMono.set (P.subMono ? 1.0f : 0.0f);
        sDriveOn.set (P.driveOn ? 1.0f : 0.0f);
        sDrive.set (std::fmax (0.f, std::fmin (1.f, P.drive)));
        sBias.set (std::fmax (-1.f, std::fmin (1.f, P.bias)));
        sFormOn.set (P.formantOn ? 1.0f : 0.0f);
        sVowel.set (std::fmax (0.f, std::fmin (1.f, P.vowel)));
        sShift.set (std::fmax (-12.f, std::fmin (12.f, P.formantShift)));
        sFocus.set (std::fmax (0.f, std::fmin (1.f, P.focus)));
        sFAmt.set (std::fmax (0.f, std::fmin (1.f, P.formantAmount)));
        sDepth.set (std::fmax (0.f, std::fmin (1.f, P.lfoDepth)));
        sPostOn.set (P.postOn ? 1.0f : 0.0f);
        sPostHp.set (P.postType == 1 ? 1.0f : 0.0f);
        sPostCut.set (std::log (std::fmax (20.f, std::fmin (20000.f, P.postCutHz))));
        sPostRes.set (std::fmax (0.5f, std::fmin (10.f, P.postRes)));
        sMix.set (std::fmax (0.f, std::fmin (1.f, P.mix)));
        sOut.set (std::pow (10.0f, std::fmax (-24.f, std::fmin (12.f, P.outGainDb)) / 20.0f));
        sLim.set (P.limiter ? 1.0f : 0.0f);
    }

    void snapSmoothers()
    {
        for (auto* s : { &sIn, &sXover, &sSub, &sMono, &sDriveOn, &sDrive, &sBias, &sFormOn, &sVowel, &sShift, &sFocus, &sFAmt, &sDepth,
                         &sPostOn, &sPostHp, &sPostCut, &sPostRes, &sMix, &sOut, &sLim })
            s->snap (s->target);
        sLfoOut.snap (0.0f);
        typeFrom = typeTo = P.satType; xfading = false; xf = 1.0f;
        osCur = osReq; gate = gateTarget = 1.0f; hold = 0;
        vowelNow = sVowel.cur;
        ctl = 0;
    }

    float currentVowel() const noexcept { return vowelNow; }   // vowel morph incl. LFO (for the UI)
    int   latencySamples() const noexcept { return kLatency; }

    void process (float* const* io, int numChannels, int numSamples, const Transport& tp) noexcept
    {
        using namespace detail;
        const int n = std::min (numChannels, nch);
        const double beatsPerCycle = kLfoBeats[P.lfoRate];
        const double bpm = (tp.bpm > 10.0 && tp.bpm < 1000.0) ? tp.bpm : 120.0;
        const double ppqPerSample = bpm / 60.0 / sr;
        double ppq = tp.hasPpq ? tp.ppq : freePpq;
        const int shapeMode = P.lfoShape;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- smoothers ----
            const float gIn = sIn.next(), subG = sSub.next(), mono = sMono.next(), driveOnA = sDriveOn.next(), drv = sDrive.next(), bias = sBias.next();
            const float formOn = sFormOn.next(), fAmt = sFAmt.next(), depth = sDepth.next(), vow = sVowel.next();
            const float postOn = sPostOn.next(), postHp = sPostHp.next(), mix = sMix.next(), outG = sOut.next(), lim = sLim.next();
            sXover.next(); sShift.next(); sFocus.next(); sPostCut.next(); sPostRes.next();

            // ---- LFO -> vowel morph ----
            {
                double ph = ppq / beatsPerCycle; ph -= std::floor (ph);
                const float p = (float) ph; float l;
                switch (shapeMode)
                {
                    case LfoSine:    l = std::sin (2.0f * (float) kPi * p); break;
                    case LfoTri:     { float q = p + 0.25f; q -= std::floor (q); l = 1.0f - 4.0f * std::fabs (q - 0.5f); } break;
                    case LfoSawUp:   l = 2.0f * p - 1.0f; break;
                    case LfoSawDown: l = 1.0f - 2.0f * p; break;
                    default:         l = p < 0.5f ? 1.0f : -1.0f; break;
                }
                sLfoOut.set (l);
                vowelNow = std::fmax (0.0f, std::fmin (1.0f, vow + depth * sLfoOut.next()));
                ppq += ppqPerSample;
            }

            // ---- control-rate coefficient refresh ----
            if (ctl == 0)
            {
                ctl = kCtlInterval;
                const float fx = std::exp (sXover.cur);
                xoCoef[0] = svfCoef (fx, 0.54119610f, (float) sr); xoCoef[1] = svfCoef (fx, 1.30656296f, (float) sr);   // 2 x Butterworth-4 = Linkwitz-Riley 8 (48 dB/oct)
                float F[3], Q[3];
                vowelFormants (vowelNow, sShift.cur, sFocus.cur, (float) sr, F, Q);
                for (int b = 0; b < 3; ++b) { fmCoef[b] = svfCoef (F[b], Q[b], (float) sr); fmW[b] = kFormantWeight[b] * fmCoef[b].k * kFormantMakeup; }
                postCoef = svfCoef (std::exp (sPostCut.cur), sPostRes.cur, (float) sr);
                for (int c = 0; c < kMaxChannels; ++c)
                {
                    for (auto& s : xo[c].lp) { s.ic1 = flush (s.ic1); s.ic2 = flush (s.ic2); }
                    for (auto& s : xo[c].hp) { s.ic1 = flush (s.ic1); s.ic2 = flush (s.ic2); }
                    ch[c].post.ic1 = flush (ch[c].post.ic1); ch[c].post.ic2 = flush (ch[c].post.ic2);
                    for (auto& s : ch[c].fm) { s.ic1 = flush (s.ic1); s.ic2 = flush (s.ic2); }
                }
            }
            --ctl;

            // ---- oversampling-mode switch: mute the high band, swap, un-mute ----
            if (osReq != osCur || hold > 0)
            {
                gateTarget = 0.0f;
                if (osReq != osCur && gate < 0.002f)
                {
                    osCur = osReq; hold = holdLen;
                    for (int c = 0; c < kMaxChannels; ++c) { ch[c].os.reset(); ch[c].hd.reset(); ch[c].pad.reset(); }
                }
                else if (hold > 0 && osReq == osCur && --hold == 0) gateTarget = 1.0f;
            }
            else gateTarget = 1.0f;
            gate += (gateTarget - gate) * gateCoef;

            if (xfading) { xf += typeStep; if (xf >= 1.0f) { xf = 1.0f; xfading = false; typeFrom = typeTo; } }
            const int tA = typeFrom, tB = typeTo; const float xfv = xf; const bool xfa = xfading;

            const float preG   = std::pow (10.0f, 42.0f * drv / 20.0f);
            const float blend  = driveOnA * std::fmin (1.0f, drv * 20.0f);
            const int   Lmode  = Oversampler::latency (osCur);
            const int   padDly = kLatency - Lmode;
            const float biasOff = 0.6f * bias;
            const float gateV  = gate;

            // ---- stage A: split, low band, drive ----
            float lowC[kMaxChannels] {}, hiC[kMaxChannels] {};
            for (int c = 0; c < n; ++c)
            {
                auto& C = ch[c];
                const float x0 = sanitize (io[c][i]);
                C.dry.push (x0);
                const float x = x0 * gIn;
                float lp = x, hp = x, bp, t;
                for (int k = 0; k < 4; ++k) { svfTick (xo[c].lp[k], xoCoef[k & 1], lp, lp, bp, t); }
                for (int k = 0; k < 4; ++k) { svfTick (xo[c].hp[k], xoCoef[k & 1], hp, t, bp, hp); }
                lowC[c] = lp; hiC[c] = hp;
            }
            float lowM = lowC[0];
            if (n == 2) lowM = 0.5f * (lowC[0] + lowC[1]);

            for (int c = 0; c < n; ++c)
            {
                auto& C = ch[c];
                // low band: clean, optionally mono, level; delayed to the common latency
                C.low.push ((lowC[c] + mono * (lowM - lowC[c])) * subG);
                const float lowD = C.low.read (kLatency);

                // high band
                const float hi = hiC[c];
                C.hd.push (hi);
                const float hd = C.hd.read (Lmode);
                const float v = hi * preG + biasOff;
                float y = C.os.process (v, osCur, [&] (float s) noexcept
                {
                    const float a = shapeOne (tA, s);
                    return xfa ? a + xfv * (shapeOne (tB, s) - a) : a;
                });
                const float dcy = y - C.dcX + dcR * C.dcY; C.dcX = y; C.dcY = dcy;       // remove the DC the bias / asymmetric shapers create
                const float nl = hd + blend * (dcy - hd);
                C.pad.push (nl);
                const float hp0 = C.pad.read (padDly);

                // formant bank (3 parallel band-passes)
                float bpSum = 0.0f, l, b, h;
                for (int k = 0; k < 3; ++k) { svfTick (C.fm[k], fmCoef[k], hp0, l, b, h); bpSum += fmW[k] * b; }
                const float fa = formOn * fAmt;
                const float fo = hp0 + fa * (bpSum - hp0);

                // post filter (LP/HP cross-faded)
                float pl, pb, ph2; svfTick (C.post, postCoef, fo, pl, pb, ph2);
                const float pf = pl + postHp * (ph2 - pl);
                const float po = fo + postOn * (pf - fo);

                const float wet = lowD + po * gateV;
                const float dryD = C.dry.read (kLatency);
                float o = (dryD * (1.0f - mix) + wet * mix) * outG;
                o = o + lim * (softLimit (o) - o);
                io[c][i] = sanitize (o);
            }
        }
        if (! tp.hasPpq) freePpq = ppq;
    }

private:
    struct Xo { Svf lp[4], hp[4]; };
    struct Ch
    {
        Oversampler os; Ring dry, low, hd, pad; Svf fm[3]; Svf post; float dcX = 0.f, dcY = 0.f;
    };

    double sr = 44100.0; int nch = 2;
    Params P;
    detail::Smooth sIn, sXover, sSub, sMono, sDriveOn, sDrive, sBias, sFormOn, sVowel, sShift, sFocus, sFAmt, sDepth,
                   sPostOn, sPostHp, sPostCut, sPostRes, sMix, sOut, sLim, sLfoOut;
    detail::SvfCoef xoCoef[2], fmCoef[3], postCoef; float fmW[3] {};
    Xo xo[kMaxChannels]; Ch ch[kMaxChannels];
    int ctl = 0; double freePpq = 0.0; float vowelNow = 0.4f;
    int osCur = 0, osReq = 0, hold = 0, holdLen = 192; float gate = 1.0f, gateTarget = 1.0f, gateCoef = 0.01f;
    int typeFrom = 0, typeTo = 0; float xf = 1.0f, typeStep = 0.01f; bool xfading = false;
    float dcR = 0.999f;
};
} // namespace gw
