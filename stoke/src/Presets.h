#pragma once
#include "Parameters.h"

namespace stoke
{
inline constexpr int presetCount = 12;
inline constexpr const char *presetNames[presetCount] = {
    "Init",
    "Subtle",
    "Vocal Forward",
    "Synth Lead",
    "Bass Mids",
    "Snare Crack",
    "Master Glue",
    "Guitar Bite",
    "Podcast Voice",
    "Drum Bus",
    "Piano Body",
    "Air Presence",
};

// Values: focus, centre, width, glow, smooth, midonly, mix, output, gainmatch, limiter, bypass
inline constexpr float presetValues[presetCount][Count] = {
    { 0.50f, 1650.f, 1.60f, 0.28f, 0.55f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Init (defaults)
    { 0.28f, 1700.f, 1.80f, 0.16f, 0.50f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Subtle
    { 0.62f, 2200.f, 1.30f, 0.22f, 0.72f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Vocal Forward
    { 0.70f, 1850.f, 1.55f, 0.46f, 0.50f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Synth Lead
    { 0.55f,  700.f, 1.20f, 0.22f, 0.30f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Bass Mids
    { 0.68f, 2800.f, 1.00f, 0.32f, 0.58f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Snare Crack
    { 0.40f, 1500.f, 2.20f, 0.35f, 0.68f, 1.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Master Glue (mid only)
    { 0.65f, 2500.f, 1.10f, 0.42f, 0.62f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Guitar Bite
    { 0.58f, 2000.f, 1.50f, 0.18f, 0.78f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Podcast Voice
    { 0.50f, 2000.f, 2.00f, 0.30f, 0.55f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Drum Bus
    { 0.50f,  900.f, 1.40f, 0.20f, 0.45f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Piano Body
    { 0.55f, 4200.f, 1.20f, 0.26f, 0.70f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f }, // Air Presence
};

inline Settings makePreset(int index)
{
    Settings s;
    const int i = clampi(index, 0, presetCount - 1);
    for (int p = 0; p < Count; ++p)
        s[p] = presetValues[i][p];
    return s;
}

} // namespace stoke
