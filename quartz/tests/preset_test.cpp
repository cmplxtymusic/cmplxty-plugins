// Renders every factory preset (Quartz presets with ALTER at defaults, ALTER presets with Quartz at defaults, and with the A3 Quartz preset)
// with a voice-like signal and silence; checks finite output, peak, level and tail decay; measures CPU.
#include "../Source/Engine.h"
#include "../Source/FactoryPresets.h"
#include <cstdio>
#include <chrono>
#include <cstdlib>
using namespace qz;
static float rmsOf (const float* x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[i] * x[i]; return (float) std::sqrt (s / std::max (1, b - a)); }
static void voice (std::vector<float>& in, double fs, float f0, float amp)
{
    SVF f1, f2, f3; f1.set (700, 8, (float) fs); f2.set (1200, 10, (float) fs); f3.set (2600, 12, (float) fs); float ph = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const float f = f0 * (1.0f + 0.01f * std::sin (6.28f * 5.0f * (float) i / (float) fs));
        ph += f / (float) fs; float p = 0; if (ph >= 1) { ph -= 1; p = 1; }
        in[i] = (f1.bandpass (p) + 0.7f * f2.bandpass (p) + 0.4f * f3.bandpass (p));
    }
    float mx = 0; for (float v : in) mx = std::max (mx, std::fabs (v)); for (auto& v : in) v *= amp / mx;
}
static void apply (ParamValues& pv, const FactoryPreset& p) { for (int i = 0; i < p.count; ++i) pv.v[p.e[i].index] = p.e[i].value; }
struct Res { float peak, rmsDb, tailDb; bool bad; double cpu; };
static Res run (double fs, const ParamValues& pv, const std::vector<float>& in)
{
    Engine e; e.prepare (fs); std::vector<float> L = in, R = in; const int N = (int) in.size(); Res r { 0, 0, 0, false, 0 };
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; i += 512) e.process (&L[i], &R[i], std::min (512, N - i), pv);
    r.cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / ((double) N / fs);
    for (int i = 0; i < N; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) r.bad = true; r.peak = std::max (r.peak, std::max (std::fabs (L[i]), std::fabs (R[i]))); }
    const int s = (int) (fs * 0.5), a = (int) (fs * 3.0);
    r.rmsDb = 20 * std::log10 (rmsOf (L.data(), s, a) / std::max (1e-9f, rmsOf (in.data(), 0, a - s)) + 1e-9f);   // relative to input rms (in is voice from 0)
    r.tailDb = 20 * std::log10 (rmsOf (L.data(), N - (int) (fs * 0.3), N) + 1e-9f);
    return r;
}
int main()
{
    int fails = 0; double worstCpu = 0;
    for (double fs : std::getenv ("LEVELS") ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 })
    {
        const int N = (int) (fs * 6);
        std::vector<float> in (N, 0.0f), sil (N, 0.0f); { std::vector<float> v ((int) (fs * 3)); voice (v, fs, 140, 0.4f); std::copy (v.begin(), v.end(), in.begin()); }
        float worstPeak = 0, minDb = 99, maxDb = -99, worstTail = -200; int count = 0;
        auto check = [&] (const char* what, const char* name, const ParamValues& pv)
        {
            Res r = run (fs, pv, in), z = run (fs, pv, sil); ++count;
            worstPeak = std::max (worstPeak, r.peak); minDb = std::min (minDb, r.rmsDb); maxDb = std::max (maxDb, r.rmsDb); worstTail = std::max (worstTail, r.tailDb);
            worstCpu = std::max (worstCpu, r.cpu);
            if (std::getenv ("LEVELS")) std::printf ("LV %s %s %+.1f %.2f\n", what, name, r.rmsDb, r.peak);
            const bool zerr = z.peak > 1e-4f;
            if (r.bad || z.bad || r.peak > 2.0f || zerr || r.rmsDb < -30 || r.rmsDb > 12 || r.tailDb > -35)
            { std::printf ("  FAIL [%s] %-22s peak %.2f level %+.1f dB tail %.0f dB silence-peak %.1e%s\n", what, name, r.peak, r.rmsDb, r.tailDb, z.peak, r.bad ? " NaN" : ""); ++fails; }
        };
        ParamValues a3; apply (a3, kQuartzPresets[1]);
        for (auto& p : kQuartzPresets) { ParamValues pv; apply (pv, p); check ("Q", p.name, pv); }
        for (auto& p : kAlterPresets) { ParamValues pv; apply (pv, p); check ("A", p.name, pv); }
        for (auto& p : kAlterPresets) { ParamValues pv = a3; apply (pv, p); check ("A+Q", p.name, pv); }
        std::printf ("fs %.0f: %d renders, worst peak %.2f, level range %+.1f..%+.1f dB, worst tail %.0f dB\n", fs, count, worstPeak, minDb, maxDb, worstTail);
    }
    std::printf ("worst CPU (render time / audio time, one core): %.3f\n", worstCpu);
    std::printf (fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
