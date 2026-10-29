#include "../Source/Engine.h"
#include <cstdio>
#include <random>
using namespace qz;
static float rmsOf (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += x[i] * x[i]; return (float) std::sqrt (s / (double) (b - a)); }
int main()
{
    const double fs = 48000; const int N = (int) (fs * 3);
    std::vector<float> L (N), R (N);
    // glottal-ish voiced signal: 180 Hz pulse train through formant filters
    std::vector<float> in (N);
    { SVF f1, f2; f1.set (700, 8, (float) fs); f2.set (1200, 10, (float) fs); float ph = 0;
      for (int i = 0; i < N; ++i) { ph += 180.0f / (float) fs; float p = 0; if (ph >= 1) { ph -= 1; p = 1; } in[i] = 0.5f * (f1.bandpass (p) + f2.bandpass (p)) * 0.6f; }
      float mx = 0; for (float v : in) mx = std::max (mx, std::fabs (v)); for (auto& v : in) v *= 0.5f / mx; }
    Engine e; e.prepare (fs);
    std::printf ("latency %d (alter %d)\n", e.latency(), e.alterLatency());
    ParamValues pv;
    pv.v[pAon] = 1; pv.v[pQon] = 0; pv.v[pPitch] = 0;
    L = in; R = in;
    for (int i = 0; i < N; i += 512) e.process (&L[i], &R[i], std::min (512, N - i), pv);
    // identity check at ratio 1
    int D = e.latency();
    double err = 0, sig = 0; for (int i = 8000; i < N - 100; ++i) { double d = L[i] - in[i - D]; err += d * d; sig += in[i - D] * (double) in[i - D]; }
    std::printf ("alter ratio1 error vs delayed input: %.2f dB\n", 10 * std::log10 (err / sig + 1e-12));
    auto f0of = [&] (const std::vector<float>& x) { AlterEngine a; a.prepare (fs); float acc = 0; int cnt = 0; for (int i = 0; i < N; ++i) { a.process (x[i]); if (i > 48000 && i % 480 == 0) { float f = a.lastF0(); if (f > 0) { acc += f; ++cnt; } } } return cnt ? acc / cnt : 0.0f; };
    std::printf ("input f0 %.1f rms %.3f\n", f0of (in), rmsOf (in, 48000, N));
    const float tests[][4] = { {0,0,0,0}, {12,0,0,0}, {-12,0,0,0}, {7,0,0,0}, {0,5,0,0}, {0,-5,0,0}, {0,0,100,0}, {0,0,-100,0} };
    for (auto& t : tests)
    {
        pv.v[pPitch] = t[0]; pv.v[pFormant] = t[1]; pv.v[pGender] = t[2];
        e.reset(); L = in; R = in;
        for (int i = 0; i < N; i += 512) e.process (&L[i], &R[i], std::min (512, N - i), pv);
        std::printf ("pitch %+5.1f form %+4.1f gender %+4.0f -> f0 %.1f rms %.3f\n", t[0], t[1], t[2], f0of (L), rmsOf (L, 48000, N));
    }
    return 0;
}
