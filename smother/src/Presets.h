#pragma once
#include "Parameters.h"
#include <juce_core/juce_core.h>

namespace smother
{
inline constexpr int presetCount = 22;
inline const char *presetNames[presetCount] = {
    "Velvet Grid",          "Night Shift",         "Iron Sleeve",        "Five Against Sixteen",
    "Soft Blanket",         "Dust Pocket",         "Glass Needles",      "Slow Pulse",
    "Rust Waltz",           "Core Pressure",       "Flare Drift",        "Moss Lane",
    "Spark Chamber",        "Gridlock",            "Hollow March",       "Wide Curtain",
    "Broken Stairs",        "Tight Room",          "Late Swing",         "Open Floor",
    "Under Cloth",          "Two-Speed"
};

inline void steps(Lane &ln, std::initializer_list<int> on, int every = 16)
{
    ln.length = clampi(every, 1, maxSteps);
    ln.vel.fill(0);
    for (int i : on)
        if (i >= 0 && i < maxSteps)
            ln.vel[(size_t)i] = 1.f;
}

inline void stepsVel(Lane &ln, std::initializer_list<float> v)
{
    ln.vel.fill(0);
    int i = 0;
    for (float x : v)
    {
        if (i >= maxSteps)
            break;
        ln.vel[(size_t)i++] = clampf(x, 0, 1);
    }
    if (ln.length < i)
        ln.length = i;
}

inline Sleeve slv(int start, int length, SleeveType t, float att = 20, float rel = 40, float depth = 1,
                  float curve = 0.35f)
{
    Sleeve s;
    s.start = start;
    s.length = length;
    s.type = t;
    s.attackMs = att;
    s.releaseMs = rel;
    s.depth = depth;
    s.curve = curve;
    return s;
}

inline Pattern makePreset(int index)
{
    Pattern p;
    const int kitFor[] = { 0, 1, 0, 8, 7, 3, 2, 4, 9, 11, 10, 7, 6, 8, 1, 5, 9, 2, 4, 10, 7, 8 };
    const int kit = kitFor[clampi(index, 0, presetCount - 1)];
    initDefaultLanes(p, kit);
    for (int i = 0; i < 8; ++i)
        p.lanes[(size_t)i].kit = kit;

    auto &k = p.lanes[0];
    auto &s = p.lanes[1];
    auto &c = p.lanes[2];
    auto &h = p.lanes[3];
    auto &o = p.lanes[4];
    auto &t = p.lanes[5];
    auto &r = p.lanes[6];
    auto &y = p.lanes[7];

    switch (clampi(index, 0, presetCount - 1))
    {
    case 0: // Velvet Grid — screenshot default, several visible sleeves
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(c, { 10 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps(o, { 6, 14 });
        steps(t, { 8 });
        steps(r, { 3, 11 });
        steps(y, { 0 });
        k.decay = 0.55f;
        s.decay = 0.45f;
        addSleeve(h, slv(4, 4, SleeveType::Mute, 8, 16, 1, 0));
        addSleeve(h, slv(12, 3, SleeveType::Lowpass, 15, 30, 1, 0.3f));
        addSleeve(s, slv(8, 4, SleeveType::Crush, 10, 25, 0.7f, 0.2f));
        addSleeve(k, slv(8, 4, SleeveType::Distort, 12, 30, 0.55f, 0.2f));
        addSleeve(o, slv(0, 8, SleeveType::Highpass, 25, 40, 0.6f, 0.4f));
        addAllSleeve(p, slv(12, 4, SleeveType::Reverb, 30, 80, 0.45f, 0.4f));
        break;
    case 1: // Night Shift
        steps(k, { 0, 7, 8, 14 });
        steps(s, { 4, 12, 15 });
        steps(h, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
        steps(o, { 4, 12 });
        steps(r, { 2, 6, 10, 14 });
        h.level = 0.55f;
        addSleeve(h, slv(0, 8, SleeveType::Lowpass, 40, 60, 0.85f, 0.5f));
        addSleeve(k, slv(4, 4, SleeveType::Mute, 6, 12, 1, 0));
        addAllSleeve(p, slv(8, 8, SleeveType::Flange, 50, 80, 0.4f, 0.5f));
        break;
    case 2: // Iron Sleeve
        steps(k, { 0, 3, 6, 8, 11 });
        steps(s, { 4, 12 });
        steps(c, { 4, 12 });
        steps(h, { 2, 6, 10, 14 });
        steps(y, { 8 });
        k.drive = 0.35f;
        s.crush = 0.2f;
        addSleeve(k, slv(0, 8, SleeveType::Distort, 8, 20, 0.7f, 0.1f));
        addSleeve(s, slv(0, 16, SleeveType::Crush, 5, 10, 0.5f, 0));
        addSleeve(y, slv(8, 8, SleeveType::Delay, 20, 80, 0.6f, 0.3f));
        break;
    case 3: // Five Against Sixteen — polymeter showcase
        steps(k, { 0, 4, 8, 12 });
        steps(h, { 0, 1, 2, 3, 4 }, 5);
        h.length = 5;
        h.rate = Rate::Sixteenth;
        steps(s, { 4, 12 });
        steps(r, { 0, 2, 3, 5, 7 }, 7);
        r.length = 7;
        r.rate = Rate::Sixteenth;
        steps(o, { 6 });
        addSleeve(h, slv(3, 2, SleeveType::Mute, 5, 10, 1, 0));
        addSleeve(k, slv(8, 4, SleeveType::Lowpass, 20, 30, 0.8f, 0.2f));
        break;
    case 4: // Soft Blanket
        steps(k, { 0, 8 });
        steps(s, { 8 });
        steps(h, { 0, 4, 8, 12 });
        steps(o, { 14 });
        steps(y, { 0 });
        k.decay = 0.7f;
        y.decay = 0.9f;
        addAllSleeve(p, slv(0, 16, SleeveType::Reverb, 80, 120, 0.55f, 0.6f));
        addSleeve(s, slv(4, 8, SleeveType::Lowpass, 40, 80, 0.7f, 0.5f));
        break;
    case 5: // Dust Pocket
        steps(k, { 0, 8, 10 });
        steps(s, { 4, 11, 12 });
        steps(h, { 0, 3, 6, 8, 11, 14 });
        steps(r, { 2, 7, 13 });
        k.crush = 0.25f;
        s.tone = 0.4f;
        addSleeve(k, slv(8, 8, SleeveType::Crush, 10, 20, 0.8f, 0.1f));
        addSleeve(h, slv(0, 8, SleeveType::Highpass, 15, 25, 0.5f, 0.2f));
        addAllSleeve(p, slv(12, 4, SleeveType::Mute, 8, 20, 0.7f, 0));
        break;
    case 6: // Glass Needles
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(h, { 0, 2, 3, 5, 6, 8, 10, 11, 13, 14 });
        steps(o, { 2, 10 });
        steps(r, { 7, 15 });
        h.tone = 0.95f;
        h.decay = 0.22f;
        o.hp = 800;
        addSleeve(h, slv(6, 4, SleeveType::Highpass, 5, 10, 0.85f, 0));
        addSleeve(o, slv(8, 8, SleeveType::Flange, 30, 50, 0.6f, 0.4f));
        break;
    case 7: // Slow Pulse
        steps(k, { 0, 8 });
        steps(s, { 8 });
        steps(t, { 4, 12 });
        steps(h, { 0, 8 });
        k.decay = 0.8f;
        t.tune = -5;
        k.rate = Rate::Eighth;
        k.length = 8;
        s.rate = Rate::Eighth;
        s.length = 8;
        addSleeve(k, slv(2, 2, SleeveType::Lowpass, 60, 80, 0.9f, 0.5f));
        addAllSleeve(p, slv(4, 4, SleeveType::Delay, 40, 90, 0.4f, 0.3f));
        break;
    case 8: // Rust Waltz — 1/8T feel
        k.rate = Rate::EighthT;
        k.length = 12;
        steps(k, { 0, 3, 6, 9 }, 12);
        s.rate = Rate::EighthT;
        s.length = 12;
        steps(s, { 3, 9 }, 12);
        h.rate = Rate::EighthT;
        h.length = 12;
        steps(h, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12);
        addSleeve(h, slv(4, 4, SleeveType::Mute, 8, 16, 1, 0));
        addSleeve(s, slv(6, 3, SleeveType::Distort, 12, 24, 0.55f, 0.2f));
        break;
    case 9: // Core Pressure
        steps(k, { 0, 4, 8, 12 });
        steps(s, { 4, 12 });
        steps(c, { 4, 7, 12 });
        steps(h, { 2, 6, 10, 14 });
        steps(t, { 0, 8 });
        k.ott = 0.45f;
        k.drive = 0.4f;
        s.transient = 0.5f;
        addSleeve(k, slv(0, 16, SleeveType::Distort, 15, 20, 0.45f, 0.2f));
        addAllSleeve(p, slv(0, 8, SleeveType::Lowpass, 40, 60, 0.5f, 0.3f));
        break;
    case 10: // Flare Drift
        steps(k, { 0, 8, 14 });
        steps(s, { 4, 12 });
        steps(o, { 2, 6, 10, 14 });
        steps(y, { 0, 8 });
        y.decay = 0.95f;
        o.decay = 0.8f;
        addSleeve(y, slv(4, 8, SleeveType::Flange, 80, 120, 0.7f, 0.6f));
        addSleeve(o, slv(0, 8, SleeveType::Reverb, 50, 100, 0.55f, 0.5f));
        addAllSleeve(p, slv(8, 8, SleeveType::Highpass, 30, 60, 0.4f, 0.3f));
        break;
    case 11: // Moss Lane
        steps(k, { 0, 9 });
        steps(s, { 6 });
        steps(h, { 0, 4, 8, 12 });
        steps(t, { 4, 12 });
        steps(r, { 2, 10 });
        k.tone = 0.3f;
        s.tone = 0.35f;
        t.tune = -3;
        addSleeve(k, slv(4, 8, SleeveType::Lowpass, 50, 80, 0.75f, 0.5f));
        addSleeve(r, slv(0, 16, SleeveType::Delay, 20, 60, 0.4f, 0.3f));
        break;
    case 12: // Spark Chamber
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(h, { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 });
        steps(r, { 3, 7, 11, 15 });
        r.decay = 0.25f;
        r.tone = 1;
        addSleeve(h, slv(2, 2, SleeveType::Mute, 3, 6, 1, 0));
        addSleeve(h, slv(10, 2, SleeveType::Mute, 3, 6, 1, 0));
        addSleeve(r, slv(0, 8, SleeveType::Crush, 5, 10, 0.6f, 0));
        break;
    case 13: // Gridlock
        steps(k, { 0, 4, 8, 12, 14 });
        steps(s, { 4, 12 });
        steps(c, { 8 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps(o, { 14 });
        addSleeve(k, slv(12, 4, SleeveType::Mute, 4, 8, 1, 0));
        addSleeve(h, slv(8, 8, SleeveType::Lowpass, 10, 20, 0.9f, 0.1f));
        addAllSleeve(p, slv(0, 4, SleeveType::Distort, 8, 16, 0.3f, 0.1f));
        break;
    case 14: // Hollow March
        steps(k, { 0, 6, 8, 14 });
        steps(s, { 4, 12 });
        steps(t, { 2, 10 });
        steps(y, { 0 });
        k.hp = 80;
        s.hp = 200;
        addSleeve(k, slv(0, 16, SleeveType::Highpass, 30, 40, 0.45f, 0.3f));
        addSleeve(y, slv(0, 16, SleeveType::Reverb, 100, 200, 0.7f, 0.6f));
        break;
    case 15: // Wide Curtain — ALL-row showcase
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps(o, { 6, 14 });
        steps(c, { 12 });
        addAllSleeve(p, slv(0, 4, SleeveType::Lowpass, 20, 40, 0.7f, 0.3f));
        addAllSleeve(p, slv(4, 4, SleeveType::Highpass, 20, 40, 0.55f, 0.3f));
        addAllSleeve(p, slv(8, 4, SleeveType::Crush, 15, 30, 0.5f, 0.2f));
        addAllSleeve(p, slv(12, 4, SleeveType::Reverb, 30, 80, 0.6f, 0.4f));
        break;
    case 16: // Broken Stairs
        steps(k, { 0, 5, 8, 13 });
        steps(s, { 3, 7, 11, 15 });
        steps(h, { 1, 2, 6, 9, 10, 14 });
        steps(r, { 4, 12 });
        addSleeve(s, slv(6, 5, SleeveType::Delay, 15, 40, 0.65f, 0.3f));
        addSleeve(k, slv(8, 5, SleeveType::Flange, 25, 50, 0.5f, 0.4f));
        break;
    case 17: // Tight Room
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(c, { 4, 12 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        k.decay = 0.3f;
        s.decay = 0.28f;
        s.transient = 0.6f;
        h.decay = 0.18f;
        addSleeve(s, slv(0, 16, SleeveType::Highpass, 10, 15, 0.35f, 0.1f));
        addAllSleeve(p, slv(0, 16, SleeveType::Lowpass, 20, 20, 0.25f, 0));
        break;
    case 18: // Late Swing — swing showcase (set swing from processor)
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps(o, { 6, 14 });
        steps(c, { 10 });
        addSleeve(h, slv(4, 4, SleeveType::Mute, 6, 12, 1, 0));
        addSleeve(o, slv(8, 8, SleeveType::Delay, 20, 50, 0.45f, 0.3f));
        break;
    case 19: // Open Floor
        steps(k, { 0, 8 });
        steps(s, { 4, 12 });
        steps(o, { 0, 4, 8, 12 });
        steps(y, { 0 });
        steps(r, { 6, 14 });
        o.decay = 0.85f;
        y.decay = 1;
        addSleeve(o, slv(0, 16, SleeveType::Reverb, 60, 140, 0.5f, 0.5f));
        addSleeve(y, slv(8, 8, SleeveType::Flange, 80, 160, 0.55f, 0.6f));
        break;
    case 20: // Under Cloth
        steps(k, { 0, 8, 12 });
        steps(s, { 4, 12 });
        steps(h, { 0, 4, 8, 12 });
        steps(t, { 8 });
        k.eqLow = 3;
        s.eqHigh = -2;
        addAllSleeve(p, slv(0, 16, SleeveType::Lowpass, 80, 80, 0.55f, 0.6f));
        addSleeve(h, slv(8, 8, SleeveType::Mute, 20, 40, 0.85f, 0.3f));
        break;
    case 21: // Two-Speed — DOUBLE length kick against 1/16 hats
        steps(k, { 0, 4 }, 8);
        k.length = 8;
        k.doubleLen = true; // each step is 1/8, loop is 4 bars of 1/16
        k.rate = Rate::Sixteenth;
        steps(s, { 4, 12 });
        steps(h, { 0, 2, 4, 6, 8, 10, 12, 14 });
        steps(o, { 14 });
        addSleeve(k, slv(2, 2, SleeveType::Distort, 20, 40, 0.5f, 0.2f));
        addSleeve(h, slv(0, 8, SleeveType::Highpass, 15, 25, 0.4f, 0.2f));
        addAllSleeve(p, slv(8, 8, SleeveType::Reverb, 40, 90, 0.35f, 0.4f));
        break;
    }
    p.numLanes = 8;
    return p;
}

inline juce::StringArray presetNameArray()
{
    juce::StringArray a;
    for (int i = 0; i < presetCount; ++i)
        a.add(presetNames[i]);
    return a;
}

} // namespace smother
