#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace fever
{
inline constexpr float kCeiling = 0.8912509381337456f; // -1 dBFS
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;
inline constexpr int kRatios = 5;
inline constexpr int kPeaks = 4;
inline constexpr int kMaxGrains = 48;
// Pitch-cloud ratios: octave down, fifth down, fifth up, octave up, octave+fifth up.
inline constexpr float kRatio[kRatios] = { 0.5f, 2.f / 3.f, 1.5f, 2.f, 3.f };
// Inharmonic resonator centres (Hz). Documented for the spectral-peak test.
inline constexpr float kPeakHz[kPeaks] = { 247.f, 619.f, 1553.f, 3901.f };
// Halo detune in cents at HALO = 1 (asymmetric, dissonant).
inline constexpr float kHaloCentsUp = 38.f;
inline constexpr float kHaloCentsDn = -51.f;
// Equivalent frequency shifts at 440 Hz (Hilbert SSB). A 440 Hz tone splits by these cents.
inline constexpr float kHaloUpHz = 9.7506094f;   // 440 * (2^(38/1200) - 1)
inline constexpr float kHaloDnHz = -12.778852f;  // 440 * (2^(-51/1200) - 1)
inline constexpr float kSpreadCents = 25.f; // max per-grain detune at SPREAD = 1

inline constexpr const char *syncNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/2" };
inline constexpr double syncBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 2.0 };
inline constexpr int syncCount = 5;

enum class Kind : int
{
    Float = 0,
    Bool,
    Choice
};

enum Param
{
    FeverAmt = 0,
    SideA,
    SideB,
    Mix,
    Output,
    GainMatch,
    LimiterOn,
    Bypass,
    Heartbeat,
    HeartRate,
    HeartDepth,
    ReduceMotion,
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
    { "fever", "FEVER", 0, 1, 0.5f, "", Kind::Float, 0, nullptr },
    { "sidea", "SIDE A", 0, 1, 0.5f, "", Kind::Float, 0, nullptr },
    { "sideb", "SIDE B", 0, 1, 0.5f, "", Kind::Float, 0, nullptr },
    { "mix", "MIX", 0, 1, 1, "", Kind::Float, 0, nullptr },
    { "output", "OUTPUT", -24, 12, 0, "dB", Kind::Float, 0, nullptr },
    { "gainmatch", "MATCH", 0, 1, 1, "", Kind::Bool, 2, nullptr },
    { "limiter", "LIMIT", 0, 1, 1, "", Kind::Bool, 2, nullptr },
    { "bypass", "BYPASS", 0, 1, 0, "", Kind::Bool, 2, nullptr },
    { "heartbeat", "HEART", 0, 1, 0, "", Kind::Bool, 2, nullptr },
    { "heartrate", "RATE", 0, 4, 0, "", Kind::Choice, 5, syncNames },
    { "heartdepth", "DEPTH", 0, 1, 0.35f, "", Kind::Float, 0, nullptr },
    { "reducemotion", "REDUCE MOTION", 0, 1, 0, "", Kind::Bool, 2, nullptr },
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
inline float centsToRatio(float cents) { return std::pow(2.f, cents / 1200.f); }
inline float loveAmt(float fever) { return clampf((0.5f - fever) * 2.f, 0.f, 1.f); }
inline float hateAmt(float fever) { return clampf((fever - 0.5f) * 2.f, 0.f, 1.f); }
inline bool onHateSide(float fever) { return fever > 0.5f; }
inline const char *sideAName(float fever) { return onHateSide(fever) ? "FREEZE" : "DENSITY"; }
inline const char *sideBName(float fever) { return onHateSide(fever) ? "HALO" : "SPREAD"; }

// Tail RT60 as a function of FREEZE. Used by the engine and documented for the decay test.
inline float tailRt60(float freeze)
{
    const float f = clampf(freeze, 0.f, 1.f);
    return 0.32f + 2.8f * f * f; // 0.32 s at 0, 1.02 s at 0.5, 3.12 s at 1
}

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

} // namespace fever
