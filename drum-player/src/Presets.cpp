// Drum Player factory kits: only the values that differ from the defaults (real units; choices by index).
// sounds 0 Kick808 1 KickPunch 2 Snare 3 Clap 4 ClosedHat 5 OpenHat 6 LowTom 7 MidTom 8 Rim 9 Cowbell 10 Crash 11 Shaker 12 Zap 13 SubBoom
// 14 Snap 15 Glitch 16 Sample. Defaults: pads 1-8 = 1 2 3 4 5 6 8 10, pads 4 + 5 in choke group 1, decay Full.
#include "PresetManager.h"

const std::vector<cw::FactoryPreset>& cw::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {  } },
        { "808 Classic", { { "p1Sound", 0 }, { "p7Sound", 9 }, { "p1Decay", 1200 }, { "p2Tune", -1 }, { "p6Tune", -3 }, { "p6Decay", 700 }, { "p7Pan", 0.25f }, { "p8Level", -6 }, { "p4Pan", -0.2f }, { "p5Pan", -0.2f } } },
        { "Techno Warehouse", { { "p2Sound", 3 }, { "p3Sound", 8 }, { "p6Sound", 13 }, { "p7Sound", 11 }, { "p8Sound", 15 }, { "p1Tune", -2 }, { "p1Level", 2 }, { "p2Decay", 350 }, { "p3Pan", 0.3f }, { "p4Decay", 60 }, { "p6Level", -4 }, { "p7Pan", -0.35f }, { "p8Level", -6 }, { "kitTune", -1 } } },
        { "Boom Bap Dusty", { { "p3Sound", 14 }, { "p6Sound", 7 }, { "p8Sound", 9 }, { "p1Tune", -4 }, { "p1Decay", 600 }, { "p2Tune", -3 }, { "p2Decay", 300 }, { "p3Level", -3 }, { "p4Tune", -5 }, { "p5Tune", -5 }, { "p6Tune", -2 }, { "velSens", 0.9f } } },
        { "Trap Sub Kit", { { "p1Sound", 13 }, { "p6Sound", 12 }, { "p7Sound", 14 }, { "p1Decay", 1800 }, { "p1Level", 1 }, { "p2Tune", 2 }, { "p3Tune", 1 }, { "p4Decay", 35 }, { "p6Tune", 5 }, { "p6Level", -6 }, { "p7Pan", 0.3f }, { "p8Level", -8 } } },
        { "Cyberpunk Glitch", { { "p2Sound", 15 }, { "p3Sound", 12 }, { "p6Sound", 14 }, { "p7Sound", 15 }, { "p8Sound", 13 }, { "p2Tune", -5 }, { "p3Tune", 7 }, { "p4Tune", 3 }, { "p5Tune", 3 }, { "p6Pan", -0.4f }, { "p7Tune", 7 }, { "p7Pan", 0.4f }, { "p8Tune", -12 }, { "p8Decay", 900 }, { "p8Level", -4 } } },
        { "Lo-Fi Bedroom", { { "p1Sound", 0 }, { "p8Sound", 11 }, { "p1Tune", -3 }, { "p1Decay", 500 }, { "p2Tune", -5 }, { "p2Decay", 220 }, { "p3Tune", -4 }, { "p4Tune", -7 }, { "p4Level", -4 }, { "p5Tune", -7 }, { "p5Decay", 300 }, { "p8Pan", 0.3f }, { "volume", -5 }, { "velSens", 0.85f } } },
        { "Synthwave Gated", { { "p7Sound", 7 }, { "p2Decay", 260 }, { "p2Level", 1 }, { "p3Decay", 260 }, { "p6Tune", -2 }, { "p6Pan", -0.3f }, { "p7Pan", 0.3f }, { "p8Decay", 1500 }, { "p8Level", -5 } } },
        { "Industrial Hammer", { { "p2Sound", 15 }, { "p4Sound", 9 }, { "p6Sound", 13 }, { "p1Tune", -6 }, { "p1Level", 2 }, { "p2Tune", -12 }, { "p2Decay", 400 }, { "p3Tune", -3 }, { "p4Tune", -7 }, { "p4Decay", 120 }, { "p6Tune", -5 }, { "p8Tune", -5 }, { "p8Level", -4 }, { "volume", -7 } } },
        { "Minimal Clicks", { { "p2Sound", 8 }, { "p3Sound", 14 }, { "p5Sound", 11 }, { "p6Sound", 8 }, { "p7Sound", 14 }, { "p8Sound", 12 }, { "p1Decay", 180 }, { "p2Tune", 5 }, { "p3Tune", 4 }, { "p3Pan", 0.4f }, { "p4Decay", 25 }, { "p6Tune", -7 }, { "p6Pan", -0.5f }, { "p7Tune", -6 }, { "p7Pan", -0.3f }, { "p8Tune", 10 }, { "p8Level", -6 } } },
        { "Electro Funk", { { "p1Sound", 0 }, { "p6Sound", 7 }, { "p7Sound", 9 }, { "p8Sound", 8 }, { "p1Decay", 800 }, { "p2Tune", 1 }, { "p3Tune", 2 }, { "p6Tune", 3 }, { "p6Pan", 0.3f }, { "p7Pan", -0.3f }, { "p8Tune", 2 } } },
        { "House Groove", { { "p2Sound", 3 }, { "p3Sound", 2 }, { "p6Sound", 11 }, { "p2Level", -1 }, { "p3Level", -5 }, { "p3Tune", 2 }, { "p4Pan", 0.15f }, { "p5Pan", 0.15f }, { "p5Decay", 280 }, { "p6Pan", -0.25f }, { "p7Pan", 0.2f }, { "p8Level", -7 } } },
        { "Drum & Bass Break", { { "p3Sound", 14 }, { "p6Sound", 7 }, { "p7Sound", 6 }, { "p1Tune", 2 }, { "p1Decay", 260 }, { "p2Tune", 3 }, { "p2Level", 1 }, { "p3Pan", 0.2f }, { "p4Tune", 2 }, { "p5Tune", 2 }, { "p6Tune", 4 }, { "p7Tune", 2 }, { "kitTune", 1 } } },
        { "Percussion Box", { { "p1Sound", 6 }, { "p2Sound", 7 }, { "p3Sound", 8 }, { "p4Sound", 9 }, { "p5Sound", 11 }, { "p6Sound", 14 }, { "p7Sound", 3 }, { "p8Sound", 12 }, { "p1Pan", -0.4f }, { "p2Pan", 0.4f }, { "p3Pan", 0.2f }, { "p4Pan", -0.2f }, { "p5Pan", 0.5f }, { "p6Pan", -0.5f }, { "p8Tune", -12 }, { "p8Level", -5 }, { "p4Choke", 0 }, { "p5Choke", 0 } } },
        { "Arcade 8-Bit", { { "p1Sound", 12 }, { "p2Sound", 15 }, { "p3Sound", 15 }, { "p5Sound", 15 }, { "p6Sound", 12 }, { "p7Sound", 12 }, { "p8Sound", 15 }, { "p1Tune", -12 }, { "p2Tune", -7 }, { "p3Tune", 5 }, { "p4Tune", 12 }, { "p5Tune", 12 }, { "p6Tune", -5 }, { "p7Tune", 7 }, { "p8Tune", -12 }, { "p8Decay", 600 } } },
        { "Cinematic Impacts", { { "p1Sound", 13 }, { "p2Sound", 10 }, { "p3Sound", 0 }, { "p4Sound", 5 }, { "p5Sound", 10 }, { "p7Sound", 13 }, { "p8Sound", 12 }, { "p1Decay", 3000 }, { "p1Level", 1 }, { "p2Tune", -12 }, { "p2Level", -2 }, { "p3Tune", -7 }, { "p3Decay", 2500 }, { "p4Tune", -12 }, { "p6Tune", -7 }, { "p6Decay", 1500 }, { "p7Tune", -12 }, { "p8Tune", -24 }, { "p8Decay", 1200 }, { "volume", -4 } } },
        { "Tight Studio Kit", { { "p3Sound", 14 }, { "p7Sound", 7 }, { "p1Decay", 350 }, { "p2Decay", 250 }, { "p3Level", -4 }, { "p4Decay", 45 }, { "p6Pan", -0.35f }, { "p7Pan", 0.35f }, { "p8Pan", 0.25f }, { "p8Decay", 1400 }, { "p8Level", -6 } } },
        { "Hyperpop Crunch", { { "p2Sound", 3 }, { "p3Sound", 14 }, { "p6Sound", 12 }, { "p7Sound", 15 }, { "p1Tune", 3 }, { "p1Level", 2 }, { "p2Tune", 4 }, { "p3Tune", 6 }, { "p4Tune", 5 }, { "p5Tune", 5 }, { "p6Tune", 12 }, { "p7Tune", 12 }, { "p8Tune", 4 }, { "kitTune", 1 } } },
    };
    return p;
}
