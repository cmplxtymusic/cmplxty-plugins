#pragma once
// CMPLXTY Neon FX DSP (no JUCE). Chain: drive -> filter (LP / HP / BP / formant, tempo LFO) -> trance gate -> stutter -> tape stop
// -> DJ filter macro -> dry / wet mix -> output. Every module has zero latency and fades in / out when switched, so toggling never clicks.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cw::dsp
{
constexpr double kPi = 3.14159265358979323846;

// note lengths in beats (quarter notes)
inline double lenBeats (int i)      { static const double v[] = { 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 }; return v[std::clamp (i, 0, 7)]; }      // stutter
inline double gateBeats (int i)     { static const double v[] = { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 }; return v[std::clamp (i, 0, 4)]; }                        // gate step
inline double lfoBeats (int i)      { static const double v[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 }; return v[std::clamp (i, 0, 6)]; }                         // filter LFO period
constexpr int kPatterns = 8;
inline const char* gatePattern (int i)
{
    static const char* p[kPatterns] = { "1111111111111111", "1010101010101010", "0101010101010101", "1011011010110110",
                                        "1110111011101110", "1001001010010010", "1000100010101111", "1111111011101010" };
    return p[std::clamp (i, 0, kPatterns - 1)];
}
constexpr float kGateDuty = 0.72f;   // an "on" step sounds for this fraction of the step, so even Straight chops

struct Params
{
    bool drOn = false; float drDb = 12.0f; int drMode = 0; float drTone = 12000.0f;        // modes: Soft, Hard, Fold, Crush
    bool flOn = false; int flMode = 0; float flCut = 1200.0f, flReso = 0.3f, flVowel = 0.0f; int flLfoRate = 2; float flLfoDepth = 0.0f;   // LP, HP, BP, Formant
    bool gtOn = false; int gtRate = 1, gtPattern = 3; float gtDepth = 1.0f, gtSmooth = 4.0f;
    bool stOn = false; int stLen = 3; float stGate = 1.0f, stDecay = 0.0f;
    bool tsOn = false; float tsStop = 800.0f, tsStart = 300.0f;
    float dj = 0.0f, intensity = 0.0f, mix = 1.0f, outDb = 0.0f;
};

struct Transport { double bpm = 120.0; double ppq = 0.0; bool playing = false; };

// Cytomic / Simper trapezoidal SVF: stable under fast modulation
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set (double fc, double sr, double q)
    {
        fc = std::clamp (fc, 10.0, sr * 0.45);
        const double g = std::tan (kPi * fc / sr); k = (float) (1.0 / q);
        a1 = (float) (1.0 / (1.0 + g * (g + k))); a2 = (float) g * a1; a3 = (float) g * a2;
    }
    // returns lp, bp, hp through refs
    inline void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

inline float mixf (float a, float b, float t) { return t >= 1.0f ? b : t <= 0.0f ? a : a + t * (b - a); }

struct OnePole { float a = 0, z = 0; void set (double ms, double sr) { a = ms <= 0 ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * sr)); } float tick (float x) { z = x + a * (z - x); return z; } };

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        stBuf[0].assign ((size_t) (sr * 8.5) + 8, 0.0f); stBuf[1] = stBuf[0];
        tsBuf[0].assign ((size_t) (sr * 12.0) + 8, 0.0f); tsBuf[1] = tsBuf[0];
        fade.set (5.0, sr); slow.set (20.0, sr);
        reset();
    }
    void reset()
    {
        for (auto& c : ch) c = Chan {};
        for (auto& b : stBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : tsBuf) std::fill (b.begin(), b.end(), 0.0f);
        eDr = eFl = eGt = eDj = 0; gate = 1; stState = 0; stPos = 0; stX = 0; stGain = 1;
        speed = 1; lag = 0; tsW = 0; tsX = 0; prevTs = false; prevSt = false;
        cutS = -1; vowS = -1; djS = 0; outG = -1; ppqOwn = 0; filtCount = 0; heldPos = 0;
    }
    void setParams (const Params& p) { P = p; }
    const Params& params() const { return P; }

    // in place, L / R may be the same buffer for mono
    void render (float* L, float* R, int n, const Transport& t)
    {
        const double bpm = t.bpm > 20 && t.bpm < 999 ? t.bpm : 120.0;
        double ppq = t.playing ? t.ppq : ppqOwn;
        const double dppq = bpm / 60.0 / sr;
        const float drOn = P.drOn ? 1.0f : 0.0f, flOn = P.flOn ? 1.0f : 0.0f, gtOn = P.gtOn ? 1.0f : 0.0f;
        const float m = std::clamp (P.intensity, 0.0f, 1.0f);
        const float driveDb = P.drDb + 18.0f * m, reso = std::min (0.97f, P.flReso + (0.95f - P.flReso) * m * 0.5f), depth = P.gtDepth + (1.0f - P.gtDepth) * m;
        const float g = (float) std::pow (10.0, driveDb / 20.0), comp = (float) std::pow (g, -0.35);
        const double toneA = P.drTone >= 19500.0f ? 0.0 : std::exp (-2.0 * kPi * std::clamp ((double) P.drTone, 200.0, sr * 0.45) / sr);
        const int bits = std::clamp ((int) std::lround (16.0 - 12.0 * P.drDb / 36.0), 4, 16), hold = 1 + (int) (P.drDb / 6.0f);
        const float q = (float) (1 << (bits - 1));
        const double q0 = 0.5 + 9.5 * reso * reso;  // Q 0.5 .. 10
        OnePole gs; gs.set (std::max (0.5f, P.gtSmooth), sr);
        const char* pat = gatePattern (P.gtPattern); const double stepB = gateBeats (P.gtRate);
        const double lfoP = lfoBeats (P.flLfoRate);
        const float outT = (float) std::pow (10.0, P.outDb / 20.0); if (outG < 0) outG = outT;
        const float djT = std::abs (P.dj) < 0.01f ? 0.0f : P.dj;

        // stutter engage / release
        if (P.stOn && ! prevSt) { stState = 1; stPos = 0; stGain = 1; stLen = std::clamp ((int) (lenBeats (P.stLen) * 60.0 / bpm * sr), 64, (int) stBuf[0].size() - 4); }
        if (! P.stOn && prevSt && stState == 1) stState = 0;   // released before the slice was captured
        prevSt = P.stOn;
        const int fadeN = std::max (16, (int) (sr * 0.0015));

        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], R[i] };
            const float dry[2] = { x[0], x[1] };
            eDr = fade.tick2 (eDr, drOn); eFl = fade.tick2 (eFl, flOn); eGt = fade.tick2 (eGt, gtOn);
            // ---- drive
            if (eDr > 1e-5f)
                for (int c = 0; c < 2; ++c)
                {
                    float y;
                    switch (P.drMode)
                    {
                        case 1:  y = std::clamp (x[c] * g, -1.0f, 1.0f) * comp; break;
                        case 2:  y = (float) std::sin (std::clamp (x[c] * g, -64.0f, 64.0f) * (float) (kPi * 0.5)) * comp; break;
                        case 3:
                        {
                            auto& s = ch[(size_t) c];
                            if (++s.holdN >= hold) { s.holdN = 0; s.held = std::round (std::clamp (x[c], -1.0f, 1.0f) * q) / q; }
                            y = s.held; break;
                        }
                        default: y = std::tanh (x[c] * g) * comp; break;
                    }
                    auto& s = ch[(size_t) c];
                    s.tone = (float) (y + toneA * (s.tone - y)); y = s.tone;
                    x[c] = mixf (x[c], y, eDr);
                }
            // ---- filter (coefficients every 8 samples)
            const double lfo = std::sin (2.0 * kPi * ppq / lfoP);
            lfoNow = (float) lfo;
            if (eFl > 1e-5f)
            {
                if ((filtCount++ & 7) == 0)
                {
                    if (P.flMode == 3)
                    {
                        const float vt = std::clamp (P.flVowel + P.flLfoDepth * (float) lfo * 0.5f, 0.0f, 1.0f);
                        vowS = vowS < 0 ? vt : vowS + 0.05f * (vt - vowS);
                        static const float F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                        const float pos = vowS * 4.0f; const int a = std::min (3, (int) pos); const float fr = pos - (float) a;
                        for (int k = 0; k < 3; ++k) { const double f = F[a][k] + fr * (F[a + 1][k] - F[a][k]); for (auto& s : ch) s.fm[(size_t) k].set (f, sr, 4.0 + q0 * 1.5); }
                    }
                    else
                    {
                        const double ct = std::log2 (std::clamp ((double) P.flCut, 20.0, 20000.0)) + 4.0 * P.flLfoDepth * lfo;
                        cutS = cutS < 0 ? ct : cutS + 0.15 * (ct - cutS);
                        for (auto& s : ch) s.f.set (std::pow (2.0, cutS), sr, q0);
                    }
                }
                for (int c = 0; c < 2; ++c)
                {
                    auto& s = ch[(size_t) c]; float lp, bp, hp, y;
                    if (P.flMode == 3)
                    {
                        y = 0; static const float gk[3] = { 1.0f, 0.6f, 0.35f };
                        for (int k = 0; k < 3; ++k) { s.fm[(size_t) k].tick (x[c], lp, bp, hp); y += gk[k] * bp * s.fm[(size_t) k].k; }
                        y *= 0.9f;
                    }
                    else { s.f.tick (x[c], lp, bp, hp); y = P.flMode == 1 ? hp : P.flMode == 2 ? bp * s.f.k : lp; }
                    x[c] = mixf (x[c], y, eFl);
                }
            }
            // ---- trance gate
            {
                const double sp = ppq / stepB; const double fl = std::floor (sp);
                const int step = (int) (((long long) fl) & 15); const bool on = pat[step] == '1' && (sp - fl) < kGateDuty;
                gateStep = step;
                { const float t2 = on ? 1.0f : 1.0f - depth; gate = t2 + gs.a * (gate - t2); }
                const float gg = 1.0f + eGt * (gate - 1.0f);
                x[0] *= gg; x[1] *= gg;
            }
            // ---- stutter: capture one slice (live audio passes), then loop it with gate / decay; 1.5 ms fades at every edge
            {
                float st[2] = { x[0], x[1] };
                if (stState == 1)
                {
                    stBuf[0][(size_t) stPos] = x[0]; stBuf[1][(size_t) stPos] = x[1];
                    if (++stPos >= stLen) { stState = 2; stPos = 0; stGain = 1.0f - P.stDecay; }
                }
                else if (stState == 2)
                {
                    const int audible = std::max (fadeN * 2, (int) (stLen * std::clamp (P.stGate, 0.05f, 1.0f)));
                    float w = 0;
                    if (stPos < audible) w = std::min ({ 1.0f, (float) (stPos + 1) / (float) fadeN, (float) (audible - stPos) / (float) fadeN });
                    st[0] = stBuf[0][(size_t) stPos] * w * stGain; st[1] = stBuf[1][(size_t) stPos] * w * stGain;
                    if (++stPos >= stLen) { stPos = 0; stGain *= 1.0f - P.stDecay; }
                    if (! P.stOn) stState = 3;
                }
                const float tgt = stState == 2 ? 1.0f : 0.0f;
                stX = tgt > stX ? std::min (tgt, stX + 1.0f / (float) fadeN) : std::max (tgt, stX - 1.0f / (float) fadeN);
                if (stState == 3) { /* fading out the loop */ if (stX <= 0.0f) stState = 0; else { st[0] = stBuf[0][(size_t) stPos] * stGain; st[1] = stBuf[1][(size_t) stPos] * stGain; if (++stPos >= stLen) stPos = 0; } }
                x[0] = mixf (x[0], st[0], stX); x[1] = mixf (x[1], st[1], stX);
            }
            // ---- tape stop: read head slows to a halt (pitch and level fall), spins back up, then crossfades to live
            {
                const int N = (int) tsBuf[0].size();
                tsBuf[0][(size_t) tsW] = x[0]; tsBuf[1][(size_t) tsW] = x[1];
                if (P.tsOn) speed = std::max (0.0, speed - 1.0 / (std::max (10.0f, P.tsStop) * 0.001 * sr));
                else if (speed < 1.0) speed = P.tsStart <= 0.5f ? 1.0 : std::min (1.0, speed + 1.0 / (P.tsStart * 0.001 * sr));
                lag += 1.0 - speed;
                if (lag > N * 0.9) lag = N * 0.9;   // stopped for a long time: audio under the head is gone, but the head is silent here
                const bool active = lag > 0.0 || speed < 1.0;
                if (active)
                {
                    double rp = tsW - lag; while (rp < 0) rp += N;
                    const int i0 = (int) rp; const float fr = (float) (rp - i0);
                    const float lvl = (float) std::min (1.0, speed * 3.0);
                    float y[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto& b = tsBuf[(size_t) c];
                        const float xm1 = b[(size_t) ((i0 - 1 + N) % N)], x0 = b[(size_t) i0], x1 = b[(size_t) ((i0 + 1) % N)], x2 = b[(size_t) ((i0 + 2) % N)];
                        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                        y[c] = (((c3 * fr + c2) * fr + c1) * fr + x0) * lvl;
                    }
                    if (speed >= 1.0) { tsX -= 1.0f / (float) (fadeN * 6); if (tsX <= 0) { tsX = 0; lag = 0; } }   // back at speed: crossfade to live, then drop the lag
                    else tsX = 1.0f;
                    x[0] = mixf (x[0], y[0], tsX); x[1] = mixf (x[1], y[1], tsX);
                }
                else tsX = 0;
                if (++tsW >= N) tsW = 0;
                tapeSpeed = (float) speed;
            }
            // ---- DJ filter macro (bipolar: left = low-pass, right = high-pass)
            djS += 0.002f * (djT - djS);
            eDj = fade.tick2 (eDj, djT != 0.0f ? 1.0f : 0.0f);
            if (eDj > 1e-5f)
            {
                if ((filtCount & 7) == 1 || djDirty)
                {
                    const double f = djS < 0 ? 20000.0 * std::pow (2.0, 8.5 * djS) : 20.0 * std::pow (2.0, 9.5 * djS);
                    for (auto& s : ch) s.dj.set (f, sr, 0.9);
                    djDirty = false;
                }
                for (int c = 0; c < 2; ++c) { float lp, bp, hp; ch[(size_t) c].dj.tick (x[c], lp, bp, hp); x[c] = mixf (x[c], djS < 0 ? lp : hp, eDj); }
            }
            else djDirty = true;
            if (! (eFl > 1e-5f)) ++filtCount;
            // ---- mix + output
            outG += 0.001f * (outT - outG);
            const float mx = std::clamp (P.mix, 0.0f, 1.0f);
            L[i] = mixf (dry[0], x[0], mx) * outG;
            R[i] = mixf (dry[1], x[1], mx) * outG;
            ppq += dppq;
        }
        ppqOwn = ppq;
        float pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::fabs (L[i]), std::fabs (R[i]) });
        outPeak.store (pk, std::memory_order_relaxed);
        stutterOn.store (stState == 2 || stState == 3, std::memory_order_relaxed);
        stutterCapturing.store (stState == 1, std::memory_order_relaxed);
        speedNow.store (tapeSpeed, std::memory_order_relaxed);
        stepNow.store (gateStep, std::memory_order_relaxed);
        gateNow.store (gate, std::memory_order_relaxed);
        lfoOut.store (lfoNow, std::memory_order_relaxed);
    }

    // for the display / tests
    std::atomic<float> outPeak { 0 }, speedNow { 1 }, gateNow { 1 }, lfoOut { 0 };
    std::atomic<int> stepNow { 0 };
    std::atomic<bool> stutterOn { false }, stutterCapturing { false };
    double currentLag() const { return lag; }

private:
    struct Fade { float a = 0; void set (double ms, double sr) { a = (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * sr))); }
                  float tick2 (float v, float t) const { v += a * (t - v); return std::abs (t - v) < 1e-6f ? t : v; } };
    struct Chan { Svf f, dj; std::array<Svf, 3> fm; float tone = 0, held = 0; int holdN = 0; };
    struct GS : OnePole {};
    Params P;
    double sr = 48000;
    Fade fade, slow;
    std::array<Chan, 2> ch;
    std::array<std::vector<float>, 2> stBuf, tsBuf;
    float eDr = 0, eFl = 0, eGt = 0, eDj = 0, gate = 1, stX = 0, stGain = 1, tsX = 0, djS = 0, outG = -1, vowS = -1, lfoNow = 0, tapeSpeed = 1;
    int stState = 0, stPos = 0, stLen = 64, tsW = 0, gateStep = 0, heldPos = 0;
    unsigned filtCount = 0;
    double speed = 1, lag = 0, cutS = -1, ppqOwn = 0;
    bool prevTs = false, prevSt = false, djDirty = true;
};
} // namespace cw::dsp
