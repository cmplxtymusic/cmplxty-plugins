// Offline headless tests for the Growl DSP core (no JUCE, no audio device). Prints real measurements.
#include "../src/GrowlDSP.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// ---- allocation counter (to verify the audio path never allocates) ----
static bool g_countAllocs = false;
static long g_allocs = 0;
void* operator new (std::size_t n) { if (g_countAllocs) ++g_allocs; void* p = std::malloc (n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using Buf = std::vector<float>;
static const double PI = 3.14159265358979323846;

static Buf sine (int n, double sr, double f, double a, double ph = 0) { Buf b ((size_t) n); for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (a * std::sin (2 * PI * f * i / sr + ph)); return b; }
static Buf noise (int n, float a, unsigned seed) { std::mt19937 g (seed); std::uniform_real_distribution<float> d (-a, a); Buf b ((size_t) n); for (auto& x : b) x = d (g); return b; }
// naive saw with fundamental f (only used as a harmonically rich growl source)
static Buf saw (int n, double sr, double f, double a) { Buf b ((size_t) n); double ph = 0; for (int i = 0; i < n; ++i) { b[(size_t) i] = (float) (a * (2 * ph - 1)); ph += f / sr; ph -= std::floor (ph); } return b; }

static double rms (const Buf& b, size_t from, size_t to) { double s = 0; for (size_t i = from; i < to; ++i) s += (double) b[i] * b[i]; return std::sqrt (s / (double) (to - from)); }
static double db (double x) { return 20 * std::log10 (std::fmax (x, 1e-20)); }
static bool finiteAll (const std::vector<Buf>& v, float& peak) { peak = 0; for (auto& b : v) for (float x : b) { if (! std::isfinite (x)) return false; peak = std::fmax (peak, std::fabs (x)); } return true; }

// run the engine over channel buffers with a repeating block pattern. Params must already be set.
static std::vector<Buf> run (gw::Engine& e, const std::vector<Buf>& in, const std::vector<int>& blocks, const gw::Transport& tp0, bool hasPpq, double sr)
{
    std::vector<Buf> out = in; const int n = (int) in[0].size(); const int nc = (int) in.size();
    int pos = 0, bi = 0;
    while (pos < n)
    {
        const int len = std::min (blocks[(size_t) (bi++ % (int) blocks.size())], n - pos);
        std::vector<float*> ptrs; for (int c = 0; c < nc; ++c) ptrs.push_back (out[(size_t) c].data() + pos);
        gw::Transport tp = tp0; tp.hasPpq = hasPpq; tp.ppq = tp0.ppq + pos * tp0.bpm / 60.0 / sr;
        e.process (ptrs.data(), nc, len, tp);
        pos += len;
    }
    return out;
}
static Buf runMono (const gw::Params& p, const Buf& in, double sr, gw::Transport tp = {})
{
    gw::Engine e; e.prepare (sr, 4096, 1); e.setParams (p); e.snapSmoothers();
    Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, (int) o.size(), tp); return o;
}
static std::vector<Buf> runStereo (const gw::Params& p, const std::vector<Buf>& in, double sr, int block = 512, gw::Transport tp = {})
{
    gw::Engine e; e.prepare (sr, 4096, 2); e.setParams (p); e.snapSmoothers(); return run (e, in, { block }, tp, false, sr);
}

// bare-bones complex FFT (power of two)
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}
// power spectrum (|X|^2 / N^2*4 -> amplitude^2 per bin for a Hann window not applied: use coherent signals)
static std::vector<double> spectrum (const float* x, size_t n, bool hann)
{
    std::vector<std::complex<double>> a (n); double wsum = 0;
    for (size_t i = 0; i < n; ++i) { const double w = hann ? 0.5 - 0.5 * std::cos (2 * PI * (double) i / (double) n) : 1.0; wsum += w; a[i] = (double) x[i] * w; }
    fft (a); std::vector<double> p (n / 2);
    for (size_t k = 0; k < n / 2; ++k) { const double m = 2.0 * std::abs (a[k]) / wsum; p[k] = m * m; }
    return p;
}
static double gainAt (const gw::Params& p, double sr, double f, double a = 0.1, int delayComp = gw::kLatency)
{
    const int n = (int) sr; Buf in = sine (n, sr, f, a); Buf o = runMono (p, in, sr);
    (void) delayComp; return rms (o, (size_t) n / 2, (size_t) n) / rms (in, (size_t) n / 2, (size_t) n);
}

static gw::Params bypassAll()    // everything off: only the crossover (allpass) + delay remain
{
    gw::Params p; p.driveOn = false; p.formantOn = false; p.postOn = false; p.mix = 1; p.outGainDb = 0; p.subLevelDb = 0; p.subMono = false; p.inGainDb = 0; p.lfoDepth = 0; return p;
}

int main()
{
    std::printf ("Growl DSP tests (latency %d samples)\n", gw::kLatency);
    const double rates[] = { 44100, 48000, 96000 };

    // ---------------------------------------------------------------- 1. oversampler
    std::printf ("\n[1] half-band oversampler\n");
    {
        for (int mode = 0; mode < 3; ++mode)
        {
            gw::detail::Oversampler os; auto id = [] (float x) { return x; };
            const int L = gw::detail::Oversampler::latency (mode);
            std::vector<float> imp (400, 0.f); imp[100] = 1.f; int pk = 0; float pv = 0;
            for (int i = 0; i < 400; ++i) { const float y = os.process (imp[(size_t) i], mode, id); if (std::fabs (y) > std::fabs (pv)) { pv = y; pk = i; } }
            std::printf ("  mode %d: impulse peak at +%d samples (expected %d), peak value %.5f\n", mode, pk - 100, L, pv);
            CHECK (pk - 100 == L, "oversampler latency %d != %d", pk - 100, L);
            CHECK (std::fabs (pv - 1.0f) < 0.05f, "impulse peak value %f", pv);
            // pass band / stop band through up -> down with identity
            for (double f : { 0.05, 0.2, 0.35, 0.40 })
            {
                os.reset(); const int n = 8192; double a = 0, b = 0;
                for (int i = 0; i < n; ++i) { const float x = (float) std::sin (2 * PI * f * i); const float y = os.process (x, mode, id); if (i > 2000) { a += (double) x * x; b += (double) y * y; } }
                const double g = db (std::sqrt (b / a));
                std::printf ("    f=%.2f*fs : gain %+.4f dB\n", f, g);
                CHECK (std::fabs (g) < 0.1, "pass band gain %.3f dB at %.2f fs mode %d", g, f, mode);
            }
        }
        // alias rejection of the down-sampler: a tone at 0.7*fs in the 2x domain must vanish
        gw::detail::Down2<gw::kM1> d; double a = 0, b = 0; const double fh = 0.7 * 0.5;   // 0.7 * (fs/2) ... in 2x domain cycles/sample
        for (int i = 0; i < 8192; ++i) { const float x0 = (float) std::sin (2 * PI * (0.35 + 0.125) * (2 * i)), x1 = (float) std::sin (2 * PI * (0.35 + 0.125) * (2 * i + 1)); const float y = d.process (x0, x1); if (i > 200) { a += 0.5; b += (double) y * y; } }
        (void) fh; std::printf ("  down-sampler stop band (tone at 0.475 fs_hi): %.1f dB\n", db (std::sqrt (b / a)));
        CHECK (db (std::sqrt (b / a)) < -60, "stop band rejection");
    }

    // ---------------------------------------------------------------- 2. mix 0 null and latency
    std::printf ("\n[2] mix = 0 is a bit-exact delayed bypass; latency = %d\n", gw::kLatency);
    for (double sr : rates) for (int os = 0; os < 3; ++os)
    {
        gw::Params p; p.mix = 0; p.drive = 1; p.oversampling = os; p.lfoDepth = 1; p.satType = gw::SatFold; p.formantOn = p.postOn = true;
        const Buf in = noise (20000, 0.8f, 5);
        std::vector<Buf> o = runStereo (p, { in, in }, sr, 333);
        double md = 0; for (size_t i = (size_t) gw::kLatency; i < in.size(); ++i) md = std::fmax (md, std::fabs ((double) o[0][i] - in[i - (size_t) gw::kLatency]));
        double lead = 0; for (int i = 0; i < gw::kLatency; ++i) lead = std::fmax (lead, std::fabs (o[0][(size_t) i]));
        std::printf ("  sr=%.0f os=%d : max |out - in(n-%d)| = %.1e   leading zeros max %.1e\n", sr, os, gw::kLatency, md, lead);
        CHECK (md == 0.0 && lead == 0.0, "mix=0 must equal the delayed input exactly (sr %.0f os %d)", sr, os);
    }

    // ---------------------------------------------------------------- 3. everything off: allpass crossover
    std::printf ("\n[3] all sections off: crossover sums flat (LR8 all-pass), and drive 0 == drive off\n");
    {
        const double sr = 48000; double worst = 0;
        for (double f : { 30., 60., 100., 150., 300., 1000., 5000., 12000., 18000. })
        {
            gw::Params p = bypassAll(); p.crossoverHz = 100;
            const double g = db (gainAt (p, sr, f)); worst = std::fmax (worst, std::fabs (g));
            std::printf ("  f=%6.0f Hz : %+.4f dB\n", f, g);
        }
        CHECK (worst < 0.02, "crossover must sum flat; worst %.4f dB", worst);
        for (int os = 0; os < 3; ++os)
        {
            gw::Params a = bypassAll(); a.oversampling = os; a.drive = 0.0f; a.driveOn = true;     // drive knob at 0
            gw::Params b = bypassAll(); b.oversampling = os; b.drive = 0.9f; b.driveOn = false;    // drive section switched off
            const Buf in = noise (20000, 0.7f, 9); const Buf oa = runMono (a, in, sr), ob = runMono (b, in, sr);
            double md = 0; for (size_t i = 0; i < in.size(); ++i) md = std::fmax (md, std::fabs ((double) oa[i] - ob[i]));
            std::printf ("  os=%d: drive=0 vs section off max diff %.1e\n", os, md);
            CHECK (md == 0.0, "drive 0 must be an exact bypass of the shaper");
        }
    }

    // ---------------------------------------------------------------- 4. crossover & clean low band
    std::printf ("\n[4] crossover, clean + mono low band\n");
    {
        const double sr = 48000;
        for (double fc : { 60., 100., 200. })
        {
            // high band alone (sub off, drive off) should be a 24 dB/oct high-pass ; low alone a low-pass
            gw::Params p = bypassAll(); p.crossoverHz = (float) fc; p.subLevelDb = -48;      // low off -> output = HP4
            const double atFc = db (gainAt (p, sr, fc)), below = db (gainAt (p, sr, fc / 2)), above = db (gainAt (p, sr, fc * 4));
            std::printf ("  fc=%3.0f: HP8 at fc %+.2f dB (LR = -6.02), at fc/2 %+.2f (-48.2), at 4fc %+.3f\n", fc, atFc, below, above);
            CHECK (std::fabs (atFc + 6.02) < 0.15 && std::fabs (below + 48.2) < 1.0 && std::fabs (above) < 0.2, "HP4 shape fc=%f", fc);
        }
        // a 45 Hz sub with max drive (type tanh): the fundamental level must not change (low band is never saturated)
        gw::Params p; p.crossoverHz = 120; p.drive = 1; p.formantOn = false; p.postOn = false; p.satType = gw::SatTanh; p.subMono = false;
        const Buf in = sine (48000, sr, 45, 0.5); const Buf o = runMono (p, in, sr);
        const double g = db (rms (o, 24000, 48000) / rms (in, 24000, 48000));
        // fundamental via Goertzel-ish projection
        double sc = 0, ss = 0; for (size_t i = 24000; i < 48000; ++i) { sc += o[i] * std::cos (2 * PI * 45 * (double) i / sr); ss += o[i] * std::sin (2 * PI * 45 * (double) i / sr); }
        const double amp = 2 * std::sqrt (sc * sc + ss * ss) / 24000.0;
        std::printf ("  45 Hz sub, drive 100%%: fundamental %.4f (in 0.5000) = %+.2f dB; total RMS change %+.2f dB (harmonics added by the high band)\n", amp, db (amp / 0.5), g);
        CHECK (std::fabs (db (amp / 0.5)) < 1.5, "sub fundamental changed by %.2f dB", db (amp / 0.5));
        // mono: L = -R sub cancels when subMono on; kept when off
        for (int mono = 0; mono < 2; ++mono)
        {
            gw::Params q = bypassAll(); q.subMono = mono; q.crossoverHz = 150;
            const Buf l = sine (48000, sr, 50, 0.4), r = [&] { Buf b = l; for (auto& x : b) x = -x; return b; }();
            auto o2 = runStereo (q, { l, r }, sr);
            const double a = rms (o2[0], 24000, 48000);
            std::printf ("  50 Hz, L=-R, subMono=%d: output RMS %.4f (input %.4f)\n", mono, a, rms (l, 24000, 48000));
            CHECK (mono ? a < 0.12 * rms (l, 24000, 48000) + 0.02 : a > 0.5 * rms (l, 24000, 48000), "mono sub behaviour mono=%d", mono);
        }
        // sub level
        gw::Params q = bypassAll(); q.crossoverHz = 200; q.subLevelDb = -6;
        const double gl = db (gainAt (q, sr, 40)), gh = db (gainAt (q, sr, 4000));
        std::printf ("  sub level -6 dB: 40 Hz %+.2f dB, 4 kHz %+.2f dB\n", gl, gh);
        CHECK (std::fabs (gl + 6.0) < 1.0 && std::fabs (gh) < 0.1, "sub level");
    }

    // ---------------------------------------------------------------- 5. saturators
    std::printf ("\n[5] saturator types: harmonic content of a 1 kHz sine, drive 0.4 (+17 dB), formant/post off, 4x\n");
    {
        const double sr = 48000; const int N = 32768; const double k = 683; const double f0 = k * sr / N;   // coherent
        double h2[5] {}, h3[5] {};
        const char* names[] = { "tanh", "tube", "fold", "hard", "diode" };
        for (int t = 0; t < 5; ++t)
        {
            gw::Params p; p.satType = t; p.drive = 0.4f; p.formantOn = false; p.postOn = false; p.oversampling = 2; p.crossoverHz = 60; p.subLevelDb = -48; p.limiter = false;
            Buf in = sine (N + 8192, sr, f0, 0.2); Buf o = runMono (p, in, sr);
            auto s = spectrum (o.data() + 8192, (size_t) N, false);
            auto at = [&] (int h) { return s[(size_t) (k * h)]; };
            h2[t] = db (std::sqrt (at (2) / at (1))); h3[t] = db (std::sqrt (at (3) / at (1)));
            double thd = 0; for (int h = 2; h < 12; ++h) thd += at (h); thd = std::sqrt (thd / at (1));
            std::printf ("  %-5s : H2 %7.1f dB  H3 %7.1f dB  THD(2..11) %6.2f %%\n", names[t], h2[t], h3[t], 100 * thd);
            float pk; CHECK (finiteAll ({ o }, pk), "finite");
        }
        CHECK (h2[0] < -70, "tanh is odd symmetric: H2 %.1f", h2[0]);
        CHECK (h2[1] > h2[0] + 30 && h2[4] > h2[0] + 30, "tube/diode must add even harmonics");
        CHECK (h3[0] > -40, "tanh at +17 dB drive adds H3");
        // bias gives even harmonics to the symmetric tanh
        gw::Params p; p.satType = 0; p.drive = 0.4f; p.bias = 0.5f; p.formantOn = false; p.postOn = false; p.oversampling = 2; p.crossoverHz = 60; p.subLevelDb = -48; p.limiter = false;
        Buf in = sine (N + 8192, sr, f0, 0.2); Buf o = runMono (p, in, sr); auto s = spectrum (o.data() + 8192, (size_t) N, false);
        const double b2 = db (std::sqrt (s[(size_t) (k * 2)] / s[(size_t) k]));
        std::printf ("  tanh + bias 0.5 : H2 %.1f dB (was %.1f)\n", b2, h2[0]);
        CHECK (b2 > h2[0] + 40, "bias must create even harmonics");
        // DC after bias is blocked
        double dc = 0; for (size_t i = 16384; i < o.size(); ++i) dc += o[i]; dc /= (double) (o.size() - 16384);
        std::printf ("  residual DC with bias: %.5f\n", dc); CHECK (std::fabs (dc) < 0.01, "dc");
    }

    // ---------------------------------------------------------------- 6. aliasing
    std::printf ("\n[6] aliasing (coherent sine 3.4 kHz at 48 kHz through hard clip / fold, drive 0.6 = +25 dB). Non-harmonic energy 20 Hz..20 kHz relative to the fundamental\n");
    {
        const double sr = 48000; const int N = 32768; const double k = 2411; const double f0 = k * sr / N;  // k odd prime -> aliases never land on harmonics
        const char* nm[] = { "tanh", "tube", "fold", "hard", "diode" };
        for (int t : { 0, 2, 3 })
        {
            double al[3] {};
            for (int os = 0; os < 3; ++os)
            {
                gw::Params p; p.satType = t; p.drive = 0.6f; p.formantOn = false; p.postOn = false; p.oversampling = os; p.crossoverHz = 60; p.subLevelDb = -48; p.limiter = false;
                Buf in = sine (N + 8192, sr, f0, 0.3); Buf o = runMono (p, in, sr);
                auto s = spectrum (o.data() + 8192, (size_t) N, false);
                double fund = s[(size_t) k], sum = 0;
                const size_t hiBin = (size_t) (20000.0 / sr * N);
                for (size_t b = (size_t) (20.0 / sr * N); b < hiBin; ++b)
                {
                    if (b % (size_t) k == 0) continue;                       // exact harmonics (bins are integer multiples of k)
                    sum += s[b];
                }
                al[os] = db (std::sqrt (sum / fund));
            }
            std::printf ("  %-5s : off %7.1f dB   2x %7.1f dB   4x %7.1f dB   (2x gain %+.1f dB, 4x gain %+.1f dB)\n", nm[t], al[0], al[1], al[2], al[0] - al[1], al[0] - al[2]);
            CHECK (al[1] < al[0] - 6 && al[2] <= al[1] + 1, "oversampling must reduce aliasing (%s)", nm[t]);
        }
    }

    // ---------------------------------------------------------------- 7. formants
    std::printf ("\n[7] formant bank (white noise in, drive off, crossover 60 Hz, sub off): spectral peaks\n");
    {
        const double sr = 48000; const int N = 1 << 17;
        // local maxima of the smoothed noise spectrum inside +-18 % windows around the expected formants (edge hits = no peak)
        auto peaksOf = [&] (float vowel, float shift, float focus, const double expect[3], double out[3])
        {
            gw::Params p = bypassAll(); p.formantOn = true; p.formantAmount = 1; p.vowel = vowel; p.formantShift = shift; p.focus = focus; p.crossoverHz = 60; p.subLevelDb = -48;
            Buf in = noise (N + 8192, 0.3f, 3); Buf o = runMono (p, in, sr);
            auto s = spectrum (o.data() + 8192, (size_t) N, true);
            for (int b = 0; b < 3; ++b)
            {
                const size_t lo = (size_t) (expect[b] * 0.82 / sr * N), hi = (size_t) (expect[b] * 1.18 / sr * N);
                double best = 0; size_t bi = lo;
                for (size_t k2 = lo; k2 <= hi; ++k2) { double v = 0; for (int d = -6; d <= 6; ++d) v += s[(size_t) ((long) k2 + d)]; if (v > best) { best = v; bi = k2; } }
                out[b] = (bi == lo || bi == hi) ? -1.0 : (double) bi * sr / N;
            }
        };
        auto within = [] (const double pk[3], const double ex[3], double tol) { for (int i = 0; i < 3; ++i) if (pk[i] < 0 || std::fabs (pk[i] / ex[i] - 1.0) > tol) return false; return true; };
        double pk[3];
        const double ah[3] = { 730, 1090, 2440 }, ee[3] = { 270, 2290, 3010 }, ah12[3] = { 1460, 2180, 4880 };
        peaksOf (0.5f, 0, 0.5f, ah, pk);
        std::printf ("  vowel 0.50 (AH): peaks %.0f / %.0f / %.0f Hz (table 730 / 1090 / 2440)\n", pk[0], pk[1], pk[2]);
        CHECK (within (pk, ah, 0.08), "AH formants");
        peaksOf (1.0f, 0, 0.5f, ee, pk);
        std::printf ("  vowel 1.00 (EE): peaks %.0f / %.0f / %.0f Hz (table 270 / 2290 / 3010)\n", pk[0], pk[1], pk[2]);
        CHECK (within (pk, ee, 0.08), "EE formants");
        peaksOf (0.5f, 12, 0.5f, ah12, pk);
        std::printf ("  vowel 0.50 shift +12 st: peaks %.0f / %.0f / %.0f Hz (table x2: 1460 / 2180 / 4880)\n", pk[0], pk[1], pk[2]);
        CHECK (within (pk, ah12, 0.08), "formant shift +12 st doubles the frequencies");
        // focus narrows
        auto widthDb = [&] (float focus)
        {
            gw::Params p = bypassAll(); p.formantOn = true; p.vowel = 0.5f; p.focus = focus; p.crossoverHz = 60; p.subLevelDb = -48;
            return std::make_pair (db (gainAt (p, sr, 730, 0.1)), db (gainAt (p, sr, 900, 0.1)));
        };
        const auto w0 = widthDb (0.0f), w1 = widthDb (1.0f);
        std::printf ("  focus 0: F1 %+.1f dB, 170 Hz above %+.1f dB | focus 1: F1 %+.1f dB, 170 Hz above %+.1f dB\n", w0.first, w0.second, w1.first, w1.second);
        CHECK ((w1.first - w1.second) > (w0.first - w0.second) + 3, "focus must sharpen the formants");
        // amount 0 and formant off are the same signal
        gw::Params a = bypassAll(); a.formantOn = true; a.formantAmount = 0; gw::Params b = bypassAll();
        const Buf in = noise (20000, 0.5f, 4); const Buf oa = runMono (a, in, sr), ob = runMono (b, in, sr); double md = 0; for (size_t i = 0; i < in.size(); ++i) md = std::fmax (md, std::fabs ((double) oa[i] - ob[i]));
        std::printf ("  amount 0 vs section off: max diff %.1e\n", md); CHECK (md == 0.0, "amount 0 == bypass");
        // level: growl source 55 Hz saw through drive, then formants: loudness change
        gw::Params g; g.drive = 0.6f; g.formantOn = false; g.postOn = false; g.crossoverHz = 100;
        const Buf src = saw (96000, sr, 55, 0.5); const Buf dry = runMono (g, src, sr); g.formantOn = true; g.vowel = 0.5f; const Buf wet = runMono (g, src, sr);
        std::printf ("  saw 55 Hz growl, drive 0.6: RMS without formants %.3f, with AH formants %.3f (%+.1f dB)\n", rms (dry, 48000, 96000), rms (wet, 48000, 96000), db (rms (wet, 48000, 96000) / rms (dry, 48000, 96000)));
        CHECK (std::fabs (db (rms (wet, 48000, 96000) / rms (dry, 48000, 96000))) < 6.0, "formant make-up level within +-6 dB");
    }

    // ---------------------------------------------------------------- 8. LFO
    std::printf ("\n[8] tempo-synced vowel LFO\n");
    {
        const double sr = 48000; const char* rn[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
        for (double bpm : { 90.0, 120.0, 174.0 }) for (int rate = 2; rate < 6; ++rate)
        {
            gw::Params p; p.lfoDepth = 1; p.vowel = 0.5f; p.lfoRate = rate; p.lfoShape = gw::LfoSine;
            gw::Engine e; e.prepare (sr, 512, 1); e.setParams (p); e.snapSmoothers();
            Buf in (48000 * 4, 0.f); gw::Transport tp; tp.bpm = bpm;
            std::vector<float> v; int pos = 0; float* ptr; Buf o = in;
            while (pos < (int) in.size()) { ptr = o.data() + pos; e.process (&ptr, 1, 64, tp); v.push_back (e.currentVowel()); pos += 64; }
            // measure period from upward zero-crossings of (v - 0.5)
            std::vector<double> zc; for (size_t i = 1; i < v.size(); ++i) if (v[i - 1] < 0.5f && v[i] >= 0.5f) zc.push_back ((double) i + (0.5 - v[i - 1]) / (v[i] - v[i - 1]));
            const double per = zc.size() > 3 ? (zc.back() - zc[1]) / (double) (zc.size() - 2) * 64.0 / sr : 0.0;
            const double expect = gw::kLfoBeats[rate] * 60.0 / bpm;
            std::printf ("  %6.1f bpm rate %-4s: period %.4f s (expected %.4f s)\n", bpm, rn[rate], per, expect);
            CHECK (std::fabs (per - expect) < 0.01 * expect + 0.0015, "LFO period");
        }
        // phase locks to host ppq: sine starts at centre when ppq = 0, peaks a quarter cycle later
        gw::Params p; p.lfoDepth = 0.4f; p.vowel = 0.5f; p.lfoRate = 2; gw::Engine e; e.prepare (sr, 512, 1); e.setParams (p); e.snapSmoothers();
        Buf o (64, 0.f); float* ptr = o.data(); gw::Transport tp; tp.bpm = 120; tp.hasPpq = true; tp.ppq = 0.25;     // quarter of a 1/4-note cycle
        for (int i = 0; i < 12; ++i) e.process (&ptr, 1, 64, tp);
        std::printf ("  ppq 0.25 (rate 1/4): vowel %.3f (centre 0.5 + depth 0.4 -> peak 0.9)\n", e.currentVowel());
        CHECK (e.currentVowel() > 0.85f, "LFO phase must follow the host ppq");
        // depth 0 => constant
        p.lfoDepth = 0; e.setParams (p); e.snapSmoothers(); for (int i = 0; i < 20; ++i) { tp.ppq += 0.1; e.process (&ptr, 1, 64, tp); }
        CHECK (std::fabs (e.currentVowel() - 0.5f) < 1e-4f, "depth 0 must not modulate");
        // each shape stays inside [0,1] and covers the range
        for (int sh = 0; sh < gw::NumLfoShapes; ++sh)
        {
            p.lfoDepth = 1; p.lfoShape = sh; gw::Engine e2; e2.prepare (sr, 512, 1); e2.setParams (p); e2.snapSmoothers(); float mn = 9, mx = -9; Buf o2 (64, 0.f); float* q = o2.data(); gw::Transport t2; t2.bpm = 120;
            for (int i = 0; i < 400; ++i) { e2.process (&q, 1, 64, t2); mn = std::fmin (mn, e2.currentVowel()); mx = std::fmax (mx, e2.currentVowel()); }
            std::printf ("  shape %d: vowel range %.3f .. %.3f\n", sh, mn, mx); CHECK (mn >= 0 && mx <= 1 && mx - mn > 0.9f, "LFO shape %d range", sh);
        }
    }

    // ---------------------------------------------------------------- 9. post filter and output
    std::printf ("\n[9] post filter, output gain, limiter\n");
    {
        const double sr = 48000;
        gw::Params p = bypassAll(); p.postOn = true; p.postType = 0; p.postCutHz = 1000; p.postRes = 0.707f; p.crossoverHz = 60; p.subLevelDb = -48;
        const double lo = db (gainAt (p, sr, 300)), at = db (gainAt (p, sr, 1000)), hi = db (gainAt (p, sr, 8000));
        std::printf ("  LP 1 kHz: 300 Hz %+.2f dB, 1 kHz %+.2f dB, 8 kHz %+.2f dB\n", lo, at, hi);
        CHECK (std::fabs (lo) < 0.7 && std::fabs (at + 3) < 1.0 && hi < -30, "post LP");
        p.postType = 1; const double lo2 = db (gainAt (p, sr, 200)), hi2 = db (gainAt (p, sr, 8000));
        std::printf ("  HP 1 kHz: 200 Hz %+.2f dB, 8 kHz %+.2f dB\n", lo2, hi2); CHECK (lo2 < -20 && std::fabs (hi2) < 0.5, "post HP");
        gw::Params q = bypassAll(); q.outGainDb = 6; const double og = db (gainAt (q, sr, 1000)); std::printf ("  output +6 dB: %+.2f dB\n", og); CHECK (std::fabs (og - 6) < 0.05, "output gain");
        q = bypassAll(); q.mix = 0.5f; q.driveOn = true; q.drive = 1; q.satType = gw::SatHard; q.limiter = true;
        // limiter: peak never above 1.0, exactly transparent below -1 dBFS
        const Buf loud = sine (48000, sr, 220, 3.0), quiet = sine (48000, sr, 220, 0.85);
        gw::Params lp = bypassAll(); lp.outGainDb = 12; lp.limiter = true; const Buf ol = runMono (lp, loud, sr); float pk = 0; for (float x : ol) pk = std::fmax (pk, std::fabs (x));
        std::printf ("  limiter on, +12 dB gain into 3.0 peak: output peak %.4f\n", pk); CHECK (pk <= 1.0f, "limiter peak");
        lp.outGainDb = 0; const Buf oq = runMono (lp, quiet, sr); lp.limiter = false; const Buf oq2 = runMono (lp, quiet, sr); double md = 0; for (size_t i = 4000; i < oq.size(); ++i) md = std::fmax (md, std::fabs ((double) oq[i] - oq2[i]));
        std::printf ("  limiter below knee (0.85 peak, after the 80 ms filter settle): max diff vs limiter off %.1e\n", md); CHECK (md == 0.0, "limiter transparent below knee");
        // dry/wet half
    }

    // ---------------------------------------------------------------- 10. block independence
    std::printf ("\n[10] block-size independence (bit-exact)\n");
    for (double sr : rates) for (int os = 0; os < 3; ++os)
    {
        gw::Params p; p.drive = 0.8f; p.satType = gw::SatTube; p.bias = 0.3f; p.oversampling = os; p.lfoDepth = 0.7f; p.lfoRate = 4; p.lfoShape = gw::LfoSawUp; p.postOn = true; p.postCutHz = 5000; p.formantShift = 3; p.crossoverHz = 130;
        const Buf l = saw (30000, sr, 70, 0.4), r = noise (30000, 0.2f, 11); gw::Transport tp; tp.bpm = 137;
        gw::Engine e1; e1.prepare (sr, 4096, 2); e1.setParams (p); e1.snapSmoothers(); const auto ref = run (e1, { l, r }, { 30000 }, tp, false, sr);
        const std::vector<std::vector<int>> pats = { { 1 }, { 7 }, { 64 }, { 480, 33, 1, 1023, 5, 128 }, { 4096 } };
        double worst = 0;
        for (auto& pat : pats) { gw::Engine e2; e2.prepare (sr, 4096, 2); e2.setParams (p); e2.snapSmoothers(); const auto o = run (e2, { l, r }, pat, tp, false, sr); for (int c = 0; c < 2; ++c) for (size_t i = 0; i < l.size(); ++i) worst = std::fmax (worst, std::fabs ((double) o[(size_t) c][i] - ref[(size_t) c][i])); }
        std::printf ("  sr=%.0f os=%d : worst difference over 5 block patterns %.1e\n", sr, os, worst);
        CHECK (worst == 0.0, "block-size independence sr %.0f os %d", sr, os);
    }

    // ---------------------------------------------------------------- 11. NaN / extreme safety + stress
    std::printf ("\n[11] NaN / inf / extreme input and random-parameter stress\n");
    {
        const double sr = 48000; gw::Params p; p.drive = 1; p.lfoDepth = 1; p.postOn = true; p.formantOn = true; p.focus = 1; p.postRes = 10; p.oversampling = 2; p.limiter = true;
        Buf in = noise (48000, 1.0f, 2); for (size_t i = 5000; i < in.size(); i += 2500) { in[i] = NAN; in[i + 1] = INFINITY; in[i + 2] = -INFINITY; in[i + 3] = 1e30f; in[i + 4] = -1e30f; }
        auto o = runStereo (p, { in, in }, sr, 256); float pk; const bool fin = finiteAll (o, pk);
        std::printf ("  NaN/inf/1e30 injected: finite=%d  peak %.3f\n", fin, pk); CHECK (fin && pk <= 1.0f, "NaN safety");
        std::mt19937 g (42); std::uniform_real_distribution<float> U (0, 1); bool allFin = true; float maxPk = 0; int blocks = 0;
        for (double sr2 : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            gw::Engine e; e.prepare (sr2, 1024, 2); gw::Params q; e.setParams (q); e.snapSmoothers(); Buf a (1024), b (1024);
            for (int blk = 0; blk < 600; ++blk)
            {
                q.inGainDb = -24 + 48 * U (g); q.crossoverHz = 60 + 140 * U (g); q.subLevelDb = -48 + 54 * U (g); q.subMono = U (g) > 0.5f; q.driveOn = U (g) > 0.2f; q.drive = U (g); q.satType = (int) (U (g) * 5) % 5; q.bias = 2 * U (g) - 1;
                q.oversampling = (int) (U (g) * 3) % 3; q.formantOn = U (g) > 0.2f; q.vowel = U (g); q.formantShift = -12 + 24 * U (g); q.focus = U (g); q.formantAmount = U (g); q.lfoRate = (int) (U (g) * 6) % 6; q.lfoShape = (int) (U (g) * 5) % 5;
                q.lfoDepth = U (g); q.postOn = U (g) > 0.3f; q.postType = U (g) > 0.5f; q.postCutHz = 20 * std::pow (1000.f, U (g)); q.postRes = 0.5f + 9.5f * U (g); q.mix = U (g); q.outGainDb = -24 + 36 * U (g); q.limiter = true;
                e.setParams (q); for (int i = 0; i < 1024; ++i) { a[(size_t) i] = 1.5f * (U (g) * 2 - 1); b[(size_t) i] = 0.8f * std::sin ((float) (blk * 1024 + i) * 0.03f); }
                float* ptrs[2] = { a.data(), b.data() }; gw::Transport tp; tp.bpm = 60 + 200 * U (g); tp.hasPpq = U (g) > 0.5f; tp.ppq = U (g) * 100; e.process (ptrs, 2, 1024, tp); ++blocks;
                for (int i = 0; i < 1024; ++i) { if (! std::isfinite (a[(size_t) i]) || ! std::isfinite (b[(size_t) i])) allFin = false; maxPk = std::fmax (maxPk, std::fmax (std::fabs (a[(size_t) i]), std::fabs (b[(size_t) i]))); }
            }
        }
        std::printf ("  %d random-parameter blocks at 4 sample rates: finite=%d, max |out| %.4f\n", blocks, allFin, maxPk); CHECK (allFin && maxPk <= 1.0f, "stress");
    }

    // ---------------------------------------------------------------- 12. allocation
    std::printf ("\n[12] real-time safety\n");
    {
        const double sr = 48000; gw::Engine e; e.prepare (sr, 512, 2); gw::Params p; e.setParams (p); e.snapSmoothers(); Buf a = noise (512, 0.5f, 1), b = a; float* ptrs[2] = { a.data(), b.data() };
        std::mt19937 g (1); gw::Transport tp; tp.hasPpq = true;
        g_allocs = 0; g_countAllocs = true;
        for (int blk = 0; blk < 400; ++blk) { p.oversampling = blk % 3; p.satType = (blk / 3) % 5; p.drive = (float) (blk % 10) / 10; p.vowel = (float) (blk % 7) / 7; p.crossoverHz = 60 + (float) (blk % 14) * 10; e.setParams (p); tp.ppq = blk * 1.0; e.process (ptrs, 2, 512, tp); }
        g_countAllocs = false;
        std::printf ("  400 blocks incl. oversampling / shaper switches: %ld heap allocations in process()/setParams()\n", g_allocs); CHECK (g_allocs == 0, "audio path allocated");
        // CPU
        gw::Params hp; hp.oversampling = 2; hp.drive = 0.8f; hp.lfoDepth = 0.5f; hp.postOn = true; gw::Engine e2; e2.prepare (sr, 512, 2); e2.setParams (hp); e2.snapSmoothers();
        Buf x = saw (512, sr, 55, 0.5), y = x; float* pp[2] = { x.data(), y.data() }; gw::Transport t2; t2.bpm = 140;
        const auto t0 = std::chrono::steady_clock::now(); const int nb = 4000; for (int i = 0; i < nb; ++i) { std::copy (x.begin(), x.end(), x.begin()); e2.process (pp, 2, 512, t2); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), audio = nb * 512.0 / sr;
        std::printf ("  CPU (this box, -O2, stereo, 4x oversampling, 48 kHz): %.1f x real time (%.2f %% of one core)\n", audio / sec, 100 * sec / audio);
        CHECK (audio / sec > 20, "too slow");
    }

    // ---------------------------------------------------------------- 13. no clicks when toggling sections / switching modes
    std::printf ("\n[13] click-free section toggles and mode switches\n");
    {
        const double sr = 48000; gw::Transport tp; tp.bpm = 120; const Buf in = sine (96000, sr, 82.4, 0.5);
        struct Case { const char* name; void (*mod) (gw::Params&); };
        const Case cases[] = {
            { "DRIVE off", [] (gw::Params& p) { p.driveOn = false; } }, { "FORMANT off", [] (gw::Params& p) { p.formantOn = false; } },
            { "POST off", [] (gw::Params& p) { p.postOn = false; } }, { "sub MONO off", [] (gw::Params& p) { p.subMono = false; } },
            { "LIMITER off", [] (gw::Params& p) { p.limiter = false; } }, { "shaper type tanh->fold", [] (gw::Params& p) { p.satType = gw::SatFold; } },
            { "oversampling 2x->4x", [] (gw::Params& p) { p.oversampling = 2; } }, { "oversampling 4x->off", [] (gw::Params& p) { p.oversampling = 0; } },
            { "post LP->HP", [] (gw::Params& p) { p.postType = 1; } } };
        for (auto& cs : cases)
        {
            gw::Params a; a.drive = 0.7f; a.postOn = true; a.postCutHz = 3000; a.oversampling = 1; a.satType = 0; a.lfoDepth = 0.0f; a.vowel = 0.6f; a.crossoverHz = 100;
            if (std::string (cs.name).find ("4x->off") != std::string::npos) a.oversampling = 2;
            gw::Params b = a; cs.mod (b);
            double steady = 0;
            for (const gw::Params* pp : { &a, &b }) { Buf o = runMono (*pp, in, sr, tp); for (size_t i = 24000; i < o.size(); ++i) steady = std::fmax (steady, std::fabs (o[i] - o[i - 1])); }
            gw::Engine e; e.prepare (sr, 4096, 1); e.setParams (a); e.snapSmoothers(); Buf o = in; float* ptr = o.data(); e.process (&ptr, 1, 48000, tp); e.setParams (b); float* p2 = ptr + 48000; e.process (&p2, 1, 48000, tp);
            double m = 0; for (size_t i = 47990; i < 60000; ++i) m = std::fmax (m, std::fabs (o[i] - o[i - 1]));
            float pk; const bool fin = finiteAll ({ o }, pk);
            std::printf ("  %-24s : max sample step %.4f  (steady-state max %.4f)\n", cs.name, m, steady);
            CHECK (fin && m <= steady * 1.5 + 2.0e-3, "click when switching '%s'", cs.name);
        }
    }

    // ---------------------------------------------------------------- 14. sample rates, channels
    std::printf ("\n[14] channel counts and sample rates\n");
    for (double sr : { 22050.0, 44100.0, 88200.0, 192000.0 }) for (int nc : { 1, 2 })
    {
        gw::Params p; gw::Engine e; e.prepare (sr, 512, nc); e.setParams (p); e.snapSmoothers(); std::vector<Buf> in; for (int c = 0; c < nc; ++c) in.push_back (saw (20000, sr, 55, 0.5));
        auto o = run (e, in, { 128 }, {}, false, sr); float pk; const bool fin = finiteAll (o, pk); CHECK (fin && pk > 0.01f && pk <= 1.0f, "sr %.0f nc %d peak %f", sr, nc, pk);
    }
    std::printf ("  22.05 / 44.1 / 88.2 / 192 kHz, mono + stereo: finite and non-silent\n");

    std::printf ("\nRESULT: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
