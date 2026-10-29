// ALTER voice engine: YIN pitch tracker + TD-PSOLA pitch/formant shifter + quantizer/robot + tube drive.
// Own implementation; no third-party code. Mono in, mono out, fixed latency (see latency()).
#pragma once
#include "Dsp.h"

namespace qz
{
struct AlterControls
{
    int   mode = 0;          // 0 Transpose, 1 Quantize, 2 Robot
    float pitchSt = 0;       // knob value, semitones
    float formantSt = 0;     // knob value, semitones
    bool  link = false;
    float gender = 0;        // -100..100
    float speedMs = 12;      // retune time (Quantize)
    int   key = 12;          // 0..11 = C..B, 12 = "--" (no quantizing)
    int   scale = 7;         // 0..6 = scales, 7 = "--" (no quantizing)
    bool quantOff() const { return key < 0 || key > 11 || scale < 0 || scale > 6; }
};

// GENDER knob (-100..100) -> pitch / formant contribution. Soft-limited so the extremes stay natural:
// feminine end about +5.5 st pitch and +2.9 st (about +18 %) formant, masculine end about -4.5 st / -2.6 st (about -14 %).
inline float genderNorm (float gender) { const float g = clampf (gender * 0.01f, -1.0f, 1.0f); return std::tanh (g) / 0.76159416f; }
inline float genderPitchSt (float gender) { const float t = genderNorm (gender); return t >= 0 ? 5.5f * t : 4.5f * t; }
inline float genderFormantSt (float gender) { const float t = genderNorm (gender); return t >= 0 ? 2.9f * t : 2.6f * t; }

inline bool scaleAllows (int scale, int degree)   // degree = semitones above key, 0..11
{
    static const int masks[7] = {
        0xFFF,                                   // chromatic
        (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),   // major
        (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),   // natural minor
        (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),   // harmonic minor
        (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                   // major pentatonic
        (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),                  // minor pentatonic
        (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10) };         // blues
    return (masks[scale < 0 ? 0 : (scale > 6 ? 6 : scale)] >> (((degree % 12) + 12) % 12)) & 1;
}

// nearest allowed integer note to midi (float)
inline int snapToScale (float midi, int key, int scale)
{
    const int c = (int) std::floor (midi + 0.5f);
    for (int d = 0; d <= 12; ++d)
    {
        const bool up = scaleAllows (scale, c + d - key), dn = scaleAllows (scale, c - d - key);
        if (up && dn) return (std::fabs (midi - (float) (c + d)) <= std::fabs (midi - (float) (c - d))) ? c + d : c - d;
        if (up) return c + d;
        if (dn) return c - d;
    }
    return c;
}

class AlterEngine
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        D = (int) std::lround (0.044 * sampleRate);
        decim = std::max (1, (int) std::lround (sampleRate / 16000.0));
        fsd = fs / (float) decim;
        wd = (int) std::lround (0.020f * fsd);
        tauMin = std::max (2, (int) std::floor (fsd / 600.0f));
        tauMax = (int) std::ceil (fsd / 65.0f);
        span = wd + tauMax + 2;
        hop = std::max (32, (int) std::lround (0.005 * sampleRate));
        unvPeriod = 0.008f * fs;
        maxHalf = (int) std::lround (0.017f * fs);
        maxSrcHalf = 0.018f * fs;
        int n = 1; while (n < (int) (fs * 0.5f)) n <<= 1;
        inMask = n - 1; inBuf.assign ((size_t) n, 0.0f);
        int m = 1; while (m < (int) (fs * 0.25f)) m <<= 1;
        outMask = m - 1; outBuf.assign ((size_t) m, 0.0f);
        int dn = 1; while (dn < span * 2 + 8) dn <<= 1;
        decMask = dn - 1; decBuf.assign ((size_t) dn, 0.0f);
        scratch.assign ((size_t) span, 0.0f);
        diff.assign ((size_t) tauMax + 2, 0.0f);
        cmnd.assign ((size_t) tauMax + 2, 0.0f);
        driveDC.setup (fs);
        cChest.set (330.0f, 0.7f, fs); cChestM.set (250.0f, 0.7f, fs); cPres.set (3000.0f, 0.9f, fs);
        cAir.set (4500.0f, 0.7071f, fs); cShelf.set (7000.0f, 0.7071f, fs);
        reset();
    }

    int latency() const { return D; }

    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        std::fill (decBuf.begin(), decBuf.end(), 0.0f);
        n = 0; k = 0; dCount = 0; decPhase = 0; hopCount = 0;
        trackN = 0; markN = 0; hasMark = false; sNext = 0; haveMidi = false; lastNote = -1000;
        lpA.reset(); lpB.reset(); f0hist[0] = f0hist[1] = f0hist[2] = 0; polarity = 1;
        driveDC.reset();
        cChest.reset(); cChestM.reset(); cPres.reset(); cAir.reset(); cShelf.reset(); cEnv = 0; cNoise.s = 22222u;
        const float fc = 0.36f * fsd;
        lpA.set (fc, 0.7071f, fs); lpB.set (fc, 0.7071f, fs);
    }

    void setControls (const AlterControls& c) { ctl = c; cg = genderNorm (c.gender); }

    // Tube-style drive applied by the owner after the shifter (kept here so tests can use it).
    float drive (float x, float amount)    // amount 0..10
    {
        if (amount <= 0.001f) return x;
        const float kk = 1.0f + amount * 1.6f, bias = 0.12f * (amount / 10.0f);
        float y = std::tanh (kk * x + bias) - std::tanh (bias);
        y = driveDC.process (y) / std::sqrt (kk);
        const float w = std::min (1.0f, amount * 0.5f);
        return x + w * (y * 1.4f - x);
    }
    DCBlock driveDC;

    float process (float x)
    {
        inBuf[(size_t) (n & inMask)] = x;
        // decimated copy for the pitch tracker
        {
            float l, b, h;
            lpA.process (x, l, b, h);
            float l2, b2, h2;
            lpB.process (l, l2, b2, h2);
            if (++decPhase >= decim) { decPhase = 0; decBuf[(size_t) (dCount & decMask)] = l2; ++dCount; }
        }
        ++n;
        if (++hopCount >= hop) { hopCount = 0; analyze(); }
        advanceMarks();
        const long long e = k - D;           // output sample index emitted now
        placeGrains (e);
        float y = 0;
        if (e >= 0)
        {
            y = outBuf[(size_t) (e & outMask)];
            outBuf[(size_t) (e & outMask)] = 0.0f;
        }
        ++k;
        return colour (y);
    }

    // for tests
    float lastF0() const { return trackN > 0 ? track[(trackN - 1) & 63].f0 : 0.0f; }

private:
    struct TrackEntry { long long center; float f0; };
    struct Mark { long long pos; float period; float f0; };

    // ---- parameters/state
    float fs = 48000, fsd = 16000;
    int D = 2112, decim = 3, wd = 320, tauMin = 26, tauMax = 247, span = 570, hop = 240, maxHalf = 816;
    float unvPeriod = 384, maxSrcHalf = 864;
    std::vector<float> inBuf, outBuf, decBuf, scratch, diff, cmnd;
    int inMask = 0, outMask = 0, decMask = 0;
    long long n = 0, k = 0, dCount = 0;
    int decPhase = 0, hopCount = 0;
    SVF lpA, lpB;
    TrackEntry track[64]; long long trackN = 0;
    Mark marks[256]; long long markN = 0;
    bool hasMark = false; int polarity = 1;
    float f0hist[3] = {};
    double sNext = 0;
    AlterControls ctl;
    float curMidi = 60; bool haveMidi = false; int lastNote = -1000;

    // ---- GENDER voice colour (applied to the shifted signal; vocal-tract envelope itself is only moved moderately by PSOLA).
    // Feminine end: less low-mid chest, a little more presence, slight breathy air that follows the voice level.
    // Masculine end: a bit more chest, less presence and air.
    SVF cChest, cChestM, cPres, cAir, cShelf; Noise cNoise; float cEnv = 0, cg = 0;
    float colour (float y)
    {
        const float a = std::fabs (cg);
        float l, b, h;
        cChest.process (y, l, b, h);   const float chest = cChest.k * b;
        cChestM.process (y, l, b, h);  const float chestM = cChestM.k * b;
        cPres.process (y, l, b, h);    const float pres = cPres.k * b;
        cShelf.process (y, l, b, h);   const float top = h;
        const float rect = std::fabs (y);
        cEnv += (rect > cEnv ? 0.02f : 0.0004f) * (rect - cEnv);
        float out = y;
        if (cg > 0)
        {
            out += -0.30f * cg * chest + 0.45f * cg * pres;
            cAir.process (cNoise.next(), l, b, h);
            out += 0.075f * cg * cEnv * h;                    // breath/air, about -22 dB re voice level at full
        }
        else
        {
            out += 0.30f * a * chestM - 0.30f * a * pres - 0.45f * a * top;
        }
        return out;
    }

    inline float inAt (long long i) const { return inBuf[(size_t) (i & inMask)]; }

    // fractional read from the input ring, clamped to written range
    inline float readIn (double pos) const
    {
        long long i0 = (long long) std::floor (pos);
        const float f = (float) (pos - (double) i0);
        if (i0 + 1 > n - 1) { i0 = n - 2; }
        if (i0 < n - (long long) inMask + 4) i0 = n - (long long) inMask + 4;
        const float a = inAt (i0 - 1), b = inAt (i0), c = inAt (i0 + 1), d = inAt (i0 + 2 > n - 1 ? n - 1 : i0 + 2);
        // Catmull-Rom
        const float c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * d, c3 = 0.5f * (d - a) + 1.5f * (b - c);
        return ((c3 * f + c2) * f + c1) * f + b;
    }

    void lookup (long long t, float& f0) const
    {
        f0 = 0;
        for (long long i = trackN - 1; i >= 0 && i > trackN - 64; --i)
        {
            const TrackEntry& te = track[i & 63];
            if (te.center <= t || i == trackN - 64 + 1) { f0 = te.f0; return; }
        }
    }

    void analyze()
    {
        if (dCount < span) return;
        float rms = 0;
        for (int i = 0; i < span; ++i)
        {
            const float v = decBuf[(size_t) ((dCount - span + i) & decMask)];
            scratch[(size_t) i] = v; rms += v * v;
        }
        rms = std::sqrt (rms / (float) span);
        TrackEntry te; te.f0 = 0; te.center = n - (long long) (span / 2) * decim;
        if (rms > 8e-4f)
        {
            const float* x = scratch.data();
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                float s = 0;
                for (int j = 0; j < wd; ++j) { const float dd = x[j] - x[j + tau]; s += dd * dd; }
                diff[(size_t) tau] = s;
            }
            cmnd[0] = 1; float run = 0;
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                run += diff[(size_t) tau];
                cmnd[(size_t) tau] = run > 1e-12f ? diff[(size_t) tau] * (float) tau / run : 1.0f;
            }
            int best = -1;
            for (int tau = tauMin; tau <= tauMax - 1; ++tau)
                if (cmnd[(size_t) tau] < 0.18f)
                {
                    while (tau + 1 <= tauMax - 1 && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                    best = tau; break;
                }
            if (best < 0)
            {
                float mn = 1e9f; int bi = -1;
                for (int tau = tauMin; tau <= tauMax - 1; ++tau) if (cmnd[(size_t) tau] < mn) { mn = cmnd[(size_t) tau]; bi = tau; }
                if (bi > 0 && mn < 0.30f) best = bi;
            }
            if (best > 1)
            {
                const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
                const float den = a - 2 * b + c;
                const float off = std::fabs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
                const float tauF = (float) best + clampf (off, -1.0f, 1.0f);
                float f = fsd / tauF;
                // median of last three (octave-error guard)
                f0hist[0] = f0hist[1]; f0hist[1] = f0hist[2]; f0hist[2] = f;
                float a0 = f0hist[0], a1 = f0hist[1], a2 = f0hist[2];
                if (a0 > 0 && a1 > 0)
                {
                    const float mx = std::max (a0, std::max (a1, a2)), mn2 = std::min (a0, std::min (a1, a2));
                    f = a0 + a1 + a2 - mx - mn2;
                }
                te.f0 = f;
                te.center = n - (long long) ((float) (span) * 0.5f * (float) decim);
            }
        }
        if (te.f0 <= 0) { f0hist[0] = f0hist[1] = f0hist[2] = 0; }
        track[trackN & 63] = te; ++trackN;
    }

    void advanceMarks()
    {
        const long long known = n - (long long) (span / 2) * decim + hop;
        if (!hasMark)
        {
            marks[0] = { 0, unvPeriod, 0.0f }; markN = 1; hasMark = true;
        }
        for (int guard = 0; guard < 8; ++guard)
        {
            const Mark& last = marks[(markN - 1) & 255];
            float f0; lookup (last.pos, f0);
            const float T = f0 > 0 ? fs / f0 : unvPeriod;
            const double pred = (double) last.pos + (double) T;
            if (pred > (double) known || pred + T * 0.3 > (double) (n - 2)) break;
            long long pos = (long long) std::llround (pred);
            float f0n; lookup (pos, f0n);
            if (f0n > 0)
            {
                const long long lo = pos - (long long) (0.25f * T), hi = pos + (long long) (0.25f * T);
                float best = -1e9f; long long bi = pos;
                for (long long i = lo; i <= hi; ++i)
                {
                    const float v = (float) polarity * 0.25f * (inAt (i - 1) + 2.0f * inAt (i) + inAt (i + 1));
                    if (v > best) { best = v; bi = i; }
                }
                pos = bi;
                const float sv = inAt (pos);
                if (std::fabs (sv) > 1e-4f) polarity = sv >= 0 ? 1 : -1;
            }
            if (pos <= last.pos + (long long) (0.5f * T)) pos = last.pos + (long long) (0.5f * T) + 1;
            marks[markN & 255] = { pos, f0n > 0 ? fs / f0n : unvPeriod, f0n };
            ++markN;
        }
    }

    const Mark* nearestMark (double t) const
    {
        if (markN == 0) return nullptr;
        long long i = markN - 1;
        const Mark* best = &marks[i & 255]; double bd = std::fabs ((double) best->pos - t);
        for (long long j = markN - 2; j >= 0 && j > markN - 200; --j)
        {
            const Mark& m = marks[j & 255];
            const double d = std::fabs ((double) m.pos - t);
            if (d < bd) { bd = d; best = &m; } else if ((double) m.pos < t) break;
        }
        return best;
    }

    void placeGrains (long long e)
    {
        const long long limit = e + maxHalf;
        for (int guard = 0; guard < 64 && sNext <= (double) limit; ++guard)
        {
            const Mark* m = nearestMark (sNext);
            if (m == nullptr) { sNext += unvPeriod; continue; }
            const float Ta = m->period;
            float ratio = 1.0f;
            float To = Ta;
            const float genderPitch = genderPitchSt (ctl.gender), genderForm = genderFormantSt (ctl.gender);
            if (m->f0 > 0)
            {
                const float midiIn = 69.0f + 12.0f * std::log2 (m->f0 / 440.0f);
                if (ctl.mode == 0 || (ctl.mode == 1 && ctl.quantOff()))
                {
                    ratio = std::exp2 ((ctl.pitchSt + genderPitch) / 12.0f);
                }
                else if (ctl.mode == 1)
                {
                    // snap to scale with hysteresis, glide with "speed"
                    int note;
                    if (lastNote > -500 && std::fabs (midiIn - (float) lastNote) < 0.62f && scaleAllows (ctl.scale, lastNote - ctl.key)) note = lastNote;
                    else note = snapToScale (midiIn, ctl.key, ctl.scale);
                    lastNote = note;
                    if (!haveMidi) { curMidi = (float) note; haveMidi = true; }
                    else
                    {
                        const float a = 1.0f - std::exp (-(Ta / fs) / (std::max (0.5f, ctl.speedMs) * 0.001f));
                        curMidi += a * ((float) note - curMidi);
                    }
                    ratio = std::exp2 ((curMidi + ctl.pitchSt + genderPitch - midiIn) / 12.0f);
                }
                else
                {
                    const float noteF = ctl.quantOff() ? 72.0f + ctl.pitchSt : (float) snapToScale (72.0f + ctl.pitchSt, ctl.key, ctl.scale);
                    const float outMidi = noteF + genderPitch;
                    const float fT = 440.0f * std::exp2 ((outMidi - 69.0f) / 12.0f);
                    ratio = fT / m->f0;
                }
                ratio = clampf (ratio, 0.25f, 4.0f);
                To = Ta / ratio;
            }
            else { haveMidi = false; lastNote = -1000; }
            To = clampf (To, 0.0005f * fs, 0.040f * fs);

            float formSt = ctl.formantSt + genderForm + (ctl.link ? 12.0f * std::log2 (ratio) : 0.0f);
            const float beta = std::exp2 (clampf (formSt, -12.0f, 12.0f) / 12.0f);
            float Lh = Ta / beta;
            Lh = clampf (Lh, 0.0010f * fs, (float) maxHalf);
            if (Lh * beta > maxSrcHalf) Lh = maxSrcHalf / beta;
            const float g = std::sqrt (To / Ta) * std::sqrt (beta);

            const long long j0 = (long long) std::ceil (sNext - (double) Lh), j1 = (long long) std::floor (sNext + (double) Lh);
            const float invL = 1.0f / Lh;
            for (long long j = j0; j <= j1; ++j)
            {
                if (j < 0) continue;
                const float u = (float) ((double) j - sNext) * invL;
                const float w = 0.5f + 0.5f * std::cos (kPi * u);
                const double src = (double) m->pos + ((double) j - sNext) * (double) beta;
                outBuf[(size_t) (j & outMask)] += g * w * readIn (src);
            }
            sNext += (double) To;
        }
    }
};
} // namespace qz
