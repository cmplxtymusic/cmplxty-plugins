// CMPLXTY Rewired 1.1.0 MAP mode: beat grid, downbeat, sections and per-instrument lanes of a reference song.
// Everything here works on the engine's Result (notes, drum hits, instrument sub-parts) plus the stem audio; it costs
// milliseconds next to the separation and note models. Tuned and measured with test/make_map_songs.py + test/map_eval.py.
#include <limits>
#include "RewiredEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <chrono>

namespace rw
{
namespace
{
constexpr double kPiMap = 3.14159265358979323846;

// energy of any span in O(1) from a prefix sum of the 10 ms power envelope (linear inside a frame)
struct Energy
{
    std::vector<double> cum, pw;
    explicit Energy (const MapEnvelope* e)
    {
        if (e == nullptr || e->pow.empty()) return;
        pw.assign (e->pow.begin(), e->pow.end());
        cum.resize (pw.size() + 1, 0.0);
        for (size_t i = 0; i < pw.size(); ++i) cum[i + 1] = cum[i] + pw[i];
    }
    bool valid() const { return ! cum.empty(); }
    double at (double f) const   // integral of the power up to frame position f
    {
        const size_t i = (size_t) f;
        return i >= pw.size() ? cum.back() : cum[i] + (f - (double) i) * pw[i];
    }
    double db (double t0, double t1) const   // mean power in dBFS over [t0, t1)
    {
        if (! valid()) return -120.0;
        const auto n = (double) pw.size();
        const double a = std::clamp (t0 * MapEnvelope::kFps, 0.0, n), b = std::clamp (t1 * MapEnvelope::kFps, 0.0, n);
        if (b - a < 0.25) return -120.0;
        // whole frames inside the span only: a frame that straddles the edge would smear a part that starts right after the span
        // into it (an entry would then look one beat early). Short spans (< 2 frames) interpolate.
        const double A = std::ceil (a - 1e-9), B = std::floor (b + 1e-9);
        if (B - A >= 2.0) return 10.0 * std::log10 ((cum[(size_t) B] - cum[(size_t) A]) / (B - A) + 1e-12);
        return 10.0 * std::log10 ((at (b) - at (a)) / (b - a) + 1e-12);
    }
};

double percentile (std::vector<double> v, double q)
{
    if (v.empty()) return -120.0;
    const size_t k = (size_t) std::clamp (q * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

struct Ev { double t; double w; };

// phase-coherent tempo: the whole bpm (near the detected one) at which the hits line up best over the whole song, and the beat
// phase. A tempo that is off by 0.3 bpm drifts by a beat over a few minutes; this does not. forced > 0: that tempo (the user's), phase only.
void refineGrid (const std::vector<Ev>& ev, double bpm0, double forced, double& bpm, double& phase)
{
    auto coh = [&] (double b, double* ph)
    {
        const double P = 60.0 / b; std::complex<double> z = 0;
        for (auto& e : ev) z += e.w * std::polar (1.0, 2.0 * kPiMap * e.t / P);
        if (ph != nullptr) { double a = std::arg (z) / (2.0 * kPiMap) * P; while (a < 0) a += P; *ph = a; }
        return std::abs (z);
    };
    phase = 0.0;
    if (forced > 0.0) { bpm = forced; if (ev.size() >= 4) coh (bpm, &phase); return; }
    bpm = std::round (bpm0);
    if (ev.size() < 8) return;
    // octave: drum & bass is often heard at half tempo (snares then fall on the off-beats); take double when it lines up better.
    // 78-95 bpm doubles more easily (160-190 is far more common in the music this is for than 80-95 with busy drums)
    const double need = (bpm0 >= 78.0 && bpm0 <= 95.0) ? 1.15 : 1.3;
    if (bpm0 * 2.0 <= 190.0 && coh (bpm0 * 2.0, nullptr) > need * coh (bpm0, nullptr)) bpm0 *= 2.0;
    double best = bpm0, bs = -1.0;
    for (double b = bpm0 * 0.985; b <= bpm0 * 1.015; b += 0.005) { const double s = coh (b, nullptr); if (s > bs) { bs = s; best = b; } }
    // a whole bpm (studio tracks sit on one; a map with 139.68 bpm is no use in a DAW): the better of the two around the best fit
    const double lo = std::floor (best), hi = std::ceil (best);
    bpm = lo == hi ? lo : (coh (hi, nullptr) >= coh (lo, nullptr) ? hi : lo);
    coh (bpm, &phase);
}

// beat-level on/off clean-up: close short gaps, drop lone beats (spurious notes / bleed), drop decaying one-beat tails
void tidy (std::vector<unsigned char>& on, int maxGap, bool keepLone)
{
    const int n = (int) on.size();
    for (int i = 0; i < n;)
    {
        if (on[(size_t) i]) { ++i; continue; }
        int j = i; while (j < n && ! on[(size_t) j]) ++j;
        if (i > 0 && j < n && j - i <= maxGap) for (int k = i; k < j; ++k) on[(size_t) k] = 1;
        i = j;
    }
    if (keepLone) return;
    for (int i = 0; i < n; ++i)
        if (on[(size_t) i] && (i == 0 || ! on[(size_t) i - 1]) && (i == n - 1 || ! on[(size_t) i + 1])) on[(size_t) i] = 0;
}
} // namespace

MapFeatures makeMapFeatures (const MapSources& src, const std::array<int, kNumMapGroups>& stemOf)
{
    MapFeatures f;
    const std::vector<float>* x[kNumMapGroups] = { src.drums, src.bass, src.lead, src.chords, src.music, src.vocals };
    const size_t hop = (size_t) (Engine::kSampleRate / MapEnvelope::kFps);
    for (int g = 0; g < kNumMapGroups; ++g)
    {
        if (x[g] == nullptr || x[g]->empty()) continue;
        MapEnvelope e; e.group = g; e.stem = stemOf[(size_t) g];
        const auto& a = *x[g];
        e.pow.resize ((a.size() + hop - 1) / hop);
        for (size_t i = 0; i < e.pow.size(); ++i)
        {
            double s = 0.0; const size_t b = i * hop, z = std::min (a.size(), b + hop);
            for (size_t k = b; k < z; ++k) s += (double) a[k] * a[k];
            e.pow[i] = (float) (s / (double) hop);
        }
        f.env.push_back (std::move (e));
    }
    f.leadIsVoice = src.vocals != nullptr && src.vocals == src.lead;
    if (f.leadIsVoice) for (auto& e : f.env) if (e.group == kGrpLead) e.pow.clear();   // one lane for that audio: the vocal one
    f.env.erase (std::remove_if (f.env.begin(), f.env.end(), [] (const MapEnvelope& e) { return e.pow.empty(); }), f.env.end());
    return f;
}

namespace
{
SongMap buildCore (const Result& r, const MapFeatures& feat, double forcedBpm, double anchor);   // anchor: bar 1 to keep, NaN = find it
}

// A tempo the user set that is the detected one, half or double of it (or x4 / x0.25) keeps the detected bar 1 (only the bar length
// changes); any other tempo finds its own beat phase and bar 1.
SongMap buildSongMap (const Result& r, const MapFeatures& feat, double forcedBpm)
{
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (forcedBpm <= 0.0) { auto m = buildCore (r, feat, 0.0, none); m.autoBpm = m.bpm; return m; }
    const auto clock0 = std::chrono::steady_clock::now();
    const auto a = buildCore (r, feat, 0.0, none);
    const double q = forcedBpm / a.bpm;
    bool related = false;
    for (double k : { 0.25, 0.5, 1.0, 2.0, 4.0 }) related |= std::abs (q / k - 1.0) < 0.03;
    auto m = buildCore (r, feat, forcedBpm, related ? a.firstDownbeat : none);   // (bar 1 can sit a hair before 0 s)
    m.autoBpm = a.bpm;
    m.buildMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - clock0).count();
    return m;
}

namespace
{
SongMap buildCore (const Result& r, const MapFeatures& feat, double forcedBpm, double anchor)
{
    const auto clock0 = std::chrono::steady_clock::now();
    SongMap m;
    const double len = std::max (1.0, r.seconds);

    // ---------------------------------------------------------------- grid
    std::vector<Ev> ev;
    for (auto& n : r.parts[kKick])  ev.push_back ({ n.start, 1.0 });
    for (auto& n : r.parts[kSnare]) ev.push_back ({ n.start, 0.8 });
    if (ev.size() < 16)
        for (int p : { (int) kBass, (int) kChords, (int) kMelody, (int) kHats })
            for (auto& n : r.parts[(size_t) p]) ev.push_back ({ n.start, 0.5 });
    double bpm = r.bpm > 20 ? r.bpm : 120.0, phase = 0.0;
    refineGrid (ev, bpm, forcedBpm, bpm, phase);
    m.bpm = bpm;
    const double P = 60.0 / bpm;
    const bool anchored = ! std::isnan (anchor);
    if (anchored) { phase = std::fmod (anchor, P); if (phase < 0.0) phase += P; }

    // stem loudness used for activity and section features; missing stems fall back to notes
    std::vector<Energy> en; std::vector<int> enGroup;
    for (int g : { (int) kGrpDrums, (int) kGrpBass, (int) kGrpLead, (int) kGrpChords, (int) kGrpMusic, (int) kGrpVocals })
        if (auto* e = feat.get (g)) { en.emplace_back (e); enGroup.push_back (g); }

    // beats over the whole song, starting at the first beat at or after 0
    const int nBeats = std::max (1, (int) std::ceil ((len - phase) / P));
    auto beatT = [&] (int k) { return phase + k * P; };
    std::vector<std::vector<double>> bdb (en.size(), std::vector<double> ((size_t) nBeats, -120.0));
    for (size_t s = 0; s < en.size(); ++s) for (int k = 0; k < nBeats; ++k) bdb[s][(size_t) k] = en[s].db (beatT (k), beatT (k + 1));
    std::vector<double> ref (en.size(), -120.0);
    for (size_t s = 0; s < en.size(); ++s)
    {
        std::vector<double> v; for (double d : bdb[s]) if (d > -90.0) v.push_back (d);
        ref[s] = percentile (v, 0.95);
    }

    const double loudest = ref.empty() ? -120.0 : *std::max_element (ref.begin(), ref.end());

    // first sound: the first beat where any stem (or any note) is within 40 dB of its loud level
    double firstSound = len, lastSound = 0.0;
    for (size_t s = 0; s < en.size(); ++s)
        for (int k = 0; k < nBeats; ++k)
            if (bdb[s][(size_t) k] > std::max (-70.0, ref[s] - 40.0))
            {
                // refine inside the beat on a 10 ms grid
                const double t0 = beatT (k);
                for (double t = std::max (0.0, t0 - P); t < t0 + P; t += 0.01)
                    if (en[s].db (t, t + 0.01) > std::max (-70.0, ref[s] - 40.0)) { firstSound = std::min (firstSound, t); break; }
                break;
            }
    for (size_t s = 0; s < en.size(); ++s)
        for (int k = nBeats - 1; k >= 0; --k)
            if (bdb[s][(size_t) k] > std::max (-70.0, ref[s] - 40.0)) { lastSound = std::max (lastSound, beatT (k + 1)); break; }
    for (auto& p : r.parts) for (auto& n : p) { firstSound = std::min (firstSound, n.start); lastSound = std::max (lastSound, n.end); }
    if (firstSound >= lastSound) { firstSound = 0.0; lastSound = len; }
    lastSound = std::min (lastSound, len);

    // downbeat: of the four beat phases, the one where the arrangement changes (energy novelty across stems) and crashes land
    std::vector<double> nov ((size_t) nBeats, 0.0), cym ((size_t) nBeats, 0.0);
    for (size_t s = 0; s < en.size(); ++s)
        for (int k = 4; k + 4 <= nBeats; ++k)
        {
            double a = 0, b = 0;
            for (int i = 0; i < 4; ++i) { a += std::max (bdb[s][(size_t) (k - 1 - i)], ref[s] - 40.0); b += std::max (bdb[s][(size_t) (k + i)], ref[s] - 40.0); }
            nov[(size_t) k] += std::min (30.0, std::abs (b - a) / 4.0);
        }
    for (auto& n : r.parts[kCymbals]) { const int k = (int) std::lround ((n.start - phase) / P); if (k >= 0 && k < nBeats) cym[(size_t) k] += 1.0; }
    for (auto& n : r.parts[kKick])    { const int k = (int) std::lround ((n.start - phase) / P); if (k >= 0 && k < nBeats && std::abs (n.start - beatT (k)) < P * 0.15) cym[(size_t) k] += 0.05; }
    // big changes count most (squared novelty); crashes are a strong downbeat cue. Each cue is a share of its total over the 4 phases.
    int bestJ = 0; double bestS = -1.0, novT = 1e-12, cymT = 1e-12, novJ[4] = {}, cymJ[4] = {};
    for (int k = 0; k < nBeats; ++k) { novJ[k % 4] += nov[(size_t) k] * nov[(size_t) k]; cymJ[k % 4] += cym[(size_t) k]; }
    for (int j = 0; j < 4; ++j) { novT += novJ[j]; cymT += cymJ[j]; }
    // entries: a part that comes in after a rest (4 beats mostly off, then 8 beats mostly on) almost always comes in on a downbeat
    double entJ[4] = {}, entT = 1e-12;
    for (size_t q = 0; q < en.size(); ++q)
        for (int k = 4; k + 8 <= nBeats; ++k)
        {
            const double thr = std::max (-60.0, ref[q] - 24.0);
            auto onAt = [&] (int i) { return bdb[q][(size_t) i] > thr; };
            if (! onAt (k) || onAt (k - 1)) continue;
            int before = 0, after = 0;
            for (int i = 1; i <= 4; ++i) before += ! onAt (k - i);
            for (int i = 0; i < 8; ++i) after += onAt (k + i);
            if (before >= 3 && after >= 6) { entJ[k % 4] += 1.0; entT += 1.0; }
        }
    for (int j = 0; j < 4; ++j)
    {
        const double s = 0.25 * novJ[j] / novT + (entT > 2.0 ? entJ[j] / entT : 0.0) + (cymT > 1.0 ? cymJ[j] / cymT : 0.0);
        if (s > bestS + 1e-9) { bestS = s; bestJ = j; }
    }
    double bar0 = anchored ? anchor : beatT (bestJ);
    while (bar0 - 4 * P >= firstSound - P * 0.25) bar0 -= 4 * P;
    while (bar0 < firstSound - P * 0.25) bar0 += 4 * P;
    m.firstDownbeat = bar0;
    const double barLen = 4 * P;
    m.bars = std::max (1, (int) std::ceil ((lastSound - bar0) / barLen - 0.1));
    const int k0 = (int) std::lround ((bar0 - phase) / P);   // beat index of bar 1
    const int nb = m.bars, nBeatsMap = nb * 4;
    auto mapBeatT = [&] (int i) { return bar0 + i * P; };     // i = beat inside the map (0 = bar 1, beat 1); may be < 0 for a pickup

    // ---------------------------------------------------------------- lanes
    // beat activity per lane over [-4, nBeatsMap): one bar of pickup before bar 1
    constexpr int kPre = 4;
    const int NB = nBeatsMap + kPre;
    auto audioLane = [&] (const MapEnvelope* x, std::vector<unsigned char>& on, std::vector<double>& lvl)
    {
        Energy e (x);
        std::vector<double> d ((size_t) NB);
        for (int i = 0; i < NB; ++i) d[(size_t) i] = e.db (mapBeatT (i - kPre), mapBeatT (i - kPre + 1));
        std::vector<double> v; for (double q : d) if (q > -90.0) v.push_back (q);
        const double rf = percentile (v, 0.95), thr = std::max (-60.0, rf - 24.0);
        on.assign ((size_t) NB, 0); lvl.assign ((size_t) NB, 0.0);
        if (rf < loudest - 26.0) return;   // only separation bleed (an instrumental's vocal stem): nothing plays
        for (int i = 0; i < NB; ++i)
        {
            on[(size_t) i] = d[(size_t) i] > thr;
            lvl[(size_t) i] = std::clamp ((d[(size_t) i] - (rf - 30.0)) / 30.0, 0.0, 1.0);
        }
        // a beat that only holds the release of the beat before (drops 9 dB+, nothing after it) is a tail, not playing
        for (int i = 1; i + 1 < NB; ++i)
            if (on[(size_t) i] && on[(size_t) i - 1] && ! on[(size_t) i + 1] && d[(size_t) i] < d[(size_t) i - 1] - 9.0) on[(size_t) i] = 0;
    };
    auto noteLane = [&] (const std::vector<const std::vector<Note>*>& notes, bool drum, std::vector<unsigned char>& on, std::vector<double>& lvl)
    {
        on.assign ((size_t) NB, 0); lvl.assign ((size_t) NB, 0.0);
        std::vector<double> cover ((size_t) NB, 0.0), vel ((size_t) NB, 0.0), cnt ((size_t) NB, 0.0);
        for (auto* v : notes)
            for (auto& n : *v)
            {
                if (drum)
                {
                    const int i = (int) std::floor ((n.start - bar0) / P + 0.125) + kPre;
                    if (i >= 0 && i < NB) { cover[(size_t) i] = 1.0; vel[(size_t) i] += n.velocity; cnt[(size_t) i] += 1; }
                    continue;
                }
                const int i0 = std::max (0, (int) std::floor ((n.start - bar0) / P) + kPre), i1 = std::min (NB - 1, (int) std::floor ((n.end - bar0) / P) + kPre);
                for (int i = i0; i <= i1; ++i)
                {
                    const double a = std::max (n.start, mapBeatT (i - kPre)), b = std::min (n.end, mapBeatT (i - kPre + 1));
                    if (b > a) { cover[(size_t) i] += (b - a) / P; vel[(size_t) i] += n.velocity; cnt[(size_t) i] += 1; }
                }
            }
        for (int i = 0; i < NB; ++i)
        {
            on[(size_t) i] = drum ? cover[(size_t) i] > 0.0 : cover[(size_t) i] >= 0.25;
            lvl[(size_t) i] = cnt[(size_t) i] > 0 ? std::clamp (vel[(size_t) i] / cnt[(size_t) i] / 127.0, 0.0, 1.0) : 0.0;
        }
    };
    auto finishLane = [&] (MapLane& L, std::vector<unsigned char> on, const std::vector<double>& lvl, int maxGap, bool keepLone, int minBeatsPerBar)
    {
        tidy (on, maxGap, keepLone);
        for (int i = 0; i < NB;)
        {
            if (! on[(size_t) i]) { ++i; continue; }
            int j = i; double lv = 0; while (j < NB && on[(size_t) j]) lv += lvl[(size_t) j++];
            MapBlock b; b.start = std::max (0.0, mapBeatT (i - kPre)); b.end = std::min (len, mapBeatT (j - kPre));
            b.velocity = std::clamp ((int) std::lround (40.0 + 87.0 * lv / (j - i)), 1, 127);
            if (b.end > b.start) L.blocks.push_back (b);
            i = j;
        }
        L.barOn.assign ((size_t) nb, 0);
        for (int b = 0; b < nb; ++b)
        {
            int c = 0; for (int q = 0; q < 4; ++q) c += on[(size_t) (kPre + b * 4 + q)];
            L.barOn[(size_t) b] = c >= minBeatsPerBar;
        }
    };

    std::vector<unsigned char> on; std::vector<double> lvl;
    const bool sungMelody = r.melodyFrom == "vocals";
    auto subLanes = [&] (int part, int group, const char* base)
    {
        const auto& subs = r.subs[(size_t) part];
        if (subs.size() < 2) return;
        for (size_t k = 0; k < subs.size(); ++k)
        {
            MapLane L; L.name = std::string (base) + " - " + subs[k].name; L.group = group; L.part = part; L.sub = (int) k; L.child = true;
            noteLane ({ &subs[k].notes }, false, on, lvl); finishLane (L, on, lvl, 2, false, 2);
            m.lanes.push_back (std::move (L));
        }
    };
    auto withInst = [&] (int part, const char* base)
    {
        const auto& subs = r.subs[(size_t) part];
        return subs.size() == 1 ? std::string (base) + " (" + subs[0].name + ")" : std::string (base);
    };

    // drums: the whole kit (stem audio) + one lane per piece
    {
        MapLane L; L.name = "Drums"; L.group = kGrpDrums; L.drumNote = 36; L.midiParts = { kKick, kSnare, kHats, kCymbals };
        if (auto* x = feat.get (kGrpDrums)) { L.fromAudio = true; L.stem = x->stem; audioLane (x, on, lvl); finishLane (L, on, lvl, 1, false, 2); }
        else { noteLane ({ &r.parts[kKick], &r.parts[kSnare], &r.parts[kHats], &r.parts[kCymbals] }, true, on, lvl); finishLane (L, on, lvl, 4, true, 1); }
        m.lanes.push_back (std::move (L));
        for (int p = kKick; p <= kCymbals; ++p)
        {
            MapLane D; D.name = partName (p); D.group = kGrpDrums; D.part = p; D.child = true; D.drumNote = drumNote (p);
            noteLane ({ &r.parts[(size_t) p] }, true, on, lvl); finishLane (D, on, lvl, p == kCymbals ? 0 : 4, true, 1);
            m.lanes.push_back (std::move (D));
        }
    }
    // bass
    {
        MapLane L; L.name = withInst (kBass, "Bass"); L.group = kGrpBass; L.part = kBass;
        if (auto* x = feat.get (kGrpBass)) { L.fromAudio = true; L.stem = x->stem; audioLane (x, on, lvl); finishLane (L, on, lvl, 1, false, 2); }
        else { noteLane ({ &r.parts[kBass] }, false, on, lvl); finishLane (L, on, lvl, 2, false, 2); }
        m.lanes.push_back (std::move (L));
        subLanes (kBass, kGrpBass, "Bass");
    }
    // lead / melody (in song mode with a sung melody the melody is the vocal lane, so the lead lane is the instruments' top line)
    const bool leadIsVoice = feat.leadIsVoice;
    if (! leadIsVoice && ! (sungMelody && feat.get (kGrpLead) == nullptr))
    {
        MapLane L; L.name = withInst (kMelody, "Lead"); L.group = kGrpLead; L.part = kMelody;
        if (auto* x = feat.get (kGrpLead)) { L.fromAudio = true; L.stem = x->stem; audioLane (x, on, lvl); finishLane (L, on, lvl, 1, false, 2); }
        else { noteLane ({ &r.parts[kMelody] }, false, on, lvl); finishLane (L, on, lvl, 2, false, 2); }
        m.lanes.push_back (std::move (L));
        subLanes (kMelody, kGrpLead, "Lead");
    }
    // chords
    {
        MapLane L; L.name = withInst (kChords, "Chords"); L.group = kGrpChords; L.part = kChords;
        if (auto* x = feat.get (kGrpChords)) { L.fromAudio = true; L.stem = x->stem; audioLane (x, on, lvl); finishLane (L, on, lvl, 1, false, 2); }
        else { noteLane ({ &r.parts[kChords] }, false, on, lvl); finishLane (L, on, lvl, 2, false, 2); }
        m.lanes.push_back (std::move (L));
        subLanes (kChords, kGrpChords, "Chords");
    }
    // music: everything that is not drums / bass / vocals (song mode only: Spleeter's "other" stem)
    if (auto* x = feat.get (kGrpMusic))
    {
        MapLane L; L.name = "Music"; L.group = kGrpMusic; L.fromAudio = true; L.stem = x->stem; L.midiParts = { kChords };
        if (! sungMelody) L.midiParts.push_back (kMelody);
        audioLane (x, on, lvl); finishLane (L, on, lvl, 1, false, 2);
        m.lanes.push_back (std::move (L));
    }
    // vocal map: the vocal stem; without one, the notes labelled "Vocals" (or an empty lane, so the slot is always there)
    {
        MapLane L; L.name = "Vocals"; L.group = kGrpVocals;
        if (sungMelody || leadIsVoice) L.part = kMelody;
        // no gap closing: the vocal map shows the phrases, a one-beat breath between two lines stays a gap
        if (auto* x = feat.get (kGrpVocals)) { L.fromAudio = true; L.stem = x->stem; audioLane (x, on, lvl); finishLane (L, on, lvl, 0, false, 2); }
        else
        {
            std::vector<const std::vector<Note>*> v;
            if (sungMelody) v.push_back (&r.parts[kMelody]);
            else for (int p : { (int) kMelody, (int) kChords })
                for (size_t k = 0; k < r.subs[(size_t) p].size(); ++k)
                    if (r.subs[(size_t) p][k].inst == kInstVocals) { v.push_back (&r.subs[(size_t) p][k].notes); if (L.part < 0) { L.part = p; L.sub = (int) k; } }
            noteLane (v, false, on, lvl); finishLane (L, on, lvl, 2, false, 2);
        }
        m.lanes.push_back (std::move (L));
    }

    // ---------------------------------------------------------------- sections
    // per-bar features: stem levels, drum-piece density, lead / chord activity
    struct Feat { std::vector<double> v; };
    std::vector<std::vector<double>> F ((size_t) nb);
    std::vector<double> fw;
    std::vector<double> bassLvl ((size_t) nb, 0.0), drumLvl ((size_t) nb, 0.0), total ((size_t) nb, 0.0), snareD ((size_t) nb, 0.0), kickD ((size_t) nb, 0.0);
    int iDrumAudio = -1, iBassAudio = -1;
    for (size_t s = 0; s < en.size(); ++s) { if (enGroup[s] == kGrpDrums) iDrumAudio = (int) s; if (enGroup[s] == kGrpBass) iBassAudio = (int) s; }
    for (int b = 0; b < nb; ++b)
    {
        auto& f = F[(size_t) b];
        const double t0 = m.barTime (b), t1 = m.barTime (b + 1);
        for (size_t s = 0; s < en.size(); ++s)
        {
            const double l = std::clamp ((en[s].db (t0, t1) - (ref[s] - 30.0)) / 30.0, 0.0, 1.0);
            f.push_back (l);
            if (b == 0) fw.push_back (enGroup[s] == kGrpDrums || enGroup[s] == kGrpBass ? 1.0 : enGroup[s] == kGrpVocals ? 0.4 : 0.6);
            total[(size_t) b] += l / std::max<size_t> (1, en.size());
            if ((int) s == iDrumAudio) drumLvl[(size_t) b] = l;
            if ((int) s == iBassAudio) bassLvl[(size_t) b] = l;
        }
        const double div[] = { 4.0, 4.0, 16.0, 1.0 };
        for (int p = kKick; p <= kHats; ++p)
        {
            double c = 0; for (auto& n : r.parts[(size_t) p]) if (n.start >= t0 - P * 0.125 && n.start < t1 - P * 0.125) c += 1;
            const double d = std::min (1.0, c / div[p - kKick]);
            f.push_back (d); if (b == 0) fw.push_back (0.7);
            if (p == kSnare) snareD[(size_t) b] = c;
            if (p == kKick) kickD[(size_t) b] = d;
        }
        for (auto& L : m.lanes)
            if (! L.fromAudio && ! L.child && (L.group == kGrpLead || L.group == kGrpChords || L.group == kGrpBass))
            { f.push_back (L.barOn[(size_t) b]); if (b == 0) fw.push_back (L.group == kGrpBass ? 0.8 : 0.3); }
    }
    if (iDrumAudio < 0) for (int b = 0; b < nb; ++b) drumLvl[(size_t) b] = kickD[(size_t) b];
    if (iBassAudio < 0) for (auto& L : m.lanes) if (L.group == kGrpBass && ! L.child) for (int b = 0; b < nb; ++b) bassLvl[(size_t) b] = L.barOn[(size_t) b];
    const size_t nf = fw.size();
    auto meanF = [&] (int a, int b, std::vector<double>& out)   // mean feature vector over bars [a, b)
    {
        out.assign (nf, 0.0); a = std::max (0, a); b = std::min (nb, b);
        if (b <= a) return false;
        for (int i = a; i < b; ++i) for (size_t q = 0; q < nf && q < F[(size_t) i].size(); ++q) out[q] += F[(size_t) i][q];
        for (auto& x : out) x /= (b - a);
        return true;
    };
    std::vector<double> nv ((size_t) nb, 0.0), A, B;
    for (int b = 1; b < nb; ++b)
    {
        double d = 0;
        for (auto [w, g] : { std::pair<int, double> { 2, 0.5 }, { 4, 1.0 }, { 8, 0.5 } })
        {
            if (! meanF (b - w, b, A) || ! meanF (b, b + w, B)) continue;
            double s = 0; for (size_t q = 0; q < nf; ++q) s += fw[q] * std::abs (A[q] - B[q]);
            d += g * s;
        }
        nv[(size_t) b] = d / 2.0;
    }
    std::vector<std::pair<double, int>> cand;
    double maxS = 0;
    for (int b = 2; b <= nb - 2; ++b)
    {
        const double prior = b % 8 == 0 ? 1.0 : b % 4 == 0 ? 0.8 : b % 2 == 0 ? 0.5 : 0.3;
        const double s = nv[(size_t) b] * prior;
        cand.push_back ({ s, b }); maxS = std::max (maxS, s);
    }
    std::sort (cand.begin(), cand.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    std::vector<int> bounds;
    for (auto& [s, b] : cand)
    {
        if (s < std::max (0.45, 0.25 * maxS)) break;
        bool ok = true; for (int x : bounds) if (std::abs (x - b) < 4) ok = false;
        if (ok) bounds.push_back (b);
    }
    std::sort (bounds.begin(), bounds.end());
    bounds.insert (bounds.begin(), 0); bounds.push_back (nb);

    // label: intensity = drums + bass + overall level + kick density
    struct Seg { int a, b; double I, drums, bass, snare1, snare2, tot1, tot2; int type = kSecBreak; };
    std::vector<Seg> seg;
    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        Seg s { bounds[i], bounds[i + 1], 0, 0, 0, 0, 0, 0, 0 };
        const int n = s.b - s.a, h = s.a + n / 2;
        for (int b = s.a; b < s.b; ++b)
        {
            s.I += (drumLvl[(size_t) b] + bassLvl[(size_t) b] + total[(size_t) b] + kickD[(size_t) b]) / n;
            s.drums += drumLvl[(size_t) b] / n; s.bass += bassLvl[(size_t) b] / n;
            (b < h ? s.snare1 : s.snare2) += snareD[(size_t) b]; (b < h ? s.tot1 : s.tot2) += total[(size_t) b];
        }
        s.snare1 /= std::max (1, h - s.a); s.snare2 /= std::max (1, s.b - h); s.tot1 /= std::max (1, h - s.a); s.tot2 /= std::max (1, s.b - h);
        seg.push_back (s);
    }
    double Imax = 1e-9, bassMax = 1e-9;
    for (auto& s : seg) { Imax = std::max (Imax, s.I); bassMax = std::max (bassMax, s.bass); }
    for (auto& s : seg) if (s.I >= 0.8 * Imax && s.bass >= 0.55 * bassMax && s.drums > 0.3) s.type = kSecDrop;
    for (size_t i = 0; i + 1 < seg.size(); ++i)
    {
        auto& s = seg[i];
        if (s.type == kSecDrop || seg[i + 1].type != kSecDrop) continue;
        const bool rollUp = s.snare2 > s.snare1 * 1.25 + 0.5, rise = s.tot2 > s.tot1 + 0.08;
        if (rollUp || rise) s.type = kSecBuild;
    }
    if (! seg.empty() && seg.front().type == kSecBreak) seg.front().type = kSecIntro;
    // more low sections before the first build / drop: the same intro unless the drums fall away (a break)
    for (size_t i = 1; i < seg.size() && seg[i].type == kSecBreak && seg[i - 1].type == kSecIntro; ++i)
        if (seg[i].drums > seg[i - 1].drums - 0.3) seg[i].type = kSecIntro;
    // a short (<= 4 bar) dip inside a drop (a switch-up, the pads dropping out) stays part of that drop
    for (size_t i = 1; i + 1 < seg.size(); ++i)
        if (seg[i].type != kSecDrop && seg[i - 1].type == kSecDrop && seg[i + 1].type == kSecDrop && seg[i].b - seg[i].a <= 4) seg[i].type = kSecDrop;
    // everything after the last drop is the outro (a mix-out); a song without a drop keeps its last section as the outro
    {
        int lastDrop = -1; for (size_t i = 0; i < seg.size(); ++i) if (seg[i].type == kSecDrop) lastDrop = (int) i;
        if (lastDrop >= 0) { for (size_t i = (size_t) lastDrop + 1; i < seg.size(); ++i) seg[i].type = kSecOutro; }
        else if (seg.size() > 1) seg.back().type = kSecOutro;
    }
    // merge neighbours with the same label, then number Build / Drop / Break
    std::vector<Seg> mg;
    for (auto& s : seg) { if (! mg.empty() && mg.back().type == s.type) mg.back().b = s.b; else mg.push_back (s); }
    int count[kNumSecTypes] = {};
    for (auto& s : mg) ++count[s.type];
    int idx[kNumSecTypes] = {};
    for (auto& s : mg)
    {
        MapSection sec; sec.type = s.type; sec.startBar = s.a; sec.endBar = s.b;
        sec.label = sectionTypeName (s.type);
        if ((s.type == kSecBuild || s.type == kSecDrop || s.type == kSecBreak) && count[s.type] > 1) sec.label += " " + std::to_string (++idx[s.type]);
        m.sections.push_back (sec);
    }
    (void) k0;
    m.ok = true;
    m.buildMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - clock0).count();
    return m;
}
} // namespace
} // namespace rw
