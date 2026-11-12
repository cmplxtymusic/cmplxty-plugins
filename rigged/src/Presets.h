#pragma once
#include "Parameters.h"

namespace rigged
{
inline constexpr int presetCount = 25;
inline const char *presetNames[presetCount] = {
    "Wired Floor",    "Iron Gate",     "Cold Room",     "Pulse Train",  "Glass Hall",
    "Night Shift",    "Static Bed",    "Phase Drift",   "Sharp Edge",   "Slow Burn",
    "Tight Box",      "Mono Punch",    "Radio Age",     "Hall Of Wire", "Sub Merge",
    "Flicker",        "Satellite",     "Compound",      "Wide Awake",   "Ring Street",
    "Heavy Lid",      "Deep Fold",     "Clean Slate",   "Grid Current", "Full House"
};

inline void allOff(Settings &s)
{
    for (int i = 0; i < kFxCount; ++i)
    {
        s[enableOf(i)] = 0;
        s[lockOf(i)] = 0;
    }
    s[Mix] = 1;
    s[Output] = 0;
    s[GainMatch] = 1;
    s[LimiterOn] = 1;
    s[Bypass] = 0;
    s[Amount] = 0.75f;
    s[ReduceMotion] = 0;
    s[Randomize] = 0;
    for (int i = 0; i < kFxCount; ++i)
        s.order[(size_t)i] = i;
}

inline Settings makePreset(int index)
{
    Settings s;
    allOff(s);
    const int i = clampi(index, 0, presetCount - 1);
    auto on = [&](Fx f) { s[enableOf((int)f)] = 1; };
    switch (i)
    {
    case 0: // Wired Floor — screenshot default, every module on and visible
        for (int f = 0; f < kFxCount; ++f)
            s[enableOf(f)] = 1;
        s[FltMode] = 0;
        s[FltCut] = 4200;
        s[FltRes] = 0.85f;
        s[FltMix] = 1;
        s[DrvAmt] = 0.32f;
        s[DrvTone] = 0.55f;
        s[DrvMix] = 0.7f;
        s[CrshBits] = 10;
        s[CrshRate] = 2;
        s[CrshMix] = 0.28f;
        s[RingFreq] = 220;
        s[RingMix] = 0.18f;
        s[ChorRate] = 0.7f;
        s[ChorDepth] = 0.5f;
        s[ChorMix] = 0.3f;
        s[PhasRate] = 0.28f;
        s[PhasDepth] = 0.55f;
        s[PhasFb] = 0.3f;
        s[PhasMix] = 0.28f;
        s[DlaySync] = 1;
        s[DlayFb] = 0.32f;
        s[DlayMix] = 0.22f;
        s[RvrbSize] = 0.42f;
        s[RvrbDamp] = 0.45f;
        s[RvrbMix] = 0.2f;
        s[CompThres] = -16;
        s[CompRatio] = 3.2f;
        s[CompAtk] = 6;
        s[CompRel] = 90;
        s[TremSync] = 3;
        s[TremDepth] = 0.22f;
        s[TremShape] = 0;
        s[WdthAmt] = 1.25f;
        s[Amount] = 0.8f;
        break;
    case 1: // Iron Gate
        on(Fx::Filter);
        on(Fx::Drive);
        on(Fx::Comp);
        on(Fx::Width);
        s[FltMode] = 0;
        s[FltCut] = 2800;
        s[FltRes] = 1.4f;
        s[DrvAmt] = 0.55f;
        s[DrvTone] = 0.35f;
        s[CompThres] = -14;
        s[CompRatio] = 5;
        s[CompAtk] = 8;
        s[CompRel] = 60;
        s[WdthAmt] = 0.85f;
        break;
    case 2: // Cold Room
        on(Fx::Filter);
        on(Fx::Reverb);
        on(Fx::Width);
        s[FltMode] = 1;
        s[FltCut] = 180;
        s[FltRes] = 0.7f;
        s[RvrbSize] = 0.78f;
        s[RvrbDamp] = 0.62f;
        s[RvrbMix] = 0.45f;
        s[WdthAmt] = 1.4f;
        break;
    case 3: // Pulse Train
        on(Fx::Tremolo);
        on(Fx::Crush);
        on(Fx::Comp);
        s[TremSync] = 1;
        s[TremDepth] = 0.8f;
        s[TremShape] = 2;
        s[CrshBits] = 8;
        s[CrshRate] = 4;
        s[CrshMix] = 0.45f;
        s[CompThres] = -18;
        s[CompRatio] = 4;
        break;
    case 4: // Glass Hall
        on(Fx::Chorus);
        on(Fx::Reverb);
        on(Fx::Width);
        s[ChorRate] = 0.35f;
        s[ChorDepth] = 0.65f;
        s[ChorMix] = 0.5f;
        s[RvrbSize] = 0.7f;
        s[RvrbDamp] = 0.25f;
        s[RvrbMix] = 0.4f;
        s[WdthAmt] = 1.5f;
        break;
    case 5: // Night Shift
        on(Fx::Filter);
        on(Fx::Tremolo);
        on(Fx::Phaser);
        s[FltMode] = 2;
        s[FltCut] = 1100;
        s[FltRes] = 1.8f;
        s[TremSync] = 0;
        s[TremDepth] = 0.45f;
        s[PhasRate] = 0.18f;
        s[PhasDepth] = 0.7f;
        s[PhasFb] = 0.45f;
        s[PhasMix] = 0.55f;
        break;
    case 6: // Static Bed
        on(Fx::Crush);
        on(Fx::Width);
        on(Fx::Filter);
        s[CrshBits] = 6;
        s[CrshRate] = 6;
        s[CrshMix] = 0.55f;
        s[FltMode] = 0;
        s[FltCut] = 6500;
        s[WdthAmt] = 1.15f;
        break;
    case 7: // Phase Drift
        on(Fx::Phaser);
        on(Fx::Chorus);
        on(Fx::Delay);
        s[PhasRate] = 0.12f;
        s[PhasDepth] = 0.8f;
        s[PhasFb] = 0.5f;
        s[PhasMix] = 0.7f;
        s[ChorRate] = 0.22f;
        s[ChorDepth] = 0.4f;
        s[ChorMix] = 0.35f;
        s[DlaySync] = 3;
        s[DlayFb] = 0.28f;
        s[DlayMix] = 0.22f;
        break;
    case 8: // Sharp Edge
        on(Fx::Filter);
        on(Fx::Drive);
        on(Fx::Comp);
        s[FltMode] = 1;
        s[FltCut] = 350;
        s[FltRes] = 1.1f;
        s[DrvAmt] = 0.62f;
        s[DrvTone] = 0.8f;
        s[CompThres] = -12;
        s[CompRatio] = 6;
        s[CompAtk] = 2;
        s[CompRel] = 50;
        break;
    case 9: // Slow Burn
        on(Fx::Delay);
        on(Fx::Reverb);
        on(Fx::Filter);
        s[DlaySync] = 0;
        s[DlayFb] = 0.55f;
        s[DlayMix] = 0.4f;
        s[RvrbSize] = 0.82f;
        s[RvrbDamp] = 0.5f;
        s[RvrbMix] = 0.35f;
        s[FltMode] = 0;
        s[FltCut] = 5000;
        break;
    case 10: // Tight Box
        on(Fx::Reverb);
        on(Fx::Comp);
        on(Fx::Filter);
        s[RvrbSize] = 0.18f;
        s[RvrbDamp] = 0.7f;
        s[RvrbMix] = 0.25f;
        s[CompThres] = -10;
        s[CompRatio] = 8;
        s[CompAtk] = 1;
        s[CompRel] = 40;
        s[FltMode] = 0;
        s[FltCut] = 8000;
        break;
    case 11: // Mono Punch
        on(Fx::Width);
        on(Fx::Drive);
        on(Fx::Comp);
        s[WdthAmt] = 0.15f;
        s[DrvAmt] = 0.5f;
        s[DrvTone] = 0.4f;
        s[CompThres] = -16;
        s[CompRatio] = 5;
        s[CompAtk] = 4;
        s[CompRel] = 70;
        break;
    case 12: // Radio Age
        on(Fx::Crush);
        on(Fx::Filter);
        on(Fx::Tremolo);
        s[CrshBits] = 7;
        s[CrshRate] = 5;
        s[CrshMix] = 0.7f;
        s[FltMode] = 2;
        s[FltCut] = 1600;
        s[FltRes] = 0.9f;
        s[TremSync] = 2;
        s[TremDepth] = 0.35f;
        s[TremShape] = 1;
        break;
    case 13: // Hall Of Wire
        on(Fx::Reverb);
        on(Fx::Delay);
        on(Fx::Width);
        on(Fx::Chorus);
        s[RvrbSize] = 0.72f;
        s[RvrbDamp] = 0.35f;
        s[RvrbMix] = 0.38f;
        s[DlaySync] = 4;
        s[DlayFb] = 0.4f;
        s[DlayMix] = 0.28f;
        s[WdthAmt] = 1.45f;
        s[ChorRate] = 0.4f;
        s[ChorDepth] = 0.35f;
        s[ChorMix] = 0.25f;
        break;
    case 14: // Sub Merge
        on(Fx::Filter);
        on(Fx::Drive);
        on(Fx::Comp);
        s[FltMode] = 0;
        s[FltCut] = 420;
        s[FltRes] = 1.6f;
        s[DrvAmt] = 0.45f;
        s[DrvTone] = 0.2f;
        s[CompThres] = -20;
        s[CompRatio] = 4;
        break;
    case 15: // Flicker
        on(Fx::Tremolo);
        on(Fx::Phaser);
        on(Fx::Width);
        s[TremSync] = 3;
        s[TremDepth] = 0.7f;
        s[TremShape] = 0;
        s[PhasRate] = 0.55f;
        s[PhasDepth] = 0.5f;
        s[PhasMix] = 0.45f;
        s[WdthAmt] = 1.2f;
        break;
    case 16: // Satellite
        on(Fx::Ring);
        on(Fx::Chorus);
        on(Fx::Width);
        s[RingFreq] = 880;
        s[RingMix] = 0.35f;
        s[ChorRate] = 1.4f;
        s[ChorDepth] = 0.55f;
        s[ChorMix] = 0.4f;
        s[WdthAmt] = 1.6f;
        break;
    case 17: // Compound
        on(Fx::Filter);
        on(Fx::Drive);
        on(Fx::Delay);
        on(Fx::Comp);
        on(Fx::Width);
        s[FltMode] = 0;
        s[FltCut] = 3500;
        s[FltRes] = 1.0f;
        s[DrvAmt] = 0.38f;
        s[DlaySync] = 1;
        s[DlayFb] = 0.38f;
        s[DlayMix] = 0.25f;
        s[CompThres] = -18;
        s[CompRatio] = 3.5f;
        s[WdthAmt] = 1.1f;
        break;
    case 18: // Wide Awake
        on(Fx::Width);
        on(Fx::Chorus);
        on(Fx::Comp);
        s[WdthAmt] = 1.75f;
        s[ChorRate] = 0.9f;
        s[ChorDepth] = 0.4f;
        s[ChorMix] = 0.3f;
        s[CompThres] = -20;
        s[CompRatio] = 2.2f;
        break;
    case 19: // Ring Street
        on(Fx::Ring);
        on(Fx::Delay);
        on(Fx::Filter);
        s[RingFreq] = 110;
        s[RingMix] = 0.42f;
        s[DlaySync] = 1;
        s[DlayFb] = 0.48f;
        s[DlayMix] = 0.35f;
        s[FltMode] = 0;
        s[FltCut] = 7000;
        break;
    case 20: // Heavy Lid
        on(Fx::Drive);
        on(Fx::Comp);
        on(Fx::Filter);
        s[DrvAmt] = 0.72f;
        s[DrvTone] = 0.3f;
        s[CompThres] = -12;
        s[CompRatio] = 8;
        s[CompAtk] = 15;
        s[CompRel] = 180;
        s[FltMode] = 0;
        s[FltCut] = 5500;
        s[FltRes] = 0.8f;
        break;
    case 21: // Deep Fold
        on(Fx::Filter);
        on(Fx::Phaser);
        on(Fx::Reverb);
        on(Fx::Comp);
        s[FltMode] = 0;
        s[FltCut] = 2200;
        s[FltRes] = 1.2f;
        s[PhasRate] = 0.2f;
        s[PhasDepth] = 0.65f;
        s[PhasMix] = 0.4f;
        s[RvrbSize] = 0.55f;
        s[RvrbMix] = 0.28f;
        s[CompThres] = -18;
        s[CompRatio] = 3;
        break;
    case 22: // Clean Slate — almost dry, limiter and match on
        on(Fx::Width);
        on(Fx::Comp);
        s[WdthAmt] = 1;
        s[CompThres] = -24;
        s[CompRatio] = 1.5f;
        s[CompAtk] = 10;
        s[CompRel] = 120;
        s[Amount] = 0.5f;
        break;
    case 23: // Grid Current
        on(Fx::Tremolo);
        on(Fx::Delay);
        on(Fx::Crush);
        on(Fx::Width);
        s[TremSync] = 5;
        s[TremDepth] = 0.5f;
        s[TremShape] = 2;
        s[DlaySync] = 3;
        s[DlayFb] = 0.3f;
        s[DlayMix] = 0.2f;
        s[CrshBits] = 9;
        s[CrshRate] = 2;
        s[CrshMix] = 0.3f;
        s[WdthAmt] = 1.3f;
        break;
    case 24: // Full House — all eleven, musical and level-safe
        for (int f = 0; f < kFxCount; ++f)
            s[enableOf(f)] = 1;
        s[FltMode] = 0;
        s[FltCut] = 8000;
        s[FltRes] = 0.707f;
        s[DrvAmt] = 0.22f;
        s[DrvTone] = 0.5f;
        s[DrvMix] = 0.55f;
        s[CrshBits] = 12;
        s[CrshRate] = 1;
        s[CrshMix] = 0.15f;
        s[RingFreq] = 330;
        s[RingMix] = 0.1f;
        s[ChorRate] = 0.6f;
        s[ChorDepth] = 0.35f;
        s[ChorMix] = 0.22f;
        s[PhasRate] = 0.3f;
        s[PhasDepth] = 0.4f;
        s[PhasFb] = 0.25f;
        s[PhasMix] = 0.2f;
        s[DlaySync] = 3;
        s[DlayFb] = 0.25f;
        s[DlayMix] = 0.16f;
        s[RvrbSize] = 0.35f;
        s[RvrbDamp] = 0.5f;
        s[RvrbMix] = 0.16f;
        s[CompThres] = -18;
        s[CompRatio] = 2.8f;
        s[CompAtk] = 8;
        s[CompRel] = 100;
        s[TremSync] = 1;
        s[TremDepth] = 0.12f;
        s[WdthAmt] = 1.15f;
        break;
    default:
        break;
    }
    return s;
}

} // namespace rigged
