// Rewired engine tests (no models needed): resampling, tempo, drum split, melody / chord split, monophonic lines, quantize, edge cases, speed.
// Built on its own: the engine sources are plain C++17 (ONNX Runtime is loaded at run time, never linked).
#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wunused-result"   // ORT C API statuses on optional settings
#endif
#include "../src/RewiredEngine.cpp"
#include "../src/OrtModel.cpp"
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
    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails;
}
