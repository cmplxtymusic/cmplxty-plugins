// Drift DSP core - plain C++17, no JUCE dependency.
//
// A selective-band frequency shifter (single-sideband modulation).
//
// Signal flow (per channel, stereo max):
//   in -> 3-way Linkwitz-Riley split (xoLow / xoHigh, 24/48/96 dB per octave, allpass-compensated so low+mid+high is flat)
//       low band / high band : pass through (a removed band is merged into the middle band, a soloed band is auditioned alone)
//       mid band             : [+ feedback] -> Hilbert allpass pair (I, Q) -> multiply by cos/sin of the shift phase -> shifted signal
//   Every band also runs through the same "I" allpass chain, so that at zero shift the whole thing is a pure allpass (flat).
//   out = low + high + ( (1-mix) * dry_mid + mix * shifted_mid )
//
// If only one band is left the shifter acts on the full signal (that falls out naturally: the removed bands are merged into the middle one).
//
// The Hilbert transformer is a pair of IIR allpass chains (7 sections each, designed with tools/design_hilbert.py): zero latency,
// 0.02 degree phase error from about 0.0004 x fs (19 Hz at 48 kHz) up to the mirror frequency. Below that the image sideband
// rejection falls off, as it does in every Hilbert based shifter.
//
// All buffers are fixed-size members: process() never allocates. Everything is block-size independent: smoothers run per sample,
// filter coefficients are refreshed every kCtlInterval samples on a counter that survives block boundaries.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace df
{
constexpr int    kMaxChannels = 2;
constexpr int    kCtlInterval = 8;     // coefficient refresh period (samples)
constexpr int    kLatency     = 0;     // the allpass Hilbert pair has no latency
constexpr double kPi          = 3.14159265358979323846;

// ---- Hilbert allpass pair, coefficients from tools/design_hilbert.py 7 0.0004 ----
// N=7 sections per path, band 0.0004 .. 0.4996 x fs, worst phase error 0.0206 deg  (image rejection ~74.9 dB)
constexpr int kHilbertN = 7;
constexpr double kHilbertI[7] = { 0.4681394417825615, 0.7668394303461943, 0.9081386682896411, 0.9654465855882227, 0.9873768816747949, 0.9957156711413337, 0.9993320078285751 };
constexpr double kHilbertQ[7] = { 0.2489527942875909, 0.6413745183281527, 0.8523066503771626, 0.9434676345672638, 0.9790296700631729, 0.9925058392278172, 0.9978149079794422 };

// allpass section H(z) = (c - z^-2) / (1 - c z^-2), c = a^2, transposed direct form II (double precision: poles sit close to the unit circle)
struct HilbertChain
{
    double c[kHilbertN] {};
    double s1[kHilbertN] {}, s2[kHilbertN] {};
    double delay = 0.0;     // the 'I' path carries a one-sample delay
    bool   useDelay = false;
    void init (const double* coefs, bool withDelay) { for (int i = 0; i < kHilbertN; ++i) c[i] = coefs[i] * coefs[i]; useDelay = withDelay; reset(); }
    void reset() { for (int i = 0; i < kHilbertN; ++i) s1[i] = s2[i] = 0.0; delay = 0.0; }
    inline double tick (double x)
    {
        for (int i = 0; i < kHilbertN; ++i)
        {
            const double y = c[i] * x + s1[i];
            s1[i] = s2[i];
            s2[i] = c[i] * y - x;
            x = y;
        }
        if (useDelay) { const double o = delay; delay = x; return o; }
        return x;
    }
};

// ---- Linkwitz-Riley crossover (cascade of TPT state-variable sections) ----
constexpr int kMaxSections = 8;
enum Slope { Slope24 = 0, Slope48, Slope96, NumSlopes };
constexpr const char* kSlopeNames[NumSlopes] = { "24 dB", "48 dB", "96 dB" };

// section Qs of the squared Butterworth (LR) response: LR4 = 2 x BW2, LR8 = 2 x BW4, LR16 = 2 x BW8
inline int slopeSections (int slope) { return slope == Slope24 ? 2 : slope == Slope48 ? 4 : 8; }
inline double slopeQ (int slope, int i)
{
    static const double bw2[2] = { 0.70710678118654752, 0.70710678118654752 };
    static const double bw4[4] = { 0.54119610014619698, 1.3065629648763766, 0.54119610014619698, 1.3065629648763766 };
    static const double bw8[8] = { 0.50979557910415916, 0.60134488693504529, 0.89997622313641570, 2.5629154477415055,
                                   0.50979557910415916, 0.60134488693504529, 0.89997622313641570, 2.5629154477415055 };
    return slope == Slope24 ? bw2[i] : slope == Slope48 ? bw4[i] : bw8[i];
}

struct SvfCoef { double a1 = 0, a2 = 0, a3 = 0, k = 1.4142; };
struct XoverCoefs
{
    SvfCoef s[kMaxSections];
    int     n = 4;
    void set (double fc, double sr, int slope)
    {
        n = slopeSections (slope);
        fc = std::min (fc, sr * 0.45);
        const double g = std::tan (kPi * fc / sr);
        for (int i = 0; i < n; ++i)
        {
            const double k = 1.0 / slopeQ (slope, i);
            s[i].k = k; s[i].a1 = 1.0 / (1.0 + g * (g + k)); s[i].a2 = g * s[i].a1; s[i].a3 = g * s[i].a2;
        }
    }
};
struct SvfState
{
    double ic1[kMaxSections] {}, ic2[kMaxSections] {};
    void reset() { for (int i = 0; i < kMaxSections; ++i) ic1[i] = ic2[i] = 0.0; }
    // low-pass cascade
    inline double lp (const XoverCoefs& c, double x)
    {
        for (int i = 0; i < c.n; ++i)
        {
            const SvfCoef& k = c.s[i];
            const double v3 = x - ic2[i];
            const double v1 = k.a1 * ic1[i] + k.a2 * v3;
            const double v2 = ic2[i] + k.a2 * ic1[i] + k.a3 * v3;
            ic1[i] = 2.0 * v1 - ic1[i]; ic2[i] = 2.0 * v2 - ic2[i];
            x = v2;
        }
        return x;
    }
    // high-pass cascade
    inline double hp (const XoverCoefs& c, double x)
    {
        for (int i = 0; i < c.n; ++i)
        {
            const SvfCoef& k = c.s[i];
            const double v3 = x - ic2[i];
            const double v1 = k.a1 * ic1[i] + k.a2 * v3;
            const double v2 = ic2[i] + k.a2 * ic1[i] + k.a3 * v3;
            ic1[i] = 2.0 * v1 - ic1[i]; ic2[i] = 2.0 * v2 - ic2[i];
            x = x - k.k * v1 - v2;
        }
        return x;
    }
};

// ---- parameters (plain values, already in user units) ----
struct Params
{
    float coarseHz   = 0.0f;    // -10000..10000
    float fineHz     = 0.0f;    // -500..500
    bool  shiftOn    = true;
    float spreadHz   = 0.0f;    // -500..500 : L gets -spread/2, R gets +spread/2 (only when wide)
    bool  wide       = true;
    float mix        = 1.0f;    // 0..1 (applies to the shifted band)
    float feedback   = 0.0f;    // 0..0.95
    bool  keyOn      = false;
    float keyHzPerSemi = 10.0f; // -100..100 Hz per semitone away from MIDI note 60
    bool  lowBand    = true;    // false = the low band is removed (merged into the middle band)
    bool  highBand   = true;
    float xoLowHz    = 200.0f;  // 40..8000
    float xoHighHz   = 4000.0f; // 80..16000, kept at least 1.25x above xoLow
    int   slope      = Slope48;
    bool  soloLow = false, soloMid = false, soloHigh = false;
};

// ---- lock-free sample FIFO feeding the spectrum display (audio thread writes, GUI thread reads the newest N samples) ----
class SampleTap
{
public:
    static constexpr int kSize = 1 << 14;
    void push (float v) noexcept { const uint32_t w = widx.load (std::memory_order_relaxed); buf[w & (kSize - 1)].store (v, std::memory_order_relaxed); widx.store (w + 1, std::memory_order_release); }
    // copies the newest n samples (oldest first); returns false until n samples have been written
    bool latest (float* dst, int n) const noexcept
    {
        const uint32_t w = widx.load (std::memory_order_acquire);
        if (w < (uint32_t) n || n > kSize) return false;
        for (int i = 0; i < n; ++i) dst[i] = buf[(w - (uint32_t) n + (uint32_t) i) & (kSize - 1)].load (std::memory_order_relaxed);
        return true;
    }
private:
    std::array<std::atomic<float>, kSize> buf {};
    std::atomic<uint32_t> widx { 0 };
};

// ---- the engine ----
class Engine
{
public:
    Engine() { for (int c = 0; c < kMaxChannels; ++c) { ch[c].hi.init (kHilbertI, true); ch[c].hq.init (kHilbertQ, false);
                                                      ch[c].lowI.init (kHilbertI, true); ch[c].highI.init (kHilbertI, true); ch[c].dryI.init (kHilbertI, true); } }

    void prepare (double sampleRate, int channels)
    {
        sr = std::max (8000.0, sampleRate); nch = std::max (1, std::min (kMaxChannels, channels));
        auto tc = [this] (double sec) { return 1.0 - std::exp (-1.0 / (sec * sr)); };
        aShift = tc (0.015); aGain = tc (0.010); aXo = tc (0.015);
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { c.hi.reset(); c.hq.reset(); c.lowI.reset(); c.highI.reset(); c.dryI.reset();
                             c.resetFilters(); c.fbState = 0.0; c.phase = 0.0; c.shiftCur = 0.0; }
        // jump smoothers to their targets
        keyCur = 0.0;
        snap = true; heldCount = 0; ctlCounter = 0;
    }

    void setParams (const Params& p) { target = p; }

    // MIDI (monophonic, last note wins; released keys fall back to the previous held key, then to no offset)
    void noteOn (int note)
    {
        note = std::max (0, std::min (127, note));
        noteOff (note);
        if (heldCount < (int) held.size()) held[(size_t) heldCount++] = note; else { for (int i = 1; i < heldCount; ++i) held[(size_t) i - 1] = held[(size_t) i]; held[(size_t) heldCount - 1] = note; }
    }
    void noteOff (int note)
    {
        int j = 0; for (int i = 0; i < heldCount; ++i) if (held[(size_t) i] != note) held[(size_t) j++] = held[(size_t) i];
        heldCount = j;
    }
    void allNotesOff() { heldCount = 0; }
    int  currentNote() const { return heldCount > 0 ? held[(size_t) heldCount - 1] : -1; }

    // The spectrum taps are only fed while a display is open
    void setDisplayActive (bool on) noexcept { displayActive.store (on, std::memory_order_relaxed); }
    const SampleTap& inTap()  const noexcept { return tapIn; }
    const SampleTap& outTap() const noexcept { return tapOut; }
    double sampleRate() const noexcept { return sr; }

    // in-place processing
    void process (float* const* chans, int numChans, int n)
    {
        const int nc = std::min (numChans, nch);
        const bool tapOn = displayActive.load (std::memory_order_relaxed);
        const Params& p = target;

        // block-constant targets
        const bool lowOn = p.lowBand, highOn = p.highBand;
        const bool sLow = p.soloLow && lowOn, sMid = p.soloMid, sHigh = p.soloHigh && highOn;
        const bool anySolo = sLow || sMid || sHigh;
        const double tgtWl = lowOn ? 1.0 : 0.0, tgtWh = highOn ? 1.0 : 0.0;
        const double tgtGl = anySolo ? (sLow ? 1.0 : 0.0) : 1.0, tgtGm = anySolo ? (sMid ? 1.0 : 0.0) : 1.0, tgtGh = anySolo ? (sHigh ? 1.0 : 0.0) : 1.0;
        const double tgtMix = p.shiftOn ? (double) std::clamp (p.mix, 0.0f, 1.0f) : 0.0;
        const double tgtFb  = p.shiftOn ? (double) std::clamp (p.feedback, 0.0f, 0.95f) : 0.0;
        const double xoL = std::clamp ((double) p.xoLowHz, 20.0, sr * 0.2);
        const double xoH = std::clamp (std::max ((double) p.xoHighHz, xoL * 1.25), 40.0, sr * 0.42);
        const double keyTarget = (p.keyOn && heldCount > 0) ? (double) p.keyHzPerSemi * (double) (std::clamp (held[(size_t) heldCount - 1], 0, 96) - 60) : 0.0;
        const double maxShift = sr * 0.48;
        const double base = std::clamp ((double) p.coarseHz + (double) p.fineHz, -maxShift, maxShift);
        const double half = p.wide ? (double) p.spreadHz * 0.5 : 0.0;
        const double sgn[2] = { -1.0, 1.0 };

        if (snap)
        {
            wl = tgtWl; wh = tgtWh; gl = tgtGl; gm = tgtGm; gh = tgtGh; mixC = tgtMix; fbC = tgtFb; xoLc = xoL; xoHc = xoH; keyCur = keyTarget;
            for (int c = 0; c < kMaxChannels; ++c)
            {
                const double s = (nch == 1) ? 0.0 : sgn[c] * half;
                ch[c].shiftCur = std::clamp (base + keyTarget + s, -maxShift, maxShift);
            }
            slopeCur = p.slope; coefL.set (xoLc, sr, slopeCur); coefH.set (xoHc, sr, slopeCur); snap = false;
        }
        if (p.slope != slopeCur) { slopeCur = p.slope; for (auto& c : ch) c.resetFilters(); coefL.set (xoLc, sr, slopeCur); coefH.set (xoHc, sr, slopeCur); }

        for (int i = 0; i < n; ++i)
        {
            wl += aGain * (tgtWl - wl); wh += aGain * (tgtWh - wh);
            gl += aGain * (tgtGl - gl); gm += aGain * (tgtGm - gm); gh += aGain * (tgtGh - gh);
            mixC += aGain * (tgtMix - mixC); fbC += aGain * (tgtFb - fbC);
            keyCur += aShift * (keyTarget - keyCur);
            xoLc += aXo * (xoL - xoLc); xoHc += aXo * (xoH - xoHc);
            if (++ctlCounter >= kCtlInterval) { ctlCounter = 0; coefL.set (xoLc, sr, slopeCur); coefH.set (xoHc, sr, slopeCur); }

            double inMono = 0.0, outMono = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                Chan& s = ch[c];
                const double x = (double) chans[c][i];
                // split
                const double lpH = s.hLp.lp (coefH, x);
                const double hpH = s.hHp.hp (coefH, x);
                const double low = s.lLp.lp (coefL, lpH);
                const double mid = s.lHp.hp (coefL, lpH);
                const double highAp = s.aLp.lp (coefL, hpH) + s.aHp.hp (coefL, hpH);   // allpass-compensates the high band against the low/mid split
                // band routing: removed bands merge into the middle one
                const double lowU = wl * low, highU = wh * highAp;
                const double midU = mid + (1.0 - wl) * low + (1.0 - wh) * highAp;
                // shift
                const double fb = fbC * softClip (s.fbState);
                const double midIn = midU + fb;
                const double I = s.hi.tick (midIn), Q = s.hq.tick (midIn);
                const double spreadOff = nch == 1 ? 0.0 : sgn[c] * half;
                const double wantF = std::clamp (base + keyCur + spreadOff, -maxShift, maxShift);
                s.shiftCur += aShift * (wantF - s.shiftCur);
                s.phase += 2.0 * kPi * s.shiftCur / sr;
                if (s.phase > kPi) s.phase -= 2.0 * kPi; else if (s.phase < -kPi) s.phase += 2.0 * kPi;
                // I + jQ is analytic with a negative-frequency image; I*cos + Q*sin moves every component up by shiftCur (down when negative)
                const double S = I * std::cos (s.phase) + Q * std::sin (s.phase);
                s.fbState = S;
                const double dry = s.dryI.tick (midU);
                const double lowO = s.lowI.tick (lowU), highO = s.highI.tick (highU);
                const double y = gl * lowO + gh * highO + gm * ((1.0 - mixC) * dry + mixC * S);
                chans[c][i] = (float) y;
                inMono += x; outMono += y;
            }
            if (tapOn) { const double k = 1.0 / std::max (1, nc); tapIn.push ((float) (inMono * k)); tapOut.push ((float) (outMono * k)); }
        }
        // channels beyond what we process (should not happen) are left untouched
    }

private:
    static inline double softClip (double x)
    {
        const double a = std::fabs (x);
        if (a <= 0.8) return x;
        const double y = 0.8 + 0.2 * std::tanh ((a - 0.8) / 0.2);
        return x < 0 ? -y : y;
    }

    struct Chan
    {
        HilbertChain hi, hq, lowI, highI, dryI;
        SvfState hLp, hHp, lLp, lHp, aLp, aHp;
        double fbState = 0.0, phase = 0.0, shiftCur = 0.0;
        void resetFilters() { hLp.reset(); hHp.reset(); lLp.reset(); lHp.reset(); aLp.reset(); aHp.reset(); }
    };
    Chan ch[kMaxChannels];
    XoverCoefs coefL, coefH;
    Params target;
    double sr = 48000.0, aShift = 0.001, aGain = 0.002, aXo = 0.001;
    double wl = 1, wh = 1, gl = 1, gm = 1, gh = 1, mixC = 1, fbC = 0, xoLc = 200, xoHc = 4000, keyCur = 0;
    int nch = 2, slopeCur = Slope48, ctlCounter = 0, heldCount = 0;
    bool snap = true;
    std::array<int, 16> held {};
    std::atomic<bool> displayActive { false };
    SampleTap tapIn, tapOut;
};

// ---- FFT + spectrum analysis for the display ----
class Fft
{
public:
    void init (int log2n)
    {
        order = log2n; n = 1 << log2n;
        cosT.resize ((size_t) n / 2); sinT.resize ((size_t) n / 2); rev.resize ((size_t) n);
        for (int i = 0; i < n / 2; ++i) { cosT[(size_t) i] = (float) std::cos (2.0 * kPi * i / n); sinT[(size_t) i] = (float) -std::sin (2.0 * kPi * i / n); }
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < log2n; ++b) if (i & (1 << b)) r |= 1 << (log2n - 1 - b); rev[(size_t) i] = r; }
    }
    int size() const { return n; }
    void forward (float* re, float* im) const
    {
        for (int i = 0; i < n; ++i) { const int j = rev[(size_t) i]; if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int halfLen = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < halfLen; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + halfLen;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
        }
    }
private:
    int order = 0, n = 0;
    std::vector<float> cosT, sinT; std::vector<int> rev;
};

class SpectrumAnalyzer
{
public:
    void init (int log2n)
    {
        fft.init (log2n); const int n = fft.size();
        win.resize ((size_t) n); re.resize ((size_t) n); im.resize ((size_t) n);
        double sum = 0; for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n); win[(size_t) i] = (float) w; sum += w; }
        norm = (float) (2.0 / sum);
    }
    int size() const { return fft.size(); }
    int bins() const { return fft.size() / 2; }
    // in: size() samples; out: bins() magnitudes in dB (0 dB = full-scale sine)
    void analyse (const float* in, float* outDb)
    {
        const int n = fft.size();
        for (int i = 0; i < n; ++i) { re[(size_t) i] = in[i] * win[(size_t) i]; im[(size_t) i] = 0.0f; }
        fft.forward (re.data(), im.data());
        for (int i = 0; i < n / 2; ++i) { const float m = std::sqrt (re[(size_t) i] * re[(size_t) i] + im[(size_t) i] * im[(size_t) i]) * norm; outDb[i] = 20.0f * std::log10 (m + 1.0e-9f); }
    }
private:
    Fft fft; std::vector<float> win, re, im; float norm = 1.0f;
};
} // namespace df
