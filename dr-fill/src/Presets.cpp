// Dr Fill factory presets: fill recipes. Only values that differ from the defaults, in real units (choices by index).
// length: 0 1 beat, 1 2 beats, 2 1 bar, 3 2 bars   style: 0 Auto, 1 Snare Roll, 2 Build-Up, 3 Tom Run, 4 Triplets, 5 Flams, 6 Break Chop, 7 Stutter
// ending: 0 off, 1 open hat, 2 crash. Presets never touch the tempo, hit sensitivity or bypass.
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Tight Snare Roll", { { "style", 1 }, { "length", 1 }, { "density", 0.6f }, { "complex", 0.25f }, { "ramp", 0.5f }, { "ending", 0 } } },
        { "32nd Snare Rush", { { "style", 1 }, { "length", 2 }, { "density", 0.95f }, { "complex", 0.6f }, { "ramp", 0.85f }, { "humanize", 0.05f } } },
        { "Trap Roll Build", { { "style", 2 }, { "length", 2 }, { "density", 0.85f }, { "complex", 0.7f }, { "ramp", 0.9f }, { "pitch", 5.0f }, { "kick", 0.3f } } },
        { "Tom Run Down", { { "style", 3 }, { "length", 2 }, { "density", 0.6f }, { "pitch", -7.0f }, { "ramp", 0.5f } } },
        { "Tom Run Up", { { "style", 3 }, { "length", 2 }, { "density", 0.6f }, { "pitch", 7.0f }, { "ramp", 0.6f } } },
        { "Triplet Turnaround", { { "style", 4 }, { "length", 1 }, { "density", 0.7f }, { "complex", 0.45f }, { "ending", 1 } } },
        { "Flam Accents", { { "style", 5 }, { "length", 1 }, { "flam", 0.8f }, { "density", 0.45f }, { "humanize", 0.2f } } },
        { "Break Chop", { { "style", 6 }, { "length", 2 }, { "complex", 0.75f }, { "kick", 0.7f }, { "hats", 0.5f }, { "swing", 0.15f } } },
        { "Stutter Edit", { { "style", 7 }, { "length", 1 }, { "density", 0.8f }, { "complex", 0.5f }, { "ending", 0 } } },
        { "One Beat Pickup", { { "style", 1 }, { "length", 0 }, { "density", 0.55f }, { "ramp", 0.7f }, { "ending", 0 } } },
        { "Big 2-Bar Build", { { "style", 2 }, { "length", 3 }, { "density", 0.9f }, { "complex", 0.5f }, { "ramp", 1.0f }, { "pitch", 4.0f }, { "kick", 0.6f } } },
        { "House Crash Lift", { { "style", 2 }, { "length", 2 }, { "density", 0.5f }, { "complex", 0.2f }, { "kick", 0.8f }, { "hats", 0.6f }, { "ending", 2 } } },
        { "Lo-Fi Lazy Fill", { { "style", 6 }, { "length", 1 }, { "density", 0.4f }, { "swing", 0.55f }, { "humanize", 0.45f }, { "flam", 0.4f }, { "level", -2.0f }, { "ending", 0 } } },
        { "DnB Rush", { { "style", 1 }, { "length", 2 }, { "density", 0.9f }, { "complex", 0.8f }, { "ramp", 0.7f }, { "kick", 0.6f }, { "hats", 0.5f } } },
        { "Hat Shuffle Ending", { { "style", 4 }, { "length", 1 }, { "hats", 0.9f }, { "swing", 0.4f }, { "kick", 0.2f }, { "ending", 1 } } },
    };
    return p;
}
