// Neon FX factory presets: only the values that differ from the defaults (real units; choices by index).
// drMode 0 Soft 1 Hard 2 Fold 3 Crush | flMode 0 LP 1 HP 2 BP 3 Formant | flLfoRate 0 4bars 1 2bars 2 1bar 3 1/2 4 1/4 5 1/8 6 1/16
// gtRate 0 1/8 1 1/16 2 1/32 3 1/8T 4 1/16T | gtPattern 0 Straight 1 Halves 2 Offbeat 3 Trance1 4 Trance2 5 Syncopate 6 Build 7 Stair
// stLen 0 1/2 1 1/4 2 1/8 3 1/16 4 1/32 5 1/4T 6 1/8T 7 1/16T. Stutter / tape stop stay off in every preset: they are played live.
#include "PresetManager.h"

const std::vector<cw::FactoryPreset>& cw::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Trance Gate 16ths", { { "gtOn", 1 }, { "gtPattern", 3 }, { "gtRate", 1 }, { "gtDepth", 1.0f }, { "gtSmooth", 3 } } },
        { "Offbeat Pump", { { "gtOn", 1 }, { "gtPattern", 2 }, { "gtRate", 0 }, { "gtDepth", 0.8f }, { "gtSmooth", 12 } } },
        { "Neon Sweep", { { "flOn", 1 }, { "flMode", 0 }, { "flCut", 900 }, { "flReso", 0.6f }, { "flLfoRate", 1 }, { "flLfoDepth", 0.6f }, { "drOn", 1 }, { "drDrive", 6 } } },
        { "Talkbox Vowels", { { "flOn", 1 }, { "flMode", 3 }, { "flVowel", 0.5f }, { "flReso", 0.5f }, { "flLfoRate", 3 }, { "flLfoDepth", 1.0f }, { "drOn", 1 }, { "drDrive", 9 } } },
        { "Robot Formant Gate", { { "flOn", 1 }, { "flMode", 3 }, { "flVowel", 0.2f }, { "flLfoRate", 5 }, { "flLfoDepth", 0.7f }, { "gtOn", 1 }, { "gtPattern", 0 }, { "gtRate", 1 } } },
        { "Bitcrush Arcade", { { "drOn", 1 }, { "drMode", 3 }, { "drDrive", 24 }, { "drTone", 9000 }, { "output", -2 } } },
        { "Hard Clip Smash", { { "drOn", 1 }, { "drMode", 1 }, { "drDrive", 20 }, { "drTone", 14000 }, { "output", -3 } } },
        { "Wavefold Acid", { { "drOn", 1 }, { "drMode", 2 }, { "drDrive", 14 }, { "flOn", 1 }, { "flMode", 0 }, { "flCut", 1400 }, { "flReso", 0.75f }, { "flLfoRate", 4 }, { "flLfoDepth", 0.45f }, { "output", -3 } } },
        { "Telephone Line", { { "flOn", 1 }, { "flMode", 2 }, { "flCut", 1500 }, { "flReso", 0.35f }, { "drOn", 1 }, { "drMode", 1 }, { "drDrive", 10 }, { "drTone", 5000 } } },
        { "Stutter Ready 1/16", { { "stLen", 3 }, { "stGate", 0.7f }, { "stDecay", 0.08f }, { "tsStop", 600 } } },
        { "Glitch Repeat 1/32", { { "stLen", 4 }, { "stGate", 0.5f }, { "stDecay", 0.02f }, { "drOn", 1 }, { "drMode", 3 }, { "drDrive", 12 } } },
        { "Tape Stop Slow", { { "tsStop", 2500 }, { "tsStart", 1200 }, { "flOn", 1 }, { "flCut", 9000 } } },
        { "Tape Stop Brake", { { "tsStop", 250 }, { "tsStart", 0 } } },
        { "DJ Filter Build", { { "djFilter", 0.0f }, { "gtOn", 1 }, { "gtPattern", 6 }, { "gtRate", 1 }, { "gtDepth", 0.6f }, { "intensity", 0.3f } } },
        { "Cyber Chop", { { "gtOn", 1 }, { "gtPattern", 5 }, { "gtRate", 4 }, { "gtSmooth", 1.5f }, { "drOn", 1 }, { "drMode", 0 }, { "drDrive", 15 }, { "flOn", 1 }, { "flMode", 1 }, { "flCut", 180 }, { "output", -2 } } },
        { "Neon Nightdrive", { { "drOn", 1 }, { "drDrive", 9 }, { "drTone", 8000 }, { "flOn", 1 }, { "flCut", 2500 }, { "flReso", 0.45f }, { "flLfoRate", 0 }, { "flLfoDepth", 0.35f }, { "gtOn", 1 }, { "gtPattern", 4 }, { "gtRate", 1 }, { "gtDepth", 0.5f }, { "gtSmooth", 8 }, { "mix", 0.85f } } },
        { "Lo-Fi Wobble", { { "drOn", 1 }, { "drMode", 3 }, { "drDrive", 8 }, { "drTone", 4500 }, { "flOn", 1 }, { "flCut", 3500 }, { "flLfoRate", 3 }, { "flLfoDepth", 0.25f }, { "mix", 0.8f } } },
    };
    return p;
}
