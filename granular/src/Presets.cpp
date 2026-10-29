// Granular factory presets: only the values that differ from the defaults (real units; choices by index).
// spreadMode: 0 Free, 1 Semitones, 2 Fifths, 3 Octaves.  shape: 0 Smooth, 1 Triangle, 2 Perc, 3 Glitch.
#include "PresetManager.h"

const std::vector<gr::FactoryPreset>& gr::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Soft Cloud",       { { "size", 220 }, { "density", 22 }, { "spray", 300 }, { "chaos", 0.25f }, { "width", 0.9f }, { "tone", 9000 }, { "mix", 0.6f } } },
        { "Glitch Stutter",   { { "size", 35 }, { "density", 28 }, { "position", 60 }, { "spray", 20 }, { "shape", 3 }, { "chaos", 0.7f }, { "reverse", 0.25f }, { "width", 0.6f }, { "mix", 0.7f } } },
        { "Reverse Swell",    { { "size", 450 }, { "density", 10 }, { "position", 400 }, { "spray", 250 }, { "reverse", 1.0f }, { "width", 0.8f }, { "tone", 7000 }, { "mix", 0.55f } } },
        { "Octave Shimmer",   { { "size", 260 }, { "density", 18 }, { "spray", 200 }, { "pitch", 12 }, { "spread", 12 }, { "spreadMode", 3 }, { "feedback", 0.45f }, { "width", 1.0f }, { "tone", 11000 }, { "mix", 0.45f } } },
        { "Freeze Pad",       { { "size", 600 }, { "density", 24 }, { "position", 500 }, { "spray", 900 }, { "spread", 0.15f }, { "chaos", 0.4f }, { "width", 1.0f }, { "tone", 6000 }, { "feedback", 0.35f }, { "mix", 0.9f } } },   // hit FREEZE to hold it
        { "Fifths Choir",     { { "size", 320 }, { "density", 20 }, { "spray", 350 }, { "spread", 12 }, { "spreadMode", 2 }, { "chaos", 0.3f }, { "width", 0.95f }, { "tone", 8000 }, { "mix", 0.6f } } },
        { "Down Under",       { { "size", 300 }, { "density", 14 }, { "spray", 150 }, { "pitch", -12 }, { "feedback", 0.3f }, { "tone", 4000 }, { "width", 0.5f }, { "mix", 0.5f } } },
        { "Bit Rain",         { { "size", 12 }, { "density", 70 }, { "position", 20 }, { "spray", 800 }, { "spread", 24 }, { "spreadMode", 1 }, { "shape", 2 }, { "chaos", 0.9f }, { "width", 1.0f }, { "mix", 0.5f }, { "gain", -3 } } },
        { "Tape Scatter",     { { "size", 90 }, { "density", 12 }, { "position", 250 }, { "spray", 600 }, { "spread", 0.4f }, { "reverse", 0.4f }, { "chaos", 0.6f }, { "tone", 5000 }, { "width", 0.7f }, { "mix", 0.5f } } },
        { "Granular Delay",   { { "size", 180 }, { "density", 6 }, { "position", 375 }, { "spray", 10 }, { "chaos", 0.05f }, { "feedback", 0.6f }, { "width", 0.8f }, { "tone", 7000 }, { "mix", 0.4f } } },
        { "Robot Chatter",    { { "size", 25 }, { "density", 40 }, { "position", 30 }, { "spray", 120 }, { "spread", 7 }, { "spreadMode", 1 }, { "shape", 3 }, { "chaos", 0.5f }, { "width", 0.5f }, { "mix", 0.65f } } },
        { "Infinite Wash",    { { "size", 900 }, { "density", 30 }, { "position", 800 }, { "spray", 1000 }, { "spread", 0.3f }, { "reverse", 0.5f }, { "feedback", 0.85f }, { "width", 1.0f }, { "tone", 5000 }, { "mix", 0.7f }, { "gain", -4 } } },
        { "Perc Sparkle",     { { "size", 60 }, { "density", 20 }, { "spray", 400 }, { "pitch", 12 }, { "spread", 12 }, { "spreadMode", 3 }, { "shape", 2 }, { "chaos", 0.6f }, { "width", 1.0f }, { "tone", 14000 }, { "mix", 0.4f } } },
        { "Dark Matter",      { { "size", 500 }, { "density", 16 }, { "position", 600 }, { "spray", 700 }, { "pitch", -24 }, { "spread", 12 }, { "spreadMode", 3 }, { "feedback", 0.5f }, { "tone", 1800 }, { "width", 0.9f }, { "mix", 0.6f } } },
        { "Micro Smear",      { { "size", 15 }, { "density", 80 }, { "position", 10 }, { "spray", 40 }, { "spread", 0.3f }, { "shape", 1 }, { "chaos", 0.2f }, { "width", 0.6f }, { "mix", 0.5f } } },
    };
    return p;
}
