// CMPLXTY Visualizer - factory presets. Original looks written for this plug-in.
#include "Presets.h"
#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace vz::presets
{
namespace
{
const char* kLyrics = "TURN IT UP\nFEEL THE LOW END\nLIGHTS DOWN\nONE MORE TIME";
const char* kCyber  = "NEON NEVER SLEEPS\nSIGNAL // NOISE\nJACK IN\nCITY OF CHROME";
const char* kWords  = "BASS\nKICK\nSNARE\nLOOP\nDROP\nWAVE\nPULSE\nNOISE\nGLOW\nRUSH";

std::vector<Factory> build()
{
    return {
        { "Init",                "INIT",     "" },
        // ---- scope ----
        { "Green Phosphor",      "SCOPE",    "mode=Glow Scope;palette=Matrix;glow=0.6;trails=0.5;texOn=1;texType=CRT;texAmt=0.55" },
        { "Lissajous Ice",       "SCOPE",    "mode=XY Scope;palette=Ice;trails=0.7;glow=0.5;detail=0.6" },
        { "Halo Ring",           "SCOPE",    "mode=Ring Scope;palette=Neon Night;glow=0.5;trails=0.4;spin=0.12" },
        { "Polygon Pulse",       "SCOPE",    "mode=Shape Scope;palette=Synthwave;detail=0.7;glow=0.45;flash=0.3" },
        { "Mono Oscillator",     "SCOPE",    "mode=Glow Scope;palette=Mono;glow=0.4;trails=0.3;flash=0" },
        // ---- lava ----
        { "Lava Lamp",           "LAVA",     "mode=Lava Lamp;palette=Lava;speed=0.6;glow=0.3" },
        { "Goo Contours",        "LAVA",     "mode=Goo Contours;palette=Toxic;speed=0.8;glow=0.4" },
        { "Blob Pulse",          "LAVA",     "mode=Blob Pulse;palette=Candy;flash=0.3;glow=0.4" },
        { "Liquid Wax",          "LAVA",     "mode=Lava Lamp;palette=Sunset;zoom=1.3;texOn=1;texType=Grain;texAmt=0.35" },
        // ---- retro / arcade ----
        { "Outrun Grid",         "RETRO",    "mode=Synth Grid;palette=Synthwave;glow=0.5;texOn=1;texType=CRT;texAmt=0.35" },
        { "Neon Tunnel",         "RETRO",    "mode=Neon Tunnel;palette=Neon Night;glow=0.5;speed=1.2" },
        { "Pulsar Ridges",       "RETRO",    "mode=Ridge Lines;palette=Mono;glow=0.2;flash=0.1" },
        { "Copper Demo",         "RETRO",    "mode=Copper Bars;palette=Rainbow;texOn=1;texType=CRT;texAmt=0.4" },
        { "Arcade Cabinet",      "RETRO",    "mode=Synth Grid;palette=Amber CRT;texOn=1;texType=CRT;texAmt=0.8;glow=0.55" },
        // ---- space ----
        { "Deep Field",          "SPACE",    "mode=Starfield;palette=Ice;trails=0.3" },
        { "Jump To Light",       "SPACE",    "mode=Hyperspace;palette=Ultraviolet;glow=0.6;speed=1.3" },
        { "Nebula Drift",        "SPACE",    "mode=Nebula;palette=Aurora;speed=0.5;glow=0.45" },
        { "Orbit Lab",           "SPACE",    "mode=Orbits;palette=Hologram;trails=0.6;glow=0.5" },
        // ---- code ----
        { "Green Rain",          "CODE",     "mode=Code Rain;palette=Matrix;glow=0.45" },
        { "Glyph Storm",         "CODE",     "mode=Glyph Storm;palette=Cyber Yellow;glow=0.4" },
        { "Binary Feed",         "CODE",     "mode=Binary Stream;palette=Mono Blue;glow=0.35" },
        { "Cyberpunk District",  "CODE",     "mode=Code Rain;palette=Cyber Yellow;texOn=1;texType=VHS;texAmt=0.45;trkOn=1;trkStyle=Brackets;trkAmt=0.5;textOver=1;textAnim=Glitch;textGlitch=0.6;textSize=0.8", kCyber },
        // ---- wash ----
        { "Slow Wash",           "WASH",     "mode=Color Wash;palette=Pastel;speed=0.4;flash=0.1" },
        { "Gradient Sweep",      "WASH",     "mode=Gradient Sweep;palette=Sunset" },
        { "Beat Blocks",         "WASH",     "mode=Beat Blocks;palette=Acid;flash=0.4" },
        { "Paper White",         "WASH",     "mode=Color Wash;palette=Paper;texOn=1;texType=Grain;texAmt=0.4" },
        // ---- static / TV ----
        { "Dead Channel",        "STATIC",   "mode=TV Static;palette=Mono;texOn=1;texType=CRT;texAmt=0.5" },
        { "Scanline Roll",       "STATIC",   "mode=Scanline Roll;palette=Amber CRT;glow=0.4" },
        { "Signal Lost",         "STATIC",   "mode=Signal Lost;palette=Rainbow" },
        // ---- 8-bit ----
        { "8-Bit Meter",         "PIXEL",    "mode=8-Bit Bars;palette=Pocket Green;glow=0.2" },
        { "Pixel Fire",          "PIXEL",    "mode=Pixel Fire;palette=Fire;glow=0.35" },
        { "Life Grid",           "PIXEL",    "mode=Life Grid;palette=Neon Night;glow=0.4" },
        { "Mosaic Floor",        "PIXEL",    "mode=Mosaic;palette=Coral Reef" },
        // ---- text ----
        { "Lyric Pulse",         "TEXT",     "mode=Lyric;palette=Mono;textAnim=Pulse;textCycle=Bar", kLyrics },
        { "Glitch Type",         "TEXT",     "mode=Glitch Type;palette=Neon Night;textAnim=Glitch;textGlitch=0.6", kLyrics },
        { "Marquee",             "TEXT",     "mode=Marquee;palette=Synthwave;glow=0.5", kLyrics },
        { "Word Wall",           "TEXT",     "mode=Word Wall;palette=Inferno;textCycle=Beat", kWords },
        { "Typewriter",          "TEXT",     "mode=Lyric;palette=Paper;textAnim=Typewriter;textCycle=2 Bars;texOn=1;texType=Grain;texAmt=0.3", kLyrics },
        { "Wave Words",          "TEXT",     "mode=Lyric;palette=Vapor;textAnim=Wave;glow=0.5", kLyrics },
        { "Zoom Lines",          "TEXT",     "mode=Lyric;palette=Ultraviolet;textAnim=Zoom;textCycle=Bar;glow=0.5", kLyrics },
        { "Shake Shout",         "TEXT",     "mode=Glitch Type;palette=Blood;textAnim=Shake;textCycle=Beat;flash=0.4", kWords },
        // ---- plasma ----
        { "Classic Plasma",      "PLASMA",   "mode=Plasma;palette=Neon Night" },
        { "Liquid Waves",        "PLASMA",   "mode=Liquid Waves;palette=Ocean" },
        { "Interference",        "PLASMA",   "mode=Interference;palette=Mono" },
        { "Warp Swirl",          "PLASMA",   "mode=Warp Swirl;palette=Inferno" },
        { "Acid Plasma",         "PLASMA",   "mode=Plasma;palette=Acid;speed=1.6;cycle=0.5" },
        // ---- spectrum ----
        { "Spectrum Classic",    "SPECTRUM", "mode=Spectrum Bars;palette=Heat Map;glow=0.3" },
        { "Mirror Bars",         "SPECTRUM", "mode=Mirror Bars;palette=Ice;glow=0.5" },
        { "Circle Spectrum",     "SPECTRUM", "mode=Circle Spectrum;palette=Hologram;spin=0.1;glow=0.5" },
        { "Radial Burst",        "SPECTRUM", "mode=Radial Burst;palette=Sunset;glow=0.5" },
        { "Waterfall",           "SPECTRUM", "mode=Waterfall;palette=Inferno" },
        { "Mono Bars",           "SPECTRUM", "mode=Spectrum Bars;palette=Silver;glow=0.25" },
        // ---- image ----
        { "Image Pulse",         "IMAGE",    "mode=Image Pulse;palette=Mono;imgChroma=0.4;imgPulse=0.7" },
        { "Image Tiles",         "IMAGE",    "mode=Image Tiles;palette=Neon Night" },
        { "Image Slices",        "IMAGE",    "mode=Image Slices;palette=Vapor" },
        { "Image Pixelate",      "IMAGE",    "mode=Image Pixelate;palette=Pocket Green" },
        { "Cover Art Glow",      "IMAGE",    "mode=Circle Spectrum;palette=Mono;imgOn=1;imgBlend=Over;imgSize=0.5;imgGlow=0.7;imgPulse=0.7;imgChroma=0.3" },
        { "Image Mask",          "IMAGE",    "mode=Plasma;palette=Rainbow;imgOn=1;imgBlend=Mask;imgSize=1.2;imgPulse=0.4" },
        { "Image Texture",       "IMAGE",    "mode=Liquid Waves;palette=Sunset;imgOn=1;imgBlend=Texture;imgSize=2;imgPulse=0.2" },
        { "Photo Shake",         "IMAGE",    "mode=TV Static;palette=Graphite;imgOn=1;imgBlend=Screen;imgSize=0.9;imgShake=0.6;imgChroma=0.7;texOn=1;texType=VHS;texAmt=0.5" },
        // ---- FX ----
        { "ASCII Plasma",        "FX",       "mode=Plasma;palette=Matrix;ditOn=1;ditType=Symbol;ditSize=0.3" },
        { "Star Dither",         "FX",       "mode=Nebula;palette=Gold;ditOn=1;ditType=Star;ditSize=0.45" },
        { "Hex Hive",            "FX",       "mode=Lava Lamp;palette=Gold;ditOn=1;ditType=Hex;ditSize=0.5" },
        { "Halftone",            "FX",       "mode=Liquid Waves;palette=Mono;ditOn=1;ditType=Dots;ditSize=0.35" },
        { "Bayer Handheld",      "FX",       "mode=Starfield;palette=Pocket Green;ditOn=1;ditType=Bayer;ditSize=0.5" },
        { "Kaleidoscope",        "FX",       "mode=Plasma;palette=Hologram;glsOn=1;glsType=Kaleido;glsAmt=0.6" },
        { "Lens Bulge",          "FX",       "mode=Spectrum Bars;palette=Neon Night;glsOn=1;glsType=Lens;glsAmt=0.6" },
        { "Prism Burst",         "FX",       "mode=Radial Burst;palette=Rainbow;glsOn=1;glsType=Prism;glsAmt=0.5" },
        { "Ripple Pool",         "FX",       "mode=Liquid Waves;palette=Deep Sea;glsOn=1;glsType=Ripple;glsAmt=0.6" },
        { "Shard Glass",         "FX",       "mode=Warp Swirl;palette=Ultraviolet;glsOn=1;glsType=Shards;glsAmt=0.5" },
        { "Tracker HUD",         "FX",       "mode=Lava Lamp;palette=Mono;trkOn=1;trkStyle=Boxes;trkAmt=0.6;texOn=1;texType=Grain;texAmt=0.3" },
        { "Target Lock",         "FX",       "mode=Blob Pulse;palette=Mono Red;trkOn=1;trkStyle=Crosshair;trkAmt=0.5" },
        { "Node Network",        "FX",       "mode=Starfield;palette=Mono Blue;trkOn=1;trkStyle=Network;trkAmt=0.7" },
        { "VHS Tape",            "FX",       "mode=Plasma;palette=Vapor;texOn=1;texType=VHS;texAmt=0.6" },
        { "Threshold Noir",      "FX",       "mode=Lava Lamp;palette=Noir;texOn=1;texType=Threshold;texAmt=0.5" },
        { "Noise Floor",         "FX",       "mode=Color Wash;palette=Deep Sea;texOn=1;texType=Noise;texAmt=0.6" },
        // ---- multi-grid ----
        { "Grid 2x2 Cascade",    "GRID",     "mode=Plasma;palette=Neon Night;grid=2x2;gridLink=Cascade" },
        { "Grid 3x3 Mirror",     "GRID",     "mode=Circle Spectrum;palette=Ice;grid=3x3;gridLink=Mirror" },
        { "Grid Bands",          "GRID",     "mode=Blob Pulse;palette=Candy;grid=3x1;gridLink=Bands" },
        { "Wall Of Screens",     "GRID",     "mode=Code Rain;palette=Matrix;grid=6x6;gridLink=Cascade;gridGap=0.3" },
        { "Beat Swap 4x4",       "GRID",     "mode=Spectrum Bars;palette=Synthwave;grid=4x4;gridLink=Beat Swap" },
        { "Mixed Monitors",      "GRID",     "mode=Glow Scope;palette=Hologram;grid=3x2;gridLink=Free;gridGap=0.4" },
        // ---- mono ----
        { "Mono Tunnel",         "MONO",     "mode=Neon Tunnel;palette=Silver;glow=0.4" },
        { "Mono Rain",           "MONO",     "mode=Code Rain;palette=Mono" },
        { "Blood Moon",          "MONO",     "mode=Circle Spectrum;palette=Mono Red;glow=0.5" },
        { "Graphite Lava",       "MONO",     "mode=Goo Contours;palette=Graphite" },
    };
}
} // namespace

const std::vector<Factory>& factory()
{
    static const std::vector<Factory> f = build();
    return f;
}

ParamValues defaults()
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = kParams[i].def;
    return v;
}

int paramIndex (const juce::String& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (id.equalsIgnoreCase (kParams[i].id)) return i;
    return -1;
}

float parseValue (int param, const juce::String& value)
{
    const auto& d = kParams[param];
    const auto v = value.trim();
    if (d.type == 'c')
    {
        const auto ch = juce::StringArray::fromTokens (d.choices, "|", "");
        const int idx = ch.indexOf (v, true);
        if (idx >= 0) return (float) idx;
        return (float) juce::jlimit (0, ch.size() - 1, v.getIntValue());
    }
    if (d.type == 'b')
        return (v.equalsIgnoreCase ("on") || v.equalsIgnoreCase ("true") || v.getFloatValue() > 0.5f) ? 1.0f : 0.0f;
    return juce::jlimit (d.lo, d.hi, v.getFloatValue());
}

juce::String formatValue (int param, float value)
{
    const auto& d = kParams[param];
    if (d.type == 'c')
    {
        const auto ch = juce::StringArray::fromTokens (d.choices, "|", "");
        return ch[juce::jlimit (0, ch.size() - 1, juce::roundToInt (value))];
    }
    if (d.type == 'b') return value > 0.5f ? "1" : "0";
    return juce::String (value, 4);
}

int applySpec (ParamValues& base, const juce::String& spec)
{
    int bad = 0;
    for (auto& tok : juce::StringArray::fromTokens (spec, ";", ""))
    {
        if (tok.trim().isEmpty()) continue;
        const int eq = tok.indexOfChar ('=');
        const int idx = eq > 0 ? paramIndex (tok.substring (0, eq).trim()) : -1;
        if (idx < 0) { ++bad; continue; }
        const auto& d = kParams[idx];
        const auto val = tok.substring (eq + 1).trim();
        if (d.type == 'c' && juce::StringArray::fromTokens (d.choices, "|", "").indexOf (val, true) < 0 && ! val.containsOnly ("0123456789"))
            ++bad;
        base[(size_t) idx] = parseValue (idx, val);
    }
    return bad;
}

ParamValues valuesFor (const Factory& f)
{
    auto v = defaults();
    applySpec (v, f.spec);
    return v;
}

bool isGlobalParam (int p)
{
    return p == pGain || p == pStrobe || p == pStrobeRate || p == pBlackout || p == pQuality || p == pFps;
}

juce::File dataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CMPLXTY").getChildFile ("Visualizer");
}
juce::File userDir()   { return dataDir().getChildFile ("Presets"); }
juce::File imagesDir() { return dataDir().getChildFile ("Images"); }

juce::Array<juce::File> userPresets()
{
    auto files = userDir().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExt);
    files.sort();
    return files;
}
} // namespace vz::presets
