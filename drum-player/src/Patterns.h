#pragma once
// CMPLXTY Drum Player patterns (no JUCE; 1.1.0, step grid / fill density / ending 1.2.0): factory MIDI drum patterns on General MIDI / FPC notes, a seeded fill generator for
// the end of the clip, and the clip builder used by the preview, the .mid drag-out and the audio drag-outs.
// Steps are 16ths: 'X' accent, 'x' hit, 'o' ghost, anything else = rest. A lane string is 16 steps per bar (1 or 2 bars, repeated).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cw::pat
{
enum : int { nKick = 36, nRim = 37, nSnare = 38, nClap = 39, nTomL = 41, nCH = 42, nOH = 46, nTomM = 45, nTomH = 48, nCrash = 49, nRide = 51 };

struct Lane { int note; const char* steps; };
struct Pattern { const char* name; std::vector<Lane> lanes; };

inline const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> p {
        { "House 4/4",        { { nKick, "x---x---x---x---" }, { nClap, "----x-------x---" }, { nCH, "--x---x---x---x-" }, { nOH, "------------x---" } } },
        { "Tech House",       { { nKick, "x---x---x---x---" }, { nClap, "----x-------x---" }, { nCH, "xoxoxoxoxoxoxoxo" }, { nOH, "--x---x---x---x-" }, { nRim, "-------x--x-----" } } },
        { "Techno Drive",     { { nKick, "X---x---X---x---" }, { nClap, "----x-------x---" }, { nCH, "x-xxx-xxx-xxx-xx" }, { nOH, "--x---x---x---x-" }, { nRide, "x-x-x-x-x-x-x-x-" } } },
        { "Midtempo",         { { nKick, "x-------x-x-----" }, { nSnare, "----X-------X---" }, { nCH, "x-x-x-x-x-x-x-x-" }, { nOH, "--------------x-" } } },
        { "Dubstep Half",     { { nKick, "x---------x-----x-------x---x---" }, { nSnare, "--------X---------------X-------" }, { nCH, "x-x-x-x-x-x-x-x-x-x-x-x-x-x-xxxx" } } },
        { "Trap",             { { nKick, "x------x--x-----x-----x---x-----" }, { nSnare, "--------X---------------X-------" }, { nCH, "x-x-x-x-xxx-x-x-x-x-x-x-x-xxxxx-" }, { nOH, "--------------------------x-----" } } },
        { "Boom Bap",         { { nKick, "x------xx-x-----x-------x-x--x--" }, { nSnare, "----X--o----X-o-----X--o----X---" }, { nCH, "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-" } } },
        { "Breakbeat",        { { nKick, "x---------x-----x-x-------x-----" }, { nSnare, "----X--o-o--X-o-----X--o-o--X---" }, { nCH, "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-" }, { nOH, "--------------x---------------x-" } } },
        { "Drum & Bass",      { { nKick, "x---------x-----" }, { nSnare, "----X-------X---" }, { nCH, "x-x-x-x-x-x-x-x-" }, { nRide, "--x---x---x---x-" } } },
        { "Hip Hop Lazy",     { { nKick, "x-----x---x-----x-------x--x----" }, { nSnare, "----X-------X-------X-------X--o" }, { nCH, "x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-x-" } } },
        { "Rock Beat",        { { nKick, "x-------x-x-----" }, { nSnare, "----X-------X---" }, { nCH, "x-x-x-x-x-x-x-x-" } } },
        { "Funk",             { { nKick, "x--x--x---x--x--" }, { nSnare, "----X--o-o--X--o" }, { nCH, "xxxxxxxxxxxxxxxx" }, { nOH, "-------x--------" } } },
        { "Reggaeton",        { { nKick, "x---x---x---x---" }, { nSnare, "---x--x----x--x-" }, { nCH, "x-x-x-x-x-x-x-x-" } } },
        { "Afro House",       { { nKick, "x---x---x---x---" }, { nRim, "--x--x----x--x--" }, { nCH, "--x---x---x---x-" }, { nTomL, "------x-------x-" }, { nTomM, "---x-------x----" } } },
        { "Synthwave",        { { nKick, "x-------x-------" }, { nSnare, "----X-------X---" }, { nClap, "----x-------x---" }, { nCH, "x-x-x-x-x-x-x-x-" }, { nOH, "--------------x-" } } },
        { "Garage 2-Step",    { { nKick, "x---------x-----x-------x-x-----" }, { nSnare, "----X-------X-------X-------X---" }, { nCH, "--x---x---x---xx--x---x---x---x-" }, { nRim, "-------x-------x-------x-------x" } } },
        { "Hardstyle",        { { nKick, "X---X---X---X---" }, { nClap, "----x-------x---" }, { nOH, "--x---x---x---x-" }, { nRide, "x-x-x-x-x-x-x-x-" } } },
        { "Industrial",       { { nKick, "X--x--X---x-X---" }, { nSnare, "----X--x----X-x-" }, { nCH, "x-xxx-xxx-xxx-xx" }, { nTomL, "-------------x--" } } },
    };
    return p;
}

enum FillStyle { kNoFill = 0, kAuto, kRoll, kBuild, kToms, kTriplets, kFlams, kBreak, kStutter, kNumFill };
inline const char* fillName (int s) { static const char* n[] = { "Off", "Auto", "Roll", "Build", "Toms", "Triplets", "Flams", "Break", "Stutter" }; return s >= 0 && s < kNumFill ? n[s] : "Off"; }

struct Ev { double beat; int note; float vel; };

// 1.2.0 step editor: every pattern can be edited as a grid of 11 lanes x 32 sixteenths (2 bars, repeated over the loop).
// A cell holds the velocity in percent (0 = off, 1..100); the factory 'X' / 'x' / 'o' are 100 / 80 / 40, so an edited copy of a
// factory pattern plays exactly the factory velocities.
constexpr int kLanes = 11, kSteps = 32, kGrid = kLanes * kSteps;
inline const int* laneNotes() { static const int n[kLanes] = { nCrash, nRide, nOH, nCH, nRim, nClap, nSnare, nTomH, nTomM, nTomL, nKick }; return n; }
inline const char* laneName (int l) { static const char* n[kLanes] = { "CRASH", "RIDE", "OPEN HAT", "CL HAT", "RIM", "CLAP", "SNARE", "TOM HI", "TOM MID", "TOM LO", "KICK" }; return l >= 0 && l < kLanes ? n[l] : ""; }
inline int laneOf (int note) { for (int l = 0; l < kLanes; ++l) if (laneNotes()[l] == note) return l; return -1; }
inline int stepLevel (char ch) { return ch == 'X' ? 100 : ch == 'x' ? 80 : ch == 'o' ? 40 : 0; }
enum FillEnd { kEndCrash = 0, kEndRide, kEndOpenHat, kEndNone };

// swing: 0.5 straight .. 0.75 (1.2.0). density / ending shape the fill (1.2.0); steps = the edited grid of this pattern (nullptr = factory)
struct Clip { int bars = 4; int pattern = 0, fill = kAuto, fillBeats = 4; uint32_t seed = 1; float swing = 0.5f, density = 1.0f; int ending = kEndCrash; const uint8_t* steps = nullptr; };

struct Rng { uint32_t s; explicit Rng (uint32_t x) : s (x * 2654435761u + 0x9e3779b9u) { next(); } uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } float f() { return (next() & 0xffffff) / 16777216.0f; } int i (int n) { return (int) (next() % (uint32_t) std::max (1, n)); } };

// fill hits for [start, start + beats) - appended to out
inline void makeFill (std::vector<Ev>& out, int style, double start, int beats, uint32_t seed)
{
    Rng r (seed);
    if (style == kAuto) style = kRoll + r.i (kStutter - kRoll + 1);
    const double end = start + beats;
    auto add = [&] (double b, int n, float v) { if (b >= start - 1e-9 && b < end - 1e-9) out.push_back ({ b, n, std::clamp (v, 0.05f, 1.0f) }); };
    const int steps = beats * 4;
    switch (style)
    {
        case kRoll:
            for (int k = 0; k < steps; ++k) { const double b = start + k * 0.25; add (b, nSnare, 0.45f + 0.55f * k / std::max (1, steps - 1)); if (k >= steps - 4 && beats > 1) add (b + 0.125, nSnare, 0.6f + 0.3f * r.f()); }
            add (start, nKick, 0.9f); break;
        case kBuild:
            for (int k = 0; k < steps; ++k)
            {
                const double b = start + k * 0.25, frac = (double) k / steps;
                if (frac < 0.5 && (k & 1)) continue;
                add (b, nSnare, (float) (0.5 + 0.5 * frac));
                if (frac >= 0.75) add (b + 0.125, nSnare, (float) (0.55 + 0.4 * frac));
                if ((k & 3) == 0) add (b, nKick, 0.85f);
            }
            break;
        case kToms:
        {
            const int seq[] = { nSnare, nTomH, nTomH, nTomM, nTomM, nTomL, nTomL, nKick };
            const int off = r.i (2);
            for (int k = 0; k < steps; ++k) { const int idx = std::min (7, (k + off) * 8 / std::max (1, steps)); add (start + k * 0.25, seq[idx], 0.7f + 0.3f * ((k & 3) == 0) - 0.1f * r.f()); if ((k & 3) == 0) add (start + k * 0.25, nKick, 0.8f); }
            break;
        }
        case kTriplets:
            for (int k = 0; k < beats * 3; ++k) { const double b = start + k / 3.0; add (b, (k % 3 == 0) ? nSnare : ((k / 3) & 1) ? nTomL : nTomM, 0.6f + 0.4f * (k % 3 == 0)); if (k % 3 == 0) add (b, nKick, 0.75f); }
            break;
        case kFlams:
            for (int k = 0; k < beats * 2; ++k) { const double b = start + k * 0.5; add (b - 1.0 / 32.0, nSnare, 0.35f); add (b, k & 1 ? nTomM : nSnare, 0.95f); if (k % 2 == 0) add (b, nKick, 0.8f); }
            break;
        case kBreak:
            for (int k = 0; k < steps; ++k)
            {
                const double b = start + k * 0.25; const int c = r.i (6);
                if (c <= 1) add (b, nKick, 0.8f + 0.2f * r.f()); else if (c <= 3) add (b, nSnare, (k & 1) ? 0.45f : 0.9f); else if (c == 4) add (b, nCH, 0.6f);
                if ((k & 3) == 0 && c > 1) add (b, nKick, 0.7f);
            }
            break;
        case kStutter:
        default:
            for (int k = 0; k < steps; ++k)
            {
                const double b = start + k * 0.25;
                add (b, (k & 1) ? nSnare : nKick, 0.7f + 0.3f * k / std::max (1, steps));
                if (k >= steps / 2) { add (b + 0.125, nSnare, 0.65f); add (b + 0.125, nCH, 0.5f); }
            }
            break;
    }
}

// a factory pattern as a step grid (16-step lanes repeated to 32)
inline void factoryGrid (int pattern, uint8_t* g)
{
    std::fill (g, g + kGrid, (uint8_t) 0);
    const auto& all = patterns();
    for (const auto& l : all[(size_t) std::clamp (pattern, 0, (int) all.size() - 1)].lanes)
    {
        const int lane = laneOf (l.note), n = (int) std::strlen (l.steps);
        if (lane >= 0) for (int k = 0; k < kSteps; ++k) g[lane * kSteps + k] = (uint8_t) stepLevel (l.steps[k % n]);
    }
}

// The whole clip: the pattern repeated over `bars` bars, the last fillBeats replaced by a fill, a crash on beat 1 (where the fill
// lands when the clip loops). Sorted by beat; all beats in [0, bars * 4). `out` is cleared, not shrunk (no allocation once reserved).
inline void build (const Clip& c, std::vector<Ev>& out)
{
    out.clear();
    const auto& all = patterns();
    const auto& P = all[(size_t) std::clamp (c.pattern, 0, (int) all.size() - 1)];
    const int bars = std::max (1, c.bars);
    const double len = bars * 4.0;
    const bool fill = c.fill != kNoFill;
    const double fillStart = fill ? len - std::clamp (c.fillBeats, 1, bars * 4) : len;
    // lanes in the pattern's own order first (same-sample hits trigger in the factory order, so an unchanged edit is bit-identical), then the rest
    int order[kLanes]; int no = 0;
    for (const auto& l : P.lanes) if (const int ln = laneOf (l.note); ln >= 0) { bool dup = false; for (int q = 0; q < no; ++q) dup = dup || order[q] == ln; if (! dup) order[no++] = ln; }
    if (c.steps != nullptr) for (int ln = 0; ln < kLanes; ++ln) { bool dup = false; for (int q = 0; q < no; ++q) dup = dup || order[q] == ln; if (! dup) order[no++] = ln; }
    for (int oi = 0; oi < no; ++oi)
    {
        const int lane = order[oi];
        const char* str = nullptr; int n = kSteps;
        if (c.steps == nullptr) for (const auto& l : P.lanes) if (laneOf (l.note) == lane) { str = l.steps; n = (int) std::strlen (l.steps); break; }
        for (int k = 0; k < bars * 16; ++k)
        {
            const int lvl = c.steps != nullptr ? c.steps[lane * kSteps + k % kSteps] : stepLevel (str[k % n]);
            const float v = (float) std::min (lvl, 100) / 100.0f;
            const double b = k * 0.25;
            if (v <= 0 || b >= fillStart - 1e-9) continue;
            // 1.2.0 swing: every second 16th of the groove moves later (0.5 = straight, 0.66 = triplet feel, 0.75 = dotted); fills stay straight
            out.push_back ({ (k & 1) ? b + (std::clamp ((double) c.swing, 0.5, 0.75) - 0.5) * 0.5 : b, laneNotes()[lane], v });
        }
    }
    if (fill)
    {
        const size_t f0 = out.size();
        makeFill (out, c.fill, fillStart, (int) (len - fillStart + 0.5), c.seed);
        if (c.density < 0.999f)   // 1.2.0 FILL DENSITY: a seeded choice of the fill's hits is dropped (the first one always stays)
        {
            Rng r (c.seed * 7919u + 17u); size_t w = f0;
            for (size_t i = f0; i < out.size(); ++i) if (i == f0 || r.f() < c.density) out[w++] = out[i];
            out.resize (w);   // shrinks only: never allocates
        }
        // 1.2.0 FILL ENDING: what lands on beat 1 when the loop comes round (Crash = 1.1)
        static const int endNote[] = { nCrash, nRide, nOH, 0 };
        const int en = endNote[std::clamp (c.ending, 0, 3)];
        bool has = en == 0; for (auto& e : out) if (e.beat < 1e-9 && e.note == en) has = true;
        if (! has) out.push_back ({ 0.0, en, 0.9f });
    }
    for (auto& e : out) if (e.beat < 0) e.beat += len;   // flam grace notes before beat 0 wrap
    // stable insertion sort by beat (same order as std::stable_sort, which may allocate a buffer: this also runs on the audio thread)
    for (size_t i = 1; i < out.size(); ++i) { const Ev e = out[i]; size_t j = i; while (j > 0 && e.beat < out[j - 1].beat) { out[j] = out[j - 1]; --j; } out[j] = e; }
}
} // namespace cw::pat
