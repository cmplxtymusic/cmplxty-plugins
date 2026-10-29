// Robot Voice factory presets: only the values that differ from the defaults, in real units.
// Choice parameters use their index.  The Key is never touched by a preset (see PresetManager::keepIds).
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Classic Robot", { { "scale", 0 }, { "speed", 0.0f }, { "carrier", 0 }, { "bands", 1 }, { "vocMix", 0.8f } } },
        { "Hard Tune Major", { { "scale", 1 }, { "speed", 0.0f }, { "vocOn", 0 } } },
        { "Soft Tune Minor", { { "scale", 2 }, { "speed", 60.0f }, { "vocOn", 0 } } },
        { "Daft Vocoder", { { "scale", 1 }, { "carrier", 1 }, { "bands", 2 }, { "vocMix", 1.0f }, { "gain", -10.0f } } },
        { "Buzz Bot", { { "carrier", 2 }, { "bands", 1 }, { "vocMix", 0.9f }, { "gain", -2.0f } } },
        { "Whisper Machine", { { "carrier", 3 }, { "bands", 2 }, { "vocMix", 0.85f } } },
        { "Dalek", { { "vocOn", 0 }, { "snapOn", 0 }, { "ringOn", 1 }, { "ringHz", 30.0f }, { "ringMix", 1.0f } } },
        { "Metallic Ring", { { "ringOn", 1 }, { "ringHz", 420.0f }, { "ringMix", 0.6f }, { "vocMix", 0.5f } } },
        { "Sub Octave Droid", { { "octave", 1 }, { "carrier", 0 }, { "vocMix", 0.75f } } },
        { "Deep Two Octaves", { { "octave", 0 }, { "carrier", 2 }, { "bands", 2 }, { "vocMix", 0.8f }, { "gain", -3.0f } } },
        { "Chipmunk Bot", { { "octave", 3 }, { "carrier", 1 }, { "bands", 0 } } },
        { "Lo-Fi 8 Band", { { "bands", 0 }, { "carrier", 1 }, { "vocMix", 1.0f }, { "ringOn", 1 }, { "ringHz", 1200.0f }, { "ringMix", 0.2f } } },
        { "Pentatonic Pop", { { "scale", 3 }, { "speed", 15.0f }, { "vocMix", 0.35f } } },
        { "Blues Machine", { { "scale", 4 }, { "speed", 25.0f }, { "carrier", 2 }, { "vocMix", 0.5f } } },
        { "Subtle Texture", { { "mix", 0.35f }, { "vocMix", 0.5f }, { "speed", 40.0f } } },
    };
    return p;
}
