#pragma once
// CMPLXTY Drum Player DSP (no JUCE): 8 one-shot pads, 32 voices. Each pad plays a built-in synthesised drum sound or a user sample,
// with tune (resampling, 4-point Hermite), decay (exponential, -60 dB at the set time; max = the whole sample), equal-power pan,
// level, velocity and choke groups (a hit fades out every other voice in its group in 5 ms - e.g. closed hat chokes open hat).
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace cw::dsp
{
constexpr int kPads = 8, kVoices = 32, kSounds = 16, kSampleSound = 16, kChokeGroups = 4;
constexpr float kDecayFull = 4000.0f;   // decay at its maximum = no envelope (the whole sample plays)

struct Smp
{
    std::vector<float> L, R;   // R empty = mono
    double rate = 48000.0;
    std::string name;
    int len() const { return (int) L.size(); }
};

inline const char* soundName (int i)
{
    static const char* n[] = { "Kick 808", "Kick Punch", "Snare", "Clap", "Closed Hat", "Open Hat", "Low Tom", "Mid Tom",
                               "Rim", "Cowbell", "Crash", "Shaker", "Zap", "Sub Boom", "Snap", "Glitch", "Sample" };
    return n[std::clamp (i, 0, kSampleSound)];
}

namespace synth
{
constexpr double kPi = 3.14159265358979323846, kSr = 48000.0;
struct Rng { uint32_t s = 22222; float next() { s = s * 1664525u + 1013904223u; return (float) ((s >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f; } };
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make (int type, double f, double q)   // 0 LP, 1 HP, 2 BP (RBJ)
    {
        Biquad b; const double w = 2 * kPi * f / kSr, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        if (type == 0) { b.b0 = (1 - c) / 2; b.b1 = 1 - c; b.b2 = (1 - c) / 2; }
        else if (type == 1) { b.b0 = (1 + c) / 2; b.b1 = -(1 + c); b.b2 = (1 + c) / 2; }
        else { b.b0 = al; b.b1 = 0; b.b2 = -al; }
        b.b0 /= a0; b.b1 /= a0; b.b2 /= a0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b;
    }
    double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
inline double ex (double t, double tau) { return std::exp (-t / tau); }
inline double sq (double ph) { return ph - std::floor (ph) < 0.5 ? 1.0 : -1.0; }

inline Smp make (int i)
{
    Smp s; s.rate = kSr; s.name = soundName (i);
    const double lens[] = { 1.4, 0.5, 0.45, 0.5, 0.15, 0.9, 0.8, 0.7, 0.12, 0.5, 2.4, 0.25, 0.4, 2.2, 0.12, 0.35 };
    const int n = (int) (lens[i] * kSr);
    s.L.assign ((size_t) n, 0.0f);
    Rng r; r.s += (uint32_t) i * 7919u;
    double ph = 0, ph2 = 0;
    std::array<double, 6> mph {}; const double mf[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    auto metal = [&] (double scale) { double m = 0; for (int k = 0; k < 6; ++k) { mph[(size_t) k] += mf[k] * scale / kSr; m += sq (mph[(size_t) k]); } return m / 6.0; };
    Biquad f1, f2;
    switch (i)
    {
        case 2: f1 = Biquad::make (1, 1800, 0.7); break;
        case 3: f1 = Biquad::make (2, 1200, 1.4); break;
        case 4: case 5: f1 = Biquad::make (1, 7000, 0.7); f2 = Biquad::make (1, 7000, 0.7); break;
        case 9: f1 = Biquad::make (2, 700, 2.0); break;
        case 10: f1 = Biquad::make (1, 4500, 0.7); break;
        case 11: f1 = Biquad::make (2, 6500, 1.2); break;
        case 14: f1 = Biquad::make (1, 2200, 0.7); break;
        default: break;
    }
    float held = 0;
    for (int k = 0; k < n; ++k)
    {
        const double t = k / kSr; double y = 0;
        switch (i)
        {
            case 0: { const double f = 48 + 112 * ex (t, 0.04); ph += f / kSr; y = std::tanh (1.6 * std::sin (2 * kPi * ph) * ex (t, 0.38)) + 0.3 * r.next() * ex (t, 0.002); break; }
            case 1: { const double f = 55 + 170 * ex (t, 0.018); ph += f / kSr; y = std::tanh (2.4 * std::sin (2 * kPi * ph) * ex (t, 0.12)) + 0.5 * r.next() * ex (t, 0.003); break; }
            case 2: { ph += 190 / kSr; ph2 += 330 / kSr; y = 0.55 * (std::sin (2 * kPi * ph) + 0.6 * std::sin (2 * kPi * ph2)) * ex (t, 0.045) + 0.8 * f1.tick (r.next()) * ex (t, 0.09); break; }
            case 3: { const double burst = t < 0.03 ? ex (std::fmod (t, 0.01), 0.002) : ex (t - 0.03, 0.09); y = f1.tick (r.next()) * burst * 1.6; break; }
            case 4: y = f2.tick (f1.tick (metal (1.0) + 0.3 * r.next())) * ex (t, 0.022) * 2.0; break;
            case 5: y = f2.tick (f1.tick (metal (1.0) + 0.3 * r.next())) * ex (t, 0.22) * 2.0; break;
            case 6: { const double f = 80 + 45 * ex (t, 0.06); ph += f / kSr; y = std::sin (2 * kPi * ph) * ex (t, 0.2) + 0.15 * r.next() * ex (t, 0.004); break; }
            case 7: { const double f = 120 + 60 * ex (t, 0.05); ph += f / kSr; y = std::sin (2 * kPi * ph) * ex (t, 0.16) + 0.15 * r.next() * ex (t, 0.004); break; }
            case 8: { ph += 1700 / kSr; ph2 += 450 / kSr; y = (0.6 * std::sin (2 * kPi * ph) + 0.6 * std::sin (2 * kPi * ph2)) * ex (t, 0.012) + 0.4 * r.next() * ex (t, 0.0015); break; }
            case 9: { ph += 540 / kSr; ph2 += 800 / kSr; y = f1.tick (sq (ph) + sq (ph2)) * (0.6 * ex (t, 0.02) + 0.4 * ex (t, 0.12)) * 1.4; break; }
            case 10: y = f1.tick (0.6 * metal (1.37) + 0.7 * r.next()) * (t < 0.002 ? t / 0.002 : ex (t, 0.6)); break;
            case 11: y = f1.tick (r.next()) * (t < 0.015 ? t / 0.015 : ex (t - 0.015, 0.04)) * 1.5; break;
            case 12: { const double f = 80 + 1900 * ex (t, 0.03); ph += f / kSr; y = std::sin (2 * kPi * ph) * ex (t, 0.09); break; }
            case 13: { const double f = 46 + 24 * ex (t, 0.08); ph += f / kSr; y = std::tanh (1.3 * std::sin (2 * kPi * ph)) * ex (t, 0.6); break; }
            case 14: { ph += 2200 / kSr; y = f1.tick (r.next()) * ex (t, 0.006) + 0.5 * std::sin (2 * kPi * ph) * ex (t, 0.004); break; }
            case 15: { if (k % 24 == 0) held = r.next(); const double crush = std::round (held * 6.0) / 6.0; ph += (300 + 900 * std::fabs (held)) / kSr; y = (0.6 * crush + 0.4 * sq (ph)) * ex (t, 0.08); break; }
            default: break;
        }
        s.L[(size_t) k] = (float) y;
    }
    float pk = 0; for (float v : s.L) pk = std::max (pk, std::fabs (v));
    if (pk > 0) for (auto& v : s.L) v *= 0.9f / pk;
    const int fade = std::min (n, 480); for (int k = 0; k < fade; ++k) s.L[(size_t) (n - 1 - k)] *= (float) k / fade;   // click-free end
    return s;
}
} // namespace synth

inline const Smp& builtIn (int i)
{
    static const std::array<Smp, kSounds> kit = [] { std::array<Smp, kSounds> a; for (int k = 0; k < kSounds; ++k) a[(size_t) k] = synth::make (k); return a; }();
    return kit[(size_t) std::clamp (i, 0, kSounds - 1)];
}

struct PadP { int sound = 0; float tune = 0, decay = kDecayFull, pan = 0, levelDb = 0; int choke = 0; };
struct Params { std::array<PadP, kPads> pad {}; float volumeDb = -3, velSens = 0.7f, kitTune = 0; int baseNote = 36; };

class Engine
{
public:
    Engine() { builtIn (0); }   // build the kit here, never on the audio thread
    void prepare (double sampleRate) { sr = sampleRate > 0 ? sampleRate : 48000.0; reset(); }
    void reset() { for (auto& v : voices) v.on = false; activeVoices.store (0); vol = -1.0f; }
    void setParams (const Params& p) { P = p; }
    const Params& params() const { return P; }
    // user sample for a pad (owned by the caller, must stay alive while set). A change cuts that pad's playing sample voices.
    void setUser (int pad, const Smp* s)
    {
        if (pad < 0 || pad >= kPads || user[(size_t) pad] == s) return;
        for (auto& v : voices) if (v.on && v.pad == pad && v.s == user[(size_t) pad]) v.on = false;
        user[(size_t) pad] = s;
    }
    int padForNote (int note) const { const int p = note - P.baseNote; return p >= 0 && p < kPads ? p : -1; }

    void noteOn (int note, float vel) { trigger (padForNote (note), vel); }
    void trigger (int pad, float vel)
    {
        if (pad < 0 || vel <= 0) return;
        const auto& pp = P.pad[(size_t) pad];
        const Smp* s = pp.sound >= kSounds ? user[(size_t) pad] : &builtIn (pp.sound);
        hits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
        if (s == nullptr || s->len() < 2) return;
        if (pp.choke > 0)
            for (auto& v : voices) if (v.on && P.pad[(size_t) v.pad].choke == pp.choke) v.choke (sr);
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.on) { slot = &v; break; }
        if (slot == nullptr) { slot = &voices[0]; for (auto& v : voices) if (v.age < slot->age) slot = &v; }   // steal the oldest
        auto& v = *slot;
        v = Voice {};
        v.on = true; v.pad = pad; v.s = s; v.pos = 0; v.age = ++clock;
        v.inc = s->rate / sr * std::pow (2.0, (double) (pp.tune + P.kitTune) / 12.0);
        const float g = std::pow (10.0f, pp.levelDb / 20.0f) * (1.0f - P.velSens + P.velSens * std::clamp (vel, 0.0f, 1.0f));
        const float th = (std::clamp (pp.pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
        v.gL = g * std::cos (th) * 1.41421356f; v.gR = g * std::sin (th) * 1.41421356f;
        v.env = 1; v.envMul = pp.decay >= kDecayFull - 0.5f ? 1.0f : (float) std::exp (std::log (0.001) / (std::max (0.005, (double) pp.decay / 1000.0) * sr));
    }
    void allSoundOff() { for (auto& v : voices) v.on = false; activeVoices.store (0); }
    void allNotesOff() { for (auto& v : voices) if (v.on) v.choke (sr); }

    void render (float* L, float* R, int n)
    {
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        int act = 0;
        for (auto& v : voices)
        {
            if (! v.on) continue;
            const Smp& s = *v.s; const int len = s.len(); const bool st = ! s.R.empty();
            for (int i = 0; i < n; ++i)
            {
                const int k = (int) v.pos; const float f = (float) (v.pos - k);
                if (k >= len || v.env < 1e-4f) { v.on = false; break; }
                const float a = herm (s.L, k, len, f), b = st ? herm (s.R, k, len, f) : a;
                L[i] += a * v.gL * v.env; R[i] += b * v.gR * v.env;
                v.env *= v.envMul;
                if (v.fade > 0) { v.env -= v.fadeStep; if (v.env <= 0) { v.on = false; break; } }
                v.pos += v.inc;
            }
            act += v.on ? 1 : 0;
        }
        const float tg = std::pow (10.0f, P.volumeDb / 20.0f);
        if (vol < 0) vol = tg;
        float pk = 0;
        for (int i = 0; i < n; ++i) { vol += (tg - vol) * 0.002f; L[i] *= vol; R[i] *= vol; pk = std::max ({ pk, std::fabs (L[i]), std::fabs (R[i]) }); }
        activeVoices.store (act, std::memory_order_relaxed);
        outPeak.store (std::max (pk, outPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    }

    std::atomic<int> activeVoices { 0 };
    std::atomic<float> outPeak { 0 };
    std::array<std::atomic<int>, kPads> hits {};   // trigger counters, for the display's pad flash

private:
    struct Voice
    {
        bool on = false; int pad = 0; const Smp* s = nullptr; double pos = 0, inc = 1; float gL = 1, gR = 1, env = 1, envMul = 1, fade = 0, fadeStep = 0; uint32_t age = 0;
        void choke (double sr) { if (fade > 0) return; fade = 1; fadeStep = std::max (env, 1e-3f) / (float) (0.005 * sr); }
    };
    static float herm (const std::vector<float>& x, int k, int len, float f)
    {
        auto at = [&] (int j) { return j >= 0 && j < len ? x[(size_t) j] : 0.0f; };
        const float xm = at (k - 1), x0 = at (k), x1 = at (k + 1), x2 = at (k + 2);
        const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    Params P;
    double sr = 48000.0;
    std::array<Voice, kVoices> voices {};
    std::array<const Smp*, kPads> user {};
    uint32_t clock = 0;
    float vol = -1.0f;   // < 0: snap to the target on the first block
};
} // namespace cw::dsp
