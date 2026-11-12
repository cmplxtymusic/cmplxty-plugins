#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace rigged
{
inline constexpr int kFxCount = 11;
inline constexpr int kHistory = 32;
inline constexpr float kCeiling = 0.8912509381337456f; // -1 dBFS
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;

inline constexpr const char *fxNames[kFxCount] = { "FILTER", "DRIVE",  "CRUSH", "RING",    "CHORUS", "PHASER",
                                                   "DELAY",  "REVERB", "COMP",  "TREMOLO", "WIDTH" };
inline constexpr const char *fxIds[kFxCount] = { "flt",  "drv",  "crsh", "ring", "chor", "phas",
                                                 "dlay", "rvrb", "comp", "trem", "wdth" };

inline constexpr const char *filterModes[] = { "LP", "HP", "BP" };
inline constexpr const char *syncNames[] = { "1/4", "1/8", "1/8D", "1/16", "1/16D", "1/32", "1/4T", "1/8T" };
inline constexpr double syncBeats[] = { 1.0, 0.5, 0.75, 0.25, 0.375, 0.125, 2.0 / 3.0, 1.0 / 3.0 };
inline constexpr int syncCount = 8;
inline constexpr const char *tremShapes[] = { "SINE", "TRI", "SQUARE" };

enum class Fx : int
{
    Filter = 0,
    Drive,
    Crush,
    Ring,
    Chorus,
    Phaser,
    Delay,
    Reverb,
    Comp,
    Tremolo,
    Width
};

enum class Kind : int
{
    Float = 0,
    Bool,
    Choice
};

enum Param
{
    Mix = 0,
    Output,
    GainMatch,
    LimiterOn,
    Bypass,
    Randomize,
    Amount,
    ReduceMotion,
    FltOn,
    FltLock,
    FltMode,
    FltCut,
    FltRes,
    FltMix,
    DrvOn,
    DrvLock,
    DrvAmt,
    DrvTone,
    DrvMix,
    CrshOn,
    CrshLock,
    CrshBits,
    CrshRate,
    CrshMix,
    RingOn,
    RingLock,
    RingFreq,
    RingMix,
    ChorOn,
    ChorLock,
    ChorRate,
    ChorDepth,
    ChorMix,
    PhasOn,
    PhasLock,
    PhasRate,
    PhasDepth,
    PhasFb,
    PhasMix,
    DlayOn,
    DlayLock,
    DlaySync,
    DlayFb,
    DlayMix,
    RvrbOn,
    RvrbLock,
    RvrbSize,
    RvrbDamp,
    RvrbMix,
    CompOn,
    CompLock,
    CompThres,
    CompRatio,
    CompAtk,
    CompRel,
    TremOn,
    TremLock,
    TremSync,
    TremDepth,
    TremShape,
    WdthOn,
    WdthLock,
    WdthAmt,
    Count
};

struct Definition
{
    const char *id, *name;
    float low, high, initial;
    const char *unit;
    Kind kind;
    int choices;
    const char *const *labels;
    bool skew; // log-ish centre skew for frequency
};

inline constexpr std::array<Definition, Count> definitions{{
    { "mix", "MIX", 0, 1, 1, "", Kind::Float, 0, nullptr, false },
    { "output", "OUTPUT", -24, 12, 0, "dB", Kind::Float, 0, nullptr, false },
    { "gainmatch", "MATCH", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "limiter", "LIMIT", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "bypass", "BYPASS", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "randomize", "RANDOMIZE", 0, 1, 0, "", Kind::Float, 0, nullptr, false },
    { "amount", "AMOUNT", 0, 1, 0.75f, "", Kind::Float, 0, nullptr, false },
    { "reducemotion", "REDUCE MOTION", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "flt_on", "FILTER ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "flt_lock", "FILTER LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "flt_mode", "FILTER MODE", 0, 2, 0, "", Kind::Choice, 3, filterModes, false },
    { "flt_cut", "CUTOFF", 20, 20000, 12000, "Hz", Kind::Float, 0, nullptr, true },
    { "flt_res", "RESO", 0.3f, 8, 0.70710678f, "", Kind::Float, 0, nullptr, false },
    { "flt_mix", "FILTER MIX", 0, 1, 1, "", Kind::Float, 0, nullptr, false },
    { "drv_on", "DRIVE ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "drv_lock", "DRIVE LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "drv_amt", "DRIVE", 0, 1, 0.25f, "", Kind::Float, 0, nullptr, false },
    { "drv_tone", "TONE", 0, 1, 0.5f, "", Kind::Float, 0, nullptr, false },
    { "drv_mix", "DRIVE MIX", 0, 1, 1, "", Kind::Float, 0, nullptr, false },
    { "crsh_on", "CRUSH ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "crsh_lock", "CRUSH LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "crsh_bits", "BITS", 1, 16, 12, "", Kind::Float, 0, nullptr, false },
    { "crsh_rate", "DOWNSAMPLE", 1, 32, 1, "x", Kind::Float, 0, nullptr, false },
    { "crsh_mix", "CRUSH MIX", 0, 1, 1, "", Kind::Float, 0, nullptr, false },
    { "ring_on", "RING ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "ring_lock", "RING LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "ring_freq", "RING FREQ", 20, 8000, 440, "Hz", Kind::Float, 0, nullptr, true },
    { "ring_mix", "RING MIX", 0, 1, 0.5f, "", Kind::Float, 0, nullptr, false },
    { "chor_on", "CHORUS ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "chor_lock", "CHORUS LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "chor_rate", "CHORUS RATE", 0.05f, 8, 0.8f, "Hz", Kind::Float, 0, nullptr, true },
    { "chor_depth", "CHORUS DEPTH", 0, 1, 0.45f, "", Kind::Float, 0, nullptr, false },
    { "chor_mix", "CHORUS MIX", 0, 1, 0.4f, "", Kind::Float, 0, nullptr, false },
    { "phas_on", "PHASER ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "phas_lock", "PHASER LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "phas_rate", "PHASER RATE", 0.05f, 8, 0.35f, "Hz", Kind::Float, 0, nullptr, true },
    { "phas_depth", "PHASER DEPTH", 0, 1, 0.6f, "", Kind::Float, 0, nullptr, false },
    { "phas_fb", "PHASER FB", 0, 0.9f, 0.35f, "", Kind::Float, 0, nullptr, false },
    { "phas_mix", "PHASER MIX", 0, 1, 0.5f, "", Kind::Float, 0, nullptr, false },
    { "dlay_on", "DELAY ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "dlay_lock", "DELAY LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "dlay_sync", "DELAY SYNC", 0, 7, 1, "", Kind::Choice, 8, syncNames, false },
    { "dlay_fb", "DELAY FB", 0, 0.95f, 0.35f, "", Kind::Float, 0, nullptr, false },
    { "dlay_mix", "DELAY MIX", 0, 1, 0.3f, "", Kind::Float, 0, nullptr, false },
    { "rvrb_on", "REVERB ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "rvrb_lock", "REVERB LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "rvrb_size", "SIZE", 0, 1, 0.45f, "", Kind::Float, 0, nullptr, false },
    { "rvrb_damp", "DAMP", 0, 1, 0.4f, "", Kind::Float, 0, nullptr, false },
    { "rvrb_mix", "REVERB MIX", 0, 1, 0.3f, "", Kind::Float, 0, nullptr, false },
    { "comp_on", "COMP ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "comp_lock", "COMP LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "comp_thres", "THRESHOLD", -40, 0, -18, "dB", Kind::Float, 0, nullptr, false },
    { "comp_ratio", "RATIO", 1, 20, 3, ":1", Kind::Float, 0, nullptr, false },
    { "comp_atk", "ATTACK", 0.1f, 50, 5, "ms", Kind::Float, 0, nullptr, true },
    { "comp_rel", "RELEASE", 10, 500, 80, "ms", Kind::Float, 0, nullptr, true },
    { "trem_on", "TREMOLO ON", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "trem_lock", "TREMOLO LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "trem_sync", "TREMOLO SYNC", 0, 7, 1, "", Kind::Choice, 8, syncNames, false },
    { "trem_depth", "TREMOLO DEPTH", 0, 1, 0.55f, "", Kind::Float, 0, nullptr, false },
    { "trem_shape", "TREMOLO SHAPE", 0, 2, 0, "", Kind::Choice, 3, tremShapes, false },
    { "wdth_on", "WIDTH ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "wdth_lock", "WIDTH LOCK", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },
    { "wdth_amt", "WIDTH", 0, 2, 1, "", Kind::Float, 0, nullptr, false },
}};

// Parameter indices that belong to each effect, in display order (skip on/lock for knobs).
inline constexpr int fxFirst[kFxCount] = { FltOn,  DrvOn,  CrshOn, RingOn, ChorOn, PhasOn,
                                           DlayOn, RvrbOn, CompOn, TremOn, WdthOn };
inline constexpr int fxCountParams[kFxCount] = { 6, 5, 5, 4, 5, 6, 5, 5, 6, 5, 3 };

inline int fxOf(int p)
{
    for (int i = 0; i < kFxCount; ++i)
        if (p >= fxFirst[i] && p < fxFirst[i] + fxCountParams[i])
            return i;
    return -1;
}

inline bool isLockParam(int p)
{
    switch (p)
    {
    case FltLock:
    case DrvLock:
    case CrshLock:
    case RingLock:
    case ChorLock:
    case PhasLock:
    case DlayLock:
    case RvrbLock:
    case CompLock:
    case TremLock:
    case WdthLock:
        return true;
    default:
        return false;
    }
}

inline bool isEnableParam(int p)
{
    switch (p)
    {
    case FltOn:
    case DrvOn:
    case CrshOn:
    case RingOn:
    case ChorOn:
    case PhasOn:
    case DlayOn:
    case RvrbOn:
    case CompOn:
    case TremOn:
    case WdthOn:
        return true;
    default:
        return false;
    }
}

inline int lockOf(int fx)
{
    return fxFirst[fx] + 1;
}

inline int enableOf(int fx)
{
    return fxFirst[fx];
}

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
inline float dbToGain(float db) { return std::pow(10.f, db * 0.05f); }
inline float gainToDb(float g) { return 20.f * std::log10(std::max(g, 1.0e-12f)); }
inline float clean(float x)
{
    return std::isfinite(x) && std::abs(x) > 1.0e-25f ? clampf(x, -32.f, 32.f) : 0.f;
}
inline float onePole(double sec, double sr)
{
    return float(1.0 - std::exp(-1.0 / (std::max(sec, 1.0e-5) * sr)));
}

// Musical ranges used by the smart roll (narrower than the raw parameter span).
inline void smartRange(int p, float &lo, float &hi)
{
    lo = definitions[(size_t)p].low;
    hi = definitions[(size_t)p].high;
    switch (p)
    {
    case FltCut:
        lo = 500;
        hi = 6000;
        break;
    case FltRes:
        lo = 0.5f;
        hi = 1.6f;
        break;
    case DrvAmt:
        lo = 0.05f;
        hi = 0.7f;
        break;
    case CrshBits:
        lo = 5;
        hi = 14;
        break;
    case CrshRate:
        lo = 1;
        hi = 10;
        break;
    case RingFreq:
        lo = 60;
        hi = 800;
        break;
    case RingMix:
        lo = 0.08f;
        hi = 0.4f;
        break;
    case ChorRate:
    case PhasRate:
        lo = 0.12f;
        hi = 2.8f;
        break;
    case ChorDepth:
        lo = 0.15f;
        hi = 0.75f;
        break;
    case PhasDepth:
        lo = 0.2f;
        hi = 0.85f;
        break;
    case PhasFb:
        lo = 0.05f;
        hi = 0.6f;
        break;
    case DlayFb:
        lo = 0.08f;
        hi = 0.62f;
        break;
    case RvrbSize:
        lo = 0.18f;
        hi = 0.8f;
        break;
    case CompThres:
        lo = -28;
        hi = -8;
        break;
    case CompRatio:
        lo = 1.6f;
        hi = 8;
        break;
    case TremDepth:
        lo = 0.15f;
        hi = 0.8f;
        break;
    case WdthAmt:
        lo = 0.55f;
        hi = 1.55f;
        break;
    default:
        break;
    }
}

struct Settings
{
    std::array<float, Count> v{};
    std::array<int, kFxCount> order{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    Settings()
    {
        for (int i = 0; i < Count; ++i)
            v[(size_t)i] = definitions[(size_t)i].initial;
    }

    float operator[](int i) const { return v[(size_t)i]; }
    float &operator[](int i) { return v[(size_t)i]; }

    bool on(int fx) const { return v[(size_t)enableOf(fx)] >= 0.5f; }
    bool locked(int fx) const { return v[(size_t)lockOf(fx)] >= 0.5f; }

    bool operator==(const Settings &o) const
    {
        for (int i = 0; i < Count; ++i)
            if (v[(size_t)i] != o.v[(size_t)i])
                return false;
        return order == o.order;
    }
};

inline uint32_t nextRng(uint32_t &r)
{
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}

inline float rand01(uint32_t &r)
{
    return (nextRng(r) >> 8) * (1.0f / 16777216.0f);
}

// Move each unlocked, rollable parameter from its current value toward a musical target,
// by `amount` (0 = no change, 1 = land on the target). Level-safe constraints applied after.
inline Settings smartRoll(const Settings &in, float amount, uint32_t &rng)
{
    Settings o = in;
    amount = clampf(amount, 0, 1);
    auto roll = [&](int p) {
        if (p < 0 || p >= Count)
            return;
        if (p == Mix || p == Output || p == GainMatch || p == LimiterOn || p == Bypass || p == Randomize ||
            p == Amount || p == ReduceMotion)
            return;
        if (isLockParam(p))
            return;
        const int fx = fxOf(p);
        if (fx >= 0 && o.locked(fx))
            return;
        const auto &d = definitions[(size_t)p];
        if (isEnableParam(p))
        {
            // Enables stay put: you set the rack up, then roll the knobs. LOCK already
            // protects a whole effect. Leaving enables alone also keeps rolls level-safe.
            return;
        }
        if (d.kind == Kind::Choice)
        {
            const int cur = clampi((int)std::round(o[p]), 0, d.choices - 1);
            const int target = (int)(rand01(rng) * (float)d.choices) % d.choices;
            const int stepped = cur + (int)std::round(amount * (float)(target - cur));
            o[p] = (float)clampi(stepped, 0, d.choices - 1);
            return;
        }
        float lo, hi;
        smartRange(p, lo, hi);
        float target;
        if (d.skew && lo > 0 && hi > 0)
            target = lo * std::pow(hi / lo, rand01(rng));
        else
            target = lo + rand01(rng) * (hi - lo);
        o[p] = o[p] + amount * (target - o[p]);
        o[p] = clampf(o[p], d.low, d.high);
    };
    for (int p = 0; p < Count; ++p)
        roll(p);

    // Level-safe: don't stack heavy drive with a deep crush, and cap delay feedback.
    if (!o.locked((int)Fx::Drive) && !o.locked((int)Fx::Crush) && o.on((int)Fx::Drive) && o.on((int)Fx::Crush))
    {
        if (o[DrvAmt] > 0.5f && o[CrshBits] < 7.f)
            o[CrshBits] = 7.f + 3.f * rand01(rng);
        if (o[DrvAmt] > 0.65f && o[CrshRate] > 8.f)
            o[CrshRate] = 4.f + 3.f * rand01(rng);
    }
    if (!o.locked((int)Fx::Delay) && o[DlayFb] > 0.7f)
        o[DlayFb] = 0.7f;
    // At least one effect stays on so a roll cannot produce complete silence.
    bool any = false;
    for (int i = 0; i < kFxCount; ++i)
        if (o.on(i))
            any = true;
    if (!any)
    {
        for (int i = 0; i < kFxCount; ++i)
            if (!o.locked(i))
            {
                o[enableOf(i)] = 1.f;
                break;
            }
    }
    return o;
}

inline std::string orderString(const Settings &s)
{
    std::string o;
    for (int i = 0; i < kFxCount; ++i)
    {
        if (i)
            o += ',';
        o += std::to_string(s.order[(size_t)i]);
    }
    return o;
}

inline void parseOrder(Settings &s, const std::string &str)
{
    std::array<int, kFxCount> o{};
    std::array<bool, kFxCount> seen{};
    int n = 0;
    size_t i = 0;
    while (i < str.size() && n < kFxCount)
    {
        int v = 0;
        bool digit = false;
        while (i < str.size() && str[i] >= '0' && str[i] <= '9')
        {
            v = v * 10 + (str[i] - '0');
            ++i;
            digit = true;
        }
        if (digit)
        {
            o[(size_t)n++] = v;
        }
        if (i < str.size())
            ++i;
    }
    if (n != kFxCount)
        return;
    for (int k = 0; k < kFxCount; ++k)
    {
        if (o[(size_t)k] < 0 || o[(size_t)k] >= kFxCount || seen[(size_t)o[(size_t)k]])
            return;
        seen[(size_t)o[(size_t)k]] = true;
    }
    s.order = o;
}

} // namespace rigged
