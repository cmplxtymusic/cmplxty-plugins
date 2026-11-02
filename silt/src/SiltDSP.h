#pragma once
// CMPLXTY Silt - granular synth engine (plain C++17, no JUCE, header only so the DSP tests build on their own).
//  Granular mode: every voice runs its own cloud of up to 64 grains read from sample A (and B), each grain with its own
//    position (+ spray, Magnet pull to the nearest transient), size, window shape, pitch (+ jitter, scale lock), pan, filter and level.
//    Tune maps the sample's root so the key you play is the pitch you hear ("C plays C"). Cohere locks the grains to the
//    sample's pitch: grain starts snap to cycle starts in the source and grain onsets snap to the output period, and jitter /
//    spray fade out - a noisy cloud firms up into a steady tone.
//  Slice mode: the sample is cut into chops (transients / grid / manual); key per chop from the base note (C1 = 36),
//    One Shot / Gate / Granular playback, and a 16-step sequencer locked to the host grid.
//  Layer B: a second sample mixed per grain - Blend, Swap (random per grain), Ring (A x B) or Cross (B played with A's envelope),
//    optionally key matched to its own root.
//  Mod: 2 LFOs (free or synced), a mod envelope, 4 macros, per-grain random, velocity, note, wheel, MPE pressure / timbre;
//    8 matrix slots. FX rack (reorderable): Beat Repeat, Compressor, Delay, Reverb; then the level and a peak limiter.
#include "SiltAnalysis.h"
#include <atomic>

namespace slt
{
constexpr float kPif = 3.14159265f;
inline float clean (float x) { return (std::isfinite (x) && std::fabs (x) < 1.0e6f) ? (std::fabs (x) < 1.0e-20f ? 0.0f : x) : 0.0f; }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float smooth01 (float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3.0f - 2.0f * x); }   // C1-continuous fade curve
inline float wrap01 (float x) { x -= std::floor (x); return x >= 1.0f ? 0.0f : x; }

constexpr int kMaxVoices = 16, kVoiceSlots = kMaxVoices + 4, kGrains = 64, kSteps = 16, kModSlots = 8, kMacros = 4, kChunk = 32;

struct SVF   // Zavalishin TPT state-variable filter
{
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 2;
    void set (float g, float kk) { k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    float run (int type, float v0) { float l, b, h; tick (v0, l, b, h); return type == 0 ? l : type == 1 ? b * k : type == 2 ? h : v0 - k * b; }  // LP, BP, HP, notch
    void sanitize() { ic1 = clean (ic1); ic2 = clean (ic2); }
    void reset() { ic1 = ic2 = 0; }
};

struct Env   // linear attack (from the current level, so retriggers never click), exponential decay / release (times to -60 dB)
{
    enum St { idle, att, dec, rel } st = idle;
    float v = 0, aInc = 1, dC = 1, rC = 1, S = 1, out = 0, smC = 0.03f;
    void set (float a, float d, float s, float r, float sr)
    {
        smC = 1.0f - std::exp (-1.0f / (0.0007f * sr));   // 0.7 ms smoothing of the output: no corners, so no clicks at any time setting
        aInc = 1.0f / (std::max (0.0005f, a) * sr);
        dC = 1.0f - std::exp (-6.9f / (std::max (0.001f, d) * sr));
        rC = 1.0f - std::exp (-6.9f / (std::max (0.003f, r) * sr));   // 3 ms shortest release
        S = std::clamp (s, 0.0f, 1.0f);
    }
    void on() { st = att; }
    void off() { if (st != idle) st = rel; }
    float tick()
    {
        switch (st)
        {
            case att: v += aInc; if (v >= 1.0f) { v = 1.0f; st = dec; } break;
            case dec: v += (S - v) * dC; break;
            case rel: v -= v * rC; if (v < 1.0e-4f) { v = 0; if (out < 1.0e-5f) { st = idle; out = 0; } } break;
            case idle: v = 0; out = 0; return 0;
        }
        out += (v - out) * smC;
        return out;
    }
};

struct FastRng
{
    uint32_t s = 0x12345678u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float sym() { return uni() * 2.0f - 1.0f; }
};

enum LfoShape { lfoSine = 0, lfoTri, lfoSaw, lfoSquare, lfoSH, lfoSmooth, kNumLfoShapes };
inline const char* lfoShapeChoices() { return "Sine|Triangle|Saw|Square|S&H|Smooth Rnd"; }
inline const char* syncChoices() { return "Free|8 Bars|4 Bars|2 Bars|1 Bar|1/2|1/4|1/8|1/16|1/2T|1/4T|1/8T|1/16T|1/2D|1/4D|1/8D"; }
inline float syncBeats (int i)
{
    static const float b[] = { 0, 32, 16, 8, 4, 2, 1, 0.5f, 0.25f, 4.0f / 3, 2.0f / 3, 1.0f / 3, 1.0f / 6, 3, 1.5f, 0.75f };
    return b[std::clamp (i, 0, 15)];
}
struct Lfo
{
    double ph = 0; float sh = 0, shPrev = 0; FastRng rng;
    void step (double dph) { ph += dph; if (ph >= 1.0) { ph -= std::floor (ph); shPrev = sh; sh = rng.sym(); } }
    void setPhase (double p) { p -= std::floor (p); if (p < ph - 0.5) { shPrev = sh; sh = rng.sym(); } ph = p; }
    float value (int shape) const
    {
        const float p = (float) ph;
        switch (shape)
        {
            case lfoSine:   return std::sin (2.0f * kPif * p);
            case lfoTri:    return p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4;
            case lfoSaw:    return 1 - 2 * p;
            case lfoSquare: return p < 0.5f ? 1.0f : -1.0f;
            case lfoSH:     return sh;
            default:        { const float t = 0.5f - 0.5f * std::cos (kPif * p); return shPrev + (sh - shPrev) * t; }
        }
    }
};

enum ModSrc { msNone = 0, msLfo1, msLfo2, msEnv2, msM1, msM2, msM3, msM4, msRand, msVel, msNote, msWheel, msPress, msTimbre, kNumModSrc };
inline const char* modSrcChoices() { return "-|LFO 1|LFO 2|Mod Env|Macro 1|Macro 2|Macro 3|Macro 4|Random|Velocity|Note|Wheel|Pressure|Timbre"; }
enum ModDst { mdNone = 0, mdPos, mdScan, mdDensity, mdSize, mdPitch, mdPJit, mdSpray, mdSpread, mdShape, mdReverse, mdMagnet, mdCohere, mdMorph, mdCutoff, mdRes, mdLevel, mdPan, kNumModDst };
inline const char* modDstChoices() { return "-|Position|Scan|Density|Size|Pitch|Pitch Jitter|Spray|Spread|Shape|Reverse|Magnet|Cohere|Morph|Cutoff|Resonance|Level|Pan"; }
// per-grain destinations take the per-grain Random value; voice-level ones use the voice's random value drawn at note on
inline bool grainDst (int d) { return d != mdScan && d != mdDensity && d != mdLevel && d != mdNone; }

enum ScaleId { scOff = 0, scMajor, scMinor, scDorian, scPhrygian, scLydian, scMixo, scHarmMinor, scPentMaj, scPentMin, scBlues, scWhole, scFifths, scOctaves, kNumScales };
inline const char* scaleChoices() { return "Off|Major|Minor|Dorian|Phrygian|Lydian|Mixolydian|Harm Minor|Pent Major|Pent Minor|Blues|Whole Tone|Fifths|Octaves"; }
inline const char* keyChoices() { return "C|C#|D|D#|E|F|F#|G|G#|A|A#|B"; }
inline uint16_t scaleMask (int s)
{
    auto m = [] (std::initializer_list<int> l) { uint16_t r = 0; for (int i : l) r |= (uint16_t) (1u << i); return r; };
    switch (s)
    {
        case scMajor: return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case scMinor: return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case scDorian: return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case scPhrygian: return m ({ 0, 1, 3, 5, 7, 8, 10 });
        case scLydian: return m ({ 0, 2, 4, 6, 7, 9, 11 });
        case scMixo: return m ({ 0, 2, 4, 5, 7, 9, 10 });
        case scHarmMinor: return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case scPentMaj: return m ({ 0, 2, 4, 7, 9 });
        case scPentMin: return m ({ 0, 3, 5, 7, 10 });
        case scBlues: return m ({ 0, 3, 5, 6, 7, 10 });
        case scWhole: return m ({ 0, 2, 4, 6, 8, 10 });
        case scFifths: return m ({ 0, 7 });
        case scOctaves: return m ({ 0 });
        default: return 0xFFF;
    }
}
// nearest note of the scale (ties go down); returns a whole semitone
inline float snapScale (float semi, int scale, int key)
{
    if (scale <= scOff) return std::round (semi);
    const uint16_t mask = scaleMask (scale);
    const int c = (int) std::lround (semi);
    float best = (float) c; float bd = 1e9f;
    for (int d = -12; d <= 12; ++d)
    {
        const int n = c + d, pc = ((n - key) % 12 + 12) % 12;
        if (! (mask & (1u << pc))) continue;
        const float dist = std::fabs ((float) n - semi) + (d > 0 ? 1e-4f : 0.0f);
        if (dist < bd) { bd = dist; best = (float) n; }
    }
    return best;
}

enum MorphMode { mmBlend = 0, mmSwap, mmRing, mmCross };
enum Mode { modeGranular = 0, modeSlice };
enum SlPlay { slOneShot = 0, slGate, slGranular };
enum TuneMode { tuneOff = 0, tuneAuto, tuneManual };
enum FxId { fxRepeat = 0, fxComp, fxDelay, fxReverb, kNumFx };
inline const char* fxName (int f) { static const char* n[] = { "REPEAT", "COMP", "DELAY", "REVERB" }; return f >= 0 && f < kNumFx ? n[f] : "?"; }
inline std::array<int, 4> fxPerm (int idx)
{
    std::array<int, 4> pool { 0, 1, 2, 3 }, out {};
    idx = std::clamp (idx, 0, 23);
    static const int fact[] = { 6, 2, 1, 1 };
    int n = 4;
    for (int i = 0; i < 4; ++i) { const int k = idx / fact[i]; idx %= fact[i]; out[(size_t) i] = pool[(size_t) k]; for (int j = k; j < n - 1; ++j) pool[(size_t) j] = pool[(size_t) (j + 1)]; --n; }
    return out;
}
inline int fxPermIndex (const std::array<int, 4>& o)
{
    for (int i = 0; i < 24; ++i) if (fxPerm (i) == o) return i;
    return 0;
}
inline std::string fxPermName (int idx) { auto o = fxPerm (idx); std::string s; for (int i = 0; i < 4; ++i) { if (i) s += " > "; s += fxName (o[(size_t) i]); } return s; }
inline const char* divChoices() { return "1/4|1/8|1/16|1/32|1/8T|1/16T"; }
inline float divBeats (int d) { static const float b[] = { 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f }; return b[std::clamp (d, 0, 5)]; }
inline const char* dlyChoices() { return "1/16|1/8T|1/8|1/8D|1/4T|1/4|1/4D|1/2|1 Bar"; }
inline float dlyBeats (int d) { static const float b[] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 4.0f }; return b[std::clamp (d, 0, 8)]; }

struct Params
{
    // grain
    float pos = 0.25f, scan = 0, density = 24, size = 0.12f, sizeRand = 0.2f, pitch = 0, fine = 0, pitchJit = 0, spray = 0.1f, spread = 0.5f,
          shape = 0.5f, reverse = 0, magnet = 0, gcomp = 0.5f;
    // tune / voice
    int tune = tuneAuto; float root = 60; int scale = scOff, key = 0; float cohere = 0, glide = 0; int voices = 8; float bendRange = 2; bool mpe = false;
    // layer B
    bool bOn = false; float morph = 0.5f; int morphMode = mmBlend; float bOffset = 0, bPitch = 0; bool keyMatch = true;
    // filter
    bool fOn = false; int fType = 0; float cutoff = 6000, res = 0.15f, fRand = 0, fKey = 0;
    // amp
    float aA = 0.02f, aD = 0.4f, aS = 1.0f, aR = 0.6f, velSens = 0.6f, levelDb = 0;
    // mod
    float l1Rate = 0.5f; int l1Sync = 0, l1Shape = lfoSine; float l2Rate = 3.0f; int l2Sync = 0, l2Shape = lfoSmooth;
    float e2A = 0.5f, e2D = 1.0f, e2S = 0.0f, e2R = 0.8f;
    float macro[kMacros] { 0, 0, 0, 0 };
    int modSrc[kModSlots] {}, modDst[kModSlots] {}; float modAmt[kModSlots] {};
    // slice / seq
    int mode = modeGranular, slPlay = slOneShot, slNote = 36;
    bool seq = false; int seqRate = 2, seqLen = 16; float seqSwing = 0, seqGate = 0.6f; int seqTrig = 0;   // 0 Hold Key, 1 Host Play
    // fx
    int fxOrder = 0;
    bool rpOn = false; int rpLen = 2; float rpChance = 0.3f, rpMix = 1.0f;
    bool cpOn = false; float cpThr = -18, cpRatio = 3, cpGain = 3;
    bool dlOn = false; int dlTime = 3; float dlFb = 0.35f, dlMix = 0.25f;
    bool rvOn = true; float rvSize = 0.6f, rvDark = 0.5f, rvMix = 0.25f;
};

struct Transport { bool playing = false; double ppq = 0; double bpm = 120; };

// display taps (written by the audio thread, read by the UI)
struct GrainTap { std::atomic<float> pos { 0 }, pitch { 0 }, amp { 0 }, pan { 0 }; std::atomic<int> layer { 0 }; std::atomic<uint32_t> stamp { 0 }; };
constexpr int kTaps = 512;

class Engine
{
public:
    // ------------------------------------------------------------------ windows
    static constexpr int kWinShapes = 33, kWinLen = 512;
    Engine()
    {
        for (int s = 0; s < kWinShapes; ++s)
        {
            const float sh = (float) s / (kWinShapes - 1);
            double sq = 0, sm = 0;
            for (int i = 0; i <= kWinLen; ++i)
            {
                const float x = (float) i / kWinLen; float w;
                if (sh <= 0.5f)
                {   // Tukey: flat top with raised-cosine edges; 0 = 10 % edges, 0.5 = Hann
                    const float r = 0.1f + 0.9f * (sh / 0.5f), e = r * 0.5f;
                    w = x < e ? 0.5f - 0.5f * std::cos (kPif * x / e) : x > 1 - e ? 0.5f - 0.5f * std::cos (kPif * (1 - x) / e) : 1.0f;
                }
                else
                {   // percussive: short attack, curved decay
                    const float u = (sh - 0.5f) / 0.5f, a = 0.5f * (1 - u) + 0.015f * u;
                    if (x < a) w = 0.5f - 0.5f * std::cos (kPif * x / a);
                    else { const float d = (x - a) / (1 - a); w = std::pow (0.5f + 0.5f * std::cos (kPif * d), 1.0f + 2.0f * u); }
                }
                win[(size_t) s][(size_t) i] = w; if (i < kWinLen) { sq += (double) w * w; sm += w; }
            }
            winPow[(size_t) s] = (float) (sq / kWinLen); winMean[(size_t) s] = (float) (sm / kWinLen);
        }
        for (int i = 0; i < kVoiceSlots; ++i) { voice[(size_t) i].rng.s = 0x9e3779b9u * (uint32_t) (i + 1); voice[(size_t) i].id = i; }
        held.reserve (128);
    }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        dlyL.assign ((size_t) (sr * 6.2f) + 4, 0.0f); dlyR.assign (dlyL.size(), 0.0f);
        rpL.assign ((size_t) (sr * 2.1f) + 4, 0.0f); rpR.assign (rpL.size(), 0.0f);
        limLa = std::max (8, (int) std::lround (0.001 * sr)); limL.assign ((size_t) limLa, 0.0f); limR.assign ((size_t) limLa, 0.0f); limA.assign ((size_t) limLa, 0.0f);
        static const int base[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        for (int i = 0; i < 8; ++i) { rv[(size_t) i].buf.assign ((size_t) (base[i] * 2.2f * sr / 44100.0f) + 8, 0.0f); rv[(size_t) i].base = base[i] * sr / 44100.0f; }
        reset();
    }
    void reset()
    {
        for (auto& v : voice) { v.state = 0; v.active = 0; for (auto& g : v.g) g.on = false; }
        held.clear(); sustain = false; bend = 0; wheel = 0; press = 0; timbre = 0; lastPitch = -1;
        for (int c = 0; c < 16; ++c) { chBend[c] = 0; chPress[c] = 0; chTimbre[c] = 0; }
        lfo1 = Lfo(); lfo2 = Lfo(); lfo2.rng.s = 0xabcdef01u;
        std::fill (dlyL.begin(), dlyL.end(), 0.0f); std::fill (dlyR.begin(), dlyR.end(), 0.0f); dlyW = 0; dlyLp[0] = dlyLp[1] = 0; dTime = -1;
        for (auto& l : rv) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.w = 0; l.lp = 0; }
        rpActive = rpWas = false; rpCap = rpLoop = rpPlay = rpTailLen = rpTailPos = 0; rpG = 0; rpBeat = -1; cpEnv = 0; lim = 1;
        std::fill (limL.begin(), limL.end(), 0.0f); std::fill (limR.begin(), limR.end(), 0.0f); std::fill (limA.begin(), limA.end(), 0.0f); limW = 0;
        freeBeats = 0; seqBeats = 0; seqLastStep = -1; seqRunning = false; seqWasHost = false;
        counter = 0;
    }

    int latency() const { return limLa; }
    void setParams (const Params& np) { P = np; }
    // sources may change at block boundaries; grains of the old source fade out over 5 ms (the caller keeps it alive until unused())
    void setSources (const Source* a, const Source* b)
    {
        if (a != srcA || b != srcB)
            for (auto& v : voice) for (auto& g : v.g) if (g.on && (g.sa != a || g.sb != b)) kill (g);
        srcA = (a && a->len() > 8) ? a : nullptr; srcB = (b && b->len() > 8) ? b : nullptr;
    }
    bool uses (const Source* s) const
    {
        if (! s) return false;
        for (auto& v : voice) if (v.state) for (auto& g : v.g) if (g.on && (g.sa == s || g.sb == s)) return true;
        return false;
    }
    void setSlices (const int* starts, int count)
    {
        nSlices = std::clamp (count, 0, kMaxSlices);
        for (int i = 0; i < nSlices; ++i) slice[(size_t) i] = starts[i];
    }
    void setPattern (const int* steps) { for (int i = 0; i < kSteps; ++i) pattern[(size_t) i] = steps[i]; }

    // ------------------------------------------------------------------ MIDI
    void noteOn (int ch, int note, float vel)
    {
        ch = std::clamp (ch, 0, 15);
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        held.push_back (note);
        if (seqActive()) { heldVel = vel; return; }        // keys hold / transpose the sequencer
        if (P.mode == modeSlice)
        {
            const int idx = note - P.slNote;
            if (idx < 0 || idx >= nSlices) return;
            startVoice (ch, note, vel, idx, 0, 0, false);
            return;
        }
        if (P.voices <= 1)
        {
            for (auto& v : voice)
                if (v.state && ! v.fading && v.amp.st != Env::rel && ! v.seq)
                { v.note = note; v.ch = ch; v.pitchTarget = (float) note; if (P.glide <= 0.0005f) v.pitchCur = (float) note; v.vel = vel; v.held = true; v.sustained = false; lastPitch = (float) note; return; }
        }
        startVoice (ch, note, vel, -1, 0, 0, false);
    }
    void noteOff (int ch, int note)
    {
        ch = std::clamp (ch, 0, 15);
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (seqActive()) return;
        for (auto& v : voice)
        {
            if (! v.state || v.seq || v.note != note || ! v.held) continue;
            if (P.mpe && v.ch != ch) continue;
            if (P.voices <= 1 && P.mode == modeGranular && ! held.empty())
            {   // legato: back to the last key still held
                const int back = held.back(); v.note = back; v.pitchTarget = (float) back; if (P.glide <= 0.0005f) v.pitchCur = (float) back; lastPitch = (float) back; continue;
            }
            v.held = false;
            if (sustain) v.sustained = true; else release (v);
        }
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on) for (auto& v : voice) if (v.state && v.sustained) { v.sustained = false; release (v); }
    }
    void pitchBend (int ch, float v) { ch = std::clamp (ch, 0, 15); chBend[ch] = v; bend = v; }
    void channelPressure (int ch, float v) { ch = std::clamp (ch, 0, 15); chPress[ch] = v; press = v; }
    void polyPressure (int ch, int note, float v) { for (auto& vc : voice) if (vc.state && vc.note == note && (! P.mpe || vc.ch == ch)) vc.polyPress = v; }
    void controller (int ch, int num, float v)
    {
        ch = std::clamp (ch, 0, 15);
        if (num == 1) wheel = v;
        else if (num == 74) { chTimbre[ch] = v; timbre = v; }
        else if (num == 64) setSustain (v >= 0.5f);
        else if (num == 120 || num == 123) allNotesOff (num == 120);
    }
    void allNotesOff (bool hard)
    {
        held.clear(); sustain = false; seqRunning = false;
        for (auto& v : voice) if (v.state) { if (hard) { v.state = 0; for (auto& g : v.g) g.on = false; v.active = 0; } else { v.held = false; v.sustained = false; release (v); } }
    }

    // ------------------------------------------------------------------ render
    void render (float* L, float* R, int n, const Transport& tr)
    {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0;
        const double spb = 60.0 / std::max (20.0, tr.bpm) * sr;   // samples per beat
        bpmNow = (float) std::max (20.0, tr.bpm);
        int done = 0;
        while (done < n)
        {
            const int m = std::min (kChunk, n - done);
            const double beat0 = tr.playing ? tr.ppq + done / spb : freeBeats;
            control (m, tr.playing, beat0, spb);
            if (seqActive()) runSeq (m, tr.playing, beat0, spb);
            for (auto& v : voice) if (v.state) renderVoice (v, L + done, R + done, m);
            done += m;
            if (! tr.playing) freeBeats += m / spb;
            counter += (uint64_t) m;
        }
        if (tr.playing) freeBeats = tr.ppq + n / spb;
        post (L, R, n, tr, spb);
        for (int i = 0; i < kVoiceSlots; ++i) voicePos[(size_t) i].store (voice[(size_t) i].state ? voice[(size_t) i].dispPos : -1.0f, std::memory_order_relaxed);
    }

    // test / display probe: glide position of the newest sounding voice, and how many are sounding (not fading out)
    float newestPitch (int* live = nullptr) const { const Voice* b = nullptr; int n = 0; for (auto& v : voice) if (v.state && ! v.fading) { ++n; if (! b || v.age > b->age) b = &v; } if (live) *live = n; return b ? b->pitchCur : -1.0f; }
    int activeVoices() const { int c = 0; for (auto& v : voice) c += v.state && ! v.fading ? 1 : 0; return c; }
    int activeGrains() const { int c = 0; for (auto& v : voice) if (v.state) c += v.active; return c; }
    bool seqActive() const { return P.seq && P.mode == modeSlice && nSlices > 0; }

    Params P;
    float sr = 48000;
    // display
    std::array<GrainTap, kTaps> taps;
    std::atomic<int> tapPos { 0 };
    std::array<std::atomic<float>, kVoiceSlots> voicePos {};
    std::atomic<float> outPeak { 0 };
    std::atomic<int> seqStepNow { -1 };
    std::atomic<float> lfoOut[2] {};
    uint64_t counter = 0;

private:
    struct Grain
    {
        bool on = false, flat = false, filt = false, stereoA = false, stereoB = false;
        int wait = 0, len = 1, t = 0, fade = 1, layer = 0, ftype = 0;
        const Source* sa = nullptr; const Source* sb = nullptr;
        double pa = 0, ia = 0, pb = 0, ib = 0;
        float gl = 1, gr = 1, ma = 1, mb = 0, refB = 1, kill = 1, killInc = 0, wPos = 0, wInc = 0;
        int lim0 = 0, lim1 = 0;                // flat (slice) grains stay inside their slice
        const float* win = nullptr;
        SVF fl, fr;
    };
    struct Voice
    {
        int id = 0, state = 0, note = 60, ch = 0, active = 0, sliceIdx = -1, s0 = 0, s1 = 0, gateLeft = 0;
        bool held = false, sustained = false, seq = false, fading = false, oneShotDone = false;
        float vel = 1, pitchCur = 60, pitchTarget = 60, transpose = 0, polyPress = 0, rnd = 0, dispPos = 0;
        uint64_t age = 0;
        Env amp, env2;
        double next = 0, cphase = 0, scan = 0;
        FastRng rng;
        std::array<Grain, kGrains> g;
        float mod[kNumModDst] {};
    };
    struct RvLine { std::vector<float> buf; float base = 1000; int w = 0; float lp = 0; };

    std::array<std::array<float, kWinLen + 1>, kWinShapes> win {};
    std::array<float, kWinShapes> winPow {}, winMean {};
    std::array<Voice, kVoiceSlots> voice;
    const Source* srcA = nullptr; const Source* srcB = nullptr;
    std::array<int, kMaxSlices> slice {}; int nSlices = 0;
    std::array<int, kSteps> pattern { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    std::vector<int> held;
    bool sustain = false;
    float bend = 0, wheel = 0, press = 0, timbre = 0, lastPitch = -1, heldVel = 0.8f, bpmNow = 120;
    float chBend[16] {}, chPress[16] {}, chTimbre[16] {};
    Lfo lfo1, lfo2; float lv1 = 0, lv2 = 0;
    uint64_t ageCtr = 0;
    double freeBeats = 0, seqBeats = 0; int64_t seqLastStep = -1; bool seqRunning = false, seqWasHost = false;
    // fx
    std::vector<float> dlyL, dlyR, rpL, rpR; int dlyW = 0; float dlyLp[2] {}, dTime = -1;
    std::array<RvLine, 8> rv;
    bool rpActive = false, rpWas = false; int rpCap = 0, rpLoop = 0, rpPlay = 0, rpTailLen = 0, rpTailPos = 0; float rpG = 0; int64_t rpBeat = -1; FastRng rpRng;
    std::array<float, 1024> rpTL {}, rpTR {};
    float cpEnv = 0, lim = 1;
    std::vector<float> limL, limR, limA; int limLa = 48, limW = 0;   // 1 ms look-ahead peak limiter

    static float bendRangeMpe() { return 48.0f; }

    void kill (Grain& g) const { if (g.killInc <= 0) g.killInc = 1.0f / (0.005f * sr); }

    float rootA() const { return P.tune == tuneOff ? 60.0f : P.tune == tuneManual ? P.root : (srcA ? srcA->root : 60.0f); }
    float rootB() const { return P.tune == tuneOff ? 60.0f : (srcB ? srcB->root : 60.0f); }

    void release (Voice& v)
    {
        if (P.mode == modeSlice && P.slPlay == slOneShot && v.sliceIdx >= 0) return;   // one shots ignore note off
        v.amp.off(); v.env2.off();
    }

    void startVoice (int ch, int note, float vel, int sliceIdx, int delay, float transpose, bool fromSeq)
    {
        // voice limit: steal the oldest (released first)
        const int limit = std::clamp (P.voices, 1, kMaxVoices);
        int busy = 0; for (auto& v : voice) busy += v.state && ! v.fading ? 1 : 0;
        if (busy >= limit)
        {
            Voice* st = nullptr;
            for (auto& v : voice) if (v.state && ! v.fading && v.amp.st == Env::rel && (! st || v.age < st->age)) st = &v;
            if (! st) for (auto& v : voice) if (v.state && ! v.fading && (! st || v.age < st->age)) st = &v;
            if (st) { st->fading = true; st->amp.rC = 1.0f - std::exp (-6.9f / (0.012f * sr)); st->amp.st = Env::rel; }
        }
        Voice* v = nullptr;
        for (auto& x : voice) if (! x.state) { v = &x; break; }
        if (! v) { for (auto& x : voice) if (x.fading && (! v || x.age < v->age)) v = &x; if (v) { for (auto& g : v->g) g.on = false; v->active = 0; } }
        if (! v) return;
        v->state = 1; v->note = note; v->ch = ch; v->vel = vel; v->held = ! fromSeq; v->sustained = false; v->seq = fromSeq; v->fading = false;
        v->age = ++ageCtr; v->sliceIdx = sliceIdx; v->transpose = transpose; v->polyPress = 0; v->oneShotDone = false;
        v->pitchTarget = (float) note;
        v->pitchCur = (P.glide > 0.0005f && lastPitch >= 0 && P.mode == modeGranular) ? lastPitch : (float) note;
        lastPitch = (float) note;
        v->amp.v = 0; v->amp.out = 0; v->amp.set (P.aA, P.aD, P.aS, P.aR, sr); v->amp.on();
        if (sliceIdx >= 0 && P.slPlay != slGranular) { v->amp.set (std::min (P.aA, 0.004f), 10.0f, 1.0f, P.aR, sr); v->amp.v = 0; v->amp.on(); }
        v->env2.v = 0; v->env2.out = 0; v->env2.set (P.e2A, P.e2D, P.e2S, P.e2R, sr); v->env2.on();
        v->next = delay; v->cphase = 0; v->scan = 0; v->rnd = v->rng.sym();
        v->active = 0; for (auto& g : v->g) g.on = false;
        v->gateLeft = 0;
        if (sliceIdx >= 0 && srcA)
        {
            v->s0 = slice[(size_t) sliceIdx];
            v->s1 = sliceIdx + 1 < nSlices ? slice[(size_t) (sliceIdx + 1)] : srcA->len();
            if (v->s1 <= v->s0 + 16) v->s1 = std::min (srcA->len(), v->s0 + 16);
            if (P.slPlay != slGranular) spawnSliceGrain (*v, delay);
        }
        v->dispPos = srcA ? (float) (sliceIdx >= 0 ? (double) v->s0 / srcA->len() : P.pos) : 0.0f;
    }

    // ------------------------------------------------------------------ modulation
    float srcValue (const Voice& v, int s) const
    {
        switch (s)
        {
            case msLfo1: return lv1;
            case msLfo2: return lv2;
            case msEnv2: return v.env2.v;
            case msM1: case msM2: case msM3: case msM4: return P.macro[s - msM1];
            case msRand: return v.rnd;
            case msVel: return v.vel;
            case msNote: return std::clamp (((float) v.note - 60.0f) / 48.0f, -1.0f, 1.0f);
            case msWheel: return wheel;
            case msPress: return std::max (v.polyPress, P.mpe ? chPress[v.ch] : press);
            case msTimbre: return P.mpe ? chTimbre[v.ch] : timbre;
            default: return 0;
        }
    }

    void control (int m, bool playing, double beat0, double spb)
    {
        auto lfoStep = [&] (Lfo& l, int sync, float rate)
        {
            if (sync > 0)
            {
                const double beats = syncBeats (sync);
                if (playing) l.setPhase (beat0 / beats); else l.step (m / (beats * spb));
            }
            else l.step ((double) rate * m / sr);
        };
        lfoStep (lfo1, P.l1Sync, P.l1Rate); lfoStep (lfo2, P.l2Sync, P.l2Rate);
        lv1 = lfo1.value (P.l1Shape); lv2 = lfo2.value (P.l2Shape);
        lfoOut[0].store (lv1, std::memory_order_relaxed); lfoOut[1].store (lv2, std::memory_order_relaxed);
        for (auto& v : voice)
        {
            if (! v.state) continue;
            for (int d = 0; d < kNumModDst; ++d) v.mod[d] = 0;
            for (int k = 0; k < kModSlots; ++k)
            {
                const int s = P.modSrc[k], d = P.modDst[k];
                if (s <= msNone || d <= mdNone || s >= kNumModSrc || d >= kNumModDst || P.modAmt[k] == 0) continue;
                v.mod[d] += srcValue (v, s) * P.modAmt[k];
            }
        }
    }

    // ------------------------------------------------------------------ sequencer
    void runSeq (int m, bool playing, double beat0, double spb)
    {
        const bool keys = ! held.empty();
        const bool hostRun = P.seqTrig == 1 && playing;
        const bool run = hostRun || keys;
        if (! run) { seqRunning = false; seqStepNow.store (-1, std::memory_order_relaxed); return; }
        const double stepB = divBeats (P.seqRate);
        const int len = std::clamp (P.seqLen, 1, kSteps);
        // clock: the host grid while it plays, otherwise our own (starts at the key press)
        if (! playing && (! seqRunning || seqWasHost)) seqBeats = 0;
        const double b0 = playing ? beat0 : seqBeats;
        const double b1 = b0 + m / spb;
        const double sw = std::clamp (P.seqSwing, 0.0f, 0.75f) * stepB * 0.5;
        const int64_t firstK = (int64_t) std::ceil (b0 / stepB - 1e-9);
        // (re)start, or the host jumped (loop / locate): continue from the step at the playhead
        if (! seqRunning || seqWasHost != playing || seqLastStep >= firstK + 2 || seqLastStep < firstK - 2) seqLastStep = firstK - 1;
        seqRunning = true; seqWasHost = playing;
        for (int64_t k = seqLastStep + 1; ; ++k)
        {
            const double t = k * stepB + ((k & 1) ? sw : 0.0);
            if (t >= b1) break;
            seqLastStep = k;
            const int stepIdx = (int) (((k % len) + len) % len);
            seqStepNow.store (stepIdx, std::memory_order_relaxed);
            const int sl = pattern[(size_t) stepIdx];
            if (sl < 0) continue;
            const int off = std::clamp ((int) std::lround ((t - b0) * spb), 0, m - 1);
            const float tr = keys ? (float) (held.back() - 60) : 0.0f;
            // a new step chokes the previous one (like a sampler in mono-choke mode), with a short fade
            for (auto& v : voice) if (v.state && v.seq && ! v.fading) { v.fading = true; v.amp.rC = 1.0f - std::exp (-6.9f / (0.006f * sr)); v.amp.st = Env::rel; }
            startVoice (0, 60 + (int) tr, keys ? heldVel : 0.8f, sl % nSlices, off, tr, true);
            for (auto& v : voice) if (v.state && v.seq && v.age == ageCtr) v.gateLeft = off + (int) (std::clamp (P.seqGate, 0.05f, 1.0f) * stepB * spb);
        }
        if (! playing) seqBeats = b1;
    }

    // ------------------------------------------------------------------ grains
    float pitchOf (const Voice& v) const
    {
        const float bendSemis = P.mpe ? (v.ch == 0 ? chBend[0] * P.bendRange : chBend[v.ch] * bendRangeMpe() + chBend[0] * P.bendRange) : bend * P.bendRange;
        return v.pitchCur + bendSemis;
    }

    void spawnSliceGrain (Voice& v, int delay)
    {
        const Source* A = srcA; if (! A) return;
        Grain* g = nullptr; for (auto& x : v.g) if (! x.on) { g = &x; break; }
        if (! g) return;
        const auto& mod = v.mod;
        const float bendSemis = pitchOf (v) - v.pitchCur;
        const float semis = v.transpose + P.pitch + P.fine / 100.0f + bendSemis + mod[mdPitch] * 24.0f;
        const double ratio = std::pow (2.0, semis / 12.0) * A->audio->rate / sr;
        const bool rev = P.reverse + mod[mdReverse] >= 0.5f;
        const int srcLen = v.s1 - v.s0;
        *g = Grain();
        g->on = true; g->flat = true; g->sa = A; g->sb = nullptr; g->stereoA = A->audio->stereo();
        g->wait = delay; g->t = 0;
        g->len = std::max (8, (int) (srcLen / ratio)); g->fade = std::max (4, std::min (g->len / 3, (int) (0.002f * sr)));
        g->pa = rev ? v.s1 - 1.0 : (double) v.s0; g->ia = rev ? -ratio : ratio;
        g->lim0 = v.s0; g->lim1 = v.s1;
        const float pan = std::clamp (mod[mdPan], -1.0f, 1.0f);
        g->gl = std::cos ((pan + 1) * kPif / 4) * 1.41421356f; g->gr = std::sin ((pan + 1) * kPif / 4) * 1.41421356f;
        setFilter (*g, v, mod[mdCutoff], mod[mdRes], 0.0f);
        g->layer = 0; g->ma = 1;
        ++v.active;
        pushTap ((float) v.s0 / A->len(), semis, 1.0f, pan, 0);
    }

    void setFilter (Grain& g, const Voice& v, float modCut, float modRes, float rndCut)
    {
        g.filt = P.fOn;
        if (! g.filt) return;
        float fc = P.cutoff * std::pow (2.0f, modCut * 6.0f + rndCut + P.fKey * ((float) v.note - 60.0f) / 12.0f);
        fc = std::clamp (fc, 20.0f, 0.45f * sr);
        const float res = std::clamp (P.res + modRes, 0.0f, 1.0f);
        const float gg = std::tan (kPif * fc / sr), k = 2.0f - 1.96f * res;
        g.fl.reset(); g.fr.reset(); g.fl.set (gg, k); g.fr.set (gg, k); g.ftype = P.fType;
    }

    void pushTap (float pos, float pitch, float amp, float pan, int layer)
    {
        const int i = tapPos.load (std::memory_order_relaxed);
        auto& t = taps[(size_t) i];
        t.pos.store (pos, std::memory_order_relaxed); t.pitch.store (pitch, std::memory_order_relaxed); t.amp.store (amp, std::memory_order_relaxed);
        t.pan.store (pan, std::memory_order_relaxed); t.layer.store (layer, std::memory_order_relaxed);
        t.stamp.store ((uint32_t) (counter & 0xffffffffu), std::memory_order_relaxed);
        tapPos.store ((i + 1) % kTaps, std::memory_order_release);
    }

    void spawnGrain (Voice& v, int offset, float norm, double Tout)
    {
        const Source* A = srcA; if (! A) return;
        Grain* g = nullptr; for (auto& x : v.g) if (! x.on) { g = &x; break; }
        if (! g) return;
        const auto& vm = v.mod;
        // per-grain random modulation replaces the voice's random value
        float mod[kNumModDst]; for (int d = 0; d < kNumModDst; ++d) mod[d] = vm[d];
        const float gr = v.rng.sym();
        for (int k = 0; k < kModSlots; ++k)
            if (P.modSrc[k] == msRand && grainDst (P.modDst[k])) mod[P.modDst[k]] += (gr - v.rnd) * P.modAmt[k];
        const float coh = std::clamp (P.cohere + mod[mdCohere], 0.0f, 1.0f), loose = 1.0f - coh;
        const bool inSlice = v.sliceIdx >= 0;
        const int lenA = A->len();
        // pitch
        float semis;
        const float bendSemis = pitchOf (v) - v.pitchCur;
        const float jit = std::max (0.0f, P.pitchJit + mod[mdPJit] * 12.0f) * loose * v.rng.sym();
        if (inSlice) semis = 60.0f + v.transpose + P.pitch + jit + P.fine / 100.0f + bendSemis + mod[mdPitch] * 24.0f;
        else
        {
            float base = v.pitchCur + P.pitch + mod[mdPitch] * 24.0f + jit;
            if (P.scale > scOff) base = snapScale (base, P.scale, P.key);
            semis = base + P.fine / 100.0f + bendSemis;
        }
        const float rA = inSlice ? 60.0f : rootA();
        const double ratio = std::pow (2.0, (semis - rA) / 12.0) * A->audio->rate / sr;
        // size
        float sizeS = P.size * std::pow (2.0f, mod[mdSize] * 3.0f) * (1.0f + std::clamp (P.sizeRand, 0.0f, 1.0f) * 0.75f * v.rng.sym());
        sizeS = std::clamp (sizeS, 0.004f, 1.0f);
        const int outLen = std::max (16, (int) (sizeS * sr));
        const double srcLen = outLen * ratio;
        // position
        const int r0 = inSlice ? v.s0 : 0, r1 = inSlice ? v.s1 : lenA;
        const int span = std::max (1, r1 - r0);
        float posN = wrap01 (P.pos + mod[mdPos] + (float) v.scan);
        double start = r0 + (double) posN * span;
        const float spray = std::clamp (P.spray + mod[mdSpray], 0.0f, 1.0f) * loose;
        start += (double) spray * spray * std::min ((double) span, 2.0 * A->audio->rate) * 0.5 * v.rng.sym();
        const float mag = std::clamp (P.magnet + mod[mdMagnet], 0.0f, 1.0f);
        if (mag > 0)
        {
            const int o = A->nearestOnset (start);
            if (o >= 0) start += mag * ((o - 0.003 * A->audio->rate) - start);
        }
        if (coh > 0)
        {
            const int mk = A->nearestMark (start);
            if (mk >= 0) start += coh * (mk - start);
            else { const double Ps = A->audio->rate / (440.0 * std::pow (2.0, (rA - 69.0) / 12.0)); start += coh * (std::round (start / Ps) * Ps - start); }
        }
        if (srcLen < span - 2) start = std::clamp (start, (double) r0, r1 - srcLen - 2);
        else start = std::clamp (start, (double) r0, (double) std::max (r0, r1 - 2));
        if (inSlice && srcLen >= span - 2) start = r0;
        const bool rev = v.rng.uni() < std::clamp (P.reverse + mod[mdReverse], 0.0f, 1.0f);
        // onset: Cohere moves it onto the next output period boundary
        int wait = offset;
        if (coh > 0 && Tout > 1)
        {
            const double ph = v.cphase + offset / Tout;
            wait += (int) std::lround (coh * (std::ceil (ph) - ph) * Tout);
        }
        *g = Grain();
        g->on = true; g->sa = A; g->stereoA = A->audio->stereo(); g->wait = wait; g->len = outLen; g->t = 0;
        const int shp = std::clamp ((int) std::lround (std::clamp (P.shape + mod[mdShape], 0.0f, 1.0f) * (kWinShapes - 1)), 0, kWinShapes - 1);
        g->win = win[(size_t) shp].data(); g->wPos = 0; g->wInc = (float) kWinLen / (float) outLen;
        g->pa = rev ? start + srcLen : start; g->ia = rev ? -ratio : ratio;
        g->lim0 = 0; g->lim1 = lenA;
        // level: overlap normalisation (incoherent sum by power, coherent sum by amplitude), window loudness, grain level compensation
        float gain = norm / std::sqrt (std::max (0.05f, winPow[(size_t) shp] / 0.375f)) * (1.0f - coh) + norm * (0.5f / std::max (0.05f, winMean[(size_t) shp])) * coh;
        if (P.gcomp > 0)
        {
            const float lvl = std::max (1e-4f, A->rmsAt (start + srcLen * 0.5));
            const float f = std::clamp (A->rmsRef / lvl, 0.25f, 4.0f);
            gain *= 1.0f + P.gcomp * (f - 1.0f);
        }
        const float pan = std::clamp (std::clamp (P.spread + mod[mdSpread], 0.0f, 1.0f) * v.rng.sym() + mod[mdPan], -1.0f, 1.0f);
        g->gl = std::cos ((pan + 1) * kPif / 4) * 1.41421356f * gain; g->gr = std::sin ((pan + 1) * kPif / 4) * 1.41421356f * gain;
        // layer B
        int layer = 0;
        const Source* B = srcB;
        if (P.bOn && B)
        {
            const float mo = std::clamp (P.morph + mod[mdMorph], 0.0f, 1.0f);
            const int mm = P.morphMode;
            const int lenB = B->len();
            double ratioB = P.keyMatch ? std::pow (2.0, (semis + P.bPitch - rootB()) / 12.0) * B->audio->rate / sr
                                       : ratio * (B->audio->rate / A->audio->rate) * std::pow (2.0, P.bPitch / 12.0);
            double sb = (double) wrap01 (posN + P.bOffset) * lenB + (start - (r0 + (double) posN * span)) * (double) lenB / std::max (1, lenA);
            if (coh > 0) { const int mk = B->nearestMark (sb); if (mk >= 0) sb += coh * (mk - sb); }
            if (mag > 0) { const int o = B->nearestOnset (sb); if (o >= 0) sb += mag * ((o - 0.003 * B->audio->rate) - sb); }
            const double sLenB = outLen * ratioB;
            sb = sLenB < lenB - 2 ? std::clamp (sb, 0.0, lenB - sLenB - 2) : 0.0;
            if (mm == mmSwap) layer = v.rng.uni() < mo ? 1 : 0;
            else layer = mm == mmBlend ? 2 : mm == mmRing ? 3 : 4;
            if (layer != 0)
            {
                g->sb = B; g->stereoB = B->audio->stereo();
                g->pb = rev ? sb + sLenB : sb; g->ib = rev ? -ratioB : ratioB;
                if (layer == 2) { g->ma = std::cos (mo * kPif * 0.5f); g->mb = std::sin (mo * kPif * 0.5f); }
                else if (layer == 1) { g->ma = 0; g->mb = 1; g->sa = A; }
                else { g->ma = 1 - mo; g->mb = mo; }
                g->refB = layer == 3 ? 1.0f / std::max (0.02f, B->rmsRef * 1.41f) : layer == 4 ? 1.0f / std::max (0.01f, B->rmsRef) / std::max (0.01f, A->rmsRef) : 1.0f;
                if (layer == 4) g->refB = 1.0f / std::max (0.01f, B->rmsRef);   // B * envA / refB (envA read per sample)
            }
        }
        g->layer = layer;
        // filter: random cutoff spread per grain (+- 3 octaves at full)
        setFilter (*g, v, mod[mdCutoff], mod[mdRes], P.fRand * 3.0f * v.rng.sym());
        ++v.active;
        pushTap ((float) (start / std::max (1, lenA)), semis - rA, gain, pan, layer == 1 ? 1 : layer >= 2 ? 2 : 0);
    }

    static inline float rd (const std::vector<float>& d, double p, int n)
    {
        if (p < 0 || p >= n - 1) return 0.0f;
        const int i = (int) p; const float f = (float) (p - i);
        return d[(size_t) i] + (d[(size_t) (i + 1)] - d[(size_t) i]) * f;
    }

    void renderGrain (Grain& g, float* vl, float* vr, int m)
    {
        // specialised loops: no per-sample branches for the features a grain does not use
        if (g.flat) { if (g.filt) renderGrainT<true, false, true> (g, vl, vr, m); else renderGrainT<true, false, false> (g, vl, vr, m); }
        else if (g.layer) { if (g.filt) renderGrainT<false, true, true> (g, vl, vr, m); else renderGrainT<false, true, false> (g, vl, vr, m); }
        else { if (g.filt) renderGrainT<false, false, true> (g, vl, vr, m); else renderGrainT<false, false, false> (g, vl, vr, m); }
    }

    template <bool Flat, bool Layer, bool Filt>
    void renderGrainT (Grain& g, float* vl, float* vr, int m)
    {
        int i = 0;
        if (g.wait > 0) { if (g.wait >= m) { g.wait -= m; return; } i = g.wait; g.wait = 0; }
        const Audio& a = *g.sa->audio; const int na = a.len();
        const float* aL = a.L.data(); const float* aR = a.stereo() ? a.R.data() : a.L.data();
        const Audio* b = Layer ? g.sb->audio.get() : nullptr;
        const int nb = Layer ? b->len() : 0;
        const float* bL = Layer ? b->L.data() : nullptr; const float* bR = Layer ? (b->stereo() ? b->R.data() : b->L.data()) : nullptr;
        const bool stA = g.stereoA, stB = Layer && g.stereoB, stF = stA || stB;
        const int left = std::min (m - i, g.len - g.t);
        const int end = i + left;
        const float* win = g.win;
        double pa = g.pa, pb = g.pb; const double ia = g.ia, ib = g.ib;
        float wPos = g.wPos; const float wInc = g.wInc;
        const bool killing = g.killInc > 0;
        for (; i < end; ++i)
        {
            float w;
            if constexpr (Flat) { const int t = g.t + (i - (end - left)); w = smooth01 (std::min ((float) (t + 1) / g.fade, (float) (g.len - t) / g.fade)); }
            else { const int wi = std::min ((int) wPos, kWinLen - 1); const float wf = wPos - (float) wi; w = win[wi] + (win[wi + 1] - win[wi]) * wf; wPos += wInc; }
            if (killing) { g.kill -= g.killInc; if (g.kill <= 0) { g.on = false; break; } w *= smooth01 (g.kill); }
            float l = 0, r = 0;
            if (! Flat || (pa >= g.lim0 && pa < g.lim1))
            {
                if (pa >= 0 && pa < na - 1)
                {
                    const int k = (int) pa; const float f = (float) (pa - k);
                    l = aL[k] + (aL[k + 1] - aL[k]) * f;
                    r = stA ? aR[k] + (aR[k + 1] - aR[k]) * f : l;
                }
            }
            if constexpr (Layer)
            {
                float bl = 0, br = 0;
                if (pb >= 0 && pb < nb - 1) { const int k = (int) pb; const float f = (float) (pb - k); bl = bL[k] + (bL[k + 1] - bL[k]) * f; br = stB ? bR[k] + (bR[k + 1] - bR[k]) * f : bl; }
                switch (g.layer)
                {
                    case 1: l = bl; r = br; break;
                    case 2: l = l * g.ma + bl * g.mb; r = r * g.ma + br * g.mb; break;
                    case 3: { const float kk = g.refB * g.mb; l = l * (g.ma + bl * kk); r = r * (g.ma + br * kk); } break;
                    default: { const float e = g.sa->rmsAt (pa) * g.refB * g.mb; l = l * g.ma + bl * e; r = r * g.ma + br * e; } break;
                }
                pb += ib;
            }
            if constexpr (Filt) { l = g.fl.run (g.ftype, l); r = stF ? g.fr.run (g.ftype, r) : l; }
            vl[i] += l * w * g.gl; vr[i] += r * w * g.gr;
            pa += ia;
        }
        const int done = i - (end - left);
        g.t += done; g.pa = pa; g.pb = pb; g.wPos = wPos;
        if (g.t >= g.len) g.on = false;
        if constexpr (Filt) { g.fl.sanitize(); g.fr.sanitize(); }
    }

    void renderVoice (Voice& v, float* L, float* R, int m)
    {
        const Source* A = srcA;
        float vl[kChunk], vr[kChunk];
        for (int i = 0; i < m; ++i) vl[i] = vr[i] = 0;
        // glide (time to cover the distance ~ glide seconds)
        if (v.pitchCur != v.pitchTarget)
        {
            if (P.glide <= 0.0005f) v.pitchCur = v.pitchTarget;
            else { const float c = 1.0f - std::exp (-4.6f * m / (P.glide * sr)); v.pitchCur += (v.pitchTarget - v.pitchCur) * c; if (std::fabs (v.pitchTarget - v.pitchCur) < 0.001f) v.pitchCur = v.pitchTarget; }
        }
        const bool granular = v.sliceIdx < 0 || P.slPlay == slGranular;
        if (granular && A && v.amp.st != Env::idle)
        {
            // scan: moves the position over time (x real time: 1 = plays through the sample at normal speed)
            const float scan = P.scan + v.mod[mdScan] * 2.0f;
            const double spanS = v.sliceIdx >= 0 ? std::max (1, v.s1 - v.s0) : A->len();
            v.scan += (double) scan * m / sr * A->audio->rate / spanS;
            v.scan -= std::floor (v.scan);
            const float dens = std::clamp (P.density * std::pow (2.0f, v.mod[mdDensity] * 4.0f), 0.5f, 400.0f);
            const double interval = sr / dens;
            const float sizeS = std::clamp (P.size * std::pow (2.0f, v.mod[mdSize] * 3.0f), 0.004f, 1.0f);
            const float ov = std::min ((float) kGrains, dens * sizeS);
            const float coh = std::clamp (P.cohere + v.mod[mdCohere], 0.0f, 1.0f);
            const float norm = 1.0f / ((1.0f - coh) * std::sqrt (std::max (1.0f, ov * 0.375f)) + coh * std::max (1.0f, ov * 0.5f));
            // output period for Cohere (no jitter)
            float semis = pitchOf (v) + P.pitch + P.fine / 100.0f + v.mod[mdPitch] * 24.0f;
            if (v.sliceIdx >= 0) semis = 60.0f + v.transpose + P.pitch + P.fine / 100.0f;
            else if (P.scale > scOff) semis = snapScale (v.pitchCur + P.pitch + v.mod[mdPitch] * 24.0f, P.scale, P.key) + P.fine / 100.0f + (pitchOf (v) - v.pitchCur);
            const double Tout = v.sliceIdx >= 0 ? 0.0 : sr / (440.0 * std::pow (2.0, (semis - 69.0) / 12.0));
            const float jitT = 0.5f * (1.0f - coh) * std::clamp (P.spray + v.mod[mdSpray], 0.0f, 1.0f);
            while (v.next < m)
            {
                spawnGrain (v, std::max (0, (int) v.next), norm, Tout);
                v.next += interval * (1.0 + jitT * v.rng.sym());
            }
            v.next -= m;
            if (Tout > 1) { v.cphase += m / Tout; v.cphase -= std::floor (v.cphase); }
            v.dispPos = (float) (v.sliceIdx >= 0 ? (v.s0 + wrap01 (P.pos + v.mod[mdPos] + (float) v.scan) * (v.s1 - v.s0)) / A->len() : wrap01 (P.pos + v.mod[mdPos] + (float) v.scan));
        }
        int act = 0;
        for (auto& g : v.g) if (g.on) { renderGrain (g, vl, vr, m); if (g.on) ++act; }
        v.active = act;
        if (! granular && A && act > 0)
            for (auto& g : v.g) if (g.on && g.flat) { v.dispPos = (float) (g.pa / A->len()); break; }
        // amp
        const float velG = 1.0f - std::clamp (P.velSens, 0.0f, 1.0f) * (1.0f - std::clamp (v.vel, 0.0f, 1.0f));
        const float lvl = velG * std::clamp (1.0f + v.mod[mdLevel], 0.0f, 2.0f);
        for (int i = 0; i < m; ++i)
        {
            const float e = v.amp.tick() * lvl; v.env2.tick();
            L[i] += vl[i] * e; R[i] += vr[i] * e;
        }
        if (v.seq && v.gateLeft > 0) { v.gateLeft -= m; if (v.gateLeft <= 0) release (v); }
        // voice end
        const bool sliceShot = v.sliceIdx >= 0 && P.slPlay != slGranular;
        if (v.amp.st == Env::idle) { v.state = 0; for (auto& g : v.g) g.on = false; v.active = 0; }
        else if (sliceShot && act == 0) { v.state = 0; }
        else if (! A && act == 0 && v.amp.st == Env::rel) v.state = 0;
    }

    // ------------------------------------------------------------------ fx
    float readFrac (const std::vector<float>& b, int w, float d) const
    {
        const int N = (int) b.size(); float rp = (float) w - d; while (rp < 0) rp += (float) N;
        const int i0 = (int) rp; const float f = rp - (float) i0; const int i1 = (i0 + 1) % N;
        return b[(size_t) (i0 % N)] * (1 - f) + b[(size_t) i1] * f;
    }

    void post (float* L, float* R, int n, const Transport& tr, double spb)
    {
        const auto order = fxPerm (P.fxOrder);
        for (int s = 0; s < 4; ++s)
        {
            switch (order[(size_t) s])
            {
                case fxRepeat: fxRepeatRun (L, R, n, tr, spb); break;
                case fxComp:   if (P.cpOn) fxCompRun (L, R, n); break;
                case fxDelay:  if (P.dlOn) fxDelayRun (L, R, n, spb); break;
                default:       if (P.rvOn) fxReverbRun (L, R, n); break;
            }
        }
        // peak limiter with 1 ms look-ahead: the gain glides down over the look-ahead before a peak arrives (an instant gain
        // drop is a click), holds while the peak passes, then releases over 150 ms. The output is delayed by latency() samples.
        const float vol = dbToGain (P.levelDb), thr = 0.944f;
        const float relC = 1.0f - std::exp (-1.0f / (0.15f * sr));
        const float atkC = 1.0f - std::pow (0.002f, 1.0f / (float) limLa);
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = clean (L[i] * vol), r = clean (R[i] * vol);
            const float dl = limL[(size_t) limW], dr = limR[(size_t) limW];
            limL[(size_t) limW] = l; limR[(size_t) limW] = r; limA[(size_t) limW] = std::max (std::fabs (l), std::fabs (r));
            limW = limW + 1 == limLa ? 0 : limW + 1;
            float a = 0; for (int k = 0; k < limLa; ++k) a = std::max (a, limA[(size_t) k]);
            const float want = a > thr ? thr / a : 1.0f;
            lim = want < lim ? lim + (want - lim) * atkC : lim + (1.0f - lim) * relC;
            const float ol = std::clamp (dl * lim, -thr, thr), orr = std::clamp (dr * lim, -thr, thr);   // the clamp only catches what the glide missed
            L[i] = ol; R[i] = orr; pk = std::max (pk, std::max (std::fabs (ol), std::fabs (orr)));
        }
        lim = clean (lim) == 0 ? 1.0f : lim;
        outPeak.store (std::max (pk, outPeak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    }

    // Beat Repeat: at every beat (host grid when it plays, else a free clock at the host tempo), with the chance set,
    // the first 'length' of the beat is captured and looped until the next beat. 1.5 ms edges on every repeat, 3 ms in / out of the effect.
    void fxRepeatRun (float* L, float* R, int n, const Transport& tr, double spb)
    {
        const float ramp = 1.0f / (0.003f * sr);
        const int edge = std::max (8, (int) (0.0015f * sr));
        const double b0 = tr.playing ? tr.ppq : freeBeats - n / spb;
        for (int i = 0; i < n; ++i)
        {
            const double bp = b0 + i / spb;
            const int64_t beat = (int64_t) std::floor (bp + 1e-9);
            if (beat != rpBeat)
            {
                rpBeat = beat;
                // leaving a repeat: keep playing the loop (with its edges) for the 3 ms fade back to the live signal
                rpTailLen = 0; rpTailPos = 0;
                if (rpWas)
                {
                    const int tl = std::min ((int) rpTL.size(), (int) (0.0032f * sr) + 2);
                    int p = rpPlay;
                    for (int k = 0; k < tl; ++k) { const float e = smooth01 (std::min ((float) (p + 1) / edge, (float) (rpLoop - p) / edge)); rpTL[(size_t) k] = rpL[(size_t) p] * e; rpTR[(size_t) k] = rpR[(size_t) p] * e; if (++p >= rpLoop) p = 0; }
                    rpTailLen = tl;
                }
                rpActive = P.rpOn && rpRng.uni() < P.rpChance;
                rpCap = 0; rpPlay = 0;
                rpLoop = std::clamp ((int) (divBeats (P.rpLen) * spb), 64, (int) rpL.size() - 1);
            }
            const float x = L[i], y = R[i];
            float ol = x, orr = y;
            bool repeating = false;
            if (rpActive)
            {
                if (rpCap < rpLoop) { rpL[(size_t) rpCap] = x; rpR[(size_t) rpCap] = y; ++rpCap; }
                else
                {
                    repeating = true;
                    const float e = smooth01 (std::min ((float) (rpPlay + 1) / edge, (float) (rpLoop - rpPlay) / edge));
                    ol = rpL[(size_t) rpPlay] * e; orr = rpR[(size_t) rpPlay] * e;
                    if (++rpPlay >= rpLoop) rpPlay = 0;
                }
            }
            if (! repeating && rpTailPos < rpTailLen) { ol = rpTL[(size_t) rpTailPos]; orr = rpTR[(size_t) rpTailPos]; ++rpTailPos; }
            const float target = repeating ? 1.0f : 0.0f;
            rpG = target > rpG ? std::min (target, rpG + ramp) : std::max (target, rpG - ramp);
            if (rpG > 0)
            {
                const float wm = smooth01 (rpG) * std::clamp (P.rpMix, 0.0f, 1.0f);
                L[i] = x * (1 - wm) + ol * wm; R[i] = y * (1 - wm) + orr * wm;
            }
            rpWas = repeating;
        }
    }

    void fxCompRun (float* L, float* R, int n)
    {
        const float aC = 1.0f - std::exp (-1.0f / (0.004f * sr)), rC = 1.0f - std::exp (-1.0f / (0.12f * sr));
        const float ratio = std::max (1.0f, P.cpRatio), knee = 6.0f;
        float gain = 1;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::fabs (L[i]), std::fabs (R[i]));
            cpEnv += (a - cpEnv) * (a > cpEnv ? aC : rC);
            if ((i & 3) == 0)
            {
                const float db = 20.0f * std::log10 (cpEnv + 1e-9f), over = db - P.cpThr;
                float red = 0;
                if (over > knee * 0.5f) red = over * (1 - 1 / ratio);
                else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; red = (1 - 1 / ratio) * x * x / (2 * knee); }
                gain = dbToGain (P.cpGain - red);
            }
            L[i] *= gain; R[i] *= gain;
        }
        cpEnv = clean (cpEnv);
    }

    void fxDelayRun (float* L, float* R, int n, double spb)
    {
        const float dTarget = std::clamp ((float) (dlyBeats (P.dlTime) * spb), 0.01f * sr, (float) dlyL.size() - 8);
        if (dTime < 0) dTime = dTarget;
        const float fb = std::clamp (P.dlFb, 0.0f, 0.95f), dA = 1.0f - std::exp (-2.0f * kPif * 4200.0f / sr);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            dTime += (dTarget - dTime) * 0.0005f;
            const float yl = readFrac (dlyL, dlyW, dTime), yr = readFrac (dlyR, dlyW, dTime);
            dlyLp[0] += (yl - dlyLp[0]) * dA; dlyLp[1] += (yr - dlyLp[1]) * dA;
            dlyL[(size_t) dlyW] = clean (std::tanh ((0.5f * (l + r)) * 0.7f + dlyLp[1] * fb));
            dlyR[(size_t) dlyW] = clean (std::tanh (dlyLp[0] * fb));
            dlyW = (dlyW + 1) % (int) dlyL.size();
            L[i] = l + yl * P.dlMix * 1.2f; R[i] = r + yr * P.dlMix * 1.2f;
        }
        dlyLp[0] = clean (dlyLp[0]); dlyLp[1] = clean (dlyLp[1]);
    }

    void fxReverbRun (float* L, float* R, int n)
    {
        const float rt = 0.4f + P.rvSize * P.rvSize * 9.0f, size = 0.45f + P.rvSize * 1.5f;
        const float rA = 1.0f - std::exp (-2.0f * kPif * (9000.0f - 7600.0f * P.rvDark) / sr);
        std::array<float, 8> rg, rlen;
        for (int k = 0; k < 8; ++k) { rlen[(size_t) k] = std::min ((float) rv[(size_t) k].buf.size() - 4.0f, rv[(size_t) k].base * size); rg[(size_t) k] = std::pow (10.0f, -3.0f * rlen[(size_t) k] / (sr * rt)); }
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            std::array<float, 8> o;
            for (int k = 0; k < 8; ++k) { auto& ln = rv[(size_t) k]; o[(size_t) k] = readFrac (ln.buf, ln.w, rlen[(size_t) k]); }
            for (int h = 1; h < 8; h <<= 1) for (int a = 0; a < 8; a += h << 1) for (int b = a; b < a + h; ++b) { const float x = o[(size_t) b], y = o[(size_t) (b + h)]; o[(size_t) b] = x + y; o[(size_t) (b + h)] = x - y; }
            const float in = (l + r) * 0.5f;
            float wl = 0, wr = 0;
            for (int k = 0; k < 8; ++k)
            {
                auto& ln = rv[(size_t) k];
                const float f = o[(size_t) k] * 0.35355339f * rg[(size_t) k];
                ln.lp += (f - ln.lp) * rA; ln.lp = clean (ln.lp);
                ln.buf[(size_t) ln.w] = ln.lp + in * ((k & 1) ? 0.5f : -0.5f);
                ln.w = (ln.w + 1) % (int) ln.buf.size();
                if (k & 1) wr += ln.lp; else wl += ln.lp;
            }
            L[i] = l * (1.0f - 0.5f * P.rvMix) + wl * P.rvMix * 0.6f;
            R[i] = r * (1.0f - 0.5f * P.rvMix) + wr * P.rvMix * 0.6f;
        }
    }
};
} // namespace slt
