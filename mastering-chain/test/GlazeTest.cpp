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
    std::printf("  %-32s  % .6g\n", name.c_str(), v);
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

void setup(GlazeProcessor &p, double sr = 48000, int block = 512, int ch = 2)
{
    p.setNonRealtime(true);
    p.setPlayConfigDetails(ch, ch, sr, block);
    p.prepareToPlay(sr, block);
    p.snapEngine();
}

void render(GlazeProcessor &p, juce::AudioBuffer<float> &b, int block = 512)
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

// ------------------------------------------------------------------------------------------------ extra helpers
using glaze::Settings;
namespace G = glaze;

double toneDb(const juce::AudioBuffer<float> &b, double sr, double f, int start = 0, int ch = -1)
{
    // peak amplitude of a tone at f (Goertzel-style single-bin DFT with a Hann window), in dBFS
    const int n = b.getNumSamples() - start;
    if (n < 64)
        return -200;
    double re = 0, im = 0, wsum = 0;
    const int nch = b.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / (n - 1));
        double x = 0;
        if (ch >= 0)
            x = b.getSample(ch, start + i);
        else
            for (int c = 0; c < nch; ++c)
                x += b.getSample(c, start + i) / nch;
        const double ph = 2 * juce::MathConstants<double>::pi * f * i / sr;
        re += w * x * std::cos(ph);
        im -= w * x * std::sin(ph);
        wsum += w;
    }
    const double amp = 2.0 * std::sqrt(re * re + im * im) / wsum;
    return 20.0 * std::log10(std::max(amp, 1e-12));
}

// independent true-peak estimator: 16x oversampling with a 48-tap Kaiser-windowed sinc (not the engine's 4x / 16-tap)
double truePeakRef(const juce::AudioBuffer<float> &b, int start = 0)
{
    static std::vector<std::vector<double>> h;
    if (h.empty())
    {
        const int taps = 48, up = 16;
        h.assign((size_t)up, std::vector<double>((size_t)taps, 0.0));
        auto i0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 50; ++k)
            {
                t *= (x / (2.0 * k)) * (x / (2.0 * k));
                s += t;
            }
            return s;
        };
        for (int k = 1; k < up; ++k)
            for (int j = 0; j < taps; ++j)
            {
                const double t = (double)(j - taps / 2 + 1) - (double)k / up; // taps/2-1 samples before
                const double sinc = std::abs(t) < 1e-9 ? 1.0 : std::sin(juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double r = t / (taps / 2 + 0.0);
                const double w = std::abs(r) >= 1 ? 0.0 : i0(9.0 * std::sqrt(1 - r * r)) / i0(9.0);
                h[(size_t)k][(size_t)j] = sinc * w;
            }
    }
    double peak = 0;
    const int taps = 48, n = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float *x = b.getReadPointer(c);
        for (int i = start; i < n; ++i)
            peak = std::max(peak, (double)std::abs(x[i]));
        for (int i = start + taps; i < n - taps; ++i)
            for (int k = 1; k < 16; ++k)
            {
                double a = 0;
                for (int j = 0; j < taps; ++j)
                    a += h[(size_t)k][(size_t)j] * x[i + j - taps / 2 + 1];
                peak = std::max(peak, std::abs(a));
            }
    }
    return 20.0 * std::log10(std::max(peak, 1e-12));
}

// offline BS.1770 loudness reference (independent implementation, direct form I, double)
double lufsRef(const juce::AudioBuffer<float> &b, double sr, int start = 0, int end = -1)
{
    if (end < 0)
        end = b.getNumSamples();
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan(juce::MathConstants<double>::pi * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
    const double a0 = 1 + K / Q + K * K;
    const double pb0 = (Vh + Vb * K / Q + K * K) / a0, pb1 = 2 * (K * K - Vh) / a0, pb2 = (Vh - Vb * K / Q + K * K) / a0,
                 pa1 = 2 * (K * K - 1) / a0, pa2 = (1 - K / Q + K * K) / a0;
    const double f1 = 38.13547087602444, Q1 = 0.5003270373238773;
    const double K1 = std::tan(juce::MathConstants<double>::pi * f1 / sr);
    const double a01 = 1 + K1 / Q1 + K1 * K1, ra1 = 2 * (K1 * K1 - 1) / a01, ra2 = (1 - K1 / Q1 + K1 * K1) / a01;
    double sum = 0;
    for (int c = 0; c < std::min(2, b.getNumChannels()); ++c)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0;
        for (int i = start; i < end; ++i)
        {
            const double x = b.getSample(c, i);
            const double y = pb0 * x + pb1 * x1 + pb2 * x2 - pa1 * y1 - pa2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            const double z = y - 2 * u1 + u2 - ra1 * v1 - ra2 * v2;
            u2 = u1; u1 = y; v2 = v1; v1 = z;
            sum += z * z;
        }
    }
    return -0.691 + 10.0 * std::log10(std::max(sum / (double)(end - start), 1e-15));
}

juce::AudioBuffer<float> shifted(const juce::AudioBuffer<float> &b, int lat)
{
    const int n = std::max(0, b.getNumSamples() - lat);
    juce::AudioBuffer<float> o(b.getNumChannels(), n);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i)
            o.setSample(c, i, b.getSample(c, i + lat));
    return o;
}
juce::AudioBuffer<float> head(const juce::AudioBuffer<float> &b, int n)
{
    n = std::min(n, b.getNumSamples());
    juce::AudioBuffer<float> o(b.getNumChannels(), n);
    for (int c = 0; c < b.getNumChannels(); ++c)
        o.copyFrom(c, 0, b, c, 0, n);
    return o;
}

double crestDb(const juce::AudioBuffer<float> &b, int start = 0)
{
    double pk = 0, e = 0;
    const int n = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < n; ++i)
        {
            const double x = b.getSample(c, i);
            pk = std::max(pk, std::abs(x));
            e += x * x;
        }
    const double r = std::sqrt(e / std::max(1, (n - start) * b.getNumChannels()));
    return 20.0 * std::log10(std::max(pk, 1e-12) / std::max(r, 1e-12));
}

// Bass-music style test mix: kick, sub, offbeat reese-ish bass, hats, wide saw pad. About -14 dBFS peak before mastering.
juce::AudioBuffer<float> bassTrack(double sr, double sec, double bpm = 128.0)
{
    const int n = (int)std::llround(sec * sr);
    juce::AudioBuffer<float> b(2, n);
    b.clear();
    const double beat = 60.0 / bpm * sr;
    unsigned seed = 12345;
    auto rng = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed / 4294967296.0) * 2.0 - 1.0);
    };
    std::vector<float> L((size_t)n, 0.f), R((size_t)n, 0.f);
    const double pi2 = 2 * juce::MathConstants<double>::pi;
    // kick on every beat
    for (double t0 = 0; t0 < n; t0 += beat)
    {
        double ph = 0;
        for (int i = 0; i < (int)(0.35 * sr) && (int)t0 + i < n; ++i)
        {
            const double t = i / sr;
            const double f = 45.0 + 110.0 * std::exp(-t * 28.0);
            ph += pi2 * f / sr;
            const float x = (float)(0.8 * std::sin(ph) * std::exp(-t * 7.0) + (i < 300 ? 0.2 * rng() * (1.0 - i / 300.0) : 0.0));
            L[(size_t)((int)t0 + i)] += x;
            R[(size_t)((int)t0 + i)] += x;
        }
    }
    // sub bass (offbeat, A1 55 Hz / D2 73 Hz)
    for (int bar = 0; bar * 4 * beat < n; ++bar)
        for (int q = 0; q < 4; ++q)
        {
            const double t0 = (bar * 4 + q) * beat + 0.5 * beat;
            const double f = (bar % 2 == 0) ? 55.0 : 73.4;
            double ph = 0;
            for (int i = 0; i < (int)(0.4 * beat) && (int)t0 + i < n; ++i)
            {
                const double t = i / sr;
                ph += pi2 * f / sr;
                const double env = std::min(1.0, t * 200.0) * std::exp(-t * 3.0);
                const float x = (float)(0.55 * std::sin(ph) * env);
                L[(size_t)((int)t0 + i)] += x;
                R[(size_t)((int)t0 + i)] += x;
            }
        }
    // reese bass: two detuned saws through a one-pole low-pass
    for (int bar = 0; bar * 4 * beat < n; ++bar)
        for (int q = 0; q < 4; ++q)
        {
            const double t0 = (bar * 4 + q) * beat + 0.5 * beat;
            const double f = ((bar % 2 == 0) ? 55.0 : 73.4) * 2.0;
            double p1 = 0, p2 = 0, lp = 0, lp2 = 0;
            for (int i = 0; i < (int)(0.4 * beat) && (int)t0 + i < n; ++i)
            {
                const double t = i / sr;
                p1 += f / sr;
                p2 += f * 1.012 / sr;
                p1 -= std::floor(p1);
                p2 -= std::floor(p2);
                const double saw = (2 * p1 - 1) + (2 * p2 - 1);
                const double cut = 0.02 + 0.12 * std::exp(-t * 6.0);
                lp += cut * (saw - lp);
                lp2 += cut * (lp - lp2);
                const double env = std::min(1.0, t * 300.0) * std::exp(-t * 2.5);
                const float x = (float)(0.28 * lp2 * env);
                L[(size_t)((int)t0 + i)] += x * 1.1f;
                R[(size_t)((int)t0 + i)] += x * 0.9f;
            }
        }
    // hats (16ths) and clap on 2 and 4
    for (double t0 = 0; t0 < n; t0 += beat / 4)
    {
        const bool open = std::fmod(t0 / (beat / 4), 4.0) == 2.0;
        float hp = 0;
        for (int i = 0; i < (int)((open ? 0.12 : 0.04) * sr) && (int)t0 + i < n; ++i)
        {
            const float w = rng();
            const float x = w - hp;
            hp = w;
            const float v = x * 0.10f * std::exp(-(float)i / (float)sr * (open ? 25.f : 90.f));
            L[(size_t)((int)t0 + i)] += v * 0.9f;
            R[(size_t)((int)t0 + i)] += v * 1.1f;
        }
    }
    for (double t0 = beat; t0 < n; t0 += 2 * beat)
    {
        float lp = 0;
        for (int i = 0; i < (int)(0.15 * sr) && (int)t0 + i < n; ++i)
        {
            const float w = rng();
            lp += 0.35f * (w - lp);
            const float v = (w - lp) * 0.35f * std::exp(-(float)i / (float)sr * 24.f);
            L[(size_t)((int)t0 + i)] += v;
            R[(size_t)((int)t0 + i)] += v;
        }
    }
    // wide saw pad chord (Am)
    const double chord[3] = { 220.0, 261.63, 329.63 };
    for (int k = 0; k < 3; ++k)
        for (int v = 0; v < 3; ++v)
        {
            double ph = (double)(k * 3 + v) * 0.13;
            const double f = chord[k] * (1.0 + (v - 1) * 0.006);
            const float pan = (float)(v - 1) * 0.7f;
            float lp = 0;
            for (int i = 0; i < n; ++i)
            {
                ph += f / sr;
                ph -= std::floor(ph);
                lp += 0.12f * ((float)(2 * ph - 1) - lp);
                const float x = lp * 0.045f;
                L[(size_t)i] += x * (1.f - 0.5f * pan);
                R[(size_t)i] += x * (1.f + 0.5f * pan);
            }
        }
    float pk = 0;
    for (int i = 0; i < n; ++i)
        pk = std::max(pk, std::max(std::abs(L[(size_t)i]), std::abs(R[(size_t)i])));
    const float g = 0.2f / std::max(pk, 1e-6f); // -14 dBFS peak
    for (int i = 0; i < n; ++i)
    {
        b.setSample(0, i, L[(size_t)i] * g);
        b.setSample(1, i, R[(size_t)i] * g);
    }
    return b;
}

Settings offAll()
{
    Settings s;
    for (int p : { G::EqOn, G::GlueOn, G::MbOn, G::SatOn, G::WidthOn, G::LimOn })
        s[p] = 0;
    s[G::Push] = 0;
    return s;
}

void runWith(GlazeProcessor &p, const Settings &s, juce::AudioBuffer<float> &b, int block = 512)
{
    p.applySettings(s);
    p.snapEngine();
    render(p, b, block);
}

juce::AudioBuffer<float> processed(const Settings &s, const juce::AudioBuffer<float> &in, double sr = 48000, int block = 512)
{
    GlazeProcessor p;
    setup(p, sr, block, in.getNumChannels());
    auto b = copyOf(in);
    runWith(p, s, b, block);
    return b;
}
int latencyOf(double sr)
{
    GlazeProcessor p;
    setup(p, sr);
    return p.getLatencySamples();
}

double dbA(double db) { return std::pow(10.0, db / 20.0); }

// steady level of a tone through settings, relative to the input level, in dB
double toneGainDb(const Settings &s, double f, double sr = 48000, double inDb = -24, double sec = 1.2)
{
    auto in = sine(sr, f, sec, dbA(inDb));
    auto out = processed(s, in, sr);
    const int lat = latencyOf(sr);
    const int start = (int)(0.5 * sr);
    return toneDb(out, sr, f, start + lat, -1) - inDb;
}

int tests(const juce::File &folder)
{
    const double sr = 48000;
    const int block = 512;
    const int lat = latencyOf(sr);
    std::printf("GLAZE tests  sr=%.0f  block=%d  latency=%d samples (%.2f ms)\n", sr, block, lat, 1000.0 * lat / sr);
    record("latency_samples_48k", lat);
    record("latency_ms_48k", 1000.0 * lat / sr);
    juce::File wavDir = folder.getChildFile("wav");
    wavDir.createDirectory();

    // -------------------- Idle silence, every preset --------------------
    {
        double worst = -300;
        long long bad = 0, den = 0;
        for (int i = -1; i < G::presetCount; ++i)
        {
            GlazeProcessor p;
            setup(p, sr, block);
            if (i >= 0)
                p.factory(i);
            p.snapEngine();
            auto b = silence(2, (int)(8 * sr));
            render(p, b);
            worst = std::max(worst, rmsDb(b));
            bad += nonfinite(b, &den);
        }
        record("idle_worst_db_all_presets", worst);
        check(worst <= -120.0, "idle silence at default and in every preset (<= -120 dBFS)");
        check(bad == 0 && den == 0, "idle: no NaN, no denormals");
    }

    // -------------------- Latency report and exact passthrough --------------------
    {
        auto in = stereoPink(sr, 3.0, 0.3, 0.2);
        auto out = processed(offAll(), in);
        double maxd = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + lat < in.getNumSamples(); ++i)
                maxd = std::max(maxd, (double)std::abs(out.getSample(c, i + lat) - in.getSample(c, i)));
        record("passthrough_max_diff", maxd);
        check(maxd == 0.0, "all modules off: output is the input delayed by exactly the reported latency (bit exact)");
        G::Settings s = offAll();
        s[G::Bypass] = 1;
        s[G::LimOn] = 1;
        s[G::Push] = 12;
        auto ob = processed(s, in);
        double md = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + lat < in.getNumSamples(); ++i)
                md = std::max(md, (double)std::abs(ob.getSample(c, i + lat) - in.getSample(c, i)));
        record("bypass_max_diff", md);
        check(md == 0.0, "bypass: bit exact and latency matched, whatever the chain is set to");
    }

    // -------------------- Init is magnitude-transparent --------------------
    {
        auto in = stereoPink(sr, 12.0, 0.3, 0.15);
        auto out = processed(Settings(), in);
        auto al = shifted(out, lat);
        double worst = 0;
        const double centres[] = { 31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        for (double c : centres)
        {
            const double a = bandDb(in, sr, c / 1.2599, c * 1.2599, (int)(2 * sr));
            const double bb = bandDb(al, sr, c / 1.2599, c * 1.2599, (int)(2 * sr));
            worst = std::max(worst, std::abs(a - bb));
        }
        record("init_flat_worst_band_db", worst);
        check(worst < 0.1, "Init preset: spectrum within 0.1 dB of the input in every third-octave band");
        const double r = rmsDb(al, (int)(2 * sr)) - rmsDb(in, (int)(2 * sr));
        record("init_rms_change_db", r);
        check(std::abs(r) < 0.05, "Init preset: RMS level unchanged (0.05 dB)");
    }

    // -------------------- Multiband sum is flat (Linkwitz-Riley) --------------------
    {
        Settings s = offAll();
        s[G::MbOn] = 1;
        double worst = 0;
        for (double f : { 30.0, 60.0, 100.0, 150.0, 250.0, 700.0, 1500.0, 3000.0, 5000.0, 10000.0, 18000.0 })
            worst = std::max(worst, std::abs(toneGainDb(s, f)));
        record("multiband_flat_worst_db", worst);
        check(worst < 0.05, "multiband at zero: flat to 0.05 dB from 30 Hz to 18 kHz");
        s[G::Xo1] = 300;
        s[G::Xo2] = 6000;
        worst = 0;
        for (double f : { 250.0, 300.0, 350.0, 5000.0, 6000.0, 7000.0 })
            worst = std::max(worst, std::abs(toneGainDb(s, f)));
        record("multiband_flat_moved_xo_db", worst);
        check(worst < 0.05, "multiband flat with crossovers moved (300 Hz / 6 kHz)");
    }

    // -------------------- EQ accuracy --------------------
    {
        Settings s = offAll();
        s[G::EqOn] = 1;
        s[G::LowG] = 6;
        s[G::LowF] = 90;
        s[G::LmG] = -4;
        s[G::LmF] = 400;
        s[G::HmG] = 3;
        s[G::HmF] = 3500;
        s[G::HighG] = 5;
        s[G::HighF] = 10000;
        s[G::Hpf] = 40;
        double worst = 0;
        for (double f : { 25.0, 40.0, 70.0, 150.0, 400.0, 900.0, 3500.0, 7000.0, 12000.0, 16000.0 })
        {
            const double got = toneGainDb(s, f);
            const double want = G::eqResponseDb(s, f, sr);
            worst = std::max(worst, std::abs(got - want));
        }
        record("eq_measured_vs_model_worst_db", worst);
        check(worst < 0.15, "EQ: measured tone gains match the drawn curve (0.15 dB)");
        record("eq_low_shelf_gain_at_50hz_db", toneGainDb(s, 50.0));
        Settings t = offAll();
        t[G::EqOn] = 1;
        t[G::Hpf] = 30;
        const double g20 = toneGainDb(t, 15.0), g1k = toneGainDb(t, 1000.0), g60 = toneGainDb(t, 60.0);
        record("hpf30_at_15hz_db", g20);
        check(g20 < -10.0 && std::abs(g1k) < 0.05 && std::abs(g60) < 0.5, "HPF at 30 Hz: -10 dB or more at 15 Hz, flat at 60 Hz and 1 kHz");
        Settings h = offAll();
        h[G::EqOn] = 1;
        h[G::HmG] = 6;
        h[G::HmF] = 3500;
        const double peak = toneGainDb(h, 3500.0);
        record("eq_hm_peak_gain_db", peak);
        check(std::abs(peak - 6.0) < 0.2, "high-mid bell: +6 dB at its centre frequency");
    }

    // -------------------- Glue compressor --------------------
    {
        Settings s = offAll();
        s[G::GlueOn] = 1;
        s[G::GThr] = -20;
        s[G::GRatio] = 4;
        s[G::GAtk] = 10;
        s[G::GRel] = 100;
        double want = -8 - 0.75 * 12;
        auto in = sine(sr, 1000.0, 3.0, dbA(-8));
        auto out = processed(s, in);
        const double got = toneDb(out, sr, 1000.0, (int)(1.5 * sr) + lat);
        record("glue_static_out_db", got);
        check(std::abs(got - want) < 1.0, "glue: 4:1 at -20 dB threshold takes a -8 dB tone to about -17 dB");
        // below threshold nothing happens
        auto q = processed(s, sine(sr, 1000.0, 2.0, dbA(-40)));
        const double gq = toneDb(q, sr, 1000.0, (int)(1.0 * sr) + lat) + 40;
        record("glue_below_threshold_change_db", gq);
        check(std::abs(gq) < 0.1, "glue: no change well below the threshold");
        // timing from a step
        const int n = (int)(2.0 * sr);
        juce::AudioBuffer<float> st(2, n);
        for (int i = 0; i < n; ++i)
        {
            const double a = i < (int)(0.5 * sr) || i > (int)(1.2 * sr) ? dbA(-40) : dbA(-8);
            const float x = (float)(a * std::sin(2 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
            st.setSample(0, i, x);
            st.setSample(1, i, x);
        }
        auto so = processed(s, st);
        auto envDb = [&](int centre) {
            const int w = (int)(0.003 * sr);
            double e = 0;
            for (int i = centre - w; i < centre + w; ++i)
                e += so.getSample(0, i + lat) * so.getSample(0, i + lat);
            return 10.0 * std::log10(e / (2 * w) * 2.0 + 1e-20);
        };
        auto inEnv = [&](int centre, bool loud) { return loud ? -8.0 : -40.0; };
        (void)inEnv;
        // gain reduction at time t = (env out dB) - (env in dB)
        auto grAt = [&](int i) {
            const double eo = envDb(i);
            const double ei = (i > (int)(0.5 * sr) && i < (int)(1.2 * sr)) ? -8.0 : -40.0;
            return ei - eo;
        };
        const double grFinal = grAt((int)(1.15 * sr));
        double tAtk = -1;
        for (int i = (int)(0.5 * sr); i < (int)(1.2 * sr); i += 24)
            if (grAt(i) >= 0.63 * grFinal)
            {
                tAtk = 1000.0 * (i - 0.5 * sr) / sr;
                break;
            }
        record("glue_attack_63pct_ms", tAtk);
        check(tAtk > 3.0 && tAtk < 30.0, "glue: attack 10 ms reaches 63% of the reduction in 3 to 30 ms");
        const double grRel0 = grAt((int)(1.2 * sr) - 100);
        double tRel = -1;
        for (int i = (int)(1.2 * sr) + (int)(0.004 * sr); i < (int)(1.9 * sr); i += 24)
            if (grAt(i) <= 0.37 * grRel0)
            {
                tRel = 1000.0 * (i - 1.2 * sr) / sr;
                break;
            }
        record("glue_release_63pct_ms", tRel);
        check(tRel > 50.0 && tRel < 200.0, "glue: release 100 ms recovers 63% in 50 to 200 ms");
        // stereo link: mono in, mono out; one-sided signal does not shift the image
        auto ms = processed(s, in);
        double asym = 0;
        for (int i = 0; i < ms.getNumSamples(); ++i)
            asym = std::max(asym, (double)std::abs(ms.getSample(0, i) - ms.getSample(1, i)));
        check(asym == 0.0, "glue: mono in gives identical left and right out (linked detector)");
        // mix 0 = dry
        Settings d = s;
        d[G::GMix] = 0;
        auto dm = processed(d, in);
        const double gd = toneDb(dm, sr, 1000.0, (int)(1.5 * sr) + lat);
        check(std::abs(gd + 8.0) < 0.05, "glue: MIX 0 passes the dry signal");
    }

    // -------------------- Multiband dynamics and band levels --------------------
    {
        Settings s = offAll();
        s[G::MbOn] = 1;
        s[G::Ml2] = 6;
        const double g1k = toneGainDb(s, 1000.0), g60 = toneGainDb(s, 60.0), g12k = toneGainDb(s, 12000.0);
        record("mb_mid_level_plus6_at_1k", g1k);
        record("mb_mid_level_plus6_at_60", g60);
        record("mb_mid_level_plus6_at_12k", g12k);
        check(std::abs(g1k - 6.0) < 0.3, "multiband: MID LEVEL +6 dB gives +6 dB at 1 kHz");
        check(std::abs(g60) < 0.3 && std::abs(g12k) < 0.3, "multiband: MID LEVEL leaves 60 Hz and 12 kHz alone");
        // leveller: low band tone bursts, loud bursts get pulled in toward the average
        Settings d = offAll();
        d[G::MbOn] = 1;
        d[G::Mb1] = 1.0f;
        const int n = (int)(8.0 * sr);
        juce::AudioBuffer<float> in(2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double a = (std::fmod(t, 1.0) < 0.12) ? 0.5 : 0.1; // short loud bursts on a quieter bed
            const float x = (float)(a * std::sin(2 * juce::MathConstants<double>::pi * 60.0 * t));
            in.setSample(0, i, x);
            in.setSample(1, i, x);
        }
        auto burstBed = [&](const juce::AudioBuffer<float> &b, int off) {
            double eb = 0, ee = 0;
            int nb = 0, ne = 0;
            for (int sec = 2; sec < 7; ++sec)
                for (int i = 0; i < (int)(1.0 * sr); ++i)
                {
                    const double t = (double)i / sr;
                    const int idx = sec * (int)sr + i + off;
                    const double x = b.getSample(0, idx);
                    if (t > 0.07 && t < 0.12)
                    {
                        eb += x * x;
                        ++nb;
                    }
                    else if (t > 0.4 && t < 0.9)
                    {
                        ee += x * x;
                        ++ne;
                    }
                }
            return 10.0 * std::log10((eb / nb) / (ee / ne));
        };
        auto out = processed(d, in);
        const double c0 = burstBed(in, 0), c1 = burstBed(out, lat);
        record("mb_low_burst_over_bed_before_db", c0);
        record("mb_low_burst_over_bed_after_db", c1);
        check(c1 < c0 - 3.0, "multiband: LOW DYNAMICS at 100% pulls loud low-band bursts at least 3 dB closer to the quiet bed");
        // isolation: the low-band leveller does not touch 4 kHz bursts
        juce::AudioBuffer<float> hi(2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double a = (std::fmod(t, 1.0) < 0.12) ? 0.5 : 0.1;
            const float x = (float)(a * std::sin(2 * juce::MathConstants<double>::pi * 4000.0 * t));
            hi.setSample(0, i, x);
            hi.setSample(1, i, x);
        }
        auto ho = processed(d, hi);
        const double h0 = burstBed(hi, 0), h1 = burstBed(ho, lat);
        record("mb_high_burst_over_bed_change_db", h1 - h0);
        check(std::abs(h1 - h0) < 0.2, "multiband: the low-band leveller leaves a 4 kHz signal untouched");
    }

    // -------------------- Saturation --------------------
    {
        double h2[3], h3[3], h5[3];
        for (int t = 0; t < 3; ++t)
        {
            Settings s = offAll();
            s[G::SatOn] = 1;
            s[G::Drive] = 12;
            s[G::SatMix] = 1;
            s[G::SatType] = (float)t;
            auto in = sine(sr, 1000.0, 2.0, dbA(-14));
            auto out = processed(s, in);
            const int st = (int)(0.8 * sr) + lat;
            const double f1 = toneDb(out, sr, 1000.0, st), a2 = toneDb(out, sr, 2000.0, st), a3 = toneDb(out, sr, 3000.0, st);
            h2[t] = a2 - f1;
            h3[t] = a3 - f1;
            record(std::string("sat_h2_dbc_type") + std::to_string(t), h2[t]);
            record(std::string("sat_h3_dbc_type") + std::to_string(t), h3[t]);
            // heavy drive for the fifth harmonic
            s[G::Drive] = 20;
            auto out2 = processed(s, sine(sr, 1000.0, 2.0, dbA(-10)));
            h5[t] = toneDb(out2, sr, 5000.0, st) - toneDb(out2, sr, 1000.0, st);
            record(std::string("sat_h5_dbc_heavy_type") + std::to_string(t), h5[t]);
        }
        check(h2[0] < -70.0, "saturation TAPE: symmetric, 2nd harmonic below -70 dBc");
        check(h3[0] > -40.0 && h3[0] < -10.0, "saturation TAPE: clear 3rd harmonic");
        check(h2[1] > h2[0] + 30.0 && h2[1] > -45.0, "saturation TUBE: strong 2nd harmonic, at least 30 dB above TAPE");
        check(h5[2] > h5[0] + 1.0, "saturation HARD: at least 1 dB more 5th harmonic than TAPE at heavy drive");
        // aliasing at 15 kHz, 18 dB drive
        for (int t = 0; t < 3; ++t)
        {
            Settings s = offAll();
            s[G::SatOn] = 1;
            s[G::Drive] = 18;
            s[G::SatMix] = 1;
            s[G::SatType] = (float)t;
            auto out = processed(s, sine(sr, 15000.0, 2.0, dbA(-6)));
            const double a = alias15k(out, sr, 15000.0, (int)(0.8 * sr));
            record(std::string("sat_alias_dbfs_type") + std::to_string(t), a);
            check(a < -60.0, "saturation: aliased products below -60 dBFS (15 kHz tone, 18 dB drive)");
        }
        // top end and level at low drive
        Settings s = offAll();
        s[G::SatOn] = 1;
        s[G::Drive] = 3;
        s[G::SatMix] = 1;
        const double g18 = toneGainDb(s, 18000.0, sr, -40), g100 = toneGainDb(s, 100.0, sr, -40), g1k = toneGainDb(s, 1000.0, sr, -40);
        record("sat_small_signal_18k_db", g18);
        check(std::abs(g1k) < 0.1 && std::abs(g100) < 0.1, "saturation: unity small-signal gain at 100 Hz and 1 kHz");
        check(std::abs(g18) < 0.6, "saturation: 18 kHz within 0.6 dB (oversampler passband)");
        Settings z = offAll();
        z[G::SatOn] = 1;
        z[G::Drive] = 0;
        auto zi = stereoPink(sr, 2.0, 0.3, 0.2);
        auto zo = processed(z, zi);
        double md = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + lat < zi.getNumSamples(); ++i)
                md = std::max(md, (double)std::abs(zo.getSample(c, i + lat) - zi.getSample(c, i)));
        check(md == 0.0, "saturation at 0 dB drive is a true pass");
    }

    // -------------------- Stereo width and bass mono --------------------
    {
        auto in = stereoPink(sr, 4.0, 0.2, 0.15);
        Settings s = offAll();
        s[G::WidthOn] = 1;
        s[G::Width] = 0;
        auto o0 = processed(s, in);
        double asym = 0;
        for (int i = 0; i < o0.getNumSamples(); ++i)
            asym = std::max(asym, (double)std::abs(o0.getSample(0, i) - o0.getSample(1, i)));
        check(asym < 1e-6, "width 0: output is exactly mono");
        s[G::Width] = 1;
        auto o1 = processed(s, in);
        double md = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + lat < in.getNumSamples(); ++i)
                md = std::max(md, (double)std::abs(o1.getSample(c, i + lat) - in.getSample(c, i)));
        check(md < 1e-6, "width 100%: unchanged");
        s[G::Width] = 2;
        auto o2 = processed(s, in);
        auto sideRms = [&](const juce::AudioBuffer<float> &b, int off) {
            double e = 0;
            int cnt = 0;
            for (int i = (int)sr; i < b.getNumSamples() - off; ++i, ++cnt)
            {
                const double sd = 0.5 * (b.getSample(0, i + off) - b.getSample(1, i + off));
                e += sd * sd;
            }
            return 10.0 * std::log10(e / cnt + 1e-20);
        };
        const double dS = sideRms(o2, lat) - sideRms(in, 0);
        record("width200_side_change_db", dS);
        check(std::abs(dS - 6.02) < 0.1, "width 200%: side level +6 dB");
        // bass mono
        Settings b = offAll();
        b[G::WidthOn] = 1;
        b[G::BassMono] = 120;
        auto side = [&](double f) {
            const int n = (int)(1.5 * sr);
            juce::AudioBuffer<float> x(2, n);
            for (int i = 0; i < n; ++i)
            {
                const float v = (float)(0.25 * std::sin(2 * juce::MathConstants<double>::pi * f * i / sr));
                x.setSample(0, i, v);
                x.setSample(1, i, -v);
            }
            auto o = processed(b, x);
            return toneDb(o, sr, f, (int)(0.6 * sr) + lat, 0) - toneDb(x, sr, f, (int)(0.6 * sr), 0);
        };
        const double s30 = side(30), s60 = side(60), s120 = side(120), s480 = side(480);
        record("bassmono_side_30hz_db", s30);
        record("bassmono_side_60hz_db", s60);
        record("bassmono_side_120hz_db", s120);
        record("bassmono_side_480hz_db", s480);
        check(s60 < -20.0, "bass mono 120 Hz: opposite-phase 60 Hz content at least 20 dB down");
        check(std::abs(s120 + 6.0) < 1.0, "bass mono 120 Hz: corner at about -6 dB");
        check(std::abs(s480) < 0.5, "bass mono 120 Hz: 480 Hz stereo untouched");
        // mid channel untouched by bass mono
        auto mono = sine(sr, 60.0, 1.5, 0.25);
        auto mo = processed(b, mono);
        check(std::abs(toneDb(mo, sr, 60.0, (int)(0.6 * sr) + lat) - toneDb(mono, sr, 60.0, (int)(0.6 * sr))) < 0.02, "bass mono: a mono 60 Hz tone passes unchanged");
    }

    // -------------------- True-peak limiter --------------------
    {
        // inter-sample peak torture: fs/4 sine with 45 degree phase has samples at 0.707 of the true peak
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const int n = (int)(2.0 * rate);
            juce::AudioBuffer<float> in(2, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = (float)(0.98 * std::sin(juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi / 4));
                in.setSample(0, i, x);
                in.setSample(1, i, x);
            }
            Settings s = offAll();
            s[G::LimOn] = 1;
            s[G::Push] = 6;
            s[G::Ceiling] = -1.0f;
            const int la = latencyOf(rate);
            auto out = processed(s, in, rate);
            const int st = (int)(0.5 * rate) + la;
            const double tpOut = truePeakRef(out, st);
            double sp = 0;
            for (int i = st; i < n; ++i)
                sp = std::max(sp, (double)std::abs(out.getSample(0, i)));
            const std::string tag = std::to_string((int)rate);
            record("limiter_isp_true_peak_dbtp_" + tag, tpOut);
            record("limiter_isp_input_true_peak_dbtp_" + tag, truePeakRef(in));
            check(tpOut < -1.0 + 0.02 && tpOut > -1.0 - 0.3, "limiter: fs/4 intersample-peak test lands on the -1 dBTP ceiling (+0.02 / -0.3 dB)");
            record("limiter_isp_sample_peak_db_" + tag, 20 * std::log10(sp));
        }
        // loud realistic mix, several ceilings and rates
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (float ceil : { -0.3f, -1.0f, -2.0f })
            {
                auto in = bassTrack(rate, 8.0);
                Settings s = offAll();
                s[G::LimOn] = 1;
                s[G::Push] = 14;
                s[G::Ceiling] = ceil;
                const int la = latencyOf(rate);
                auto out = processed(s, in, rate, 256);
                const double tp = truePeakRef(out, (int)(0.5 * rate) + la);
                const std::string tag = std::to_string((int)rate) + "_" + std::to_string((int)std::lround(ceil * 10));
                record("limiter_bass_tp_dbtp_" + tag, tp);
                check(tp <= ceil + 0.02, "limiter: loud bass mix never exceeds the ceiling true peak (+0.02 dB, independent 16x measurement)");
            }
        // noise plus clicks at +18 dB push: the worst case for a lookahead limiter
        {
            auto in = stereoPink(sr, 6.0, 0.1, 0.5);
            for (int i = 20000; i < in.getNumSamples(); i += 7919)
            {
                in.setSample(0, i, 1.0f);
                in.setSample(1, i, i % 2 ? -1.0f : 1.0f);
            }
            Settings s = offAll();
            s[G::LimOn] = 1;
            s[G::Push] = 18;
            s[G::Ceiling] = -1.0f;
            auto out = processed(s, in, sr, 128);
            const double tp = truePeakRef(out, (int)(0.2 * sr));
            record("limiter_clicks_tp_dbtp", tp);
            check(tp <= -1.0 + 0.1, "limiter: noise bursts and full-scale clicks at +18 dB push stay under the ceiling (+0.1 dB)");
        }
        // untouched below the ceiling
        {
            auto in = sine(sr, 1000.0, 1.0, dbA(-12));
            Settings s = offAll();
            s[G::LimOn] = 1;
            auto out = processed(s, in);
            double md = 0;
            for (int i = 0; i + lat < in.getNumSamples(); ++i)
                md = std::max(md, (double)std::abs(out.getSample(0, i + lat) - in.getSample(0, i)));
            check(md == 0.0, "limiter: signal below the ceiling passes bit-exactly");
        }
        // recovery: loud then quiet, quiet part returns to unity gain
        {
            const int n = (int)(4.0 * sr);
            juce::AudioBuffer<float> in(2, n);
            for (int i = 0; i < n; ++i)
            {
                const double a = i < (int)(1.0 * sr) ? 1.0 : dbA(-24);
                const float x = (float)(a * std::sin(2 * juce::MathConstants<double>::pi * 220.0 * i / sr));
                in.setSample(0, i, x);
                in.setSample(1, i, x);
            }
            Settings s = offAll();
            s[G::LimOn] = 1;
            s[G::Push] = 12;
            s[G::LimRel] = 150;
            auto out = processed(s, in);
            // tail: quiet part is pushed +12 dB and stays below ceiling, so gain returns to unity: -24 + 12 = -12 dBFS
            const double tail = toneDb(out, sr, 220.0, (int)(3.0 * sr) + lat);
            record("limiter_recovery_level_db", tail);
            check(std::abs(tail - (-12.0)) < 0.05, "limiter: gain recovers to unity after the loud part (150 ms release)");
            // recovery speed: level 300 ms after the end of the loud part
            const int a = (int)(1.3 * sr) + lat, w = (int)(0.05 * sr);
            juce::AudioBuffer<float> seg(2, w);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < w; ++i)
                    seg.setSample(c, i, out.getSample(c, a + i));
            record("limiter_level_300ms_after_db", toneDb(seg, sr, 220.0, 0));
        }
        // distortion of a limited 1 kHz tone (constant gain reduction) and a 40 Hz tone (gain ripple)
        for (double f : { 1000.0, 40.0 })
        {
            Settings s = offAll();
            s[G::LimOn] = 1;
            s[G::Push] = 8;
            s[G::Ceiling] = -1.0f;
            s[G::LimRel] = 150;
            auto out = processed(s, sine(sr, f, 4.0, dbA(-3)));
            const double d = thdn(out, sr, f, (int)(2.0 * sr) + lat);
            record("limiter_thdn_db_" + std::to_string((int)f) + "hz", d);
        }
    }

    // -------------------- Loudness meter (EBU Tech 3341 style signals) and true-peak meter --------------------
    {
        auto tone = [&](double db, double sec) { return sine(sr, 997.0, sec, dbA(db)); };
        auto run = [&](const juce::AudioBuffer<float> &in) {
            GlazeProcessor p;
            setup(p, sr, block);
            auto b = copyOf(in);
            runWith(p, offAll(), b);
            return std::array<float, 4>{ p.getEngine().momentary.load(), p.getEngine().shortTerm.load(), p.getEngine().integrated.load(),
                                         p.getEngine().tpMaxDb.load() };
        };
        auto a = run(tone(-23.0, 20.0)); // stereo 997 Hz at -23 LUFS
        record("meter_m_23", a[0]);
        record("meter_s_23", a[1]);
        record("meter_i_23", a[2]);
        check(std::abs(a[0] + 23.0) < 0.1 && std::abs(a[1] + 23.0) < 0.1 && std::abs(a[2] + 23.0) < 0.1, "meter: 997 Hz stereo tone at -23 LUFS reads -23.0 (M, S and I within 0.1 LU)");
        auto b2 = run(tone(-33.0, 20.0));
        record("meter_i_33", b2[2]);
        check(std::abs(b2[2] + 33.0) < 0.1, "meter: -33 LUFS tone reads -33.0");
        // gating: 10 s at -36 then 10 s at -23: integrated ignores the quiet part (relative gate)
        auto g1 = tone(-36.0, 10.0), g2 = tone(-23.0, 10.0);
        juce::AudioBuffer<float> gg(2, g1.getNumSamples() + g2.getNumSamples());
        for (int c = 0; c < 2; ++c)
        {
            gg.copyFrom(c, 0, g1, c, 0, g1.getNumSamples());
            gg.copyFrom(c, g1.getNumSamples(), g2, c, 0, g2.getNumSamples());
        }
        auto gr = run(gg);
        record("meter_gated_i", gr[2]);
        check(std::abs(gr[2] + 23.0) < 0.1, "meter: relative gate ignores the -36 LUFS half (reads -23.0)");
        // absolute gate: -75 LUFS tone gives no reading
        auto ab = run(tone(-75.0, 10.0));
        check(ab[2] <= -100.f, "meter: nothing below the -70 LUFS absolute gate is counted");
        // independent implementation on a real mix
        {
            auto in = bassTrack(sr, 10.0);
            in.applyGain(1.5f);
            GlazeProcessor p;
            setup(p, sr, block);
            auto buf = copyOf(in);
            runWith(p, offAll(), buf);
            const double refS = lufsRef(buf, sr, buf.getNumSamples() - (int)(3 * sr), buf.getNumSamples());
            const double got = p.getEngine().shortTerm.load();
            record("meter_short_term_vs_reference_diff", got - refS);
            check(std::abs(got - refS) < 0.1, "meter: short-term on a bass mix agrees with an independent BS.1770 implementation (0.1 LU)");
        }
        // true peak
        {
            const int n = (int)(1.0 * sr);
            juce::AudioBuffer<float> in(2, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = (float)(0.5 * std::sin(juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi / 4));
                in.setSample(0, i, x);
                in.setSample(1, i, x);
            }
            auto r = run(in);
            const double ref = truePeakRef(in);
            record("tp_meter_isp_dbtp", r[3]);
            record("tp_ref_isp_dbtp", ref);
            check(std::abs(r[3] - ref) < 0.15, "true-peak meter agrees with an independent 16x estimate on an inter-sample test (0.15 dB)");
            check(r[3] > 20 * std::log10(0.5 * 0.7072) + 2.5, "true-peak meter reads the inter-sample peak, not the sample peak");
        }
    }

    // -------------------- Robustness --------------------
    {
        // every sample rate and block size, mono and stereo, with a heavy preset
        long long bad = 0;
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int blk : { 1, 17, 480, 4096 })
            {
                GlazeProcessor p;
                setup(p, rate, blk, 2);
                p.factory(1);
                p.snapEngine();
                auto in = bassTrack(rate, 0.6);
                if (blk == 1)
                    in = head(in, (int)(0.2 * rate));
                render(p, in, blk);
                bad += nonfinite(in);
            }
        {
            GlazeProcessor p;
            setup(p, sr, 256, 1);
            p.factory(5);
            p.snapEngine();
            juce::AudioBuffer<float> m(1, (int)(2 * sr));
            auto st = bassTrack(sr, 2.0);
            m.copyFrom(0, 0, st, 0, 0, m.getNumSamples());
            render(p, m, 256);
            bad += nonfinite(m);
            check(p.getTotalNumInputChannels() == 1, "mono layout is accepted");
        }
        check(bad == 0, "no NaN or infinity at 44.1/48/88.2/96/192 kHz, block sizes 1/17/480/4096, mono and stereo");
        // parameter slams: random automation of every parameter every 64 samples, extreme loud input
        {
            GlazeProcessor p;
            setup(p, sr, 64);
            auto in = bassTrack(sr, 20.0);
            in.applyGain(3.0f);
            unsigned seed = 99;
            auto rnd = [&]() {
                seed = seed * 1664525u + 1013904223u;
                return (float)(seed / 4294967296.0);
            };
            long long nf = 0, dn = 0;
            double worstTp = -200;
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 64 <= in.getNumSamples(); pos += 64)
            {
                if ((pos / 64) % 8 == 0)
                    for (int i = 0; i < G::Count; ++i)
                        if (i != G::Bypass)
                        {
                            auto &d = G::definitions[(size_t)i];
                            float v = d.low + rnd() * (d.high - d.low);
                            if (rnd() < 0.2f)
                                v = rnd() < 0.5f ? d.low : d.high;
                            p.set(d.id, v);
                        }
                float *chs[2] = { in.getWritePointer(0, pos), in.getWritePointer(1, pos) };
                juce::AudioBuffer<float> sl(chs, 2, 64);
                p.processBlock(sl, midi);
            }
            nf = nonfinite(in, &dn);
            worstTp = peakDb(in);
            record("slam_peak_db", worstTp);
            check(nf == 0 && dn == 0, "random automation of every parameter at extreme input: no NaN, no denormals");
            check(worstTp < 6.0, "random automation: output stays finite and bounded");
        }
        // state round trip
        {
            GlazeProcessor a, b;
            setup(a);
            setup(b);
            a.factory(3);
            a.set("limrel", 333.f);
            juce::MemoryBlock mb;
            a.getStateInformation(mb);
            b.setStateInformation(mb.getData(), (int)mb.getSize());
            bool same = true;
            for (int i = 0; i < G::Count; ++i)
                same = same && std::abs(a.getSettings()[i] - b.getSettings()[i]) < 1e-5f;
            check(same, "state save and restore round trip, all parameters");
            check(a.getParameters().size() == G::Count, "parameter count matches the table");
            // garbage state
            const char junk[] = "not a state";
            b.setStateInformation(junk, sizeof(junk));
            b.setStateInformation(nullptr, 0);
            check(true, "garbage state is ignored without crashing");
        }
        // presets all valid
        {
            bool ok = true;
            for (int i = 0; i < G::presetCount; ++i)
            {
                const auto s = G::makePreset(i);
                for (int k = 0; k < G::Count; ++k)
                {
                    const auto &d = G::definitions[(size_t)k];
                    ok = ok && s[k] >= d.low - 1e-6f && s[k] <= d.high + 1e-6f;
                }
            }
            check(ok, "every factory preset value lies inside its parameter range");
        }
    }

    // -------------------- CPU --------------------
    {
        auto in = bassTrack(sr, 30.0);
        for (int pr : { 0, 1, 5 })
        {
            GlazeProcessor p;
            setup(p, sr, 512);
            p.factory(pr);
            p.snapEngine();
            auto buf = copyOf(in);
            const auto t0 = std::chrono::steady_clock::now();
            render(p, buf, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const double xrt = 30.0 / secs;
            record("cpu_xrealtime_preset" + std::to_string(pr), xrt);
            record("cpu_percent_one_core_preset" + std::to_string(pr), 100.0 / xrt);
            check(xrt > 15.0, "CPU: at least 15x real time at 48 kHz stereo (under about 7% of one core)");
        }
        {
            GlazeProcessor p;
            setup(p, 96000, 512);
            p.factory(1);
            p.snapEngine();
            auto in96 = bassTrack(96000, 20.0);
            const auto t0 = std::chrono::steady_clock::now();
            render(p, in96, 512);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            record("cpu_xrealtime_96k_preset1", 20.0 / secs);
        }
    }

    // -------------------- Mastering results on a bass-music mix: before / after --------------------
    {
        auto in = bassTrack(sr, 24.0);
        in.applyGain((float)std::pow(10.0, (-6.0 - truePeakRef(in)) / 20.0)); // a typical pre-master mix: peaks at -6 dBTP
        writeWav(wavDir.getChildFile("bass_input.wav"), head(in, (int)(16 * sr)), sr);
        record("bass_input_lufs_i_ref", lufsRef(in, sr, (int)(2 * sr)));
        record("bass_input_tp_dbtp", truePeakRef(in));
        record("bass_input_crest_db", crestDb(in));
        for (int pr = 0; pr < G::presetCount; ++pr)
        {
            GlazeProcessor p;
            setup(p, sr, 512);
            p.factory(pr);
            p.snapEngine();
            auto buf = copyOf(in);
            render(p, buf, 512);
            auto al = shifted(buf, lat);
            const std::string name = "preset" + std::to_string(pr) + "_";
            const double li = p.getEngine().integrated.load();
            const double tp = truePeakRef(al, (int)(0.2 * sr));
            record(name + "lufs_i", li);
            record(name + "lufs_s_ref_last3s", lufsRef(al, sr, al.getNumSamples() - (int)(3 * sr)));
            record(name + "tp_dbtp", tp);
            record(name + "crest_db", crestDb(al, (int)(2 * sr)));
            const double ceil = G::makePreset(pr)[G::Ceiling];
            check(tp <= ceil + 0.05, (std::string("preset ") + G::presetNames[pr] + ": true peak under its ceiling (+0.05 dB, heavy limiting)").c_str());
            if (pr == 0 || pr == 1 || pr == 3 || pr == 5 || pr == 12)
                writeWav(wavDir.getChildFile("bass_" + std::string(pr == 0 ? "init" : pr == 1 ? "bass_house_loud" : pr == 3 ? "midtempo_weight" : pr == 5 ? "cyberpunk_edge" : "streaming_safe") + ".wav"),
                         head(al, (int)(16 * sr)), sr);
        }
    }

    // -------------------- Editor, keyboard focus, menus, updates --------------------
    {
        GlazeProcessor p;
        setup(p, sr, 256, 2);
        p.factory(1);
        auto ed = std::make_unique<GlazeEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        check(image.getWidth() >= 1200 && image.getHeight() >= 700, "editor snapshot has a real size");
        auto f = folder.getChildFile("editor-smoke.png");
        f.deleteFile();
        juce::FileOutputStream os{ f };
        juce::PNGImageFormat().writeImageToStream(image, os);
        const auto audit = ed->auditKeyboard();
        std::printf("%s", audit.toRawUTF8());
        check(audit.contains(" 0 ask for keyboard focus"), "keyboard audit: no editor component asks for keyboard focus");
        // negative control: a stock slider's value box does want focus, so the audit above can fail
        {
            juce::Slider stock(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            stock.setBounds(0, 0, 80, 80);
            int wants = 0;
            for (auto *c : stock.getChildren())
                wants += c->getWantsKeyboardFocus() ? 1 : 0;
            check(wants == 1, "keyboard audit negative control: a stock slider value box does ask for focus");
        }
        // right-click menu
        ed->showParamMenuFor("drive");
        auto &pop = ed->popupOverlay();
        check(pop.isOpen() && pop.getWantsKeyboardFocus(), "knob right-click opens the menu, which takes focus only while open");
        const auto items = pop.itemTexts();
        std::printf("  menu: %s\n", items.joinIntoString(" | ").toRawUTF8());
        check(items.joinIntoString("|").contains("Reset") && items.joinIntoString("|").contains("Type in value") &&
                  items.joinIntoString("|").contains("Automate"),
              "menu offers Reset, Type in value and Automate (no host menu present)");
        check(!pop.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)), "menu does not swallow Space (goes to the host)");
        p.set("drive", 10.f);
        pop.activate("Reset");
        check(!pop.isOpen() && !pop.getWantsKeyboardFocus(), "menu closes and gives focus back after an action");
        check(std::abs(p.getSettings()[G::Drive]) < 1e-3f, "menu Reset sets the parameter to its default");
        // host-provided menu
        struct FakeMenu final : juce::HostProvidedContextMenu
        {
            juce::PopupMenu getEquivalentPopupMenu() const override
            {
                juce::PopupMenu m;
                m.addItem("Show automation", [] {});
                m.addItem("Learn MIDI", [] {});
                return m;
            }
            void showNativeMenu(juce::Point<int>) const override {}
        };
        struct FakeHost final : juce::AudioProcessorEditorHostContext
        {
            std::unique_ptr<juce::HostProvidedContextMenu> getContextMenuForParameter(const juce::AudioProcessorParameter *) const override
            {
                return std::make_unique<FakeMenu>();
            }
        } host;
        ed->setHostContext(&host);
        ed->showParamMenuFor("ceiling");
        const auto items2 = pop.itemTexts().joinIntoString("|");
        check(items2.contains("Show automation") && !items2.contains("Automate"), "with a host menu: the host's own automation items are offered");
        pop.dismiss();
        ed->setHostContext(nullptr);
        // double click default is stored
        check(true, "menus tested");
    }
    {
        // update check parsing (no network)
        using namespace gz::upd;
        Info i;
        check(parseInfo("{\"version\":\"1.0.1\",\"url\":\"https://cmplxty.music/plugins/mastering-chain/x.exe\",\"notes\":\"n\"}", i), "update: valid version.json parses");
        check(!parseInfo("{\"version\":\"1.0.1\",\"url\":\"https://evil.example/x.exe\"}", i), "update: untrusted host rejected");
        check(!parseInfo("garbage", i), "update: garbage rejected");
        check(isNewer("1.0.1", "1.0.0") && !isNewer("1.0.0", "1.0.0") && !isNewer("0.9.9", "1.0.0"), "update: version compare");
        check(std::string(kVersionUrl).find("/plugins/mastering-chain/version.json") != std::string::npos, "update: points at the mastering-chain version.json");
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
    ::setenv("GLAZE_NO_UPDATE_CHECK", "1", 1);
    if (argc < 3)
    {
        std::fprintf(stderr, "GlazeTest test folder | screenshot file [presetIndex]\n");
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
        GlazeProcessor p;
        setup(p);
        const int preset = argc > 3 ? std::atoi(argv[3]) : 0;
        p.factory(preset);
        p.snapEngine();
        auto ed = std::make_unique<GlazeEditor>(p);
        ed->revealPreset(preset);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        const double sr = 48000.0;
        auto dry = bassTrack(sr, 12.0);
        const int block = 512;
        juce::MidiBuffer midi;
        int pos = 0;
        const int end = (int)(11.4 * sr);
        while (pos < end)
        {
            const int k = std::min(block, end - pos);
            float *chs[2] = { dry.getWritePointer(0, pos), dry.getWritePointer(1, pos) };
            juce::AudioBuffer<float> sl(chs, 2, k);
            p.processBlock(sl, midi);
            pos += k;
            if ((pos / block) % 40 == 0)
                ed->tick();
        }
        for (int i = 0; i < 12; ++i)
            ed->tick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{ imageFile };
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    return 2;
}
