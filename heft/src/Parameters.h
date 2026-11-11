#pragma once
#include <array>
namespace heft
{
struct Definition
{
    const char *id;
    const char *name;
    float low, high, initial;
    const char *unit;
};
inline constexpr std::array<Definition, 19> definitions{{{"slam", "SLAM", 0, 1, .35f, ""},
                                                         {"fuzz", "FUZZ", 0, 1, 0, ""},
                                                         {"wide", "WIDE", 0, 1, 0, ""},
                                                         {"smear", "DISPERSER", 0, 1, 0, ""},
                                                         {"shift", "FREQ SHIFT", -400, 400, 0, "Hz"},
                                                         {"transient", "TRANSIENT", 0, 1, 0, ""},
                                                         {"tight", "TIGHT", 0, 1, 0, ""},
                                                         {"mix", "MIX", 0, 1, 1, ""},
                                                         {"output", "OUTPUT", -24, 6, -3, "dB"},
                                                         {"drive", "DRIVE RANGE", 1, 4, 2, "x"},
                                                         {"threshold", "THRESHOLD", -36, -6, -18, "dB"},
                                                         {"ratio", "RATIO", 1, 12, 4, ":1"},
                                                         {"attack", "ATTACK", 1, 50, 10, "ms"},
                                                         {"release", "RELEASE", 20, 400, 120, "ms"},
                                                         {"tone", "NOISE TONE", 100, 10000, 2200, "Hz"},
                                                         {"focus", "DISP FOCUS", 80, 4000, 700, "Hz"},
                                                         {"rate", "LFO RATE", .05f, 12, 1, "Hz"},
                                                         {"envtime", "ENV RELEASE", 10, 500, 100, "ms"},
                                                         {"motion", "MOTION", 0, 1, .5f, ""}}};
enum Parameter
{
    Slam,
    Fuzz,
    Wide,
    Smear,
    Shift,
    Transient,
    Tight,
    Mix,
    Output,
    Drive,
    Threshold,
    Ratio,
    Attack,
    Release,
    Tone,
    Focus,
    Rate,
    EnvTime,
    Motion,
    Count
};
struct Route
{
    int source = 0, destination = 0;
    float amount = 0;
};
struct Settings
{
    std::array<float, Count> value{};
    std::array<Route, 4> routes{};
    Settings()
    {
        for (int i = 0; i < Count; ++i)
            value[i] = definitions[i].initial;
    }
};
} // namespace heft
