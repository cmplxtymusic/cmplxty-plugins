#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace smother
{
inline constexpr int maxSteps = 32, maxLanes = 16, maxSleeves = 16, maxVoices = 8, kitCount = 12;
inline constexpr int drumCount = 8, rateCount = 6, sleeveTypeCount = 8, envCount = 4;
inline constexpr const char *kitNames[] = { "IRON", "VOID",  "GLASS", "DUST",  "PULSE", "SLATE",
                                            "SPARK", "MOSS",  "GRID",  "RUST",  "FLARE", "CORE" };
inline constexpr const char *drumNames[] = { "KICK", "SNARE", "CLAP", "CHAT", "OHAT", "TOM", "PERC", "CYM" };
inline constexpr const char *rateNames[] = { "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T" };
inline constexpr const char *sleeveNames[] = { "MUTE", "LOWPASS", "HIGHPASS", "CRUSH",
                                               "DISTORT", "FLANGE", "DELAY", "REVERB" };
inline constexpr const char *envNames[] = { "SNAP", "LINEAR", "EXPO", "PUNCH" };
// Quarter-note units. DOUBLE multiplies these by two.
inline constexpr double rateBeats[] = { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
// Default GM-style note for each drum type.
inline constexpr int gmNote[] = { 36, 38, 39, 42, 46, 45, 37, 49 };

enum Parameter
{
    Swing,
    Human,
    SleeveHuman,
    MasterHP,
    MasterLP,
    MasterOTT,
    Output,
    Seq,
    Kit,
    Count
};
enum class Rate : int
{
    Eighth = 0,
    Sixteenth,
    ThirtySecond,
    EighthT,
    SixteenthT,
    ThirtySecondT
};
enum class SleeveType : int
{
    Mute = 0,
    Lowpass,
    Highpass,
    Crush,
    Distort,
    Flange,
    Delay,
    Reverb
};
enum class DrumType : int
{
    Kick = 0,
    Snare,
    Clap,
    ClosedHat,
    OpenHat,
    Tom,
    Perc,
    Cymbal
};
enum class EnvShape : int
{
    Snap = 0,
    Linear,
    Expo,
    Punch
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
    { "swing", "SWING", 0, 1, 0, "", false, 0 },
    { "human", "HUMAN", 0, 1, 0, "", false, 0 },
    { "sleevehuman", "SLV HUM", 0, 1, 0, "", false, 0 },
    { "masterhp", "M.HP", 20, 2000, 20, "Hz", false, 0 },
    { "masterlp", "M.LP", 200, 20000, 20000, "Hz", false, 0 },
    { "masterott", "M.OTT", 0, 1, 0.15f, "", false, 0 },
    { "output", "OUTPUT", -24, 6, -3, "dB", false, 0 },
    { "seq", "SEQ", 0, 1, 1, "", true, 2 },
    { "kit", "KIT", 0, 11, 0, "", true, 12 },
}};

struct Sleeve
{
    int start = 0, length = 4;
    SleeveType type = SleeveType::Mute;
    float attackMs = 20, releaseMs = 40, depth = 1, curve = 0.35f;
};

struct Lane
{
    bool enabled = true, mute = false, solo = false, doubleLen = false;
    int length = 16;
    Rate rate = Rate::Sixteenth;
    DrumType drum = DrumType::Kick;
    int midiNote = 36;
    int kit = 0;
    float tune = 0, decay = 0.55f, tone = 0.65f, pan = 0, level = 0.85f;
    EnvShape env = EnvShape::Expo;
    float eqLow = 0, eqMid = 0, eqHigh = 0;
    float hp = 20, lp = 20000, drive = 0, crush = 0, transient = 0, ott = 0;
    float chopStart = 0, chopEnd = 1;
    std::array<float, maxSteps> vel{}; // 0 off, else 0..1
    std::array<Sleeve, maxSleeves> sleeves{};
    int sleeveCount = 0;
    bool hasUser = false;
    std::vector<float> user; // mono
    double userSr = 48000;
    std::string userName;
};

struct Pattern
{
    int numLanes = 8;
    std::array<Lane, maxLanes> lanes{};
    std::array<Sleeve, maxSleeves> allSleeves{};
    int allCount = 0;
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

struct Hit
{
    int lane = 0, step = 0, sample = 0;
    double ppq = 0;
};

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline double wrap(double x, double m)
{
    if (!(m > 1.0e-12) || !std::isfinite(x))
        return 0;
    x = std::fmod(x, m);
    if (x < 0)
        x += m;
    return x;
}

inline double laneStepBeats(const Lane &ln)
{
    const int r = clampi((int)ln.rate, 0, rateCount - 1);
    return rateBeats[r] * (ln.doubleLen ? 2.0 : 1.0);
}

inline double laneLoopBeats(const Lane &ln)
{
    return laneStepBeats(ln) * std::max(1, clampi(ln.length, 1, maxSteps));
}

// Deterministic 0..1 hash.
inline float hash01(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (x >> 8) * (1.f / 16777215.f);
}

inline double humanOffsetBeats(int lane, int step, double stepBeats, float human)
{
    if (human <= 0)
        return 0;
    const float h = hash01((uint32_t)(0xA11E0000u ^ (lane * 97 + step * 13)));
    return ((double)h - 0.5) * 0.12 * stepBeats * (double)human;
}

// Swing delays odd (0-based) steps by up to half a step. swing=1 -> 50% of a step late.
inline double stepOnset(const Lane &ln, int laneIndex, int step, float swing, float human)
{
    const int length = clampi(ln.length, 1, maxSteps);
    const int s = ((step % length) + length) % length;
    const double sb = laneStepBeats(ln);
    double t = (double)s * sb;
    if (s & 1)
        t += (double)clampf(swing, 0, 1) * 0.5 * sb;
    t += humanOffsetBeats(laneIndex, s, sb, human);
    return t;
}

inline float shapeCurve(float x, float curve)
{
    x = clampf(x, 0, 1);
    curve = clampf(curve, 0, 1);
    if (curve < 0.001f)
        return x;
    const float k = 0.5f + curve * 6.5f;
    return (std::exp(k * x) - 1.f) / (std::exp(k) - 1.f);
}

// Sleeve envelope at musical time `pos` within one loop of `loopBeats`. Release extends past the
// sleeve end. Attack and release are in milliseconds, converted with bpm. Depth is applied by the
// caller. Returns 0..1.
inline float sleeveEnvelope(const Sleeve &sv, double pos, double stepBeats, double loopBeats, double bpm,
                            float sleeveHuman, int seed)
{
    if (sv.length < 1 || !(stepBeats > 0) || !(loopBeats > 0) || !(bpm > 1))
        return 0;
    const double beatSec = 60.0 / bpm;
    const double hum = (double)hash01((uint32_t)(0x51EE0000u ^ (uint32_t)seed)) - 0.5;
    const double nudge = hum * (double)clampf(sleeveHuman, 0, 1) * 0.04 * loopBeats;
    double start = (double)sv.start * stepBeats + nudge;
    double end = (double)(sv.start + sv.length) * stepBeats + nudge;
    if (end <= start)
        end = start + stepBeats;
    double att = std::max(0.0005, (double)sv.attackMs / 1000.0) / beatSec;
    double rel = std::max(0.0005, (double)sv.releaseMs / 1000.0) / beatSec;
    // If the sleeve is shorter than attack+something, still honour the times; they may overlap.
    auto envAt = [&](double p) -> float {
        if (p < start)
            return 0.f;
        float a = 1.f;
        if (att > 1.0e-12 && p < start + att)
            a = shapeCurve(float((p - start) / att), sv.curve);
        else if (p >= start)
            a = 1.f;
        if (p < end)
            return a;
        if (p >= end + rel)
            return 0.f;
        const float r = 1.f - shapeCurve(float((p - end) / rel), sv.curve);
        // During a potential overlap of a long attack and the release, take the min.
        return std::min(a, r);
    };
    const double p = wrap(pos, loopBeats);
    // Release (and a late attack) may wrap around the loop boundary.
    return std::max(envAt(p), std::max(envAt(p + loopBeats), envAt(p - loopBeats)));
}

inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
inline float gainToDb(float g) { return g > 1.0e-12f ? 20.f * std::log10(g) : -240.f; }

inline int defaultMidi(DrumType d, int ordinal)
{
    const int base = gmNote[clampi((int)d, 0, drumCount - 1)];
    // Extra lanes of the same type get nearby GM notes where they exist, else base+ordinal.
    static constexpr int kickAlts[] = { 36, 35, 34 };
    static constexpr int snareAlts[] = { 38, 40, 37 };
    static constexpr int tomAlts[] = { 45, 47, 43, 41, 50, 48 };
    static constexpr int cymAlts[] = { 49, 51, 57, 55, 52, 59 };
    static constexpr int percAlts[] = { 37, 56, 54, 75, 70, 69 };
    const int *alts = nullptr;
    int n = 0;
    switch (d)
    {
    case DrumType::Kick:
        alts = kickAlts;
        n = 3;
        break;
    case DrumType::Snare:
        alts = snareAlts;
        n = 3;
        break;
    case DrumType::Tom:
        alts = tomAlts;
        n = 6;
        break;
    case DrumType::Cymbal:
        alts = cymAlts;
        n = 6;
        break;
    case DrumType::Perc:
        alts = percAlts;
        n = 6;
        break;
    default:
        return clampi(base + ordinal, 0, 127);
    }
    return alts[clampi(ordinal, 0, n - 1)];
}

inline const char *drumLabel(DrumType d) { return drumNames[clampi((int)d, 0, drumCount - 1)]; }

inline void initDefaultLanes(Pattern &p, int kit = 0)
{
    p.numLanes = 8;
    p.allCount = 0;
    static constexpr DrumType types[8] = { DrumType::Kick,    DrumType::Snare,     DrumType::Clap,
                                           DrumType::ClosedHat, DrumType::OpenHat, DrumType::Tom,
                                           DrumType::Perc,    DrumType::Cymbal };
    for (int i = 0; i < maxLanes; ++i)
    {
        Lane ln;
        ln.drum = types[i % 8];
        ln.midiNote = defaultMidi(ln.drum, i / 8);
        ln.kit = clampi(kit, 0, kitCount - 1);
        ln.length = 16;
        ln.rate = Rate::Sixteenth;
        ln.level = (ln.drum == DrumType::ClosedHat || ln.drum == DrumType::Perc) ? 0.7f : 0.85f;
        if (ln.drum == DrumType::Kick)
            ln.decay = 0.5f, ln.tone = 0.45f;
        if (ln.drum == DrumType::ClosedHat)
            ln.decay = 0.28f, ln.tone = 0.85f;
        if (ln.drum == DrumType::OpenHat)
            ln.decay = 0.7f, ln.tone = 0.8f;
        if (ln.drum == DrumType::Cymbal)
            ln.decay = 0.85f, ln.tone = 0.9f;
        p.lanes[(size_t)i] = std::move(ln);
    }
}

inline void addSleeve(Lane &ln, const Sleeve &s)
{
    if (ln.sleeveCount >= maxSleeves)
        return;
    Sleeve t = s;
    t.start = clampi(t.start, 0, maxSteps - 1);
    t.length = clampi(t.length, 1, maxSteps - t.start);
    ln.sleeves[(size_t)ln.sleeveCount++] = t;
}

inline void addAllSleeve(Pattern &p, const Sleeve &s)
{
    if (p.allCount >= maxSleeves)
        return;
    Sleeve t = s;
    t.start = clampi(t.start, 0, maxSteps - 1);
    t.length = clampi(t.length, 1, maxSteps - t.start);
    p.allSleeves[(size_t)p.allCount++] = t;
}

} // namespace smother
