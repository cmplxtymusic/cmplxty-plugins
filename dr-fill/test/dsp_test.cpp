// Dr Fill engine tests (plain C++17, no JUCE): onset detection accuracy on synthetic "real-ish" kick / snare / hat loops at several tempos,
// tempo detection, hit sorting, fills on the grid at the host tempo, no clipping, no clicks at slice edges, determinism, speed.
// DRF_MEASURE_DIR=<dir> also writes the loops and some rendered fills as WAVs there (for the spectrum / waveform plots).
#include "TestLoops.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>

using namespace drf;
using namespace drftest;
static int fails = 0;
#define CHECK(c, ...) do { if (! (c)) { ++fails; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// greedy one-to-one matching within tol seconds; returns matches and the mean |error|
static int matchOnsets (const std::vector<double>& est, const std::vector<double>& ref, double tol, double* meanErr, double* maxErr)
{
    std::vector<bool> used (est.size(), false); int m = 0; double e = 0, mx = 0;
    for (double r : ref)
    {
        int bi = -1; double bd = tol;
        for (size_t i = 0; i < est.size(); ++i) if (! used[i] && std::fabs (est[i] - r) <= bd) { bd = std::fabs (est[i] - r); bi = (int) i; }
        if (bi >= 0) { used[(size_t) bi] = true; ++m; e += bd; mx = std::max (mx, bd); }
    }
    if (meanErr) *meanErr = m ? e / m : 0; if (maxErr) *maxErr = mx;
    return m;
}

// ---------------------------------------------------------------- WAV out (16-bit... no: 32-bit float) for the measurements
static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>* R, double sr)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const int ch = R ? 2 : 1; const uint32_t n = (uint32_t) L.size(), bytes = n * ch * 4, srI = (uint32_t) sr, br = srI * ch * 4;
    const uint16_t fmt = 3, chs = (uint16_t) ch, ba = (uint16_t) (ch * 4), bits = 32; const uint32_t sz = 36 + bytes, f16 = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&sz, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&f16, 4, 1, f);
    std::fwrite (&fmt, 2, 1, f); std::fwrite (&chs, 2, 1, f); std::fwrite (&srI, 4, 1, f); std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); if (R) std::fwrite (&(*R)[i], 4, 1, f); }
    std::fclose (f);
}

static Kit makeKit (double bpm, int variant, double sr = 44100.0)
{
    Kit k;
    for (int l = 0; l < kLanes; ++l) { auto t = makeLoop (l, bpm, variant, sr); k[(size_t) l].audio = t.a; k[(size_t) l].name = std::string (laneName (l)) + " loop"; analyse (k[(size_t) l], l); }
    return k;
}

int main()
{
    const char* mdir = std::getenv ("DRF_MEASURE_DIR");
    // ---------------------------------------------------------------- 1. onset detection
    {
        int tp = 0, nEst = 0, nRef = 0; double errSum = 0; int errN = 0; double worstMax = 0; float worstF1 = 1; std::string worst;
        std::map<std::string, std::array<int, 3>> perVar;
        for (double bpm : { 85.0, 100.0, 120.0, 140.0, 174.0 })
            for (int v = 0; v < 3; ++v)
                for (int l = 0; l < kLanes; ++l)
                {
                    auto t = makeLoop (l, bpm, v);
                    const auto on = detectOnsets (t.a);
                    std::vector<double> est; for (int s : on) est.push_back (s / t.a.rate);
                    double me = 0, mx = 0; const int m = matchOnsets (est, t.truth, 0.025, &me, &mx);
                    tp += m; nEst += (int) est.size(); nRef += (int) t.truth.size(); errSum += me * m; errN += m; worstMax = std::max (worstMax, mx);
                    const float p = est.empty() ? 0 : (float) m / est.size(), r = (float) m / t.truth.size(), f1 = p + r > 0 ? 2 * p * r / (p + r) : 0;
                    auto& pv = perVar[std::to_string (v)]; pv[0] += m; pv[1] += (int) est.size(); pv[2] += (int) t.truth.size();
                    if (f1 < worstF1) { worstF1 = f1; char b[96]; std::snprintf (b, sizeof b, "%s %g bpm variant %d (%d/%d found, %d extra)", laneName (l), bpm, v, m, (int) t.truth.size(), (int) est.size() - m); worst = b; }
                    if (mdir && bpm == 120.0) { char fn[512]; std::snprintf (fn, sizeof fn, "%s/loop_%s_120_v%d.wav", mdir, laneName (l), v); writeWav (fn, t.a.L, t.a.stereo() ? &t.a.R : nullptr, t.a.rate);
                        std::snprintf (fn, sizeof fn, "%s/loop_%s_120_v%d.csv", mdir, laneName (l), v); if (FILE* cf = std::fopen (fn, "w")) { std::fprintf (cf, "truth_s,detected_s\n");
                        for (size_t i = 0; i < std::max (t.truth.size(), est.size()); ++i) std::fprintf (cf, "%s,%s\n", i < t.truth.size() ? std::to_string (t.truth[i]).c_str() : "", i < est.size() ? std::to_string (est[i]).c_str() : ""); std::fclose (cf); } }
                    if (mdir && bpm == 140.0 && v == 1) { char fn[512]; std::snprintf (fn, sizeof fn, "%s/loop_%s_140_v1.wav", mdir, laneName (l)); writeWav (fn, t.a.L, nullptr, t.a.rate); }
                }
        const double P = (double) tp / nEst, R = (double) tp / nRef, F1 = 2 * P * R / (P + R);
        std::printf ("ONSETS: %d loops, %d hits: precision %.3f recall %.3f F1 %.3f, mean timing error %.2f ms, worst %.2f ms; worst loop F1 %.3f (%s)\n",
                     5 * 3 * 3, nRef, P, R, F1, errSum / std::max (1, errN) * 1000, worstMax * 1000, worstF1, worst.c_str());
        for (auto& kv : perVar) { const double p = (double) kv.second[0] / kv.second[1], r = (double) kv.second[0] / kv.second[2];
            std::printf ("  variant %s: F1 %.3f (%d / %d hits, %d extra)\n", kv.first.c_str(), 2 * p * r / (p + r), kv.second[0], kv.second[2], kv.second[1] - kv.second[0]); }
        CHECK (F1 >= 0.97, "onset F1 %.3f >= 0.97 over 45 loops (25 ms window)", F1);
        CHECK (errSum / std::max (1, errN) * 1000 < 2.0, "mean onset timing error %.2f ms < 2 ms", errSum / std::max (1, errN) * 1000);
        CHECK (worstF1 >= 0.9, "worst single loop F1 %.3f >= 0.9", worstF1);
        // 48 k / 96 k
        for (double sr : { 48000.0, 96000.0 })
        {
            auto t = makeLoop (kSnare, 128.0, 1, sr); auto on = detectOnsets (t.a); std::vector<double> est; for (int s : on) est.push_back (s / sr);
            double me = 0; const int m = matchOnsets (est, t.truth, 0.025, &me, nullptr);
            CHECK (m == (int) t.truth.size() && est.size() == t.truth.size(), "snare loop with ghosts at %g Hz: %d / %d found, %d extra, %.2f ms mean error", sr, m, (int) t.truth.size(), (int) est.size() - m, me * 1000);
        }
    }
    // ---------------------------------------------------------------- 2. hit sorting
    {
        auto k = makeKit (120, 1);
        int ghosts = 0; for (auto& h : k[kSnare].hits) ghosts += h.kind == 1;
        CHECK (ghosts == 4 && k[kSnare].hits[(size_t) k[kSnare].main].kind == 0, "snare loop: 4 ghost notes found, main hit is a full snare (%d ghosts)", ghosts);
        int c = 0, o = 0, cr = 0; for (auto& h : k[kHats].hits) { c += h.kind == kClosed; o += h.kind == kOpen; cr += h.kind == kCrash; }
        CHECK (c == 14 && o == 2 && cr == 1, "hat loop: 14 closed, 2 open, 1 crash (%d / %d / %d)", c, o, cr);
        std::string dec; for (auto& h : k[kHats].hits) { char b[16]; std::snprintf (b, sizeof b, "%.0f ", h.decayMs); dec += b; }
        std::printf ("  hat decays (ms): %s\n", dec.c_str());
        // slices: start before the transient, end before the next one
        bool sl = true; for (auto& lp : k) for (size_t i = 0; i < lp.hits.size(); ++i) { auto& h = lp.hits[i]; sl = sl && h.start <= h.onset && h.onset < h.end && (i + 1 >= lp.hits.size() || h.end <= lp.hits[i + 1].onset); }
        CHECK (sl, "every slice starts at / before its transient and ends before the next one");
    }
    // ---------------------------------------------------------------- 3. tempo
    {
        int ok = 0, n = 0; std::string bad;
        for (double bpm : { 85.0, 92.0, 100.0, 120.0, 128.0, 140.0, 150.0, 174.0 })
            for (int v = 0; v < 3; ++v)
            {
                auto k = makeKit (bpm, v);
                // crash tails make the hat loop longer than 2 bars: the tempo must come from the hits, not only the length
                const auto t = estimateTempo (k);
                ++n; if (std::fabs (t.bpm - bpm) < 0.1) ++ok; else { char b[64]; std::snprintf (b, sizeof b, "%g->%.2f(v%d) ", bpm, t.bpm, v); bad += b; }
            }
        CHECK (ok == n, "tempo from the loops: %d / %d within 0.1 BPM %s", ok, n, bad.c_str());
        Kit one; auto t = makeLoop (kKick, 100, 0); one[kKick].audio = t.a; analyse (one[kKick], kKick);
        CHECK (std::fabs (estimateTempo (one).bpm - 100) < 0.1, "tempo from a kick loop on its own: %.2f (100)", estimateTempo (one).bpm);
        CHECK (tempoFromName ("Snare Loop 140 BPM") == 140 && tempoFromName ("hats_92bpm") == 92 && tempoFromName ("kick 174") == 174 && tempoFromName ("808 loop 01") == 0
               && tempoFromName ("Kick 2") == 0, "tempo in file names (140 BPM / 92bpm / kick 174; not 808 or 01)");
        Kit named; auto t2 = makeLoop (kSnare, 130, 0); named[kSnare].audio = t2.a; named[kSnare].name = "snare 130bpm"; analyse (named[kSnare], kSnare);
        CHECK (estimateTempo (named).bpm == 130 && estimateTempo (named).fromName, "file name tempo used when it fits the hits");
    }
    // ---------------------------------------------------------------- 4. fills: on the grid, no clipping, no clicks, every style / length
    {
        double worstGrid = 0, sumGrid = 0; int nGrid = 0; float worstPeak = 0, worstEdge = 0; int renders = 0; double tRender = 0;
        int emptyFills = 0; std::string emptyWhat;
        for (double bpm : { 90.0, 120.0, 140.0, 174.0 })
        {
            auto kit = makeKit (bpm, 1);
            for (double hostBpm : { bpm, 128.0 })
                for (int len : { 1, 2, 4, 8 })
                    for (int st = 0; st < kNumStyles; ++st)
                        for (uint32_t seed : { 1u, 77u, 4242u })
                        {
                            FillSettings s; s.lengthBeats = len; s.style = st; s.seed = seed; s.humanize = 0; s.swing = 0;
                            s.density = (seed % 3) * 0.45f; s.complexity = 0.5f; s.kick = 0.6f; s.hats = 0.5f; s.ending = (int) (seed % 3); s.level = 1.6f;
                            const auto fill = generate (kit, s);
                            const auto t0 = std::chrono::steady_clock::now();
                            const auto r = render (kit, fill, hostBpm, 48000.0, s);
                            tRender += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count(); ++renders;
                            if (fill.events.empty() || r.events == 0) { ++emptyFills; emptyWhat = styleName (st); }
                            worstPeak = std::max (worstPeak, r.peak); worstEdge = std::max (worstEdge, r.maxEdge);
                            // timing: every non-grace event's transient must land on its grid time (no swing / humanize here)
                            for (size_t i = 0; i < fill.events.size() && i < r.eventTimes.size(); ++i)
                            {
                                const auto& e = fill.events[i]; if (e.grace) continue;
                                const double want = e.gridBeat * 60.0 / hostBpm, err = std::fabs (r.eventTimes[i] - want);
                                worstGrid = std::max (worstGrid, err); sumGrid += err; ++nGrid;
                            }
                        }
        }
        std::printf ("FILLS: %d renders (4 tempos x 2 host tempos x 4 lengths x 8 styles x 3 seeds): placement error mean %.4f ms worst %.4f ms, worst peak %.2f dBFS, worst slice edge %.1f dBFS, render %.2f ms avg\n",
                     renders, sumGrid / std::max (1, nGrid) * 1000, worstGrid * 1000, 20 * std::log10 (std::max (1e-9f, worstPeak)), 20 * std::log10 (std::max (1e-9f, worstEdge)), tRender / renders);
        CHECK (emptyFills == 0, "every style / length gives a fill (%d empty, e.g. %s)", emptyFills, emptyWhat.c_str());
        CHECK (worstGrid < 1e-4, "events placed on the host-tempo grid (worst %.4f ms)", worstGrid * 1000);
        CHECK (worstPeak <= 0.8913f, "no clipping: full fill peak %.2f dBFS <= -1 dBFS even at +4 dB level", 20 * std::log10 (worstPeak));
        CHECK (worstEdge < 1e-3f, "no clicks: every slice starts and ends at %.1f dBFS (< -60 dBFS)", 20 * std::log10 (std::max (1e-9f, worstEdge)));
        CHECK (tRender / renders < 40.0, "a fill renders in %.2f ms on average", tRender / renders);
    }
    // ---------------------------------------------------------------- 5. measured timing: onsets detected in the rendered audio vs the grid
    {
        double worst = 0, sum = 0; int n = 0, missed = 0, total = 0, masked = 0, missedClear = 0;
        for (double hostBpm : { 95.0, 128.0, 160.0 })
        {
            auto kit = makeKit (120, 0);
            for (int st : { kRoll, kToms, kBreak, kTriplets })
            {
                FillSettings s; s.lengthBeats = 4; s.style = st; s.seed = 9; s.humanize = 0; s.density = 0.5f; s.hats = 0; s.kick = 0; s.ending = kEndOff; s.complexity = 0;
                const auto fill = generate (kit, s); const auto r = render (kit, fill, hostBpm, 44100.0, s);
                Audio a; a.rate = 44100.0; a.L = r.L[kSnare];
                const auto on = detectOnsets (a, 0.6f);
                std::vector<double> est; for (int x : on) est.push_back (x / a.rate);
                std::vector<double> ref; for (auto& e : fill.events) if (e.lane == kSnare && ! e.grace) ref.push_back (e.gridBeat * 60.0 / hostBpm);
                std::sort (ref.begin(), ref.end()); ref.erase (std::unique (ref.begin(), ref.end(), [] (double x, double y) { return std::fabs (x - y) < 1e-6; }), ref.end());
                double me = 0, mx = 0; const int m = matchOnsets (est, ref, 0.012, &me, &mx);
                worst = std::max (worst, mx); sum += me * m; n += m; missed += (int) ref.size() - m; total += (int) ref.size();
                // a soft hit right after a loud one can be quieter than the tail it cuts off ("masked"): no level-based
                // detector can place it, so it is reported separately; every clearly audible hit must be found
                for (double t : ref)
                {
                    const int c = (int) std::lround (t * a.rate); float before = 0, after = 0;
                    for (int k = std::max (0, c - 662); k < std::max (0, c - 88); ++k) before = std::max (before, std::fabs (a.L[(size_t) k]));
                    for (int k = c; k < std::min ((int) a.L.size(), c + 529); ++k) after = std::max (after, std::fabs (a.L[(size_t) k]));
                    const bool found = std::any_of (est.begin(), est.end(), [t] (double x) { return std::fabs (x - t) < 0.012; });
                    if (after < before * 1.25f) ++masked; else if (! found) ++missedClear;
                }
                if (mdir && hostBpm == 128.0) { char fn[512]; std::snprintf (fn, sizeof fn, "%s/fill_%s_128.wav", mdir, styleName (st)); writeWav (fn, r.L[kSnare], nullptr, 44100.0); }
            }
        }
        std::printf ("TIMING (onsets found in the rendered snare part vs the host grid): %d hits, mean %.2f ms, worst %.2f ms, %d not found (%d masked by the previous hit's tail, %d clearly audible)\n", total, sum / std::max (1, n) * 1000, worst * 1000, missed, masked, missedClear);
        CHECK (missedClear == 0 && missed <= masked && worst < 3.0e-3, "rendered hits land on the grid: worst %.2f ms (< 3 ms), every clearly audible hit found (%d missed, %d masked of %d)", worst * 1000, missedClear, masked, total);
    }
    // ---------------------------------------------------------------- 6. swing / humanize / pitch / ramp / determinism / flams
    {
        auto kit = makeKit (120, 1);
        FillSettings s; s.style = kRoll; s.lengthBeats = 2; s.density = 0.5f; s.complexity = 0; s.humanize = 0; s.kick = 0; s.hats = 0; s.ending = kEndOff;
        s.swing = 1.0f; auto f = generate (kit, s);
        bool swung = false; for (auto& e : f.events) if (std::fabs (e.gridBeat - 0.25) < 1e-9 && std::fabs (e.beat - 0.375) < 1e-9) swung = true;
        CHECK (swung, "swing 100%% moves the off 16th from 0.25 to 0.375 beats");
        s.swing = 0; s.humanize = 1.0f; f = generate (kit, s); double mx = 0; for (auto& e : f.events) mx = std::max (mx, (double) std::fabs (e.offsetMs));
        CHECK (mx > 1.0 && mx <= 12.0, "humanize moves hits (max %.1f ms, capped at 12)", mx);
        s.humanize = 0; s.pitch = -7; f = generate (kit, s); float lastSemis = 0; for (auto& e : f.events) if (e.lane == kSnare) lastSemis = e.semis;
        CHECK (lastSemis < -5.0f, "pitch run -7 st bends the roll down (last hit %.1f st)", lastSemis);
        s.pitch = 0; s.ramp = 1.0f; f = generate (kit, s); const float v0 = f.events.front().vel, v1 = f.events.back().vel;
        CHECK (v1 > v0 * 2.0f, "intensity ramp: last hit %.2f vs first %.2f", v1, v0);
        s.seed = 5; s.style = kAuto; s.lengthBeats = 4; auto a = generate (kit, s), b = generate (kit, s); s.seed = 6; auto c = generate (kit, s);
        bool same = a.events.size() == b.events.size(); for (size_t i = 0; same && i < a.events.size(); ++i) same = a.events[i].beat == b.events[i].beat && a.events[i].hit == b.events[i].hit;
        bool diff = a.events.size() != c.events.size() || a.style != c.style; for (size_t i = 0; ! diff && i < a.events.size(); ++i) diff = a.events[i].beat != c.events[i].beat || a.events[i].lane != c.events[i].lane;
        CHECK (same && diff, "same seed = same fill, new seed = new fill");
        std::set<int> styles; for (uint32_t sd = 1; sd < 60; ++sd) { s.seed = sd; styles.insert (generate (kit, s).style); }
        CHECK (styles.size() >= 6, "Auto picks between %d styles", (int) styles.size());
        s.style = kFlams; s.flam = 1.0f; s.seed = 3; f = generate (kit, s); int graces = 0; for (auto& e : f.events) graces += e.grace;
        CHECK (graces >= 3, "flams add grace notes (%d)", graces);
        s.style = kRoll; s.ending = kEndCrash; f = generate (kit, s); bool crash = false; for (auto& e : f.events) crash |= e.lane == kHats && e.ring && kit[kHats].hits[(size_t) e.hit].kind == kCrash && e.gridBeat == 4;
        CHECK (crash, "crash ending: the loop's crash on the next downbeat");
        auto r = render (kit, f, 120, 48000, s);
        CHECK (r.total > r.fillLen + 24000, "the crash rings past the end of the fill (%.2f s tail)", (r.total - r.fillLen) / 48000.0);
        // parts add up to the full fill, all the same length
        CHECK (r.L[0].size() == r.L[1].size() && r.L[1].size() == r.L[2].size() && r.R[2].size() == r.L[0].size(), "kick / snare / hats parts have the same length");
        if (mdir)
        {
            std::vector<float> fl (r.L[0].size()), fr (r.L[0].size());
            for (size_t i = 0; i < fl.size(); ++i) { fl[i] = r.L[0][i] + r.L[1][i] + r.L[2][i]; fr[i] = r.R[0][i] + r.R[1][i] + r.R[2][i]; }
            writeWav (std::string (mdir) + "/fill_full_roll_crash_120.wav", fl, &fr, 48000);
            if (FILE* cf = std::fopen ((std::string (mdir) + "/fill_full_roll_crash_120.csv").c_str(), "w"))
            {   std::fprintf (cf, "time_s,lane,beat,fill_len_s\n");
                for (size_t i = 0; i < r.eventTimes.size() && i < f.events.size(); ++i) std::fprintf (cf, "%.6f,%d,%.4f,%.6f\n", r.eventTimes[i], f.events[i].lane, f.events[i].beat, r.fillLen / 48000.0);
                std::fclose (cf); }
            for (int l = 0; l < kLanes; ++l) writeWav (std::string (mdir) + "/fill_part_" + laneName (l) + "_120.wav", r.L[(size_t) l], &r.R[(size_t) l], 48000);
            // the same fill with hard cuts (no fades) for the click comparison plot
        }
    }
    // ---------------------------------------------------------------- 7. edge cases
    {
        Kit empty; FillSettings s; auto f = generate (empty, s); auto r = render (empty, f, 120, 48000, s);
        CHECK (f.events.empty() && r.total == r.fillLen && r.peak == 0, "no loops: empty fill of the right length, silent");
        Kit k; k[kSnare].audio.rate = 44100; k[kSnare].audio.L.assign (44100, 0.0f); analyse (k[kSnare], kSnare);
        CHECK (k[kSnare].hits.empty(), "a silent file has no hits");
        Kit k2; Synth sy; k2[kSnare].audio.rate = 44100; k2[kSnare].audio.L.assign (20000, 0.0f); sy.snare (k2[kSnare].audio.L, 44100, 0.0, 0.8f); analyse (k2[kSnare], kSnare);
        CHECK (k2[kSnare].hits.size() == 1, "a one-shot (hit at sample 0) is one hit");
        s.style = kRoll; auto f2 = generate (k2, s); auto r2 = render (k2, f2, 120, 48000, s);
        CHECK (! f2.events.empty() && r2.peak > 0.01f && r2.maxEdge < 1e-3f, "a fill from a single one-shot works");
        Kit k3 = makeKit (120, 0); auto r3 = render (k3, generate (k3, s), 30, 44100, s); auto r4 = render (k3, generate (k3, s), 300, 44100, s);
        CHECK (r3.peak <= 0.8913f && r4.peak <= 0.8913f && r3.maxEdge < 1e-3f && r4.maxEdge < 1e-3f, "30 and 300 BPM render cleanly");
    }
    // ---------------------------------------------------------------- 8. speed: analysis of three 2-bar loops
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) makeKit (120, 2);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 5;
        std::printf ("SPEED: synth + analysis of 3 loops %.1f ms\n", ms);
        CHECK (ms < 400, "loading three loops takes %.1f ms", ms);
    }
    std::printf (fails == 0 ? "ALL DSP TESTS PASSED\n" : "%d DSP TEST(S) FAILED\n", fails);
    return fails;
}
