// Dark Reverb factory presets: only the values that differ from the defaults, in real units.
// Choice parameters use their index.  Freeze is never touched by a preset (see PresetManager::keepIds).
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Dark Room", { { "size", 0.3f }, { "decay", 1.2f }, { "dark", 2500.0f }, { "preDelay", 8.0f }, { "mix", 0.25f } } },
        { "Black Hall", { { "size", 0.8f }, { "decay", 9.0f }, { "dark", 2200.0f }, { "diffusion", 0.85f }, { "mix", 0.4f } } },
        { "Abyss", { { "size", 1.0f }, { "decay", 30.0f }, { "dark", 1500.0f }, { "mod", 0.5f }, { "mix", 0.5f }, { "gain", -3.0f } } },
        { "Cathedral of Ash", { { "size", 0.95f }, { "decay", 16.0f }, { "dark", 4500.0f }, { "preDelay", 60.0f }, { "mix", 0.4f } } },
        { "Metal Tank", { { "size", 0.45f }, { "decay", 4.0f }, { "metal", 0.85f }, { "dark", 6000.0f }, { "mix", 0.35f } } },
        { "Rusted Plate", { { "size", 0.35f }, { "decay", 2.5f }, { "metal", 0.6f }, { "diffusion", 0.9f }, { "dark", 7000.0f }, { "lowCut", 200.0f } } },
        { "Dark Shimmer", { { "shimOn", 1 }, { "shimPitch", 0 }, { "shimAmt", 0.4f }, { "decay", 10.0f }, { "size", 0.85f }, { "mix", 0.45f } } },
        { "Fifth Heaven", { { "shimOn", 1 }, { "shimPitch", 1 }, { "shimAmt", 0.35f }, { "decay", 8.0f }, { "dark", 6000.0f }, { "mix", 0.4f } } },
        { "Underworld", { { "shimOn", 1 }, { "shimPitch", 4 }, { "shimAmt", 0.45f }, { "decay", 12.0f }, { "dark", 1800.0f }, { "mix", 0.45f } } },
        { "Long Pre-Delay Vocal", { { "preDelay", 140.0f }, { "decay", 3.5f }, { "lowCut", 250.0f }, { "dark", 5000.0f }, { "mix", 0.25f } } },
        { "Drum Smash Room", { { "size", 0.25f }, { "decay", 0.8f }, { "diffusion", 0.95f }, { "dark", 5000.0f }, { "lowCut", 150.0f }, { "mix", 0.3f } } },
        { "Mono Cave", { { "width", 0.0f }, { "size", 0.55f }, { "decay", 5.0f }, { "dark", 2000.0f } } },
        { "Wobbly Tape Verb", { { "mod", 1.0f }, { "decay", 4.0f }, { "dark", 3000.0f }, { "mix", 0.35f } } },
        { "Haunted Pad", { { "size", 0.9f }, { "decay", 20.0f }, { "mod", 0.7f }, { "shimOn", 1 }, { "shimPitch", 3 }, { "shimAmt", 0.25f }, { "mix", 0.6f }, { "gain", -3.0f } } },
        { "Thin Ambience", { { "size", 0.15f }, { "decay", 0.5f }, { "lowCut", 400.0f }, { "mix", 0.2f } } },
    };
    return p;
}
