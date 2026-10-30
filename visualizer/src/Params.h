#pragma once
// CMPLXTY Visualizer - parameter table, visual modes, palettes. Every parameter here is host-automatable.
#include "ParamTable.h"
#include <array>

namespace vz
{
enum P : int
{
    pMode, pPalette, pSpeed, pZoom, pDetail, pSpin, pHue, pCycle, pBright, pTrails, pGlow, pFlash, pMirror,
    pReact, pGain, pSmooth, pSync, pBeatMode,
    pImgOn, pImgBlend, pImgSize, pImgOpacity, pImgSpin, pImgCycle, pImgPulse, pImgGlow, pImgShake, pImgChroma,
    pTextOver, pTextAnim, pTextSize, pTextCycle, pTextGlitch,
    pTexOn, pTexType, pTexAmt, pDitOn, pDitType, pDitSize, pDitMix, pTrkOn, pTrkStyle, pTrkAmt, pGlsOn, pGlsType, pGlsAmt,
    pGrid, pGridLink, pGridGap,
    pStrobe, pStrobeRate, pBlackout, pQuality, pFps,
    kNumParams
};
using ParamValues = std::array<float, kNumParams>;

// ---- visual modes (12 categories) -----------------------------------------------------------------------------
enum Cat : int { cScope, cLava, cRetro, cSpace, cCode, cWash, cStatic, cPixel, cText, cPlasma, cSpectrum, cImage, kNumCats };
inline constexpr const char* kCatNames[kNumCats] = { "SCOPE", "LAVA", "RETRO", "SPACE", "CODE", "WASH", "STATIC", "PIXEL", "TEXT", "PLASMA", "SPECTRUM", "IMAGE" };
struct ModeInfo { const char* name; int cat; };
inline constexpr ModeInfo kModes[] = {
    { "Glow Scope", cScope }, { "XY Scope", cScope }, { "Ring Scope", cScope }, { "Shape Scope", cScope },
    { "Lava Lamp", cLava }, { "Blob Pulse", cLava }, { "Goo Contours", cLava },
    { "Synth Grid", cRetro }, { "Neon Tunnel", cRetro }, { "Ridge Lines", cRetro }, { "Copper Bars", cRetro },
    { "Starfield", cSpace }, { "Hyperspace", cSpace }, { "Nebula", cSpace }, { "Orbits", cSpace },
    { "Code Rain", cCode }, { "Glyph Storm", cCode }, { "Binary Stream", cCode },
    { "Color Wash", cWash }, { "Gradient Sweep", cWash }, { "Beat Blocks", cWash },
    { "TV Static", cStatic }, { "Scanline Roll", cStatic }, { "Signal Lost", cStatic },
    { "8-Bit Bars", cPixel }, { "Pixel Fire", cPixel }, { "Life Grid", cPixel }, { "Mosaic", cPixel },
    { "Lyric", cText }, { "Glitch Type", cText }, { "Marquee", cText }, { "Word Wall", cText },
    { "Plasma", cPlasma }, { "Liquid Waves", cPlasma }, { "Interference", cPlasma }, { "Warp Swirl", cPlasma },
    { "Spectrum Bars", cSpectrum }, { "Mirror Bars", cSpectrum }, { "Circle Spectrum", cSpectrum }, { "Radial Burst", cSpectrum }, { "Waterfall", cSpectrum },
    { "Image Pulse", cImage }, { "Image Tiles", cImage }, { "Image Slices", cImage }, { "Image Pixelate", cImage },
};
constexpr int kNumModes = (int) (sizeof (kModes) / sizeof (kModes[0]));
#define VZ_MODE_CHOICES "Glow Scope|XY Scope|Ring Scope|Shape Scope|Lava Lamp|Blob Pulse|Goo Contours|Synth Grid|Neon Tunnel|Ridge Lines|Copper Bars|" \
    "Starfield|Hyperspace|Nebula|Orbits|Code Rain|Glyph Storm|Binary Stream|Color Wash|Gradient Sweep|Beat Blocks|TV Static|Scanline Roll|Signal Lost|" \
    "8-Bit Bars|Pixel Fire|Life Grid|Mosaic|Lyric|Glitch Type|Marquee|Word Wall|Plasma|Liquid Waves|Interference|Warp Swirl|" \
    "Spectrum Bars|Mirror Bars|Circle Spectrum|Radial Burst|Waterfall|Image Pulse|Image Tiles|Image Slices|Image Pixelate"

// ---- palettes ---------------------------------------------------------------------------------------------------
struct PaletteDef { const char* name; bool mono; int n; unsigned int stops[7]; };
inline constexpr PaletteDef kPalettes[] = {
    { "Mono",        true,  2, { 0xff000000, 0xffffffff } },
    { "Silver",      true,  4, { 0xff050505, 0xff4a4a4a, 0xffb4b4b4, 0xffffffff } },
    { "Graphite",    true,  3, { 0xff000000, 0xff3a3a3a, 0xff9a9a9a } },
    { "Noir",        true,  5, { 0xff000000, 0xff101010, 0xff303030, 0xffd0d0d0, 0xffffffff } },
    { "Paper",       true,  3, { 0xff111111, 0xffe8e8e8, 0xffffffff } },
    { "Neon Night",  false, 5, { 0xff05010f, 0xff3b0a8c, 0xffff2bd6, 0xff21f7ff, 0xffffffff } },
    { "Synthwave",   false, 6, { 0xff0b0221, 0xff2d0b5a, 0xffb5179e, 0xffff4d6d, 0xffffb347, 0xfffff3b0 } },
    { "Vapor",       false, 5, { 0xff1a1035, 0xff6a5acd, 0xffff71ce, 0xff01cdfe, 0xfffffb96 } },
    { "Cyber Yellow",false, 4, { 0xff000000, 0xff1c1c1c, 0xffb8a000, 0xfffff200 } },
    { "Matrix",      false, 4, { 0xff000000, 0xff003b00, 0xff00b140, 0xffc8ffc8 } },
    { "Amber CRT",   false, 4, { 0xff000000, 0xff3a1800, 0xffff8c00, 0xffffe0a0 } },
    { "Inferno",     false, 6, { 0xff000004, 0xff320a5e, 0xff8a226a, 0xffd94d3d, 0xfffa9c1b, 0xfffcffa4 } },
    { "Fire",        false, 5, { 0xff000000, 0xff600000, 0xffe02000, 0xffffa000, 0xffffffc0 } },
    { "Lava",        false, 5, { 0xff0a0000, 0xff5c0010, 0xffd7263d, 0xffff8c42, 0xfffff1a8 } },
    { "Ocean",       false, 5, { 0xff000814, 0xff001d3d, 0xff0077b6, 0xff00b4d8, 0xffcaf0f8 } },
    { "Ice",         false, 4, { 0xff02060f, 0xff1b3b6f, 0xff8ecae6, 0xffffffff } },
    { "Aurora",      false, 6, { 0xff020b14, 0xff0b3d2e, 0xff1fd18f, 0xff3ec1ff, 0xff9b5de5, 0xfff7f7ff } },
    { "Sunset",      false, 6, { 0xff12002b, 0xff5a0f5e, 0xffc81d77, 0xffff6f3c, 0xffffc045, 0xfffff4d6 } },
    { "Toxic",       false, 4, { 0xff030500, 0xff2b4000, 0xff9dff00, 0xfff4ffbf } },
    { "Acid",        false, 5, { 0xff000000, 0xffff00a0, 0xff00ff60, 0xfffff000, 0xff00e0ff } },
    { "Blood",       false, 4, { 0xff000000, 0xff3d0000, 0xffb00010, 0xffff5050 } },
    { "Gold",        false, 5, { 0xff0a0600, 0xff4d3200, 0xffb8860b, 0xffffd34d, 0xfffff8e1 } },
    { "Rainbow",     false, 7, { 0xffff0040, 0xffff8000, 0xffffee00, 0xff20e040, 0xff00b0ff, 0xff6040ff, 0xffff00c0 } },
    { "Pastel",      false, 5, { 0xff2b2d42, 0xffffadad, 0xffffd6a5, 0xffcaffbf, 0xffa0c4ff } },
    { "Candy",       false, 5, { 0xff1b0026, 0xffff4fa3, 0xffffd1e8, 0xff7afcff, 0xffffffff } },
    { "Forest",      false, 5, { 0xff020a04, 0xff0f3d1e, 0xff2d6a4f, 0xff95d5b2, 0xffe9f5db } },
    { "Ultraviolet", false, 5, { 0xff000000, 0xff1a0033, 0xff5a00b4, 0xffb026ff, 0xffffb3ff } },
    { "Hologram",    false, 6, { 0xff050510, 0xff2de2e6, 0xff9b5de5, 0xfff15bb5, 0xfffee440, 0xff00f5d4 } },
    { "Deep Sea",    false, 5, { 0xff000000, 0xff00121f, 0xff003a52, 0xff00a6a6, 0xffa8fff0 } },
    { "Pocket Green",false, 4, { 0xff0f380f, 0xff306230, 0xff8bac0f, 0xff9bbc0f } },
    { "Heat Map",    false, 6, { 0xff000020, 0xff0000ff, 0xff00ffff, 0xff00ff00, 0xffffff00, 0xffff0000 } },
    { "Coral Reef",  false, 5, { 0xff03045e, 0xff0096c7, 0xffff6b6b, 0xffffd93d, 0xfffff5e4 } },
    { "Mono Red",    true,  3, { 0xff000000, 0xff9a0000, 0xffffffff } },
    { "Mono Blue",   true,  3, { 0xff000000, 0xff1e3a8a, 0xffffffff } },
};
constexpr int kNumPalettes = (int) (sizeof (kPalettes) / sizeof (kPalettes[0]));
#define VZ_PALETTE_CHOICES "Mono|Silver|Graphite|Noir|Paper|Neon Night|Synthwave|Vapor|Cyber Yellow|Matrix|Amber CRT|Inferno|Fire|Lava|Ocean|Ice|Aurora|" \
    "Sunset|Toxic|Acid|Blood|Gold|Rainbow|Pastel|Candy|Forest|Ultraviolet|Hologram|Deep Sea|Pocket Green|Heat Map|Coral Reef|Mono Red|Mono Blue"

// grid layouts: columns x rows
struct GridDef { const char* name; int cols, rows; };
inline constexpr GridDef kGrids[] = { { "Off", 1, 1 }, { "2x1", 2, 1 }, { "1x2", 1, 2 }, { "2x2", 2, 2 }, { "3x1", 3, 1 }, { "3x2", 3, 2 }, { "3x3", 3, 3 },
                                      { "4x3", 4, 3 }, { "4x4", 4, 4 }, { "5x5", 5, 5 }, { "6x6", 6, 6 } };
constexpr int kNumGrids = (int) (sizeof (kGrids) / sizeof (kGrids[0]));
constexpr int kMaxTiles = 36, kNumSlots = 36, kMaxImages = 8;
inline constexpr const char* kSlotKeys = "1234567890QWERTYUIOPASDFGHJKLZXCVBNM";
inline constexpr int kQualityLines[] = { 270, 360, 540, 720 };
inline constexpr int kFpsValues[] = { 24, 30, 60 };
inline constexpr float kCycleBeats[] = { 0, 1, 2, 4, 8, 16 };        // text / image cycle: Off|Beat|2 Beats|Bar|2 Bars|4 Bars
inline constexpr float kStrobeBeats[] = { 1.0f, 0.5f, 0.25f, 0.125f }; // 1/4 1/8 1/16 1/32

inline const PDef kParams[kNumParams] = {
    { "mode",      "Mode",            'c', 0, 0, 1, 1, 32, "", VZ_MODE_CHOICES },
    { "palette",   "Palette",         'c', 0, 0, 1, 1, 0, "", VZ_PALETTE_CHOICES },
    { "speed",     "Speed",           'f', 0.0f, 4.0f, 0.001f, 0.5f, 1.0f, "x" },
    { "zoom",      "Zoom",            'f', 0.25f, 4.0f, 0.001f, 0.5f, 1.0f, "x" },
    { "detail",    "Detail",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "spin",      "Spin",            'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "hue",       "Hue Shift",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "cycle",     "Color Cycle",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.1f, "%" },
    { "bright",    "Brightness",      'f', 0.0f, 2.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "trails",    "Trails",          'f', 0.0f, 0.98f, 0.001f, 1.0f, 0.0f, "%" },
    { "glow",      "Glow",            'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.35f, "%" },
    { "flash",     "Beat Flash",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "mirror",    "Mirror",          'c', 0, 0, 1, 1, 0, "", "Off|Left-Right|Top-Bottom|Quad" },
    { "react",     "Reactivity",      'f', 0.0f, 2.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "gain",      "Input Gain",      'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "smooth",    "Smoothing",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.4f, "%" },
    { "sync",      "Tempo Sync",      'b', 0, 1, 1, 1, 1.0f, "" },
    { "beatMode",  "Beat Source",     'c', 0, 0, 1, 1, 0, "", "Auto|Onsets|Tempo" },
    { "imgOn",     "Image",           'b', 0, 1, 1, 1, 1.0f, "" },
    { "imgBlend",  "Image Blend",     'c', 0, 0, 1, 1, 0, "", "Over|Behind|Mask|Texture|Multiply|Screen" },
    { "imgSize",   "Image Size",      'f', 0.1f, 2.0f, 0.001f, 1.0f, 0.65f, "%" },
    { "imgOpacity","Image Opacity",   'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "imgSpin",   "Image Spin",      'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "imgCycle",  "Image Cycle",     'c', 0, 0, 1, 1, 0, "", "Off|Beat|2 Beats|Bar|2 Bars|4 Bars" },
    { "imgPulse",  "Image Pulse",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "imgGlow",   "Image Glow",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "imgShake",  "Image Shake",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.1f, "%" },
    { "imgChroma", "Image Chroma",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "textOver",  "Text Overlay",    'b', 0, 1, 1, 1, 0.0f, "" },
    { "textAnim",  "Text Animation",  'c', 0, 0, 1, 1, 0, "", "Pulse|Glitch|Typewriter|Flash|Wave|Zoom|Shake" },
    { "textSize",  "Text Size",       'f', 0.2f, 2.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "textCycle", "Text Cycle",      'c', 0, 0, 1, 1, 3, "", "All Lines|Beat|2 Beats|Bar|2 Bars|4 Bars" },
    { "textGlitch","Text Glitch",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "texOn",     "Texture",         'b', 0, 1, 1, 1, 0.0f, "" },
    { "texType",   "Texture Type",    'c', 0, 0, 1, 1, 1, "", "Noise|Grain|CRT|Threshold|VHS" },
    { "texAmt",    "Texture Amount",  'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "ditOn",     "Dither",          'b', 0, 1, 1, 1, 0.0f, "" },
    { "ditType",   "Dither Type",     'c', 0, 0, 1, 1, 0, "", "Symbol|Star|Hex|Dots|Bayer" },
    { "ditSize",   "Dither Size",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.4f, "%" },
    { "ditMix",    "Dither Mix",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "trkOn",     "Track",           'b', 0, 1, 1, 1, 0.0f, "" },
    { "trkStyle",  "Track Style",     'c', 0, 0, 1, 1, 0, "", "Boxes|Brackets|Crosshair|Network" },
    { "trkAmt",    "Track Amount",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "glsOn",     "Glass",           'b', 0, 1, 1, 1, 0.0f, "" },
    { "glsType",   "Glass Type",      'c', 0, 0, 1, 1, 0, "", "Lens|Kaleido|Ripple|Prism|Shards" },
    { "glsAmt",    "Glass Amount",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "grid",      "Grid",            'c', 0, 0, 1, 1, 0, "", "Off|2x1|1x2|2x2|3x1|3x2|3x3|4x3|4x4|5x5|6x6" },
    { "gridLink",  "Grid Link",       'c', 0, 0, 1, 1, 0, "", "Free|Same|Mirror|Cascade|Bands|Beat Swap" },
    { "gridGap",   "Grid Gap",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "strobe",    "Strobe",          'b', 0, 1, 1, 1, 0.0f, "" },
    { "strobeRate","Strobe Rate",     'c', 0, 0, 1, 1, 1, "", "1/4|1/8|1/16|1/32" },
    { "blackout",  "Blackout",        'b', 0, 1, 1, 1, 0.0f, "" },
    { "quality",   "Quality",         'c', 0, 0, 1, 1, 1, "", "Low|Medium|High|Ultra" },
    { "fps",       "Frame Rate",      'c', 0, 0, 1, 1, 1, "", "24 fps|30 fps|60 fps" },
};
} // namespace vz
