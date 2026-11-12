#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

namespace
{
int failures = 0;
struct Num
{
    std::string name;
    double value;
};
std::vector<Num> numbers;

void record(const std::string &name, double v)
{
    numbers.push_back({ name, v });
    std::printf("  %-32s  % .4f\n", name.c_str(), v);
}

void check(bool ok, const char *msg)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", msg);
    }
    else
        std::printf("  ok  %s\n", msg);
}

void setup(StokeProcessor &p, double sr = 48000, int block = 512, int ch = 2)
{
    p.setNonRealtime(true);
    p.setPlayConfigDetails(ch, ch, sr, block);
    p.prepareToPlay(sr, block);
    p.snapEngine();
}

void render(StokeProcessor &p, juce::AudioBuffer<float> &b, int block = 512)
{
    juce::MidiBuffer m;
    const int n = b.getNumSamples();
    for (int i = 0; i < n; i += block)
    {
        const int k = std::min(block, n - i);
        juce::AudioBuffer<float> sl(b.getArrayOfWritePointers(), b.getNumChannels(), i, k);
        p.processBlock(sl, m);
    }
}

juce::AudioBuffer<float> silence(int ch, int n)
{
    juce::AudioBuffer<float> b(ch, n);
    b.clear();
    return b;
}

juce::AudioBuffer<float> sine(double sr, double hz, double sec, double amp = 0.125, int ch = 2)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(ch, n);
    for (int i = 0; i < n; ++i)
    {
        const float x = (float)(amp * std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / sr));
        for (int c = 0; c < ch; ++c)
            b.setSample(c, i, x);
    }
    return b;
}

juce::AudioBuffer<float> pink(double sr, double sec, unsigned seed = 1, double amp = 0.125, int ch = 2)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(ch, n);
    // Voss-McCartney-ish pink via 1/f shaping of white in the frequency domain, per channel.
    for (int c = 0; c < ch; ++c)
    {
        std::vector<double> re(n, 0), im(n, 0);
        unsigned s = seed + (unsigned)c * 17u;
        auto rng = [&s]() {
            s = s * 1664525u + 1013904223u;
            return (s / 4294967296.0) * 2.0 - 1.0;
        };
        for (int i = 1; i < n / 2; ++i)
        {
            const double f = (double)i;
            const double mag = 1.0 / std::sqrt(f);
            const double ph = rng() * juce::MathConstants<double>::pi;
            re[(size_t)i] = mag * std::cos(ph);
            im[(size_t)i] = mag * std::sin(ph);
            re[(size_t)(n - i)] = re[(size_t)i];
            im[(size_t)(n - i)] = -im[(size_t)i];
        }
        // inverse DFT (naive would be slow; use iterative FFT)
        const int N = n;
        // pad to power of two already? n is sec*sr — 4*48000 is not pot. Use time-domain filter instead.
        (void)N;
        (void)re;
        (void)im;
    }
    // Time-domain Paul Kellet pink, much simpler.
    for (int c = 0; c < ch; ++c)
    {
        unsigned s = seed + (unsigned)c * 17u;
        auto rng = [&s]() {
            s = s * 1664525u + 1013904223u;
            return (float)((s / 4294967296.0) * 2.0 - 1.0);
        };
        float b0 = 0, b1 = 0, b2 = 0;
        std::vector<float> tmp((size_t)n);
        double acc = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = rng();
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            const float x = b0 + b1 + b2 + w * 0.1848f;
            tmp[(size_t)i] = x;
            acc += (double)x * (double)x;
        }
        const float g = (float)(amp / std::sqrt(acc / std::max(n, 1)));
        const int fade = std::min(n / 20, (int)(0.008 * sr));
        for (int i = 0; i < n; ++i)
        {
            float env = 1;
            if (i < fade)
                env = (float)i / (float)std::max(fade, 1);
            if (i > n - fade)
                env = (float)(n - i) / (float)std::max(fade, 1);
            b.setSample(c, i, tmp[(size_t)i] * g * env);
        }
    }
    return b;
}

juce::AudioBuffer<float> stereoPink(double sr, double sec, double corr = 0.3, double amp = 0.125)
{
    auto L = pink(sr, sec, 1, amp, 1);
    auto R = pink(sr, sec, 18, amp, 1);
    const int n = L.getNumSamples();
    juce::AudioBuffer<float> b(2, n);
    const float c = (float)corr, s = (float)std::sqrt(std::max(0.0, 1.0 - corr * corr));
    for (int i = 0; i < n; ++i)
    {
        const float l = L.getSample(0, i);
        const float r = c * l + s * R.getSample(0, i);
        b.setSample(0, i, l);
        b.setSample(1, i, r);
    }
    return b;
}

// Simple synthesized loops for A/B: drums, bass, vocal-ish, mixed.
juce::AudioBuffer<float> drums(double sr, double sec)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(2, n);
    b.clear();
    const double dt = 0.5; // 120 bpm quarter
    unsigned seed = 11;
    auto rng = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed / 4294967296.0) * 2.0 - 1.0;
    };
    for (double t0 = 0; t0 < sec; t0 += dt)
    {
        const int i0 = (int)(t0 * sr);
        const int nk = std::min((int)(0.22 * sr), n - i0);
        for (int i = 0; i < nk; ++i)
        {
            const double t = i / sr;
            const double env = std::exp(-t * 18.0);
            const double freq = 55.0 * std::exp(-t * 12.0) + 35.0;
            const float x = (float)(0.85 * env * std::sin(2 * juce::MathConstants<double>::pi * freq * t));
            b.addSample(0, i0 + i, x);
            b.addSample(1, i0 + i, x);
        }
    }
    for (double t0 = dt; t0 < sec; t0 += 2 * dt)
    {
        const int i0 = (int)(t0 * sr);
        const int nk = std::min((int)(0.18 * sr), n - i0);
        for (int i = 0; i < nk; ++i)
        {
            const double t = i / sr;
            const double env = std::exp(-t * 22.0);
            const float nz = (float)rng();
            const float x = (float)(0.45 * env * (0.55 * nz + 0.45 * std::sin(2 * juce::MathConstants<double>::pi * 190 * t)));
            b.addSample(0, i0 + i, x);
            b.addSample(1, i0 + i, x * 0.95f);
        }
    }
    for (double t0 = 0; t0 < sec; t0 += dt / 2)
    {
        const int i0 = (int)(t0 * sr);
        const int nk = std::min((int)(0.05 * sr), n - i0);
        const float pan = ((int)std::llround(t0 / (dt / 2)) % 2 == 0) ? 0.2f : -0.2f;
        for (int i = 0; i < nk; ++i)
        {
            const double t = i / sr;
            const float x = (float)(0.12 * std::exp(-t * 70.0) * rng());
            b.addSample(0, i0 + i, x * (1.f - pan));
            b.addSample(1, i0 + i, x * (1.f + pan));
        }
    }
    float pk = 1e-12f;
    for (int i = 0; i < n; ++i)
        pk = std::max(pk, std::max(std::abs(b.getSample(0, i)), std::abs(b.getSample(1, i))));
    b.applyGain(0.2f / pk);
    return b;
}

juce::AudioBuffer<float> vocalish(double sr, double sec)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(2, n);
    const double notes[] = { 220.0, 246.94, 261.63, 246.94, 220.0, 196.0, 220.0, 174.61 };
    const int nn = 8;
    const double beat = sec / nn;
    for (int k = 0; k < nn; ++k)
    {
        const int i0 = (int)(k * beat * sr);
        const int nk = std::min((int)(beat * sr * 0.95), n - i0);
        const double f0 = notes[k];
        for (int i = 0; i < nk; ++i)
        {
            const double t = i / sr;
            const double env = (1.0 - std::exp(-t * 40.0)) * std::exp(-t * 1.6);
            // a few formants
            double x = 0.6 * std::sin(2 * juce::MathConstants<double>::pi * f0 * t);
            x += 0.25 * std::sin(2 * juce::MathConstants<double>::pi * f0 * 2 * t);
            x += 0.10 * std::sin(2 * juce::MathConstants<double>::pi * f0 * 3 * t);
            x += 0.06 * std::sin(2 * juce::MathConstants<double>::pi * 900 * t) * env;
            x += 0.05 * std::sin(2 * juce::MathConstants<double>::pi * 2200 * t) * env;
            const float y = (float)(0.22 * env * x);
            b.setSample(0, i0 + i, y);
            b.setSample(1, i0 + i, y * 0.98f);
        }
    }
    return b;
}

juce::AudioBuffer<float> synthLead(double sr, double sec)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = 330.0 * (1.0 + 0.02 * std::sin(2 * juce::MathConstants<double>::pi * 0.5 * t));
        double x = 0;
        for (int h = 1; h <= 8; ++h)
            x += (1.0 / h) * std::sin(2 * juce::MathConstants<double>::pi * f * h * t);
        const float y = (float)(0.12 * x * (0.7 + 0.3 * std::sin(2 * juce::MathConstants<double>::pi * 3 * t)));
        b.setSample(0, i, y);
        b.setSample(1, i, y * 0.92f + 0.08f * (float)std::sin(2 * juce::MathConstants<double>::pi * f * 1.01 * t));
    }
    return b;
}

juce::AudioBuffer<float> mix(const juce::AudioBuffer<float> &a, const juce::AudioBuffer<float> &b,
                             float ga = 0.7f, float gb = 0.55f)
{
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    const int c = std::min(a.getNumChannels(), b.getNumChannels());
    juce::AudioBuffer<float> o(c, n);
    for (int ch = 0; ch < c; ++ch)
        for (int i = 0; i < n; ++i)
            o.setSample(ch, i, ga * a.getSample(ch, i) + gb * b.getSample(ch, i));
    return o;
}

double rms(const juce::AudioBuffer<float> &b, int start = 0, int end = -1)
{
    if (end < 0)
        end = b.getNumSamples();
    double a = 0;
    long k = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < end; ++i, ++k)
            a += (double)b.getSample(c, i) * (double)b.getSample(c, i);
    return k ? std::sqrt(a / (double)k) : 0;
}

double rmsDb(const juce::AudioBuffer<float> &b, int start = 0, int end = -1)
{
    return 20.0 * std::log10(std::max(rms(b, start, end), 1e-12));
}

double peakDb(const juce::AudioBuffer<float> &b)
{
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            p = std::max(p, std::abs(b.getSample(c, i)));
    return 20.0 * std::log10(std::max((double)p, 1e-12));
}

double residualDb(const juce::AudioBuffer<float> &a, const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    const int c = std::min(a.getNumChannels(), b.getNumChannels());
    double num = 0, den = 0;
    for (int ch = 0; ch < c; ++ch)
        for (int i = start; i < n; ++i)
        {
            const double d = (double)a.getSample(ch, i) - (double)b.getSample(ch, i);
            num += d * d;
            den += (double)a.getSample(ch, i) * (double)a.getSample(ch, i);
        }
    if (num <= 0)
        return -240.0;
    return 20.0 * std::log10(std::sqrt(num / std::max(den, 1e-30)));
}

double residualAbsDb(const juce::AudioBuffer<float> &a, const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    const int c = std::min(a.getNumChannels(), b.getNumChannels());
    double num = 0;
    long k = 0;
    for (int ch = 0; ch < c; ++ch)
        for (int i = start; i < n; ++i, ++k)
        {
            const double d = (double)a.getSample(ch, i) - (double)b.getSample(ch, i);
            num += d * d;
        }
    if (num <= 0)
        return -240.0;
    return 20.0 * std::log10(std::sqrt(num / std::max((double)k, 1.0)));
}

double correlation(const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = b.getNumSamples();
    if (b.getNumChannels() < 2 || n - start < 8)
        return 1;
    double sl = 0, sr = 0, sl2 = 0, sr2 = 0, slr = 0;
    const int k = n - start;
    for (int i = start; i < n; ++i)
    {
        const double l = b.getSample(0, i), r = b.getSample(1, i);
        sl += l;
        sr += r;
        sl2 += l * l;
        sr2 += r * r;
        slr += l * r;
    }
    const double ml = sl / k, mr = sr / k;
    const double vl = sl2 / k - ml * ml, vr = sr2 / k - mr * mr, cv = slr / k - ml * mr;
    const double d = std::sqrt(std::max(vl, 0.0) * std::max(vr, 0.0));
    return d > 1e-20 ? cv / d : 1;
}

double foldLossDb(const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = b.getNumSamples();
    if (b.getNumChannels() < 2)
        return 0;
    double sL = 0, sM = 0;
    for (int i = start; i < n; ++i)
    {
        const double l = b.getSample(0, i), r = b.getSample(1, i);
        sL += l * l;
        const double m = 0.5 * (l + r);
        sM += m * m;
    }
    const double rL = std::sqrt(sL / std::max(n - start, 1));
    const double rM = std::sqrt(sM / std::max(n - start, 1));
    // Compare mono fold to a single channel; 0 means no loss vs average channel energy.
    const double ref = rL;
    return 20.0 * std::log10(std::max(rM, 1e-12) / std::max(ref, 1e-12));
}

void fftMag(const std::vector<float> &x, std::vector<double> &mag)
{
    int n = (int)x.size();
    int N = 1;
    while (N < n)
        N <<= 1;
    std::vector<double> re(N, 0), im(N, 0);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / std::max(n - 1, 1));
        re[(size_t)i] = x[(size_t)i] * w;
    }
    for (int i = 1, j = 0; i < N; ++i)
    {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[(size_t)i], re[(size_t)j]);
            std::swap(im[(size_t)i], im[(size_t)j]);
        }
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const double ang = -2 * juce::MathConstants<double>::pi / len;
        const double wr0 = std::cos(ang), wi0 = std::sin(ang);
        for (int i = 0; i < N; i += len)
        {
            double wr = 1, wi = 0;
            for (int k = 0; k < len / 2; ++k)
            {
                const int i1 = i + k, i2 = i1 + len / 2;
                const double tr = wr * re[(size_t)i2] - wi * im[(size_t)i2];
                const double ti = wr * im[(size_t)i2] + wi * re[(size_t)i2];
                re[(size_t)i2] = re[(size_t)i1] - tr;
                im[(size_t)i2] = im[(size_t)i1] - ti;
                re[(size_t)i1] += tr;
                im[(size_t)i1] += ti;
                const double nwr = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = nwr;
            }
        }
    }
    mag.resize((size_t)N / 2);
    for (int i = 0; i < N / 2; ++i)
        mag[(size_t)i] = std::sqrt(re[(size_t)i] * re[(size_t)i] + im[(size_t)i] * im[(size_t)i]);
}

double bandDb(const juce::AudioBuffer<float> &b, double sr, double f1, double f2, int start = 0)
{
    const int n = b.getNumSamples() - start;
    if (n < 64)
        return -240;
    std::vector<float> x((size_t)n, 0);
    const int ch = b.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        double s = 0;
        for (int c = 0; c < ch; ++c)
            s += b.getSample(c, start + i);
        x[(size_t)i] = (float)(s / ch);
    }
    std::vector<double> mag;
    fftMag(x, mag);
    const int N = (int)mag.size() * 2;
    double acc = 0;
    int k0 = 0;
    for (int i = 1; i < (int)mag.size(); ++i)
    {
        const double hz = sr * i / N;
        if (hz >= f1 && hz < f2)
        {
            acc += mag[(size_t)i] * mag[(size_t)i];
            ++k0;
        }
    }
    return 10.0 * std::log10(std::max(acc / std::max(k0, 1), 1e-30));
}

double alias15k(const juce::AudioBuffer<float> &b, double sr, double f0 = 15000.0, int start = 0)
{
    // Energy at the principal alias of the 2nd harmonic: 2*f0 folds about Nyquist.
    // At 48 kHz, 30 kHz aliases to 18 kHz. Also look at other unexpected bins,
    // ignoring the fundamental and a narrow window around it.
    const int n = b.getNumSamples() - start;
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = 0.5f * (b.getSample(0, start + i) + (b.getNumChannels() > 1 ? b.getSample(1, start + i) : 0));
    std::vector<double> mag;
    fftMag(x, mag);
    const int N = (int)mag.size() * 2;
    const double ny = sr * 0.5;
    auto binHz = [&](double hz) { return (int)std::llround(hz * N / sr); };
    auto energyAt = [&](double hz, int w) {
        const int c = binHz(hz);
        double e = 0;
        for (int i = std::max(1, c - w); i <= std::min((int)mag.size() - 1, c + w); ++i)
            e = std::max(e, mag[(size_t)i]);
        return e;
    };
    // Peak near 15 kHz (fundamental) — for scaling, but we report absolute dBFS of the
    // first alias (2nd harmonic folded).
    double aliasHz = 2.0 * f0;
    while (aliasHz > ny)
        aliasHz = 2.0 * ny - aliasHz;
    if (aliasHz < 0)
        aliasHz = -aliasHz;
    // If it lands on the fundamental, use the 3rd harmonic fold.
    if (std::abs(aliasHz - f0) < 80.0)
    {
        aliasHz = 3.0 * f0;
        while (aliasHz > ny)
            aliasHz = 2.0 * ny - aliasHz;
        if (aliasHz < 0)
            aliasHz = -aliasHz;
    }
    const double a = energyAt(aliasHz, 3);
    // Convert FFT magnitude of a Hann-windowed signal to a rough dBFS (peak of a 0 dBFS sine
    // is about N/4). We report 20*log10(a / (N/4)).
    const double ref = (double)N / 4.0;
    return 20.0 * std::log10(std::max(a / ref, 1e-16));
}

double thdn(const juce::AudioBuffer<float> &b, double sr, double f0, int start = 0)
{
    const int n = b.getNumSamples() - start;
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = 0.5f * (b.getSample(0, start + i) + (b.getNumChannels() > 1 ? b.getSample(1, start + i) : 0));
    std::vector<double> mag;
    fftMag(x, mag);
    const int N = (int)mag.size() * 2;
    auto bin = [&](double hz) { return (int)std::llround(hz * N / sr); };
    auto around = [&](double hz, int w) {
        const int c = bin(hz);
        double e = 0;
        for (int i = std::max(1, c - w); i <= std::min((int)mag.size() - 1, c + w); ++i)
            e += mag[(size_t)i] * mag[(size_t)i];
        return e;
    };
    double fund = around(f0, 4);
    double harm = 0, rest = 0;
    for (int h = 2; h <= 10; ++h)
    {
        const double hz = f0 * h;
        if (hz >= sr * 0.5 - 30)
            break;
        harm += around(hz, 3);
    }
    for (int i = 1; i < (int)mag.size(); ++i)
        rest += mag[(size_t)i] * mag[(size_t)i];
    rest = std::max(0.0, rest - fund);
    return 20.0 * std::log10(std::sqrt(std::max(rest, 0.0)) / std::sqrt(std::max(fund, 1e-30)));
}

long long nonfinite(const juce::AudioBuffer<float> &b, long long *denorm = nullptr)
{
    long long bad = 0, den = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample(c, i);
            if (!std::isfinite(x))
                ++bad;
            else if (x != 0.f && std::abs(x) < 1.0e-37f)
                ++den;
        }
    if (denorm)
        *denorm = den;
    return bad;
}

void writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
    if (!os)
        return;
    if (auto w = std::unique_ptr<juce::AudioFormatWriter>(
            fmt.createWriterFor(os.get(), sr, (unsigned)b.getNumChannels(), 24, {}, 0)))
    {
        os.release();
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
}

juce::AudioBuffer<float> copyOf(const juce::AudioBuffer<float> &b)
{
    juce::AudioBuffer<float> o(b.getNumChannels(), b.getNumSamples());
    o.makeCopyOf(b);
    return o;
}

int tests(const juce::File &folder)
{
    const double sr = 48000;
    const int block = 512;
    std::printf("STOKE tests  sr=%.0f  block=%d\n", sr, block);

    // -------------------- Idle silence, default + every preset --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        const int n = (int)(32 * sr);
        auto b = silence(2, n);
        render(p, b);
        const double d = rmsDb(b);
        record("idle_default_db", d);
        check(d <= -120.0, "idle at default is true silence (<= -120 dBFS)");
        long long den = 0;
        check(nonfinite(b, &den) == 0, "idle default: no NaNs");
        check(den == 0, "idle default: no denormals");

        for (int i = 0; i < stoke::presetCount; ++i)
        {
            StokeProcessor q;
            setup(q, sr, block);
            q.factory(i);
            auto s = silence(2, (int)(4 * sr));
            render(q, s);
            const double pd = rmsDb(s);
            record((std::string("idle_preset_") + std::to_string(i) + "_db").c_str(), pd);
            check(pd <= -120.0, (std::string("idle preset ") + stoke::presetNames[i] + " is silent").c_str());
        }
    }

    // -------------------- Amount 0 is a bit-transparent null --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        p.set("focus", 0);
        p.set("glow", 0.28f); // leftover glow must still be silent at FOCUS 0
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        p.snapEngine();
        auto dry = pink(sr, 3.0, 3, 0.125);
        auto wet = copyOf(dry);
        render(p, wet);
        const double r = residualAbsDb(dry, wet, (int)(0.05 * sr));
        const double rel = residualDb(dry, wet, (int)(0.05 * sr));
        record("null_focus0_abs_db", r);
        record("null_focus0_rel_db", rel);
        check(r <= -120.0 || rel <= -120.0, "FOCUS 0 is a bit-transparent null (<= -120 dB)");
        writeWav(folder.getChildFile("null-focus0.wav"), wet, sr);
    }

    // Mix 0 is a wire, even with FOCUS up.
    {
        StokeProcessor p;
        setup(p, sr, block);
        p.set("focus", 1);
        p.set("mix", 0);
        p.set("gainmatch", 0);
        p.snapEngine();
        auto dry = pink(sr, 2.0, 5, 0.125);
        auto wet = copyOf(dry);
        render(p, wet);
        const double r = residualAbsDb(dry, wet, (int)(0.05 * sr));
        record("null_mix0_abs_db", r);
        check(r <= -120.0, "MIX 0 is a true wire");
    }

    // -------------------- Default gain (MATCH on) within +/- 0.5 dB --------------------
    auto measureGain = [&](const juce::AudioBuffer<float> &sig, const char *tag) {
        StokeProcessor p;
        setup(p, sr, block);
        // defaults already: FOCUS 0.5, MATCH on
        auto dry = copyOf(sig);
        auto wet = copyOf(sig);
        render(p, wet);
        const int skip = (int)(0.8 * sr); // let match settle
        const double gd = rmsDb(wet, skip) - rmsDb(dry, skip);
        record((std::string("gain_default_") + tag).c_str(), gd);
        return gd;
    };
    {
        const double gp = measureGain(pink(sr, 4.0, 1, 0.125), "pink");
        const double gd = measureGain(drums(sr, 6.0), "drums");
        const double gv = measureGain(vocalish(sr, 6.0), "vocal");
        check(std::abs(gp) <= 0.5, "default gain on pink within +/- 0.5 dB");
        check(std::abs(gd) <= 0.5, "default gain on drums within +/- 0.5 dB");
        check(std::abs(gv) <= 0.5, "default gain on vocal within +/- 0.5 dB");
    }

    // -------------------- Mud minus gain at default --------------------
    auto mudGain = [&](const juce::AudioBuffer<float> &sig, const char *tag) {
        StokeProcessor p;
        setup(p, sr, block);
        auto dry = copyOf(sig);
        auto wet = copyOf(sig);
        render(p, wet);
        const int skip = (int)(0.5 * sr);
        const double mud = bandDb(wet, sr, 200, 500, skip) - bandDb(dry, sr, 200, 500, skip);
        const double gain = rmsDb(wet, skip) - rmsDb(dry, skip);
        const double mg = mud - gain;
        record((std::string("mud_delta_") + tag).c_str(), mud);
        record((std::string("mud_minus_gain_") + tag).c_str(), mg);
        writeWav(folder.getChildFile(juce::String("default-") + tag + ".wav"), wet, sr);
        writeWav(folder.getChildFile(juce::String("dry-") + tag + ".wav"), dry, sr);
        return mg;
    };
    {
        const double mp = mudGain(pink(sr, 4.0, 2, 0.125), "pink");
        const double md = mudGain(drums(sr, 6.0), "drums");
        const double mv = mudGain(vocalish(sr, 6.0), "vocal");
        // A bass line with some mid (not a pure 110 Hz sine, whose 200-500 Hz
        // floor is near digital zero and makes any tiny harmonic look huge).
        auto bass = sine(sr, 55, 3.0, 0.12);
        {
            const int n = bass.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                const double ti = i / sr;
                const float x = (float)(0.10 * std::sin(2 * juce::MathConstants<double>::pi * 55 * ti)
                                        + 0.06 * std::sin(2 * juce::MathConstants<double>::pi * 110 * ti)
                                        + 0.03 * std::sin(2 * juce::MathConstants<double>::pi * 165 * ti)
                                        + 0.04 * std::sin(2 * juce::MathConstants<double>::pi * 440 * ti));
                bass.setSample(0, i, x);
                bass.setSample(1, i, x);
            }
        }
        const double mb = mudGain(bass, "bass");
        check(mp <= 0.5, "mud-gain on pink <= +0.5 dB");
        check(md <= 0.5, "mud-gain on drums <= +0.5 dB");
        check(mv <= 0.5, "mud-gain on vocal <= +0.5 dB");
        check(mb <= 0.5, "mud-gain on bass <= +0.5 dB");
    }

    // -------------------- THD+N and 15 kHz alias at default --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        // 1 kHz at -18 dBFS, mid-band, so GLOW will add a little THD.
        auto dry = sine(sr, 1000, 2.5, 0.125);
        auto wet = copyOf(dry);
        render(p, wet);
        const double t = thdn(wet, sr, 1000, (int)(0.2 * sr));
        record("thdn_1k_default_db", t);
        writeWav(folder.getChildFile("thdn-1k.wav"), wet, sr);

        StokeProcessor q;
        setup(q, sr, block);
        auto s15 = sine(sr, 15000, 2.5, 0.5); // hotter, still under ceiling
        auto w15 = copyOf(s15);
        render(q, w15);
        const double a = alias15k(w15, sr, 15000, (int)(0.2 * sr));
        record("alias_15k_default_dBFS", a);
        check(a <= -90.0, "15 kHz alias at default is below -90 dBFS");
        writeWav(folder.getChildFile("alias-15k.wav"), w15, sr);
    }

    // -------------------- Mono-safe stereo --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        auto monoIn = pink(sr, 3.0, 4, 0.125, 2); // L = R already
        // force exact L=R
        for (int i = 0; i < monoIn.getNumSamples(); ++i)
            monoIn.setSample(1, i, monoIn.getSample(0, i));
        auto wet = copyOf(monoIn);
        render(p, wet);
        const double c = correlation(wet, (int)(0.2 * sr));
        record("corr_mono_input", c);
        check(c > 0.999, "mono input stays mono (correlation ~ 1)");

        StokeProcessor q;
        setup(q, sr, block);
        auto st = stereoPink(sr, 3.0, 0.3, 0.125);
        auto wst = copyOf(st);
        render(q, wst);
        const double cs = correlation(wst, (int)(0.2 * sr));
        const double flDry = foldLossDb(st, (int)(0.2 * sr));
        const double flWet = foldLossDb(wst, (int)(0.2 * sr));
        const double flDelta = flWet - flDry;
        record("corr_stereo_pink", cs);
        record("fold_loss_dry_db", flDry);
        record("fold_loss_wet_db", flWet);
        record("fold_loss_delta_db", flDelta);
        check(cs > 0.0, "stereo pink keeps positive correlation");
        check(flDelta > -0.5, "mono fold is within 0.5 dB of the dry image");

        // Mid-only on a side-only signal should be nearly a wire.
        StokeProcessor r;
        setup(r, sr, block);
        r.set("midonly", 1);
        r.set("focus", 0.8f);
        r.snapEngine();
        auto side = pink(sr, 2.5, 9, 0.125, 2);
        for (int i = 0; i < side.getNumSamples(); ++i)
        {
            const float x = side.getSample(0, i);
            side.setSample(0, i, x);
            side.setSample(1, i, -x);
        }
        auto dryS = copyOf(side);
        auto wetS = copyOf(side);
        render(r, wetS);
        const double rs = residualDb(dryS, wetS, (int)(0.2 * sr));
        record("midonly_side_residual_db", rs);
        check(rs < -40.0, "MID ONLY leaves a side-only signal almost untouched");
    }

    // -------------------- Latency --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        check(p.getLatencySamples() == 0, "reported latency is 0 samples");
        record("latency_samples", (double)p.getLatencySamples());
    }

    // -------------------- Zipper: silence stays silent while knobs move --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        auto b = silence(2, (int)(2 * sr));
        juce::MidiBuffer m;
        const int n = b.getNumSamples();
        for (int i = 0; i < n; i += block)
        {
            const float t = (float)i / (float)n;
            p.set("focus", t);
            p.set("centre", 300.f + t * 4700.f);
            p.set("width", 0.5f + t * 2.5f);
            p.set("glow", t);
            p.set("smooth", t);
            p.set("mix", 0.5f + 0.5f * t);
            p.set("output", -6.f + 12.f * t);
            const int k = std::min(block, n - i);
            juce::AudioBuffer<float> sl(b.getArrayOfWritePointers(), 2, i, k);
            p.processBlock(sl, m);
        }
        const double z = rmsDb(b);
        record("zipper_silence_db", z);
        check(z <= -120.0, "sweeping knobs on silence produces no zipper noise");
    }

    // -------------------- Every knob is audibly useful --------------------
    {
        auto useful = [&](const char *id, float a, float b, const char *tag, double minDb) {
            StokeProcessor p, q;
            setup(p, sr, block);
            setup(q, sr, block);
            p.set("gainmatch", 0);
            q.set("gainmatch", 0);
            p.set("focus", 0.7f);
            q.set("focus", 0.7f);
            p.set(id, a);
            q.set(id, b);
            p.snapEngine();
            q.snapEngine();
            auto sig = vocalish(sr, 3.0);
            auto wa = copyOf(sig), wb = copyOf(sig);
            render(p, wa);
            render(q, wb);
            const double d = residualDb(wa, wb, (int)(0.2 * sr));
            record((std::string("knob_") + tag + "_delta_db").c_str(), d);
            check(d > minDb, (std::string(tag) + " changes the sound across its travel").c_str());
            return d;
        };
        useful("focus", 0.15f, 0.9f, "focus", -20);          // large change (residual is relative, more negative = more similar)
        // residualDb is 20*log10(residual/energy) so a BIG difference is closer to 0 dB (or positive).
        // A small difference is -60. We want d > -20 meaning "clearly different".
        useful("centre", 500.f, 3500.f, "centre", -25);
        useful("width", 0.6f, 2.8f, "width", -30);
        useful("glow", 0.f, 1.f, "glow", -30);
        useful("smooth", 0.f, 1.f, "smooth", -35);
        useful("mix", 0.25f, 1.f, "mix", -20);
        useful("output", -6.f, 6.f, "output", -10);
    }

    // -------------------- Mid-band presence lift, loudness-matched, on real loops --------------------
    auto presence = [&](const juce::AudioBuffer<float> &sig, const char *tag) {
        StokeProcessor p;
        setup(p, sr, block);
        auto dry = copyOf(sig);
        auto wet = copyOf(sig);
        render(p, wet);
        const int skip = (int)(0.5 * sr);
        // Loudness-match the wet to the dry for a fair mid comparison.
        const double g = rms(dry, skip) / std::max(rms(wet, skip), 1e-12);
        wet.applyGain((float)g);
        const double midLift = bandDb(wet, sr, 1000, 4000, skip) - bandDb(dry, sr, 1000, 4000, skip);
        const double harshPeak = bandDb(wet, sr, 2000, 5000, skip) - bandDb(dry, sr, 2000, 5000, skip);
        const double lowMid = bandDb(wet, sr, 200, 500, skip) - bandDb(dry, sr, 200, 500, skip);
        record((std::string("presence_1_4k_") + tag).c_str(), midLift);
        record((std::string("harsh_2_5k_") + tag).c_str(), harshPeak);
        record((std::string("lowmid_200_500_") + tag).c_str(), lowMid);
        writeWav(folder.getChildFile(juce::String("ab-wet-") + tag + ".wav"), wet, sr);
        writeWav(folder.getChildFile(juce::String("ab-dry-") + tag + ".wav"), dry, sr);
        return midLift;
    };
    {
        const auto d = drums(sr, 6.0);
        const auto v = vocalish(sr, 6.0);
        const auto s = synthLead(sr, 6.0);
        const auto mx = mix(mix(d, v, 0.8f, 0.7f), s, 1.f, 0.55f);
        const double pd = presence(d, "drums");
        const double pv = presence(v, "vocal");
        const double ps = presence(s, "synth");
        const double pm = presence(mx, "mix");
        check(pv > 0.4, "vocal: mid-band presence is up (loudness-matched)");
        check(ps > 0.3, "synth: mid-band presence is up (loudness-matched)");
        check(pd > 0.15, "drums: mid-band presence is up (loudness-matched)");
        check(pm > 0.2, "full mix: mid-band presence is up (loudness-matched)");
        writeWav(folder.getChildFile("loop-drums.wav"), d, sr);
        writeWav(folder.getChildFile("loop-vocal.wav"), v, sr);
        writeWav(folder.getChildFile("loop-synth.wav"), s, sr);
        writeWav(folder.getChildFile("loop-mix.wav"), mx, sr);
    }

    // -------------------- State save / restore, presets, no NaNs --------------------
    long long bad = 0, den = 0;
    {
        StokeProcessor p;
        setup(p, sr, block);
        p.factory(2); // Vocal Forward
        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        StokeProcessor q;
        setup(q, sr, block);
        q.setStateInformation(mb.getData(), (int)mb.getSize());
        q.snapEngine();
        auto sig = vocalish(sr, 2.0);
        auto wa = copyOf(sig), wb = copyOf(sig);
        render(p, wa);
        render(q, wb);
        const double d = residualDb(wa, wb, (int)(0.2 * sr));
        record("state_restore_residual_db", d);
        check(d < -60.0, "state save/restore is consistent");
        bad += nonfinite(wa, &den);
    }

    // -------------------- CPU --------------------
    {
        StokeProcessor p;
        setup(p, sr, block);
        p.set("focus", 0.85f);
        p.set("glow", 0.8f);
        p.snapEngine();
        auto sig = stereoPink(sr, 8.0, 0.3, 0.15);
        const auto t0 = std::chrono::steady_clock::now();
        render(p, sig);
        const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double audio = 8.0;
        const double rtf = sec / audio;
        const double pct = rtf * 100.0;
        record("cpu_rtf", rtf);
        record("cpu_pct_one_core", pct);
        check(pct < 15.0, "CPU is under 15% of one core at a heavy setting");
        bad += nonfinite(sig, &den);
    }

    record("nonfinite", (double)bad);
    record("denorm", (double)den);
    check(bad == 0, "no NaNs or infinities");
    check(den == 0, "no denormals (FTZ/DAZ)");

    // -------------------- Editor snapshot --------------------
    {
        StokeProcessor p;
        setup(p, sr, 256, 2);
        p.factory(0);
        auto ed = std::make_unique<StokeEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        check(image.getWidth() > 100 && image.getHeight() > 100, "editor snapshot has a real size");
        auto f = folder.getChildFile("editor-smoke.png");
        f.deleteFile();
        juce::FileOutputStream os{ f };
        juce::PNGImageFormat().writeImageToStream(image, os);
    }

    {
        std::ofstream o(folder.getChildFile("results.json").getFullPathName().toStdString());
        o << "{\n  \"failures\": " << failures << ",\n";
        for (size_t i = 0; i < numbers.size(); ++i)
        {
            o << "  \"" << numbers[i].name << "\": " << numbers[i].value;
            if (i + 1 < numbers.size())
                o << ",";
            o << "\n";
        }
        o << "}\n";
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}

} // namespace

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf(stderr, "StokeTest test folder | screenshot file [presetIndex]\n");
        return 2;
    }
    juce::String mode = argv[1];
    if (mode == "test")
    {
        juce::File f(argv[2]);
        f.createDirectory();
        return tests(f);
    }
    if (mode == "screenshot")
    {
        StokeProcessor p;
        setup(p);
        const int preset = argc > 3 ? std::atoi(argv[3]) : 0;
        p.factory(preset);
        auto ed = std::make_unique<StokeEditor>(p);
        ed->revealPreset(preset);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{ imageFile };
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    return 2;
}
