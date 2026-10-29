// Growl factory presets: only the values that differ from the defaults, in real units.
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Warm Tube Bass", { { "drive", 0.35f }, { "satType", 1 }, { "formantOn", 0 }, { "crossover", 90.0f } } },
        { "Growl Wobble", { { "drive", 0.6f }, { "vowel", 0.2f }, { "lfoDepth", 0.7f }, { "lfoRate", 3 }, { "focus", 0.7f } } },
        { "Yoi Talk", { { "drive", 0.55f }, { "satType", 2 }, { "vowel", 0.75f }, { "lfoDepth", 0.5f }, { "lfoRate", 2 }, { "lfoShape", 1 }, { "shift", 2.0f } } },
        { "Neuro Fold", { { "drive", 0.8f }, { "satType", 2 }, { "bias", 0.25f }, { "vowel", 0.5f }, { "focus", 0.8f }, { "postOn", 1 }, { "postCut", 7000.0f }, { "outGain", -4.0f } } },
        { "Hard Clip Reese", { { "drive", 0.7f }, { "satType", 3 }, { "formantAmount", 0.5f }, { "vowel", 0.35f }, { "lfoDepth", 0.25f }, { "lfoRate", 0 } } },
        { "Diode Grit", { { "drive", 0.65f }, { "satType", 4 }, { "bias", -0.2f }, { "formantOn", 0 }, { "postOn", 1 }, { "postCut", 5500.0f } } },
        { "Vowel Sweep 1/16", { { "drive", 0.5f }, { "lfoDepth", 1.0f }, { "lfoRate", 4 }, { "lfoShape", 2 }, { "focus", 0.65f } } },
        { "Square Chatter", { { "drive", 0.6f }, { "satType", 3 }, { "lfoDepth", 0.8f }, { "lfoRate", 4 }, { "lfoShape", 4 }, { "vowel", 0.5f } } },
        { "Subtle Harmonics", { { "drive", 0.2f }, { "satType", 1 }, { "formantAmount", 0.3f }, { "mix", 0.6f } } },
        { "Mid Honk", { { "drive", 0.5f }, { "vowel", 0.62f }, { "shift", 5.0f }, { "focus", 0.9f }, { "postOn", 1 }, { "postType", 1 }, { "postCut", 180.0f } } },
        { "Dark Gutter", { { "drive", 0.7f }, { "satType", 1 }, { "vowel", 0.05f }, { "shift", -6.0f }, { "postOn", 1 }, { "postCut", 2500.0f }, { "postRes", 1.4f } } },
        { "Screech Lead", { { "drive", 0.85f }, { "satType", 2 }, { "vowel", 0.95f }, { "shift", 7.0f }, { "focus", 0.85f }, { "subLevel", -48.0f }, { "outGain", -5.0f } } },
        { "Parallel Snarl", { { "drive", 0.9f }, { "satType", 4 }, { "vowel", 0.45f }, { "mix", 0.4f } } },
        { "Saw Down Yawn", { { "drive", 0.55f }, { "lfoDepth", 0.9f }, { "lfoRate", 1 }, { "lfoShape", 3 }, { "vowel", 0.3f } } },
        { "Clean Sub, Dirty Top", { { "crossover", 160.0f }, { "subLevel", 3.0f }, { "drive", 0.75f }, { "satType", 3 }, { "vowel", 0.4f } } },
        { "Lo-Fi Radio Growl", { { "drive", 0.6f }, { "satType", 4 }, { "oversampling", 0 }, { "postOn", 1 }, { "postType", 1 }, { "postCut", 400.0f }, { "postRes", 1.8f } } },
    };
    return p;
}
