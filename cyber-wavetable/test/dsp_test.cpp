// Cyber Wavetable DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/CyberWavetableDSP.h"
#include <chrono>
#include <cstdio>
#include <random>
using namespace cw::dsp;

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what); if (! ok) ++fails; }
static const double SR = 48000;

struct Out { std::vector<float> l, r; };
static Out run (Engine& e, int n, int block = 256)
{
    Out o; o.l.resize ((size_t) n); o.r.resize ((size_t) n);
    for (int i = 0; i < n; i += block) { const int k = std::min (block, n - i); e.render (o.l.data() + i, o.r.data() + i, k); }
    return o;
}
static double rms (const std::vector<float>& x, size_t a = 0, size_t b = 0) { if (! b) b = x.size(); double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, b - a)); }
static double freqOf (const std::vector<float>& x, size_t a, size_t b)
{
    int n = 0; double first = -1, last = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n > 1 ? (n - 1) * SR / (last - first) : 0;
}
static Params sine()
{
    Params p; p.a = OscP { true, 0, 0.0f, 0, 0, 1, 0, 1.0f }; p.b.on = false; p.cutoff = 20000; p.reso = 0; p.fKey = 0; p.fType = FilterType::lp12;
    p.aA = 0.001f; p.aD = 0.1f; p.aS = 1; p.aR = 0.05f; p.volumeDb = 0; p.width = 0; return p;
}

int main()
{
    const auto t0 = std::chrono::steady_clock::now();
    (void) tables();
    std::printf ("tables built in %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());

    {   // tables: normalised, finite, frames differ, the top mip is a pure sine
        bool fin = true, norm = true, morph = true;
        for (int b = 0; b < kBanks; ++b)
        {
            for (int f = 0; f < kFrames; ++f)
            {
                float pk = 0; for (int i = 0; i <= kTab; ++i) { const float v = tables().at (b, f, 0)[i]; fin = fin && std::isfinite (v); pk = std::max (pk, std::fabs (v)); }
                norm = norm && std::fabs (pk - 1.0f) < 1e-3f;
            }
            double d = 0; for (int i = 0; i < kTab; ++i) d += std::fabs (tables().at (b, 0, 0)[i] - tables().at (b, kFrames - 1, 0)[i]);
            morph = morph && d / kTab > 0.05;
        }
        check (fin && norm, "6 banks x 8 frames: finite and normalised to peak 1");
        check (morph, "every bank changes shape across its morph range");
        double se = 0; const float* t = tables().at (0, kFrames - 1, kMips - 1);
        float pk = 0; for (int i = 0; i < kTab; ++i) pk = std::max (pk, std::fabs (t[i]));
        for (int i = 0; i < kTab; ++i) { const double ph = 2 * kPi * i / kTab; double best = 1e9;
            (void) best; se += 0; (void) ph; }
        // the top mip keeps one harmonic only: correlate with sin/cos
        double s = 0, c = 0, e = 0; for (int i = 0; i < kTab; ++i) { const double ph = 2 * kPi * i / kTab; s += t[i] * std::sin (ph); c += t[i] * std::cos (ph); e += (double) t[i] * t[i]; }
        const double h1 = (s * s + c * c) * 2.0 / kTab;
        check (e > 0 && std::fabs (h1 / e - 1.0) < 1e-6, "top mip level is a single sine (no harmonics left to alias)");
    }
    {   // mip selection keeps every harmonic below Nyquist
        bool ok = true;
        for (double f = 20; f < 20000; f *= 1.01)
            for (double sr : { 44100.0, 48000.0, 96000.0 })
            {
                const int m = mipFor (f / sr); const int maxH = (kTab / 2) >> m;
                if (m < kMips - 1 && maxH * f >= sr / 2) ok = false;
                if (m > 0 && ((kTab / 2) >> (m - 1)) * f < sr / 2 * 0.97) ok = false;   // and not more filtering than needed
            }
        check (ok, "mip level choice: band-limited to Nyquist at 44.1 / 48 / 96 kHz, never over-filtered");
    }
    {   // pitch
        Engine e; e.prepare (SR); e.setParams (sine());
        e.noteOn (69, 1); auto o = run (e, 48000);
        const double f = freqOf (o.l, 4800, 48000);
        std::printf ("  A4 = %.3f Hz\n", f);
        check (std::fabs (f - 440.0) < 0.2, "note 69 plays 440 Hz");
        auto p = sine(); p.a.semi = 12; p.a.fine = 50; e.setParams (p); o = run (e, 48000);
        const double f2 = freqOf (o.l, 4800, 48000), want = 880 * std::pow (2.0, 0.5 / 12);
        check (std::fabs (f2 - want) < 0.5, "semitone +12 and fine +50 ct");
        e.setParams (sine()); e.setPitchBend (1.0f); o = run (e, 48000);
        check (std::fabs (freqOf (o.l, 4800, 48000) - 440 * std::pow (2.0, 2.0 / 12)) < 0.5, "pitch bend +2 semitones");
        e.setPitchBend (0);
        e.noteOff (69); o = run (e, 24000);
        check (rms (o.l, 12000) < 1e-6 && e.activeVoices.load() == 0, "release ends in silence, voice freed");
        Engine s; s.prepare (SR); s.setParams (sine()); o = run (s, 4800);
        check (rms (o.l) == 0 && rms (o.r) == 0, "no notes = silence");
    }
    {   // polyphony and voice stealing
        Engine e; e.prepare (SR); auto p = sine(); p.voices = 4; e.setParams (p);
        for (int n = 60; n < 66; ++n) e.noteOn (n, 0.8f);
        auto o = run (e, 4800);
        check (e.activeVoices.load() == 4, "voices = 4 caps the polyphony");
        bool fin = true; for (float v : o.l) fin = fin && std::isfinite (v) && std::fabs (v) <= 2.0f;
        check (fin, "stealing stays bounded");
        p.voices = 2; e.setParams (p); o = run (e, 24000);
        check (e.activeVoices.load() <= 2, "lowering the voice count releases the extra voices");
    }
    {   // mono legato with note stack
        Engine e; e.prepare (SR); auto p = sine(); p.voices = 1; e.setParams (p);
        e.noteOn (57, 1); run (e, 9600); e.noteOn (69, 1); auto o = run (e, 24000);
        check (e.activeVoices.load() == 1 && std::fabs (freqOf (o.l, 2400, 24000) - 440) < 0.5, "mono: second note takes over (legato, one voice)");
        e.noteOff (69); o = run (e, 24000);
        check (std::fabs (freqOf (o.l, 2400, 24000) - 220) < 0.5, "mono: releasing it returns to the held note");
        e.noteOff (57); o = run (e, 24000);
        check (rms (o.l, 12000) < 1e-6, "mono: all keys up = silence");
        p.glide = 0.2f; e.setParams (p); e.noteOn (57, 1); run (e, 9600); e.noteOn (69, 1);
        o = run (e, 4800); const double fEarly = freqOf (o.l, 0, 2400); o = run (e, 48000); const double fLate = freqOf (o.l, 24000, 48000);
        check (fEarly > 225 && fEarly < 430 && std::fabs (fLate - 440) < 0.5, "glide slides between notes");
    }
    {   // sustain pedal
        Engine e; e.prepare (SR); e.setParams (sine());
        e.setSustain (true); e.noteOn (64, 1); run (e, 4800); e.noteOff (64); auto o = run (e, 24000);
        check (rms (o.l, 12000) > 0.1, "sustain pedal holds a released note");
        e.setSustain (false); o = run (e, 24000);
        check (rms (o.l, 12000) < 1e-6, "pedal up releases it");
    }
    {   // filter
        auto p = sine(); p.a.pos = 0.5f;   // saw
        auto level = [&] (Params q, int note) { Engine e; e.prepare (SR); e.setParams (q); e.noteOn (note, 1); auto o = run (e, 24000); return rms (o.l, 4800); };
        const double open = level (p, 57);
        p.cutoff = 150; p.fType = FilterType::lp24; const double shut = level (p, 57);
        p.fType = FilterType::lp12; const double shut12 = level (p, 57);
        std::printf ("  saw 220 Hz: open %.3f, LP12 150 Hz %.3f, LP24 150 Hz %.3f\n", open, shut12, shut);
        check (shut < open * 0.15 && shut < shut12, "low-pass removes the saw (LP24 steeper than LP12)");
        p = sine(); p.fType = FilterType::hp; p.cutoff = 3000; const double hp = level (p, 45);
        check (hp < 0.01, "high-pass removes a low sine");
        p = sine(); p.a.pos = 0.5f; p.cutoff = 60; p.fEnv = 1; p.mA = 0.001f; p.mD = 0.15f; p.mS = 0;
        Engine e; e.prepare (SR); e.setParams (p); e.noteOn (57, 1); auto o = run (e, 48000);
        check (rms (o.l, 0, 2400) > 2 * rms (o.l, 36000, 48000), "filter envelope opens then closes the filter");
        p = sine(); p.a.pos = 0.5f; p.reso = 1.0f; p.cutoff = 1200;
        Engine r; r.prepare (SR); r.setParams (p); r.noteOn (45, 1); o = run (r, 48000);
        bool fin = true; for (float v : o.l) fin = fin && std::isfinite (v) && std::fabs (v) <= 2.0f;
        check (fin, "full resonance stays stable");
    }
    {   // unison and stereo
        auto p = sine(); p.a.pos = 0.5f; p.a.uni = 7; p.a.detune = 0.6f; p.width = 1;
        Engine e; e.prepare (SR); e.setParams (p); e.noteOn (57, 1); auto o = run (e, 24000);
        double d = 0; for (size_t i = 4800; i < o.l.size(); ++i) d += std::fabs (o.l[i] - o.r[i]);
        check (d / 19200 > 0.01 && rms (o.l, 4800) > 0.05, "7-voice unison with width is stereo");
        p.width = 0; Engine m; m.prepare (SR); m.setParams (p); m.noteOn (57, 1); o = run (m, 24000);
        d = 0; for (size_t i = 0; i < o.l.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - o.r[i]));
        check (d < 1e-5, "width 0 = mono");
    }
    {   // sub + noise
        Params p = sine(); p.a.on = false; p.sub = 1;
        Engine e; e.prepare (SR); e.setParams (p); e.noteOn (69, 1); auto o = run (e, 24000);
        check (std::fabs (freqOf (o.l, 2400, 24000) - 220) < 0.5, "sub oscillator one octave down");
        p.sub = 0; p.noise = 1; Engine n; n.prepare (SR); n.setParams (p); n.noteOn (69, 1); o = run (n, 24000);
        check (rms (o.l, 2400) > 0.02, "noise generator");
    }
    {   // mod matrix
        Params p = sine(); p.a.pos = 0.5f; p.cutoff = 400; p.l1Rate = 4; p.l1Shape = lfoSquare;
        p.mod[0] = { srcLfo1, dCutoff, 0.8f };
        Engine e; e.prepare (SR); e.setParams (p); e.noteOn (57, 1); auto o = run (e, 48000);
        double lo = 1e9, hi = 0; for (int w = 0; w < 16; ++w) { const double r = rms (o.l, (size_t) (w * 3000), (size_t) (w * 3000 + 2000)); lo = std::min (lo, r); hi = std::max (hi, r); }
        check (hi > 2 * lo, "LFO 1 -> cutoff modulates the level over time");
        Params q = sine(); q.mod[0] = { srcVel, dAmp, 1.0f };
        auto lev = [&] (float vel) { Engine x; x.prepare (SR); x.setParams (q); x.noteOn (69, vel); return rms (run (x, 9600).l, 4800); };
        check (lev (1.0f) > 1.8 * lev (0.1f), "velocity -> amp");
        q = sine(); q.mod[1] = { srcWheel, dPitch, 0.5f };   // +12 st at full wheel
        Engine w; w.prepare (SR); w.setParams (q); w.setModWheel (1); w.noteOn (57, 1); o = run (w, 24000);
        check (std::fabs (freqOf (o.l, 4800, 24000) - 440) < 0.6, "mod wheel -> pitch (+12 st)");
        q = sine(); q.mod[2] = { srcLfo2, dAPos, 1.0f }; q.l2Rate = 2; q.a.bank = 2;
        Engine m; m.prepare (SR); m.setParams (q); m.noteOn (57, 1); run (m, 6000); const float p1 = m.dispPosA.load(); run (m, 6000); const float p2 = m.dispPosA.load();
        check (std::fabs (p1 - p2) > 0.05f, "LFO 2 -> wavetable position morphs");
    }
    {   // fuzz: random settings, notes, rates, block sizes
        std::mt19937 rng (5); std::uniform_real_distribution<float> u (0, 1);
        bool fin = true; float pk = 0;
        for (int it = 0; it < 120; ++it)
        {
            Params p;
            for (auto* o : { &p.a, &p.b }) { o->on = u (rng) > 0.2f; o->bank = (int) (u (rng) * 6); o->pos = u (rng); o->semi = std::round (u (rng) * 48 - 24); o->fine = u (rng) * 200 - 100; o->uni = 1 + (int) (u (rng) * 7); o->detune = u (rng); o->level = u (rng); }
            p.sub = u (rng); p.noise = u (rng); p.width = u (rng); p.fType = (FilterType) (int) (u (rng) * 4); p.cutoff = 20 * std::pow (1000.0f, u (rng)); p.reso = u (rng);
            p.drive = u (rng); p.fEnv = u (rng) * 2 - 1; p.fKey = u (rng); p.aA = u (rng) * 0.5f; p.aD = u (rng); p.aS = u (rng); p.aR = u (rng) * 0.5f;
            p.mA = u (rng); p.mD = u (rng); p.mS = u (rng); p.mR = u (rng); p.l1Shape = (int) (u (rng) * 5); p.l2Shape = (int) (u (rng) * 5); p.l1Rate = 30 * u (rng); p.l2Rate = 30 * u (rng);
            p.voices = 1 + (int) (u (rng) * 16); p.glide = u (rng) < 0.5f ? 0 : u (rng); p.volumeDb = 6;
            for (auto& s : p.mod) s = { (int) (u (rng) * kNumSrc), (int) (u (rng) * kNumDst), u (rng) * 2 - 1 };
            const double sr = it % 3 == 0 ? 44100 : it % 3 == 1 ? 48000 : 96000;
            Engine e; e.prepare (sr); e.setParams (p); e.setModWheel (u (rng)); e.setPressure (u (rng)); e.setPitchBend (u (rng) * 2 - 1);
            std::vector<float> l (4096), r (4096);
            for (int b = 0; b < 12; ++b)
            {
                if (u (rng) < 0.6f) e.noteOn ((int) (u (rng) * 128), u (rng));
                if (u (rng) < 0.4f) e.noteOff ((int) (u (rng) * 128));
                if (u (rng) < 0.1f) e.setSustain (u (rng) < 0.5f);
                const int n = 1 + (int) (u (rng) * 4095);
                e.render (l.data(), r.data(), n);
                for (int i = 0; i < n; ++i) { fin = fin && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]); pk = std::max (pk, std::max (std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]))); }
            }
            e.allSoundOff(); e.render (l.data(), r.data(), 64);
            fin = fin && l[10] == 0 && e.activeVoices.load() == 0;
        }
        std::printf ("  fuzz peak %.2f\n", pk);
        check (fin && pk <= 2.0f, "120 random patches x notes x rates x block sizes: finite, bounded, all-sound-off silences");
    }
    {   // speed: 16 voices x 2 oscillators x 7 unison
        Params p; p.a.uni = 7; p.b.on = true; p.b.uni = 7; p.voices = 16; p.aS = 1;
        Engine e; e.prepare (48000); e.setParams (p); for (int n = 0; n < 16; ++n) e.noteOn (40 + n * 2, 0.8f);
        std::vector<float> l (512), r (512);
        const auto a = std::chrono::steady_clock::now();
        for (int b = 0; b < 940; ++b) e.render (l.data(), r.data(), 512);   // ~10 s
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        std::printf ("  worst case (16 voices x 14 oscillators): %.1fx realtime\n", 10.0 / s);
        check (10.0 / s > 3.0, "worst case runs well faster than realtime");
    }
    std::printf (fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
