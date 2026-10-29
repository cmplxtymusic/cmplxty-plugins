// Quartz parameter table: single source of truth for IDs, ranges and defaults.
// IDs are STABLE once released (saved projects and automation depend on them).
#pragma once
#include <cstdint>

namespace qz
{
enum ParamIndex : int
{
    // ---- Quartz section
    pQon = 0, pIn, pGate, pBits, pRate, pDrive, pGrit, pDType, pTone, pVocode, pCShape, pCNote,
    pLowCut, pHighCut, pDouble, pWiden, pDelay, pFb, pReverb, pDecay, pMix, pOut, pIntensity,
    // ---- ALTER section
    pAon, pMode, pPitch, pLink, pFormant, pGender, pADrive, pSpeed, pKey, pScale, pAMix, pAOut,
    // ---- master
    pBypass,
    // ---- per-knob bypass switches (true = knob bypassed)
    bIn, bGate, bBits, bRate, bDrive, bGrit, bTone, bVocode, bCarrier, bLowCut, bHighCut,
    bDouble, bWiden, bDelay, bFb, bReverb, bDecay, bMix, bOut, bIntensity,
    bPitch, bFormant, bGender, bADrive, bSpeed, bAMix, bAOut,
    kNumParams
};

// Alter Key / Scale: the "--" choice is appended LAST so every 1.0.0/1.0.1 saved index keeps its meaning; it is the default.
inline constexpr int kKeyOff = 12, kScaleOff = 7;

enum class Kind : uint8_t { Float, Bool, Choice };

struct ParamSpec
{
    const char* id;
    const char* name;
    Kind kind;
    float mn, mx, def;
    float skewMid;      // value that sits at the centre of the knob travel (0 = linear)
    float step;         // 0 = continuous
    const char* unit;   // "dB", "Hz", "%", ...
    const char* choices;// '|' separated, for Kind::Choice
};

inline constexpr ParamSpec kParams[kNumParams] = {
    // id           name              kind        min     max      def     mid   step   unit  choices
    { "qon",       "Quartz On",      Kind::Bool,   0,      1,      1,      0,    1,   "",   nullptr },
    { "in",        "Input",          Kind::Float, -18,     18,     0,      0,    0,   "dB", nullptr },
    { "gate",      "Gate",           Kind::Float, -80,     0,     -80,     0,    0,   "dB", nullptr },
    { "bits",      "Bits",           Kind::Float,  2,      24,     24,     0,    1,   "bit",nullptr },
    { "rate",      "Rate",           Kind::Float,  1000,   48000,  48000,  9000, 0,   "Hz", nullptr },
    { "drive",     "Drive",          Kind::Float,  0,      36,     0,      0,    0,   "dB", nullptr },
    { "grit",      "Grit",           Kind::Float,  0,      100,    0,      0,    0,   "%",  nullptr },
    { "dtype",     "Drive Type",     Kind::Choice, 0,      2,      0,      0,    1,   "",   "Clip|Fold|Fuzz" },
    { "tone",      "Tone",           Kind::Float, -12,     12,     0,      0,    0,   "",   nullptr },
    { "vocode",    "Vocode",         Kind::Float,  0,      100,    0,      0,    0,   "%",  nullptr },
    { "cshape",    "Carrier Shape",  Kind::Choice, 0,      2,      0,      0,    1,   "",   "Saw|Square|Noise" },
    { "cnote",     "Carrier Note",   Kind::Float,  24,     72,     48,     0,    1,   "",   nullptr },
    { "lowcut",    "Low Cut",        Kind::Float,  20,     1000,   20,     120,  0,   "Hz", nullptr },
    { "highcut",   "High Cut",       Kind::Float,  2000,   20000,  20000,  8000, 0,   "Hz", nullptr },
    { "double",    "Double",         Kind::Float,  0,      60,     0,      0,    0,   "ct", nullptr },
    { "widen",     "Widen",          Kind::Float,  0,      100,    0,      0,    0,   "%",  nullptr },
    { "delay",     "Delay",          Kind::Float,  0,      1000,   0,      120,  0,   "ms", nullptr },
    { "fb",        "Feedback",       Kind::Float,  0,      95,     0,      0,    0,   "%",  nullptr },
    { "reverb",    "Reverb",         Kind::Float,  0,      100,    0,      0,    0,   "%",  nullptr },
    { "decay",     "Decay",          Kind::Float,  0.2f,   10,     1.5f,   1.5f, 0,   "s",  nullptr },
    { "mix",       "Mix",            Kind::Float,  0,      100,    100,    0,    0,   "%",  nullptr },
    { "out",       "Output",         Kind::Float, -24,     12,     0,      0,    0,   "dB", nullptr },
    { "intensity", "Intensity",      Kind::Float,  0,      100,    70,     0,    0,   "",   nullptr },

    { "aon",       "Alter On",       Kind::Bool,   0,      1,      1,      0,    1,   "",   nullptr },
    { "mode",      "Alter Mode",     Kind::Choice, 0,      2,      0,      0,    1,   "",   "Transpose|Quantize|Robot" },
    { "pitch",     "Alter Pitch",    Kind::Float, -12,     12,     0,      0,    0,   "st", nullptr },
    { "link",      "Alter Link",     Kind::Bool,   0,      1,      0,      0,    1,   "",   nullptr },
    { "formant",   "Alter Formant",  Kind::Float, -12,     12,     0,      0,    0,   "st", nullptr },
    { "gender",    "Alter Gender",   Kind::Float, -100,    100,    0,      0,    0,   "",   nullptr },
    { "adrive",    "Alter Drive",    Kind::Float,  0,      10,     0,      0,    0,   "",   nullptr },
    { "speed",     "Alter Speed",    Kind::Float,  1,      200,    12,     30,   0,   "ms", nullptr },
    { "key",       "Alter Key",      Kind::Choice, 0,      12,     12,     0,    1,   "",   "C|C#|D|D#|E|F|F#|G|G#|A|A#|B|--" },
    { "scale",     "Alter Scale",    Kind::Choice, 0,      7,      7,      0,    1,   "",   "Chromatic|Major|Minor|Harm Minor|Pent Major|Pent Minor|Blues|--" },
    { "amix",      "Alter Mix",      Kind::Float,  0,      100,    100,    0,    0,   "%",  nullptr },
    { "aout",      "Alter Output",   Kind::Float, -24,     12,     0,      0,    0,   "dB", nullptr },

    { "bypass",    "Bypass",         Kind::Bool,   0,      1,      0,      0,    1,   "",   nullptr },

    { "b_in",       "Bypass Input",      Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_gate",     "Bypass Gate",       Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_bits",     "Bypass Bits",       Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_rate",     "Bypass Rate",       Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_drive",    "Bypass Drive",      Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_grit",     "Bypass Grit",       Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_tone",     "Bypass Tone",       Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_vocode",   "Bypass Vocode",     Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_carrier",  "Bypass Carrier",    Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_lowcut",   "Bypass Low Cut",    Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_highcut",  "Bypass High Cut",   Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_double",   "Bypass Double",     Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_widen",    "Bypass Widen",      Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_delay",    "Bypass Delay",      Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_fb",       "Bypass Feedback",   Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_reverb",   "Bypass Reverb",     Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_decay",    "Bypass Decay",      Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_mix",      "Bypass Mix",        Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_out",      "Bypass Output",     Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_intensity","Bypass Intensity",  Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_pitch",    "Bypass Alter Pitch",   Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_formant",  "Bypass Alter Formant", Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_gender",   "Bypass Alter Gender",  Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_adrive",   "Bypass Alter Drive",   Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_speed",    "Bypass Alter Speed",   Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_amix",     "Bypass Alter Mix",     Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
    { "b_aout",     "Bypass Alter Output",  Kind::Bool, 0, 1, 0, 0, 1, "", nullptr },
};

// Which bypass switch belongs to which knob (-1 = none).
inline constexpr int bypassFor(int p)
{
    switch (p)
    {
        case pIn: return bIn;           case pGate: return bGate;       case pBits: return bBits;
        case pRate: return bRate;       case pDrive: return bDrive;     case pGrit: return bGrit;
        case pTone: return bTone;       case pVocode: return bVocode;   case pCNote: return bCarrier;
        case pLowCut: return bLowCut;   case pHighCut: return bHighCut; case pDouble: return bDouble;
        case pWiden: return bWiden;     case pDelay: return bDelay;     case pFb: return bFb;
        case pReverb: return bReverb;   case pDecay: return bDecay;     case pMix: return bMix;
        case pOut: return bOut;         case pIntensity: return bIntensity;
        case pPitch: return bPitch;     case pFormant: return bFormant; case pGender: return bGender;
        case pADrive: return bADrive;   case pSpeed: return bSpeed;     case pAMix: return bAMix;
        case pAOut: return bAOut;
        default: return -1;
    }
}

// Section a knob belongs to: 0 = Quartz, 1 = ALTER, -1 = master.
inline constexpr int sectionOf(int p)
{
    if (p >= pQon && p <= pIntensity) return 0;
    if (p >= pAon && p <= pAOut) return 1;
    if (p == pBypass) return -1;
    if (p >= bIn && p <= bIntensity) return 0;
    return 1;
}
} // namespace qz
