// Vocal Aligner factory presets: only the values that differ from the defaults, in real units.
// The Monitor switch is never touched by a preset (see PresetManager::keepIds).
#include "PresetKit.h"

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Tight Double", { { "tight", 0.9f }, { "maxShift", 200.0f } } },
        { "Natural Double", { { "tight", 0.55f }, { "maxShift", 250.0f } } },
        { "Loose Feel", { { "tight", 0.3f }, { "maxShift", 150.0f } } },
        { "Locked Stack", { { "tight", 1.0f }, { "maxShift", 400.0f }, { "pitchOn", 1 }, { "pitchAmt", 1.0f } } },
        { "Stack + Gentle Pitch", { { "tight", 0.85f }, { "pitchOn", 1 }, { "pitchAmt", 0.5f } } },
        { "Pitch Match Only Lite", { { "tight", 0.2f }, { "maxShift", 60.0f }, { "pitchOn", 1 }, { "pitchAmt", 0.75f } } },
        { "Harmony Layer", { { "tight", 0.8f }, { "maxShift", 300.0f }, { "pitchOn", 0 }, { "gain", -4.0f } } },
        { "ADR / Dialogue", { { "tight", 0.7f }, { "maxShift", 800.0f } } },
        { "Rap Doubles", { { "tight", 0.95f }, { "maxShift", 120.0f } } },
        { "Backing Vocals Tuck", { { "tight", 0.8f }, { "pitchOn", 1 }, { "pitchAmt", 0.8f }, { "gain", -6.0f } } },
        { "Wide Search", { { "tight", 0.75f }, { "maxShift", 1000.0f } } },
        { "Manual Align", { { "autoAlign", 0 } } },
        { "Instrument Double", { { "tight", 0.9f }, { "maxShift", 100.0f }, { "pitchOn", 0 } } },
    };
    return p;
}
