#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace glaze
{
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;

enum class Kind : int
{
    Float = 0,
    Bool,
    Choice
};

enum Param
{
    Input = 0,
    Bypass,
    // EQ
    EqOn, Hpf, LowF, LowG, LmF, LmG, HmF, HmG, HighF, HighG,
    // Glue compressor
    GlueOn, GThr, GRatio, GAtk, GRel, GMakeup, GMix,
    // Multiband
    MbOn, Xo1, Xo2, Mb1, Mb2, Mb3, Ml1, Ml2, Ml3,
    // Saturation
    SatOn, Drive, SatType, Tone, SatMix,
    // Stereo
    WidthOn, Width, BassMono,
    // Limiter
    LimOn, Push, Ceiling, LimRel,
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
    bool logScale;
};

inline constexpr const char *kSatLabels[3] = { "TAPE", "TUBE", "HARD" };

inline constexpr std::array<Definition, Count> definitions{{
    { "input", "INPUT", -12, 12, 0, "dB", Kind::Float, 0, nullptr, false },
    { "bypass", "BYPASS", 0, 1, 0, "", Kind::Bool, 2, nullptr, false },

    { "eqon", "EQ ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "hpf", "HPF", 10, 200, 10, "Hz", Kind::Float, 0, nullptr, true },
    { "lowf", "LOW FREQ", 30, 300, 90, "Hz", Kind::Float, 0, nullptr, true },
    { "lowg", "LOW", -8, 8, 0, "dB", Kind::Float, 0, nullptr, false },
    { "lmf", "LOW-MID FREQ", 120, 1200, 400, "Hz", Kind::Float, 0, nullptr, true },
    { "lmg", "LOW-MID", -8, 8, 0, "dB", Kind::Float, 0, nullptr, false },
    { "hmf", "HIGH-MID FREQ", 1500, 9000, 3500, "Hz", Kind::Float, 0, nullptr, true },
    { "hmg", "HIGH-MID", -8, 8, 0, "dB", Kind::Float, 0, nullptr, false },
    { "highf", "HIGH FREQ", 4000, 16000, 10000, "Hz", Kind::Float, 0, nullptr, true },
    { "highg", "HIGH", -8, 8, 0, "dB", Kind::Float, 0, nullptr, false },

    { "glueon", "GLUE ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "gthr", "THRESH", -30, 0, 0, "dB", Kind::Float, 0, nullptr, false },
    { "gratio", "RATIO", 1.2f, 10, 2, "", Kind::Float, 0, nullptr, true },
    { "gatk", "ATTACK", 0.1f, 50, 10, "ms", Kind::Float, 0, nullptr, true },
    { "grel", "RELEASE", 40, 1200, 200, "ms", Kind::Float, 0, nullptr, true },
    { "gmakeup", "MAKEUP", 0, 12, 0, "dB", Kind::Float, 0, nullptr, false },
    { "gmix", "MIX", 0, 1, 1, "", Kind::Float, 0, nullptr, false },

    { "mbon", "MULTI ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "xo1", "LOW/MID", 60, 400, 150, "Hz", Kind::Float, 0, nullptr, true },
    { "xo2", "MID/HIGH", 1000, 8000, 3000, "Hz", Kind::Float, 0, nullptr, true },
    { "mb1", "LOW DYNAMICS", 0, 1, 0, "", Kind::Float, 0, nullptr, false },
    { "mb2", "MID DYNAMICS", 0, 1, 0, "", Kind::Float, 0, nullptr, false },
    { "mb3", "HIGH DYNAMICS", 0, 1, 0, "", Kind::Float, 0, nullptr, false },
    { "ml1", "LOW LEVEL", -6, 6, 0, "dB", Kind::Float, 0, nullptr, false },
    { "ml2", "MID LEVEL", -6, 6, 0, "dB", Kind::Float, 0, nullptr, false },
    { "ml3", "HIGH LEVEL", -6, 6, 0, "dB", Kind::Float, 0, nullptr, false },

    { "saton", "SAT ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "drive", "DRIVE", 0, 24, 0, "dB", Kind::Float, 0, nullptr, false },
    { "sattype", "TYPE", 0, 2, 0, "", Kind::Choice, 3, kSatLabels, false },
    { "tone", "TONE", -1, 1, 0, "", Kind::Float, 0, nullptr, false },
    { "satmix", "SAT MIX", 0, 1, 0.5f, "", Kind::Float, 0, nullptr, false },

    { "widthon", "WIDTH ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "width", "WIDTH", 0, 2, 1, "", Kind::Float, 0, nullptr, false },
    { "bassmono", "BASS MONO", 0, 300, 0, "Hz", Kind::Float, 0, nullptr, false },

    { "limon", "LIMIT ON", 0, 1, 1, "", Kind::Bool, 2, nullptr, false },
    { "push", "PUSH", 0, 18, 0, "dB", Kind::Float, 0, nullptr, false },
    { "ceiling", "CEILING", -6, 0, -1, "dB", Kind::Float, 0, nullptr, false },
    { "limrel", "LIM RELEASE", 20, 1000, 150, "ms", Kind::Float, 0, nullptr, true },
}};

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
inline float dbToGain(float db) { return std::exp(db * 0.11512925464970229f); }
// A value that went through the host's normalised parameter range can come back a hair off zero (1e-6 dB): treat that as zero.
inline float dbToGainSnap(float db) { return std::abs(db) < 1.0e-3f ? 1.f : dbToGain(db); }
inline float gainToDb(float g) { return 20.f * std::log10(std::max(g, 1.0e-12f)); }
inline float clean(float x)
{
    return std::isfinite(x) && std::abs(x) > 1.0e-25f ? clampf(x, -32.f, 32.f) : 0.f;
}
inline float onePole(double sec, double sr)
{
    return float(1.0 - std::exp(-1.0 / (std::max(sec, 1.0e-5) * sr)));
}

inline bool hpfActive(float hz) { return hz >= 12.5f; }
inline bool bassMonoActive(float hz) { return hz >= 25.f; }

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

} // namespace glaze
