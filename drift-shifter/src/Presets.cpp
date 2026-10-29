// Drift factory presets: only the values that differ from the defaults, in real units.
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Subtle Widener", { { "coarse", 0.0f }, { "fine", 1.5f }, { "spread", 6.0f }, { "mix", 0.5f } } },
        { "Detuned Chorus", { { "fine", 4.0f }, { "spread", 12.0f }, { "mix", 0.5f }, { "feedback", 0.2f } } },
        { "Barber Pole Up", { { "fine", 0.6f }, { "feedback", 0.85f }, { "mix", 0.6f }, { "spread", 0.3f } } },
        { "Barber Pole Down", { { "fine", -0.6f }, { "feedback", 0.85f }, { "mix", 0.6f }, { "spread", -0.3f } } },
        { "Metallic Ring", { { "coarse", 420.0f }, { "mix", 0.8f }, { "feedback", 0.3f } } },
        { "Inharmonic Bells", { { "coarse", 1250.0f }, { "fine", 33.0f }, { "spread", 25.0f }, { "mix", 0.55f }, { "feedback", 0.45f } } },
        { "Robot Detune", { { "coarse", -90.0f }, { "spread", 15.0f }, { "mix", 1.0f } } },
        { "Keytracked Harmonics", { { "keyOn", 1 }, { "keyAmt", 20.0f }, { "coarse", 110.0f }, { "mix", 0.6f } } },
        { "Mid Band Shimmer", { { "lowBand", 0 }, { "highBand", 0 }, { "coarse", 180.0f }, { "spread", 20.0f }, { "xoLow", 300.0f }, { "xoHigh", 3000.0f }, { "mix", 0.7f } } },
        { "Air Shift", { { "lowBand", 0 }, { "coarse", 1800.0f }, { "xoLow", 2000.0f }, { "xoHigh", 6000.0f }, { "mix", 0.4f } } },
        { "Low End Safe", { { "lowBand", 0 }, { "coarse", 60.0f }, { "spread", 8.0f }, { "xoLow", 150.0f }, { "slope", 2 } } },
        { "Feedback Swirl", { { "fine", -2.5f }, { "spread", 4.0f }, { "feedback", 0.7f }, { "mix", 0.7f } } },
        { "Dark Downshift", { { "coarse", -350.0f }, { "highBand", 0 }, { "xoHigh", 2500.0f }, { "mix", 0.9f } } },
        { "Stereo Drift", { { "fine", 0.25f }, { "spread", 3.0f }, { "wide", 1 }, { "mix", 0.35f } } },
        { "Alien Radio", { { "coarse", 2600.0f }, { "fine", -40.0f }, { "spread", 60.0f }, { "feedback", 0.55f }, { "mix", 0.75f } } },
    };
    return p;
}
