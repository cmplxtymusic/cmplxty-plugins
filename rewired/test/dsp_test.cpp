// Rewired engine tests (no models needed): resampling, tempo, drum split, melody / chord split, monophonic lines, quantize, edge cases, speed,
// and (1.1.0) the song map on a synthetic arrangement.
// Built on its own: the engine sources are plain C++17 (ONNX Runtime is loaded at run time, never linked).
#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wunused-result"   // ORT C API statuses on optional settings
#endif
#include "../src/RewiredEngine.cpp"
#include "../src/OrtModel.cpp"
#include "../src/Instruments.cpp"
#include "../src/SongMap.cpp"
#include "../src/Preview.h"
#include <cstdio>
#include <random>
#include <chrono>

using namespace rw;
static int fails = 0;
#define CHECK(c, ...) do { if (! (c)) { ++fails; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
static const double kTwoPi = 6.283185307179586;

// a drum loop at 44.1k: kick (pitched-down sine thump), snare (noise + 190 Hz body), closed hats (2nd-difference noise: bright, little body)
static std::vector<float> drumLoop (double bpm, int beats, std::vector<double>* kicks, std::vector<double>* snares, std::vector<double>* hats)
{
    const double sr = 44100.0, beat = 60.0 / bpm;
    std::vector<float> x ((size_t) (sr * (beats * beat + 1.0)));
    std::mt19937 rng (7); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    float p1 = 0.0f, p2 = 0.0f;
    for (int b = 0; b < beats; ++b)
    {
        const double t = 0.2 + b * beat;
        if (b % 2 == 0) { if (kicks) kicks->push_back (t); double ph = 0;
            for (int i = 0; i < (int) (0.25 * sr); ++i) { const double tt = i / sr; ph += (50.0 + 90.0 * std::exp (-tt * 40.0)) / sr; x[(size_t) (t * sr) + i] += (float) (0.9 * std::exp (-tt * 12.0) * std::sin (kTwoPi * ph)); } }
        else { if (snares) snares->push_back (t);
            for (int i = 0; i < (int) (0.18 * sr); ++i) { const double tt = i / sr; x[(size_t) (t * sr) + i] += (float) (std::exp (-tt * 22.0) * (0.45 * u (rng) + 0.35 * std::sin (kTwoPi * 190.0 * tt))); } }
        const double th = t + beat * 0.5;
        if (hats) hats->push_back (th);
        for (int i = 0; i < (int) (0.05 * sr); ++i) { const float n = u (rng), hp = 0.25f * (n - 2.0f * p1 + p2); p2 = p1; p1 = n; x[(size_t) (th * sr) + i] += (float) (0.6 * std::exp (-i / sr * 70.0)) * hp; }
    }
    return x;
}

static int matches (const std::vector<Note>& est, const std::vector<double>& ref, double tol = 0.05)
{
    int m = 0; std::vector<bool> used (est.size());
    for (double r : ref) for (size_t i = 0; i < est.size(); ++i) if (! used[i] && std::abs (est[i].start - r) < tol) { used[i] = true; ++m; break; }
    return m;
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    // ---- resample
    {
        std::vector<float> x (48000); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (kTwoPi * 1000.0 * i / 48000.0);
        auto y = resample (x, 48000.0, 44100.0);
        CHECK (std::abs ((int) y.size() - 44100) <= 2, "48k -> 44.1k length (%d)", (int) y.size());
        int zc = 0; for (size_t i = 1000; i < 43000; ++i) zc += (y[i - 1] < 0) != (y[i] < 0);
        const double f = zc / 2.0 / (42000.0 / 44100.0);
        CHECK (std::abs (f - 1000.0) < 5.0, "a 1 kHz tone stays at 1 kHz (%.1f)", f);
        CHECK (resample ({}, 48000.0, 44100.0).empty(), "empty in, empty out");
    }
    // ---- tempo + drums
    for (double bpm : { 90.0, 120.0, 140.0 })
    {
        std::vector<double> k, s, h;
        auto x = drumLoop (bpm, 32, &k, &s, &h);
        const double t = detectTempo (x);
        CHECK (std::abs (t - bpm) < 2.0 || std::abs (t - bpm * 2) < 3.0 || std::abs (t - bpm / 2) < 1.5, "tempo %.0f detected as %.1f", bpm, t);
        auto d = detectDrums (x, 0.5f);
        const int mk = matches (d[0], k), ms = matches (d[1], s), mh = matches (d[2], h);
        CHECK (mk >= (int) k.size() * 9 / 10 && (int) d[0].size() <= (int) k.size() + 2, "%.0f BPM kicks %d/%d (found %d)", bpm, mk, (int) k.size(), (int) d[0].size());
        CHECK (ms >= (int) s.size() * 9 / 10 && (int) d[1].size() <= (int) s.size() + 2, "%.0f BPM snares %d/%d (found %d)", bpm, ms, (int) s.size(), (int) d[1].size());
        CHECK (mh >= (int) h.size() * 8 / 10, "%.0f BPM hats %d/%d (found %d)", bpm, mh, (int) h.size(), (int) d[2].size());
        CHECK (d[3].size() <= 2, "%.0f BPM no phantom cymbals (%d)", bpm, (int) d[3].size());
        for (auto& n : d[0]) if (n.pitch != 36) { CHECK (false, "kick note number"); break; }
    }
    {
        std::vector<float> silent (44100 * 3, 0.0f);
        auto d = detectDrums (silent, 1.0f);
        CHECK (d[0].empty() && d[1].empty() && d[2].empty() && d[3].empty(), "silence: no drum hits even at full sensitivity");
        const double t = detectTempo (silent);
        CHECK (t >= 60.0 && t <= 200.0, "silence: tempo falls back to a sane value (%.1f)", t);
        CHECK (detectDrums ({}, 0.5f)[0].empty(), "empty drums input");
        std::vector<double> k; auto x = drumLoop (120, 16, &k, nullptr, nullptr);
        CHECK (detectDrums (x, 0.9f)[0].size() >= detectDrums (x, 0.1f)[0].size(), "higher sensitivity never finds fewer kicks");
    }
    // ---- melody / chords split
    {
        std::vector<Note> all;
        for (int b = 0; b < 8; ++b)
        {
            const double t = b * 1.0;
            for (int p : { 48, 52, 55 }) all.push_back ({ t, t + 0.95, p, 80 });                // chord hits
            all.push_back ({ t + 0.25, t + 0.5, 72 + (b % 3), 100 });                          // melody between hits
            all.push_back ({ t + 0.5, t + 0.75, 74, 100 });
        }
        all.push_back ({ 8.0, 8.9, 48, 80 }); all.push_back ({ 8.0, 8.9, 52, 80 }); all.push_back ({ 8.0, 8.9, 76, 100 });   // a held top note on a chord hit, clear on top
        std::vector<Note> mel, ch;
        splitMelodyChords (all, mel, ch, 2.0f, 7);
        int chordsIn = 0; for (auto& n : ch) chordsIn += n.pitch < 60;
        CHECK (chordsIn == 26 && (int) ch.size() == 26, "all chord tones land in Chords (%d of %d)", chordsIn, (int) ch.size());
        CHECK ((int) mel.size() == 17, "melody keeps the line, including a top note on a chord hit (%d)", (int) mel.size());
        std::vector<Note> mel2, ch2; splitMelodyChords (all, mel2, ch2, 2.0f, 99);
        CHECK ((int) mel2.size() == 16, "a huge melody gap leaves the chord-hit top note in Chords (%d)", (int) mel2.size());
        std::vector<Note> m3, c3; splitMelodyChords ({}, m3, c3, 2.0f, 7);
        CHECK (m3.empty() && c3.empty(), "empty split");
    }
    // ---- monophonic
    {
        std::vector<Note> v { { 0.0, 1.0, 40, 90 }, { 0.0, 1.0, 52, 60 }, { 1.0, 2.0, 43, 90 }, { 1.5, 2.5, 47, 90 } };
        auto lo = monophonic (v, true);
        bool overlap = false; for (size_t i = 1; i < lo.size(); ++i) overlap |= lo[i].start < lo[i - 1].end - 1e-9;
        CHECK (! overlap && lo.front().pitch == 40, "bass line is monophonic and keeps the lowest note");
        auto hi = monophonic (v, false);
        CHECK (hi.front().pitch == 52, "lead line keeps the highest note");
    }
    // ---- quantize
    {
        std::vector<Note> v { { 0.51, 0.74, 60, 100 }, { 1.13, 1.2, 62, 100 } };
        quantizeNotes (v, 120.0, 1);   // 1/16 at 120 = 0.125 s
        CHECK (std::abs (v[0].start - 0.5) < 1e-6 && std::abs (v[1].start - 1.125) < 1e-6, "1/16 grid at 120 BPM (%.3f, %.3f)", v[0].start, v[1].start);
        CHECK (v[1].end > v[1].start, "quantized notes keep a length");
        std::vector<Note> w { { 0.51, 0.74, 60, 100 } }; quantizeNotes (w, 120.0, 0);
        CHECK (std::abs (w[0].start - 0.51) < 1e-9, "quantize off leaves notes alone");
    }
    // ---- a model-free engine fails cleanly
    {
        Engine e; Audio a; a.left.assign (44100, 0.0f);
        auto r = e.transcribeSong (a, {}, nullptr, {});
        CHECK (! r.ok && ! r.error.empty(), "transcribe without models reports an error (%s)", r.error.c_str());
        std::string err; CHECK (! e.load ("/nonexistent-dir", err) && ! err.empty(), "loading from a missing folder fails with a message");
    }
    // ---- speed
    {
        auto x = drumLoop (128, 256, nullptr, nullptr, nullptr);   // ~2 minutes
        const auto t0 = std::chrono::steady_clock::now();
        auto d = detectDrums (x, 0.5f); const double t = detectTempo (x); (void) t;
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        CHECK (ms < 4000.0, "2 min of drums split + tempo in %.0f ms", ms);
    }
    // ---- 1.1.0 song map: 24 bars at 126 bpm, starting 0.5 s in. Bars 0-7 pads only, 8-15 drums + bass (drop), 16-23 pads + vocal.
    {
        const double sr = 44100.0, bpm = 126.0, beat = 60.0 / bpm, bar = 4 * beat, off = 0.5;
        const size_t n = (size_t) (sr * (off + 24 * bar + 1.0));
        std::vector<float> drums (n, 0.0f), bass (n, 0.0f), music (n, 0.0f), vocals (n, 0.0f);
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        Result r; r.ok = true; r.bpm = 126.3; r.seconds = (double) n / sr; r.melodyFrom = "instruments";
        for (int b = 0; b < 24; ++b)
        {
            const double t0 = off + b * bar;
            const bool drop = b >= 8 && b < 16;
            for (size_t i = (size_t) (t0 * sr); i < (size_t) ((t0 + bar) * sr) && i < n; ++i)
            {
                const double t = (double) i / sr;
                if (! drop) music[i] += (float) (0.2 * std::sin (kTwoPi * 220.0 * t));
                if (drop) bass[i] += (float) (0.4 * std::sin (kTwoPi * 55.0 * t));
                if (b >= 16 && std::fmod (t - t0, bar) < bar * 0.6) vocals[i] += (float) (0.2 * std::sin (kTwoPi * 330.0 * t));
            }
            if (drop)
                for (int k = 0; k < 4; ++k)
                {
                    const double t = t0 + k * beat;
                    r.parts[kKick].push_back ({ t, t + 0.1, 36, 110 });
                    if (k % 2) r.parts[kSnare].push_back ({ t, t + 0.1, 38, 100 });
                    for (size_t i = 0; i < (size_t) (0.12 * sr); ++i) drums[(size_t) (t * sr) + i] += (float) (0.8 * std::exp (-30.0 * i / sr)) * u (rng);
                }
            if (b == 8) r.parts[kCymbals].push_back ({ t0, t0 + 0.5, 49, 100 });
            if (drop) r.parts[kBass].push_back ({ t0, t0 + bar * 0.95, 33, 100 });
            if (! drop) r.parts[kChords].push_back ({ t0, t0 + bar * 0.95, 57, 80 });
        }
        MapSources ms; ms.drums = &drums; ms.bass = &bass; ms.music = &music; ms.vocals = &vocals;
        const auto feat = makeMapFeatures (ms, { 0, 1, -1, -1, 2, 3 });
        const auto m = buildSongMap (r, feat);
        CHECK (m.ok && std::abs (m.bpm - 126.0) < 0.01, "map: tempo refined to 126 (detected 126.3): %.3f", m.bpm);
        CHECK (std::abs (m.firstDownbeat - off) < 0.03, "map: bar 1 found at %.3f s (0.5 s)", m.firstDownbeat);
        CHECK (m.bars == 24, "map: 24 bars (%d)", m.bars);
        std::string secs; for (auto& sc : m.sections) secs += sc.label + " " + std::to_string (sc.startBar) + "-" + std::to_string (sc.endBar) + "  ";
        CHECK (m.sections.size() == 3 && m.sections[0].type == kSecIntro && m.sections[1].type == kSecDrop && m.sections[2].type == kSecOutro
               && m.sections[1].startBar == 8 && m.sections[2].startBar == 16, "map: Intro / Drop / Outro at bars 1, 9, 17: %s", secs.c_str());
        auto lane = [&] (const char* nm) -> const MapLane* { for (auto& L : m.lanes) if (L.name == nm) return &L; return nullptr; };
        auto bars = [] (const MapLane* L) { std::string s; if (L) for (auto b : L->barOn) s += b ? '1' : '0'; return s; };
        const std::string drop = "000000001111111100000000", calm = "111111110000000011111111", voc = "000000000000000011111111";
        CHECK (bars (lane ("Drums")) == drop && bars (lane ("Kick")) == drop && bars (lane ("Bass")) == drop, "map: drums / kick / bass lanes play in the drop only");
        CHECK (bars (lane ("Music")) == calm, "map: music lane plays outside the drop: %s", bars (lane ("Music")).c_str());
        CHECK (bars (lane ("Vocals")) == voc && lane ("Vocals")->blocks.size() == 8, "map: vocal lane = 8 phrases in the last 8 bars: %s (%zu blocks)", bars (lane ("Vocals")).c_str(), lane ("Vocals") ? lane ("Vocals")->blocks.size() : 0);
        CHECK (m.buildMs < 200.0, "map: built in %.1f ms", m.buildMs);
        Result empty; empty.seconds = 2.0;
        CHECK (buildSongMap (empty, {}).ok, "map: an empty result still gives a (blank) map without crashing");
        CHECK (lane ("Drums")->stem == 0 && lane ("Music")->stem == 2 && lane ("Kick")->stem == -1, "map: audio lanes know their preview stem");

        // ---- 1.1.0 BPM edit: regrid() puts the same result on another tempo without the audio (envelopes only)
        Result g = r; g.mapFeat = feat; g.detectedBpm = 126.3;
        regrid (g, 0.0, 0);
        CHECK (g.bpm == 126.0 && g.map.bpm == 126.0 && g.map.bars == 24 && g.map.sections.size() == 3, "regrid auto: whole 126 bpm, 24 bars, 3 sections (%.2f, %d)", g.map.bpm, g.map.bars);
        regrid (g, 63.0, 0);
        CHECK (g.bpm == 63.0 && g.map.bpm == 63.0 && g.map.bars == 12 && std::abs (g.map.firstDownbeat - off) < 0.03, "regrid 63 (half): 12 bars, bar 1 still at 0.5 s (%d, %.3f)", g.map.bars, g.map.firstDownbeat);
        std::string sec63; for (auto& sc : g.map.sections) sec63 += std::to_string (sc.startBar) + " ";
        CHECK (g.map.sections.size() >= 2 && g.map.sections[1].startBar == 4, "regrid 63: the drop starts on (half-tempo) bar 5: %s", sec63.c_str());
        regrid (g, 252.0, 0);
        CHECK (g.map.bpm == 252.0 && g.map.bars == 48, "regrid 252 (double): 48 bars (%d)", g.map.bars);
        regrid (g, 126.4, 0);
        CHECK (g.bpm == 126.0, "regrid rounds a typed tempo to a whole bpm (%.2f)", g.bpm);
        // quantize follows bar 1 (0.5 s in), not the start of the file
        Result q = g; q.rawParts[kBass] = { { off + 0.03, off + 1.0, 33, 100 } }; q.parts = q.rawParts;
        regrid (q, 126.0, 1);
        const double step16 = 60.0 / 126.0 / 4.0;
        CHECK (std::abs (q.parts[kBass][0].start - off) < 1e-6 && q.rawParts[kBass][0].start == off + 0.03,
               "quantize 1/16 snaps onto the bar-1 grid (%.4f -> %.4f), unquantized notes kept", off + 0.03, q.parts[kBass][0].start);
        regrid (q, 126.0, 0);
        CHECK (std::abs (q.parts[kBass][0].start - (off + 0.03)) < 1e-9, "quantize off again gives the original notes back");
        (void) step16;
        // speed: a BPM drag re-grids on every step
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 10; ++k) regrid (g, 120.0 + k, 0);
        const double rgMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 10.0;
        CHECK (rgMs < 30.0, "regrid takes %.2f ms", rgMs);
    }

    // ---- 1.1.0 preview audio
    {
        std::vector<float> x (44100); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin (kTwoPi * 1000.0 * (double) i / 44100.0)) + (float) (0.3 * std::sin (kTwoPi * 18000.0 * (double) i / 44100.0));
        const auto y = decimate2 (x);
        double e = 0; for (size_t i = 1000; i < 21000; ++i) e += (double) y[i] * y[i]; e = std::sqrt (e / 20000.0) / 32767.0;
        CHECK (y.size() == 22050 && std::abs (e - 0.5 / std::sqrt (2.0)) < 0.02, "decimate2: 44.1k -> 22.05k keeps 1 kHz, removes 18 kHz (rms %.3f, want 0.354)", e);
        std::vector<Note> mel { { 0.5, 1.0, 69, 100 } }, kit { { 0.25, 0.35, 36, 120 }, { 1.25, 1.35, 42, 90 } };
        const auto a = renderNotes ({ &mel }, false, 2.0), d = renderNotes ({ &kit }, true, 2.0);
        auto peak = [] (const std::vector<int16_t>& v, double t0, double t1) { int p = 0; for (size_t i = (size_t) (t0 * 22050); i < (size_t) (t1 * 22050) && i < v.size(); ++i) p = std::max (p, std::abs ((int) v[i])); return p; };
        CHECK (a.size() == 44100 && peak (a, 0.0, 0.49) == 0 && peak (a, 0.55, 0.95) > 3000 && peak (a, 1.3, 2.0) == 0, "renderNotes: a note sounds only where it is");
        CHECK (peak (d, 0.26, 0.4) > 8000 && peak (d, 1.26, 1.3) > 1000 && peak (d, 0.6, 1.2) < 200, "renderNotes: drum hits sound (kick, hat), silence between");
    }

    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails;
}
