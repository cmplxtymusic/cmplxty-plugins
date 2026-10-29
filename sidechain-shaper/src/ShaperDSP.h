#pragma once
// CMPLXTY Sidechain Shaper DSP (plain C++, no JUCE).
//
// A volume curve (points x 0..1 / y 0..1 with a bend per segment) is read once per cycle and multiplies the audio:
//   Tempo mode     - the cycle follows the host's song position (1/32 note ... 4 bars, triplets, dotted); free-runs at the host tempo
//                    (120 BPM if none) while the transport is stopped.
//   Sidechain mode - a transient detector on the sidechain input (or on the plug-in's own input) restarts the curve; between hits it holds
//                    the curve's last value. The curve still lasts the chosen note length at the host tempo.
// gain = 1 - depth * (1 - curve), smoothed by a one-pole (Smooth ms) so steps never click. Zero latency.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace sc { namespace dsp
{
constexpr int kTable = 1024;     // curve lookup table the audio thread reads
constexpr int kMaxPts = 64;

struct Pt { float x = 0, y = 1, t = 0; };   // t = bend of the segment that starts at this point (-1 .. 1, 0 = straight)
using Curve = std::vector<Pt>;

// segment bend: k > 0 starts slow and ends fast, k < 0 starts fast and ends slow (like a compressor's release)
inline float bend (float u, float t)
{
    const float k = std::max (-1.0f, std::min (1.0f, t)) * 9.0f;
    if (std::fabs (k) < 1.0e-3f) return u;
    return (std::exp (k * u) - 1.0f) / (std::exp (k) - 1.0f);
}

// sort, clamp, pin the end points to x = 0 and x = 1, limit the point count; never returns fewer than 2 points
inline Curve sanitize (Curve c)
{
    for (auto& p : c)
    {
        if (! std::isfinite (p.x)) p.x = 0;
        if (! std::isfinite (p.y)) p.y = 1;
        if (! std::isfinite (p.t)) p.t = 0;
        p.x = std::max (0.0f, std::min (1.0f, p.x)); p.y = std::max (0.0f, std::min (1.0f, p.y)); p.t = std::max (-1.0f, std::min (1.0f, p.t));
    }
    std::stable_sort (c.begin(), c.end(), [] (const Pt& a, const Pt& b) { return a.x < b.x; });
    if ((int) c.size() > kMaxPts) c.resize ((size_t) kMaxPts);
    if (c.empty()) c = { { 0, 1, 0 }, { 1, 1, 0 } };
    if (c.front().x > 0.0f) c.insert (c.begin(), Pt { 0.0f, c.front().y, 0.0f });
    if (c.back().x < 1.0f)  c.push_back (Pt { 1.0f, c.back().y, 0.0f });
    if (c.size() < 2) c.push_back (Pt { 1.0f, c.front().y, 0.0f });
    c.front().x = 0.0f; c.back().x = 1.0f;
    return c;
}

inline float evalCurve (const Curve& c, float x)
{
    if (c.size() < 2) return c.empty() ? 1.0f : c.front().y;
    x = std::max (0.0f, std::min (1.0f, x));
    size_t i = 0;
    while (i + 2 < c.size() && c[i + 1].x <= x) ++i;          // the last point that is <= x (ties: the later point wins, so steps are vertical)
    while (i + 2 < c.size() && c[i + 1].x == c[i + 2].x && c[i + 1].x <= x) ++i;
    const auto& a = c[i]; const auto& b = c[i + 1];
    const float w = b.x - a.x;
    if (w <= 1.0e-6f) return b.y;
    return a.y + (b.y - a.y) * bend ((x - a.x) / w, a.t);
}

// text form (stored in the plug-in state and in presets): "x y t;x y t;..." as integers in 1/10000 - no locale-dependent decimals
inline std::string formatCurve (const Curve& c)
{
    std::string s;
    for (auto& p : c)
    {
        if (! s.empty()) s += ';';
        s += std::to_string ((int) std::lround (p.x * 10000.0f)) + ' ' + std::to_string ((int) std::lround (p.y * 10000.0f)) + ' ' + std::to_string ((int) std::lround (p.t * 10000.0f));
    }
    return s;
}
inline Curve parseCurve (const std::string& s)
{
    Curve c; const char* p = s.c_str();
    while (*p != 0 && (int) c.size() < kMaxPts * 2)
    {
        char* e = nullptr; long v[3] = { 0, 10000, 0 }; int got = 0;
        for (int k = 0; k < 3; ++k)
        {
            while (*p == ' ') ++p;
            if (*p == ';' || *p == 0) break;
            const long n = std::strtol (p, &e, 10);
            if (e == p) { while (*p != 0 && *p != ';') ++p; break; }
            v[k] = n; ++got; p = e;
        }
        while (*p != 0 && *p != ';') ++p;
        if (*p == ';') ++p;
        if (got >= 2) c.push_back (Pt { (float) v[0] / 10000.0f, (float) v[1] / 10000.0f, (float) v[2] / 10000.0f });
    }
    return sanitize (c);
}

// ---- curve shapes (right-click menu of the curve editor, and the factory presets)
inline Curve steps (const std::vector<int>& on, float duty = 0.92f)   // gate pattern: one step per entry, 1 = open
{
    Curve c; const float n = (float) on.size(); const float edge = 0.004f;
    for (size_t i = 0; i < on.size(); ++i)
    {
        const float x0 = (float) i / n, x1 = x0 + duty / n;
        const float y = on[i] ? 1.0f : 0.0f;
        c.push_back ({ x0 + (i == 0 ? 0.0f : edge), y, 0 });
        c.push_back ({ x1, y, 0 });
        c.push_back ({ std::min (x1 + edge, (float) (i + 1) / n), 0.0f, 0 });
    }
    c.push_back ({ 1.0f, on.empty() || on[0] ? 1.0f : 0.0f, 0 });
    c.front().x = 0.0f;
    return sanitize (c);
}
inline Curve sine (int pts = 24)
{
    Curve c;
    for (int i = 0; i <= pts; ++i) { const float x = (float) i / (float) pts; c.push_back ({ x, 0.5f + 0.5f * std::cos (6.2831853f * x), 0 }); }
    return sanitize (c);
}
struct Shape { const char* name; Curve pts; };
inline const std::vector<Shape>& shapes()
{
    static const std::vector<Shape> s {
        { "Pump",          { { 0, 0, -0.55f }, { 0.62f, 1, 0 }, { 1, 1, 0 } } },
        { "Hard Pump",     { { 0, 0, 0 }, { 0.10f, 0, -0.35f }, { 0.55f, 1, 0 }, { 1, 1, 0 } } },
        { "Soft Duck",     { { 0, 0.45f, -0.45f }, { 0.55f, 1, 0 }, { 1, 1, 0 } } },
        { "Short Duck",    { { 0, 0, -0.6f }, { 0.28f, 1, 0 }, { 1, 1, 0 } } },
        { "Long Release",  { { 0, 0, -0.25f }, { 0.92f, 1, 0 }, { 1, 1, 0 } } },
        { "Gate Half",     steps ({ 1, 0 }, 0.96f) },
        { "Chop 8",        steps ({ 1, 1, 1, 1, 1, 1, 1, 1 }, 0.6f) },
        { "Trance Gate",   steps ({ 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1 }, 0.82f) },
        { "Stutter",       steps ({ 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1 }, 0.55f) },
        { "Ramp Up",       { { 0, 0, 0.3f }, { 1, 1, 0 } } },
        { "Ramp Down",     { { 0, 1, -0.3f }, { 1, 0, 0 } } },
        { "Triangle",      { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } } },
        { "Sine",          sine() },
        { "Swell",         { { 0, 0, 0.65f }, { 0.97f, 1, 0 }, { 1, 1, 0 } } },
        { "Flat",          { { 0, 1, 0 }, { 1, 1, 0 } } },
    };
    return s;
}
inline Curve shapeByName (const std::string& n)
{
    for (auto& s : shapes()) if (n == s.name) return sanitize (s.pts);
    return sanitize (shapes().front().pts);
}
inline Curve defaultCurve() { return shapeByName ("Pump"); }

// ---- note lengths
constexpr int kNumRates = 14;
inline const char* rateNames() { return "1/32|1/16|1/8|1/4|1/2|1 Bar|2 Bars|4 Bars|1/16 T|1/8 T|1/4 T|1/16 D|1/8 D|1/4 D"; }
inline double rateBeats (int i, double barBeats = 4.0)   // in quarter notes
{
    static const double q[] = { 0.125, 0.25, 0.5, 1.0, 2.0, -1.0, -2.0, -4.0, 0.25 * 2 / 3, 0.5 * 2 / 3, 1.0 * 2 / 3, 0.375, 0.75, 1.5 };
    const double v = q[std::max (0, std::min (kNumRates - 1, i))];
    return v < 0 ? -v * (barBeats > 0.1 ? barBeats : 4.0) : v;
}

// zero for denormals, inf and NaN; tests the exponent bits, so it costs nothing even without flush-to-zero
inline float clean (float v) noexcept { std::uint32_t u; std::memcpy (&u, &v, 4); const std::uint32_t e = u & 0x7f800000u; return (e == 0 || e == 0x7f800000u) ? 0.0f : v; }

struct Params
{
    int mode = 0;            // 0 tempo, 1 sidechain
    int rate = 3;
    float offset = 0;        // tempo mode: phase offset 0..1 of a cycle
    float depth = 1;
    float smoothMs = 1.5f;
    float stereo = 0;        // right channel offset 0..0.5 of a cycle
    int source = 0;          // 0 external sidechain (falls back to the input when none is connected), 1 the plug-in's input
    float thrDb = -24;
    float focusHz = 20000;   // detector lowpass
    float holdMs = 60;       // minimum time between triggers
    float mix = 1;
    float gainDb = 0;
};
struct TimeInfo { double bpm = 120; double ppq = 0; bool playing = false; bool hasPpq = false; double barBeats = 4; };

class Engine
{
public:
    std::array<std::atomic<float>, kTable> table;           // written by the message thread (setCurve), read here; a torn update lasts one block
    std::atomic<float> phaseL { 0 }, phaseR { 0 }, gainL { 1 }, gainR { 1 }, inLevel { 0 }, outLevel { 0 }, detLevel { 0 };
    std::atomic<int> triggers { 0 }, usingExternal { 0 };

    Engine() { for (auto& v : table) v.store (1.0f); setCurve (defaultCurve()); }
    void setCurve (const Curve& c)
    {
        const auto s = sanitize (c);
        for (int i = 0; i < kTable; ++i) table[(size_t) i].store (evalCurve (s, (float) i / (float) (kTable - 1)), std::memory_order_relaxed);
    }
    float lookup (float x) const noexcept
    {
        x = std::max (0.0f, std::min (1.0f, x)) * (float) (kTable - 1);
        const int i = std::min (kTable - 2, (int) x); const float f = x - (float) i;
        const float a = table[(size_t) i].load (std::memory_order_relaxed), b = table[(size_t) i + 1].load (std::memory_order_relaxed);
        return a + (b - a) * f;
    }

    void setParams (const Params& np) { p = np; }
    const Params& params() const { return p; }
    void prepare (double sampleRate, int) { sr = sampleRate > 0 ? sampleRate : 48000.0; reset(); }
    void reset()
    {
        freeBeat = 0; scPos = 1.0; env = 0; lp1 = lp2 = 0; hold = 0; armed = true;
        gL = gR = (double) lookup (0.0f); outG = std::pow (10.0, p.gainDb / 20.0);
        if (p.depth < 1.0f) gL = gR = 1.0 - p.depth * (1.0 - gL);
    }

    // io: main channels (processed in place). sc / scCh: the sidechain input (nullptr / 0 when none).
    void process (float* const* io, int nch, int n, const float* const* sc, int scCh, const TimeInfo& ti)
    {
        if (n <= 0) return;
        const double bpm = (std::isfinite (ti.bpm) && ti.bpm >= 20 && ti.bpm <= 999) ? ti.bpm : 120.0;
        const double cycle = rateBeats (p.rate, ti.barBeats);
        const double beatsPerSample = bpm / 60.0 / sr;
        const bool ext = p.source == 0 && sc != nullptr && scCh > 0;
        usingExternal.store (ext ? 1 : 0, std::memory_order_relaxed);
        const double sA = p.smoothMs > 0.01f ? std::exp (-1.0 / (p.smoothMs * 0.001 * sr)) : 0.0;
        const double gainT = std::pow (10.0, std::max (-60.0f, std::min (24.0f, p.gainDb)) / 20.0);
        const double gA = std::exp (-1.0 / (0.02 * sr));
        const double lpA = p.focusHz < 19000.0f ? std::exp (-6.283185307 * std::max (20.0f, p.focusHz) / sr) : 0.0;
        const double relA = std::exp (-1.0 / (0.03 * sr));
        const double thr = std::pow (10.0, std::max (-80.0f, std::min (0.0f, p.thrDb)) / 20.0), rearm = thr * 0.5;
        const double holdS = std::max (1.0f, p.holdMs) * 0.001 * sr;
        const double depth = std::max (0.0f, std::min (1.0f, p.depth)), mix = std::max (0.0f, std::min (1.0f, p.mix));
        const double stereo = std::max (0.0f, std::min (0.5f, p.stereo));
        const bool sideMode = p.mode == 1;
        double pkIn = 0, pkOut = 0, pkDet = 0, phL = 0, phR = 0;

        double beat0 = freeBeat;
        if (ti.playing && ti.hasPpq && std::isfinite (ti.ppq)) beat0 = ti.ppq;
        for (int i = 0; i < n; ++i)
        {
            // detector (both modes, so the display can show it)
            double d = 0;
            if (ext) { for (int c = 0; c < std::min (2, scCh); ++c) d += clean (sc[c][i]); d /= (double) std::min (2, scCh); }
            else     { for (int c = 0; c < std::min (2, nch); ++c) d += clean (io[c][i]); d /= (double) std::max (1, std::min (2, nch)); }
            if (std::fabs (d) < 1.0e-30) d = 0;
            if (lpA > 0) { lp1 = d + (lp1 - d) * lpA; lp2 = lp1 + (lp2 - lp1) * lpA; d = lp2 * 1.3; }
            const double a = std::fabs (d);
            env = a > env ? a : env * relA;
            pkDet = std::max (pkDet, env);
            if (hold > 0) hold -= 1.0;
            if (! armed && env < rearm) armed = true;
            if (sideMode && armed && hold <= 0 && env > thr) { scPos = 0.0; armed = false; hold = holdS; triggers.fetch_add (1, std::memory_order_relaxed); }

            if (sideMode)
            {
                phL = std::min (scPos, 0.99999);
                const double r = scPos - stereo;
                phR = r < 0 ? 0.99999 : std::min (r, 0.99999);
                scPos = std::min (2.0, scPos + beatsPerSample / cycle);
            }
            else
            {
                const double beat = beat0 + (double) i * beatsPerSample;
                double ph = beat / cycle + p.offset; ph -= std::floor (ph);
                phL = ph; phR = ph + stereo; phR -= std::floor (phR);
            }
            const double tL = 1.0 - depth * (1.0 - (double) lookup ((float) phL));
            const double tR = 1.0 - depth * (1.0 - (double) lookup ((float) phR));
            gL = tL + (gL - tL) * sA; gR = tR + (gR - tR) * sA;
            outG = gainT + (outG - gainT) * gA;
            const double mL = (mix * gL + (1.0 - mix)) * outG, mR = (mix * gR + (1.0 - mix)) * outG;
            for (int c = 0; c < nch; ++c)
            {
                const double x = clean (io[c][i]);
                pkIn = std::max (pkIn, std::fabs (x));
                const double y = x * (c == 1 ? mR : mL);
                pkOut = std::max (pkOut, std::fabs (y));
                io[c][i] = (float) y;
            }
        }
        freeBeat = beat0 + (double) n * beatsPerSample;
        if (freeBeat > 1.0e7) freeBeat = std::fmod (freeBeat, 64.0);
        if (! std::isfinite (gL) || ! std::isfinite (gR)) gL = gR = 1.0;
        if (! std::isfinite (env)) env = 0;
        if (std::fabs (lp1) < 1e-20) lp1 = 0;
        if (std::fabs (lp2) < 1e-20) lp2 = 0;
        phaseL.store ((float) phL, std::memory_order_relaxed); phaseR.store ((float) phR, std::memory_order_relaxed);
        gainL.store ((float) gL, std::memory_order_relaxed); gainR.store ((float) gR, std::memory_order_relaxed);
        inLevel.store ((float) pkIn, std::memory_order_relaxed); outLevel.store ((float) pkOut, std::memory_order_relaxed);
        detLevel.store ((float) pkDet, std::memory_order_relaxed);
    }

private:
    Params p;
    double sr = 48000.0, freeBeat = 0, scPos = 1.0, env = 0, lp1 = 0, lp2 = 0, hold = 0, gL = 1, gR = 1, outG = 1;
    bool armed = true;
};
}} // namespace sc::dsp
