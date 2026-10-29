// CMPLXTY Visualizer - software renderer: 45 modes, image layer, FX stack, multi-grid, strobe/blackout.
#include <thread>
#include "Visual.h"
#include <cmath>
#include <algorithm>

namespace vz
{
namespace
{
using u32 = uint32_t;
constexpr float kTau = 6.28318530718f, kPiF = 3.14159265359f;

inline float fract (float x) noexcept { return x - std::floor (x); }
inline float clamp01 (float x) noexcept { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float smooth01 (float e0, float e1, float x) noexcept { float t = clamp01 ((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
inline float tri (float x) noexcept { float f = fract (x * 0.5f) * 2.0f; return f < 1 ? f : 2 - f; }

struct SinLut { float t[4096]; SinLut() { for (int i = 0; i < 4096; ++i) t[i] = std::sin ((float) i * kTau / 4096.0f); } };
const SinLut& sinLut() { static const SinLut s; return s; }
inline float fsin (float x) noexcept { return sinLut().t[(int) std::floor (x * (4096.0f / kTau)) & 4095]; }
inline float fcos (float x) noexcept { return fsin (x + 1.5707963f); }
inline float fatan2 (float y, float x) noexcept
{
    const float ax = std::abs (x), ay = std::abs (y);
    const float mx = std::max (ax, ay), mn = std::min (ax, ay);
    if (mx < 1.0e-9f) return 0;
    const float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = kPiF - r;
    return y < 0 ? -r : r;
}
inline u32 hash32 (u32 x) noexcept { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float hashf (int x, int y = 0) noexcept { return (float) (hash32 ((u32) x * 0x9E3779B1U ^ hash32 ((u32) y + 0x632BE5ABU)) & 0xffffff) / 16777216.0f; }
inline float vnoise (float x, float y) noexcept
{
    const float fx = std::floor (x), fy = std::floor (y);
    const int ix = (int) fx, iy = (int) fy;
    float ux = x - fx, uy = y - fy;
    ux = ux * ux * (3 - 2 * ux); uy = uy * uy * (3 - 2 * uy);
    const float a = hashf (ix, iy), b = hashf (ix + 1, iy), c = hashf (ix, iy + 1), d = hashf (ix + 1, iy + 1);
    return lerpf (lerpf (a, b, ux), lerpf (c, d, ux), uy);
}
inline float fbm (float x, float y, int oct) noexcept
{
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; ++i) { s += a * vnoise (x, y); n += a; x = x * 2.03f + 17.1f; y = y * 2.01f - 9.3f; a *= 0.5f; }
    return s / n;
}
struct Rng { u32 s = 0x12345678; u32 next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } float f() noexcept { return (float) (next() & 0xffffff) / 16777216.0f; } };

inline int cr (u32 c) noexcept { return (int) ((c >> 16) & 255); }
inline int cg (u32 c) noexcept { return (int) ((c >> 8) & 255); }
inline int cb (u32 c) noexcept { return (int) (c & 255); }
inline u32 pack (int r, int g, int b) noexcept
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return 0xff000000u | ((u32) r << 16) | ((u32) g << 8) | (u32) b;
}
inline u32 mulc (u32 c, float v) noexcept
{
    const int s = (int) (v * 256.0f);
    if (s <= 0) return 0xff000000u;
    return pack ((cr (c) * s) >> 8, (cg (c) * s) >> 8, (cb (c) * s) >> 8);
}
inline u32 addc (u32 a, u32 b) noexcept { return pack (cr (a) + cr (b), cg (a) + cg (b), cb (a) + cb (b)); }
inline u32 maxc (u32 a, u32 b) noexcept { return 0xff000000u | (std::max (a & 0xff0000u, b & 0xff0000u)) | (std::max (a & 0xff00u, b & 0xff00u)) | (std::max (a & 0xffu, b & 0xffu)); }
inline u32 lerpc (u32 a, u32 b, float t) noexcept
{
    const int s = (int) (clamp01 (t) * 256.0f), q = 256 - s;
    return pack ((cr (a) * q + cr (b) * s) >> 8, (cg (a) * q + cg (b) * s) >> 8, (cb (a) * q + cb (b) * s) >> 8);
}
inline float luma (u32 c) noexcept { return (0.299f * (float) cr (c) + 0.587f * (float) cg (c) + 0.114f * (float) cb (c)) * (1.0f / 255.0f); }
inline u32 screenc (u32 a, u32 b) noexcept { return pack (255 - ((255 - cr (a)) * (255 - cr (b)) >> 8), 255 - ((255 - cg (a)) * (255 - cg (b)) >> 8), 255 - ((255 - cb (a)) * (255 - cb (b)) >> 8)); }
inline u32 multc (u32 a, u32 b) noexcept { return pack (cr (a) * cr (b) >> 8, cg (a) * cg (b) >> 8, cb (a) * cb (b) >> 8); }

// Direct pixel access to a software ARGB image.
struct Pix
{
    juce::Image::BitmapData bd;
    int w, h;
    explicit Pix (juce::Image& im, juce::Image::BitmapData::ReadWriteMode m = juce::Image::BitmapData::readWrite)
        : bd (im, m), w (im.getWidth()), h (im.getHeight()) {}
    u32* row (int y) noexcept { return reinterpret_cast<u32*> (bd.getLinePointer (y)); }
    u32 at (int x, int y) noexcept { x = x < 0 ? 0 : (x >= w ? w - 1 : x); y = y < 0 ? 0 : (y >= h ? h - 1 : y); return row (y)[x]; }
};

// Premultiplied ARGB (alpha may be < 255 in transparent work images): unpremultiplied over-blend into opaque dst.
inline u32 overc (u32 dst, u32 src, float opacity) noexcept
{
    const int a = (int) ((float) (src >> 24) * opacity);
    if (a <= 0) return dst;
    const int q = 255 - a;
    const float o = opacity;
    return pack ((int) ((float) cr (src) * o) + (cr (dst) * q) / 255, (int) ((float) cg (src) * o) + (cg (dst) * q) / 255, (int) ((float) cb (src) * o) + (cb (dst) * q) / 255);
}

void copyImage (juce::Image& dst, juce::Image& src)
{
    Pix s (src, juce::Image::BitmapData::readOnly), d (dst, juce::Image::BitmapData::writeOnly);
    const int w = std::min (s.w, d.w), h = std::min (s.h, d.h);
    for (int y = 0; y < h; ++y) std::memcpy (d.row (y), s.row (y), (size_t) w * 4);
}

void blur3 (std::vector<u32>& buf, std::vector<u32>& tmp, int w, int h, int r)
{
    // separable box blur on packed RGB (opaque); r = radius
    tmp.resize (buf.size());
    const int n = 2 * r + 1;
    for (int y = 0; y < h; ++y)
    {
        const u32* s = buf.data() + (size_t) y * (size_t) w; u32* d = tmp.data() + (size_t) y * (size_t) w;
        int sr = 0, sg = 0, sb = 0;
        for (int k = -r; k <= r; ++k) { u32 c = s[std::clamp (k, 0, w - 1)]; sr += cr (c); sg += cg (c); sb += cb (c); }
        for (int x = 0; x < w; ++x)
        {
            d[x] = pack (sr / n, sg / n, sb / n);
            u32 a = s[std::min (x + r + 1, w - 1)], b = s[std::max (x - r, 0)];
            sr += cr (a) - cr (b); sg += cg (a) - cg (b); sb += cb (a) - cb (b);
        }
    }
    for (int x = 0; x < w; ++x)
    {
        int sr = 0, sg = 0, sb = 0;
        for (int k = -r; k <= r; ++k) { u32 c = tmp[(size_t) std::clamp (k, 0, h - 1) * (size_t) w + (size_t) x]; sr += cr (c); sg += cg (c); sb += cb (c); }
        for (int y = 0; y < h; ++y)
        {
            buf[(size_t) y * (size_t) w + (size_t) x] = pack (sr / n, sg / n, sb / n);
            u32 a = tmp[(size_t) std::min (y + r + 1, h - 1) * (size_t) w + (size_t) x], b = tmp[(size_t) std::max (y - r, 0) * (size_t) w + (size_t) x];
            sr += cr (a) - cr (b); sg += cg (a) - cg (b); sb += cb (a) - cb (b);
        }
    }
}

const juce::StringArray& defaultLines()
{
    static const juce::StringArray l { "CMPLXTY", "VISUALIZER", "TYPE YOUR LYRICS", "IN THE TEXT TAB" };
    return l;
}
} // namespace

void buildPaletteLut (int palette, float hueShift, uint32_t* lut)
{
    const auto& p = kPalettes[std::clamp (palette, 0, kNumPalettes - 1)];
    for (int i = 0; i < 256; ++i)
    {
        const float t = (float) i / 255.0f * (float) (p.n - 1);
        const int k = std::min ((int) t, p.n - 2);
        const float f = t - (float) k;
        u32 c = lerpc (p.stops[k], p.stops[k + 1], f);
        if (hueShift > 0.0005f && ! p.mono)
        {
            auto col = juce::Colour (c);
            col = col.withRotatedHue (hueShift);
            c = col.getARGB() | 0xff000000u;
        }
        lut[i] = c;
    }
}

//==============================================================================================================
struct Ctx
{
    const ParamValues& pv;
    const ana::Snapshot& a;
    const Assets& as;
    int W, H;
    float t, dt, speed, detail, zoom, spin, react;
    float level, bass, mid, high, beat;
    float colorOff;
    const u32* lut;
    float scopeGain;
    const juce::Image* img;     // current image or placeholder
    bool hasImage;
    double beatPos;
    int beatCount;
    bool onset;
    float spec (float x) const noexcept   // x in 0..1 over the 64 bands, interpolated
    {
        const float f = clamp01 (x) * 63.0f; const int i = std::min ((int) f, 62);
        return lerpf (a.spec[(size_t) i], a.spec[(size_t) i + 1], f - (float) i);
    }
    u32 pal (float x) const noexcept { return lut[(int) (tri (x + colorOff) * 255.0f)]; }
    juce::Colour palC (float x, float alpha = 1.0f) const noexcept { return juce::Colour (pal (x)).withAlpha (alpha); }
};

struct Star { float x, y, z, px, py; bool valid; };
struct Particle { float x, y, vx, vy, life, t; };
struct Track { float x, y, w, h; int id; int age; bool live; };

class Scene
{
public:
    void render (juce::Image& dst, const ParamValues& pv, const ana::Snapshot& av, const Assets& as, double dt, float timeOffset, float hueOffset);

private:
    int W = 0, H = 0;
    juce::Image layer, accum, work, work2;
    std::vector<u32> glowBuf, glowTmp;
    double t = 0, colorPhase = 0;
    float scopeGain = 1.0f, spinAngle = 0;
    juce::Image noImage;
    u32 lut[256] {};
    int lastMode = -1, lastQualityW = 0;
    Rng rng;
    // per-mode state
    std::vector<Star> stars;
    std::vector<Particle> parts;
    std::vector<float> rainHead, rainSpeed, rainLen, rowOff;
    std::vector<uint8_t> cellGlyph, fire, life, life2, lifeAge;
    std::vector<float> blocks, blockT, peaks, angles;
    std::vector<std::array<float, 64>> ridge;
    float ridgeTimer = 0, lifeTimer = 0, washT = 0.5f, washTarget = 0.5f, rollPos = 0;
    int lifeW = 0, lifeH = 0, fireW = 0, fireH = 0;
    juce::Image waterfall, placeholder;
    std::vector<Track> tracks;
    int nextTrackId = 1;
    // glyph atlas
    struct Atlas { int size = 0; juce::Typeface* face = nullptr; std::vector<juce::Image> glyphs; juce::String chars; };
    Atlas atlas, ditherAtlas;

    void ensureSize (int w, int h);
    void modeChanged (int m);
    void clearLayer (u32 c = 0xff000000u);
    template <class F> void shade (const Ctx& c, F&& f);
    template <class F> void shadeHalf (const Ctx& c, F&& f);
    void vecTransform (juce::Graphics& g, const Ctx& c);
    const Atlas& glyphAtlas (Atlas& a, int size, juce::Typeface::Ptr face, const juce::String& chars);
    juce::Font textFont (const Ctx& c, float h) const;
    juce::Font uiFont (const Ctx& c, float h) const;
    const juce::Image& getPlaceholder (const Ctx& c);
    void coverDraw (juce::Graphics& g, const juce::Image& im, juce::Rectangle<float> r, float scale, float rot, float dx, float dy, float alpha);

    // modes
    void drawMode (Ctx& c, int m);
    void mGlowScope (Ctx&); void mXYScope (Ctx&); void mRingScope (Ctx&); void mShapeScope (Ctx&);
    void mLava (Ctx&, bool contours); void mBlob (Ctx&);
    void mSynthGrid (Ctx&); void mTunnel (Ctx&); void mRidge (Ctx&); void mCopper (Ctx&);
    void mStarfield (Ctx&); void mHyperspace (Ctx&); void mNebula (Ctx&); void mOrbits (Ctx&);
    void mCodeRain (Ctx&); void mGlyphStorm (Ctx&); void mBinary (Ctx&);
    void mWash (Ctx&); void mSweep (Ctx&); void mBlocks (Ctx&);
    void mStatic (Ctx&); void mRoll (Ctx&); void mSignalLost (Ctx&);
    void mBits (Ctx&); void mFire (Ctx&); void mLife (Ctx&); void mMosaic (Ctx&);
    void mLyric (Ctx&); void mGlitchType (Ctx&); void mMarquee (Ctx&); void mWordWall (Ctx&);
    void mPlasma (Ctx&); void mLiquid (Ctx&); void mInterference (Ctx&); void mSwirl (Ctx&);
    void mBars (Ctx&, bool mirror); void mCircle (Ctx&); void mBurst (Ctx&); void mWaterfall (Ctx&);
    void mImgPulse (Ctx&); void mImgTiles (Ctx&); void mImgSlices (Ctx&); void mImgPixelate (Ctx&);

    // layers & fx
    juce::String currentText (const Ctx& c, float& progress, int& lineIndex) const;
    void drawTextAnim (juce::Graphics& g, const Ctx& c, const juce::String& s, juce::Rectangle<float> area, int anim, float progress, float sizeMul, float glitch);
    void textGlitchPass (juce::Image& im, const Ctx& c, float amount);
    void imageLayer (Ctx& c);
    void chromaComposite (juce::Image& src, juce::Image& dst, int dx, float opacity, int blend, const Ctx& c);
    void bloom (juce::Image& im, float amt);
    void mirrorPass (juce::Image& im, int mode);
    void fxGlass (juce::Image& im, const Ctx& c, int type, float amt);
    void fxTexture (juce::Image& im, const Ctx& c, int type, float amt);
    void fxDither (juce::Image& im, const Ctx& c, int type, float size, float mix);
    void fxTrack (juce::Image& im, const Ctx& c, int style, float amt);
};

//==============================================================================================================
struct TB { float t, b; };

void Scene::ensureSize (int w, int h)
{
    if (w == W && h == H && layer.isValid()) return;
    W = w; H = h;
    layer = makeSoftwareImage (W, H); accum = makeSoftwareImage (W, H);
    work = makeSoftwareImage (W, H); work2 = makeSoftwareImage (W, H);
    clearLayer();
    waterfall = juce::Image();
    lastMode = -1;   // re-init size dependent state
}

void Scene::modeChanged (int m)
{
    lastMode = m;
    stars.clear(); parts.clear(); rainHead.clear(); cellGlyph.clear(); fire.clear(); life.clear(); blocks.clear();
    peaks.clear(); angles.clear(); ridge.clear(); rowOff.clear(); waterfall = juce::Image(); tracks.clear();
}

void Scene::clearLayer (u32 c)
{
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < p.h; ++y) { auto* r = p.row (y); std::fill (r, r + p.w, c); }
}

// Splits rows [0, n) across up to 4 threads (the shaders below only read shared state). Small frames stay on this thread.
template <class Fn> static void parallelRows (int n, int pixelsPerRow, Fn&& fn)
{
    static const int cores = (int) std::max (1u, std::thread::hardware_concurrency());
    const int jobs = std::clamp (std::min (cores - 1, 4), 1, std::max (1, n / 16));
    if (jobs <= 1 || (long long) n * pixelsPerRow < 60000) { fn (0, n); return; }
    std::vector<std::thread> th; th.reserve ((size_t) jobs - 1);
    const int per = (n + jobs - 1) / jobs;
    for (int j = 1; j < jobs; ++j) { const int a = j * per, b = std::min (n, a + per); if (a < b) th.emplace_back ([&fn, a, b] { fn (a, b); }); }
    fn (0, std::min (n, per));
    for (auto& t : th) t.join();
}

template <class F> void Scene::shade (const Ctx& c, F&& f)
{
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    const float inv = 2.0f / ((float) H * c.zoom);
    const float cs = std::cos (c.spin), sn = std::sin (c.spin);
    parallelRows (H, W, [&] (int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
        {
            u32* row = p.row (y);
            const float v0 = ((float) y - (float) H * 0.5f) * inv;
            for (int x = 0; x < W; ++x)
            {
                const float u0 = ((float) x - (float) W * 0.5f) * inv;
                const TB r = f (u0 * cs - v0 * sn, u0 * sn + v0 * cs);
                row[x] = mulc (c.pal (r.t), r.b);
            }
        }
    });
}

template <class F> void Scene::shadeHalf (const Ctx& c, F&& f)
{
    const int hw = W / 2 + 2, hh = H / 2 + 2;
    glowBuf.resize ((size_t) hw * (size_t) hh);
    const float inv = 2.0f / ((float) H * c.zoom);
    const float cs = std::cos (c.spin), sn = std::sin (c.spin);
    parallelRows (hh, hw, [&] (int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
        {
            const float v0 = ((float) (y * 2) - (float) H * 0.5f) * inv;
            for (int x = 0; x < hw; ++x)
            {
                const float u0 = ((float) (x * 2) - (float) W * 0.5f) * inv;
                const TB r = f (u0 * cs - v0 * sn, u0 * sn + v0 * cs);
                glowBuf[(size_t) y * (size_t) hw + (size_t) x] = mulc (c.pal (r.t), r.b);
            }
        }
    });
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; ++y)
    {
        u32* row = p.row (y);
        const int sy = y >> 1; const bool oy = (y & 1) != 0;
        const u32* r0 = glowBuf.data() + (size_t) sy * (size_t) hw;
        const u32* r1 = r0 + hw;
        for (int x = 0; x < W; ++x)
        {
            const int sx = x >> 1;
            u32 a = r0[sx];
            if (x & 1) a = lerpc (a, r0[sx + 1], 0.5f);
            if (oy)
            {
                u32 b = r1[sx];
                if (x & 1) b = lerpc (b, r1[sx + 1], 0.5f);
                a = lerpc (a, b, 0.5f);
            }
            row[x] = a;
        }
    }
}

void Scene::vecTransform (juce::Graphics& g, const Ctx& c)
{
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    g.addTransform (juce::AffineTransform::rotation (c.spin, cx, cy).followedBy (juce::AffineTransform::scale (c.zoom, c.zoom, cx, cy)));
}

juce::Font Scene::uiFont (const Ctx& c, float h) const
{
    if (c.as.uiBold != nullptr) return juce::Font (juce::FontOptions (c.as.uiBold).withHeight (h));
    return juce::Font (juce::FontOptions (h, juce::Font::bold));
}

juce::Font Scene::textFont (const Ctx& c, float h) const
{
    if (c.as.textFont != nullptr) return juce::Font (juce::FontOptions (c.as.textFont).withHeight (h));
    return uiFont (c, h);
}

const Scene::Atlas& Scene::glyphAtlas (Atlas& a, int size, juce::Typeface::Ptr face, const juce::String& chars)
{
    size = std::max (4, size);
    if (a.size == size && a.face == face.get() && a.chars == chars && ! a.glyphs.empty()) return a;
    a.size = size; a.face = face.get(); a.chars = chars; a.glyphs.clear();
    const juce::Font f = face != nullptr ? juce::Font (juce::FontOptions (face).withHeight ((float) size * 0.95f))
                                         : juce::Font (juce::FontOptions ((float) size * 0.95f, juce::Font::bold));
    for (int i = 0; i < chars.length(); ++i)
    {
        juce::Image im (juce::Image::SingleChannel, size, size, true, juce::SoftwareImageType());
        juce::Graphics g (im);
        g.setColour (juce::Colours::white);
        g.setFont (f);
        g.drawText (juce::String::charToString (chars[i]), 0, 0, size, size, juce::Justification::centred, false);
        a.glyphs.push_back (im);
    }
    return a;
}

const juce::Image& Scene::getPlaceholder (const Ctx& c)
{
    if (placeholder.isValid()) return placeholder;
    placeholder = makeSoftwareImage (640, 640);
    juce::Graphics g (placeholder);
    g.fillAll (juce::Colour (0xff0a0a0a));
    for (int i = 9; i > 0; --i)
    {
        g.setColour (juce::Colour::greyLevel (i % 2 ? 0.85f : 0.12f));
        const float r = (float) i * 34.0f;
        g.fillEllipse (320 - r, 320 - r, r * 2, r * 2);
    }
    g.setColour (juce::Colours::white);
    g.drawLine (0, 320, 640, 320, 2); g.drawLine (320, 0, 320, 640, 2);
    g.setColour (juce::Colour (0xff0a0a0a));
    g.fillRect (90, 285, 460, 70);
    g.setColour (juce::Colours::white);
    g.setFont (uiFont (c, 44.0f));
    g.drawText ("DROP A JPG / PNG", 90, 285, 460, 70, juce::Justification::centred);
    return placeholder;
}

void Scene::coverDraw (juce::Graphics& g, const juce::Image& im, juce::Rectangle<float> r, float scale, float rot, float dx, float dy, float alpha)
{
    if (! im.isValid()) return;
    const float iw = (float) im.getWidth(), ih = (float) im.getHeight();
    const float s = std::max (r.getWidth() / iw, r.getHeight() / ih) * scale;
    g.setOpacity (clamp01 (alpha));
    g.drawImageTransformed (im, juce::AffineTransform::translation (-iw * 0.5f, -ih * 0.5f).scaled (s).rotated (rot)
                                    .translated (r.getCentreX() + dx, r.getCentreY() + dy));
    g.setOpacity (1.0f);
}

//==============================================================================================================
// SCOPE
static int triggerIndex (const std::array<float, ana::Snapshot::kWave>& w, int span)
{
    const int maxStart = (int) w.size() - span - 1;
    for (int i = 1; i < maxStart; ++i)
        if (w[(size_t) i - 1] < 0.0f && w[(size_t) i] >= 0.0f) return i;
    return std::max (0, maxStart);
}

static void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour col, float thick)
{
    g.setColour (col.withMultipliedAlpha (0.18f)); g.strokePath (p, juce::PathStrokeType (thick * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withMultipliedAlpha (0.45f)); g.strokePath (p, juce::PathStrokeType (thick * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col); g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.55f * col.getFloatAlpha())); g.strokePath (p, juce::PathStrokeType (std::max (1.0f, thick * 0.35f)));
}

void Scene::mGlowScope (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const int N = 512, start = triggerIndex (c.a.waveL, N);
    const float amp = (float) H * 0.36f * c.scopeGain, mid = (float) H * 0.5f;
    const int lines = 1 + (int) (c.detail * 2.99f);
    for (int l = lines - 1; l >= 0; --l)
    {
        juce::Path p;
        const float off = (float) l * (float) H * 0.11f, sc = 1.0f - 0.25f * (float) l;
        for (int i = 0; i < N; ++i)
        {
            const size_t k = (size_t) (start + i);
            const float s = 0.5f * (c.a.waveL[k] + c.a.waveR[k]);
            const float x = (float) i * (float) (W - 1) / (float) (N - 1);
            const float y = mid - s * amp * sc + (l % 2 ? off : -off) * (l > 0 ? 1.0f : 0.0f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const float th = (1.6f + c.level * 3.5f + c.beat * 2.0f) * (float) H / 360.0f * sc;
        strokeGlow (g, p, c.palC (0.85f - 0.25f * (float) l, l == 0 ? 1.0f : 0.6f), th);
    }
    // floor line
    g.setColour (c.palC (0.35f, 0.25f));
    g.fillRect (0.0f, mid, (float) W, 1.0f);
}

void Scene::mXYScope (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float amp = (float) H * 0.42f * std::min (c.scopeGain, 6.0f);
    juce::Path p;
    for (int i = 0; i < ana::Snapshot::kWave; ++i)
    {
        const float l = c.a.waveL[(size_t) i], r = c.a.waveR[(size_t) i];
        float x = (float) W * 0.5f + (l - r) * 0.7071f * amp, y = (float) H * 0.5f - (l + r) * 0.7071f * amp;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    strokeGlow (g, p, c.palC (0.9f), (1.2f + c.level * 2.0f) * (float) H / 360.0f);
    if (c.detail > 0.4f)
    {
        g.setColour (c.palC (0.4f, 0.25f));
        const float r = (float) H * 0.42f;
        g.drawEllipse ((float) W * 0.5f - r, (float) H * 0.5f - r, r * 2, r * 2, 1.0f);
        g.drawLine ((float) W * 0.5f - r, (float) H * 0.5f, (float) W * 0.5f + r, (float) H * 0.5f, 1.0f);
        g.drawLine ((float) W * 0.5f, (float) H * 0.5f - r, (float) W * 0.5f, (float) H * 0.5f + r, 1.0f);
    }
}

void Scene::mRingScope (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    const float r0 = (float) H * 0.24f * (1.0f + 0.22f * c.bass + 0.12f * c.beat);
    const int N = 512;
    const int rings = 1 + (int) (c.detail * 2.99f);
    for (int ring = 0; ring < rings; ++ring)
    {
        const auto& w = ring % 2 ? c.a.waveR : c.a.waveL;
        const float rr = r0 * (1.0f + 0.35f * (float) ring), rot = (float) c.t * 0.3f * (ring % 2 ? -1.0f : 1.0f);
        juce::Path p;
        for (int i = 0; i <= N; ++i)
        {
            const int k = i % N;
            const float s = w[(size_t) (k * 2)] * 0.5f + w[(size_t) (((N - k) % N) * 2)] * 0.5f; // symmetric close
            const float a = (float) k / (float) N * kTau + rot;
            const float r = rr + s * (float) H * 0.16f * c.scopeGain;
            const float x = cx + std::cos (a) * r, y = cy + std::sin (a) * r;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        p.closeSubPath();
        if (ring == 0)
        {
            g.setGradientFill (juce::ColourGradient (c.palC (0.6f, 0.35f * (0.4f + c.level)), cx, cy, c.palC (0.1f, 0.0f), cx + rr, cy, true));
            g.fillPath (p);
        }
        strokeGlow (g, p, c.palC (0.95f - 0.2f * (float) ring), (1.5f + 3.0f * c.level) * (float) H / 360.0f);
    }
}

void Scene::mShapeScope (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const int sides = 3 + (int) (c.detail * 5.99f);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    const float base = (float) H * 0.34f * (1.0f + 0.12f * c.beat + 0.1f * c.bass);
    for (int k = 0; k < 4; ++k)
    {
        const float sc = 1.0f - 0.23f * (float) k;
        const float rot = (float) c.t * 0.35f * (k % 2 ? -1.0f : 1.0f) + c.beat * 0.15f;
        juce::Path p;
        const int N = 360;
        for (int j = 0; j <= N; ++j)
        {
            const float pp = (float) (j % N) / (float) N * (float) sides;
            const int e = (int) pp; const float f = pp - (float) e;
            const float a0 = rot + (float) e / (float) sides * kTau, a1 = rot + (float) (e + 1) / (float) sides * kTau;
            float x = lerpf (std::cos (a0), std::cos (a1), f), y = lerpf (std::sin (a0), std::sin (a1), f);
            const float s = c.a.waveL[(size_t) ((j * 2 + k * 97) % ana::Snapshot::kWave)];
            const float r = base * sc * (1.0f + s * 0.35f * c.scopeGain);
            x = cx + x * r; y = cy + y * r;
            if (j == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        p.closeSubPath();
        strokeGlow (g, p, c.palC (0.95f - 0.18f * (float) k, 1.0f - 0.18f * (float) k), (1.4f + 2.5f * c.level) * (float) H / 360.0f);
    }
}

//==============================================================================================================
// LAVA
namespace { struct Ball { float x, y, r2; }; }
static int lavaBalls (const Ctx& c, Ball* b)
{
    const int n = 5 + (int) (c.detail * 7.0f);
    const float aspect = (float) c.W / (float) c.H;
    for (int i = 0; i < n; ++i)
    {
        const float h1 = hashf (i, 1), h2 = hashf (i, 2), h3 = hashf (i, 3);
        const float tt = (float) c.t;
        b[i].x = (0.25f + 0.6f * h1) * aspect * std::sin (tt * (0.11f + 0.08f * h2) + h3 * 6.0f);
        b[i].y = 0.78f * std::sin (tt * (0.07f + 0.09f * h3) + h1 * 6.0f);
        const float r = 0.13f + 0.09f * h2 + 0.09f * c.bass + 0.04f * c.beat;
        b[i].r2 = r * r;
    }
    return n;
}

void Scene::mLava (Ctx& c, bool contours)
{
    Ball b[16];
    const int n = lavaBalls (c, b);
    const float lv = c.level;
    shadeHalf (c, [&] (float u, float v) -> TB
    {
        float f = 0;
        for (int i = 0; i < n; ++i) { const float dx = u - b[i].x, dy = v - b[i].y; f += b[i].r2 / (dx * dx + dy * dy + 1.0e-4f); }
        if (contours)
        {
            const float ff = f * (2.0f + c.detail * 5.0f) - (float) c.t * 0.6f;
            const float fr = ff - std::floor (ff);
            const float line = std::pow (1.0f - std::abs (2.0f * fr - 1.0f), 5.0f);
            return { 0.3f + 0.55f * clamp01 (f * 0.5f), clamp01 (line * std::min (1.0f, f) + 0.12f * std::min (1.0f, f)) * (0.7f + 0.5f * lv) };
        }
        const float in = smooth01 (0.82f, 1.06f, f);
        const float bgT = 0.08f + 0.12f * (v + 1.0f) * 0.5f;
        const float blobT = 0.55f + 0.3f * clamp01 ((f - 1.0f) * 0.4f) - 0.1f * v;
        const float rim = smooth01 (0.55f, 0.95f, f) * (1.0f - in);
        return { lerpf (bgT, blobT, in) + rim * 0.3f, lerpf (0.25f + 0.25f * rim, 0.85f + 0.25f * lv, in) + rim * 0.5f };
    });
}

void Scene::mBlob (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    const int N = 160;
    float rs[N];
    for (int i = 0; i < N; ++i)
    {
        const float th = (float) i / (float) N * kTau;
        const float m = std::abs (fract ((float) i / (float) N + 0.25f) * 2.0f - 1.0f);   // mirrored band position
        float r = 1.0f + 0.07f * std::sin (3 * th + (float) c.t * 0.9f) + 0.05f * std::sin (5 * th - (float) c.t * 1.3f) + 0.04f * std::sin (8 * th + (float) c.t * 2.1f) * c.detail;
        r += 0.55f * c.spec (m * 0.7f);
        rs[i] = r;
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        float tmp[N];
        for (int i = 0; i < N; ++i) tmp[i] = (rs[(i + N - 1) % N] + 2 * rs[i] + rs[(i + 1) % N]) * 0.25f;
        std::copy (tmp, tmp + N, rs);
    }
    const float R = (float) H * 0.2f * (1.0f + 0.3f * c.bass + 0.15f * c.beat);
    for (int layerN = 2; layerN >= 0; --layerN)
    {
        const float sc = 1.0f + 0.22f * (float) layerN;
        juce::Path p;
        for (int i = 0; i <= N; ++i)
        {
            const float th = (float) (i % N) / (float) N * kTau + (float) c.t * 0.1f * (float) (layerN + 1);
            const float r = R * sc * rs[i % N];
            if (i == 0) p.startNewSubPath (cx + std::cos (th) * r, cy + std::sin (th) * r); else p.lineTo (cx + std::cos (th) * r, cy + std::sin (th) * r);
        }
        p.closeSubPath();
        const float a = layerN == 0 ? 1.0f : 0.35f / (float) layerN;
        g.setGradientFill (juce::ColourGradient (c.palC (0.95f - 0.25f * (float) layerN, a), cx, cy - R * 0.3f, c.palC (0.45f - 0.1f * (float) layerN, a), cx + R * sc * 1.4f, cy + R, true));
        g.fillPath (p);
        g.setColour (c.palC (1.0f, 0.6f * a));
        g.strokePath (p, juce::PathStrokeType (1.5f * (float) H / 360.0f));
    }
}

//==============================================================================================================
// RETRO
void Scene::mSynthGrid (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float w = (float) W, h = (float) H, hy = h * 0.56f;
    g.setGradientFill (juce::ColourGradient (c.palC (0.05f), 0, 0, c.palC (0.42f), 0, hy, false));
    g.fillRect (0.0f, 0.0f, w, hy);
    // sun
    const float sr = h * 0.22f * (1.0f + 0.07f * c.bass + 0.05f * c.beat), sx = w * 0.5f, sy = hy - h * 0.1f;
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, W, (int) hy));
        g.setGradientFill (juce::ColourGradient (c.palC (1.0f), sx, sy - sr, c.palC (0.62f), sx, sy + sr, false));
        g.fillEllipse (sx - sr, sy - sr, sr * 2, sr * 2);
        g.setColour (c.palC (0.3f));
        for (int k = 0; k < 7; ++k)
        {
            const float yy = sy + sr * (0.05f + 0.14f * (float) k) + fract ((float) c.t * 0.4f) * sr * 0.14f;
            g.fillRect (sx - sr, yy, sr * 2, sr * 0.018f * (float) (k + 1) * 1.6f);
        }
    }
    // mountains from the spectrum (mirrored)
    juce::Path m;
    m.startNewSubPath (0, hy);
    const int NM = 48;
    for (int i = 0; i <= NM; ++i)
    {
        const float x = w * (float) i / (float) NM;
        const float d = std::abs ((float) i / (float) NM - 0.5f) * 2.0f;
        const float hh = h * (0.03f + 0.10f * hashf (i, 7) * d + 0.16f * c.spec (1.0f - d) * d);
        m.lineTo (x, hy - hh);
    }
    m.lineTo (w, hy); m.closeSubPath();
    g.setColour (c.palC (0.04f)); g.fillPath (m);
    g.setColour (c.palC (0.75f, 0.9f)); g.strokePath (m, juce::PathStrokeType (1.2f));
    // ground grid
    g.setColour (juce::Colour (0xff000000)); g.fillRect (0.0f, hy, w, h - hy);
    const juce::Colour gc = c.palC (0.8f, 0.55f + 0.45f * c.beat);
    g.setColour (gc);
    const int NL = 14;
    const float phase = fract ((float) c.t * 0.8f);
    for (int k = 0; k < NL; ++k)
    {
        const float z = ((float) k + 1.0f - phase) / (float) NL;   // 1 far .. 0 near
        const float y = hy + (h - hy) * (0.06f / (z + 0.06f) - 0.06f / 1.06f) / (1.0f - 0.06f / 1.06f);
        g.fillRect (0.0f, y, w, std::max (1.0f, 2.2f * (1.0f - z)));
    }
    const int NV = 10 + (int) (c.detail * 16);
    for (int j = -NV; j <= NV; ++j)
    {
        const float xb = w * 0.5f + (float) j * w * 2.2f / (float) NV;
        g.drawLine (w * 0.5f + (float) j * w * 0.012f, hy, xb, h, 1.4f);
    }
    g.setColour (c.palC (0.95f, 0.8f)); g.fillRect (0.0f, hy - 1, w, 2.0f);
}

void Scene::mTunnel (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const int N = 16 + (int) (c.detail * 16);
    const int sides = 4;
    const float cx0 = (float) W * 0.5f, cy0 = (float) H * 0.5f;
    for (int k = 0; k < N; ++k)
    {
        const float z = fract ((float) k / (float) N + (float) c.t * 0.12f);
        const float size = std::pow (z, 2.4f) * (float) W * 1.5f + 2.0f;
        const float cx = cx0 + std::sin ((float) c.t * 0.7f + z * 3.0f) * (float) W * 0.08f * (1.0f - z);
        const float cy = cy0 + std::cos ((float) c.t * 0.5f + z * 2.0f) * (float) H * 0.08f * (1.0f - z);
        const float rot = z * 1.2f * std::sin ((float) c.t * 0.2f) + kPiF / 4;
        juce::Path p;
        for (int s = 0; s <= sides; ++s)
        {
            const float a = rot + (float) s / (float) sides * kTau;
            const float x = cx + std::cos (a) * size * 0.5f, y = cy + std::sin (a) * size * 0.5f * (float) H / (float) W * 1.6f;
            if (s == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const float bandv = c.spec ((float) k / (float) N);
        const float th = (1.0f + 6.0f * z * (0.4f + bandv + 0.5f * c.beat)) * (float) H / 360.0f;
        g.setColour (c.palC ((float) k / (float) N, clamp01 (z * 1.4f)));
        g.strokePath (p, juce::PathStrokeType (th, juce::PathStrokeType::mitered));
    }
}

void Scene::mRidge (Ctx& c)
{
    const int rows = 26 + (int) (c.detail * 24);
    ridgeTimer += c.dt * (float) std::max (0.2f, c.speed);
    if (ridge.empty()) ridge.assign ((size_t) rows, std::array<float, 64> {});
    if ((int) ridge.size() != rows) ridge.resize ((size_t) rows);
    if (ridgeTimer > 1.0f / 22.0f)
    {
        ridgeTimer = 0;
        std::rotate (ridge.rbegin(), ridge.rbegin() + 1, ridge.rend());
        ridge[0] = c.a.spec;
    }
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float x0 = (float) W * 0.16f, x1 = (float) W * 0.84f;
    const int NP = 72;
    for (int r = rows - 1; r >= 0; --r)
    {
        const float y = (float) H * (0.14f + 0.78f * (float) (rows - 1 - r) / (float) (rows - 1));
        juce::Path p;
        p.startNewSubPath (x0, y);
        for (int i = 0; i <= NP; ++i)
        {
            const float f = (float) i / (float) NP;
            const float d = 1.0f - std::abs (f - 0.5f) * 2.0f;
            const float env = std::pow (d, 1.6f);
            const float bf = d * 0.85f;
            const int bi = std::min (62, (int) (bf * 63.0f));
            const float v = lerpf (ridge[(size_t) r][(size_t) bi], ridge[(size_t) r][(size_t) bi + 1], bf * 63.0f - (float) bi);
            const float noise = 0.18f * vnoise (f * 9.0f, (float) r * 0.7f) * env;
            p.lineTo (lerpf (x0, x1, f), y - (v * 0.9f + noise) * env * (float) H * 0.22f);
        }
        p.lineTo (x1, y);
        juce::Path fillP (p); fillP.lineTo (x1, y + 2); fillP.lineTo (x0, y + 2); fillP.closeSubPath();
        g.setColour (juce::Colours::black); g.fillPath (fillP);
        const float age = (float) r / (float) rows;
        g.setColour (c.palC (0.95f - 0.6f * age, 1.0f - 0.55f * age));
        g.strokePath (p, juce::PathStrokeType ((r == 0 ? 2.0f : 1.2f) * (float) H / 360.0f));
    }
}

void Scene::mCopper (Ctx& c)
{
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    const int nb = 6 + (int) (c.detail * 6);
    struct Bar { float y, hb, z; int i; };
    Bar bars[16];
    for (int i = 0; i < nb; ++i)
    {
        const float ph = (float) c.t * 0.9f + (float) i * 0.52f;
        bars[i] = { (float) H * (0.5f + 0.38f * std::sin (ph) * (0.6f + 0.4f * c.spec ((float) i / (float) nb))),
                    (float) H * (0.03f + 0.035f * c.spec ((float) i / (float) nb) + 0.02f * c.beat), std::cos (ph), i };
    }
    std::sort (bars, bars + nb, [] (const Bar& a, const Bar& b) { return a.z < b.z; });
    for (int y = 0; y < H; ++y)
    {
        u32 col = mulc (c.pal (0.08f + 0.06f * (float) y / (float) H), 0.5f);
        for (int k = 0; k < nb; ++k)
        {
            const float d = std::abs ((float) y - bars[k].y) / bars[k].hb;
            if (d < 1.0f)
            {
                const float in = 1.0f - d * d;
                col = mulc (c.pal (0.35f + 0.6f * (float) bars[k].i / (float) nb), 0.25f + 0.85f * in);
            }
        }
        u32* row = p.row (y);
        std::fill (row, row + W, col);
    }
    // a scrolling dot-sine across the middle, Amiga style
    for (int x = 0; x < W; x += std::max (2, W / 160))
    {
        const float yy = (float) H * 0.5f + std::sin ((float) x * 0.03f + (float) c.t * 3.0f) * (float) H * 0.12f * (0.4f + c.mid);
        const int iy = (int) yy;
        for (int dy = -1; dy <= 1; ++dy) if (iy + dy >= 0 && iy + dy < H) p.row (iy + dy)[x] = c.pal (1.0f);
    }
}

//==============================================================================================================
// SPACE
void Scene::mStarfield (Ctx& c)
{
    const int n = 300 + (int) (c.detail * 1500);
    if ((int) stars.size() != n)
    {
        stars.resize ((size_t) n);
        for (auto& s : stars) s = { rng.f() * 2 - 1, rng.f() * 2 - 1, 0.05f + rng.f() * 0.95f, 0, 0, false };
    }
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float vel = c.dt * std::max (0.05f, c.speed) * (0.15f + c.level * 0.8f + c.beat * 1.2f) * 0.6f;
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f, sc = (float) H * 0.5f;
    for (auto& s : stars)
    {
        s.z -= vel;
        if (s.z < 0.02f) { s = { rng.f() * 2 - 1, rng.f() * 2 - 1, 1.0f, 0, 0, false }; }
        const float sx = cx + s.x / s.z * sc * 0.6f, sy = cy + s.y / s.z * sc * 0.6f;
        const float bright = std::pow (1.0f - s.z, 1.5f);
        if (sx < -50 || sx > (float) W + 50 || sy < -50 || sy > (float) H + 50) { s.z = 0; continue; }
        const juce::Colour col = c.palC (0.4f + 0.6f * (1.0f - s.z), clamp01 (bright * 1.3f));
        g.setColour (col);
        const float sz = 0.6f + 2.4f * (1.0f - s.z);
        if (s.valid && vel > 0.004f) g.drawLine (s.px, s.py, sx, sy, sz);
        else g.fillRect (sx - sz * 0.5f, sy - sz * 0.5f, sz, sz);
        s.px = sx; s.py = sy; s.valid = true;
    }
}

void Scene::mHyperspace (Ctx& c)
{
    const float K = 60.0f + c.detail * 160.0f;
    const float tt = (float) c.t, lv = c.level, bt = c.beat;
    shade (c, [&] (float u, float v) -> TB
    {
        const float r = std::sqrt (u * u + v * v);
        const float a = fatan2 (v, u) * (1.0f / kTau) + 0.5f;
        const float sa = a * K;
        const int sector = (int) sa;
        const float h = hashf (sector, 11);
        const float fa = sa - (float) sector;
        const float width = 1.0f - std::abs (fa - 0.5f) * 2.0f;
        const float depth = 0.25f / (r + 0.02f);
        const float s = fract (depth * 0.5f + tt * (0.4f + h) * (0.5f + lv * 1.5f) + h * 7.0f);
        float b = std::pow (s, 10.0f) * smooth01 (0.03f, 0.35f, r) * (0.35f + 0.65f * h) * width * width * 2.2f;
        b += 0.35f * std::exp (-r * 7.0f) * (0.6f + bt);
        return { 0.45f + 0.5f * h + 0.2f * s, clamp01 (b) };
    });
}

void Scene::mNebula (Ctx& c)
{
    const float tt = (float) c.t, lv = c.level, bs = c.bass;
    shadeHalf (c, [&] (float u, float v) -> TB
    {
        const float px = u * 1.6f, py = v * 1.6f;
        const float q = fbm (px + tt * 0.03f, py - tt * 0.02f, 3);
        const float n = fbm (px * 1.3f + q * (1.5f + bs) + tt * 0.05f, py * 1.3f - q * 1.2f - tt * 0.04f, 4);
        float dens = smooth01 (0.32f, 0.85f, n) * (0.55f + 0.7f * lv);
        const float sh = hashf ((int) std::floor (u * 260.0f), (int) std::floor (v * 260.0f));
        if (sh > 0.993f) dens = std::max (dens, (sh - 0.993f) * 140.0f * (0.6f + 0.4f * std::sin (tt * 3.0f + sh * 100.0f)));
        return { n * 0.95f + q * 0.35f, clamp01 (dens) };
    });
}

void Scene::mOrbits (Ctx& c)
{
    const int n = 64;
    if ((int) angles.size() != n) { angles.resize (n); for (int i = 0; i < n; ++i) angles[(size_t) i] = hashf (i, 5) * kTau; }
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    juce::Point<float> pts[64];
    for (int i = 0; i < n; ++i)
    {
        const float f = (float) i / (float) n;
        const float r = (float) H * (0.06f + 0.42f * f) * 1.2f;
        angles[(size_t) i] += c.dt * std::max (0.05f, c.speed) * (0.25f / (0.25f + f)) * (0.6f + c.level + c.beat * 0.8f);
        pts[i] = { cx + std::cos (angles[(size_t) i]) * r * 1.3f, cy + std::sin (angles[(size_t) i]) * r * 0.55f };
        if (c.detail > 0.25f && i % 4 == 0)
        {
            g.setColour (c.palC (f, 0.07f));
            g.drawEllipse (cx - r * 1.3f, cy - r * 0.55f, r * 2.6f, r * 1.1f, 1.0f);
        }
    }
    if (c.detail > 0.55f)
    {
        juce::Path p; p.startNewSubPath (pts[0]);
        for (int i = 1; i < n; ++i) p.lineTo (pts[i]);
        g.setColour (c.palC (0.6f, 0.25f)); g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    const float core = (float) H * 0.05f * (1.0f + c.bass * 0.8f + c.beat * 0.5f);
    g.setGradientFill (juce::ColourGradient (c.palC (1.0f), cx, cy, c.palC (0.4f, 0.0f), cx + core * 2.5f, cy, true));
    g.fillEllipse (cx - core * 2.5f, cy - core * 2.5f, core * 5, core * 5);
    for (int i = 0; i < n; ++i)
    {
        const float sp = c.a.spec[(size_t) i];
        const float sz = (1.5f + sp * 10.0f + c.beat * 2.0f) * (float) H / 360.0f;
        g.setColour (c.palC (0.35f + 0.65f * (float) i / (float) n, 0.35f));
        g.fillEllipse (pts[i].x - sz, pts[i].y - sz, sz * 2, sz * 2);
        g.setColour (c.palC (0.5f + 0.5f * sp));
        g.fillEllipse (pts[i].x - sz * 0.5f, pts[i].y - sz * 0.5f, sz, sz);
    }
}

//==============================================================================================================
// CODE
static const juce::String kRainChars = "0123456789ABCDEFHKMNXZ$#%&*+=<>?:;/\\|[]{}";

void Scene::mCodeRain (Ctx& c)
{
    const int cell = std::max (6, (int) ((float) H / (18.0f + c.detail * 30.0f)));
    const int cols = W / cell + 1, rows = H / cell + 1;
    const auto& at = glyphAtlas (atlas, cell, c.as.uiBold, kRainChars);
    const int ng = (int) at.glyphs.size();
    if ((int) rainHead.size() != cols || (int) cellGlyph.size() != cols * rows)
    {
        rainHead.resize ((size_t) cols); rainSpeed.resize ((size_t) cols); rainLen.resize ((size_t) cols);
        for (int i = 0; i < cols; ++i) { rainHead[(size_t) i] = -rng.f() * (float) rows; rainSpeed[(size_t) i] = 6 + rng.f() * 14; rainLen[(size_t) i] = 6 + rng.f() * 18; }
        cellGlyph.resize ((size_t) (cols * rows));
        for (auto& gch : cellGlyph) gch = (uint8_t) (rng.next() % (u32) ng);
    }
    for (auto& gch : cellGlyph) if (rng.f() < 0.015f + c.high * 0.08f) gch = (uint8_t) (rng.next() % (u32) ng);
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float boost = 1.0f + c.level * 1.5f + c.beat * 1.5f;
    for (int x = 0; x < cols; ++x)
    {
        auto& hd = rainHead[(size_t) x];
        hd += c.dt * rainSpeed[(size_t) x] * std::max (0.05f, c.speed) * boost;
        const float len = rainLen[(size_t) x] * (0.7f + 0.6f * c.spec ((float) x / (float) cols));
        if (hd - len > (float) rows) { hd = -rng.f() * (float) rows * 0.4f; rainSpeed[(size_t) x] = 6 + rng.f() * 14; rainLen[(size_t) x] = 6 + rng.f() * 18; }
        const int top = std::max (0, (int) (hd - len)), bottom = std::min (rows - 1, (int) hd);
        for (int y = top; y <= bottom; ++y)
        {
            const float d = (hd - (float) y) / len;
            const bool head = y == bottom;
            g.setColour (head ? c.palC (1.0f) : c.palC (0.75f - 0.5f * d, clamp01 (1.0f - d)));
            g.drawImageAt (at.glyphs[(size_t) cellGlyph[(size_t) (y * cols + x)]], x * cell, y * cell, true);
        }
    }
}

void Scene::mGlyphStorm (Ctx& c)
{
    const int cell = std::max (6, (int) ((float) H / (14.0f + c.detail * 26.0f)));
    const int cols = W / cell + 1, rows = H / cell + 1;
    const auto& at = glyphAtlas (atlas, cell, c.as.uiBold, kRainChars);
    const int ng = (int) at.glyphs.size();
    if ((int) cellGlyph.size() != cols * rows) { cellGlyph.resize ((size_t) (cols * rows)); for (auto& gch : cellGlyph) gch = (uint8_t) (rng.next() % (u32) ng); }
    for (auto& gch : cellGlyph) if (rng.f() < 0.03f + c.high * 0.4f) gch = (uint8_t) (rng.next() % (u32) ng);
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    for (int x = 0; x < cols; ++x)
    {
        const float m = std::abs ((float) x / (float) (cols - 1) - 0.5f) * 2.0f;
        const float hgt = c.spec (1.0f - m) * (float) rows * 1.1f;
        for (int y = 0; y < rows; ++y)
        {
            const float fromBottom = (float) (rows - 1 - y);
            float b = fromBottom < hgt ? 0.55f + 0.45f * (fromBottom / std::max (1.0f, hgt)) : 0.06f + 0.06f * hashf (x, y + c.beatCount);
            if (c.beat > 0.6f && hashf (x * 31 + c.beatCount, y) > 0.97f) b = 1.0f;
            g.setColour (c.palC (0.25f + 0.75f * fromBottom / (float) rows, clamp01 (b)));
            g.drawImageAt (at.glyphs[(size_t) cellGlyph[(size_t) (y * cols + x)]], x * cell, y * cell, true);
        }
    }
}

void Scene::mBinary (Ctx& c)
{
    const int rows = 10 + (int) (c.detail * 22);
    const int cell = std::max (6, H / rows);
    const auto& at = glyphAtlas (atlas, cell, c.as.uiBold, "01");
    if ((int) rowOff.size() != rows) { rowOff.assign ((size_t) rows, 0.0f); }
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const int cols = W / cell + 2;
    for (int r = 0; r < rows; ++r)
    {
        const float sp = c.spec ((float) r / (float) rows);
        const float dir = r % 2 ? 1.0f : -1.0f;
        rowOff[(size_t) r] += dir * c.dt * std::max (0.05f, c.speed) * (float) cell * (2.0f + sp * 14.0f + c.beat * 6.0f);
        const float off = rowOff[(size_t) r];
        const int base = (int) std::floor (off / (float) cell);
        const float frac = off - (float) base * (float) cell;
        for (int k = -1; k < cols; ++k)
        {
            const int idx = k - base;
            const bool one = hashf (idx, r * 7 + (c.beatCount / 2)) > 0.5f;
            const float b = one ? 0.45f + 0.55f * sp + 0.3f * c.beat : 0.12f + 0.2f * sp;
            g.setColour (c.palC (one ? 0.9f : 0.45f, clamp01 (b)));
            g.drawImageAt (at.glyphs[one ? 1 : 0], (int) ((float) k * (float) cell + frac), r * cell, true);
        }
    }
}

//==============================================================================================================
// WASH
void Scene::mWash (Ctx& c)
{
    if (c.onset) washTarget = fract (washTarget + 0.382f + 0.1f * rng.f());
    washT = lerpf (washT, washTarget, clamp01 (c.dt * (2.0f + 10.0f * (1.0f - c.pv[pSmooth]))));
    const u32 col = mulc (c.pal (0.45f + 0.55f * tri (washT * 2.0f)), 0.35f + 0.55f * c.level + 0.35f * c.beat);
    clearLayer (col);
    juce::Graphics g (layer);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    const float d = (float) c.t * 0.3f;
    g.setGradientFill (juce::ColourGradient (c.palC (washT + 0.25f, 0.35f * (0.4f + c.mid)), cx + std::cos (d) * (float) W * 0.3f, cy + std::sin (d) * (float) H * 0.3f,
                                             c.palC (washT + 0.25f, 0.0f), cx + std::cos (d) * (float) W * 0.3f + (float) W * 0.5f, cy, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, cx, cy, juce::Colours::black.withAlpha (0.55f * c.detail + 0.1f), 0, 0, true));
    g.fillAll();
}

void Scene::mSweep (Ctx& c)
{
    const float ang = (float) c.t * 0.25f;
    const float ca = std::cos (ang), sa = std::sin (ang);
    const float freq = 0.5f + c.detail * 2.5f;
    const float steps = c.detail > 0.7f ? 4.0f + (1.0f - c.detail) * 30.0f : 0.0f;
    const float tt = (float) c.t, lv = c.level, md = c.mid;
    shade (c, [&] (float u, float v) -> TB
    {
        const float d = u * ca + v * sa;
        float val = d * freq * 0.5f + tt * 0.12f;
        if (steps > 0) val = std::floor (val * steps) / steps;
        return { val, 0.6f + 0.3f * lv + 0.15f * fsin (d * 9.0f + tt * 2.0f) * md };
    });
}

void Scene::mBlocks (Ctx& c)
{
    const int nc = 2 + (int) (c.detail * 7), nr = std::max (2, (int) std::round ((float) nc * (float) H / (float) W));
    const size_t n = (size_t) (nc * nr);
    if (blocks.size() != n) { blocks.assign (n, 0.0f); blockT.assign (n, 0.0f); }
    if (c.onset)
    {
        const int k = 1 + (int) (c.bass * 4.0f + c.level * 2.0f);
        for (int i = 0; i < k; ++i) { const size_t j = rng.next() % n; blocks[j] = 1.0f; blockT[j] = rng.f(); }
    }
    const float dec = std::exp (-c.dt * 3.0f / (0.25f + c.pv[pSmooth]));
    for (auto& b : blocks) b *= dec;
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float bw = (float) W / (float) nc, bh = (float) H / (float) nr, gap = std::max (1.0f, (float) H / 180.0f);
    for (int y = 0; y < nr; ++y)
        for (int x = 0; x < nc; ++x)
        {
            const size_t j = (size_t) (y * nc + x);
            const float b = std::max (blocks[j], 0.06f + 0.1f * c.level);
            g.setColour (juce::Colour (mulc (c.pal (0.3f + 0.7f * blockT[j]), b)));
            g.fillRect ((float) x * bw + gap, (float) y * bh + gap, bw - 2 * gap, bh - 2 * gap);
        }
}

//==============================================================================================================
// STATIC
void Scene::mStatic (Ctx& c)
{
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    const float lv = 0.25f + 0.75f * clamp01 (c.level * 1.2f + c.beat * 0.4f);
    const float roll = fract ((float) c.t * 0.25f) * (float) H, band = (float) H * 0.12f;
    const float tint = c.detail;
    for (int y = 0; y < H; ++y)
    {
        u32* row = p.row (y);
        float rowB = lv;
        const float dy = std::abs ((float) y - roll);
        if (dy < band) rowB *= 0.55f + 0.45f * dy / band;
        const bool jitter = c.beat > 0.5f && (rng.next() & 15) == 0;
        if (jitter) rowB = std::min (1.0f, rowB * 1.8f);
        if ((y & 1) == 0) rowB *= 0.82f;
        for (int x = 0; x < W; ++x)
        {
            const float v = (float) (rng.next() & 255) * (1.0f / 255.0f);
            const int gv = (int) (v * rowB * 255.0f);
            const u32 grey = pack (gv, gv, gv);
            row[x] = tint > 0.01f ? lerpc (grey, mulc (c.pal (v), v * rowB * 1.2f), tint) : grey;
        }
    }
}

void Scene::mRoll (Ctx& c)
{
    rollPos += c.dt * (float) H * (0.05f + std::max (0.0f, c.speed) * 0.25f + c.beat * 1.2f);
    const float tt = (float) c.t;
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    const float bandC = fract (tt * 0.13f) * (float) H * 1.4f - (float) H * 0.2f;
    const int bands = 2 + (int) (c.detail * 6);
    for (int y = 0; y < H; ++y)
    {
        u32* row = p.row (y);
        const float yo = fract (((float) y + rollPos) / (float) H);
        const bool gapBar = yo > 0.96f;
        const float nb = std::abs ((float) y - bandC) < (float) H * 0.05f ? 1.0f : 0.0f;
        const int shift = nb > 0 ? (int) ((rng.f() - 0.5f) * (float) W * 0.05f) : 0;
        const float scan = (y & 1) ? 0.72f : 1.0f;
        for (int x = 0; x < W; ++x)
        {
            if (gapBar) { row[x] = 0xff000000u; continue; }
            const float xx = (float) (x + shift) / (float) W;
            const float val = yo * (float) bands + 0.15f * fsin (xx * 6.0f + tt * 1.5f) * (0.5f + c.mid);
            u32 col = mulc (c.pal (std::floor (val) / (float) bands * 0.8f + 0.2f), (0.55f + 0.45f * c.level) * scan);
            if (nb > 0 && (rng.next() & 3) == 0) col = lerpc (col, 0xffffffffu, rng.f() * 0.7f);
            row[x] = col;
        }
    }
}

void Scene::mSignalLost (Ctx& c)
{
    clearLayer();
    {
        juce::Graphics g (layer);
        const float bw = (float) W / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            g.setColour (juce::Colour (mulc (c.pal (1.0f - (float) i / 7.0f), 0.85f + 0.15f * c.level)));
            g.fillRect ((float) i * bw, 0.0f, bw + 1, (float) H * 0.68f);
            g.setColour (juce::Colour (mulc (c.pal ((float) ((6 - i) % 7) / 7.0f), 0.6f)));
            g.fillRect ((float) i * bw, (float) H * 0.68f, bw + 1, (float) H * 0.08f);
        }
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colour::greyLevel (i % 2 ? 0.05f : (i == 0 ? 0.95f : 0.2f)));
            g.fillRect ((float) i * (float) W / 6.0f, (float) H * 0.76f, (float) W / 6.0f + 1, (float) H * 0.24f);
        }
        if (fract ((float) c.beatPos * 0.5f) < 0.5f || c.level < 0.05f)
        {
            const float bx = (float) W * 0.3f, by = (float) H * 0.4f, bw2 = (float) W * 0.4f, bh = (float) H * 0.16f;
            g.setColour (juce::Colours::black); g.fillRect (bx, by, bw2, bh);
            g.setColour (juce::Colours::white); g.drawRect (bx, by, bw2, bh, 2.0f);
            g.setFont (uiFont (c, bh * 0.55f));
            g.drawText ("NO SIGNAL", juce::Rectangle<float> (bx, by, bw2, bh), juce::Justification::centred);
        }
    }
    // tears and static bursts on beats
    Pix p (layer);
    const float amt = clamp01 (c.beat * 1.2f + c.high * 0.5f);
    if (amt > 0.05f)
    {
        std::vector<u32> tmp ((size_t) W);
        const int slices = 2 + (int) (amt * 10);
        for (int s = 0; s < slices; ++s)
        {
            const int y0 = (int) (rng.f() * (float) H), hh = 2 + (int) (rng.f() * (float) H * 0.06f);
            const int sh = (int) ((rng.f() - 0.5f) * (float) W * 0.25f * amt);
            const bool noise = rng.f() < 0.35f;
            for (int y = y0; y < std::min (H, y0 + hh); ++y)
            {
                u32* row = p.row (y);
                if (noise) { for (int x = 0; x < W; ++x) { const int v = (int) (rng.next() & 255); row[x] = pack (v, v, v); } continue; }
                for (int x = 0; x < W; ++x) tmp[(size_t) x] = row[((x - sh) % W + W) % W];
                std::memcpy (row, tmp.data(), (size_t) W * 4);
            }
        }
    }
}

//==============================================================================================================
// PIXEL
void Scene::mBits (Ctx& c)
{
    const int n = 12 + (int) (c.detail * 36);
    const int brows = 18;
    if ((int) peaks.size() != n) peaks.assign ((size_t) n, 0.0f);
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float bw = (float) W / (float) n, bh = (float) H * 0.9f / (float) brows, gap = std::max (1.0f, bw * 0.12f);
    for (int i = 0; i < n; ++i)
    {
        const float v = c.spec (((float) i + 0.5f) / (float) n);
        const int lit = (int) std::round (v * (float) brows);
        peaks[(size_t) i] = std::max (peaks[(size_t) i] - c.dt * 0.6f, v);
        const int pk = std::min (brows - 1, (int) std::round (peaks[(size_t) i] * (float) brows));
        for (int r = 0; r < brows; ++r)
        {
            const float y = (float) H * 0.95f - (float) (r + 1) * bh;
            const bool on = r < lit;
            if (! on && r != pk) { g.setColour (c.palC (0.2f, 0.06f)); }
            else if (r == pk && ! on) g.setColour (c.palC (1.0f));
            else g.setColour (juce::Colour (c.pal (std::floor ((float) r / (float) brows * 6.0f) / 6.0f * 0.8f + 0.25f)));
            g.fillRect ((float) i * bw + gap * 0.5f, y + gap * 0.5f, bw - gap, bh - gap);
        }
    }
}

void Scene::mFire (Ctx& c)
{
    const int px = std::max (2, (int) ((float) H / 360.0f * (8.0f - c.detail * 6.0f)));
    const int fw = W / px + 1, fh = H / px + 1;
    if (fireW != fw || fireH != fh || (int) fire.size() != fw * fh) { fireW = fw; fireH = fh; fire.assign ((size_t) (fw * fh), 0); }
    const int heat = (int) (36.0f * clamp01 (0.35f + 0.65f * c.level + 0.3f * c.beat));
    for (int x = 0; x < fw; ++x) fire[(size_t) ((fh - 1) * fw + x)] = (uint8_t) std::max (0, heat - (int) (rng.next() % 6));
    const int steps = std::max (1, (int) std::round (std::max (0.1f, c.speed) * 1.5f));
    for (int s = 0; s < steps; ++s)
        for (int y = 1; y < fh; ++y)
            for (int x = 0; x < fw; ++x)
            {
                const int src = y * fw + x;
                const int pix = fire[(size_t) src];
                if (pix == 0) { fire[(size_t) (src - fw)] = 0; continue; }
                const int r = (int) (rng.next() & 3);
                const int dst = std::clamp (src - r + 1 - fw, 0, fw * fh - 1);
                fire[(size_t) dst] = (uint8_t) std::max (0, pix - (r & 1) - ((rng.next() & 7) == 0 && c.level < 0.2f ? 1 : 0));
            }
    Pix p (layer, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; ++y)
    {
        u32* row = p.row (y);
        const uint8_t* fr = fire.data() + (size_t) (std::min (fh - 1, y / px) * fw);
        for (int x = 0; x < W; ++x)
        {
            const float v = (float) fr[std::min (fw - 1, x / px)] / 36.0f;
            row[x] = mulc (c.pal (v * 0.95f), clamp01 (v * 1.7f));
        }
    }
}

void Scene::mLife (Ctx& c)
{
    const int cell = std::max (3, (int) ((float) H / 360.0f * (4.0f + (1.0f - c.detail) * 12.0f)));
    const int gw = W / cell + 1, gh = H / cell + 1;
    if (lifeW != gw || lifeH != gh || (int) life.size() != gw * gh)
    {
        lifeW = gw; lifeH = gh; life.assign ((size_t) (gw * gh), 0); life2 = life; lifeAge.assign ((size_t) (gw * gh), 0);
        for (auto& v : life) v = rng.f() < 0.25f ? 1 : 0;
    }
    auto seed = [&] (int count)
    {
        for (int k = 0; k < count; ++k)
        {
            const int x0 = (int) (rng.next() % (u32) gw), y0 = (int) (rng.next() % (u32) gh);
            static const int glider[5][2] = { { 1, 0 }, { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 } };
            static const int rpent[5][2] = { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 }, { 1, 2 } };
            const auto& sh = (rng.next() & 1) ? glider : rpent;
            for (auto& q : sh) life[(size_t) (((y0 + q[1]) % gh) * gw + (x0 + q[0]) % gw)] = 1;
        }
    };
    if (c.onset) seed (1 + (int) (c.bass * 6.0f));
    lifeTimer += c.dt * std::max (0.05f, c.speed);
    if (lifeTimer > 0.09f || c.onset)
    {
        lifeTimer = 0;
        int pop = 0;
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x)
            {
                int nsum = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (dx || dy) nsum += life[(size_t) (((y + dy + gh) % gh) * gw + (x + dx + gw) % gw)];
                const size_t i = (size_t) (y * gw + x);
                const uint8_t alive = life[i] ? (nsum == 2 || nsum == 3) : (nsum == 3);
                life2[i] = alive; pop += alive;
                lifeAge[i] = alive ? (uint8_t) std::min (255, lifeAge[i] + 1) : (uint8_t) 0;
            }
        std::swap (life, life2);
        if (pop < gw * gh / 40) seed (6);
    }
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x)
        {
            const size_t i = (size_t) (y * gw + x);
            if (! life[i]) continue;
            const float age = std::min (1.0f, (float) lifeAge[i] / 40.0f);
            g.setColour (juce::Colour (mulc (c.pal (1.0f - 0.65f * age), 0.6f + 0.4f * c.level + 0.3f * c.beat)));
            g.fillRect (x * cell, y * cell, cell - 1, cell - 1);
        }
}

void Scene::mMosaic (Ctx& c)
{
    const int cell = std::max (4, (int) ((float) H / 360.0f * (8.0f + (1.0f - c.detail) * 26.0f)));
    clearLayer();
    juce::Graphics g (layer);
    const float tt = (float) c.t;
    for (int y = 0; y < H; y += cell)
        for (int x = 0; x < W; x += cell)
        {
            const float u = ((float) x - (float) W * 0.5f) / (float) H * 2.0f / c.zoom, v = ((float) y - (float) H * 0.5f) / (float) H * 2.0f / c.zoom;
            const float val = fsin (u * 4.0f + tt) + fsin (v * 3.5f - tt * 1.3f) + fsin ((u + v) * 3.0f + tt * 0.6f) + fsin (std::sqrt (u * u + v * v) * 6.0f - tt * 1.8f);
            const float band = c.spec (fract (val * 0.125f + 0.5f));
            const float ins = (float) cell * 0.5f * clamp01 (0.35f - 0.3f * band - 0.25f * c.beat);
            g.setColour (juce::Colour (mulc (c.pal (val * 0.125f + 0.5f), 0.55f + 0.45f * band + 0.2f * c.beat)));
            g.fillRect ((float) x + ins, (float) y + ins, (float) cell - 1 - 2 * ins, (float) cell - 1 - 2 * ins);
        }
}

//==============================================================================================================
// TEXT
juce::String Scene::currentText (const Ctx& c, float& progress, int& lineIndex) const
{
    const auto& lines = c.as.lines.isEmpty() ? defaultLines() : c.as.lines;
    const float div = kCycleBeats[std::clamp ((int) c.pv[pTextCycle], 0, 5)];
    if (div <= 0) { progress = fract ((float) (c.beatPos / 4.0)); lineIndex = -1; return lines.joinIntoString ("\n"); }
    const double pos = c.beatPos / (double) div;
    lineIndex = (int) (((long long) std::floor (pos)) % lines.size());
    if (lineIndex < 0) lineIndex += lines.size();
    progress = (float) (pos - std::floor (pos));
    return lines[lineIndex];
}

void Scene::drawTextAnim (juce::Graphics& g, const Ctx& c, const juce::String& s0, juce::Rectangle<float> area, int anim, float progress, float sizeMul, float glitch)
{
    juce::String s = s0;
    if (glitch > 0.01f)
    {
        juce::String out;
        for (int i = 0; i < s.length(); ++i)
        {
            auto ch = s[i];
            if (ch != ' ' && ch != '\n' && rng.f() < glitch * 0.12f * (c.high + c.beat)) ch = kRainChars[(int) (rng.next() % (u32) kRainChars.length())];
            out += juce::String::charToString (ch);
        }
        s = out;
    }
    juce::StringArray lines; lines.addLines (s);
    lines.removeEmptyStrings (false);
    if (lines.isEmpty()) return;
    const int nl = lines.size();
    float h = area.getHeight() * 0.3f * sizeMul / std::sqrt ((float) nl);
    juce::Font f = textFont (c, h);
    float widest = 0;
    for (auto& l : lines) { juce::GlyphArrangement ga; ga.addLineOfText (f, l, 0, 0); widest = std::max (widest, ga.getBoundingBox (0, -1, true).getWidth()); }
    if (widest > area.getWidth() * 0.92f) { h *= area.getWidth() * 0.92f / widest; f = textFont (c, h); }
    if ((float) nl * h * 1.15f > area.getHeight() * 0.95f) { h *= area.getHeight() * 0.95f / ((float) nl * h * 1.15f); f = textFont (c, h); }

    int total = 0; for (auto& l : lines) total += l.length();
    const int visible = anim == 2 ? (int) (std::min (1.0f, progress * 1.5f + 0.05f) * (float) total) : total;
    float alpha = 1.0f, scale = 1.0f;
    if (anim == 0) scale = 1.0f + 0.32f * c.beat + 0.08f * c.bass;
    if (anim == 3) alpha = 0.1f + 0.9f * clamp01 (c.beat * 1.3f);
    if (anim == 5) { scale = 0.45f + 0.95f * std::sqrt (progress); alpha = 1.0f - smooth01 (0.78f, 1.0f, progress); }
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float split = (anim == 1 ? 1.0f : 0.0f) * (1.0f + 10.0f * c.beat) * (float) H / 360.0f * (0.5f + glitch) + glitch * c.beat * 6.0f;

    auto drawAll = [&] (juce::Colour top, juce::Colour bottom, float dx, float dy)
    {
        int counted = 0;
        for (int li = 0; li < nl; ++li)
        {
            juce::GlyphArrangement ga; ga.addLineOfText (f, lines[li], 0, 0);
            const auto bb = ga.getBoundingBox (0, -1, true);
            const float x0 = cx - bb.getWidth() * 0.5f - bb.getX();
            const float by = cy + ((float) li - (float) (nl - 1) * 0.5f) * h * 1.12f + h * 0.33f;
            g.setGradientFill (juce::ColourGradient (top, 0, by - h * 0.75f, bottom, 0, by + h * 0.1f, false));
            for (int i = 0; i < ga.getNumGlyphs(); ++i, ++counted)
            {
                if (counted >= visible) return;
                const auto& gl = ga.getGlyph (i);
                if (gl.isWhitespace()) continue;
                auto tr = juce::AffineTransform::translation (x0 + dx, by + dy);
                const float gcx = x0 + gl.getLeft() + gl.getRight() * 0.0f + gl.getBounds().getWidth() * 0.5f, gcy = by - h * 0.35f;
                if (anim == 4)
                {
                    const float k = (float) counted / (float) std::max (1, total);
                    const float sc = 1.0f + c.spec (k) * 0.45f;
                    tr = tr.translated (0, std::sin ((float) counted * 0.6f + (float) c.t * 4.0f) * h * 0.16f * (0.5f + c.level))
                           .scaled (sc, sc, gcx + dx, gcy + dy);
                }
                if (anim == 6)
                    tr = tr.translated ((rng.f() - 0.5f) * h * 0.3f * (c.bass + c.beat), (rng.f() - 0.5f) * h * 0.3f * (c.bass + c.beat));
                if (scale != 1.0f) tr = tr.scaled (scale, scale, cx, cy);
                gl.draw (g, tr);
            }
        }
    };
    const juce::Colour top = c.palC (1.0f, alpha), bot = c.palC (0.6f, alpha);
    drawAll (juce::Colours::black.withAlpha (0.55f * alpha), juce::Colours::black.withAlpha (0.55f * alpha), h * 0.04f, h * 0.05f);
    if (split > 0.3f)
    {
        drawAll (c.palC (0.25f, 0.85f * alpha), c.palC (0.25f, 0.85f * alpha), -split, 0);
        drawAll (c.palC (0.7f, 0.85f * alpha), c.palC (0.7f, 0.85f * alpha), split, 0);
    }
    drawAll (top, bot, 0, 0);
    if (anim == 2 && visible < total && fract ((float) c.t * 2.0f) < 0.6f)
    {
        g.setColour (c.palC (1.0f, 0.9f));
        g.fillRect (cx + h * 0.2f * (float) (visible - total / 2) * 0.5f, cy - h * 0.4f, h * 0.45f, h * 0.8f);
    }
}

void Scene::textGlitchPass (juce::Image& im, const Ctx& c, float amount)
{
    if (amount <= 0.01f) return;
    const float chance = amount * (0.15f + c.beat * 0.85f);
    if (rng.f() > chance) return;
    Pix p (im);
    std::vector<u32> tmp ((size_t) p.w);
    const int slices = 1 + (int) (amount * 8 * (0.3f + c.beat));
    for (int s = 0; s < slices; ++s)
    {
        const int y0 = (int) (rng.f() * (float) p.h), hh = 1 + (int) (rng.f() * (float) p.h * 0.05f);
        const int sh = (int) ((rng.f() - 0.5f) * (float) p.w * 0.12f * amount);
        for (int y = y0; y < std::min (p.h, y0 + hh); ++y)
        {
            u32* row = p.row (y);
            for (int x = 0; x < p.w; ++x) { const int sx = x - sh; tmp[(size_t) x] = (sx >= 0 && sx < p.w) ? row[sx] : 0u; }
            std::memcpy (row, tmp.data(), (size_t) p.w * 4);
        }
    }
}

void Scene::mLyric (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer);
    g.setGradientFill (juce::ColourGradient (c.palC (0.02f), 0, 0, c.palC (0.18f + 0.1f * c.bass), 0, (float) H, false));
    g.fillAll();
    // spectrum horizon line
    juce::Path p;
    for (int i = 0; i <= 64; ++i)
    {
        const float f = (float) i / 64.0f, m = std::abs (f - 0.5f) * 2.0f;
        const float y = (float) H * 0.86f - c.spec (1.0f - m) * (float) H * 0.1f;
        if (i == 0) p.startNewSubPath (0, y); else p.lineTo (f * (float) W, y);
    }
    g.setColour (c.palC (0.7f, 0.5f)); g.strokePath (p, juce::PathStrokeType (1.5f));
    float prog; int li;
    const auto s = currentText (c, prog, li);
    vecTransform (g, c);
    drawTextAnim (g, c, s, { 0.0f, 0.0f, (float) W, (float) H }, (int) c.pv[pTextAnim], prog, c.pv[pTextSize], c.pv[pTextGlitch] * 0.5f);
}

void Scene::mGlitchType (Ctx& c)
{
    clearLayer();
    {
        juce::Graphics g (layer);
        for (int k = 0; k < 6 + (int) (c.high * 20); ++k)
        {
            g.setColour (c.palC (rng.f(), 0.08f + 0.25f * c.beat * rng.f()));
            g.fillRect (rng.f() * (float) W, rng.f() * (float) H, rng.f() * (float) W * 0.3f, rng.f() * (float) H * 0.04f);
        }
        float prog; int li;
        const auto s = currentText (c, prog, li);
        vecTransform (g, c);
        drawTextAnim (g, c, s, { 0.0f, 0.0f, (float) W, (float) H }, 1, prog, c.pv[pTextSize], 0.4f + c.pv[pTextGlitch] * 0.6f);
    }
    textGlitchPass (layer, c, 0.5f + c.pv[pTextGlitch] * 0.5f);
    Pix p (layer);
    for (int y = 1; y < H; y += 2) { u32* r = p.row (y); for (int x = 0; x < W; ++x) r[x] = mulc (r[x], 0.6f); }
}

void Scene::mMarquee (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const auto& lines = c.as.lines.isEmpty() ? defaultLines() : c.as.lines;
    const juce::String joined = lines.joinIntoString ("  /  ") + "  /  ";
    const int rows = 2 + (int) (c.detail * 4);
    if ((int) rowOff.size() != rows) rowOff.assign ((size_t) rows, 0.0f);
    const float rh = (float) H / (float) rows;
    for (int r = 0; r < rows; ++r)
    {
        const float h = rh * (0.62f + 0.25f * (r % 2 ? 0.0f : 1.0f)) * c.pv[pTextSize];
        const auto f = textFont (c, h);
        juce::GlyphArrangement ga; ga.addLineOfText (f, joined, 0, 0);
        const float tw = std::max (10.0f, ga.getBoundingBox (0, -1, true).getWidth());
        const float sp = c.spec ((float) r / (float) rows);
        rowOff[(size_t) r] += (r % 2 ? 1.0f : -1.0f) * c.dt * std::max (0.05f, c.speed) * (float) H * (0.25f + 0.1f * (float) r + sp * 0.6f + c.beat * 0.6f);
        float off = std::fmod (rowOff[(size_t) r], tw); if (off > 0) off -= tw;
        const float by = rh * ((float) r + 0.5f) + h * 0.33f + std::sin ((float) c.t * 2.0f + (float) r) * rh * 0.06f;
        g.setColour (c.palC (0.4f + 0.6f * ((float) r + 1.0f) / (float) rows, clamp01 (0.45f + 0.55f * sp + 0.3f * c.beat)));
        for (float x = off; x < (float) W; x += tw) ga.draw (g, juce::AffineTransform::translation (x, by));
    }
}

void Scene::mWordWall (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    float prog; int li;
    juce::String word = currentText (c, prog, li).replace ("\n", " ");
    const int rows = 5 + (int) (c.detail * 9);
    const float rh = (float) H / (float) rows;
    const auto f = textFont (c, rh * 0.85f * c.pv[pTextSize]);
    juce::GlyphArrangement ga; ga.addLineOfText (f, word + "  ", 0, 0);
    const float tw = std::max (10.0f, ga.getBoundingBox (0, -1, true).getWidth());
    for (int r = 0; r < rows; ++r)
    {
        const float sp = c.spec ((float) r / (float) rows);
        const float off0 = std::fmod ((float) c.t * (float) H * 0.15f * (r % 2 ? 1.0f : -1.0f) * (1.0f + 0.3f * (float) (r % 3)), tw);
        const float off = off0 > 0 ? off0 - tw : off0;
        const bool hot = c.beat > 0.5f && ((r + c.beatCount) % rows) == 0;
        g.setColour (c.palC (0.3f + 0.7f * sp, hot ? 1.0f : clamp01 (0.15f + 0.85f * sp)));
        for (float x = off; x < (float) W; x += tw) ga.draw (g, juce::AffineTransform::translation (x, rh * ((float) r + 0.82f)));
    }
}

//==============================================================================================================
// PLASMA
void Scene::mPlasma (Ctx& c)
{
    const float k = 2.0f + c.detail * 8.0f, tt = (float) c.t, bs = c.bass, lv = c.level;
    shade (c, [&] (float u, float v) -> TB
    {
        const float val = fsin (u * k + tt) + fsin (v * k * 0.8f - tt * 1.3f) + fsin ((u + v) * k * 0.6f + tt * 0.7f)
                        + fsin (std::sqrt (u * u + v * v) * k * 1.3f - tt * 1.6f + bs * 2.0f);
        return { val * 0.125f + 0.5f, 0.6f + 0.25f * fsin (val * 1.5708f + tt) + 0.35f * lv };
    });
}

void Scene::mLiquid (Ctx& c)
{
    const int L = 4 + (int) (c.detail * 6);
    float amp[12];
    for (int i = 0; i < L; ++i) amp[i] = 0.5f + c.spec ((float) i / (float) L) * 1.6f;
    const float tt = (float) c.t, lv = c.level;
    shade (c, [&] (float u, float v) -> TB
    {
        int idx = 0; float edge = 1.0f;
        for (int i = 0; i < L; ++i)
        {
            const float yi = ((float) i / (float) L - 0.5f) * 1.9f + 0.11f * fsin (u * (2.0f + (float) i * 0.7f) + tt * (0.6f + 0.2f * (float) i)) * amp[i]
                           + 0.04f * fsin (u * 7.0f - tt * 2.0f + (float) i);
            const float d = v - yi;
            if (d > 0) ++idx;
            edge = std::min (edge, std::abs (d));
        }
        const float glow = 1.0f - smooth01 (0.0f, 0.03f, edge);
        return { (float) idx / (float) L + 0.05f * v, 0.65f + 0.25f * lv + glow * 0.6f };
    });
}

void Scene::mInterference (Ctx& c)
{
    const float tt = (float) c.t, k = 18.0f + c.detail * 40.0f, w = 3.0f * (1.0f + c.bass), lv = c.level;
    float sx[3], sy[3];
    for (int i = 0; i < 3; ++i) { sx[i] = 0.7f * std::sin (tt * (0.21f + 0.07f * (float) i) + (float) i * 2.1f); sy[i] = 0.5f * std::cos (tt * (0.17f + 0.05f * (float) i) + (float) i); }
    shade (c, [&] (float u, float v) -> TB
    {
        float val = 0, d0 = 0;
        for (int i = 0; i < 3; ++i)
        {
            const float dx = u - sx[i], dy = v - sy[i];
            const float d = std::sqrt (dx * dx + dy * dy);
            if (i == 0) d0 = d;
            val += fsin (d * k - tt * w);
        }
        val *= (1.0f / 3.0f);
        const float b = (0.5f + 0.5f * val);
        return { 0.5f + 0.45f * val + d0 * 0.25f, b * b * (0.7f + 0.6f * lv) + 0.05f };
    });
}

void Scene::mSwirl (Ctx& c)
{
    const float tt = (float) c.t, bs = c.bass;
    shadeHalf (c, [&] (float u, float v) -> TB
    {
        const float r = std::sqrt (u * u + v * v);
        const float a = (1.0f + bs * 1.5f) * 0.9f / (r + 0.35f) * std::sin (tt * 0.25f) + tt * 0.1f;
        const float cs = fcos (a), sn = fsin (a);
        const float px = (u * cs - v * sn) * 1.4f, py = (u * sn + v * cs) * 1.4f;
        const float q = fbm (px + tt * 0.05f, py, 2);
        const float n = fbm (px * 1.6f + 3.0f * q + tt * 0.08f, py * 1.6f - 2.0f * q, 3);
        return { n * 1.2f + 0.2f * r, clamp01 (0.25f + 1.0f * n) };
    });
}

//==============================================================================================================
// SPECTRUM
void Scene::mBars (Ctx& c, bool mirror)
{
    const int n = 16 + (int) (c.detail * 48);
    if ((int) peaks.size() != n) peaks.assign ((size_t) n, 0.0f);
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float floorY = mirror ? (float) H * 0.5f : (float) H * 0.8f;
    const float maxH = mirror ? (float) H * 0.45f : (float) H * 0.72f;
    const float bw = (float) W / (float) n, gap = std::max (1.0f, bw * 0.22f);
    auto bandFor = [&] (int i) { if (! mirror) return ((float) i + 0.5f) / (float) n; const float m = std::abs (((float) i + 0.5f) / (float) n - 0.5f) * 2.0f; return m; };
    g.setGradientFill (juce::ColourGradient (c.palC (1.0f), 0, floorY - maxH, c.palC (0.35f), 0, floorY, false));
    for (int i = 0; i < n; ++i)
    {
        const float v = c.spec (bandFor (i));
        peaks[(size_t) i] = std::max (peaks[(size_t) i] - c.dt * 0.5f, v);
        const float hgt = std::max (2.0f, v * maxH);
        g.fillRect ((float) i * bw + gap * 0.5f, floorY - hgt, bw - gap, hgt);
        if (mirror) g.fillRect ((float) i * bw + gap * 0.5f, floorY, bw - gap, hgt);
    }
    g.setColour (c.palC (1.0f));
    for (int i = 0; i < n; ++i)
    {
        const float pk = peaks[(size_t) i] * maxH;
        g.fillRect ((float) i * bw + gap * 0.5f, floorY - pk - 3.0f, bw - gap, 2.0f);
        if (mirror) g.fillRect ((float) i * bw + gap * 0.5f, floorY + pk + 1.0f, bw - gap, 2.0f);
    }
    if (! mirror)
    {
        g.setGradientFill (juce::ColourGradient (c.palC (0.5f, 0.3f), 0, floorY, c.palC (0.2f, 0.0f), 0, (float) H, false));
        for (int i = 0; i < n; ++i)
        {
            const float hgt = std::min ((float) H - floorY, c.spec (bandFor (i)) * maxH * 0.35f);
            g.fillRect ((float) i * bw + gap * 0.5f, floorY + 2.0f, bw - gap, hgt);
        }
    }
}

void Scene::mCircle (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    const float r0 = (float) H * 0.2f * (1.0f + 0.2f * c.bass + 0.12f * c.beat);
    const int N = 72 + (int) (c.detail * 120);
    const float rot = (float) c.t * 0.1f;
    const float bw = kTau * r0 / (float) N * 0.6f;
    for (int i = 0; i < N; ++i)
    {
        const float a = (float) i / (float) N * kTau - kPiF * 0.5f + rot;
        const float m = (float) (i < N / 2 ? i : N - i) / (float) (N / 2);
        const float v = c.spec (m * 0.9f);
        const float len = 2.0f + v * (float) H * 0.26f;
        g.setColour (c.palC (0.4f + 0.6f * v));
        juce::Path p; p.addRectangle (-bw * 0.5f, r0, bw, len);
        g.fillPath (p, juce::AffineTransform::rotation (a - kPiF * 0.5f).translated (cx, cy));
    }
    juce::Path ring;
    for (int i = 0; i <= 256; ++i)
    {
        const float a = (float) i / 256.0f * kTau;
        const float r = r0 * 0.88f + c.a.waveL[(size_t) ((i % 256) * 4)] * r0 * 0.25f * std::min (4.0f, c.scopeGain);
        if (i == 0) ring.startNewSubPath (cx + std::cos (a) * r, cy + std::sin (a) * r); else ring.lineTo (cx + std::cos (a) * r, cy + std::sin (a) * r);
    }
    ring.closeSubPath();
    g.setColour (c.palC (0.1f)); g.fillPath (ring);
    strokeGlow (g, ring, c.palC (0.9f), 1.6f * (float) H / 360.0f);
}

void Scene::mBurst (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer); vecTransform (g, c);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f;
    if (c.onset)
        for (int k = 0; k < 24 + (int) (c.detail * 40); ++k)
        {
            const float a = rng.f() * kTau, sp = (float) H * (0.25f + rng.f() * 0.9f) * (0.6f + c.bass);
            parts.push_back ({ cx, cy, std::cos (a) * sp, std::sin (a) * sp, 1.0f, rng.f() });
        }
    for (int k = 2; k >= 0; --k)
    {
        const float sc = 1.0f - 0.3f * (float) k;
        const float rot = (float) c.t * 0.2f * (k % 2 ? -1.0f : 1.0f);
        juce::Path p;
        const int N = 128;
        for (int i = 0; i <= N; ++i)
        {
            const float a = (float) (i % N) / (float) N * kTau + rot;
            const float m = std::abs (fract ((float) (i % N) / (float) N) * 2.0f - 1.0f);
            const float r = ((float) H * 0.1f + c.spec (m * 0.85f) * (float) H * 0.38f) * sc * (1.0f + 0.1f * c.beat);
            if (i == 0) p.startNewSubPath (cx + std::cos (a) * r, cy + std::sin (a) * r); else p.lineTo (cx + std::cos (a) * r, cy + std::sin (a) * r);
        }
        p.closeSubPath();
        g.setColour (c.palC (0.9f - 0.25f * (float) k, 0.35f)); g.fillPath (p);
        g.setColour (c.palC (1.0f - 0.2f * (float) k)); g.strokePath (p, juce::PathStrokeType (1.5f));
    }
    const float core = (float) H * 0.06f * (1.0f + c.beat * 1.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.9f), cx, cy, c.palC (0.8f, 0.0f), cx + core * 2, cy, true));
    g.fillEllipse (cx - core * 2, cy - core * 2, core * 4, core * 4);
    for (auto& q : parts)
    {
        q.x += q.vx * c.dt; q.y += q.vy * c.dt; q.life -= c.dt * 0.9f;
        g.setColour (c.palC (0.5f + 0.5f * q.t, clamp01 (q.life)));
        const float sz = 1.5f + 2.5f * q.life;
        g.fillEllipse (q.x - sz, q.y - sz, sz * 2, sz * 2);
    }
    parts.erase (std::remove_if (parts.begin(), parts.end(), [] (const Particle& q) { return q.life <= 0; }), parts.end());
    if (parts.size() > 2000) parts.erase (parts.begin(), parts.begin() + (long) (parts.size() - 2000));
}

void Scene::mWaterfall (Ctx& c)
{
    if (! waterfall.isValid() || waterfall.getWidth() != W || waterfall.getHeight() != H) waterfall = makeSoftwareImage (W, H);
    Pix p (waterfall);
    const int s = std::max (1, (int) std::round (std::max (0.1f, c.speed) * 2.0f * (float) H / 360.0f));
    for (int y = H - 1; y >= s; --y) std::memcpy (p.row (y), p.row (y - s), (size_t) W * 4);
    for (int y = 0; y < s; ++y)
    {
        u32* row = p.row (y);
        for (int x = 0; x < W; ++x)
        {
            const float v = c.spec ((float) x / (float) (W - 1));
            row[x] = mulc (c.pal (v), std::pow (v, 0.7f) * 1.1f + 0.02f);
        }
    }
    copyImage (layer, waterfall);
}

//==============================================================================================================
// IMAGE
void Scene::mImgPulse (Ctx& c)
{
    clearLayer();
    work.clear (work.getBounds());
    {
        juce::Graphics g (work);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        const float sc = c.zoom * (1.0f + 0.14f * c.beat * (0.3f + c.pv[pImgPulse]) + 0.05f * c.bass);
        const float rot = c.spin + 0.03f * std::sin ((float) c.t) * c.detail;
        coverDraw (g, *c.img, { 0.0f, 0.0f, (float) W, (float) H }, sc, rot, 0, 0, 1.0f);
    }
    const int dx = (int) ((2.0f + 14.0f * c.beat) * c.pv[pImgChroma] * (float) W / 640.0f);
    chromaComposite (work, layer, dx, 1.0f, 0, c);
    if (c.beat > 0.05f)
    {
        Pix p (layer);
        const float f = 1.0f + 0.35f * c.beat * c.pv[pFlash];
        for (int y = 0; y < H; ++y) { u32* r = p.row (y); for (int x = 0; x < W; ++x) r[x] = mulc (r[x], f); }
    }
}

void Scene::mImgTiles (Ctx& c)
{
    clearLayer();
    juce::Graphics g (layer);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    const int n = 2 + (int) (c.detail * 4);
    const float tw = (float) W / (float) n, th = (float) H / (float) n;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const int i = y * n + x;
            const int band = ((i + c.beatCount) % (n * n)) * 63 / std::max (1, n * n - 1);
            const float v = c.a.spec[(size_t) std::clamp (band, 0, 63)];
            const float sc = 0.55f + 0.45f * v + 0.1f * c.beat;
            juce::Graphics::ScopedSaveState ss (g);
            const juce::Rectangle<float> r ((float) x * tw, (float) y * th, tw, th);
            g.reduceClipRegion (r.toNearestInt());
            const bool flip = ((x + y + c.beatCount) % 2) == 1;
            coverDraw (g, *c.img, r, sc * c.zoom, flip ? kPiF * 0.0f : 0.0f, 0, 0, 0.5f + 0.5f * v + 0.2f);
            if (flip) { g.setColour (c.palC (0.6f + 0.4f * v, 0.35f)); g.fillRect (r); }
            g.setColour (juce::Colours::black); g.drawRect (r, 1.0f);
        }
}

void Scene::mImgSlices (Ctx& c)
{
    work.clear (work.getBounds());
    {
        juce::Graphics g (work);
        coverDraw (g, *c.img, { 0.0f, 0.0f, (float) W, (float) H }, c.zoom * (1.0f + 0.05f * c.beat), c.spin, 0, 0, 1.0f);
    }
    Pix s (work, juce::Image::BitmapData::readOnly), d (layer, juce::Image::BitmapData::writeOnly);
    const int ns = 6 + (int) (c.detail * 40);
    for (int y = 0; y < H; ++y)
    {
        const int si = y * ns / H;
        const float v = c.spec ((float) si / (float) ns);
        const float jitter = c.beat > 0.4f && hashf (si, c.beatCount) > 0.6f ? (hashf (si * 3, c.beatCount) - 0.5f) * 0.3f : 0.0f;
        const int sh = (int) (((si % 2 ? 1.0f : -1.0f) * v * 0.12f + jitter) * (float) W);
        const bool tint = hashf (si, c.beatCount / 2 + 99) > 0.7f;
        const u32* sr = s.row (y); u32* dr = d.row (y);
        for (int x = 0; x < W; ++x)
        {
            u32 px = sr[((x - sh) % W + W) % W];
            px |= 0xff000000u;
            dr[x] = tint ? c.pal (luma (px)) : px;
        }
    }
}

void Scene::mImgPixelate (Ctx& c)
{
    work.clear (work.getBounds());
    {
        juce::Graphics g (work);
        coverDraw (g, *c.img, { 0.0f, 0.0f, (float) W, (float) H }, c.zoom, c.spin, 0, 0, 1.0f);
    }
    const float drive = std::max (c.beat, c.level);
    const int blk = std::max (2, (int) ((float) H / 10.0f * (1.0f - 0.85f * drive) * (0.25f + c.detail)));
    Pix s (work, juce::Image::BitmapData::readOnly), d (layer, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; y += blk)
        for (int x = 0; x < W; x += blk)
        {
            u32 px = s.at (x + blk / 2, y + blk / 2) | 0xff000000u;
            px = lerpc (px, c.pal (luma (px)), 0.5f);
            for (int yy = y; yy < std::min (H, y + blk); ++yy)
            {
                u32* r = d.row (yy);
                const bool edge = yy == y + blk - 1 && blk > 4;
                for (int xx = x; xx < std::min (W, x + blk); ++xx) r[xx] = (edge || (xx == x + blk - 1 && blk > 4)) ? mulc (px, 0.6f) : px;
            }
        }
}

//==============================================================================================================
// IMAGE LAYER
void Scene::chromaComposite (juce::Image& srcIm, juce::Image& dstIm, int dx, float opacity, int blend, const Ctx&)
{
    Pix s (srcIm, juce::Image::BitmapData::readOnly), d (dstIm);
    const float op = clamp01 (opacity);
    for (int y = 0; y < d.h; ++y)
    {
        const u32* sr = s.row (y); u32* dr = d.row (y);
        for (int x = 0; x < d.w; ++x)
        {
            u32 px;
            if (dx != 0)
            {
                const u32 pr = sr[std::clamp (x + dx, 0, d.w - 1)], pg = sr[x], pb = sr[std::clamp (x - dx, 0, d.w - 1)];
                const u32 a = std::max ({ pr >> 24, pg >> 24, pb >> 24 });
                px = (a << 24) | (pr & 0xff0000u) | (pg & 0xff00u) | (pb & 0xffu);
            }
            else px = sr[x];
            const u32 a = px >> 24;
            if (a == 0) { if (blend == 2) dr[x] = 0xff000000u; else if (blend == 3) dr[x] = mulc (dr[x], 1.0f - 0.7f * op); continue; }
            const float af = (float) a / 255.0f * op;
            const u32 dst = dr[x];
            switch (blend)
            {
                case 0: dr[x] = overc (dst, px, op); break;
                case 1: dr[x] = lerpc (dst, screenc (px | 0xff000000u, dst), af); break;   // image sits behind (added under) the visual
                case 2: dr[x] = lerpc (mulc (dst, 1.0f - op), mulc (dst, 0.15f + 1.2f * luma (px)), af); break;
                case 3: dr[x] = lerpc (dst, mulc (px | 0xff000000u, 0.15f + 1.7f * luma (dst)), af); break;
                case 4: dr[x] = lerpc (dst, multc (dst, px | 0xff000000u), af); break;
                default: dr[x] = lerpc (dst, screenc (dst, px | 0xff000000u), af); break;
            }
        }
    }
}

void Scene::imageLayer (Ctx& c)
{
    if (c.pv[pImgOn] < 0.5f || ! c.hasImage) return;
    const int blend = std::clamp ((int) c.pv[pImgBlend], 0, 5);
    const auto& im = *c.img;
    const float iw = (float) im.getWidth(), ih = (float) im.getHeight();
    const float pulse = c.pv[pImgPulse];
    const float sc = std::min ((float) W / iw, (float) H / ih) * c.pv[pImgSize] * (1.0f + pulse * (0.28f * c.beat + 0.1f * c.bass));
    const float rot = (float) c.t * c.pv[pImgSpin] * 1.5f;
    const float shake = c.pv[pImgShake] * (float) H * 0.07f * (c.beat + 0.3f * c.bass);
    const float dx = (rng.f() - 0.5f) * shake, dy = (rng.f() - 0.5f) * shake;
    const auto tr = juce::AffineTransform::translation (-iw * 0.5f, -ih * 0.5f).scaled (sc).rotated (rot).translated ((float) W * 0.5f + dx, (float) H * 0.5f + dy);
    work.clear (work.getBounds());
    {
        juce::Graphics g (work);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        const float glow = c.pv[pImgGlow] * (0.35f + 0.65f * c.beat);
        if (glow > 0.01f && blend <= 1)
            for (int k = 3; k >= 1; --k)
            {
                const float gs = 1.0f + 0.018f * (float) k * (1.0f + c.beat);
                g.setColour (c.palC (0.95f, clamp01 (glow * 0.45f / (float) k)));
                g.drawImageTransformed (im, tr.scaled (gs, gs, (float) W * 0.5f + dx, (float) H * 0.5f + dy), true);
            }
        g.drawImageTransformed (im, tr);
    }
    const int cdx = (int) (c.pv[pImgChroma] * (2.0f + 16.0f * c.beat) * (float) W / 640.0f);
    chromaComposite (work, layer, cdx, c.pv[pImgOpacity], blend, c);
}

//==============================================================================================================
// POST
void Scene::bloom (juce::Image& im, float amt)
{
    if (amt < 0.01f) return;
    const int f = 4, lw = W / f + 1, lh = H / f + 1;
    glowBuf.assign ((size_t) (lw * lh), 0xff000000u);
    Pix p (im);
    for (int y = 0; y < lh; ++y)
        for (int x = 0; x < lw; ++x)
        {
            int r = 0, gg = 0, b = 0, n = 0;
            for (int yy = y * f; yy < std::min (H, y * f + f); yy += 2)
            {
                const u32* row = p.row (yy);
                for (int xx = x * f; xx < std::min (W, x * f + f); xx += 2) { const u32 cc = row[xx]; r += cr (cc); gg += cg (cc); b += cb (cc); ++n; }
            }
            if (n == 0) continue;
            r /= n; gg /= n; b /= n;
            const int th = 40;   // soft threshold so dark areas do not haze
            glowBuf[(size_t) (y * lw + x)] = pack (std::max (0, r - th) * 255 / (255 - th), std::max (0, gg - th) * 255 / (255 - th), std::max (0, b - th) * 255 / (255 - th));
        }
    blur3 (glowBuf, glowTmp, lw, lh, std::max (1, H / 180));
    blur3 (glowBuf, glowTmp, lw, lh, std::max (1, H / 180));
    const int s = (int) (amt * 1.6f * 256.0f);
    // bilinear upsample in fixed point: column taps once per frame, rows blended once per output row
    auto lerp8 = [] (u32 a, u32 b, int t) noexcept
    {
        const u32 it = (u32) (256 - t), tt = (u32) t;
        return ((((a & 0xff00ffu) * it + (b & 0xff00ffu) * tt) >> 8) & 0xff00ffu) | ((((a & 0xff00u) * it + (b & 0xff00u) * tt) >> 8) & 0xff00u);
    };
    std::vector<int> cx0 ((size_t) W), ctx ((size_t) W);
    for (int x = 0; x < W; ++x)
    {
        const float fx = std::max (0.0f, ((float) x + 0.5f) / (float) f - 0.5f);
        const int x0 = std::min ((int) fx, lw - 2);
        cx0[(size_t) x] = x0; ctx[(size_t) x] = std::clamp ((int) ((fx - (float) x0) * 256.0f), 0, 256);
    }
    std::vector<u32> rowMix ((size_t) lw);
    for (int y = 0; y < H; ++y)
    {
        u32* row = p.row (y);
        const float fy = std::max (0.0f, ((float) y + 0.5f) / (float) f - 0.5f);
        const int y0 = std::min ((int) fy, lh - 2); const int ty = std::clamp ((int) ((fy - (float) y0) * 256.0f), 0, 256);
        const u32* g0 = glowBuf.data() + (size_t) (y0 * lw); const u32* g1 = g0 + lw;
        for (int x = 0; x < lw; ++x) rowMix[(size_t) x] = lerp8 (g0[x], g1[x], ty);
        for (int x = 0; x < W; ++x)
        {
            const int x0 = cx0[(size_t) x];
            const u32 gcol = lerp8 (rowMix[(size_t) x0], rowMix[(size_t) x0 + 1], ctx[(size_t) x]);
            const u32 cc = row[x];
            int r = cr (cc) + ((cr (gcol) * s) >> 8), gg = cg (cc) + ((cg (gcol) * s) >> 8), b = cb (cc) + ((cb (gcol) * s) >> 8);
            row[x] = 0xff000000u | ((u32) std::min (r, 255) << 16) | ((u32) std::min (gg, 255) << 8) | (u32) std::min (b, 255);
        }
    }
}

void Scene::mirrorPass (juce::Image& im, int mode)
{
    if (mode <= 0) return;
    Pix p (im);
    if (mode == 1 || mode == 3)
        for (int y = 0; y < H; ++y) { u32* r = p.row (y); for (int x = 0; x < W / 2; ++x) r[W - 1 - x] = r[x]; }
    if (mode == 2 || mode == 3)
        for (int y = 0; y < H / 2; ++y) std::memcpy (p.row (H - 1 - y), p.row (y), (size_t) W * 4);
}

void Scene::fxGlass (juce::Image& im, const Ctx& c, int type, float amt)
{
    copyImage (work2, im);
    Pix s (work2, juce::Image::BitmapData::readOnly), d (im, juce::Image::BitmapData::writeOnly);
    const float cx = (float) W * 0.5f, cy = (float) H * 0.5f, inv = 2.0f / (float) H;
    const float tt = (float) c.t;
    const int segs = 3 + (int) (amt * 9.0f);
    const float seg = kTau / (float) segs;
    const int cells = 5 + (int) (amt * 9.0f);
    for (int y = 0; y < H; ++y)
    {
        u32* dr = d.row (y);
        const float v = ((float) y - cy) * inv;
        for (int x = 0; x < W; ++x)
        {
            const float u = ((float) x - cx) * inv;
            float su = u, sv = v;
            switch (type)
            {
                case 0: { const float r2 = u * u + v * v; const float k = amt * (0.5f + 0.5f * c.bass + 0.3f * c.beat); const float f = 1.0f - k * 0.6f * std::max (0.0f, 1.0f - r2 * 0.5f); su = u * f; sv = v * f; break; }
                case 1: { const float r = std::sqrt (u * u + v * v); float a = fatan2 (v, u) + tt * 0.15f; a = a - std::floor (a / seg) * seg; if (a > seg * 0.5f) a = seg - a; su = std::cos (a) * r; sv = std::sin (a) * r; break; }
                case 2: { const float r = std::sqrt (u * u + v * v) + 1.0e-4f; const float w = fsin (r * 28.0f - tt * 6.0f) * 0.012f * amt * (1.0f + 3.0f * c.level + 2.0f * c.beat); su = u + u / r * w; sv = v + v / r * w; break; }
                case 3:
                {
                    const float k = 0.012f * amt * (1.0f + 3.0f * c.beat);
                    auto sample = [&] (float scale) { return s.at ((int) (cx + u * scale / inv), (int) (cy + v * scale / inv)); };
                    const u32 pr = sample (1.0f + k * 2.0f), pg = sample (1.0f), pb = sample (1.0f - k * 2.0f);
                    dr[x] = 0xff000000u | (pr & 0xff0000u) | (pg & 0xff00u) | (pb & 0xffu);
                    continue;
                }
                default:
                {
                    const float gu = (u + 2.0f) * (float) cells * 0.5f, gv = (v + 2.0f) * (float) cells * 0.5f;
                    const int ix = (int) gu, iy = (int) gv;
                    const float o = amt * 0.09f * (0.3f + c.beat + 0.5f * c.bass);
                    su = u + (hashf (ix, iy) - 0.5f) * o; sv = v + (hashf (iy, ix + 7) - 0.5f) * o;
                    const float fu = gu - (float) ix, fv = gv - (float) iy;
                    if (fu < 0.02f || fv < 0.02f) { dr[x] = mulc (s.at ((int) (cx + su / inv), (int) (cy + sv / inv)), 1.6f); continue; }
                    break;
                }
            }
            dr[x] = s.at ((int) (cx + su / inv), (int) (cy + sv / inv));
        }
    }
}

void Scene::fxTexture (juce::Image& im, const Ctx& c, int type, float amt)
{
    Pix p (im);
    const float tt = (float) c.t;
    for (int y = 0; y < H; ++y)
    {
        u32* r = p.row (y);
        const float vy = ((float) y / (float) H - 0.5f) * 2.0f;
        const int vhsShift = type == 4 ? (int) ((fsin ((float) y * 0.05f + tt * 7.0f) * 2.0f + ((rng.next() & 63) == 0 ? (rng.f() - 0.5f) * 30.0f * (0.3f + c.beat) : 0.0f)) * amt * (float) W / 640.0f) : 0;
        std::vector<u32>* rowCopy = nullptr; juce::ignoreUnused (rowCopy);
        u32 tmpRow[4096];
        if (type == 4) std::memcpy (tmpRow, r, (size_t) std::min (W, 4096) * 4);
        for (int x = 0; x < W; ++x)
        {
            u32 cc = r[x];
            switch (type)
            {
                case 0: { const int n = (int) (amt * 90.0f); cc = pack (cr (cc) + (int) (rng.next() % (u32) (2 * n + 1)) - n, cg (cc) + (int) (rng.next() % (u32) (2 * n + 1)) - n, cb (cc) + (int) (rng.next() % (u32) (2 * n + 1)) - n); break; }
                case 1: { const float g = 1.0f + (rng.f() - 0.5f) * amt * 0.9f; cc = mulc (cc, g); break; }
                case 2:
                {
                    const float vx = ((float) x / (float) W - 0.5f) * 2.0f;
                    const float vig = 1.0f - amt * 0.45f * (vx * vx * 0.6f + vy * vy * 0.8f);
                    float sl = (y % 3 == 2) ? 1.0f - 0.6f * amt : 1.0f;
                    const int m = x % 3;
                    const float boost = 1.0f + 0.25f * amt;
                    const float dim = 1.0f - 0.3f * amt;
                    cc = pack ((int) ((float) cr (cc) * (m == 0 ? boost : dim) * sl * vig), (int) ((float) cg (cc) * (m == 1 ? boost : dim) * sl * vig), (int) ((float) cb (cc) * (m == 2 ? boost : dim) * sl * vig));
                    break;
                }
                case 3: { const float thr = 0.15f + 0.7f * (1.0f - amt) - 0.15f * c.bass; cc = luma (cc) > thr ? c.pal (0.98f) : c.pal (0.02f); break; }
                default:
                {
                    if (W <= 4096)
                    {
                        const int k = 2 + (int) (amt * 6.0f);
                        const u32 a = tmpRow[std::clamp (x - vhsShift + k, 0, W - 1)], b = tmpRow[std::clamp (x - vhsShift, 0, W - 1)], d2 = tmpRow[std::clamp (x - vhsShift - k, 0, W - 1)];
                        cc = 0xff000000u | (a & 0xff0000u) | (b & 0xff00u) | (d2 & 0xffu);
                        if ((rng.next() & 1023) < (u32) (amt * 6.0f)) cc = 0xffd0d0d0u;
                    }
                    break;
                }
            }
            r[x] = cc;
        }
    }
}

static const juce::String kAsciiRamp = " .:-=+*#%@";

void Scene::fxDither (juce::Image& im, const Ctx& c, int type, float size, float mix)
{
    const int cell = std::max (3, (int) ((4.0f + size * 20.0f) * (float) H / 360.0f));
    copyImage (work2, im);
    Pix s (work2, juce::Image::BitmapData::readOnly);
    if (type == 0 || type == 1)
    {
        // Symbol (ASCII ramp) or Star glyphs, coloured by the cell
        const juce::String chars = type == 0 ? kAsciiRamp : juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00B7) + "+*x*X**";
        const auto& at = glyphAtlas (ditherAtlas, cell, c.as.uiBold, type == 0 ? kAsciiRamp : juce::String (" .+x*X#@"));
        juce::ignoreUnused (chars);
        Pix d (im, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y) { u32* r = d.row (y); const u32* sr = s.row (y); for (int x = 0; x < W; ++x) r[x] = mulc (sr[x], 1.0f - mix); }
        juce::Graphics g (im);
        const int nchar = (int) at.glyphs.size();
        for (int y = 0; y < H; y += cell)
            for (int x = 0; x < W; x += cell)
            {
                const u32 px = s.at (x + cell / 2, y + cell / 2);
                const float l = luma (px);
                const int gi = std::clamp ((int) (l * (float) nchar), 0, nchar - 1);
                if (gi == 0) continue;
                const float bright = 1.0f / std::max (0.25f, std::max ({ cr (px), cg (px), cb (px) }) / 255.0f);
                g.setColour (juce::Colour (mulc (px, std::min (2.5f, bright) * 0.95f)).withAlpha (mix));
                if (type == 1)
                {
                    juce::Path star; const float rr = (float) cell * 0.55f * std::sqrt (l);
                    star.addStar ({ (float) x + (float) cell * 0.5f, (float) y + (float) cell * 0.5f }, 5, rr * 0.42f, rr, (float) c.t * 0.5f);
                    g.fillPath (star);
                }
                else g.drawImageAt (at.glyphs[(size_t) gi], x, y, true);
            }
        return;
    }
    Pix d (im, juce::Image::BitmapData::writeOnly);
    static const float bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const float hexR = (float) cell * 0.6f;
    for (int y = 0; y < H; ++y)
    {
        u32* r = d.row (y); const u32* sr = s.row (y);
        for (int x = 0; x < W; ++x)
        {
            u32 out;
            if (type == 2)
            {
                // hex pixelation (pointy-top axial rounding)
                const float qf = (std::sqrt (3.0f) / 3.0f * (float) x - (float) y / 3.0f) / hexR, rf = (2.0f / 3.0f * (float) y) / hexR;
                float xs = qf, zs = rf, ys = -xs - zs;
                float rx = std::round (xs), ry = std::round (ys), rz = std::round (zs);
                const float dx = std::abs (rx - xs), dy = std::abs (ry - ys), dz = std::abs (rz - zs);
                if (dx > dy && dx > dz) rx = -ry - rz; else if (dy <= dz) rz = -rx - ry;
                const float hx = hexR * std::sqrt (3.0f) * (rx + rz * 0.5f), hy = hexR * 1.5f * rz;
                const float dd = std::sqrt (((float) x - hx) * ((float) x - hx) + ((float) y - hy) * ((float) y - hy)) / hexR;
                out = s.at ((int) hx, (int) hy);
                if (dd > 0.86f) out = mulc (out, 0.35f);
            }
            else if (type == 3)
            {
                const int gx = x / cell, gy = y / cell;
                const u32 px = s.at (gx * cell + cell / 2, gy * cell + cell / 2);
                const float rr = std::sqrt (luma (px)) * (float) cell * 0.62f;
                const float ddx = (float) (x - gx * cell) - (float) cell * 0.5f, ddy = (float) (y - gy * cell) - (float) cell * 0.5f;
                out = (ddx * ddx + ddy * ddy) < rr * rr ? mulc (px, 1.3f) : 0xff000000u;
            }
            else
            {
                const int px = std::max (1, cell / 4);
                const u32 src = s.at ((x / px) * px, (y / px) * px);
                const float th = (bayer[(y / px) & 3][(x / px) & 3] + 0.5f) / 16.0f - 0.5f;
                const float l = clamp01 (luma (src) + th * 0.3f);
                out = c.pal (std::floor (l * 4.0f) / 3.0f);
            }
            r[x] = mix >= 0.999f ? out : lerpc (sr[x], out, mix);
        }
    }
}

void Scene::fxTrack (juce::Image& im, const Ctx& c, int style, float amt)
{
    const int gw = 32, gh = 18;
    float lum[gw * gh];
    {
        Pix p (im, juce::Image::BitmapData::readOnly);
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx)
            {
                float s = 0; int n = 0;
                const int x0 = gx * W / gw, x1 = (gx + 1) * W / gw, y0 = gy * H / gh, y1 = (gy + 1) * H / gh;
                for (int y = y0; y < y1; y += 3) for (int x = x0; x < x1; x += 3) { s += luma (p.row (y)[x]); ++n; }
                lum[gy * gw + gx] = n ? s / (float) n : 0;
            }
    }
    struct Cand { float x, y, v; };
    std::vector<Cand> cands;
    for (int gy = 1; gy < gh - 1; ++gy)
        for (int gx = 1; gx < gw - 1; ++gx)
        {
            const float v = lum[gy * gw + gx];
            if (v < 0.22f) continue;
            bool mx = true;
            for (int dy = -1; dy <= 1 && mx; ++dy) for (int dx = -1; dx <= 1; ++dx) if ((dx || dy) && lum[(gy + dy) * gw + gx + dx] > v) { mx = false; break; }
            if (mx) cands.push_back ({ ((float) gx + 0.5f) / (float) gw * (float) W, ((float) gy + 0.5f) / (float) gh * (float) H, v });
        }
    std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.v > b.v; });
    const int K = 2 + (int) (amt * 10.0f);
    if ((int) cands.size() > K) cands.resize ((size_t) K);
    for (auto& tr : tracks) tr.live = false;
    for (auto& cd : cands)
    {
        Track* best = nullptr; float bd = (float) H * 0.25f;
        for (auto& tr : tracks) { const float d = std::hypot (tr.x - cd.x, tr.y - cd.y); if (! tr.live && d < bd) { bd = d; best = &tr; } }
        const float sz = (float) H * (0.06f + 0.14f * cd.v) * (1.0f + 0.3f * c.beat);
        if (best) { best->x = lerpf (best->x, cd.x, 0.5f); best->y = lerpf (best->y, cd.y, 0.5f); best->w = lerpf (best->w, sz * 1.3f, 0.3f); best->h = lerpf (best->h, sz, 0.3f); best->live = true; best->age++; }
        else tracks.push_back ({ cd.x, cd.y, sz * 1.3f, sz, nextTrackId++ % 100, 0, true });
    }
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [] (const Track& t2) { return ! t2.live; }), tracks.end());
    juce::Graphics g (im);
    const juce::Colour col = juce::Colours::white.interpolatedWith (c.palC (1.0f), 0.3f);
    const float lw = std::max (1.0f, (float) H / 360.0f);
    g.setFont (uiFont (c, std::max (8.0f, (float) H / 34.0f)));
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& tr = tracks[i];
        const juce::Rectangle<float> r (tr.x - tr.w * 0.5f, tr.y - tr.h * 0.5f, tr.w, tr.h);
        g.setColour (col.withAlpha (0.9f));
        if (style == 1)
        {
            const float k = std::min (r.getWidth(), r.getHeight()) * 0.25f;
            for (int q = 0; q < 4; ++q)
            {
                const float x = q % 2 ? r.getRight() : r.getX(), y = q / 2 ? r.getBottom() : r.getY();
                const float sx = q % 2 ? -1.0f : 1.0f, sy = q / 2 ? -1.0f : 1.0f;
                g.drawLine (x, y, x + sx * k, y, lw * 1.5f); g.drawLine (x, y, x, y + sy * k, lw * 1.5f);
            }
        }
        else if (style == 2)
        {
            g.setColour (col.withAlpha (0.35f));
            g.drawLine (0, tr.y, (float) W, tr.y, lw); g.drawLine (tr.x, 0, tr.x, (float) H, lw);
            g.setColour (col.withAlpha (0.9f));
            g.drawEllipse (r.withSizeKeepingCentre (tr.h, tr.h), lw * 1.5f);
        }
        else g.drawRect (r, lw);
        if (style != 2)
        {
            g.fillRect (r.getX(), r.getY() - (float) H / 30.0f, std::min (r.getWidth(), (float) H / 5.0f), (float) H / 30.0f);
            g.setColour (juce::Colours::black);
            g.drawText (juce::String::formatted ("ID%02d %.2f", tr.id, 0.5f + 0.5f * std::min (1.0f, (float) tr.age / 30.0f)),
                        juce::Rectangle<float> (r.getX() + 2, r.getY() - (float) H / 30.0f, (float) H / 5.0f, (float) H / 30.0f), juce::Justification::centredLeft);
        }
        if (style == 3 && i > 0)
        {
            g.setColour (col.withAlpha (0.55f));
            g.drawLine (tracks[i - 1].x, tracks[i - 1].y, tr.x, tr.y, lw);
        }
    }
}

//==============================================================================================================
void Scene::drawMode (Ctx& c, int m)
{
    switch (m)
    {
        case 0: mGlowScope (c); break;   case 1: mXYScope (c); break;   case 2: mRingScope (c); break;   case 3: mShapeScope (c); break;
        case 4: mLava (c, false); break; case 5: mBlob (c); break;      case 6: mLava (c, true); break;
        case 7: mSynthGrid (c); break;   case 8: mTunnel (c); break;    case 9: mRidge (c); break;       case 10: mCopper (c); break;
        case 11: mStarfield (c); break;  case 12: mHyperspace (c); break; case 13: mNebula (c); break;   case 14: mOrbits (c); break;
        case 15: mCodeRain (c); break;   case 16: mGlyphStorm (c); break; case 17: mBinary (c); break;
        case 18: mWash (c); break;       case 19: mSweep (c); break;    case 20: mBlocks (c); break;
        case 21: mStatic (c); break;     case 22: mRoll (c); break;     case 23: mSignalLost (c); break;
        case 24: mBits (c); break;       case 25: mFire (c); break;     case 26: mLife (c); break;       case 27: mMosaic (c); break;
        case 28: mLyric (c); break;      case 29: mGlitchType (c); break; case 30: mMarquee (c); break;  case 31: mWordWall (c); break;
        case 32: mPlasma (c); break;     case 33: mLiquid (c); break;   case 34: mInterference (c); break; case 35: mSwirl (c); break;
        case 36: mBars (c, false); break; case 37: mBars (c, true); break; case 38: mCircle (c); break;  case 39: mBurst (c); break;
        case 40: mWaterfall (c); break;
        case 41: mImgPulse (c); break;   case 42: mImgTiles (c); break; case 43: mImgSlices (c); break;  case 44: mImgPixelate (c); break;
        default: mPlasma (c); break;
    }
}

void Scene::render (juce::Image& dst, const ParamValues& pv, const ana::Snapshot& av, const Assets& as, double dtIn, float timeOffset, float hueOffset)
{
    ensureSize (dst.getWidth(), dst.getHeight());
    const float dt = (float) std::clamp (dtIn, 0.0, 0.25);
    const int mode = std::clamp ((int) pv[pMode], 0, kNumModes - 1);
    if (mode != lastMode) modeChanged (mode);

    float tempoMul = 1.0f;
    if (pv[pSync] > 0.5f && av.bpm > 1.0f) tempoMul = std::clamp (av.bpm / 120.0f, 0.5f, 2.0f);
    t += (double) (dt * pv[pSpeed] * tempoMul);
    const float cyc = pv[pCycle];
    colorPhase += (double) (dt * cyc * cyc * 0.6f * tempoMul);
    spinAngle += dt * pv[pSpin] * 1.2f * tempoMul;
    buildPaletteLut ((int) pv[pPalette], fract (pv[pHue] + hueOffset), lut);

    float peak = 0.02f;
    for (int i = 0; i < ana::Snapshot::kWave; i += 2) peak = std::max (peak, std::abs (av.waveL[(size_t) i]));
    const float target = std::clamp (0.85f / peak, 0.5f, 20.0f);
    scopeGain += (target - scopeGain) * std::min (1.0f, dt * (target < scopeGain ? 12.0f : 2.0f));

    const juce::Image* img = nullptr;
    bool hasImage = false;
    if (! as.images.empty())
    {
        const int n = (int) as.images.size();
        const int cy = std::clamp ((int) pv[pImgCycle], 0, 5);
        int idx = as.selectedImage;
        if (cy > 0) idx = (int) (((long long) std::floor (av.beatPos / (double) kCycleBeats[cy])) % n);
        idx = ((idx % n) + n) % n;
        if (as.images[(size_t) idx].isValid()) { img = &as.images[(size_t) idx]; hasImage = true; }
    }

    Ctx c { pv, av, as, W, H, (float) std::fmod (t + (double) timeOffset, 100000.0), dt, pv[pSpeed] * tempoMul, pv[pDetail], pv[pZoom], spinAngle, pv[pReact],
            clamp01 (av.level), clamp01 (av.bass), clamp01 (av.mid), clamp01 (av.high), clamp01 (av.beat),
            (float) std::fmod (colorPhase, 2.0), lut, scopeGain, nullptr, hasImage, av.beatPos, av.beatCount, av.onset };
    c.img = hasImage ? img : &getPlaceholder (c);

    drawMode (c, mode);

    // trails
    const float trails = pv[pTrails];
    if (trails > 0.01f)
    {
        if (lastQualityW == 0) { copyImage (accum, layer); lastQualityW = 1; }   // (re)start trails from the current frame
        Pix l (layer), a (accum);
        const int s = (int) (trails * 256.0f);
        for (int y = 0; y < H; ++y)
        {
            u32* lr = l.row (y); u32* ar = a.row (y);
            for (int x = 0; x < W; ++x)
            {
                const u32 ac = ar[x];
                const u32 dec = 0xff000000u | ((((ac & 0xff00ffu) * (u32) s) >> 8) & 0xff00ffu) | ((((ac & 0xff00u) * (u32) s) >> 8) & 0xff00u);
                const u32 o = maxc (dec, lr[x]);
                ar[x] = o; lr[x] = o;
            }
        }
    }
    else if (lastQualityW != 0) { lastQualityW = 0; }

    mirrorPass (layer, std::clamp ((int) pv[pMirror], 0, 3));
    bloom (layer, pv[pGlow]);
    if (kModes[mode].cat != cImage) imageLayer (c);

    if (pv[pTextOver] > 0.5f && kModes[mode].cat != cText)
    {
        work2.clear (work2.getBounds());
        {
            juce::Graphics g (work2);
            float prog; int li;
            const auto s = currentText (c, prog, li);
            drawTextAnim (g, c, s, { 0.0f, 0.0f, (float) W, (float) H }, std::clamp ((int) pv[pTextAnim], 0, 6), prog, pv[pTextSize] * 0.8f, pv[pTextGlitch]);
        }
        textGlitchPass (work2, c, pv[pTextGlitch]);
        chromaComposite (work2, layer, 0, 1.0f, 0, c);
    }

    if (pv[pGlsOn] > 0.5f) fxGlass (layer, c, std::clamp ((int) pv[pGlsType], 0, 4), pv[pGlsAmt]);
    if (pv[pTexOn] > 0.5f) fxTexture (layer, c, std::clamp ((int) pv[pTexType], 0, 4), pv[pTexAmt]);
    if (pv[pDitOn] > 0.5f) fxDither (layer, c, std::clamp ((int) pv[pDitType], 0, 4), pv[pDitSize], pv[pDitMix]);
    if (pv[pTrkOn] > 0.5f) fxTrack (layer, c, std::clamp ((int) pv[pTrkStyle], 0, 3), pv[pTrkAmt]);

    const float f = pv[pBright] * (1.0f + pv[pFlash] * c.beat * 0.8f);
    Pix l (layer, juce::Image::BitmapData::readOnly), d (dst, juce::Image::BitmapData::writeOnly);
    if (std::abs (f - 1.0f) < 0.01f)
        for (int y = 0; y < H; ++y) std::memcpy (d.row (y), l.row (y), (size_t) W * 4);
    else
        for (int y = 0; y < H; ++y) { const u32* lr = l.row (y); u32* dr = d.row (y); for (int x = 0; x < W; ++x) dr[x] = mulc (lr[x], f); }
}

//==============================================================================================================
Renderer::Renderer() : main (std::make_unique<Scene>()) {}
Renderer::~Renderer() = default;

void Renderer::reset()
{
    main = std::make_unique<Scene>();
    tiles.clear(); tileImgs.clear();
}

void Renderer::render (juce::Image& out, const ParamValues& pv, const ana::Snapshot& av, const Assets& as, double dt)
{
    const int W = out.getWidth(), H = out.getHeight();
    const int grid = std::clamp ((int) pv[pGrid], 0, kNumGrids - 1);
    if (grid == 0)
        main->render (out, pv, av, as, dt, 0.0f, 0.0f);
    else
    {
        const int cols = kGrids[grid].cols, rows = kGrids[grid].rows, n = cols * rows;
        const int gap = (int) std::round (pv[pGridGap] * 0.03f * (float) H);
        const int tw = std::max (8, (W - gap * (cols + 1)) / cols), th = std::max (8, (H - gap * (rows + 1)) / rows);
        while ((int) tiles.size() < n) tiles.push_back (std::make_unique<Scene>());
        tileImgs.resize ((size_t) n);
        for (auto& im : tileImgs) if (! im.isValid() || im.getWidth() != tw || im.getHeight() != th) im = makeSoftwareImage (tw, th);
        const int link = std::clamp ((int) pv[pGridLink], 0, 5);
        const int renderCount = link == 2 ? 1 : n;
        for (int i = 0; i < renderCount; ++i)
        {
            ParamValues tp = pv;
            int src = i;
            if (link == 5) src = (i + av.beatCount) % n;
            if ((link == 0 || link == 5) && as.tileValid[(size_t) src]) tp = as.tiles[(size_t) src];
            else if ((link == 0 || link == 5) && src > 0)
            {
                // unassigned tiles: an automatic variation of the main preset (different mode, palette spread)
                int m = ((int) pv[pMode] + src * 7) % kNumModes;
                if (kModes[m].cat == cImage && as.images.empty()) m = (m + 11) % kNumModes;
                tp[pMode] = (float) m;
                const int pal = (int) pv[pPalette];
                if (! kPalettes[std::clamp (pal, 0, kNumPalettes - 1)].mono) tp[pPalette] = (float) (5 + (pal - 5 + src * 3 + 1000) % (kNumPalettes - 7));
            }
            tp[pGrid] = 0;
            float off = 0, hue = 0;
            if (link == 3) { off = (float) i * 0.37f; hue = (float) i / (float) n; }
            if (link == 4)
            {
                ana::Snapshot s2 = av;
                const int b = i % 3;
                const float lv = b == 0 ? av.bass : (b == 1 ? av.mid : av.high);
                s2.level = lv; s2.bass = b == 0 ? av.bass : av.bass * 0.4f; s2.beat = b == 0 ? av.beat : av.beat * 0.3f;
                tiles[(size_t) i]->render (tileImgs[(size_t) i], tp, s2, as, dt, (float) i * 0.21f, 0.0f);
            }
            else
                tiles[(size_t) i]->render (tileImgs[(size_t) i], tp, av, as, dt, off, hue);
        }
        Pix d (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y) std::fill (d.row (y), d.row (y) + W, 0xff000000u);
        for (int r = 0; r < rows; ++r)
            for (int cc = 0; cc < cols; ++cc)
            {
                const int i = r * cols + cc;
                juce::Image& ti = tileImgs[(size_t) (link == 2 ? 0 : i)];
                Pix s (ti, juce::Image::BitmapData::readOnly);
                const bool fx = link == 2 && (cc % 2) == 1, fy = link == 2 && (r % 2) == 1;
                const int x0 = gap + cc * (tw + gap), y0 = gap + r * (th + gap);
                for (int y = 0; y < th && y0 + y < H; ++y)
                {
                    const u32* sr = s.row (fy ? th - 1 - y : y);
                    u32* dr = d.row (y0 + y) + x0;
                    const int wmax = std::min (tw, W - x0);
                    if (! fx) std::memcpy (dr, sr, (size_t) wmax * 4);
                    else for (int x = 0; x < wmax; ++x) dr[x] = sr[tw - 1 - x];
                }
            }
    }

    // global strobe / blackout (performance)
    const float fdt = (float) std::clamp (dt, 0.0, 0.25);
    float strobeWhite = 0, dim = 1.0f;
    if (pv[pStrobe] > 0.5f)
    {
        const float rate = kStrobeBeats[std::clamp ((int) pv[pStrobeRate], 0, 3)];
        float phase;
        if (av.bpm > 1.0f) phase = fract ((float) (av.beatPos / (double) rate));
        else { strobeClock = fract (strobeClock + fdt * 2.0f / rate); phase = strobeClock; }
        if (phase < 0.28f) strobeWhite = 0.92f; else dim = 0.12f;
    }
    const float target = pv[pBlackout] > 0.5f ? 1.0f : 0.0f;
    blackoutAmt += (target - blackoutAmt) * std::min (1.0f, fdt / 0.08f);
    if (target > 0.5f && blackoutAmt > 0.995f) blackoutAmt = 1.0f;
    if (target < 0.5f && blackoutAmt < 0.005f) blackoutAmt = 0.0f;
    dim *= (1.0f - blackoutAmt);
    strobeWhite *= (1.0f - blackoutAmt);
    if (strobeWhite > 0 || dim < 0.999f)
    {
        Pix d (out);
        const u32 white = 0xffffffffu;
        for (int y = 0; y < H; ++y)
        {
            u32* r = d.row (y);
            for (int x = 0; x < W; ++x) r[x] = strobeWhite > 0 ? lerpc (r[x], white, strobeWhite) : mulc (r[x], dim);
        }
    }
    lastBeatPos = av.beatPos;
}
} // namespace vz
