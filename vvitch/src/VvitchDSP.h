#pragma once
// CMPLXTY VVITCH: DSP core (plain C++17, no JUCE). A dark polysynth for witch house sounds.
// Voice: 2 polyBLEP oscillators (saw / square / PWM / triangle) with up to 7 unison voices spread across the stereo field, filtered
//        noise, a choral formant stage (5 vowels, morphing), drive -> TPT state-variable filter (LP24 / LP12 / BP / HP) -> amp envelope.
//        Pitch: glide, a per-note pitch drop, the DRAG mode (everything pitched down and slowed, chopped-and-screwed style), LFO / wheel vibrato.
// Global: arpeggiator (up / down / up-down / random / chop), CHIP stage (bit depth + sample-rate crush), TAPE (wow, flutter, age),
//         dark ping-pong delay, dark 8-line FDN reverb, volume and a peak limiter that keeps the output under -0.5 dBFS.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include "Oversampler.h"

namespace vv::dsp
{
inline float clean (float x) { return (std::isfinite (x) && std::fabs (x) < 1.0e6f) ? (std::fabs (x) < 1.0e-20f ? 0.0f : x) : 0.0f; }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

constexpr int kMaxVoices = 12, kMaxUni = 7, kCtl = 16, kScope = 512;
constexpr double kPi = 3.14159265358979323846;
constexpr float kPif = 3.14159265f;

enum Wave { wSaw, wSquare, wPwm, wTri };
enum class FilterType { lp24, lp12, bp, hp };
enum LfoShape { lfoSine, lfoTri, lfoSaw, lfoSquare, lfoSH };
enum ArpMode { arpUp, arpDown, arpUpDown, arpRandom, arpChop };
inline float arpDivBeats (int d) { static const float b[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f }; return b[std::clamp (d, 0, 5)]; }

// ---------------------------------------------------------------- building blocks
inline float blep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}
inline float oscSample (int wave, double t, double dt, float pw)
{
    switch (wave)
    {
        case wSaw: return (float) (2.0 * t - 1.0) - blep (t, dt);
        case wTri: return 1.0f - 4.0f * std::fabs ((float) t - 0.5f);
        case wSquare: pw = 0.5f; [[fallthrough]];
        default:
        {
            double t2 = t + 1.0 - pw; t2 -= std::floor (t2);
            return (t < pw ? 1.0f : -1.0f) + blep (t, dt) - blep (t2, dt);
        }
    }
}

struct SVF   // Zavalishin TPT state-variable filter
{
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 2;
    void set (float g, float kk) { k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    float band (float v0) { float l, b, h; tick (v0, l, b, h); return b * k; }   // unity gain at the centre
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
            case lfoSine:   out = std::sin (2.0f * kPif * p); break;
            case lfoTri:    out = p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4; break;
            case lfoSaw:    out = 1 - 2 * p; break;
            case lfoSquare: out = p < 0.5f ? 1.0f : -1.0f; break;
            default:        out = sh; break;
        }
        return out;
    }
};

// vowel formants (F1, F2, F3 in Hz and their levels) for A E I O U: a dark, choral voice
inline void vowelAt (float v, float f[3])
{
    static const float F[5][3] = { { 760, 1150, 2800 }, { 420, 1650, 2600 }, { 300, 2050, 2900 }, { 480, 820, 2600 }, { 330, 680, 2450 } };
    const float u = std::clamp (v, 0.0f, 1.0f) * 4.0f; const int i = std::min (3, (int) u); const float fr = u - (float) i;
    for (int k = 0; k < 3; ++k) f[k] = F[i][k] * std::pow (F[i + 1][k] / F[i][k], fr);
}

// ---------------------------------------------------------------- parameters
struct Params
{
    int w1 = wSaw, w2 = wSquare; float oct1 = 0, semi2 = 0, fine2 = 7, lvl1 = 0.8f, lvl2 = 0.5f;
    float pw = 0.5f, pwm = 0.3f, pwmRate = 0.6f;
    int uni = 3; float detune = 0.25f, width = 0.6f;
    float noise = 0, noiseTone = 0.5f;
    float vox = 0, vowel = 0, voxMove = 0;
    FilterType fType = FilterType::lp24; float cutoff = 3000, reso = 0.2f, drive = 0.2f, fEnv = 0.3f, fKey = 0.3f, fA = 0.002f, fD = 0.5f, fS = 0.0f;
    float aA = 0.005f, aD = 0.4f, aS = 0.8f, aR = 0.4f;
    float drop = 0, dropTime = 0.4f; bool drag = false; float dragAmt = 5;
    float wow = 0, flutter = 0, age = 0;
    float bits = 16, crushHz = 48000, chipMix = 0;
    int lfoShape = lfoSine; float lfoRate = 4, lfoCut = 0, lfoPitch = 0;
    bool arp = false; int arpMode = arpUp, arpDiv = 3, arpOct = 1; float arpGate = 0.6f;
    int voices = 8; float glide = 0, bpm = 120;
    float dlyTime = 0.375f, dlyFb = 0.35f, dlyMix = 0, revSize = 0.6f, revMix = 0.2f, revDark = 0.6f;
    float volumeDb = -6, bendRange = 2;
};

// ---------------------------------------------------------------- engine
class Engine
{
public:
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> outPeak { 0 }, dispCut { 1000 }, dispVowel { 0 }, dispLfo { 0 }, dispPitch { 0 };
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };

    Engine() { held.reserve (128); stack.reserve (128); }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        tapeL.assign (4096, 0.0f); tapeR.assign (4096, 0.0f);
        dlyL.assign ((size_t) (sr * 2.6f) + 4, 0.0f); dlyR.assign (dlyL.size(), 0.0f);
        static const int base[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        for (int i = 0; i < 8; ++i) { rv[(size_t) i].buf.assign ((size_t) (base[i] * 2.2f * sr / 44100.0f) + 8, 0.0f); rv[(size_t) i].base = base[i] * sr / 44100.0f; }
        reset();
    }
    void reset()
    {
        for (auto& v : voice) v = Voice();
        for (int i = 0; i < kMaxVoices; ++i) voice[(size_t) i].rng.s = 0x9e3779b9u * (uint32_t) (i + 1);
        stack.clear(); held.clear(); lfo = Lfo(); voxLfo = Lfo(); sustain = false; bend = 0; wheel = 0; lastNote = -1; ctr = 0; counter = 0;
        std::fill (tapeL.begin(), tapeL.end(), 0.0f); std::fill (tapeR.begin(), tapeR.end(), 0.0f); tapeW = 0;
        std::fill (dlyL.begin(), dlyL.end(), 0.0f); std::fill (dlyR.begin(), dlyR.end(), 0.0f); dlyW = 0; dlyLp[0] = dlyLp[1] = 0;
        for (auto& l : rv) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.w = 0; l.lp = 0; }
        crushPh = 1; crushL = crushR = 0; ageL = ageR = 0; wowPh = flPh = 0; wowRnd = wowRndT = 0; lim = 1; arpOn = false; arpCtr = 0; arpIdx = -1; arpDir = 1; arpPlaying.clear();
        dTime = -1;
    }
    void setParams (const Params& np)
    {
        P = np;
        P.uni = std::clamp (P.uni, 1, kMaxUni); P.voices = std::clamp (P.voices, 1, kMaxVoices);
        P.bits = std::clamp (P.bits, 2.0f, 16.0f); P.crushHz = std::clamp (P.crushHz, 200.0f, 48000.0f);
        P.bpm = std::clamp (P.bpm, 20.0f, 400.0f); P.arpOct = std::clamp (P.arpOct, 1, 4);
        for (int i = P.voices; i < kMaxVoices; ++i) { voice[(size_t) i].gate = false; voice[(size_t) i].amp.off(); voice[(size_t) i].fenv.off(); }
        if (arpOn && ! P.arp) { for (int n : arpPlaying) stop (n); arpPlaying.clear(); for (int n : held) play (n, heldVel); }   // arp switched off: hold the chord
        if (! arpOn && P.arp) { for (int n : held) stop (n); arpIdx = -1; arpCtr = 0; }
        arpOn = P.arp;
    }
    const Params& params() const { return P; }
    float dragScale() const { return P.drag ? std::pow (2.0f, P.dragAmt / 12.0f) : 1.0f; }   // DRAG: how much slower everything runs

    // ---- MIDI
    void noteOn (int note, float vel)
    {
        vel = std::clamp (vel, 0.0f, 1.0f);
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (held.size() < 64) { held.push_back (note); std::sort (held.begin(), held.end()); }
        heldVel = vel;
        if (P.arp) { if (held.size() == 1) { arpCtr = 0; arpIdx = -1; arpDir = 1; } return; }
        play (note, vel);
    }
    void noteOff (int note)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (P.arp) return;
        stop (note);
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on) for (auto& v : voice) if (v.held) { v.held = false; v.amp.off(); v.fenv.off(); }
    }
    void allNotesOff() { stack.clear(); held.clear(); arpPlaying.clear(); for (auto& v : voice) { v.gate = false; v.held = false; v.amp.off(); v.fenv.off(); } }
    void allSoundOff() { stack.clear(); held.clear(); arpPlaying.clear(); for (auto& v : voice) v = Voice(); }
    void setPitchBend (float norm) { bend = std::clamp (norm, -1.0f, 1.0f) * P.bendRange; }
    void setModWheel (float v) { wheel = std::clamp (v, 0.0f, 1.0f); }
    void setPressure (float) {}

    // ---- audio (overwrites the buffers)
    void render (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i) { L[i] = 0; R[i] = 0; }
        int done = 0;
        while (done < n)
        {
            if (ctr == 0) { control(); ctr = kCtl; }
            const int seg = std::min (n - done, ctr);
            for (auto& v : voice) if (v.active()) renderVoice (v, L + done, R + done, seg);
            ctr -= seg; done += seg;
        }
        int act = 0; for (auto& v : voice) if (v.active()) ++act;
        activeVoices.store (act, std::memory_order_relaxed);
        post (L, R, n);
    }

private:
    struct DriveOS : cmx::Oversampler { DriveOS() { setFactor (4); } };
    // tanh drive, 4x polyphase-IIR oversampled with first-order antiderivative anti-aliasing (log-cosh): high driven notes no longer fold
    // harmonics back as inharmonic fizz. Zero reported latency (minimum-phase IIR halfbands, a fraction of a sample of group delay).
    template <class V>
    static float driveAA (V& v, float in, float drv)
    {
        float hi[16]; v.os.up (in, hi);
        for (int k = 0; k < v.os.factor(); ++k)
        {
            const double u = (double) hi[k] * drv, d = u - v.dxp;
            const double y = std::abs (d) < 1.0e-6 ? std::tanh (0.5 * (u + v.dxp)) : (lcosh (u) - lcosh (v.dxp)) / d;
            v.dxp = u; hi[k] = (float) y;
        }
        return v.os.down (hi);
    }
    static double lcosh (double u) { const double a = std::abs (u); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; }
    struct Voice
    {
        DriveOS os; double dxp = 0;
        int note = -1; float vel = 1, pitch = 60, target = 60, t = 0; bool gate = false, held = false; uint64_t age = 0;
        Env amp, fenv; std::array<double, kMaxUni> ph1 {}, ph2 {}; double pwmPh = 0;
        std::array<double, kMaxUni> inc1 {}, inc2 {}; std::array<float, kMaxUni> gl {}, gr {};
        SVF f1, f2; std::array<SVF, 3> fm; Rng rng; float nLp = 0, pwNow = 0.5f, fG = 0.1f, fK = 1.4f;
        bool active() const { return amp.st != Env::idle; }
    };
    struct RvLine { std::vector<float> buf; float base = 1000; int w = 0; float lp = 0; };

    void play (int note, float vel)
    {
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
        if (! pick)
        {
            float best = 2; for (int i = 0; i < P.voices; ++i) { auto& v = voice[(size_t) i]; if (! v.gate && ! v.held && v.amp.v < best) { best = v.amp.v; pick = &v; } }
            if (! pick) { uint64_t old = UINT64_MAX; for (int i = 0; i < P.voices; ++i) if (voice[(size_t) i].age < old) { old = voice[(size_t) i].age; pick = &voice[(size_t) i]; } }
        }
        start (*pick, note, vel);
    }
    void stop (int note)
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
    void start (Voice& v, int note, float vel)
    {
        const bool wasActive = v.active();
        v.note = note; v.vel = vel; v.target = (float) note; v.gate = true; v.held = false; v.age = ++counter; v.t = 0;
        v.pitch = (P.glide > 0 && lastNote >= 0) ? (float) lastNote : (float) note;
        if (! wasActive)
        {
            for (int u = 0; u < kMaxUni; ++u) { v.ph1[(size_t) u] = 0.5 + 0.5 * v.rng.next(); v.ph2[(size_t) u] = 0.5 + 0.5 * v.rng.next(); }
            v.f1.reset(); v.f2.reset(); for (auto& f : v.fm) f.reset(); v.nLp = 0;
        }
        v.amp.on(); v.fenv.on();
        lastNote = note;
    }
    void release (Voice& v)
    {
        v.gate = false;
        if (sustain && ! P.arp) { v.held = true; return; }
        v.amp.off(); v.fenv.off();
    }

    void arpStep()
    {
        for (int n : arpPlaying) stop (n);
        arpPlaying.clear();
        if (held.empty()) return;
        std::vector<int>& seq = arpSeq; seq.clear();
        for (int o = 0; o < P.arpOct; ++o) for (int n : held) if (n + 12 * o < 128) seq.push_back (n + 12 * o);
        if (P.arpMode == arpChop) { for (int o = 0; o < P.arpOct && o < 1; ++o) for (int n : held) { play (n, heldVel); arpPlaying.push_back (n); } return; }
        const int N = (int) seq.size();
        if (P.arpMode == arpUp) arpIdx = (arpIdx + 1) % N;
        else if (P.arpMode == arpDown) arpIdx = arpIdx < 0 ? N - 1 : (arpIdx - 1 + N) % N;
        else if (P.arpMode == arpUpDown)
        {
            if (N == 1) arpIdx = 0;
            else { if (arpIdx < 0) { arpIdx = 0; arpDir = 1; } else { arpIdx += arpDir; if (arpIdx >= N) { arpIdx = N - 2; arpDir = -1; } if (arpIdx < 0) { arpIdx = 1; arpDir = 1; } } }
        }
        else arpIdx = std::clamp ((int) ((arpRng.next() * 0.5f + 0.5f) * N), 0, N - 1);
        arpIdx = std::clamp (arpIdx, 0, N - 1);
        play (seq[(size_t) arpIdx], heldVel); arpPlaying.push_back (seq[(size_t) arpIdx]);
    }

    void control()
    {
        const float ds = dragScale(), dt = (float) kCtl / sr;
        // arpeggiator (control-rate clock: 1/3 ms resolution)
        if (P.arp)
        {
            const float step = sr * 60.0f / P.bpm * arpDivBeats (P.arpDiv) * ds;
            if (held.empty()) { if (! arpPlaying.empty()) { for (int n : arpPlaying) stop (n); arpPlaying.clear(); } arpCtr = 0; }
            else
            {
                if (arpCtr <= 0) { arpStep(); arpCtr += step; arpGateLeft = step * std::clamp (P.arpGate, 0.05f, 1.0f); }
                arpCtr -= kCtl; arpGateLeft -= kCtl;
                if (arpGateLeft <= 0 && P.arpGate < 0.999f && ! arpPlaying.empty()) { for (int n : arpPlaying) stop (n); arpPlaying.clear(); }
            }
        }
        const float lv = lfo.advance (P.lfoShape, P.lfoRate / ds, kCtl, sr);
        const float vx = voxLfo.advance (lfoTri, 0.05f + P.voxMove * 1.5f / ds, kCtl, sr);
        float vowel = std::clamp (P.vowel + (P.voxMove > 0 ? vx * 0.5f * std::min (1.0f, P.voxMove * 4.0f) : 0.0f), 0.0f, 1.0f);
        float fr[3]; vowelAt (vowel, fr);
        const float dragSt = P.drag ? -P.dragAmt : 0.0f;
        dispLfo.store (lv, std::memory_order_relaxed); dispVowel.store (vowel, std::memory_order_relaxed);
        const float vib = P.lfoPitch * lv + wheel * 0.35f * std::sin ((float) (2.0 * kPi * vibPh));
        vibPh += 5.5 * kCtl / sr; vibPh -= std::floor (vibPh);
        pwmPh += (double) P.pwmRate / ds * kCtl / sr; pwmPh -= std::floor (pwmPh);
        const float gc = P.glide > 0 ? 1.0f - std::exp (-dt / (P.glide * ds)) : 1.0f;
        const int U = P.uni;
        float lastCut = P.cutoff;
        for (auto& v : voice)
        {
            if (! v.active()) continue;
            v.amp.set (P.aA * ds, P.aD * ds, P.aS, P.aR * ds, sr);
            v.fenv.set (P.fA * ds, P.fD * ds, P.fS, P.aR * ds, sr);
            v.pitch += (v.target - v.pitch) * gc;
            v.t += dt;
            const float dropT = std::max (0.01f, P.dropTime * ds);
            const float dropSt = -P.drop * (1.0f - std::exp (-3.0f * v.t / dropT));
            const float base = v.pitch + bend + vib + dropSt + dragSt;
            dispPitch.store (dropSt + dragSt, std::memory_order_relaxed);
            for (int u = 0; u < U; ++u)
            {
                const float s = U == 1 ? 0.0f : -1.0f + 2.0f * (float) u / (float) (U - 1);
                const float det = s * P.detune * 0.6f;   // semitones
                const double f1 = 440.0 * std::pow (2.0, (base + 12.0f * P.oct1 + det - 69.0f) / 12.0);
                const double f2 = 440.0 * std::pow (2.0, (base + P.semi2 + P.fine2 / 100.0f - det * 0.8f - 69.0f) / 12.0);
                v.inc1[(size_t) u] = std::min (0.45, f1 / sr); v.inc2[(size_t) u] = std::min (0.45, f2 / sr);
                const float pan = std::clamp (s * P.width, -1.0f, 1.0f);
                const float g = 1.0f / std::sqrt ((float) U);
                v.gl[(size_t) u] = g * std::sqrt (0.5f * (1.0f - pan)); v.gr[(size_t) u] = g * std::sqrt (0.5f * (1.0f + pan));
            }
            v.pwNow = std::clamp (P.pw + P.pwm * 0.42f * std::sin ((float) (2.0 * kPi * pwmPh) + (float) v.note * 0.7f), 0.05f, 0.95f);
            const float fe = v.fenv.v;
            float fc = P.cutoff * std::pow (2.0f, P.fEnv * fe * 6.0f + P.fKey * (v.pitch - 60.0f) / 12.0f + P.lfoCut * lv * 3.0f);
            fc = std::clamp (fc, 20.0f, sr * 0.45f);
            lastCut = fc;
            v.fG = std::tan (kPif * fc / sr); v.fK = 2.0f - 1.96f * std::clamp (P.reso, 0.0f, 1.0f);
            v.f1.set (v.fG, v.fK); v.f2.set (v.fG, P.fType == FilterType::lp24 ? std::max (v.fK, 0.9f) : v.fK);
            for (int k = 0; k < 3; ++k) v.fm[(size_t) k].set (std::tan (kPif * std::min (fr[k], sr * 0.45f) / sr), 1.0f / (k == 2 ? 10.0f : 7.0f));
            v.f1.sanitize(); v.f2.sanitize(); for (auto& f : v.fm) f.sanitize(); v.nLp = clean (v.nLp);
        }
        dispCut.store (lastCut, std::memory_order_relaxed);
    }

    void renderVoice (Voice& v, float* L, float* R, int n)
    {
        const int U = P.uni;
        const float lvl1 = P.lvl1, lvl2 = P.lvl2, pw = v.pwNow;
        const float velG = 1.0f - 0.5f * (1.0f - v.vel);
        const float nA = 0.02f + 0.9f * P.noiseTone * P.noiseTone;
        const float drv = 1.0f + P.drive * 7.0f, drvN = 1.0f / std::sqrt (drv);
        const float vox = P.vox;
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            for (int u = 0; u < U; ++u)
            {
                auto& p1 = v.ph1[(size_t) u]; auto& p2 = v.ph2[(size_t) u];
                const double d1 = v.inc1[(size_t) u], d2 = v.inc2[(size_t) u];
                float s = 0;
                if (lvl1 > 0) s += lvl1 * oscSample (P.w1, p1, d1, pw);
                if (lvl2 > 0) s += lvl2 * oscSample (P.w2, p2, d2, 1.0f - pw);
                p1 += d1; if (p1 >= 1.0) p1 -= 1.0;
                p2 += d2; if (p2 >= 1.0) p2 -= 1.0;
                l += s * v.gl[(size_t) u]; r += s * v.gr[(size_t) u];
            }
            float m = 0.5f * (l + r), side = 0.5f * (l - r);
            if (P.noise > 0) { v.nLp += (v.rng.next() - v.nLp) * nA; m += P.noise * v.nLp * (1.2f - P.noiseTone * 0.6f); }
            if (vox > 0)
            {
                const float fsum = v.fm[0].band (m) + 0.55f * v.fm[1].band (m) + 0.3f * v.fm[2].band (m);
                m = m * (1.0f - vox) + fsum * vox * 2.2f;
                side *= (1.0f - 0.5f * vox);
            }
            // drive + filter on the mid; the unison side signal is scaled down with the cutoff (the stereo spread follows the filter)
            const float x = driveAA (v, m, drv) * drvN;
            float lp, bp, hp; v.f1.tick (x, lp, bp, hp);
            float y;
            switch (P.fType)
            {
                case FilterType::lp24: { float l2, b2, h2; v.f2.tick (lp, l2, b2, h2); y = l2; break; }
                case FilterType::lp12: y = lp; break;
                case FilterType::bp: y = bp * v.fK; break;
                default: y = hp; break;
            }
            const float sy = side * std::min (1.0f, v.fG * 2.0f + (P.fType == FilterType::hp ? 1.0f : 0.0f));
            const float a = v.amp.tick() * velG * 0.32f; v.fenv.tick();
            L[i] += (y + sy) * a; R[i] += (y - sy) * a;
        }
    }

    float readFrac (const std::vector<float>& b, int w, float d) const
    {
        const int N = (int) b.size(); float rp = (float) w - d; while (rp < 0) rp += (float) N;
        const int i0 = (int) rp; const float f = rp - (float) i0; const int i1 = (i0 + 1) % N;
        return b[(size_t) (i0 % N)] * (1 - f) + b[(size_t) i1] * f;
    }

    void post (float* L, float* R, int n)
    {
        const float ds = dragScale();
        const bool chip = P.chipMix > 0.0001f;
        const float q = std::pow (2.0f, P.bits - 1.0f), crInc = std::min (1.0f, P.crushHz / sr);
        const bool tape = P.wow > 0 || P.flutter > 0 || P.age > 0;
        const float ageA = 1.0f - std::exp (-2.0f * kPif * (18000.0f - 15000.0f * P.age) / sr);
        const bool dly = P.dlyMix > 0.0001f && ! dlyL.empty();
        const float dTarget = std::clamp (P.dlyTime * ds, 0.01f, 2.5f) * sr;
        if (dTime < 0) dTime = dTarget;
        const float dlyFb = std::clamp (P.dlyFb, 0.0f, 0.95f), dA = 1.0f - std::exp (-2.0f * kPif * 2800.0f / sr);
        const bool rev = P.revMix > 0.0001f;
        const float rt = 0.4f + P.revSize * P.revSize * 9.0f, size = 0.45f + P.revSize * 1.5f;
        const float rA = 1.0f - std::exp (-2.0f * kPif * (9000.0f - 7600.0f * P.revDark) / sr);
        std::array<float, 8> rg, rlen;
        for (int k = 0; k < 8; ++k) { rlen[(size_t) k] = std::min ((float) rv[(size_t) k].buf.size() - 4.0f, rv[(size_t) k].base * size); rg[(size_t) k] = std::pow (10.0f, -3.0f * rlen[(size_t) k] / (sr * rt)); }
        const float vol = dbToGain (P.volumeDb), thr = 0.944f;
        float pk = 0;
        int sp = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (chip)
            {
                crushPh += crInc;
                if (crushPh >= 1.0f) { crushPh -= 1.0f; crushL = std::round (std::clamp (l, -1.0f, 1.0f) * q) / q; crushR = std::round (std::clamp (r, -1.0f, 1.0f) * q) / q; }
                l += (crushL - l) * P.chipMix; r += (crushR - r) * P.chipMix;
            }
            if (tape && ! tapeL.empty())
            {
                wowPh += 0.55 / ds / sr; if (wowPh >= 1) { wowPh -= 1; wowRndT = tapeRng.next(); }
                flPh += 7.3 / sr; if (flPh >= 1) flPh -= 1;
                wowRnd += (wowRndT - wowRnd) * 0.00005f;
                const float d = 0.006f * sr + P.wow * 0.0045f * sr * (0.7f * std::sin ((float) (2 * kPi * wowPh)) + 0.5f * wowRnd + 1.2f) + P.flutter * 0.00045f * sr * (1.0f + std::sin ((float) (2 * kPi * flPh)));
                tapeL[(size_t) tapeW] = l; tapeR[(size_t) tapeW] = r;
                l = readFrac (tapeL, tapeW, d); r = readFrac (tapeR, tapeW, d + 3.0f);
                tapeW = (tapeW + 1) % (int) tapeL.size();
                if (P.age > 0)
                {
                    ageL += (l - ageL) * ageA; ageR += (r - ageR) * ageA;
                    const float sat = 1.0f + P.age * 2.0f;
                    l = std::tanh (ageL * sat) / sat * (1.0f + P.age * 0.6f) + tapeRng.next() * P.age * 0.003f;
                    r = std::tanh (ageR * sat) / sat * (1.0f + P.age * 0.6f) + tapeRng.next() * P.age * 0.003f;
                }
            }
            if (dly)
            {
                dTime += (dTarget - dTime) * 0.0005f;
                const float yl = readFrac (dlyL, dlyW, dTime), yr = readFrac (dlyR, dlyW, dTime);
                dlyLp[0] += (yl - dlyLp[0]) * dA; dlyLp[1] += (yr - dlyLp[1]) * dA;
                dlyL[(size_t) dlyW] = clean (std::tanh ((0.5f * (l + r)) * 0.7f + dlyLp[1] * dlyFb));   // ping-pong: input to the left, cross feedback
                dlyR[(size_t) dlyW] = clean (std::tanh (dlyLp[0] * dlyFb));
                dlyW = (dlyW + 1) % (int) dlyL.size();
                l += yl * P.dlyMix * 1.2f; r += yr * P.dlyMix * 1.2f;
            }
            if (rev)
            {
                std::array<float, 8> o;
                for (int k = 0; k < 8; ++k) { auto& ln = rv[(size_t) k]; o[(size_t) k] = readFrac (ln.buf, ln.w, rlen[(size_t) k]); }
                // 8x8 Hadamard mix (fast Walsh-Hadamard), normalised
                for (int h = 1; h < 8; h <<= 1) for (int a = 0; a < 8; a += h << 1) for (int b = a; b < a + h; ++b) { const float x = o[(size_t) b], y = o[(size_t) (b + h)]; o[(size_t) b] = x + y; o[(size_t) (b + h)] = x - y; }
                const float in = (l + r) * 0.5f;
                float wl = 0, wr = 0;
                for (int k = 0; k < 8; ++k)
                {
                    auto& ln = rv[(size_t) k];
                    const float fb = o[(size_t) k] * 0.35355339f * rg[(size_t) k];
                    ln.lp += (fb - ln.lp) * rA; ln.lp = clean (ln.lp);
                    ln.buf[(size_t) ln.w] = ln.lp + in * ((k & 1) ? 0.5f : -0.5f);
                    ln.w = (ln.w + 1) % (int) ln.buf.size();
                    if (k & 1) wr += ln.lp; else wl += ln.lp;
                }
                l = l * (1.0f - 0.5f * P.revMix) + wl * P.revMix * 0.6f;
                r = r * (1.0f - 0.5f * P.revMix) + wr * P.revMix * 0.6f;
            }
            l = clean (l * vol); r = clean (r * vol);
            // limiter: instant attack, 150 ms release, then a hard ceiling for safety
            const float a = std::max (std::fabs (l), std::fabs (r));
            const float want = a > thr ? thr / a : 1.0f;
            lim = want < lim ? want : lim + (1.0f - lim) * (1.0f - std::exp (-1.0f / (0.15f * sr)));
            l = std::clamp (l * lim, -thr, thr); r = std::clamp (r * lim, -thr, thr);
            L[i] = l; R[i] = r;
            pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
            if ((i & 1) == 0) { scope[(size_t) sp].store (0.5f * (l + r), std::memory_order_relaxed); sp = (sp + 1) % kScope; }
        }
        scopePos.store (sp, std::memory_order_relaxed);
        outPeak.store (std::max (pk, outPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
        crushL = clean (crushL); crushR = clean (crushR); dlyLp[0] = clean (dlyLp[0]); dlyLp[1] = clean (dlyLp[1]); ageL = clean (ageL); ageR = clean (ageR);
    }

    Params P;
    float sr = 48000;
    std::array<Voice, kMaxVoices> voice;
    std::vector<int> stack, held, arpPlaying, arpSeq;
    float heldVel = 0.8f;
    Lfo lfo, voxLfo; Rng arpRng, tapeRng;
    bool sustain = false, arpOn = false;
    float bend = 0, wheel = 0;
    int lastNote = -1, ctr = 0, arpIdx = -1, arpDir = 1;
    float arpCtr = 0, arpGateLeft = 0;
    uint64_t counter = 0;
    double vibPh = 0, pwmPh = 0, wowPh = 0, flPh = 0;
    // post
    float crushPh = 1, crushL = 0, crushR = 0, ageL = 0, ageR = 0, wowRnd = 0, wowRndT = 0, lim = 1, dTime = -1;
    std::vector<float> tapeL, tapeR, dlyL, dlyR; int tapeW = 0, dlyW = 0; float dlyLp[2] {};
    std::array<RvLine, 8> rv;
};
} // namespace vv::dsp
