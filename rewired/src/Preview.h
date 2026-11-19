#pragma once
// CMPLXTY Rewired 1.1.0: audio for the PLAY / SOLO preview (plain C++17, no JUCE).
//   - the song (or the loaded stems summed) as 16-bit stereo at 44.1 kHz
//   - the separated / loaded stems as 16-bit mono at 22.05 kHz (index = MapLane::stem)
//   - a lane's notes as sound (renderNotes), for lanes without their own stem audio and for REAL MIDI lanes
// ~60 MB for a 3-minute song; never saved with the project (reloaded from the files, see RewiredProcessor::ensurePreview).
#include "RewiredEngine.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace rw
{
struct Preview
{
    static constexpr double kMixRate = 44100.0, kStemRate = 22050.0;
    std::vector<int16_t> mixL, mixR;
    std::array<std::vector<int16_t>, 4> stems;
    double seconds() const { return (double) mixL.size() / kMixRate; }
    bool hasStem (int i) const { return i >= 0 && i < 4 && ! stems[(size_t) i].empty(); }
};

inline int16_t pcm16 (float v) { return (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f); }
inline std::vector<int16_t> toPcm16 (const std::vector<float>& x) { std::vector<int16_t> y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = pcm16 (x[i]); return y; }

// 44.1 kHz -> 22.05 kHz: a 31-tap windowed-sinc half-band low-pass, then every second sample (cheap; it is a preview)
inline std::vector<int16_t> decimate2 (const std::vector<float>& x)
{
    constexpr int N = 31, C = N / 2;
    float h[N]; double sum = 0.0;
    for (int k = 0; k < N; ++k)
    {
        const double d = k - C, sinc = d == 0.0 ? 1.0 : std::sin (1.5707963268 * d) / (1.5707963268 * d);
        h[k] = (float) (sinc * (0.5 - 0.5 * std::cos (6.283185307 * (k + 1) / (N + 1)))); sum += h[k];
    }
    for (auto& v : h) v = (float) (v / sum);
    int taps[N]; int nt = 0; for (int k = 0; k < N; ++k) if (std::abs (h[k]) > 1e-7f) taps[nt++] = k;   // a half-band: every other tap is zero
    std::vector<int16_t> y (x.size() / 2);
    const long nx = (long) x.size();
    for (size_t o = 0; o < y.size(); ++o)
    {
        const long c = (long) o * 2 - C;
        float s = 0.0f;
        if (c >= 0 && c + N <= nx) for (int q = 0; q < nt; ++q) s += h[taps[q]] * x[(size_t) (c + taps[q])];
        else for (int q = 0; q < nt; ++q) { const long i = c + taps[q]; if (i >= 0 && i < nx) s += h[taps[q]] * x[(size_t) i]; }
        y[o] = pcm16 (s);
    }
    return y;
}

// which drum a GM note is: 0 kick, 1 snare / clap, 2 hat, 3 cymbal / ride
inline int drumKind (int pitch) { return pitch <= 36 ? 0 : pitch <= 40 ? 1 : (pitch == 42 || pitch == 44 || pitch == 46) ? 2 : 3; }

// notes -> mono 16-bit at `rate`: melodic notes are a soft sine voice (2nd harmonic, 5 ms attack, 80 ms release), drum notes a synth kit
inline std::vector<int16_t> renderNotes (const std::vector<const std::vector<Note>*>& notes, bool drums, double seconds, double rate = Preview::kStemRate)
{
    const size_t n = (size_t) std::max (0.0, seconds * rate);
    std::vector<float> y (n, 0.0f);
    uint32_t seed = 22222u;
    auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((int32_t) seed) / 2147483648.0f; };
    for (auto* v : notes)
        if (v != nullptr)
            for (auto& nt : *v)
            {
                const size_t a = (size_t) std::max (0.0, nt.start * rate);
                if (a >= n) continue;
                const float g = 0.25f + 0.75f * (float) nt.velocity / 127.0f;
                if (drums)
                {
                    const int kind = drumKind (nt.pitch);
                    const double len = kind == 0 ? 0.28 : kind == 1 ? 0.18 : kind == 2 ? 0.06 : 0.9;
                    const size_t e = std::min (n, a + (size_t) (len * rate));
                    double ph = 0.0; float hp = 0.0f, last = 0.0f;
                    for (size_t i = a; i < e; ++i)
                    {
                        const double t = (double) (i - a) / rate;
                        float s = 0.0f;
                        if (kind == 0) { const double f = 48.0 + 110.0 * std::exp (-t * 30.0); ph += f / rate; s = 0.9f * (float) std::sin (6.283185307 * ph) * (float) std::exp (-t * 11.0); }
                        else
                        {
                            const float w = noise(); hp = 0.6f * (hp + w - last); last = w;   // crude high-pass for the noise
                            if (kind == 1) { ph += 190.0 / rate; s = (0.45f * hp + 0.3f * (float) std::sin (6.283185307 * ph)) * (float) std::exp (-t * 22.0); }
                            else if (kind == 2) s = 0.35f * hp * (float) std::exp (-t * 70.0);
                            else s = 0.25f * hp * (float) std::exp (-t * 4.0);
                        }
                        y[i] += g * s;
                    }
                    continue;
                }
                const double f = 440.0 * std::pow (2.0, (nt.pitch - 69) / 12.0);
                if (f >= rate * 0.45) continue;
                const double dur = std::max (0.03, nt.end - nt.start);
                const size_t e = std::min (n, a + (size_t) ((dur + 0.08) * rate));
                for (size_t i = a; i < e; ++i)
                {
                    const double t = (double) (i - a) / rate;
                    const double env = std::min (1.0, t / 0.005) * (t < dur ? 1.0 : std::exp (-(t - dur) / 0.02));
                    const double w = 6.283185307 * f * t;
                    y[i] += g * 0.16f * (float) (env * (std::sin (w) + 0.3 * std::sin (2.0 * w)));
                }
            }
    std::vector<int16_t> out (n);
    for (size_t i = 0; i < n; ++i) out[i] = pcm16 (std::tanh (y[i]));
    return out;
}
} // namespace rw
