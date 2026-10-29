// Rewired factory presets: detection settings for typical material. Only values that differ from the defaults, in real units.
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Full Mix - Balanced", { { "onset", 0.5f }, { "frame", 0.3f }, { "minNote", 120.0f } } },
        { "Full Mix - Clean (fewer notes)", { { "onset", 0.62f }, { "frame", 0.38f }, { "minNote", 160.0f }, { "chordMin", 3.0f } } },
        { "Full Mix - Detailed (more notes)", { { "onset", 0.4f }, { "frame", 0.25f }, { "minNote", 80.0f }, { "drumSens", 0.65f } } },
        { "Vocal Song (melody from vocals)", { { "melSrc", 1 }, { "minNote", 100.0f } } },
        { "Instrumental (melody from top line)", { { "melSrc", 2 }, { "melGap", 5.0f } } },
        { "Hip-Hop / Trap", { { "drumSens", 0.6f }, { "minNote", 90.0f }, { "quantize", 1 } } },
        { "House / Techno (quantized)", { { "quantize", 1 }, { "drumSens", 0.55f }, { "chordMin", 3.0f } } },
        { "Lo-Fi / Soft Drums", { { "drumSens", 0.75f }, { "onset", 0.45f } } },
        { "Piano / Keys Stem", { { "frame", 0.25f }, { "minNote", 70.0f }, { "chordMin", 2.0f }, { "melGap", 5.0f } } },
        { "Pads / Long Chords", { { "frame", 0.22f }, { "minNote", 250.0f }, { "chordMin", 3.0f }, { "quantize", 3 } } },
        { "Bass Lines - Tight", { { "onset", 0.55f }, { "minNote", 90.0f }, { "quantize", 1 } } },
        { "Busy Drums - Strict", { { "drumSens", 0.35f } } },
        { "Sketch (1/8 grid)", { { "quantize", 2 }, { "minNote", 150.0f } } },
    };
    return p;
}
