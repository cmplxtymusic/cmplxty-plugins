// Mix Tools factory presets: only the values that differ from the defaults (real units; choices by index).
// order: 0 Widen > Shape > Limit, 1 Shape > Widen > Limit.  speed: 0 Fast, 1 Medium, 2 Slow.  width 1.0 = 100 % (unchanged).
#include "PresetManager.h"

const std::vector<mt::FactoryPreset>& mt::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Master Safe -1 dBTP", { { "wOn", 0 }, { "tOn", 0 }, { "ceiling", -1.0f }, { "release", 120 } } },
        { "Loud Master",         { { "width", 1.1f }, { "monoBass", 120 }, { "attack", 0.1f }, { "drive", 6 }, { "ceiling", -1.0f }, { "release", 60 } } },
        { "Streaming -1 dBTP",   { { "monoBass", 100 }, { "tOn", 0 }, { "drive", 3 }, { "ceiling", -1.0f }, { "release", 150 } } },
        { "Wide Pads",           { { "width", 1.6f }, { "monoBass", 180 }, { "tOn", 0 }, { "drive", 0 }, { "ceiling", -1.0f } } },
        { "Mono Check",          { { "width", 0.0f }, { "tOn", 0 }, { "lOn", 0 } } },
        { "Tight Low End",       { { "width", 1.2f }, { "monoBass", 250 }, { "tOn", 0 }, { "ceiling", -1.0f } } },
        { "Punchy Drums",        { { "order", 1 }, { "width", 1.1f }, { "attack", 0.6f }, { "sustain", -0.2f }, { "speed", 0 }, { "drive", 3 }, { "ceiling", -1.0f }, { "release", 40 } } },
        { "Snappy Snare",        { { "wOn", 0 }, { "attack", 0.8f }, { "sustain", -0.4f }, { "speed", 0 }, { "ceiling", -1.0f }, { "release", 30 } } },
        { "Room Boost",          { { "order", 1 }, { "width", 1.3f }, { "attack", -0.2f }, { "sustain", 0.7f }, { "speed", 2 }, { "drive", 2 }, { "ceiling", -1.0f } } },
        { "Tame Peaks",          { { "wOn", 0 }, { "attack", -0.6f }, { "speed", 0 }, { "ceiling", -1.0f }, { "release", 100 } } },
        { "Dry Up The Room",     { { "wOn", 0 }, { "sustain", -0.7f }, { "speed", 1 }, { "ceiling", -1.0f } } },
        { "Plucky Guitar",       { { "width", 1.3f }, { "monoBass", 90 }, { "attack", 0.4f }, { "sustain", -0.3f }, { "ceiling", -1.0f } } },
        { "Vocal Width",         { { "width", 1.25f }, { "monoBass", 150 }, { "attack", 0.15f }, { "ceiling", -1.0f }, { "release", 120 } } },
        { "Bass Bus",            { { "width", 0.6f }, { "monoBass", 300 }, { "attack", 0.3f }, { "sustain", 0.2f }, { "drive", 2 }, { "ceiling", -1.0f } } },
        { "Brickwall Smash",     { { "wOn", 0 }, { "attack", 0.3f }, { "drive", 14 }, { "ceiling", -0.3f }, { "release", 25 }, { "gain", -1 } } },
        { "Super Wide Synth",    { { "width", 2.0f }, { "monoBass", 200 }, { "tOn", 0 }, { "drive", 1 }, { "ceiling", -1.0f } } },
        { "Soft Glue",           { { "width", 1.05f }, { "monoBass", 80 }, { "attack", -0.15f }, { "sustain", 0.15f }, { "speed", 2 }, { "drive", 2 }, { "ceiling", -1.0f }, { "release", 300 } } },
    };
    return p;
}
