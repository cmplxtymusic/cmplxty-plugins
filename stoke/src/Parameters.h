#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace stoke
{
inline constexpr float kCeiling = 0.8912509381337456f; // -1 dBFS
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;
inline constexpr float kCentreMin = 300.f;
inline constexpr float kCentreMax = 5000.f;
inline constexpr float kCentreDefault = 1650.f;
inline constexpr float kWidthMin = 0.5f;
inline constexpr float kWidthMax = 3.0f;
inline constexpr float kWidthDefault = 1.6f;
inline constexpr float kMaxBoostDb = 9.0f;
inline constexpr int kHarshBands = 3;
inline constexpr float kHarshHz[kHarshBands] = { 2200.f, 3400.f, 5100.f };
inline constexpr float kMudHz = 330.f;

enum class Kind : int
{
    Float = 0,
    Bool,
    Choice
};

enum Param
{
    Focus = 0,
    Centre,
    Width,
    Glow,
    Smooth,
    MidOnly,
    Mix,
    Output,
    GainMatch,
    LimiterOn,
    Bypass,
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
};

inline constexpr std::array<Definition, Count> definitions{{
    { "focus", "FOCUS", 0, 1, 0.50f, "", Kind::Float, 0, nullptr },
    { "centre", "CENTRE", kCentreMin, kCentreMax, kCentreDefault, "Hz", Kind::Float, 0, nullptr },
    { "width", "WIDTH", kWidthMin, kWidthMax, kWidthDefault, "oct", Kind::Float, 0, nullptr },
    { "glow", "GLOW", 0, 1, 0.28f, "", Kind::Float, 0, nullptr },
    { "smooth", "SMOOTH", 0, 1, 0.55f, "", Kind::Float, 0, nullptr },
    { "midonly", "MID ONLY", 0, 1, 0, "", Kind::Bool, 2, nullptr },
    { "mix", "MIX", 0, 1, 1, "", Kind::Float, 0, nullptr },
    { "output", "OUTPUT", -24, 12, 0, "dB", Kind::Float, 0, nullptr },
    { "gainmatch", "MATCH", 0, 1, 1, "", Kind::Bool, 2, nullptr },
    { "limiter", "LIMIT", 0, 1, 1, "", Kind::Bool, 2, nullptr },
    { "bypass", "BYPASS", 0, 1, 0, "", Kind::Bool, 2, nullptr },
}};

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

// Map FOCUS (0..1) to a mid-band boost in dB. Nearly linear, 4.5 dB at the default 0.5, 9 dB at 1.
inline float focusBoostDb(float focus)
{
    const float f = clampf(focus, 0.f, 1.f);
    return kMaxBoostDb * f;
}

// Bandwidth in octaves -> peaking Q (RBJ / analog prototype).
inline float widthToQ(float oct)
{
    const float bw = clampf(oct, kWidthMin, kWidthMax);
    return 1.f / (2.f * std::sinh(0.5f * std::log(2.f) * bw));
}

inline float centreHz(float v) { return clampf(v, kCentreMin, kCentreMax); }

// A knob is "doing something" when FOCUS is off the floor (and, for GLOW, when GLOW itself is up).
inline bool focusActive(float focus) { return focus > 0.015f; }
inline bool glowActive(float focus, float glow) { return focusActive(focus) && glow > 0.015f; }
inline bool smoothActive(float focus, float smooth) { return focusActive(focus) && smooth > 0.015f; }
inline bool midOnlyActive(float focus, float midOnly) { return focusActive(focus) && midOnly >= 0.5f; }
inline bool matchActive(float focus, float match) { return focusActive(focus) && match >= 0.5f; }

struct Settings
{
    std::array<float, Count> v{};
    Settings()
    {
        for (int i = 0; i < Count; ++i)
            v[(size_t)i] = definitions[(size_t)i].initial;
    }
    float operator[](int i) const { return v[(size_t)i]; }
    float &operator[](int i) { return v[(size_t)i]; }
};

} // namespace stoke
