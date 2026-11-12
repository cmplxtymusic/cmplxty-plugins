#pragma once
#include "Parameters.h"

namespace fever
{
inline constexpr int presetCount = 12;
inline const char *presetNames[presetCount] = {
    "Still Point", "Low Cloud",  "Fifth Floor", "Octave Swarm", "Wide Bloom", "Pulse Cloud",
    "Threshold",   "Cold Teeth", "Held Rift",   "Split Halo",   "Night Pulse", "Full Rift"
};

inline Settings defaults()
{
    Settings s;
    s[FeverAmt] = 0.5f;
    s[SideA] = 0.5f;
    s[SideB] = 0.5f;
    s[Mix] = 1;
    s[Output] = 0;
    s[GainMatch] = 1;
    s[LimiterOn] = 1;
    s[Bypass] = 0;
    s[Heartbeat] = 0;
    s[HeartRate] = 0;
    s[HeartDepth] = 0.35f;
    s[ReduceMotion] = 0;
    return s;
}

inline Settings makePreset(int index)
{
    Settings s = defaults();
    const int i = clampi(index, 0, presetCount - 1);
    switch (i)
    {
    case 0: // Still Point — centre, screenshot default
        break;
    case 1: // Low Cloud — deep love, moderate density, tight spread
        s[FeverAmt] = 0.08f;
        s[SideA] = 0.45f;
        s[SideB] = 0.22f;
        break;
    case 2: // Fifth Floor — love, mid density, some spread
        s[FeverAmt] = 0.16f;
        s[SideA] = 0.55f;
        s[SideB] = 0.40f;
        break;
    case 3: // Octave Swarm — dense cloud, almost no detune
        s[FeverAmt] = 0.04f;
        s[SideA] = 0.92f;
        s[SideB] = 0.08f;
        break;
    case 4: // Wide Bloom — lush and wide
        s[FeverAmt] = 0.12f;
        s[SideA] = 0.70f;
        s[SideB] = 0.88f;
        break;
    case 5: // Pulse Cloud — love + heartbeat
        s[FeverAmt] = 0.18f;
        s[SideA] = 0.62f;
        s[SideB] = 0.50f;
        s[Heartbeat] = 1;
        s[HeartRate] = 0;
        s[HeartDepth] = 0.45f;
        break;
    case 6: // Threshold — just over the centre toward the rift
        s[FeverAmt] = 0.62f;
        s[SideA] = 0.25f;
        s[SideB] = 0.30f;
        break;
    case 7: // Cold Teeth — resonant peaks, short tail, little halo
        s[FeverAmt] = 0.88f;
        s[SideA] = 0.18f;
        s[SideB] = 0.15f;
        break;
    case 8: // Held Rift — deep freeze
        s[FeverAmt] = 0.90f;
        s[SideA] = 0.92f;
        s[SideB] = 0.22f;
        break;
    case 9: // Split Halo — strong detuned halo
        s[FeverAmt] = 0.86f;
        s[SideA] = 0.20f;
        s[SideB] = 0.95f;
        break;
    case 10: // Night Pulse — mid-rift with a heartbeat
        s[FeverAmt] = 0.78f;
        s[SideA] = 0.48f;
        s[SideB] = 0.55f;
        s[Heartbeat] = 1;
        s[HeartRate] = 1;
        s[HeartDepth] = 0.40f;
        break;
    case 11: // Full Rift — screenshot, all the way to hate
        s[FeverAmt] = 1.0f;
        s[SideA] = 0.55f;
        s[SideB] = 0.70f;
        break;
    default:
        break;
    }
    return s;
}

} // namespace fever
