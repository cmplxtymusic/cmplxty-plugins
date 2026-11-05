#pragma once
// CMPLXTY Drum Player 1.1.0 patterns (no JUCE): factory MIDI drum patterns on General MIDI / FPC notes, a seeded fill generator for
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
struct Clip { int bars = 4; int pattern = 0, fill = kAuto, fillBeats = 4; uint32_t seed = 1; };

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
    for (const auto& l : P.lanes)
    {
        const int n = (int) std::strlen (l.steps);
        for (int k = 0; k < bars * 16; ++k)
        {
            const char ch = l.steps[k % n];
            const float v = ch == 'X' ? 1.0f : ch == 'x' ? 0.8f : ch == 'o' ? 0.4f : 0.0f;
            const double b = k * 0.25;
            if (v <= 0 || b >= fillStart - 1e-9) continue;
            out.push_back ({ b, l.note, v });
        }
    }
    if (fill)
    {
        makeFill (out, c.fill, fillStart, (int) (len - fillStart + 0.5), c.seed);
        bool crash = false; for (auto& e : out) if (e.beat < 1e-9 && e.note == nCrash) crash = true;
        if (! crash) out.push_back ({ 0.0, nCrash, 0.9f });
    }
    for (auto& e : out) if (e.beat < 0) e.beat += len;   // flam grace notes before beat 0 wrap
    std::stable_sort (out.begin(), out.end(), [] (const Ev& a, const Ev& b) { return a.beat < b.beat; });
}
} // namespace cw::pat
