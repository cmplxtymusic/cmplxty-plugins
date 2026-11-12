#pragma once
#include "Parameters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace heft
{
class Engine
{
  public:
    static constexpr int latency = 64, taps = 129;
    void prepare(double rate)
    {
        *this = Engine();
        sr = rate;
        smooth = 1 - std::exp(-1.0 / (.020 * sr));
        for (int i = 0; i < Count; ++i)
            current[i] = definitions[i].initial;
        for (int k = 0; k < taps; ++k)
        {
            int n = k - latency;
            hilbert[k] = (n != 0 && n % 2 != 0) ? float(2.0 / (pi * n) *
                                                        (.42 - .5 * std::cos(2 * pi * k / (taps - 1)) +
                                                         .08 * std::cos(4 * pi * k / (taps - 1))))
                                                : 0;
        }
        sideHpA.setHP(sr, 150.f);
        sideHpB.setHP(sr, 150.f);
    }
    void set(const Settings &s) { target = s; }
    void tick(float &left, float &right, float sidechain) noexcept
    {
        for (int k = 0; k < Count; ++k)
            current[k] += float(smooth) * (target.value[k] - current[k]);
        float l = clean(left), r = clean(right);
        const float input = std::max(std::abs(l), std::abs(r));
        follow(input, fast, .008, .0004);
        follow(input, slow, .090, .025);
        follow(input, tailEnv, .090, .001);
        follow(input, env, current[EnvTime] * .001);
        follow(std::abs(clean(sidechain)), scenv, current[EnvTime] * .001);
        phase += 2 * pi * current[Rate] / sr;
        if (phase >= 2 * pi)
            phase -= 2 * pi;
        noisePhase += 1 / sr;
        if (noisePhase >= .025)
        {
            noisePhase -= .025;
            noiseTarget = random();
        }
        modNoise += float(1 - std::exp(-1 / (.015 * sr))) * (noiseTarget - modNoise);
        const std::array<float, 5> sources{0, std::clamp(env * 3.f, 0.f, 1.f), float(std::sin(phase)),
                                           modNoise, std::clamp(scenv * 3.f, 0.f, 1.f)};
        auto v = current;
        for (const auto &rt : target.routes)
            if (rt.source > 0 && rt.source < 5 && rt.destination >= 0 && rt.destination < 7)
            {
                const int dst = rt.destination;
                v[dst] += sources[rt.source] * rt.amount * current[Motion] * (dst == Shift ? 250.f : 1.f);
            }
        for (int k = 0; k < 7; ++k)
            v[k] = std::clamp(v[k], definitions[k].low, definitions[k].high);
        // Route changes are also ramped: removing a source cannot introduce a control discontinuity.
        for (int k = 0; k < 7; ++k)
        {
            modulated[k] += float(smooth) * (v[k] - modulated[k]);
            v[k] = modulated[k];
        }
        float dryL = dry[0][index], dryR = dry[1][index];
        dry[0][index] = l;
        dry[1][index] = r;
        const float onset = std::clamp((fast - slow) / (slow + .025f), 0.f, 2.f);
        const float tail = std::clamp(fast / (tailEnv + .00001f), 0.f, 1.f);
        const float shape = (1 + v[Transient] * onset * 1.2f) * (1 - v[Tight] * (1 - tail) * .95f);
        const float gain = std::pow(10.f, v[Slam] * current[Drive] * .42f);
        const float shaped = input * shape;
        const float detector = std::max(shaped * (1 - v[Slam]) + std::tanh(shaped * gain) * v[Slam], 1.e-8f);
        follow(detector, comp, current[Release] * .001, current[Attack] * .001);
        const float db = 20 * std::log10(std::max(comp, 1.e-8f));
        const float reduction = std::max(0.f, db - current[Threshold]) * (1 - 1 / current[Ratio]) * v[Slam];
        const float cg = std::pow(10.f, (-reduction + 6 * v[Slam]) / 20);
        const float a = std::exp(-2 * float(pi) * std::min(current[Tone] * 2.f, float(sr * .4)) / float(sr));
        const float b = std::exp(-2 * float(pi) * std::min(current[Tone] * .5f, float(sr * .3)) / float(sr));
        const float ap = (std::tan(float(pi) * std::min(current[Focus], float(sr * .2)) / float(sr)) - 1) /
                         (std::tan(float(pi) * std::min(current[Focus], float(sr * .2)) / float(sr)) + 1);
        float wet[2]{l, r};
        // FREQ SHIFT taper: stored endpoints ±400 Hz are unchanged; the middle of the
        // travel is stretched so useful small shifts are easier to find (exponent 1.6).
        const float shiftHz = std::copysign(400.f * std::pow(std::min(1.f, std::abs(v[Shift]) / 400.f), 1.6f), v[Shift]);
        shiftBlend += float(smooth) * ((std::abs(shiftHz) > .01f ? 1.f : 0.f) - shiftBlend);
        shiftPhase += 2 * pi * shiftHz / sr;
        if (shiftPhase > pi)
            shiftPhase -= 2 * pi;
        if (shiftPhase < -pi)
            shiftPhase += 2 * pi;
        for (int c = 0; c < 2; ++c)
        {
            float x = wet[c] * shape;
            float driven = x * gain;
            float sat = adaa(driven, previous[c]);
            previous[c] = driven;
            x = (x * (1 - v[Slam]) + sat * v[Slam]) * cg;
            const float n = random();
            noiseLow[c] = (1 - a) * n + a * noiseLow[c];
            noiseBase[c] = (1 - b) * n + b * noiseBase[c];
            x += (noiseLow[c] - noiseBase[c]) * v[Fuzz] * .24f * std::min(1.f, fast * 8);
            float dispersed = x;
            for (int j = 0; j < 8; ++j)
            {
                float y = ap * dispersed + apX[c][j] - ap * apY[c][j];
                apX[c][j] = dispersed;
                apY[c][j] = y;
                dispersed = y;
            }
            x += v[Smear] * (dispersed - x);
            history[c][hindex] = x;
            float imag = 0;
            for (int k = 1; k < taps; k += 2)
                imag += hilbert[k] * history[c][(hindex - k + taps) % taps];
            const float real = history[c][(hindex - latency + taps) % taps];
            wet[c] = real + shiftBlend * (real * float(std::cos(shiftPhase)) -
                                          imag * float(std::sin(shiftPhase)) - real);
        }
        float mid = .5f * (wet[0] + wet[1]), side = .5f * (wet[0] - wet[1]);
        const float decor = .63f * mid + wideX - .63f * wideY;
        wideX = mid;
        wideY = decor;
        const float spread = decor - mid;
        wideLow += float(1 - std::exp(-2 * pi * 180 / sr)) * (spread - wideLow);
        // Width addition is high-passed at 150 Hz (Linkwitz-Riley 24 dB/oct, same idea as
        // Mix Tools' mono-bass). Bass stays mono; the default (WIDE = 0) is unchanged.
        const float sideNat = side;
        const float sideWide = side * (1 + v[Wide]) + (spread - wideLow) * v[Wide] * .35f;
        float delta = sideWide - sideNat;
        delta = sideHp2(sideHp1(delta));
        side = sideNat + delta;
        l = (dryL * (1 - current[Mix]) + (mid + side) * current[Mix]);
        r = (dryR * (1 - current[Mix]) + (mid - side) * current[Mix]);
        const float out = std::pow(10.f, current[Output] / 20);
        l *= out;
        r *= out;
        // Linked, zero-lookahead sample-peak limiter. No channel can exceed -1 dBFS.
        const float peak = std::max(std::abs(l), std::abs(r));
        const float need = std::min(1.f, .89125094f / std::max(peak, 1.e-12f));
        limiter = need < limiter ? need : limiter + float(1 - std::exp(-1 / (.080 * sr))) * (need - limiter);
        left = clean(std::clamp(l * limiter, -.89125094f, .89125094f));
        right = clean(std::clamp(r * limiter, -.89125094f, .89125094f));
        reductionDb = -20 * std::log10(std::max(limiter, 1.e-8f));
        index = (index + 1) % latency;
        hindex = (hindex + 1) % taps;
    }
    float reductionDb = 0;

  private:
    static constexpr double pi = 3.14159265358979323846;
    static float clean(float x)
    {
        return std::isfinite(x) && std::abs(x) > 1.e-25f ? std::clamp(x, -32.f, 32.f) : 0.f;
    }
    static float logcosh(float x)
    {
        const float a = std::abs(x);
        return a + std::log1p(std::exp(-2 * a)) - .69314718056f;
    }
    static float adaa(float x, float old)
    {
        return std::abs(x - old) > 1.e-3f ? (logcosh(x) - logcosh(old)) / (x - old)
                                          : std::tanh((x + old) * .5f);
    }
    void follow(float x, float &e, double release, double attack = .002)
    {
        const double t = x > e ? attack : release;
        e += float(1 - std::exp(-1 / (t * sr))) * (x - e);
        if (e < 1.e-25f)
            e = 0;
    }
    float random()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(double(rng) / 2147483648.0 - 1);
    }
    struct BQ
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void setHP(double srate, float hz)
        {
            const float w = std::tan(float(pi) * hz / float(srate));
            const float n = 1.f / (1.f + 1.41421356f * w + w * w);
            b0 = n;
            b1 = -2.f * n;
            b2 = n;
            a1 = 2.f * (w * w - 1.f) * n;
            a2 = (1.f - 1.41421356f * w + w * w) * n;
            x1 = x2 = y1 = y2 = 0;
        }
        float run(float x)
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = std::isfinite(y) ? y : 0.f;
            return y1;
        }
    };
    float sideHp1(float x) { return sideHpA.run(x); }
    float sideHp2(float x) { return sideHpB.run(x); }
    double sr = 48000, smooth = .001, phase = 0, shiftPhase = 0, noisePhase = 0;
    uint32_t rng = 0x173ac451;
    Settings target;
    std::array<float, Count> current{};
    std::array<float, 7> modulated{.35f, 0, 0, 0, 0, 0, 0};
    float fast = 0, slow = 0, tailEnv = 0, env = 0, scenv = 0, comp = 0, limiter = 1, noiseTarget = 0,
          modNoise = 0, wideX = 0, wideY = 0, wideLow = 0, shiftBlend = 0;
    std::array<float, 2> previous{}, noiseLow{}, noiseBase{};
    std::array<std::array<float, 8>, 2> apX{}, apY{};
    std::array<std::array<float, taps>, 2> history{};
    std::array<std::array<float, latency>, 2> dry{};
    std::array<float, taps> hilbert{};
    int index = 0, hindex = 0;
    BQ sideHpA, sideHpB;
};
} // namespace heft
