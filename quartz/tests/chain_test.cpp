// Offline tests of the full engine: finite output, level sanity, vocoder calibration, section toggles, latency.
#include "../Source/Engine.h"
#include <cstdio>
#include <random>
using namespace qz;
static float rmsOf (const float* x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[i] * x[i]; return (float) std::sqrt (s / (b - a)); }
static void voice (std::vector<float>& in, double fs, float f0, float amp, bool vib = true)
{
    SVF f1, f2, f3; f1.set (700, 8, (float) fs); f2.set (1200, 10, (float) fs); f3.set (2600, 12, (float) fs); float ph = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const float f = f0 * (vib ? 1.0f + 0.01f * std::sin (6.28f * 5.0f * (float) i / (float) fs) : 1.0f);
        ph += f / (float) fs; float p = 0; if (ph >= 1) { ph -= 1; p = 1; }
        in[i] = (f1.bandpass (p) + 0.7f * f2.bandpass (p) + 0.4f * f3.bandpass (p));
    }
    float mx = 0; for (float v : in) mx = std::max (mx, std::fabs (v)); for (auto& v : in) v *= amp / mx;
}
int main (int argc, char** argv)
{
    int fails = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int N = (int) (fs * 4);
        std::vector<float> in (N); voice (in, fs, 150.0f, 0.5f);
        Engine e; e.prepare (fs);
        std::printf ("== fs %.0f latency %d (alter %d + quartz %d)\n", fs, e.latency(), e.alterLatency(), e.latency() - e.alterLatency());
        // 1. all defaults with both sections on, sweeps: random automation of every knob each 100 ms
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> U (0, 1);
        ParamValues pv; std::vector<float> L = in, R = in;
        float peak = 0; bool bad = false; double maxStep = 0; float prev = 0;
        const int blk = 480;
        for (int i = 0; i < N; i += blk)
        {
            if ((i / blk) % 20 == 0)
                for (int p = 0; p < kNumParams; ++p)
                {
                    const auto& s = kParams[p];
                    float v = s.mn + U (rng) * (s.mx - s.mn);
                    if (s.kind == Kind::Choice) v = std::floor (v + 0.5f); else if (s.kind == Kind::Bool) v = U (rng) > (p >= bIn ? 0.85f : 0.2f) ? 1.0f : 0.0f; else if (s.step > 0) v = std::floor (v + 0.5f);
                    if (p == pBypass) v = 0;
                    pv.v[p] = v;
                }
            const int c = std::min (blk, N - i);
            e.process (&L[i], &R[i], c, pv);
            for (int k = 0; k < c; ++k) { const float a = L[i + k], b = R[i + k]; if (! std::isfinite (a) || ! std::isfinite (b)) bad = true; peak = std::max (peak, std::max (std::fabs (a), std::fabs (b))); }
        }
        std::printf ("random automation: finite=%s peak=%.2f\n", bad ? "NO" : "yes", peak); if (bad || peak > 4.01f) ++fails;
        // 2. silence in -> silence out (no noise floor / self oscillation) for defaults
        {
            ParamValues d; d.v[pAon] = 1; Engine e2; e2.prepare (fs); std::vector<float> z (N, 0.0f), z2 (N, 0.0f);
            for (int i = 0; i < N; i += blk) e2.process (&z[i], &z2[i], std::min (blk, N - i), d);
            const float r = rmsOf (z.data(), 0, N); std::printf ("silence in -> rms %.2e\n", r); if (r > 1e-4f) ++fails;
        }
        // 3. neutral settings: Quartz default (all knobs neutral), ALTER off => output == input delayed (latency alignment)
        {
            ParamValues d; d.v[pAon] = 0; d.v[pQon] = 1; Engine e3; e3.prepare (fs); std::vector<float> a = in, b = in;
            for (int i = 0; i < N; i += blk) e3.process (&a[i], &b[i], std::min (blk, N - i), d);
            const int D = e3.latency(); double err = 0, sig = 0;
            for (int i = (int) fs; i < N; ++i) { const double dd = a[i] - in[i - D]; err += dd * dd; sig += (double) in[i - D] * in[i - D]; }
            const float lv = 20 * std::log10 (rmsOf (a.data(), (int) fs, N) / rmsOf (in.data(), (int) fs, N));
            std::printf ("neutral Quartz vs delayed input: waveform err %.1f dB (IIR cut/tone phase shifts), level change %+.2f dB\n", 10 * std::log10 (err / sig + 1e-12), lv);
            if (std::fabs (lv) > 1.0f) ++fails;
        }
        // 4. both sections off: exact delayed passthrough
        {
            ParamValues d; d.v[pAon] = 0; d.v[pQon] = 0; Engine e4; e4.prepare (fs); std::vector<float> a = in, b = in;
            for (int i = 0; i < N; i += blk) e4.process (&a[i], &b[i], std::min (blk, N - i), d);
            const int D = e4.latency(); double err = 0;
            for (int i = (int) fs; i < N; ++i) err = std::max (err, (double) std::fabs (a[i] - in[i - D]));
            std::printf ("both off: max error vs delayed input %.2e\n", err); if (err > 1e-5) ++fails;
        }
        // 5. vocoder level calibration
        {
            ParamValues d; d.v[pAon] = 0; d.v[pVocode] = 100; d.v[pIntensity] = 100; Engine e5; e5.prepare (fs); std::vector<float> a = in, b = in;
            for (int i = 0; i < N; i += blk) e5.process (&a[i], &b[i], std::min (blk, N - i), d);
            std::printf ("vocoder 100%%: in rms %.3f out rms %.3f (ratio %.2f)\n", rmsOf (in.data(), (int) fs, N), rmsOf (a.data(), (int) fs, N), rmsOf (a.data(), (int) fs, N) / rmsOf (in.data(), (int) fs, N));
        }
        // 6. heavy settings level check
        {
            ParamValues d; d.v[pAon] = 1; d.v[pDrive] = 36; d.v[pGrit] = 100; d.v[pBits] = 3; d.v[pRate] = 3000; d.v[pReverb] = 100; d.v[pDelay] = 300; d.v[pFb] = 95; d.v[pDouble] = 60; d.v[pWiden] = 100; d.v[pVocode] = 100; d.v[pIntensity] = 100; d.v[pMode] = 2;
            Engine e6; e6.prepare (fs); std::vector<float> a = in, b = in; float pk = 0;
            for (int i = 0; i < N; i += blk) e6.process (&a[i], &b[i], std::min (blk, N - i), d);
            for (int i = 0; i < N; ++i) pk = std::max (pk, std::fabs (a[i]));
            std::printf ("max-everything: peak %.2f rms %.2f\n", pk, rmsOf (a.data(), (int) fs, N)); if (pk > 4.01f) ++fails;
        }
        // 7. click test on section toggle: toggling Quartz/Alter each 100ms while a sine plays; max sample-to-sample delta relative to a plain sine
        {
            std::vector<float> s (N); for (int i = 0; i < N; ++i) s[i] = 0.3f * std::sin (6.28318f * 220.0f * (float) i / (float) fs);
            ParamValues d; d.v[pDrive] = 12; d.v[pReverb] = 30; d.v[pPitch] = 3; Engine e7; e7.prepare (fs); std::vector<float> a = s, b = s; double md = 0; float pr = 0;
            for (int i = 0; i < N; i += blk)
            {
                if ((i / blk) % 10 == 0) { d.v[pAon] = ((i / blk) / 10) % 2; d.v[pQon] = ((i / blk) / 10) % 3 != 0 ? 1.0f : 0.0f; }
                e7.process (&a[i], &b[i], std::min (blk, N - i), d);
                for (int k = 0; k < std::min (blk, N - i); ++k) { if (i + k > (int) fs) md = std::max (md, (double) std::fabs (a[i + k] - pr)); pr = a[i + k]; }
            }
            std::printf ("toggle test max |dx| %.3f (sine 220Hz step %.3f)\n", md, 0.3 * 6.28318 * 220 / fs * 6); if (md > 0.35) ++fails;
        }
    }
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
