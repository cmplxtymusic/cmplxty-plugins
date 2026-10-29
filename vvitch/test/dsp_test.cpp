// VVITCH DSP tests (plain C++, no JUCE): g++ -std=c++17 -O2 test/dsp_test.cpp
#include "../src/VvitchDSP.h"
#include <chrono>
#include <cstdio>
using namespace vv::dsp;

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
static bool finite (const Out& o, float ceil = 0.95f) { for (size_t i = 0; i < o.l.size(); ++i) if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i]) || std::fabs (o.l[i]) > ceil || std::fabs (o.r[i]) > ceil) return false; return true; }
static Params pure()   // one triangle, nothing else: a near-sine for pitch tests
{
    Params p; p.w1 = wTri; p.lvl2 = 0; p.uni = 1; p.cutoff = 1200; p.fType = FilterType::lp24; p.reso = 0; p.fKey = 0; p.fEnv = 0; p.drive = 0;
    p.aA = 0.001f; p.aS = 1; p.aR = 0.05f; p.revMix = 0; p.dlyMix = 0; p.volumeDb = 0; return p;
}

int main()
{
    {   Engine e; e.prepare (SR); e.setParams (pure()); e.noteOn (69, 1); auto o = run (e, 48000);
        const double f = freqOf (o.l, 4800, 48000); std::printf ("  A4 = %.3f Hz\n", f);
        check (std::fabs (f - 440.0) < 0.3, "note 69 plays 440 Hz");
        auto p = pure(); p.drag = true; p.dragAmt = 12; e.setParams (p); o = run (e, 48000);
        const double f2 = freqOf (o.l, 9600, 48000); std::printf ("  drag -12 = %.3f Hz\n", f2);
        check (std::fabs (f2 - 220.0) < 0.5, "DRAG 12 st plays an octave lower");
        p = pure(); p.oct1 = -1; e.setParams (p); o = run (e, 48000);
        check (std::fabs (freqOf (o.l, 9600, 48000) - 220.0) < 0.5, "OSC 1 octave -1");
    }
    {   Engine e; e.prepare (SR); auto p = pure(); p.drop = 12; p.dropTime = 0.2f; e.setParams (p); e.noteOn (69, 1); auto o = run (e, 96000);
        const double f = freqOf (o.l, 48000, 96000); std::printf ("  after the drop = %.3f Hz\n", f);
        check (std::fabs (f - 220.0) < 1.0, "pitch drop 12 st settles an octave down");
    }
    {   Engine e; e.prepare (SR); e.setParams (pure()); auto o = run (e, 4800); check (rms (o.l) == 0, "silent without notes");
        e.noteOn (60, 1); o = run (e, 9600); e.noteOff (60); o = run (e, 48000); check (rms (o.l, 24000, 48000) < 1e-6, "release ends in silence");
    }
    {   // stress: every wave, max unison, full reso, drive, crush, tape, delay, reverb, 12 voices at full velocity -> finite and under the ceiling
        Engine e; e.prepare (SR);
        bool ok = true;
        for (int w = 0; w < 4; ++w)
            for (int ft = 0; ft < 4; ++ft)
            {
                Params p; p.w1 = w; p.w2 = (w + 1) % 4; p.lvl1 = p.lvl2 = 1; p.uni = 7; p.detune = 1; p.width = 1; p.noise = 1; p.vox = 0.7f; p.voxMove = 1;
                p.fType = (FilterType) ft; p.cutoff = 600; p.reso = 1; p.drive = 1; p.fEnv = 1; p.bits = 3; p.crushHz = 3000; p.chipMix = 1;
                p.wow = 1; p.flutter = 1; p.age = 1; p.dlyMix = 1; p.dlyFb = 0.95f; p.revMix = 1; p.revSize = 1; p.volumeDb = 6; p.voices = 12; p.drop = -24;
                e.setParams (p); for (int n = 0; n < 12; ++n) e.noteOn (36 + n * 5, 1);
                auto o = run (e, 48000); ok = ok && finite (o); for (int n = 0; n < 12; ++n) e.noteOff (36 + n * 5);
                o = run (e, 9600); ok = ok && finite (o);
            }
        check (ok, "stress (all waves x filters, everything maxed): no NaN, output under -0.5 dBFS");
    }
    {   // arpeggiator: 3 held notes, 1/16 at 120 bpm -> a new note every 125 ms
        Engine e; e.prepare (SR); auto p = pure(); p.arp = true; p.arpDiv = 3; p.arpGate = 0.5f; p.aR = 0.005f; p.bpm = 120; e.setParams (p);
        e.noteOn (57, 1); e.noteOn (64, 1); e.noteOn (69, 1);
        auto o = run (e, 48000);
        int onsets = 0; bool was = false;
        for (size_t i = 0; i < o.l.size(); i += 240) { const bool loud = rms (o.l, i, i + 240) > 0.01; if (loud && ! was) ++onsets; was = loud; }
        std::printf ("  arp onsets in 1 s: %d\n", onsets);
        check (onsets >= 7 && onsets <= 9, "arp 1/16 @120: 8 notes a second");
        const double f = freqOf (o.l, 6000 + 600, 6000 + 2400);   // the 2nd step (Up): note 64
        check (std::fabs (f - 329.63) < 3, "arp Up: second step is the middle note");
        e.noteOff (57); e.noteOff (64); e.noteOff (69); o = run (e, 24000);
        check (rms (o.l, 12000, 24000) < 1e-5, "arp stops when keys are released");
    }
    {   // glide
        Engine e; e.prepare (SR); auto p = pure(); p.voices = 1; p.glide = 0.3f; e.setParams (p); e.noteOn (57, 1); run (e, 24000); e.noteOn (69, 1);
        auto o = run (e, 4800); const double f = freqOf (o.l, 0, 4800); check (f > 225 && f < 400, "glide: mid-way pitch between the notes");
    }
    {   // CPU
        Engine e; e.prepare (SR); Params p; p.uni = 7; p.vox = 0.5f; p.voices = 12; p.revMix = 0.4f; p.dlyMix = 0.3f; p.wow = 0.5f; p.chipMix = 0.5f; p.bits = 6; e.setParams (p);
        for (int n = 0; n < 12; ++n) e.noteOn (48 + n * 2, 0.8f);
        const auto t0 = std::chrono::steady_clock::now(); run (e, 48000 * 4);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  12 voices x 7 unison, vox, fx: %.1f ms per second of audio (%.1f%% of one core)\n", ms / 4, ms / 40);
        check (ms / 4 < 400, "worst case runs faster than real time with room to spare");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
