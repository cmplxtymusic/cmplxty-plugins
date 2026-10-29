#pragma once
// CMPLXTY Robot Voice DSP (no JUCE): pitch tracker + scale snap (hard retune), two-tap pitch shifter, channel vocoder with an
// internal carrier that follows the snapped pitch (or a MIDI note), ring modulator, dry/wet + output gain.
// Everything runs per sample, so the result does not depend on the host block size.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <atomic>

namespace rv::dsp
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0); a1 = (float) (-2 * std::cos (w) / a0); a2 = (float) ((1 - al) / a0);
    }
    void lowpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w), a0 = 1 + al;
        b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

// ---- pitch tracker: YIN on a ~12 kHz decimated mono signal, a new estimate every ~5 ms
class PitchTracker
{
public:
    void prepare (double fs)
    {
        sr = fs; dec = std::max (1, (int) std::lround (fs / 12000.0)); dsr = fs / dec;
        aa1.lowpass (fs, std::min (4000.0, dsr * 0.4), 0.54); aa2.lowpass (fs, std::min (4000.0, dsr * 0.4), 1.31);
        buf.assign (kRing, 0.0f); w = 0; phase = 0; sinceHop = 0; hz = 0; d.assign (kWin / 2 + 1, 0.0f);
    }
    // returns true when a new estimate is ready (hz(): 0 = unvoiced)
    bool push (float x)
    {
        const float y = aa2.tick (aa1.tick (x));
        if (++phase < dec) return false;
        phase = 0; buf[(size_t) (w++ & (kRing - 1))] = y;
        if (++sinceHop < kHop) return false;
        sinceHop = 0; estimate(); return true;
    }
    float frequency() const { return hz; }
    float confidence() const { return conf; }
    static constexpr int kRing = 2048, kWin = 512, kHop = 64;
private:
    void estimate()
    {
        const int tauMax = std::min (kWin / 2, (int) (dsr / 60.0)), tauMin = std::max (2, (int) (dsr / 1100.0));
        const int start = w - kWin;
        double energy = 0;
        for (int i = 0; i < kWin; ++i) { const float v = buf[(size_t) ((start + i) & (kRing - 1))]; energy += v * v; }
        if (energy / kWin < 1.0e-6) { hz = 0; conf = 0; return; }
        const int L = kWin - tauMax;
        double run = 0; d[0] = 1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0;
            for (int i = 0; i < L; ++i)
            {
                const float a = buf[(size_t) ((start + i) & (kRing - 1))] - buf[(size_t) ((start + i + tau) & (kRing - 1))];
                s += a * a;
            }
            run += s; d[(size_t) tau] = (float) (run > 0 ? s * tau / run : 1.0);
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < 0.15f) { while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) { hz = 0; conf = 0; return; }
        double t = best;
        if (best > 1 && best + 1 <= tauMax)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2 * b + c;
            if (std::fabs (den) > 1e-9) t += 0.5 * (a - c) / den;
        }
        hz = (float) (dsr / t); conf = 1.0f - d[(size_t) best];
    }
    double sr = 48000, dsr = 12000; int dec = 4, w = 0, phase = 0, sinceHop = 0;
    Biquad aa1, aa2; std::vector<float> buf, d; float hz = 0, conf = 0;
};

// scale snap: 0 chromatic, 1 major, 2 minor, 3 pentatonic (major), 4 blues
inline float snapMidi (float midi, int key, int scale)
{
    static const int masks[] = { 0xFFF, 0xAB5, 0x5AD, 0x295, 0x4E9 };
    const int mask = masks[std::clamp (scale, 0, 4)];
    const int base = (int) std::floor (midi);
    float best = midi, bestD = 1e9f;
    for (int n = base - 6; n <= base + 7; ++n)
    {
        const int pc = ((n - key) % 12 + 12) % 12;
        if (! (mask >> pc & 1)) continue;
        const float dd = std::fabs ((float) n - midi);
        if (dd < bestD) { bestD = dd; best = (float) n; }
    }
    return best;
}
inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

// ---- two-tap crossfading delay pitch shifter
class Shifter
{
public:
    void prepare (double fs) { win = (float) (0.03 * fs); buf.assign (kSize, 0.0f); w = 0; p = 0; }
    float tick (float x, float ratio)
    {
        buf[(size_t) (w & (kSize - 1))] = x;
        p += (1.0f - ratio) / win; p -= std::floor (p);
        float q = p + 0.5f; q -= std::floor (q);
        const float g1 = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * p);
        const float y = g1 * read (p * win + 1.0f) + (1.0f - g1) * read (q * win + 1.0f);
        ++w; return y;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); p = 0; }
private:
    float read (float d) const
    {
        const float pos = (float) w - d; const int i = (int) std::floor (pos); const float f = pos - (float) i;
        return buf[(size_t) (i & (kSize - 1))] * (1 - f) + buf[(size_t) ((i + 1) & (kSize - 1))] * f;
    }
    static constexpr int kSize = 8192;
    std::vector<float> buf; int w = 0; float p = 0, win = 1440;
};

struct Params
{
    bool snapOn = true; int key = 0, scale = 0; float speedMs = 0.0f;
    bool vocOn = true; int carrier = 0, bandsIdx = 1, octave = 0; float vocMix = 1.0f;
    bool ringOn = false; float ringHz = 120.0f, ringMix = 0.5f;
    float mix = 1.0f, gainDb = 0.0f;
};

constexpr int kMaxBands = 24;

class Engine
{
public:
    void prepare (double fs, int numCh)
    {
        sr = fs; nch = std::clamp (numCh, 1, 2);
        tracker.prepare (fs);
        for (auto& s : shift) s.prepare (fs);
        lastBands = -1; ringPh = 0; oscPh = 0; rng = 0x1234567u;
        detMidi = 0; targetMidi = 0; shiftSt = 0; carHz = 110; voiced = false;
        sMix = p.mix; sGain = std::pow (10.0f, p.gainDb / 20.0f); sRingHz = p.ringHz; sRingMix = p.ringOn ? p.ringMix : 0.0f; sVoc = p.vocOn ? p.vocMix : 0.0f;
        setupBands();
        for (auto& l : levels) l.store (0.0f);
    }
    void setParams (const Params& np) { p = np; if (bandsFor (p.bandsIdx) != lastBands) setupBands(); }
    void setMidiNote (int note) { midiNote = note; }   // -1 = none

    // in-place, ch[0..nch-1]
    void process (float* const* ch, int n)
    {
        const float a = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        const float tGain = std::pow (10.0f, p.gainDb / 20.0f), tRing = p.ringOn ? p.ringMix : 0.0f, tVoc = p.vocOn ? p.vocMix : 0.0f;
        const float speedA = p.speedMs <= 0.5f ? 1.0f : 1.0f - std::exp (-1.0f / (p.speedMs * 0.001f * (float) sr));
        const float octMul = std::exp2 ((float) p.octave);
        for (int i = 0; i < n; ++i)
        {
            float dry[2]; for (int c = 0; c < nch; ++c) { const float v = ch[c][i]; dry[c] = std::isfinite (v) ? std::clamp (v, -4.0f, 4.0f) : 0.0f; }
            const float mono = nch > 1 ? 0.5f * (dry[0] + dry[1]) : dry[0];
            if (tracker.push (mono))
            {
                const float f = tracker.frequency();
                voiced = f > 0;
                if (voiced) { detMidi = hzToMidi (f); targetMidi = p.snapOn ? snapMidi (detMidi, p.key, p.scale) : detMidi; }
                detHz.store (voiced ? f : 0.0f, std::memory_order_relaxed);
                outHz.store (voiced ? midiToHz (targetMidi) : 0.0f, std::memory_order_relaxed);
            }
            // retune amount (in semitones) glides at the retune speed; holds through unvoiced parts
            const float wantShift = (p.snapOn && voiced) ? targetMidi - detMidi : (p.snapOn ? shiftSt : 0.0f);
            shiftSt += (wantShift - shiftSt) * speedA;
            const float ratio = std::exp2 (shiftSt / 12.0f);

            sMix += (p.mix - sMix) * a; sGain += (tGain - sGain) * a; sRingHz += (p.ringHz - sRingHz) * a; sRingMix += (tRing - sRingMix) * a; sVoc += (tVoc - sVoc) * a;

            float tuned[2];
            for (int c = 0; c < nch; ++c) tuned[c] = p.snapOn ? shift[c].tick (dry[c], ratio) : dry[c];

            // carrier
            float car = 0.0f;
            const bool doVoc = sVoc > 1.0e-4f;
            if (doVoc)
            {
                float want = midiNote >= 0 ? midiToHz ((float) midiNote) : (detMidi > 0 ? midiToHz (p.snapOn ? targetMidi : detMidi) : 110.0f);
                if (midiNote < 0) want *= octMul;
                carHz += (want - carHz) * (midiNote >= 0 ? 1.0f : speedA);
                const float dt = std::min (0.45f, carHz / (float) sr);
                oscPh += dt; if (oscPh >= 1.0f) oscPh -= 1.0f;
                const float noise = nextNoise();
                switch (p.carrier)
                {
                    case 0: car = 2.0f * oscPh - 1.0f - polyBlep (oscPh, dt); break;                      // saw
                    case 1: car = square (oscPh, dt, 0.5f); break;                                          // square
                    case 2: car = 2.5f * square (oscPh, dt, 0.12f); break;                                   // buzz (narrow pulse)
                    default: car = noise; break;                                                            // whisper
                }
                const bool unv = midiNote < 0 && ! voiced;
                car += noise * (p.carrier == 3 ? 0.0f : (unv ? 0.9f : 0.06f));
            }

            float out[2];
            for (int c = 0; c < nch; ++c)
            {
                float y = tuned[c];
                if (doVoc)
                {
                    float v = 0.0f;
                    for (int b = 0; b < nb; ++b)
                    {
                        auto& B = bands[(size_t) b];
                        const float m = B.m2[c].tick (B.m1[c].tick (dry[c]));
                        const float am = std::fabs (m);
                        float& e = B.env[c]; e += (am - e) * (am > e ? atk : rel);
                        const float cb = c == 0 ? B.c2.tick (B.c1.tick (car)) : B.cOut;
                        if (c == 0) B.cOut = cb;
                        v += cb * e * B.gain;
                        if (c == 0) B.meter = std::max (e, B.meter * 0.9995f);
                    }
                    y = y + (v * makeup - y) * sVoc;
                }
                if (sRingMix > 1.0e-4f) y *= 1.0f - sRingMix + sRingMix * std::sin (2.0f * (float) kPi * ringPh);
                out[c] = (dry[c] + (y - dry[c]) * sMix) * sGain;
                if (! std::isfinite (out[c])) out[c] = 0.0f;
            }
            ringPh += sRingHz / (float) sr; ringPh -= std::floor (ringPh);
            for (int c = 0; c < nch; ++c) ch[c][i] = out[c];
        }
        for (int b = 0; b < kMaxBands; ++b) levels[(size_t) b].store (b < nb ? bands[(size_t) b].meter : 0.0f, std::memory_order_relaxed);
    }

    static int bandsFor (int idx) { return idx <= 0 ? 8 : (idx == 1 ? 16 : 24); }
    int numBands() const { return nb; }
    std::atomic<float> detHz { 0 }, outHz { 0 };
    std::array<std::atomic<float>, kMaxBands> levels {};
    float makeup = 1.0f;

private:
    struct Band { Biquad m1[2], m2[2], c1, c2; float env[2] {}, gain = 1, cOut = 0, meter = 0; };
    void setupBands()
    {
        nb = bandsFor (p.bandsIdx); lastBands = nb;
        const double lo = 110.0, hi = std::min (8000.0, sr * 0.42), k = std::pow (hi / lo, 1.0 / (nb - 1));
        const double q = std::sqrt (k) / (k - 1) * 1.1;
        for (int b = 0; b < nb; ++b)
        {
            auto& B = bands[(size_t) b]; const double f = lo * std::pow (k, b);
            for (int c = 0; c < 2; ++c) { B.m1[c].bandpass (sr, f, q); B.m2[c].bandpass (sr, f, q); B.m1[c].reset(); B.m2[c].reset(); B.env[c] = 0; }
            B.c1.bandpass (sr, f, q); B.c2.bandpass (sr, f, q); B.c1.reset(); B.c2.reset(); B.meter = 0;
            B.gain = (float) std::sqrt (f / lo);    // carrier saw falls 6 dB/oct - tilt it back up
        }
        makeup = nb == 8 ? 2.85f : (nb == 16 ? 6.4f : 17.1f);   // measured: equal loudness for 8 / 16 / 24 bands
        atk = 1.0f - std::exp (-1.0f / (0.002f * (float) sr)); rel = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    }
    static float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static float square (float ph, float dt, float duty)
    {
        float v = ph < duty ? 1.0f : -1.0f;
        v += polyBlep (ph, dt);
        float t2 = ph - duty; if (t2 < 0) t2 += 1.0f;
        v -= polyBlep (t2, dt);
        return v - (2.0f * duty - 1.0f);   // remove DC
    }
    float nextNoise() { rng = rng * 1664525u + 1013904223u; return (float) (int32_t) rng * (1.0f / 2147483648.0f); }

    double sr = 48000; int nch = 2, nb = 16, lastBands = -1, midiNote = -1;
    Params p; PitchTracker tracker; Shifter shift[2];
    std::array<Band, kMaxBands> bands {};
    float atk = 0.01f, rel = 0.001f, ringPh = 0, oscPh = 0, detMidi = 0, targetMidi = 0, shiftSt = 0, carHz = 110;
    float sMix = 1, sGain = 1, sRingHz = 120, sRingMix = 0, sVoc = 1;
    uint32_t rng = 0x1234567u; bool voiced = false;
};
} // namespace rv::dsp
