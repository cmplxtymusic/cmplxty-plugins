#pragma once
// CMPLXTY Visualizer - software renderer. Runs on its own render thread (never the audio thread), draws into
// juce::SoftwareImageType images at a capped resolution / frame rate, and the editor scales the result to the view.
#include <juce_graphics/juce_graphics.h>
#include "Analysis.h"
#include "Params.h"
#include <memory>
#include <vector>

namespace vz
{
// Everything the renderer needs besides parameters and audio features. Built on the message thread, then handed
// to the render thread as an immutable shared_ptr.
struct Assets
{
    std::vector<juce::Image> images;           // decoded, ARGB, software images (<= 1920 px)
    int selectedImage = 0;
    juce::StringArray lines;                   // custom text (lyrics / words)
    juce::Typeface::Ptr textFont;              // user font (.ttf/.otf) or null = bundled
    juce::Typeface::Ptr uiBold;                // bundled font (glyph rain, labels)
    std::array<ParamValues, kMaxTiles> tiles {};   // per-tile presets for the multi-grid (Free / Beat Swap)
    std::array<bool, kMaxTiles> tileValid {};
};

inline juce::Image makeSoftwareImage (int w, int h, bool clear = true)
{
    return juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), clear, juce::SoftwareImageType());
}

// 256-entry palette lookup with hue shift. ARGB packed (0xAARRGGBB, opaque).
void buildPaletteLut (int palette, float hueShift, uint32_t* lut256);

class Scene;

class Renderer
{
public:
    Renderer();
    ~Renderer();
    // Renders one frame of `pv` into `out` (any size; ARGB software image). dt = seconds since the last frame.
    void render (juce::Image& out, const ParamValues& pv, const ana::Snapshot& av, const Assets& as, double dt);
    void reset();   // forget per-mode state (new song / preset)
private:
    std::unique_ptr<Scene> main;
    std::vector<std::unique_ptr<Scene>> tiles;
    std::vector<juce::Image> tileImgs;
    float blackoutAmt = 0, strobeClock = 0;
    double lastBeatPos = 0;
};
} // namespace vz
