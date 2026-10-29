// Glitch Crusher factory presets: only the values that differ from the defaults, in real units.
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "8-Bit Console", { { "bits", 6.0f }, { "downsample", 3.0f }, { "drive", 0.1f }, { "glitchOn", 0 } } },
        { "Lo-Fi Sampler", { { "bits", 12.0f }, { "downsample", 2.5f }, { "glitchOn", 0 }, { "filterOn", 1 }, { "cutoff", 7500.0f } } },
        { "Broken Radio", { { "bits", 5.0f }, { "downsample", 8.0f }, { "drive", 0.5f }, { "glitchProb", 0.15f }, { "filterOn", 1 }, { "filterType", 1 }, { "cutoff", 600.0f }, { "resonance", 2.0f } } },
        { "Stutter Rain", { { "crushOn", 0 }, { "glitchProb", 0.55f }, { "glitchRate", 3 } } },
        { "Reverse Chop", { { "crushOn", 0 }, { "glitchProb", 0.45f }, { "glitchRate", 1 }, { "reverse", 1 } } },
        { "Random Shards", { { "bits", 9.0f }, { "glitchProb", 0.6f }, { "glitchRate", 2 }, { "reverse", 2 } } },
        { "Crushed Drums", { { "bits", 7.0f }, { "downsample", 5.0f }, { "drive", 0.6f }, { "glitchProb", 0.2f }, { "mix", 0.7f } } },
        { "Telephone Glitch", { { "bits", 10.0f }, { "downsample", 4.0f }, { "filterOn", 1 }, { "filterType", 1 }, { "cutoff", 900.0f }, { "resonance", 1.5f }, { "glitchProb", 0.3f } } },
        { "Dark Grit", { { "bits", 8.0f }, { "downsample", 2.0f }, { "drive", 0.7f }, { "glitchOn", 0 }, { "filterOn", 1 }, { "cutoff", 1800.0f }, { "resonance", 1.2f }, { "outGain", -3.0f } } },
        { "Resonant Crunch", { { "bits", 6.0f }, { "downsample", 6.0f }, { "filterOn", 1 }, { "cutoff", 2600.0f }, { "resonance", 6.0f }, { "outGain", -4.0f } } },
        { "Subtle Texture", { { "bits", 13.0f }, { "downsample", 1.5f }, { "drive", 0.15f }, { "glitchProb", 0.08f }, { "mix", 0.4f } } },
        { "Digital Meltdown", { { "bits", 3.0f }, { "downsample", 24.0f }, { "drive", 0.9f }, { "glitchProb", 0.7f }, { "glitchRate", 3 }, { "reverse", 2 }, { "outGain", -6.0f } } },
        { "Half-Time Skips", { { "crushOn", 0 }, { "glitchProb", 0.35f }, { "glitchRate", 0 }, { "reverse", 2 } } },
        { "Bitcrush Bass", { { "bits", 4.0f }, { "downsample", 2.0f }, { "drive", 0.4f }, { "glitchOn", 0 }, { "filterOn", 1 }, { "cutoff", 1200.0f }, { "outGain", -2.0f } } },
        { "Parallel Smash", { { "bits", 4.0f }, { "downsample", 10.0f }, { "drive", 0.8f }, { "glitchProb", 0.25f }, { "mix", 0.35f } } },
    };
    return p;
}
