// Silt factory presets. Only values that differ from the defaults, in real units (choices by index; size in ms, density in grains / s).
// Mod sources: 1 LFO 1, 2 LFO 2, 3 Mod Env, 4-7 Macro 1-4, 8 Random, 9 Velocity, 10 Note, 11 Wheel, 12 Pressure, 13 Timbre
// Mod targets: 1 Position, 2 Scan, 3 Density, 4 Size, 5 Pitch, 6 Pitch Jitter, 7 Spray, 8 Spread, 9 Shape, 10 Reverse, 11 Magnet,
//              12 Cohere, 13 Morph, 14 Cutoff, 15 Resonance, 16 Level, 17 Pan
// Each preset also picks a built-in source per layer (kSources); with "Keep My Sample" on, a sample you dropped in stays.
#include "PresetKit.h"
#include "SiltParams.h"
#include "SiltSources.h"

namespace
{
using namespace slt;
struct Src { int a, b; };
const Src kSources[] = {
    { biGlass, biChoir },   // Init
    { biGlass, -1 },        // Glass Cloud
    { biChoir, -1 },        // Frozen Choir
    { biKeys, -1 },         // Scale Lock Shimmer
    { biBreak, -1 },        // Break Slicer
    { biBreak, -1 },        // Break Seq
    { biPluck, -1 },        // Stutter Chop
    { biPluck, -1 },        // Pluck Magnet
    { biGlass, biChoir },   // Morph Glass to Choir
    { biKeys, biGlass },    // Ring Metal
    { biChoir, biAir },     // Spectral Cross
    { biAir, -1 },          // Dark Drone
    { biKeys, -1 },         // Rhythmic Grains
    { biGlass, -1 },        // Glide Lead
    { biChoir, -1 },        // Reverse Swells
    { biGlass, -1 },        // MPE Pressure Pad
    { biKeys, -1 },         // Crushed Keys
    { biGlass, biAir },     // Macro Pad
    { biPluck, -1 },        // Pluck Grid Gate
    { biBreak, -1 },        // Break Granular Keys
};
} // namespace

const std::vector<pk::FactoryPreset>& pk::factoryPresets()
{
    static const std::vector<FactoryPreset> p {
        { "Init", {} },
        { "Glass Cloud", { { "density", 40 }, { "size", 180 }, { "spray", 0.3f }, { "spread", 0.85f }, { "pjit", 0.1f }, { "aA", 0.4f }, { "aR", 2.0f },
                           { "rvMix", 0.4f }, { "rvSize", 0.8f }, { "scan", 0.05f } } },
        { "Frozen Choir", { { "pos", 0.4f }, { "density", 60 }, { "size", 250 }, { "cohere", 0.8f }, { "aA", 0.8f }, { "aR", 3.0f }, { "rvMix", 0.45f },
                            { "l1Rate", 0.1f }, { "ms1", 1 }, { "md1", 1 }, { "ma1", 0.08f } } },
        { "Scale Lock Shimmer", { { "pjit", 7.0f }, { "scale", 1 }, { "cohere", 1.0f }, { "density", 50 }, { "size", 90 }, { "dlOn", 1 }, { "dlMix", 0.3f },
                                  { "rvMix", 0.4f }, { "spread", 0.9f }, { "aR", 1.5f } } },
        { "Break Slicer", { { "mode", 1 }, { "slPlay", 0 }, { "rvMix", 0.12f }, { "aR", 0.15f }, { "velSens", 0.8f } } },
        { "Break Seq 16ths", { { "mode", 1 }, { "seq", 1 }, { "st1", 1 }, { "st2", 2 }, { "st3", 1 }, { "st4", 4 }, { "st5", 5 }, { "st6", 2 }, { "st7", 7 }, { "st8", 4 },
                               { "st9", 1 }, { "st10", 1 }, { "st11", 3 }, { "st12", 4 }, { "st13", 5 }, { "st14", 6 }, { "st15", 9 }, { "st16", 12 },
                               { "rpOn", 1 }, { "rpChance", 0.2f }, { "rvMix", 0.1f }, { "seqGate", 0.9f } } },
        { "Stutter Chop", { { "mode", 1 }, { "seq", 1 }, { "seqRate", 3 }, { "seqGate", 0.4f }, { "rpOn", 1 }, { "rpChance", 0.5f }, { "rpLen", 3 }, { "slSrc", 2 },
                            { "rvMix", 0.15f } } },
        { "Pluck Magnet", { { "magnet", 0.9f }, { "density", 30 }, { "size", 60 }, { "shape", 0.8f }, { "pos", 0.1f }, { "scan", 0.5f }, { "rvMix", 0.2f } } },
        { "Morph Glass to Choir", { { "bOn", 1 }, { "morph", 0.5f }, { "l1Rate", 0.15f }, { "ms1", 1 }, { "md1", 13 }, { "ma1", 0.5f }, { "aA", 0.3f }, { "aR", 2.0f },
                                    { "rvMix", 0.35f } } },
        { "Ring Metal", { { "bOn", 1 }, { "morphMode", 2 }, { "morph", 0.6f }, { "fOn", 1 }, { "fType", 1 }, { "cutoff", 2500 }, { "res", 0.5f }, { "density", 70 },
                          { "size", 50 } } },
        { "Spectral Cross", { { "bOn", 1 }, { "morphMode", 3 }, { "morph", 0.7f }, { "density", 50 }, { "size", 140 }, { "aA", 0.5f }, { "aR", 2.5f }, { "rvMix", 0.4f } } },
        { "Dark Drone", { { "pitch", -12 }, { "density", 80 }, { "size", 400 }, { "fOn", 1 }, { "cutoff", 900 }, { "res", 0.3f }, { "rvMix", 0.5f }, { "rvDark", 0.9f },
                          { "aA", 1.5f }, { "aR", 4.0f }, { "spray", 0.5f } } },
        { "Rhythmic Grains", { { "density", 12 }, { "size", 40 }, { "shape", 1.0f }, { "spray", 0.02f }, { "fOn", 1 }, { "cutoff", 3000 }, { "ms1", 2 }, { "md1", 14 },
                               { "ma1", 0.4f }, { "dlOn", 1 }, { "dlMix", 0.3f }, { "l2Sync", 8 } } },
        { "Glide Lead", { { "voices", 1 }, { "glide", 0.15f }, { "size", 60 }, { "density", 90 }, { "cohere", 1.0f }, { "aA", 0.005f }, { "rvMix", 0.15f },
                          { "spread", 0.2f } } },
        { "Reverse Swells", { { "reverse", 1.0f }, { "aA", 1.2f }, { "size", 300 }, { "density", 30 }, { "rvMix", 0.5f }, { "aR", 2.5f } } },
        { "MPE Pressure Pad", { { "mpe", 1 }, { "ms1", 12 }, { "md1", 14 }, { "ma1", 0.5f }, { "ms2", 13 }, { "md2", 1 }, { "ma2", 0.3f }, { "fOn", 1 }, { "cutoff", 1500 },
                                { "aA", 0.3f }, { "aR", 2.0f } } },
        { "Crushed Keys", { { "cpOn", 1 }, { "cpThr", -30 }, { "cpRatio", 8 }, { "cpGain", 10 }, { "density", 120 }, { "size", 25 }, { "fxOrder", 6 }, { "rvMix", 0.2f } } },
        { "Macro Pad", { { "bOn", 1 }, { "m1", 0.3f }, { "ms1", 4 }, { "md1", 3 }, { "ma1", 0.6f }, { "ms2", 4 }, { "md2", 4 }, { "ma2", 0.5f }, { "ms3", 5 }, { "md3", 6 },
                         { "ma3", 0.5f }, { "ms4", 6 }, { "md4", 13 }, { "ma4", 1.0f }, { "aA", 0.4f }, { "aR", 2.0f }, { "rvMix", 0.35f } } },
        { "Pluck Grid Gate", { { "mode", 1 }, { "slSrc", 2 }, { "slPlay", 1 }, { "aR", 0.08f }, { "dlOn", 1 }, { "dlTime", 2 }, { "dlMix", 0.25f } } },
        { "Break Granular Keys", { { "mode", 1 }, { "slPlay", 2 }, { "density", 50 }, { "size", 80 }, { "aA", 0.05f }, { "aR", 0.8f }, { "rvMix", 0.25f } } },
    };
    return p;
}

int siltFactorySource (int i, int layer)
{
    if (i < 0 || i >= (int) (sizeof (kSources) / sizeof (kSources[0]))) return -1;
    return layer == 0 ? kSources[i].a : kSources[i].b;
}
