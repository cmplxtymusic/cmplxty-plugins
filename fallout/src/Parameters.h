#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace fallout
{
inline constexpr int maxSteps = 32, gridRows = 25, baseNote = 36; // C1..C3 visible at octave 0
inline constexpr const char *oscNames[] = {"EDGE", "SLAB", "DUTY", "HORDE", "FOLD", "SNARL"};
inline constexpr const char *rateNames[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2"};
inline constexpr const char *midiNames[] = {"Off", "Live", "Transpose"};
inline constexpr double rateBeats[] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0};
enum Parameter
{
    Osc,
    Cutoff,
    Resonance,
    EnvMod,
    Decay,
    Accent,
    Tune,
    Drive,
    Glide,
    Pwm,
    Detune,
    Fold,
    Length,
    Rate,
    Seq,
    MidiMode,
    DelayDiv,
    DelayFb,
    DelayMix,
    DelayTone,
    Output,
    Human,
    Count
};
struct Definition
{
    const char *id, *name;
    float low, high, initial;
    const char *unit;
    bool choice;
    int choices;
};
inline constexpr std::array<Definition, Count> definitions{{
    {"osc", "OSC", 0, 5, 0, "", true, 6},
    {"cutoff", "CUTOFF", 20, 18000, 720, "Hz", false, 0},
    {"res", "RES", 0, 1, .45f, "", false, 0},
    {"envmod", "ENV", 0, 1, .62f, "", false, 0},
    {"decay", "DECAY", 20, 2000, 240, "ms", false, 0},
    {"accent", "ACCENT", 0, 1, .7f, "", false, 0},
    {"tune", "TUNE", -24, 24, 0, "st", false, 0},
    {"drive", "DRIVE", 0, 1, .28f, "", false, 0},
    {"glide", "GLIDE", 0, 1, .22f, "", false, 0},
    {"pwm", "DUTY", .05f, .95f, .5f, "", false, 0},
    {"detune", "HORDE", 0, 1, .35f, "", false, 0},
    {"fold", "FOLD", 0, 1, .4f, "", false, 0},
    {"length", "LENGTH", 1, 32, 16, "", false, 0},
    {"rate", "RATE", 0, 7, 2, "", true, 8},
    {"seq", "SEQ", 0, 1, 1, "", true, 2},
    {"midimode", "MIDI", 0, 2, 1, "", true, 3},
    {"delaydiv", "TIME", 0, 7, 4, "", true, 8},
    {"delayfb", "FEEDBACK", 0, .95f, .32f, "", false, 0},
    {"delaymix", "DLY MIX", 0, 1, .18f, "", false, 0},
    {"delaytone", "DLY TONE", 200, 16000, 5500, "Hz", false, 0},
    {"output", "OUTPUT", -24, 6, -3, "dB", false, 0},
    {"human", "RUBATO", 0, 1, 0, "", false, 0},
}};
struct Step
{
    int note = -1; // -1 rest, else MIDI note
    bool accent = false, slide = false;
    float micro = 0, rubato = 0; // cents ±100; step-fraction ±0.45
};
struct Pattern
{
    int length = 16;
    std::array<Step, maxSteps> steps{};
};
struct Settings
{
    std::array<float, Count> value{};
    Settings()
    {
        for (int i = 0; i < Count; ++i)
            value[i] = definitions[i].initial;
    }
};
inline int clampNote(int n) { return n < 0 ? -1 : (n > 127 ? 127 : n); }
inline float midiToHz(float n)
{
    return 440.f * std::pow(2.f, (n - 69.f) / 12.f);
}
} // namespace fallout
