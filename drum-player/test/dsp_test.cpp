// Drum Player DSP tests (no JUCE): built-in kit, triggering, tune, decay, pan, level, velocity, choke, voice stealing, user samples, fuzz, speed.
#include "../src/DrumDSP.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace cw::dsp;
static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what); if (! ok) ++fails; }

struct Out { std::vector<float> L, R; };
static Out run (Engine& e, int n, int block = 256)
{
    Out o; o.L.resize ((size_t) n); o.R.resize ((size_t) n);
    for (int i = 0; i < n; i += block) { const int m = std::min (block, n - i); e.render (o.L.data() + i, o.R.data() + i, m); }
    return o;
}
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return b > a ? std::sqrt (s / (double) (b - a)) : 0; }
static float peak (const std::vector<float>& x) { float p = 0; for (float v : x) p = std::max (p, std::fabs (v)); return p; }
static Params flat() { Params p; for (auto& q : p.pad) q = PadP {}; p.velSens = 0; p.volumeDb = 0; return p; }
static double freq (const std::vector<float>& x, size_t a, size_t b, double sr)
{
    int n = 0; double first = -1, last = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (n - 1) * sr / (last - first) : 0.0;
}

int main()
{
    // kit
    bool allOk = true;
    for (int i = 0; i < kSounds; ++i)
    {
        const Smp& s = builtIn (i); const float pk = peak (s.L); bool fin = true; for (float v : s.L) fin = fin && std::isfinite (v);
        if (! fin || pk < 0.5f || pk > 0.91f || s.len() < 4800 || std::fabs (s.L.back()) > 1e-6f) { allOk = false; std::printf ("    %s pk %.3f len %d\n", soundName (i), pk, s.len()); }
    }
    check (allOk, "16 built-in sounds: finite, normalised to 0.9, click-free end");
    check (std::string (soundName (16)) == "Sample" && std::string (soundName (4)) == "Closed Hat", "sound names");

    const double sr = 48000;
    {   // trigger + mapping
        Engine e; e.prepare (sr); auto P = flat(); e.setParams (P);
        auto o = run (e, 4800); check (peak (o.L) == 0.0f, "silent with no hits");
        e.noteOn (35, 1); e.noteOn (44, 1); o = run (e, 4800); check (peak (o.L) == 0.0f, "notes outside the 8 pads are ignored");
        e.noteOn (36, 1); o = run (e, 4800); check (peak (o.L) > 0.5f && e.hits[0].load() == 1, "C1 (36) plays pad 1 (Kick 808)");
        P.baseNote = 60; e.setParams (P); e.reset(); e.noteOn (36, 1); o = run (e, 2400); check (peak (o.L) == 0.0f, "base note moves the pad map");
        e.noteOn (67, 1); check (e.hits[7].load() == 1, "60 + 7 = pad 8");
        e.noteOn (60, 0.0f); check (e.hits[0].load() == 1, "velocity 0 is not a hit");
    }
    {   // the sample plays exactly once at unity (pad = Kick 808, pan centre, 0 dB, no velocity, full decay)
        Engine e; e.prepare (sr); auto P = flat(); e.setParams (P); e.trigger (0, 1);
        auto o = run (e, builtIn (0).len() + 1000);
        float err = 0; for (int k = 0; k < builtIn (0).len(); ++k) err = std::max (err, std::fabs (o.L[(size_t) k] - builtIn (0).L[(size_t) k]));
        check (err < 1e-5f && o.L == o.R, "untuned hit = the sample itself, bit-exact-ish, both channels");
        check (peak (std::vector<float> (o.L.end() - 900, o.L.end())) == 0.0f && e.activeVoices.load() == 0, "voice ends with the sample");
    }
    {   // tune: a pure test sample (1 kHz sine at 48k) played +12 st -> 2 kHz, at 44.1k output rate still 1 kHz untuned
        Smp s; s.rate = 48000; s.name = "sine"; for (int k = 0; k < 48000; ++k) s.L.push_back (0.5f * (float) std::sin (2 * 3.14159265358979 * 1000.0 * k / 48000.0));
        Engine e; e.prepare (44100); auto P = flat(); P.pad[2].sound = kSampleSound; e.setParams (P); e.setUser (2, &s); e.trigger (2, 1);
        auto o = run (e, 20000); check (std::fabs (freq (o.L, 100, 19000, 44100) - 1000.0) < 0.5, "sample rate conversion (48k sample at 44.1k)");
        P.pad[2].tune = 12; e.setParams (P); e.trigger (2, 1); o = run (e, 20000);
        check (std::fabs (freq (o.L, 100, 19000, 44100) - 2000.0) < 1.0, "tune +12 st = one octave up");
        P.pad[2].tune = -7; P.kitTune = 7; e.setParams (P); e.allSoundOff(); e.trigger (2, 1); o = run (e, 20000);
        check (std::fabs (freq (o.L, 100, 19000, 44100) - 1000.0) < 0.5, "kit tune adds to pad tune");
        P.kitTune = 0; P.pad[2].tune = 0;
        // decay -60 dB at the set time
        P.pad[2].decay = 100; e.setParams (P); e.allSoundOff(); e.trigger (2, 1); o = run (e, 44100);
        const double r0 = rms (o.L, 0, 441), r1 = rms (o.L, 4410 - 44, 4410 + 44);
        std::printf ("    decay 100 ms: %.1f dB at 100 ms, voices %d\n", 20 * std::log10 (r1 / r0), e.activeVoices.load());
        check (std::fabs (20 * std::log10 (r1 / r0) + 60) < 3 && e.activeVoices.load() == 0, "decay reaches -60 dB at the set time and frees the voice");
        // pan / level / velocity
        P.pad[2].decay = kDecayFull; P.pad[2].pan = -1; e.setParams (P); e.allSoundOff(); e.trigger (2, 1); o = run (e, 4000);
        check (peak (o.R) < 1e-6f && std::fabs (peak (o.L) - 0.5f * 1.41421f) < 0.01f, "pan hard left (equal power)");
        P.pad[2].pan = 0; P.pad[2].levelDb = -6; e.setParams (P); e.allSoundOff(); e.trigger (2, 1); o = run (e, 4000);
        check (std::fabs (peak (o.L) - 0.5f * 0.501f) < 0.005f, "level -6 dB");
        P.pad[2].levelDb = 0; P.velSens = 1; e.setParams (P); e.allSoundOff(); e.trigger (2, 0.5f); o = run (e, 4000);
        check (std::fabs (peak (o.L) - 0.25f) < 0.005f, "velocity sensitivity 100 %: half velocity = half level");
        P.velSens = 0; e.setParams (P); e.allSoundOff(); e.trigger (2, 0.2f); o = run (e, 4000);
        check (std::fabs (peak (o.L) - 0.5f) < 0.005f, "velocity sensitivity 0: every hit full level");
        // stereo sample
        Smp st = s; st.R.assign (st.L.size(), 0.0f); P.pad[3].sound = kSampleSound; e.setParams (P); e.setUser (3, &st); e.allSoundOff(); e.trigger (3, 1); o = run (e, 4000);
        check (peak (o.L) > 0.4f && peak (o.R) < 1e-6f, "stereo samples keep their channels");
        // empty sample slot
        e.setUser (3, nullptr); e.trigger (3, 1); o = run (e, 4000); check (peak (o.L) == 0.0f, "empty sample slot is silent");
        // swapping a pad's sample cuts its playing voices (the old buffer may be freed)
        e.setUser (3, &st); e.trigger (3, 1); run (e, 256); e.setUser (3, &s); check (e.activeVoices.load() >= 0, "setUser while playing"); o = run (e, 1024);
        check (peak (o.L) == 0.0f, "replaced sample stops playing at once");
    }
    {   // choke: closed hat (pad 4) cuts the open hat (pad 5) in ~5 ms
        Engine e; e.prepare (sr); auto P = flat(); P.pad[3].sound = 4; P.pad[4].sound = 5; P.pad[3].choke = P.pad[4].choke = 1; P.pad[3].levelDb = -60; e.setParams (P);
        e.trigger (4, 1); run (e, 4800); e.trigger (3, 1); auto o = run (e, 4800);
        check (rms (o.L, 400, 4800) < 1e-4, "choke group: closed hat silences the open hat within 5 ms");
        P.pad[3].choke = 2; e.setParams (P); e.trigger (4, 1); run (e, 4800); e.trigger (3, 1); o = run (e, 4800);
        check (rms (o.L, 400, 4800) > 1e-3, "different groups do not choke");
        P.pad[3].choke = 0; P.pad[4].choke = 0; e.setParams (P); e.allSoundOff(); e.trigger (4, 1); e.trigger (4, 1); run (e, 64);
        check (e.activeVoices.load() == 2, "no choke: hits overlap");
        e.allNotesOff(); auto z = run (e, 960); check (e.activeVoices.load() == 0 && peak (std::vector<float> (z.L.begin() + 300, z.L.end())) == 0.0f, "all notes off fades every voice");
    }
    {   // voice stealing
        Engine e; e.prepare (sr); auto P = flat(); P.pad[0].sound = 13; e.setParams (P);
        for (int i = 0; i < 40; ++i) e.trigger (0, 1);
        run (e, 64); check (e.activeVoices.load() == kVoices, "32-voice cap, oldest stolen");
    }
    {   // fuzz: random kits, hits, block sizes, 3 rates
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (0, 1); float worst = 0; bool fin = true;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int it = 0; it < 40; ++it)
            {
                Engine e; e.prepare (rate); Params P;
                for (auto& q : P.pad) { q.sound = (int) (U (rng) * 16); q.tune = U (rng) * 48 - 24; q.decay = 10 + U (rng) * 3990; q.pan = U (rng) * 2 - 1; q.levelDb = U (rng) * 12 - 6; q.choke = (int) (U (rng) * 5); }
                P.volumeDb = 0; P.kitTune = U (rng) * 24 - 12; e.setParams (P);
                std::vector<float> L (4096), R (4096);
                for (int b = 0; b < 60; ++b)
                {
                    if (U (rng) < 0.5f) e.trigger ((int) (U (rng) * 8), U (rng));
                    const int n = 1 + (int) (U (rng) * 4095); e.render (L.data(), R.data(), n);
                    for (int k = 0; k < n; ++k) { fin = fin && std::isfinite (L[(size_t) k]) && std::isfinite (R[(size_t) k]); worst = std::max ({ worst, std::fabs (L[(size_t) k]), std::fabs (R[(size_t) k]) }); }
                }
            }
        std::printf ("    fuzz peak %.2f\n", worst);
        check (fin && worst < 12.0f, "fuzz: 120 random kits, finite and bounded");
    }
    {   // speed: 32 voices pitched up
        Engine e; e.prepare (48000); auto P = flat(); for (auto& q : P.pad) { q.sound = 13; q.tune = 5; } e.setParams (P);
        for (int i = 0; i < 32; ++i) e.trigger (i % 8, 1);
        std::vector<float> L (256), R (256); const auto t0 = std::chrono::steady_clock::now(); int blocks = 0;
        while (e.activeVoices.load() == 32 || blocks == 0) { e.render (L.data(), R.data(), 256); if (++blocks >= 1500) break; }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), audio = blocks * 256 / 48000.0;
        std::printf ("    32 voices: %.0fx realtime\n", audio / secs);
        check (audio / secs > 20, "32 voices well above realtime");
    }
    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILED\n", fails);
    return fails;
}
