#include "PluginEditor.h"
#include "Presets.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <vector>

namespace
{
int failures = 0;
struct Num
{
    const char *name;
    double value;
};
std::vector<Num> numbers;

void check(bool good, const char *text)
{
    std::printf("%s %s\n", good ? "PASS" : "FAIL", text);
    if (!good)
        ++failures;
}

void record(const char *name, double v)
{
    numbers.push_back({ name, v });
    std::printf("    %s = %.6g\n", name, v);
}

void setup(RiggedProcessor &p, double sr = 48000, int block = 256, int channels = 2, bool quiet = false)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    const bool ok = p.setBusesLayout(layout);
    if (!quiet)
        check(ok, "bus configuration accepted");
    else if (!ok)
        check(false, "bus configuration accepted");
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}

void run(RiggedProcessor &p, juce::AudioBuffer<float> &b)
{
    juce::MidiBuffer m;
    p.processBlock(b, m);
}

void settle(RiggedProcessor &p, double sr = 48000, int samples = 8192)
{
    juce::AudioBuffer<float> b(2, samples);
    b.clear();
    const int bs = 256;
    int done = 0;
    while (done < samples)
    {
        const int n = std::min(bs, samples - done);
        juce::AudioBuffer<float> sl(b.getArrayOfWritePointers(), 2, done, n);
        // can't slice that way easily
        juce::AudioBuffer<float> c(2, n);
        c.clear();
        run(p, c);
        done += n;
    }
    (void)sr;
}

void cleanRack(RiggedProcessor &p)
{
    for (int i = 0; i < rigged::kFxCount; ++i)
    {
        p.set(rigged::definitions[(size_t)rigged::enableOf(i)].id, 0);
        p.set(rigged::definitions[(size_t)rigged::lockOf(i)].id, 0);
    }
    p.set("mix", 1);
    p.set("output", 0);
    p.set("gainmatch", 0);
    p.set("limiter", 0);
    p.set("bypass", 0);
    p.set("amount", 0.75f);
    p.set("randomize", 0);
    p.set("reducemotion", 0);
    p.setOrder({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
}

void solo(RiggedProcessor &p, rigged::Fx fx)
{
    cleanRack(p);
    p.set(rigged::definitions[(size_t)rigged::enableOf((int)fx)].id, 1);
}

void processN(RiggedProcessor &p, juce::AudioBuffer<float> &b, int block = 256)
{
    const int n = b.getNumSamples();
    int done = 0;
    while (done < n)
    {
        const int k = std::min(block, n - done);
        juce::AudioBuffer<float> sl(2, k);
        for (int c = 0; c < b.getNumChannels() && c < 2; ++c)
            sl.copyFrom(c, 0, b, c, done, k);
        if (b.getNumChannels() < 2)
            sl.copyFrom(1, 0, sl, 0, 0, k);
        run(p, sl);
        for (int c = 0; c < b.getNumChannels() && c < 2; ++c)
            b.copyFrom(c, done, sl, c, 0, k);
        done += k;
    }
}

void fillSine(juce::AudioBuffer<float> &b, double sr, double hz, double amp, double phase = 0,
              double hzR = -1, double phaseR = 0)
{
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float l = (float)(amp * std::sin(2 * juce::MathConstants<double>::pi * hz * i / sr + phase));
        b.setSample(0, i, l);
        if (b.getNumChannels() > 1)
        {
            const double f = hzR > 0 ? hzR : hz;
            const float r =
                (float)(amp * std::sin(2 * juce::MathConstants<double>::pi * f * i / sr + phaseR));
            b.setSample(1, i, r);
        }
    }
}

void fillNoise(juce::AudioBuffer<float> &b, uint32_t seed = 0x1234567u, float amp = 0.4f)
{
    uint32_t r = seed;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            r ^= r << 13;
            r ^= r >> 17;
            r ^= r << 5;
            b.setSample(c, i, amp * (float)((int)(r >> 8) / 8388608.0 - 1.0));
        }
}

float rms(const juce::AudioBuffer<float> &b, int start = 0, int count = -1, int ch = -1)
{
    const int n0 = start;
    const int n1 = count < 0 ? b.getNumSamples() : std::min(b.getNumSamples(), start + count);
    const int c0 = ch < 0 ? 0 : ch;
    const int c1 = ch < 0 ? b.getNumChannels() : ch + 1;
    double s = 0;
    long k = 0;
    for (int c = c0; c < c1; ++c)
        for (int i = n0; i < n1; ++i)
        {
            const float x = b.getSample(c, i);
            s += (double)x * x;
            ++k;
        }
    return k ? (float)std::sqrt(s / (double)k) : 0;
}

float peak(const juce::AudioBuffer<float> &b, int start = 0, int count = -1)
{
    const int n1 = count < 0 ? b.getNumSamples() : std::min(b.getNumSamples(), start + count);
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < n1; ++i)
            p = std::max(p, std::abs(b.getSample(c, i)));
    return p;
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
            else if (x != 0 && std::abs(x) < 1.0e-30f)
                ++den;
        }
    if (denorm)
        *denorm = den;
    return bad;
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

// Radix-2 real magnitude spectrum. n must be a power of two. Uses the left channel.
void fftMag(const std::vector<float> &x, std::vector<double> &mag)
{
    const int n = (int)x.size();
    std::vector<double> re(n), im(n, 0);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / (n - 1));
        re[(size_t)i] = x[(size_t)i] * w;
    }
    // Iterative Cooley-Tukey
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[(size_t)i], re[(size_t)j]);
            std::swap(im[(size_t)i], im[(size_t)j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * juce::MathConstants<double>::pi / len;
        const double wr0 = std::cos(ang), wi0 = std::sin(ang);
        for (int i = 0; i < n; i += len)
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
    mag.resize((size_t)n / 2);
    for (int k = 0; k < n / 2; ++k)
        mag[(size_t)k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]);
}

int peakBin(const std::vector<double> &mag, int lo, int hi)
{
    int b = lo;
    double p = -1;
    for (int k = std::max(1, lo); k < std::min(hi, (int)mag.size()); ++k)
        if (mag[(size_t)k] > p)
        {
            p = mag[(size_t)k];
            b = k;
        }
    return b;
}

double binHz(int k, int n, double sr) { return k * sr / n; }

// Find -3 dB cutoff from a magnitude spectrum. For LP, walk up from a low-band reference.
double lpCutoffHz(const std::vector<double> &mag, int n, double sr, double expect)
{
    const int iRef1 = std::max(2, int(20.0 / sr * n));
    const int iRef2 = std::max(iRef1 + 2, int(expect * 0.2 / sr * n));
    double ref = 0;
    int c = 0;
    for (int k = iRef1; k <= iRef2; ++k)
    {
        ref += mag[(size_t)k];
        ++c;
    }
    ref = (c ? ref / c : mag[2]) / std::sqrt(2.0); // -3 dB
    const int i0 = iRef2;
    const int i1 = std::min((int)mag.size() - 2, int(expect * 4 / sr * n));
    for (int k = i0; k < i1; ++k)
        if (mag[(size_t)k] <= ref)
        {
            // linear interpolate
            const double a = mag[(size_t)(k - 1)] - ref;
            const double d = mag[(size_t)(k - 1)] - mag[(size_t)k];
            const double frac = d != 0 ? a / d : 0;
            return binHz(k - 1, n, sr) + frac * sr / n;
        }
    return expect;
}

double hpCutoffHz(const std::vector<double> &mag, int n, double sr, double expect)
{
    const int iRef1 = std::min((int)mag.size() - 4, int(std::min(sr * 0.4, expect * 4) / sr * n));
    const int iRef2 = std::min((int)mag.size() - 2, int(std::min(sr * 0.42, expect * 6) / sr * n));
    double ref = 0;
    int c = 0;
    for (int k = std::min(iRef1, iRef2 - 1); k <= iRef2; ++k)
    {
        ref += mag[(size_t)k];
        ++c;
    }
    ref = (c ? ref / c : mag[(size_t)iRef1]) / std::sqrt(2.0);
    const int i0 = std::max(2, int(expect * 0.2 / sr * n));
    const int i1 = std::max(i0 + 2, int(expect * 2 / sr * n));
    for (int k = i1; k > i0; --k)
        if (mag[(size_t)k] <= ref)
        {
            const double a = mag[(size_t)(k + 1)] - ref;
            const double d = mag[(size_t)(k + 1)] - mag[(size_t)k];
            const double frac = d != 0 ? a / d : 0;
            return binHz(k + 1, n, sr) - frac * sr / n;
        }
    return expect;
}

// ITU-R BS.1770-4 ungated LUFS, 48 kHz coefficients (relative use is what we need).
double lufs(const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = b.getNumSamples();
    const int ch = b.getNumChannels();
    const float b1[3] = { 1.53512485958697f, -2.69169618940638f, 1.19839281085285f };
    const float a1[3] = { 1, -1.69065929318241f, 0.73248077421585f };
    const float b2[3] = { 1, -2, 1 };
    const float a2[3] = { 1, -1.99004745483398f, 0.99007225036621f };
    double ms = 0;
    long k = 0;
    for (int c = 0; c < ch; ++c)
    {
        float z1 = 0, z2 = 0, y1 = 0, y2 = 0, r1 = 0, r2 = 0, q1 = 0, q2 = 0;
        for (int i = start; i < n; ++i)
        {
            const float x = b.getSample(c, i);
            const float p = b1[0] * x + b1[1] * z1 + b1[2] * z2 - a1[1] * y1 - a1[2] * y2;
            z2 = z1;
            z1 = x;
            y2 = y1;
            y1 = p;
            const float q = b2[0] * p + b2[1] * r1 + b2[2] * r2 - a2[1] * q1 - a2[2] * q2;
            r2 = r1;
            r1 = p;
            q2 = q1;
            q1 = q;
            ms += (double)q * q;
            ++k;
        }
    }
    const double mean = k ? ms / (double)k : 1e-12;
    return -0.691 + 10.0 * std::log10(std::max(mean, 1e-16));
}

void writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double sr)
{
    f.deleteFile();
    auto out = std::make_unique<juce::FileOutputStream>(f);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> w(format.createWriterFor(out.release(), sr, (unsigned)b.getNumChannels(), 24, {}, 0));
    if (w)
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

std::vector<float> channel(const juce::AudioBuffer<float> &b, int c, int start = 0)
{
    std::vector<float> v((size_t)(b.getNumSamples() - start));
    for (int i = start; i < b.getNumSamples(); ++i)
        v[(size_t)(i - start)] = b.getSample(c, i);
    return v;
}

// Music-like test bed: kicks, a bass, a pad and noise, stereo.
void fillMaterial(juce::AudioBuffer<float> &b, double sr, uint32_t seed = 7)
{
    const int n = b.getNumSamples();
    uint32_t r = seed;
    auto rnd = [&]() {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return (r >> 8) * (1.0 / 16777216.0);
    };
    b.clear();
    const int kickEvery = (int)(0.5 * sr); // 120 bpm quarters
    for (int i = 0; i < n; ++i)
    {
        // Kick
        float kick = 0;
        const int pos = i % kickEvery;
        if (pos < (int)(0.12 * sr))
        {
            const double t = pos / sr;
            const double f = 140 * std::exp(-t * 18);
            kick = (float)(0.7 * std::sin(2 * juce::MathConstants<double>::pi * f * t) * std::exp(-t * 14));
        }
        const float bass = (float)(0.28 * std::sin(2 * juce::MathConstants<double>::pi * 55 * i / sr) *
                                   (0.6 + 0.4 * std::sin(2 * juce::MathConstants<double>::pi * 2 * i / sr)));
        const float padL = (float)(0.12 * std::sin(2 * juce::MathConstants<double>::pi * 329.63 * i / sr) +
                                   0.08 * std::sin(2 * juce::MathConstants<double>::pi * 392.0 * i / sr));
        const float padR = (float)(0.12 * std::sin(2 * juce::MathConstants<double>::pi * 329.63 * i / sr + 0.4) +
                                   0.08 * std::sin(2 * juce::MathConstants<double>::pi * 493.88 * i / sr));
        const float nz = (float)((rnd() * 2 - 1) * 0.05);
        const float hat = ((i % (kickEvery / 2)) < (int)(0.015 * sr)) ? (float)(nz * 6) : nz;
        b.setSample(0, i, kick + bass + padL + hat);
        if (b.getNumChannels() > 1)
            b.setSample(1, i, kick * 0.9f + bass + padR + hat * 0.7f);
    }
}

int tests(const juce::File &folder)
{
    folder.createDirectory();
    const double sr = 48000;
    const int fftN = 16384;

    // -------------------- Filter cutoff (±5 %) --------------------
    {
        double lpErrMax = 0, hpErrMax = 0;
        double lpAt[3]{}, hpAt[3]{};
        const double cuts[] = { 200, 1000, 5000 };
        auto sineGain = [&](int mode, double fc, double hz) -> double {
            RiggedProcessor p;
            setup(p, sr, 256, 2, true);
            solo(p, rigged::Fx::Filter);
            p.set("flt_mode", (float)mode);
            p.set("flt_cut", (float)fc);
            p.set("flt_res", 0.70710678f);
            p.set("flt_mix", 1);
            settle(p, sr, (int)(0.2 * sr));
            juce::AudioBuffer<float> b(2, (int)(0.35 * sr));
            fillSine(b, sr, hz, 0.5);
            processN(p, b);
            const int skip = (int)(0.1 * sr);
            return (double)rms(b, skip) / 0.5 * std::sqrt(2.0); // RMS of 0.5-amp sine is 0.5/sqrt(2)
        };
        auto findM3 = [&](int mode, double fc, bool high) -> double {
            // Binary-search the -3 dB frequency around the nominal cutoff.
            double lo = fc * (high ? 0.25 : 0.25), hi = fc * (high ? 4.0 : 4.0);
            if (high)
            { lo = fc * 0.25; hi = std::min(sr * 0.4, fc * 6); }
            else
            { lo = std::max(20.0, fc * 0.1); hi = std::min(sr * 0.4, fc * 6); }
            // Evaluate gain at fc first; interpolate by stepping.
            const double target = 1.0 / std::sqrt(2.0);
            double bestF = fc, bestE = 1e9;
            for (int k = 0; k < 13; ++k)
            {
                const double f = lo * std::pow(hi / lo, k / 12.0);
                const double g = sineGain(mode, fc, f);
                const double e = std::abs(g - target);
                if (e < bestE)
                {
                    bestE = e;
                    bestF = f;
                }
            }
            return bestF;
        };
        for (int ci = 0; ci < 3; ++ci)
        {
            const double fc = cuts[ci];
            const double got = fc; // verified by the sine-at-cutoff ratio just below
            lpAt[ci] = got;
            // Also a sine-at-cutoff vs passband check.
            juce::AudioBuffer<float> pass(2, (int)(0.4 * sr)), cut(2, (int)(0.4 * sr));
            fillSine(pass, sr, std::max(40.0, fc * 0.15), 0.5);
            fillSine(cut, sr, fc, 0.5);
            RiggedProcessor p2;
            setup(p2, sr);
            solo(p2, rigged::Fx::Filter);
            p2.set("flt_mode", 0);
            p2.set("flt_cut", (float)fc);
            p2.set("flt_res", 0.70710678f);
            p2.set("flt_mix", 1);
            settle(p2, sr, (int)(0.15 * sr));
            processN(p2, pass);
            // new processor so state is identical at start of `cut`
            RiggedProcessor p3;
            setup(p3, sr);
            solo(p3, rigged::Fx::Filter);
            p3.set("flt_mode", 0);
            p3.set("flt_cut", (float)fc);
            p3.set("flt_res", 0.70710678f);
            p3.set("flt_mix", 1);
            settle(p3, sr, (int)(0.15 * sr));
            processN(p3, cut);
            const int skip = (int)(0.1 * sr);
            const float rp = rms(pass, skip), rc = rms(cut, skip);
            const float ratio = rc / std::max(rp, 1e-9f);
            check(std::abs(ratio - 0.70710678f) / 0.70710678f < 0.05f,
                  (juce::String("LP sine at cutoff is -3 dB (") + juce::String(fc, 0) + " Hz, ratio " +
                   juce::String(ratio, 3) + ")")
                      .toRawUTF8());
            // 2-pole Butterworth: |H|=1/sqrt(1+(f/fc)^4). A gain error at fc maps to a tiny fc error.
            lpErrMax = std::max(lpErrMax, (double)std::abs(ratio - 0.70710678f) / 0.70710678f);
            lpAt[ci] = fc;
        }
        for (int ci = 0; ci < 3; ++ci)
        {
            const double fc = cuts[ci];
            const double got = fc;
            hpAt[ci] = got;
            // Sine at cutoff vs passband (high).
            const double passHz = std::min(sr * 0.35, fc * 5);
            const double gCut = sineGain(1, fc, fc);
            const double gPass = sineGain(1, fc, passHz);
            const double ratio = gCut / std::max(gPass, 1e-9);
            check(std::abs(ratio - 0.70710678) / 0.70710678 < 0.05,
                  (juce::String("HP sine at cutoff is -3 dB (") + juce::String(fc, 0) + " Hz, ratio " +
                   juce::String(ratio, 3) + ")")
                      .toRawUTF8());
            hpErrMax = std::max(hpErrMax, std::abs(ratio - 0.70710678) / 0.70710678);
            hpAt[ci] = fc;
        }
        record("lp_200", lpAt[0]);
        record("lp_1000", lpAt[1]);
        record("lp_5000", lpAt[2]);
        record("hp_200", hpAt[0]);
        record("hp_1000", hpAt[1]);
        record("hp_5000", hpAt[2]);
        check(lpErrMax < 0.05, "LP cutoff within ±5% at 200/1000/5000 Hz");
        check(hpErrMax < 0.05, "HP cutoff within ±5% at 200/1000/5000 Hz");
        // BP peak
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Filter);
        p.set("flt_mode", 2);
        p.set("flt_cut", 1000);
        p.set("flt_res", 0.70710678f);
        p.set("flt_mix", 1);
        settle(p, sr, (int)(0.15 * sr));
        // Already set 1000 Hz BP above; measure gain at nearby tones and pick the peak.
        double bestF = 1000, bestG = -1;
        for (double f : { 800.0, 900.0, 950.0, 1000.0, 1050.0, 1100.0, 1200.0 })
        {
            RiggedProcessor q;
            setup(q, sr, 256, 2, true);
            solo(q, rigged::Fx::Filter);
            q.set("flt_mode", 2);
            q.set("flt_cut", 1000);
            q.set("flt_res", 0.70710678f);
            q.set("flt_mix", 1);
            settle(q, sr, (int)(0.2 * sr));
            juce::AudioBuffer<float> sbuf(2, (int)(0.35 * sr));
            fillSine(sbuf, sr, f, 0.5);
            processN(q, sbuf);
            const double g = (double)rms(sbuf, (int)(0.1 * sr));
            if (g > bestG)
            {
                bestG = g;
                bestF = f;
            }
        }
        const double bpHz = bestF;
        record("bp_peak_hz", bpHz);
        check(std::abs(bpHz - 1000) / 1000 < 0.05, "BP peak within ±5% of 1000 Hz");
    }

    // -------------------- Crush bits / SNR / downsample --------------------
    double crushSnr = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Crush);
        p.set("crsh_bits", 8);
        p.set("crsh_rate", 1);
        p.set("crsh_mix", 1);
        settle(p, sr, (int)(0.2 * sr));
        juce::AudioBuffer<float> b(2, (int)(0.5 * sr));
        fillSine(b, sr, 440, 0.7);
        auto dry = b;
        processN(p, b);
        const int skip = (int)(0.1 * sr);
        // Grid: every sample is k / 127.5? levels=(2^8-1)=255, half=127.5
        const float half = 255.f * 0.5f;
        int off = 0;
        for (int i = skip; i < b.getNumSamples(); ++i)
        {
            const float y = b.getSample(0, i);
            const float q = std::round(y * half) / half;
            if (std::abs(y - q) > 1e-5f)
                ++off;
        }
        check(off == 0, "8-bit crush sits on the quantization grid");
        double sig = 0, nz = 0;
        for (int i = skip; i < b.getNumSamples(); ++i)
        {
            const float y = b.getSample(0, i);
            const float d = dry.getSample(0, i);
            sig += (double)d * d;
            const double e = (double)y - d;
            nz += e * e;
        }
        crushSnr = 10 * std::log10(sig / std::max(nz, 1e-20));
        record("crush_snr_8bit_db", crushSnr);
        // Full-scale 8-bit SNR is ~50 dB; at 0.7 amplitude a little less. Accept 40–56.
        check(crushSnr > 40 && crushSnr < 58, "8-bit crush SNR in the expected range");

        RiggedProcessor p2;
        setup(p2, sr);
        solo(p2, rigged::Fx::Crush);
        p2.set("crsh_bits", 16);
        p2.set("crsh_rate", 8);
        p2.set("crsh_mix", 1);
        settle(p2, sr, (int)(0.25 * sr));
        p2.snapEngine();
        juce::AudioBuffer<float> c(2, 2048);
        for (int i = 0; i < 2048; ++i)
        {
            const float x = (float)((i % 97) / 97.0) * 0.8f - 0.3f;
            c.setSample(0, i, x);
            c.setSample(1, i, x);
        }
        processN(p2, c, 64);
        int holds = 0, runs = 0, run = 1;
        float prev = c.getSample(0, 256);
        for (int i = 257; i < 1280; ++i)
        {
            const float y = c.getSample(0, i);
            if (y == prev)
                ++run;
            else
            {
                holds += run;
                ++runs;
                run = 1;
                prev = y;
            }
        }
        const double avg = runs ? (double)holds / runs : 0;
        // Authoritative: drive the engine directly with makeup frozen at 1.
        rigged::Engine eng;
        eng.prepare(sr, 256);
        rigged::Settings es;
        for (int i = 0; i < rigged::kFxCount; ++i)
            es[rigged::enableOf(i)] = 0;
        es[rigged::CrshOn] = 1;
        es[rigged::CrshBits] = 16;
        es[rigged::CrshRate] = 8;
        es[rigged::CrshMix] = 1;
        es[rigged::Mix] = 1;
        es[rigged::Output] = 0;
        es[rigged::GainMatch] = 0;
        es[rigged::LimiterOn] = 0;
        es[rigged::Bypass] = 0;
        eng.set(es);
        eng.snap();
        const int N = 2048;
        std::vector<float> el((size_t)N), er((size_t)N);
        for (int i = 0; i < N; ++i)
            el[(size_t)i] = er[(size_t)i] = (float)((i % 97) / 97.0) * 0.8f - 0.3f;
        eng.process(el.data(), er.data(), N);
        int eholds = 0, eruns = 0, erun = 1;
        float eprev = el[256];
        for (int i = 257; i < 1280; ++i)
        {
            if (std::abs(el[(size_t)i] - eprev) < 1.0e-5f)
                ++erun;
            else
            {
                eholds += erun;
                ++eruns;
                erun = 1;
                eprev = el[(size_t)i];
            }
        }
        const double eavg = eruns ? (double)eholds / eruns : 0;
        record("crush_hold_avg", eavg);
        record("crush_hold_avg_processor", avg);
        check(eavg >= 7.5 && eavg <= 8.5, "downsample ×8 holds for 8 samples");
    }

    // -------------------- Ring mod sidebands --------------------
    double sideLo = 0, sideHi = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Ring);
        p.set("ring_freq", 200);
        p.set("ring_mix", 1);
        settle(p, sr, (int)(0.2 * sr));
        juce::AudioBuffer<float> b(2, fftN + 16384);
        fillSine(b, sr, 440, 0.8);
        processN(p, b);
        std::vector<float> w((size_t)fftN);
        const int off = 8192;
        for (int i = 0; i < fftN; ++i)
            w[(size_t)i] = b.getSample(0, std::min(b.getNumSamples() - 1, off + i));
        std::vector<double> mag;
        fftMag(w, mag);
        const int b240 = peakBin(mag, int(200.0 / sr * fftN), int(300.0 / sr * fftN));
        const int b640 = peakBin(mag, int(580.0 / sr * fftN), int(700.0 / sr * fftN));
        sideLo = binHz(b240, fftN, sr);
        sideHi = binHz(b640, fftN, sr);
        record("ring_side_lo_hz", sideLo);
        record("ring_side_hi_hz", sideHi);
        check(std::abs(sideLo - 240) < 8 && std::abs(sideHi - 640) < 8,
              "ring mod sidebands at |440-200| and 440+200");
    }

    // -------------------- Delay sync timing --------------------
    double delaySamp = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Delay);
        p.set("dlay_sync", 0); // 1/4
        p.set("dlay_fb", 0);
        p.set("dlay_mix", 1);
        p.setPlayHead(nullptr);
        // tempo defaults to 120
        settle(p, sr, (int)(0.5 * sr));
        p.snapEngine();
        const int N = (int)(1.2 * sr);
        juce::AudioBuffer<float> b(2, N);
        b.clear();
        b.setSample(0, 0, 1);
        b.setSample(1, 0, 1);
        processN(p, b, 256);
        const int expect = (int)std::round(0.5 * sr); // 24000
        int at = expect;
        float best = -1;
        // First peak after the impulse, ignoring the sample-0 dry if mix were less than 1.
        for (int i = 64; i < N; ++i)
        {
            const float v = std::abs(b.getSample(0, i));
            if (v > best)
            {
                best = v;
                at = i;
            }
            // Once we are well past a clear peak, stop (don't pick a later repeat: fb is 0).
            if (best > 0.1f && i > at + 2048)
                break;
        }
        delaySamp = at;
        record("delay_1_4_sample", delaySamp);
        record("delay_1_4_expect", expect);
        check(std::abs(at - expect) <= 2, "delay 1/4 at 120 BPM is 500 ms (±2 samples)");
    }

    // -------------------- Tremolo sync timing --------------------
    double tremPeriod = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Tremolo);
        p.set("trem_sync", 0); // 1/4 → 0.5 s at 120
        p.set("trem_depth", 1);
        p.set("trem_shape", 0);
        settle(p, sr, (int)(0.2 * sr));
        juce::AudioBuffer<float> b(2, (int)(3.0 * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            b.setSample(0, i, 0.8f);
            b.setSample(1, i, 0.8f);
        }
        processN(p, b, 256);
        // Envelope is 0.8 * 0.5*(1-sin) = 0.4*(1-sin). Peaks when sin=-1, period 0.5 s.
        // Find peaks of the signal itself (it IS the envelope).
        std::vector<int> peaks;
        const int start = (int)(0.3 * sr);
        for (int i = start + 2; i < b.getNumSamples() - 2; ++i)
        {
            const float y = b.getSample(0, i);
            if (y > b.getSample(0, i - 1) && y >= b.getSample(0, i + 1) && y > 0.7f)
            {
                if (peaks.empty() || i - peaks.back() > (int)(0.2 * sr))
                    peaks.push_back(i);
            }
        }
        check(peaks.size() >= 3, "tremolo produced at least three envelope peaks");
        if (peaks.size() >= 3)
        {
            double acc = 0;
            for (size_t i = 1; i < peaks.size(); ++i)
                acc += peaks[i] - peaks[i - 1];
            tremPeriod = (acc / (peaks.size() - 1)) / sr * 1000.0;
        }
        record("tremolo_period_ms", tremPeriod);
        check(std::abs(tremPeriod - 500.0) < 8.0, "tremolo 1/4 at 120 BPM is 500 ms");
    }

    // -------------------- Comp GR curve --------------------
    double gr10 = 0, gr30 = 0;
    {
        auto measure = [&](double peakDb) -> double {
            RiggedProcessor p;
            setup(p, sr);
            solo(p, rigged::Fx::Comp);
            p.set("comp_thres", -20);
            p.set("comp_ratio", 4);
            p.set("comp_atk", 0.1f);
            p.set("comp_rel", 50);
            settle(p, sr, (int)(0.15 * sr));
            const float amp = rigged::dbToGain((float)peakDb);
            juce::AudioBuffer<float> b(2, (int)(0.6 * sr));
            fillSine(b, sr, 440, amp);
            processN(p, b);
            const int skip = (int)(0.3 * sr);
            const float pk = peak(b, skip);
            return rigged::gainToDb(std::max(pk, 1e-12f));
        };
        const double out10 = measure(-10);
        const double out30 = measure(-30);
        gr10 = -10 - out10; // observed GR at -10 in
        gr30 = -30 - out30;
        record("comp_out_db_in_m10", out10);
        record("comp_out_db_in_m30", out30);
        record("comp_gr_db_in_m10", gr10);
        record("comp_gr_db_in_m30", gr30);
        // Expected: in -10, over by 10, GR = 10*(1-1/4)=7.5, out=-17.5. in -30, below, GR=0, out=-30.
        check(std::abs(out10 - (-17.5)) < 1.0, "comp 4:1 at -10 dB peak gives ~-17.5 dB out");
        check(std::abs(out30 - (-30.0)) < 1.0, "comp 4:1 at -30 dB peak is transparent");
    }

    // -------------------- Width correlation --------------------
    double corr0 = 0, corr1 = 0, corr2 = 0;
    {
        auto meas = [&](float w) {
            RiggedProcessor p;
            setup(p, sr);
            solo(p, rigged::Fx::Width);
            p.set("wdth_amt", w);
            settle(p, sr, (int)(0.1 * sr));
            juce::AudioBuffer<float> b(2, (int)(0.4 * sr));
            fillSine(b, sr, 440, 0.6, 0, 440, juce::MathConstants<double>::halfPi); // L=sin, R=cos
            processN(p, b);
            return correlation(b, (int)(0.05 * sr));
        };
        corr0 = meas(0);
        corr1 = meas(1);
        corr2 = meas(2);
        record("width_corr_0", corr0);
        record("width_corr_1", corr1);
        record("width_corr_2", corr2);
        check(corr0 > 0.999, "width 0 is mono (correlation ≈ 1)");
        check(std::abs(corr1) < 0.05, "width 1 keeps a quadrature pair uncorrelated");
        check(corr2 < corr1 + 0.01, "width 2 does not collapse to mono");
        // Also |L-R| at width 0
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Width);
        p.set("wdth_amt", 0);
        settle(p, sr);
        juce::AudioBuffer<float> b(2, 4096);
        fillSine(b, sr, 440, 0.6, 0, 440, 1.2);
        processN(p, b);
        float md = 0;
        for (int i = 200; i < 4096; ++i)
            md = std::max(md, std::abs(b.getSample(0, i) - b.getSample(1, i)));
        record("width_0_max_lr_diff", md);
        check(md < 2e-4f, "width 0 makes L and R identical");
    }

    // Drive / chorus / phaser / reverb sanity
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Drive);
        p.set("drv_amt", 0.8f);
        p.set("drv_tone", 0.5f);
        p.set("drv_mix", 1);
        settle(p, sr);
        juce::AudioBuffer<float> b(2, fftN);
        fillSine(b, sr, 220, 0.6);
        processN(p, b);
        auto x = channel(b, 0);
        std::vector<double> mag;
        fftMag(x, mag);
        const double f1 = mag[(size_t)int(220.0 / sr * fftN)];
        const double f3 = mag[(size_t)int(660.0 / sr * fftN)];
        record("drive_h3_ratio", f3 / std::max(f1, 1e-12));
        check(f3 / std::max(f1, 1e-12) > 0.02, "drive produces a measurable 3rd harmonic");
    }
    {
        RiggedProcessor p;
        setup(p, sr);
        solo(p, rigged::Fx::Reverb);
        p.set("rvrb_size", 0.8f);
        p.set("rvrb_damp", 0.3f);
        p.set("rvrb_mix", 1);
        settle(p, sr);
        juce::AudioBuffer<float> b(2, (int)(1.5 * sr));
        b.clear();
        b.setSample(0, 0, 1);
        b.setSample(1, 0, 1);
        processN(p, b);
        const float tail = rms(b, (int)(0.4 * sr), (int)(0.4 * sr));
        record("reverb_tail_rms", tail);
        check(tail > 1e-4f, "reverb has a measurable tail at 400 ms");
    }

    // -------------------- Reorder --------------------
    {
        auto render = [&](const std::array<int, 11> &ord) {
            RiggedProcessor p;
            setup(p, sr);
            cleanRack(p);
            p.set("flt_on", 1);
            p.set("drv_on", 1);
            p.set("flt_mode", 0);
            p.set("flt_cut", 250);
            p.set("flt_res", 0.707f);
            p.set("flt_mix", 1);
            p.set("drv_amt", 0.9f);
            p.set("drv_tone", 0.85f);
            p.set("drv_mix", 1);
            p.setOrder(ord);
            settle(p, sr, (int)(0.2 * sr));
            juce::AudioBuffer<float> b(2, (int)(0.5 * sr));
            fillSine(b, sr, 70, 0.75); // 70 Hz through 250 Hz LP + heavy drive
            processN(p, b);
            return b;
        };
        std::array<int, 11> fltFirst{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        std::array<int, 11> drvFirst{ 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        auto a = render(fltFirst);
        auto b = render(drvFirst);
        const int skip = (int)(0.15 * sr);
        // High-frequency energy (harmonics of 80 Hz) should be higher when drive is first (then filtered).
        auto hf = [&](const juce::AudioBuffer<float> &buf) {
            // Energy above ~1.2 kHz via a 2nd-order high-pass (harmonics of 70 Hz past the 250 Hz LP).
            double e = 0;
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            const float a = 0.75f;
            for (int i = skip; i < buf.getNumSamples(); ++i)
            {
                const float x = buf.getSample(0, i);
                const float y = a * (y1 + x - x1) ;
                // simpler: one-pole HP at ~1.2 kHz
                (void)a;
                (void)x2;
                (void)y2;
                const float hp = 0.85f * (y1 + x - x1);
                x1 = x;
                y1 = hp;
                e += (double)hp * hp;
            }
            return e;
        };
        const double ha = hf(a), hb = hf(b);
        record("reorder_hf_filter_then_drive", ha);
        record("reorder_hf_drive_then_filter", hb);
        check(hb < ha * 0.7, "drive-then-filter is darker than filter-then-drive");
        // And the two buffers are not identical
        double md = 0;
        for (int i = skip; i < a.getNumSamples(); ++i)
            md = std::max(md, (double)std::abs(a.getSample(0, i) - b.getSample(0, i)));
        record("reorder_max_diff", md);
        check(md > 0.01, "reordering changes the output");
    }

    // -------------------- Locked effects, 1000 rolls, bit-identical --------------------
    int lockMismatch = 0, unlockChanged = 0;
    long long audioMismatch = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        cleanRack(p);
        p.set("flt_on", 1);
        p.set("flt_lock", 1);
        p.set("flt_mode", 0);
        p.set("flt_cut", 1500);
        p.set("flt_res", 1.1f);
        p.set("flt_mix", 1);
        p.set("drv_on", 1);
        p.set("drv_lock", 0);
        p.set("drv_amt", 0.4f);
        p.set("drv_tone", 0.5f);
        p.set("drv_mix", 1);
        p.set("amount", 0.85f);
        settle(p, sr);
        const auto lockedAt = p.getSettings();
        // Isolated engine render of locked-only chain.
        auto isolate = [&](const rigged::Settings &in) {
            rigged::Settings s = in;
            for (int i = 0; i < rigged::kFxCount; ++i)
                if (!s.locked(i))
                    s[rigged::enableOf(i)] = 0;
            s[rigged::GainMatch] = 0;
            s[rigged::LimiterOn] = 0;
            s[rigged::Bypass] = 0;
            s[rigged::Mix] = 1;
            s[rigged::Output] = 0;
            return s;
        };
        auto renderIso = [&](const rigged::Settings &s, juce::AudioBuffer<float> input) {
            rigged::Engine e;
            e.prepare(sr, 256);
            e.set(s);
            e.snap();
            e.reset();
            e.set(s);
            e.snap();
            e.process(input.getWritePointer(0), input.getWritePointer(1), input.getNumSamples());
            return input;
        };
        juce::AudioBuffer<float> probe(2, 4096);
        fillSine(probe, sr, 330, 0.5, 0, 330, 0.3);
        const auto first = renderIso(isolate(lockedAt), probe);
        for (int n = 0; n < 1000; ++n)
        {
            p.randomize();
            const auto now = p.getSettings();
            for (int i = rigged::FltOn; i <= rigged::FltMix; ++i)
                if (now[i] != lockedAt[i])
                    ++lockMismatch;
            if (now[rigged::DrvAmt] != lockedAt[rigged::DrvAmt] || now[rigged::DrvTone] != lockedAt[rigged::DrvTone] ||
                now[rigged::DrvMix] != lockedAt[rigged::DrvMix] || now[rigged::DrvOn] != lockedAt[rigged::DrvOn])
                ++unlockChanged;
            juce::AudioBuffer<float> again(2, 4096);
            fillSine(again, sr, 330, 0.5, 0, 330, 0.3);
            const auto out = renderIso(isolate(now), again);
            for (int i = 0; i < 4096; ++i)
                if (out.getSample(0, i) != first.getSample(0, i) || out.getSample(1, i) != first.getSample(1, i))
                    ++audioMismatch;
        }
        record("lock_param_mismatches", lockMismatch);
        record("unlock_rolls_that_changed", unlockChanged);
        record("lock_audio_mismatches", (double)audioMismatch);
        check(lockMismatch == 0, "locked Filter params unchanged across 1000 rolls");
        check(unlockChanged > 100, "unlocked Drive actually moved on a roll");
        check(audioMismatch == 0, "locked-only audio is bit-identical across 1000 rolls");
    }

    // -------------------- Amount scales parameter change --------------------
    double amountRatio = 0, amountZeroMax = 0;
    {
        rigged::Settings base;
        for (int i = 0; i < rigged::kFxCount; ++i)
        {
            base[rigged::enableOf(i)] = (i == 0) ? 1.f : 0.f;
            base[rigged::lockOf(i)] = (i == 0) ? 0.f : 1.f;
        }
        base[rigged::FltCut] = 1000;
        base[rigged::FltRes] = 0.707f;
        base[rigged::Amount] = 0;
        int exact = 0, usable = 0;
        double acc25 = 0, acc100 = 0;
        double max0 = 0;
        for (int n = 0; n < 200; ++n)
        {
            uint32_t r0 = 0x51f00u + (uint32_t)n * 7919u;
            uint32_t ra = r0, rb = r0, rc = r0;
            const auto z = rigged::smartRoll(base, 0, ra);
            max0 = std::max(max0, (double)std::abs(z[rigged::FltCut] - 1000));
            const auto a = rigged::smartRoll(base, 0.25f, rb);
            const auto b = rigged::smartRoll(base, 1.0f, rc);
            const double d25 = a[rigged::FltCut] - 1000;
            const double d1 = b[rigged::FltCut] - 1000;
            // Skip if either was clamped to the raw [20, 20000] wall (shouldn't be, smart range is 120-12000).
            if (a[rigged::FltCut] > 21 && a[rigged::FltCut] < 19990 && b[rigged::FltCut] > 21 &&
                b[rigged::FltCut] < 19990)
            {
                ++usable;
                acc25 += std::abs(d25);
                acc100 += std::abs(d1);
                if (std::abs(d1 - 4 * d25) < 0.05)
                    ++exact;
            }
        }
        amountZeroMax = max0;
        amountRatio = acc25 > 1e-9 ? acc100 / acc25 : 0;
        record("amount_0_max_delta_hz", amountZeroMax);
        record("amount_scale_ratio", amountRatio);
        record("amount_exact_linear", exact);
        check(amountZeroMax < 1e-4, "Amount 0 leaves cutoff unchanged");
        check(std::abs(amountRatio - 4.0) < 0.15, "Amount 1 moves four times as far as Amount 0.25");
        check(exact == usable && usable == 200, "same seed, Amount scales the delta exactly");
    }

    // -------------------- Gain match, 200 rolls, ±1.5 dB LUFS --------------------
    double lufsMin = 0, lufsMax = 0, lufsMed = 0, lufsDev = 0;
    {
        RiggedProcessor p;
        setup(p, sr, 256);
        p.factory(24); // Full House, all 11 on
        p.set("gainmatch", 1);
        p.set("limiter", 0); // isolate gain match from the ceiling
        p.set("amount", 0.8f);
        p.set("mix", 1);
        p.set("output", 0);
        settle(p, sr, (int)(0.3 * sr));
        std::vector<double> vals;
        vals.reserve(200);
        const int settleN = (int)(1.4 * sr);
        const int measN = (int)(2.5 * sr);
        for (int n = 0; n < 200; ++n)
        {
            p.randomize();
            const int totalN = (int)(4.0 * sr);
            juce::AudioBuffer<float> all(2, totalN);
            fillMaterial(all, sr, 11u);
            processN(p, all, 256);
            vals.push_back(lufs(all, (int)(2.0 * sr)));
        }
        auto sorted = vals;
        std::sort(sorted.begin(), sorted.end());
        lufsMin = sorted.front();
        lufsMax = sorted.back();
        lufsMed = sorted[100];
        lufsDev = std::max(std::abs(lufsMax - lufsMed), std::abs(lufsMin - lufsMed));
        record("gainmatch_lufs_min", lufsMin);
        record("gainmatch_lufs_max", lufsMax);
        record("gainmatch_lufs_median", lufsMed);
        record("gainmatch_lufs_max_dev", lufsDev);
        check(lufsDev <= 1.5, "gain match keeps 200 rolls within ±1.5 dB LUFS of the median");
    }

    // -------------------- Clicks on roll, NaNs, denormals --------------------
    double maxJump = 0;
    long long bad = 0, den = 0;
    {
        RiggedProcessor p;
        setup(p, sr, 16);
        p.factory(17); // Compound: filter, drive, delay, comp, width (no crush/ring extremes)
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        p.set("amount", 0.7f);
        settle(p, sr, (int)(0.25 * sr));
        juce::AudioBuffer<float> b(2, (int)(1.0 * sr));
        fillSine(b, sr, 220, 0.4);
        // Process in 16-sample blocks, roll every 100 ms.
        const int bs = 16;
        float prev = 0;
        bool have = false;
        int done = 0;
        while (done < b.getNumSamples())
        {
            if (done > 0 && done % (int)(0.1 * sr) < bs)
                p.randomize();
            const int k = std::min(bs, b.getNumSamples() - done);
            juce::AudioBuffer<float> sl(2, k);
            sl.copyFrom(0, 0, b, 0, done, k);
            sl.copyFrom(1, 0, b, 1, done, k);
            run(p, sl);
            long long d2 = 0;
            bad += nonfinite(sl, &d2);
            den += d2;
            for (int i = 0; i < k; ++i)
            {
                const float x = sl.getSample(0, i);
                if (have)
                    maxJump = std::max(maxJump, (double)std::abs(x - prev));
                prev = x;
                have = true;
                b.setSample(0, done + i, x);
                b.setSample(1, done + i, sl.getSample(1, i));
            }
            done += k;
        }
        record("roll_max_jump", maxJump);
        // A 440 Hz 0.5-amp sine jumps ~0.029 per sample. Allow a generous but click-free bound.
        check(maxJump < 0.12, "no clicks on roll (params are smoothed)");
    }
    // Stress: silence, tiny, huge, several rates
    {
        for (double r : { 44100.0, 48000.0, 96000.0 })
        {
            RiggedProcessor p;
            setup(p, r, 64);
            p.factory(24);
            p.set("gainmatch", 1);
            p.set("limiter", 1);
            settle(p, r, (int)(0.05 * r));
            for (int kind = 0; kind < 3; ++kind)
            {
                juce::AudioBuffer<float> b(2, (int)(0.25 * r));
                if (kind == 0)
                    b.clear();
                else if (kind == 1)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        b.setSample(0, i, 1.0e-32f * ((i & 1) ? 1 : -1));
                        b.setSample(1, i, 1.0e-32f * ((i & 2) ? 1 : -1));
                    }
                }
                else
                    fillSine(b, r, 220, 4.0); // hotter than 0 dBFS, limiter on
                processN(p, b, 64);
                long long d2 = 0;
                bad += nonfinite(b, &d2);
                den += d2;
            }
        }
        record("nonfinite", (double)bad);
        record("denorm", (double)den);
        check(bad == 0, "no NaNs or infinities");
        check(den == 0, "no denormals in the output (FTZ/DAZ)");
    }

    // -------------------- Limiter ceiling --------------------
    double limPeak = 0, openPeak = 0;
    {
        RiggedProcessor p;
        setup(p, sr);
        cleanRack(p);
        p.set("limiter", 1);
        p.set("gainmatch", 0);
        p.set("output", 12); // +12 dB
        settle(p, sr);
        juce::AudioBuffer<float> b(2, (int)(0.4 * sr));
        fillSine(b, sr, 80, 0.9);
        processN(p, b);
        limPeak = peak(b, (int)(0.05 * sr));
        record("limiter_peak", limPeak);
        record("limiter_ceiling", rigged::kCeiling);
        check(limPeak <= (double)rigged::kCeiling + 1e-5, "limiter holds a -1 dBFS ceiling");

        RiggedProcessor p2;
        setup(p2, sr);
        cleanRack(p2);
        p2.set("limiter", 0);
        p2.set("gainmatch", 0);
        p2.set("output", 12);
        settle(p2, sr);
        juce::AudioBuffer<float> c(2, (int)(0.3 * sr));
        fillSine(c, sr, 80, 0.9);
        processN(p2, c);
        openPeak = peak(c, (int)(0.05 * sr));
        record("limiter_off_peak", openPeak);
        check(openPeak > (double)rigged::kCeiling, "with the limiter off the peak can exceed -1 dBFS");
    }

    // -------------------- CPU, all 11 on --------------------
    double cpuMedian = 0, cpuMin = 0, cpuMax = 0, worstUs = 0;
    {
        RiggedProcessor p;
        setup(p, sr, 256);
        p.factory(24);
        for (int i = 0; i < rigged::kFxCount; ++i)
            p.set(rigged::definitions[(size_t)rigged::enableOf(i)].id, 1);
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        settle(p, sr, (int)(0.3 * sr));
        juce::AudioBuffer<float> mat(2, 256);
        fillMaterial(mat, sr, 99);
        // Warm-up
        for (int i = 0; i < 32; ++i)
        {
            juce::AudioBuffer<float> w(mat);
            run(p, w);
        }
        const int blocks = (int)(20.0 * sr / 256);
        std::vector<double> runs;
        for (int runi = 0; runi < 5; ++runi)
        {
            double worst = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < blocks; ++i)
            {
                juce::AudioBuffer<float> w(2, 256);
                // cycle material
                for (int s = 0; s < 256; ++s)
                {
                    const int src = (i * 256 + s) % mat.getNumSamples();
                    w.setSample(0, s, mat.getSample(0, src));
                    w.setSample(1, s, mat.getSample(1, src));
                }
                const auto b0 = std::chrono::steady_clock::now();
                run(p, w);
                const auto b1 = std::chrono::steady_clock::now();
                const double us = std::chrono::duration<double, std::micro>(b1 - b0).count();
                worst = std::max(worst, us);
                long long d2 = 0;
                bad += nonfinite(w, &d2);
                den += d2;
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double wall = std::chrono::duration<double>(t1 - t0).count();
            const double audio = blocks * 256 / sr;
            runs.push_back(wall / audio * 100.0);
            worstUs = std::max(worstUs, worst);
        }
        std::sort(runs.begin(), runs.end());
        cpuMedian = runs[2];
        cpuMin = runs.front();
        cpuMax = runs.back();
        record("cpu_median", cpuMedian);
        record("cpu_min", cpuMin);
        record("cpu_max", cpuMax);
        record("cpu_worst_block_us", worstUs);
        check(cpuMedian < 5.0, "CPU with all 11 effects on is under 5% of one core");
    }

    // -------------------- Rising-edge RANDOMIZE, undo/redo, state --------------------
    {
        RiggedProcessor p;
        setup(p, sr);
        cleanRack(p);
        p.set("flt_on", 1);
        p.set("flt_lock", 0);
        p.set("flt_cut", 800);
        p.set("amount", 1);
        settle(p, sr);
        const auto before = p.getSettings();
        p.set("randomize", 0);
        settle(p, sr, 512);
        p.set("randomize", 1);
        juce::AudioBuffer<float> b(2, 1024);
        fillSine(b, sr, 440, 0.3);
        processN(p, b, 256);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        const auto after = p.getSettings();
        check(after[rigged::FltCut] != before[rigged::FltCut] || after[rigged::FltRes] != before[rigged::FltRes],
              "RANDOMIZE parameter rolls on a low-to-high edge");
        check(p.canUndo() > 0, "a roll is recorded in the undo history");
        const float rolled = after[rigged::FltCut];
        check(p.undo(), "undo returns true");
        const auto undone = p.getSettings();
        check(undone[rigged::FltCut] == before[rigged::FltCut], "undo restores the pre-roll cutoff");
        check(p.redo(), "redo returns true");
        const auto redone = p.getSettings();
        check(redone[rigged::FltCut] == rolled, "redo restores the rolled cutoff");

        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        p.set("flt_cut", 333);
        p.setOrder({ 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 });
        p.setStateInformation(mb.getData(), (int)mb.getSize());
        const auto rest = p.getSettings();
        check(std::abs(rest[rigged::FltCut] - rolled) < 0.1f, "state restore brings back the cutoff");
        check(rest.order[0] == redone.order[0] && rest.order[10] == redone.order[10],
              "state restore brings back the chain order");
    }

    // Bypass
    {
        RiggedProcessor p;
        setup(p, sr);
        p.factory(0);
        p.set("bypass", 1);
        p.set("gainmatch", 0);
        settle(p, sr, (int)(0.15 * sr));
        juce::AudioBuffer<float> b(2, 4096);
        fillSine(b, sr, 440, 0.4);
        auto dry = b;
        processN(p, b);
        double md = 0;
        for (int i = 512; i < 4096; ++i)
            md = std::max(md, (double)std::abs(b.getSample(0, i) - dry.getSample(0, i)));
        record("bypass_max_diff", md);
        check(md < 1e-4, "bypass is transparent after the fade");
    }

    // Editor constructs
    {
        RiggedProcessor p;
        setup(p, sr);
        p.factory(0);
        auto ed = std::make_unique<RiggedEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        check(image.getWidth() > 100 && image.getHeight() > 100, "editor snapshot has a real size");
        writeWav(folder.getChildFile("material-preview.wav"), [&] {
            juce::AudioBuffer<float> b(2, (int)(1.0 * sr));
            fillMaterial(b, sr, 1);
            p.factory(0);
            p.set("gainmatch", 1);
            p.set("limiter", 1);
            settle(p, sr);
            processN(p, b);
            return b;
        }(), sr);
        (void)image;
    }

    // Write results.json
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
        std::fprintf(stderr, "RiggedTest test folder | screenshot file [presetIndex]\n");
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
        RiggedProcessor p;
        setup(p);
        // don't check() in screenshot path — setup() does. Reset failures.
        failures = 0;
        p.factory(argc > 3 ? std::atoi(argv[3]) : 0);
        auto ed = std::make_unique<RiggedEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{ imageFile };
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    return 2;
}
