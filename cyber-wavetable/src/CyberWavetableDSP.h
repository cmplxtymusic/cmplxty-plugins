#pragma once
// CMPLXTY Cyber Wavetable: DSP core (plain C++17, no JUCE).
// Wavetables: 6 banks x 8 frames, each band-limited into 11 mip levels by FFT (an octave per level), so high notes do not alias.
// Voice: 2 wavetable oscillators (morphing position, up to 7 unison voices each), sub sine, noise -> drive -> TPT state-variable filter
// (LP12 / LP24 / BP / HP) -> amp envelope. Mod envelope, 2 global LFOs and a 4-slot mod matrix are evaluated every kCtl samples.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace cw::dsp
{
inline float clean (float x) { return (std::isfinite (x) && std::fabs (x) < 1.0e6f) ? (std::fabs (x) < 1.0e-20f ? 0.0f : x) : 0.0f; }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

constexpr int kTab = 2048, kFrames = 8, kMips = 11, kBanks = 6, kMaxVoices = 16, kMaxUni = 7, kCtl = 16, kSlots = 4;
constexpr double kPi = 3.14159265358979323846;

enum class FilterType { lp12, lp24, bp, hp };
enum LfoShape { lfoSine, lfoTri, lfoSaw, lfoSquare, lfoSH };
enum Src { srcOff, srcLfo1, srcLfo2, srcModEnv, srcVel, srcWheel, srcNote, srcPressure, kNumSrc };
enum Dst { dOff, dAPos, dBPos, dBothPos, dAPitch, dBPitch, dPitch, dCutoff, dReso, dALevel, dBLevel, dDetune, dPan, dAmp, dNoise, kNumDst };

// ---------------------------------------------------------------- wavetables
inline double naiveWave (int bank, double t, double x)
{
    const double s = std::sin (2 * kPi * x);
    const double tri = x < 0.25 ? 4 * x : x < 0.75 ? 2 - 4 * x : 4 * x - 4;
    const double saw = 1 - 2 * x;
    switch (bank)
    {
        case 0: // ANALOG: sine -> triangle -> saw -> square -> narrow pulse
        {
            const double u = t * 4; const int seg = std::min (3, (int) u); const double f = u - seg;
            const double sq = x < 0.5 ? 1 : -1;
            if (seg == 0) return s + (tri - s) * f;
            if (seg == 1) return tri + (saw - tri) * f;
            if (seg == 2) return saw + (sq - saw) * f;
            const double pw = 0.5 - 0.42 * f;
            return x < pw ? 1 : -1;
        }
        case 1: // DIGITAL: fundamental plus a climbing harmonic pair
        {
            const double k = 2 + std::floor (t * 14 + 0.5);
            return 0.55 * s + 0.35 * std::sin (2 * kPi * x * k) + 0.25 * t * std::sin (2 * kPi * x * (k * 2 + 1));
        }
        case 2: // VOCAL: formants of A - E - I - O - U as harmonic weights (for a ~110 Hz voice)
        {
            static const double F[5][2] = { { 800, 1150 }, { 400, 1700 }, { 300, 2300 }, { 450, 800 }, { 325, 700 } };
            const double u = t * 4; const int i = std::min (3, (int) u); const double f = u - i;
            const double f1 = (F[i][0] + (F[i + 1][0] - F[i][0]) * f) / 110.0, f2 = (F[i][1] + (F[i + 1][1] - F[i][1]) * f) / 110.0;
            double v = 0;
            for (int h = 1; h <= 64; ++h)
            {
                const double a = std::exp (-std::pow ((h - f1) / 1.6, 2)) + 0.7 * std::exp (-std::pow ((h - f2) / 2.2, 2)) + 0.08 / h;
                v += a * std::sin (2 * kPi * h * x);
            }
            return v;
        }
        case 3: // SYNC: hard-synced saw, slave ratio 1 -> 8
        {
            const double r = 1 + t * 7, p = x * r;
            return (1 - 2 * (p - std::floor (p))) * (0.6 + 0.4 * (1 - x));
        }
        case 4: // METAL: phase modulation, ratio 3, index 0 -> 6, plus a ratio-7 shimmer
            return std::sin (2 * kPi * x + t * 6 * std::sin (2 * kPi * 3 * x) + t * t * 2 * std::sin (2 * kPi * 7 * x));
        default: // CHIP: triangle / pulse quantised from 16 down to 2 levels
        {
            const double q = std::round (16 * std::pow (2.0, -t * 3));
            const double base = t < 0.6 ? tri : tri * (1 - (t - 0.6) * 2.5) + (x < 0.25 ? 1 : -1) * (t - 0.6) * 2.5;
            return std::round (base * q) / q;
        }
    }
}

inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * kPi / (double) len * (inverse ? 1 : -1);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1);
            for (size_t j = 0; j < len / 2; ++j, w *= wl)
            {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
            }
        }
    }
}

struct Tables
{
    std::vector<float> d;
    const float* at (int b, int f, int m) const { return d.data() + (((size_t) b * kFrames + (size_t) f) * kMips + (size_t) m) * (kTab + 1); }
    float* at (int b, int f, int m) { return d.data() + (((size_t) b * kFrames + (size_t) f) * kMips + (size_t) m) * (kTab + 1); }
};

inline Tables buildTables()
{
    Tables T; T.d.assign ((size_t) kBanks * kFrames * kMips * (kTab + 1), 0.0f);
    std::vector<std::complex<double>> spec (kTab), tmp (kTab);
    for (int b = 0; b < kBanks; ++b)
        for (int f = 0; f < kFrames; ++f)
        {
            const double t = (double) f / (kFrames - 1);
            for (int i = 0; i < kTab; ++i) spec[(size_t) i] = naiveWave (b, t, (double) i / kTab);
            fft (spec, false);
            spec[0] = 0; spec[kTab / 2] = 0;
            float gain = 1.0f;
            for (int m = 0; m < kMips; ++m)
            {
                const int maxH = (kTab / 2) >> m;
                for (int h = 0; h < kTab; ++h)
                {
                    const int hh = h <= kTab / 2 ? h : kTab - h;
                    tmp[(size_t) h] = hh <= maxH && hh != kTab / 2 ? spec[(size_t) h] : std::complex<double> (0);
                }
                fft (tmp, true);
                float* o = T.at (b, f, m);
                if (m == 0)
                {
                    double pk = 1e-9; for (int i = 0; i < kTab; ++i) pk = std::max (pk, std::fabs (tmp[(size_t) i].real() / kTab));
                    gain = (float) (1.0 / pk);
                }
                for (int i = 0; i < kTab; ++i) o[i] = (float) (tmp[(size_t) i].real() / kTab) * gain;
                o[kTab] = o[0];
            }
        }
    return T;
}
inline const Tables& tables() { static const Tables T = buildTables(); return T; }

inline int mipFor (double inc)   // inc = cycles per sample; highest kept harmonic must stay below Nyquist
{
    const double v = 2.0 * (kTab / 2) * inc * 1.02;
    if (v <= 1.0) return 0;
    return std::min (kMips - 1, (int) std::ceil (std::log2 (v)));
}
inline float readTab (const float* t, double ph)
{
    const double p = ph * kTab; int i = (int) p; if (i >= kTab) i = kTab - 1;
    const float fr = (float) (p - i);
    return t[i] + fr * (t[i + 1] - t[i]);
}
// wavetable sample at morph position pos (0..1), for the UI and tests
inline float sampleAt (int bank, float pos, int mip, double ph)
{
    const auto& T = tables();
    const float fp = std::clamp (pos, 0.0f, 1.0f) * (kFrames - 1);
    const int f0 = std::min (kFrames - 2, (int) fp); const float fr = fp - (float) f0;
    const float a = readTab (T.at (bank, f0, mip), ph), b = readTab (T.at (bank, f0 + 1, mip), ph);
    return a + (b - a) * fr;
}

// ---------------------------------------------------------------- building blocks
struct SVF   // Zavalishin TPT state-variable filter
{
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 2;
    void set (float g, float kk) { k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    void sanitize() { ic1 = clean (ic1); ic2 = clean (ic2); }
    void reset() { ic1 = ic2 = 0; }
};

struct Env   // linear attack (from the current level, so retriggers never click), exponential decay / release (times to -60 dB)
{
    enum St { idle, att, dec, rel } st = idle;
    float v = 0, aInc = 1, dC = 1, rC = 1, S = 1;
    void set (float a, float d, float s, float r, float sr)
    {
        aInc = 1.0f / (std::max (0.0005f, a) * sr);
        dC = 1.0f - std::exp (-6.9f / (std::max (0.001f, d) * sr));
        rC = 1.0f - std::exp (-6.9f / (std::max (0.002f, r) * sr));
        S = std::clamp (s, 0.0f, 1.0f);
    }
    void on() { st = att; }
    void off() { if (st != idle) st = rel; }
    float tick()
    {
        switch (st)
        {
            case att: v += aInc; if (v >= 1.0f) { v = 1.0f; st = dec; } break;
            case dec: v += (S - v) * dC; break;
            case rel: v -= v * rC; if (v < 1.0e-4f) { v = 0; st = idle; } break;
            case idle: v = 0; break;
        }
        return v;
    }
};

struct Rng { uint32_t s = 0x12345678u; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; } };

struct Lfo
{
    double ph = 0; float sh = 0, out = 0; Rng rng;
    float advance (int shape, float hz, int n, float sr)
    {
        ph += (double) hz * n / sr;
        if (ph >= 1.0) { ph -= std::floor (ph); sh = rng.next(); }
        const float p = (float) ph;
        switch (shape)
        {
            case lfoSine:   out = std::sin (2.0f * (float) kPi * p); break;
            case lfoTri:    out = p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4; break;
            case lfoSaw:    out = 1 - 2 * p; break;
            case lfoSquare: out = p < 0.5f ? 1.0f : -1.0f; break;
            default:        out = sh; break;
        }
        return out;
    }
};

// ---------------------------------------------------------------- parameters
struct OscP { bool on = true; int bank = 0; float pos = 0, semi = 0, fine = 0; int uni = 1; float detune = 0.2f, level = 0.8f; };
struct Slot { int src = srcOff, dst = dOff; float amt = 0; };
struct Params
{
    OscP a, b { false, 1, 0, 0, 0, 1, 0.2f, 0.6f };
    float sub = 0, noise = 0, width = 0.5f;
    FilterType fType = FilterType::lp24;
    float cutoff = 8000, reso = 0.2f, drive = 0, fEnv = 0, fKey = 0.3f;
    float aA = 0.005f, aD = 0.3f, aS = 0.8f, aR = 0.3f;
    float mA = 0.005f, mD = 0.4f, mS = 0.0f, mR = 0.3f;
    int l1Shape = lfoSine; float l1Rate = 2; int l2Shape = lfoTri; float l2Rate = 0.3f;
    int voices = 8; float glide = 0, volumeDb = -6;
    std::array<Slot, kSlots> mod {};
    float bendRange = 2;
};

// ---------------------------------------------------------------- engine
class Engine
{
public:
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> dispPosA { 0 }, dispPosB { 0 }, dispLfo1 { 0 }, dispLfo2 { 0 }, dispEnv { 0 }, dispCut { 1000 }, outPeak { 0 };

    Engine() { (void) tables(); stack.reserve (128); }

    void prepare (double sampleRate) { sr = (float) sampleRate; reset(); }
    void reset()
    {
        for (auto& v : voice) v = Voice();
        for (int i = 0; i < kMaxVoices; ++i) voice[(size_t) i].rng.s = 0x9e3779b9u * (uint32_t) (i + 1);
        stack.clear(); lfo1 = Lfo(); lfo2 = Lfo(); lfo2.rng.s = 777; sustain = false; bend = 0; wheel = 0; pressure = 0; lastNote = -1; ctr = 0; counter = 0;
    }
    void setParams (const Params& np)
    {
        P = np;
        P.a.uni = std::clamp (P.a.uni, 1, kMaxUni); P.b.uni = std::clamp (P.b.uni, 1, kMaxUni);
        P.a.bank = std::clamp (P.a.bank, 0, kBanks - 1); P.b.bank = std::clamp (P.b.bank, 0, kBanks - 1);
        P.voices = std::clamp (P.voices, 1, kMaxVoices);
        for (int i = P.voices; i < kMaxVoices; ++i) { voice[(size_t) i].gate = false; voice[(size_t) i].amp.off(); voice[(size_t) i].mod.off(); }
    }
    const Params& params() const { return P; }

    // ---- MIDI
    void noteOn (int note, float vel)
    {
        vel = std::clamp (vel, 0.0f, 1.0f);
        if (P.voices == 1)
        {
            stack.erase (std::remove (stack.begin(), stack.end(), note), stack.end()); if (stack.size() < 128) stack.push_back (note);
            auto& v = voice[0];
            if (v.gate && v.active()) { v.note = note; v.target = (float) note; v.vel = vel; if (P.glide <= 0) v.pitch = v.target; return; }   // legato
            start (v, note, vel);
            return;
        }
        Voice* pick = nullptr;
        for (int i = 0; i < P.voices && ! pick; ++i) if (voice[(size_t) i].note == note && voice[(size_t) i].active()) pick = &voice[(size_t) i];
        for (int i = 0; i < P.voices && ! pick; ++i) if (! voice[(size_t) i].active()) pick = &voice[(size_t) i];
        if (! pick)   // steal: a released voice first (quietest), otherwise the oldest
        {
            float best = 2; for (int i = 0; i < P.voices; ++i) { auto& v = voice[(size_t) i]; if (! v.gate && ! v.held && v.amp.v < best) { best = v.amp.v; pick = &v; } }
            if (! pick) { uint64_t old = UINT64_MAX; for (int i = 0; i < P.voices; ++i) if (voice[(size_t) i].age < old) { old = voice[(size_t) i].age; pick = &voice[(size_t) i]; } }
        }
        start (*pick, note, vel);
    }
    void noteOff (int note)
    {
        if (P.voices == 1)
        {
            auto& v = voice[0];
            const bool wasCurrent = ! stack.empty() && stack.back() == note;
            stack.erase (std::remove (stack.begin(), stack.end(), note), stack.end());
            if (v.note != note && ! wasCurrent) return;
            if (! stack.empty()) { v.note = stack.back(); v.target = (float) v.note; if (P.glide <= 0) v.pitch = v.target; return; }
            release (v);
            return;
        }
        for (auto& v : voice) if (v.note == note && v.gate) release (v);
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on) for (auto& v : voice) if (v.held) { v.held = false; v.amp.off(); v.mod.off(); }
    }
    void allNotesOff() { stack.clear(); for (auto& v : voice) { v.gate = false; v.held = false; v.amp.off(); v.mod.off(); } }
    void allSoundOff() { stack.clear(); for (auto& v : voice) v = Voice(); }
    void setPitchBend (float norm) { bend = std::clamp (norm, -1.0f, 1.0f) * P.bendRange; }   // norm -1..1
    void setModWheel (float v) { wheel = std::clamp (v, 0.0f, 1.0f); }
    void setPressure (float v) { pressure = std::clamp (v, 0.0f, 1.0f); }

    // ---- audio (adds nothing to the buffers' previous content: it overwrites them)
    void render (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i) { L[i] = 0; R[i] = 0; }
        int done = 0;
        while (done < n)
        {
            if (ctr == 0) { control(); ctr = kCtl; }
            const int seg = std::min (n - done, ctr);
            for (auto& v : voice) if (v.active()) renderVoice (v, L + done, R + done, seg);
            done += seg; ctr -= seg;
        }
        const float g = dbToGain (P.volumeDb);
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i] * g, r = R[i] * g;
            l = soft (clean (l)); r = soft (clean (r));
            L[i] = l; R[i] = r; pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
        }
        outPeak.store (std::max (pk, outPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
        int act = 0; for (auto& v : voice) act += v.active() ? 1 : 0;
        activeVoices.store (act, std::memory_order_relaxed);
    }

private:
    struct OscState { double ph[kMaxUni] {}; float pos = 0, dPos = 0, gain = 0, dGain = 0; double inc[kMaxUni] {}; float gl[kMaxUni] {}, gr[kMaxUni] {}; int mip = 0, n = 1; };
    struct Voice
    {
        int note = -1; bool gate = false, held = false; float vel = 0; uint64_t age = 0;
        float pitch = 60, target = 60;
        OscState A, B; double phSub = 0; double incSub = 0; float gSub = 0, dSub = 0, gNoise = 0, dNoise = 0, subL = 0.7f, subR = 0.7f;
        SVF f1[2], f2[2]; Env amp, mod; float ampMul = 1, dAmpMul = 0;
        Rng rng; bool fresh = true;
        bool active() const { return amp.st != Env::idle; }
    };

    static float soft (float x) { const float a = std::fabs (x); return a <= 1.0f ? x : std::copysign (1.0f + std::tanh (a - 1.0f), x); }

    void start (Voice& v, int note, float vel)
    {
        const bool wasActive = v.active();
        v.note = note; v.vel = vel; v.gate = true; v.held = false; v.age = ++counter;
        v.target = (float) note;
        if (P.glide > 0 && lastNote >= 0) { if (! wasActive) v.pitch = (float) lastNote; }
        else v.pitch = (float) note;
        lastNote = note;
        if (! wasActive)
        {
            for (auto* o : { &v.A, &v.B }) for (int i = 0; i < kMaxUni; ++i) o->ph[i] = i == 0 ? 0.0 : (double) (v.rng.next() * 0.5f + 0.5f);
            v.phSub = 0; for (auto& f : v.f1) f.reset(); for (auto& f : v.f2) f.reset();
            v.fresh = true;
        }
        v.amp.on(); v.mod.on();
    }
    void release (Voice& v)
    {
        v.gate = false;
        if (sustain) v.held = true; else { v.amp.off(); v.mod.off(); }
    }

    // once per kCtl samples: LFOs, envelopes' settings, mod matrix and every voice's targets
    void control()
    {
        const float l1 = lfo1.advance (P.l1Shape, P.l1Rate, kCtl, sr), l2 = lfo2.advance (P.l2Shape, P.l2Rate, kCtl, sr);
        dispLfo1.store (l1, std::memory_order_relaxed); dispLfo2.store (l2, std::memory_order_relaxed);
        Voice* newest = nullptr;
        for (auto& v : voice)
        {
            if (! v.active()) continue;
            if (! newest || v.age > newest->age) newest = &v;
            v.amp.set (P.aA, P.aD, P.aS, P.aR, sr); v.mod.set (P.mA, P.mD, P.mS, P.mR, sr);
            float src[kNumSrc] = { 0, l1, l2, v.mod.v, v.vel, wheel, std::clamp ((v.pitch - 60.0f) / 24.0f, -1.0f, 1.0f), pressure };
            float m[kNumDst] = {};
            for (auto& s : P.mod) if (s.src > srcOff && s.src < kNumSrc && s.dst > dOff && s.dst < kNumDst) m[s.dst] += src[s.src] * s.amt;

            if (P.glide > 0) { const float c = 1.0f - std::exp (-(float) kCtl / (P.glide * 0.35f * sr)); v.pitch += (v.target - v.pitch) * c; }
            else v.pitch = v.target;

            const float pan = m[dPan];
            setupOsc (v.A, P.a, v.pitch + bend + (m[dAPitch] + m[dPitch]) * 24.0f, std::clamp (P.a.pos + m[dAPos] + m[dBothPos], 0.0f, 1.0f),
                      P.a.on ? std::clamp (P.a.level + m[dALevel], 0.0f, 1.0f) : 0.0f, std::clamp (P.a.detune + m[dDetune], 0.0f, 1.0f), pan, v.fresh);
            setupOsc (v.B, P.b, v.pitch + bend + (m[dBPitch] + m[dPitch]) * 24.0f, std::clamp (P.b.pos + m[dBPos] + m[dBothPos], 0.0f, 1.0f),
                      P.b.on ? std::clamp (P.b.level + m[dBLevel], 0.0f, 1.0f) : 0.0f, std::clamp (P.b.detune + m[dDetune], 0.0f, 1.0f), pan, v.fresh);
            v.incSub = 440.0 * std::pow (2.0, (v.pitch + bend + m[dPitch] * 24.0f - 12.0f - 69.0f) / 12.0) / sr;
            ramp (v.gSub, v.dSub, P.sub, v.fresh);
            ramp (v.gNoise, v.dNoise, std::clamp (P.noise + m[dNoise], 0.0f, 1.0f), v.fresh);
            const float pp = std::clamp (pan, -1.0f, 1.0f); v.subL = std::cos ((pp + 1) * (float) kPi / 4); v.subR = std::sin ((pp + 1) * (float) kPi / 4);

            float fc = P.cutoff * std::exp2 (P.fEnv * v.mod.v * 6.0f + P.fKey * (v.pitch - 60.0f) / 12.0f + m[dCutoff] * 6.0f);
            fc = std::clamp (fc, 20.0f, std::min (20000.0f, 0.45f * sr));
            if (&v == newest || ! newest) dispCut.store (fc, std::memory_order_relaxed);
            const float g = std::tan ((float) kPi * fc / sr), res = std::clamp (P.reso + m[dReso], 0.0f, 1.0f);
            for (auto& f : v.f1) { f.set (g, 2.0f - 1.96f * res); f.sanitize(); }
            for (auto& f : v.f2) { f.set (g, 1.414f); f.sanitize(); }
            ramp (v.ampMul, v.dAmpMul, std::clamp (1.0f + m[dAmp], 0.0f, 2.0f) * (0.35f + 0.65f * v.vel) * 0.3f, v.fresh);
            v.fresh = false;
        }
        if (newest) { dispPosA.store (newest->A.pos, std::memory_order_relaxed); dispPosB.store (newest->B.pos, std::memory_order_relaxed); dispEnv.store (newest->amp.v, std::memory_order_relaxed); }
        else { dispPosA.store (P.a.pos, std::memory_order_relaxed); dispPosB.store (P.b.pos, std::memory_order_relaxed); dispEnv.store (0, std::memory_order_relaxed); }
    }
    static void ramp (float& cur, float& d, float target, bool jump)
    {
        if (jump) { cur = target; d = 0; return; }
        d = (target - cur) / (float) kCtl;
    }
    void setupOsc (OscState& o, const OscP& p, float semis, float pos, float level, float detune, float pan, bool fresh)
    {
        const double f = 440.0 * std::pow (2.0, (semis + p.semi + p.fine / 100.0 - 69.0) / 12.0);
        const double inc = std::min (0.49, f / sr);
        o.n = p.uni;
        const float spreadSt = detune * detune * 0.9f + detune * 0.1f;   // up to about ±1 semitone at full detune
        o.mip = mipFor (inc * std::pow (2.0, spreadSt / 12.0));
        for (int i = 0; i < o.n; ++i)
        {
            const float off = o.n > 1 ? 2.0f * (float) i / (float) (o.n - 1) - 1.0f : 0.0f;
            o.inc[i] = inc * std::pow (2.0, (double) (off * spreadSt) / 12.0);
            const float pp = std::clamp (off * P.width + pan, -1.0f, 1.0f);
            const float gn = 1.0f / std::sqrt ((float) o.n);
            o.gl[i] = std::cos ((pp + 1) * (float) kPi / 4) * gn; o.gr[i] = std::sin ((pp + 1) * (float) kPi / 4) * gn;
        }
        ramp (o.pos, o.dPos, pos, fresh);
        ramp (o.gain, o.dGain, level, fresh);
    }
    static void oscTick (OscState& o, int bank, float& l, float& r)
    {
        const auto& T = tables();
        const float fp = std::clamp (o.pos, 0.0f, 1.0f) * (kFrames - 1);
        const int f0 = std::min (kFrames - 2, (int) fp); const float fr = fp - (float) f0;
        const float* ta = T.at (bank, f0, o.mip); const float* tb = T.at (bank, f0 + 1, o.mip);
        float sl = 0, sr_ = 0;
        for (int i = 0; i < o.n; ++i)
        {
            const float a = readTab (ta, o.ph[i]), b = readTab (tb, o.ph[i]), s = a + (b - a) * fr;
            sl += s * o.gl[i]; sr_ += s * o.gr[i];
            o.ph[i] += o.inc[i]; if (o.ph[i] >= 1.0) o.ph[i] -= 1.0;
        }
        l = sl * o.gain; r = sr_ * o.gain;
        o.pos += o.dPos; o.gain += o.dGain;
    }
    void renderVoice (Voice& v, float* L, float* R, int n)
    {
        const float drv = 1.0f + P.drive * 9.0f, drvN = 1.0f / std::sqrt (drv);
        const int ft = (int) P.fType;
        for (int i = 0; i < n; ++i)
        {
            float al = 0, ar = 0, bl = 0, br = 0;
            if (v.A.gain > 0 || v.A.dGain != 0) oscTick (v.A, P.a.bank, al, ar); else { v.A.pos += v.A.dPos; for (int k = 0; k < v.A.n; ++k) { v.A.ph[k] += v.A.inc[k]; if (v.A.ph[k] >= 1) v.A.ph[k] -= 1; } }
            if (v.B.gain > 0 || v.B.dGain != 0) oscTick (v.B, P.b.bank, bl, br); else { v.B.pos += v.B.dPos; for (int k = 0; k < v.B.n; ++k) { v.B.ph[k] += v.B.inc[k]; if (v.B.ph[k] >= 1) v.B.ph[k] -= 1; } }
            const float sub = std::sin (2.0f * (float) kPi * (float) v.phSub) * v.gSub;
            v.phSub += v.incSub; if (v.phSub >= 1.0) v.phSub -= 1.0;
            const float nz = v.rng.next() * v.gNoise * 0.5f;
            float x[2] = { al + bl + sub * v.subL + nz, ar + br + sub * v.subR + nz };
            v.gSub += v.dSub; v.gNoise += v.dNoise;
            const float e = v.amp.tick(); v.mod.tick();
            const float g = e * v.ampMul; v.ampMul += v.dAmpMul;
            for (int c = 0; c < 2; ++c)
            {
                float s = P.drive > 0.001f ? std::tanh (x[c] * drv) * drvN * 1.4f : x[c];
                float lp, bp, hp; v.f1[c].tick (s, lp, bp, hp);
                if (ft == 0) s = lp;
                else if (ft == 1) { float l2, b2, h2; v.f2[c].tick (lp, l2, b2, h2); s = l2; }
                else if (ft == 2) s = bp;
                else s = hp;
                x[c] = s * g;
            }
            L[i] += x[0]; R[i] += x[1];
            if (! v.active()) break;
        }
    }

    Params P;
    float sr = 48000;
    std::array<Voice, kMaxVoices> voice {};
    std::vector<int> stack;
    Lfo lfo1, lfo2;
    bool sustain = false;
    float bend = 0, wheel = 0, pressure = 0;
    int lastNote = -1, ctr = 0;
    uint64_t counter = 0;
};
} // namespace cw::dsp
