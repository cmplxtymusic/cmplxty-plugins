// Sidechain Shaper factory presets: only the values that differ from the defaults (real units; choices by index), plus the curve shape.
// mode: 0 Tempo, 1 Sidechain.  source: 0 Sidechain, 1 Input.
// rate: 0 1/32, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 Bar, 6 2 Bars, 7 4 Bars, 8 1/16 T, 9 1/8 T, 10 1/4 T, 11 1/16 D, 12 1/8 D, 13 1/4 D.
#include "PresetManager.h"

namespace
{
std::string shape (const char* name) { return std::string ("<EXTRA shape=\"") + name + "\"/>"; }
}

const std::vector<sc::FactoryPreset>& sc::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Classic Pump",       { { "smooth", 2.0f } }, shape ("Pump") },
        { "Hard EDM Pump",      { { "smooth", 1.0f } }, shape ("Hard Pump") },
        { "Soft Duck",          { { "depth", 0.8f }, { "smooth", 3.0f } }, shape ("Soft Duck") },
        { "Half-Time Pump",     { { "rate", 4 }, { "smooth", 2.0f } }, shape ("Pump") },
        { "Subtle Glue",        { { "depth", 0.35f }, { "smooth", 4.0f } }, shape ("Pump") },
        { "Dotted Pump",        { { "rate", 12 }, { "smooth", 2.0f } }, shape ("Long Release") },
        { "16th Chop",          { { "rate", 4 }, { "smooth", 1.5f } }, shape ("Chop 8") },
        { "Trance Gate",        { { "rate", 5 }, { "smooth", 2.0f } }, shape ("Trance Gate") },
        { "Triplet Gate",       { { "rate", 9 }, { "depth", 0.9f }, { "smooth", 2.0f } }, shape ("Gate Half") },
        { "Stutter Bar",        { { "rate", 5 }, { "smooth", 1.0f } }, shape ("Stutter") },
        { "Wobble Sine",        { { "rate", 2 }, { "depth", 0.8f }, { "smooth", 0.0f } }, shape ("Sine") },
        { "Stereo Tremolo",     { { "rate", 1 }, { "depth", 0.6f }, { "smooth", 0.0f }, { "stereo", 0.5f } }, shape ("Sine") },
        { "Auto-Pan Triangle",  { { "rate", 4 }, { "depth", 0.7f }, { "smooth", 0.0f }, { "stereo", 0.5f } }, shape ("Triangle") },
        { "Riser Bar",          { { "rate", 5 }, { "smooth", 5.0f } }, shape ("Ramp Up") },
        { "Swell 2 Bars",       { { "rate", 6 }, { "smooth", 10.0f } }, shape ("Swell") },
        { "Kick Duck (SC)",     { { "mode", 1 }, { "rate", 2 }, { "focus", 150 }, { "threshold", -24 }, { "hold", 80 }, { "smooth", 1.0f } }, shape ("Pump") },
        { "Bass Ducker (SC)",   { { "mode", 1 }, { "rate", 3 }, { "focus", 120 }, { "threshold", -20 }, { "hold", 100 }, { "smooth", 1.5f } }, shape ("Hard Pump") },
        { "Ghost Duck (SC)",    { { "mode", 1 }, { "rate", 1 }, { "focus", 200 }, { "threshold", -30 }, { "depth", 0.5f }, { "smooth", 3.0f } }, shape ("Short Duck") },
        { "Auto Swell (Input)", { { "mode", 1 }, { "source", 1 }, { "rate", 3 }, { "threshold", -18 }, { "hold", 150 }, { "smooth", 2.0f } }, shape ("Swell") },
    };
    return p;
}
