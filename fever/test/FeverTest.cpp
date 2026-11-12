#include "PluginEditor.h"
#include "Presets.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

namespace
{
int failures = 0;
struct Num
{
    std::string name;
    double value;
};
std::vector<Num> numbers;

void check(bool good, const char *text)
{
    std::printf("%s %s\n", good ? "PASS" : "FAIL", text);
    if (!good)
        ++failures;
}

void record(const std::string &name, double v)
{
    numbers.push_back({ name, v });
    std::printf("    %s = %.6g\n", name.c_str(), v);
}

void setup(FeverProcessor &p, double sr = 48000, int block = 256, int channels = 2, bool quiet = false)
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

void run(FeverProcessor &p, juce::AudioBuffer<float> &b)
{
    juce::MidiBuffer m;
    p.processBlock(b, m);
}

void processN(FeverProcessor &p, juce::AudioBuffer<float> &b, int block = 256)
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

void defaults(FeverProcessor &p)
{
    p.factory(0);
    p.set("fever", 0.5f);
    p.set("sidea", 0.5f);
    p.set("sideb", 0.5f);
    p.set("mix", 1);
    p.set("output", 0);
    p.set("gainmatch", 0);
    p.set("limiter", 0);
    p.set("bypass", 0);
    p.set("heartbeat", 0);
    p.set("heartrate", 0);
    p.set("heartdepth", 0.35f);
    p.set("reducemotion", 0);
}

void fillSine(juce::AudioBuffer<float> &b, double sr, double hz, double amp, double phase = 0)
{
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float s = (float)(amp * std::sin(2 * juce::MathConstants<double>::pi * hz * i / sr + phase));
        b.setSample(0, i, s);
        if (b.getNumChannels() > 1)
            b.setSample(1, i, s);
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

void fillMaterial(juce::AudioBuffer<float> &b, double sr, uint32_t seed = 1)
{
    uint32_t r = seed ? seed : 1u;
    const int n = b.getNumSamples();
    const double hz[] = { 110.0, 165.0, 220.0, 330.0, 440.0, 660.0, 880.0, 1320.0 };
    for (int i = 0; i < n; ++i)
    {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        const double t = i / sr;
        const double env = 0.7 + 0.3 * std::sin(2 * juce::MathConstants<double>::pi * 1.7 * t);
        double l = 0, rr = 0;
        for (int k = 0; k < 8; ++k)
        {
            const double a = 0.12 * env / (1.0 + 0.15 * k);
            l += a * std::sin(2 * juce::MathConstants<double>::pi * hz[k] * t);
            rr += a * std::sin(2 * juce::MathConstants<double>::pi * hz[k] * t * 1.001 + 0.2 * k);
        }
        const double nz = ((int)(r >> 8) / 8388608.0 - 1.0) * 0.04 * env;
        b.setSample(0, i, (float)(l + nz));
        if (b.getNumChannels() > 1)
            b.setSample(1, i, (float)(rr + nz * 0.7));
    }
}

float rms(const juce::AudioBuffer<float> &b, int start = 0, int count = -1, int ch = -1)
{
    const int n1 = count < 0 ? b.getNumSamples() : std::min(b.getNumSamples(), start + count);
    double s = 0;
    long k = 0;
    const int c0 = ch < 0 ? 0 : ch;
    const int c1 = ch < 0 ? b.getNumChannels() : ch + 1;
    for (int c = c0; c < c1; ++c)
        for (int i = start; i < n1; ++i)
        {
            const double x = b.getSample(c, i);
            s += x * x;
            ++k;
        }
    return k ? (float)std::sqrt(s / (double)k) : 0;
}

double residualDb(const juce::AudioBuffer<float> &a, const juce::AudioBuffer<float> &b, int start = 0)
{
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    double s = 0, d = 0;
    long k = 0;
    for (int c = 0; c < std::min(a.getNumChannels(), b.getNumChannels()); ++c)
        for (int i = start; i < n; ++i)
        {
            const double x = a.getSample(c, i), y = b.getSample(c, i);
            s += x * x;
            d += (y - x) * (y - x);
            ++k;
        }
    if (s <= 1e-24 || k == 0)
        return d <= 1e-24 ? -200 : 0;
    return 10.0 * std::log10(d / s);
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

void fftMag(const std::vector<float> &x, std::vector<double> &mag)
{
    const int n = (int)x.size();
    std::vector<double> re(n), im(n, 0);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / std::max(n - 1, 1));
        re[(size_t)i] = x[(size_t)i] * w;
    }
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

double interpHz(const std::vector<double> &mag, int k, int n, double sr)
{
    if (k <= 0 || k >= (int)mag.size() - 1)
        return k * sr / n;
    const double a = mag[(size_t)(k - 1)], b = mag[(size_t)k], c = mag[(size_t)(k + 1)];
    const double den = (a - 2 * b + c);
    const double d = std::abs(den) > 1e-20 ? 0.5 * (a - c) / den : 0;
    return (k + d) * sr / n;
}

int peakBin(const std::vector<double> &mag, int lo, int hi)
{
    int b = std::max(1, lo);
    double p = -1;
    for (int k = std::max(1, lo); k < std::min(hi, (int)mag.size()); ++k)
        if (mag[(size_t)k] > p)
        {
            p = mag[(size_t)k];
            b = k;
        }
    return b;
}

double centsErr(double hz, double expect) { return 1200.0 * std::log2(std::max(hz, 1e-9) / expect); }

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
    std::unique_ptr<juce::AudioFormatWriter> w(
        format.createWriterFor(out.release(), sr, (unsigned)b.getNumChannels(), 24, {}, 0));
    if (w)
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

std::vector<float> channel(const juce::AudioBuffer<float> &b, int c, int start = 0, int count = -1)
{
    const int n1 = count < 0 ? b.getNumSamples() : std::min(b.getNumSamples(), start + count);
    std::vector<float> v((size_t)std::max(0, n1 - start));
    for (int i = start; i < n1; ++i)
        v[(size_t)(i - start)] = b.getSample(c, i);
    return v;
}

double peakNear(const std::vector<double> &mag, int n, double sr, double expect, double windowCents, double *cents)
{
    const double loHz = expect * std::pow(2.0, -windowCents / 1200.0);
    const double hiHz = expect * std::pow(2.0, windowCents / 1200.0);
    const int lo = std::max(1, (int)std::floor(loHz * n / sr));
    const int hi = std::min((int)mag.size() - 1, (int)std::ceil(hiHz * n / sr) + 1);
    const int k = peakBin(mag, lo, hi);
    const double hz = interpHz(mag, k, n, sr);
    if (cents)
        *cents = centsErr(hz, expect);
    return hz;
}

double fwhmCents(const std::vector<double> &mag, int n, double sr, double expect)
{
    const int mid = peakBin(mag, std::max(1, (int)(expect * 0.9 * n / sr)),
                            std::min((int)mag.size() - 1, (int)(expect * 1.1 * n / sr)));
    const double half = mag[(size_t)mid] / std::sqrt(2.0);
    int a = mid, b = mid;
    while (a > 1 && mag[(size_t)a] > half)
        --a;
    while (b < (int)mag.size() - 1 && mag[(size_t)b] > half)
        ++b;
    const double lo = a * sr / n, hi = b * sr / n;
    return 1200.0 * std::log2(std::max(hi, lo + 1e-9) / std::max(lo, 1e-9));
}

double measureRt60(const juce::AudioBuffer<float> &b, double sr, int burstEnd)
{
    const int n = b.getNumSamples();
    const int win = std::max(64, (int)(0.020 * sr));
    std::vector<double> db;
    std::vector<double> t;
    for (int i = burstEnd; i + win < n; i += win / 2)
    {
        double s = 0;
        for (int k = 0; k < win; ++k)
        {
            const double x = b.getSample(0, i + k);
            s += x * x;
        }
        db.push_back(10.0 * std::log10(std::max(s / win, 1e-16)));
        t.push_back((i - burstEnd) / sr);
    }
    if (db.size() < 8)
        return 0;
    // Peak shortly after the burst, then first time we drop 20 dB (extrapolate to 60).
    size_t i0 = 2;
    for (size_t i = 2; i < std::min(db.size(), (size_t)8); ++i)
        if (db[i] > db[i0])
            i0 = i;
    const double ref = db[i0];
    const double target = ref - 20.0;
    for (size_t i = i0 + 1; i < db.size(); ++i)
        if (db[i] <= target)
        {
            const double dt = t[i] - t[i0];
            if (dt <= 1e-4)
                continue;
            return dt * 3.0; // 20 dB * 3 = 60 dB
        }
    return 0;
}

struct TestHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
    void advance(int n, double sr) { ppq += (n / sr) * (bpm / 60.0); }
    double bpm = 120, ppq = 0;
    bool playing = true;
};

void settleSignal(FeverProcessor &p, juce::AudioBuffer<float> &src, int samples, int block = 256)
{
    int done = 0;
    const int N = src.getNumSamples();
    while (done < samples)
    {
        const int k = std::min(block, samples - done);
        juce::AudioBuffer<float> sl(2, k);
        for (int i = 0; i < k; ++i)
        {
            const int s = (done + i) % N;
            sl.setSample(0, i, src.getSample(0, s));
            sl.setSample(1, i, src.getNumChannels() > 1 ? src.getSample(1, s) : src.getSample(0, s));
        }
        run(p, sl);
        done += k;
    }
}

int tests(const juce::File &folder)
{
    const double sr = 48000;
    long long bad = 0, den = 0;

    // -------------------- Centre null --------------------
    double nullDb = 0;
    {
        FeverProcessor p;
        setup(p, sr);
        defaults(p);
        p.set("fever", 0.5f);
        p.set("mix", 1);
        p.set("gainmatch", 0);
        p.set("limiter", 0);
        p.set("heartbeat", 0);
        p.snapEngine();
        juce::AudioBuffer<float> b(2, (int)(2.0 * sr));
        fillSine(b, sr, 440, 0.5);
        auto dry = b;
        processN(p, b);
        nullDb = residualDb(dry, b, (int)(0.2 * sr));
        record("centre_null_db", nullDb);
        check(nullDb < -60.0, "centre is dry: residual under -60 dB relative");
        writeWav(folder.getChildFile("centre-null.wav"), b, sr);
        bad += nonfinite(b, &den);
    }

    // Also with gain match on (makeup should stay ~1).
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.5f);
        p.set("gainmatch", 1);
        p.snapEngine();
        juce::AudioBuffer<float> b(2, (int)(1.5 * sr));
        fillSine(b, sr, 440, 0.5);
        auto dry = b;
        processN(p, b);
        const double d = residualDb(dry, b, (int)(0.4 * sr));
        record("centre_null_gainmatch_db", d);
        check(d < -60.0, "centre stays dry with gain match on");
    }

    // -------------------- Smooth sweep, 21 positions each side --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("sidea", 0.55f);
        p.set("sideb", 0.35f);
        juce::AudioBuffer<float> tone(2, (int)(1.2 * sr));
        fillSine(tone, sr, 440, 0.4);
        std::vector<double> loveE, hateE;
        bool loveMono = true, hateMono = true;
        for (int side = 0; side < 2; ++side)
        {
            auto &acc = side == 0 ? loveE : hateE;
            for (int k = 0; k <= 20; ++k)
            {
                const float amt = (float)k / 20.f;
                const float fv = side == 0 ? 0.5f - 0.5f * amt : 0.5f + 0.5f * amt;
                p.set("fever", fv);
                p.snapEngine();
                settleSignal(p, tone, (int)(0.7 * sr));
                juce::AudioBuffer<float> b(tone);
                processN(p, b);
                const double e = residualDb(tone, b, (int)(0.15 * sr));
                // residualDb is more negative when closer to dry; convert to linear energy ratio.
                const double energy = std::pow(10.0, e / 10.0);
                acc.push_back(energy);
            }
            for (int k = 1; k < (int)acc.size(); ++k)
                if (acc[(size_t)k] + 1e-6 < acc[(size_t)(k - 1)] * 0.85) // allow 15% wiggle, not a drop
                {
                    if (side == 0)
                        loveMono = false;
                    else
                        hateMono = false;
                }
        }
        record("sweep_love_start", loveE.front());
        record("sweep_love_end", loveE.back());
        record("sweep_hate_start", hateE.front());
        record("sweep_hate_end", hateE.back());
        check(loveE.back() > loveE.front() * 8, "love-side residual energy grows away from centre");
        check(hateE.back() > hateE.front() * 8, "hate-side residual energy grows away from centre");
        check(loveMono, "love-side sweep is monotonic (15% tolerance)");
        check(hateMono, "hate-side sweep is monotonic (15% tolerance)");
    }

    // -------------------- Pitch-cloud partials --------------------
    double maxCents = 0;
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.0f);
        p.set("sidea", 0.70f); // density
        p.set("sideb", 0.0f);  // spread off: exact ratios
        p.snapEngine();
        juce::AudioBuffer<float> tone(2, (int)(sr));
        fillSine(tone, sr, 440, 0.5);
        settleSignal(p, tone, (int)(1.6 * sr));
        juce::AudioBuffer<float> b(2, (int)(2.0 * sr));
        fillSine(b, sr, 440, 0.5);
        processN(p, b);
        writeWav(folder.getChildFile("love-cloud.wav"), b, sr);
        const int N = 65536;
        const int start = b.getNumSamples() - N - 1024;
        auto mag = std::vector<double>{};
        fftMag(channel(b, 0, start, N), mag);
        const double expects[] = { 220.0, 440.0 * 2.0 / 3.0, 660.0, 880.0, 1320.0 };
        const char *names[] = { "octave_down", "fifth_down", "fifth_up", "octave_up", "oct_fifth_up" };
        bool all = true;
        for (int i = 0; i < 5; ++i)
        {
            double c = 0;
            const double hz = peakNear(mag, N, sr, expects[i], 40.0, &c);
            record(std::string("cloud_") + names[i] + "_hz", hz);
            record(std::string("cloud_") + names[i] + "_cents", c);
            maxCents = std::max(maxCents, std::abs(c));
            if (std::abs(c) > 10.0)
                all = false;
        }
        record("cloud_max_cents", maxCents);
        check(all, "pitch-cloud partials sit at octave and fifth ratios (±10 cents)");
        bad += nonfinite(b);
    }

    // -------------------- Detune spread and stereo correlation --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.0f);
        p.set("sidea", 0.65f);
        juce::AudioBuffer<float> tone(2, (int)(sr));
        fillSine(tone, sr, 440, 0.5);

        auto residual = [](const juce::AudioBuffer<float> &wet, const juce::AudioBuffer<float> &dry) {
            juce::AudioBuffer<float> r(wet.getNumChannels(), wet.getNumSamples());
            for (int c = 0; c < wet.getNumChannels(); ++c)
                for (int i = 0; i < wet.getNumSamples(); ++i)
                    r.setSample(c, i, wet.getSample(c, i) - dry.getSample(c, i));
            return r;
        };
        auto spreadCents = [&](const juce::AudioBuffer<float> &buf, double expect) {
            const int N = 32768;
            auto mag = std::vector<double>{};
            fftMag(channel(buf, 0, buf.getNumSamples() - N, N), mag);
            // Second moment in cents around the expected partial, ±80 cents window.
            const double lo = expect * std::pow(2.0, -80.0 / 1200.0);
            const double hi = expect * std::pow(2.0, 80.0 / 1200.0);
            const int i0 = std::max(1, (int)(lo * N / sr));
            const int i1 = std::min((int)mag.size() - 1, (int)(hi * N / sr));
            double wsum = 0, m1 = 0, m2 = 0;
            for (int k = i0; k <= i1; ++k)
            {
                const double c = centsErr(k * sr / (double)N, expect);
                const double w = mag[(size_t)k] * mag[(size_t)k];
                wsum += w;
                m1 += w * c;
                m2 += w * c * c;
            }
            if (wsum < 1e-20)
                return 0.0;
            const double mu = m1 / wsum;
            return std::sqrt(std::max(0.0, m2 / wsum - mu * mu));
        };

        p.set("sideb", 0.0f);
        p.snapEngine();
        settleSignal(p, tone, (int)(1.6 * sr));
        juce::AudioBuffer<float> a(2, (int)(1.8 * sr));
        fillSine(a, sr, 440, 0.5);
        auto dryA = a;
        processN(p, a);
        auto r0 = residual(a, dryA);
        const double corr0 = correlation(r0, (int)(0.3 * sr));
        const double w0 = spreadCents(r0, 880.0);

        p.set("sideb", 1.0f);
        p.snapEngine();
        settleSignal(p, tone, (int)(1.6 * sr));
        juce::AudioBuffer<float> b(2, (int)(1.8 * sr));
        fillSine(b, sr, 440, 0.5);
        auto dryB = b;
        processN(p, b);
        auto r1 = residual(b, dryB);
        const double corr1 = correlation(r1, (int)(0.3 * sr));
        const double w1 = spreadCents(r1, 880.0);
        record("spread0_corr", corr0);
        record("spread1_corr", corr1);
        record("spread0_sigma_cents", w0);
        record("spread1_sigma_cents", w1);
        check(corr0 > 0.92, "spread 0 keeps high stereo correlation on the cloud residual");
        check(corr1 < corr0 - 0.08, "spread 1 lowers stereo correlation on the cloud residual");
        check(w1 > w0 + 4.0, "spread 1 widens the detune of the octave partial");
        writeWav(folder.getChildFile("love-spread.wav"), b, sr);
    }

    // -------------------- Spectral peak frequencies --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 1.0f);
        p.set("sidea", 0.20f); // freeze low, so the peaks are live
        p.set("sideb", 0.0f);  // no halo
        p.snapEngine();
        juce::AudioBuffer<float> nz(2, (int)(sr));
        fillNoise(nz, 0xA11U, 0.5f);
        settleSignal(p, nz, (int)(1.2 * sr));
        juce::AudioBuffer<float> b(2, (int)(2.5 * sr));
        fillNoise(b, 0xB22U, 0.5f);
        processN(p, b);
        writeWav(folder.getChildFile("hate-peaks.wav"), b, sr);
        const int N = 65536;
        auto mag = std::vector<double>{};
        fftMag(channel(b, 0, b.getNumSamples() - N, N), mag);
        bool all = true;
        double maxRel = 0;
        for (int i = 0; i < fever::kPeaks; ++i)
        {
            const double expect = fever::kPeakHz[i];
            // Wider window: ±18% to find the peak, then check ±5%.
            double c = 0;
            const double hz = peakNear(mag, N, sr, expect, 200.0, &c);
            const double rel = std::abs(hz - expect) / expect;
            char name[64];
            std::snprintf(name, sizeof(name), "peak_%d_hz", (int)expect);
            record(name, hz);
            std::snprintf(name, sizeof(name), "peak_%d_rel", (int)expect);
            record(name, rel);
            maxRel = std::max(maxRel, rel);
            if (rel > 0.05)
                all = false;
        }
        record("peak_max_rel", maxRel);
        check(all, "spectral peak frequencies are within ±5%");
    }

    // -------------------- Tail decay time --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 1.0f);
        p.set("sidea", 0.50f);
        p.set("sideb", 0.0f);
        p.snapEngine();
        const int burst = (int)(0.25 * sr);
        const int total = (int)(4.5 * sr);
        juce::AudioBuffer<float> b(2, total);
        b.clear();
        juce::AudioBuffer<float> head(2, burst);
        fillNoise(head, 0x51U, 0.6f);
        for (int c = 0; c < 2; ++c)
            b.copyFrom(c, 0, head, c, 0, burst);
        // Prime with the burst, then the rest is silence already in the buffer.
        processN(p, b);
        const double meas = measureRt60(b, sr, burst);
        const double expect = fever::tailRt60(0.5f);
        record("tail_rt60_s", meas);
        record("tail_rt60_expect_s", expect);
        const double rel = expect > 0 ? std::abs(meas - expect) / expect : 1;
        record("tail_rt60_rel", rel);
        check(meas > 0.3 && rel < 0.45, "tail decay time matches FREEZE=0.5 (±45%)");
        writeWav(folder.getChildFile("hate-tail.wav"), b, sr);
    }

    // -------------------- Halo detune --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 1.0f);
        p.set("sidea", 0.15f);
        p.set("sideb", 1.0f);
        p.snapEngine();
        juce::AudioBuffer<float> tone(2, (int)(sr));
        fillSine(tone, sr, 440, 0.5);
        settleSignal(p, tone, (int)(1.2 * sr));
        juce::AudioBuffer<float> b(2, (int)(2.0 * sr));
        fillSine(b, sr, 440, 0.5);
        auto dryH = b;
        processN(p, b);
        writeWav(folder.getChildFile("hate-halo.wav"), b, sr);
        // Residual (out - dry) so the unsifted 440 does not steal the peak.
        juce::AudioBuffer<float> res(2, b.getNumSamples());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                res.setSample(c, i, b.getSample(c, i) - dryH.getSample(c, i));
        const int N = 65536;
        auto mag = std::vector<double>{};
        fftMag(channel(res, 0, res.getNumSamples() - N, N), mag);
        const double up = 440.0 + (double)fever::kHaloUpHz;
        const double dn = 440.0 + (double)fever::kHaloDnHz;
        double cUp = 0, cDn = 0;
        const double hzUp = peakNear(mag, N, sr, up, 25.0, &cUp);
        const double hzDn = peakNear(mag, N, sr, dn, 25.0, &cDn);
        record("halo_up_hz", hzUp);
        record("halo_up_cents", cUp);
        record("halo_dn_hz", hzDn);
        record("halo_dn_cents", cDn);
        check(std::abs(cUp) < 15.0, "halo up-voice is within 15 cents of +38");
        check(std::abs(cDn) < 15.0, "halo down-voice is within 15 cents of -51");
    }

    // -------------------- Side-knob remapping --------------------
    {
        check(std::string(fever::sideAName(0.2f)) == "DENSITY", "side A is DENSITY on the cloud side");
        check(std::string(fever::sideBName(0.2f)) == "SPREAD", "side B is SPREAD on the cloud side");
        check(std::string(fever::sideAName(0.5f)) == "DENSITY", "side A stays DENSITY at the centre");
        check(std::string(fever::sideBName(0.5f)) == "SPREAD", "side B stays SPREAD at the centre");
        check(std::string(fever::sideAName(0.8f)) == "FREEZE", "side A is FREEZE on the rift side");
        check(std::string(fever::sideBName(0.8f)) == "HALO", "side B is HALO on the rift side");

        // Density: more grains at a higher Side A on the love side.
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.0f);
        p.set("sideb", 0.2f);
        juce::AudioBuffer<float> tone(2, (int)(sr));
        fillSine(tone, sr, 440, 0.5);
        p.set("sidea", 0.15f);
        p.snapEngine();
        settleSignal(p, tone, (int)(1.0 * sr));
        const int gLo = p.getEngine().activeGrains;
        p.set("sidea", 0.95f);
        p.snapEngine();
        settleSignal(p, tone, (int)(1.0 * sr));
        const int gHi = p.getEngine().activeGrains;
        record("density_grains_lo", gLo);
        record("density_grains_hi", gHi);
        check(gHi > gLo + 4, "DENSITY raises the number of live grains");

        // Freeze: longer tail at higher Side A on the hate side.
        auto rtAt = [&](float fr) {
            FeverProcessor q;
            setup(q, sr, 256, 2, true);
            defaults(q);
            q.set("fever", 1.0f);
            q.set("sidea", fr);
            q.set("sideb", 0.0f);
            q.snapEngine();
            const int burst = (int)(0.2 * sr);
            juce::AudioBuffer<float> b(2, (int)(3.8 * sr));
            b.clear();
            juce::AudioBuffer<float> head(2, burst);
            fillNoise(head, 0x99U, 0.55f);
            for (int c = 0; c < 2; ++c)
                b.copyFrom(c, 0, head, c, 0, burst);
            processN(q, b);
            return measureRt60(b, sr, burst);
        };
        const double rtLo = rtAt(0.15f), rtHi = rtAt(0.70f);
        record("freeze_rt60_lo", rtLo);
        record("freeze_rt60_hi", rtHi);
        check(rtHi > rtLo * 1.3, "FREEZE lengthens the cold tail");

        // Halo remapping already proven by the halo-detune test (peaks only with Side B up).
        // Confirm Side B = 0 does not produce those split peaks at a comparable level.
        {
            FeverProcessor q;
            setup(q, sr, 256, 2, true);
            defaults(q);
            q.set("fever", 1.0f);
            q.set("sidea", 0.15f);
            q.set("sideb", 0.0f);
            q.snapEngine();
            juce::AudioBuffer<float> tn(2, (int)(sr));
            fillSine(tn, sr, 440, 0.5);
            settleSignal(q, tn, (int)(0.8 * sr));
            juce::AudioBuffer<float> b(2, (int)(1.4 * sr));
            fillSine(b, sr, 440, 0.5);
            processN(q, b);
            juce::AudioBuffer<float> dryB = tn;
            // b was processed from a fresh 440 sine (same phase as tn start only if lengths match)
            juce::AudioBuffer<float> res(2, b.getNumSamples());
            // reconstruct dry of b: it started as fillSine 440 0.5 phase 0
            juce::AudioBuffer<float> dry(2, b.getNumSamples());
            fillSine(dry, sr, 440, 0.5);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    res.setSample(c, i, b.getSample(c, i) - dry.getSample(c, i));
            const int N = 32768;
            auto mag = std::vector<double>{};
            fftMag(channel(res, 0, res.getNumSamples() - N, N), mag);
            const double up = 440.0 + (double)fever::kHaloUpHz;
            const double fundHz = peakNear(mag, N, sr, 440.0, 20.0, nullptr);
            (void)fundHz;
            const int kf = peakBin(mag, (int)(420 * N / sr), (int)(460 * N / sr));
            const int ku = peakBin(mag, (int)(up * std::pow(2.0, -20.0 / 1200.0) * N / sr),
                                   (int)(up * std::pow(2.0, 20.0 / 1200.0) * N / sr));
            const double fund = mag[(size_t)kf];
            const double ratio = fund > 1e-12 ? mag[(size_t)ku] / fund : 0;
            // Also compare against the residual energy: with HALO off the +38 region should be weak
            // relative to the same measurement in the HALO-on test. A simple absolute check:
            record("halo_off_up_ratio", ratio);
            check(ratio < 0.35, "HALO at 0 does not produce a strong +38-cent peak");
        }
    }

    // -------------------- Tempo-sync heartbeat --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.25f);
        p.set("sidea", 0.6f);
        p.set("sideb", 0.3f);
        p.set("heartbeat", 1);
        p.set("heartrate", 0); // 1/4, 1 beat at 120 BPM = 0.5 s
        p.set("heartdepth", 0.45f);
        p.snapEngine();
        TestHead head;
        head.bpm = 120;
        head.playing = true;
        head.ppq = 0;
        p.setPlayHead(&head);
        const int block = 256;
        const int total = (int)(4.0 * sr);
        std::vector<double> wraps;
        float prev = 0;
        juce::AudioBuffer<float> tone(2, block);
        int done = 0;
        while (done < total)
        {
            fillSine(tone, sr, 440, 0.4, 2 * juce::MathConstants<double>::pi * 440 * done / sr);
            run(p, tone);
            const float ph = p.getEngine().heartPhase;
            if (ph + 0.5f < prev)
                wraps.push_back(done / sr);
            prev = ph;
            head.advance(block, sr);
            done += block;
        }
        p.setPlayHead(nullptr);
        record("heartbeat_wraps", (double)wraps.size());
        double period = 0;
        if (wraps.size() >= 3)
        {
            std::vector<double> dp;
            for (size_t i = 1; i < wraps.size(); ++i)
                dp.push_back(wraps[i] - wraps[i - 1]);
            period = std::accumulate(dp.begin(), dp.end(), 0.0) / (double)dp.size();
        }
        record("heartbeat_period_s", period);
        record("heartbeat_expect_s", 0.5);
        check(wraps.size() >= 6 && std::abs(period - 0.5) < 0.02,
              "heartbeat period matches 120 BPM quarters (0.5 s ± 20 ms)");
    }

    // -------------------- Gain match, ±1.5 dB LUFS --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        p.set("sidea", 0.55f);
        p.set("sideb", 0.45f);
        juce::AudioBuffer<float> mat(2, (int)(2.0 * sr));
        fillMaterial(mat, sr, 7);
        std::vector<double> vals;
        for (int k = 0; k <= 20; ++k)
        {
            const float fv = (float)k / 20.f;
            p.set("fever", fv);
            p.snapEngine();
            settleSignal(p, mat, (int)(1.6 * sr));
            juce::AudioBuffer<float> b(2, (int)(2.5 * sr));
            // loop material
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const int s = i % mat.getNumSamples();
                b.setSample(0, i, mat.getSample(0, s));
                b.setSample(1, i, mat.getSample(1, s));
            }
            processN(p, b);
            vals.push_back(lufs(b, (int)(0.8 * sr)));
        }
        auto sorted = vals;
        std::sort(sorted.begin(), sorted.end());
        const double med = sorted[10];
        const double mn = sorted.front(), mx = sorted.back();
        const double dev = std::max(std::abs(mx - med), std::abs(mn - med));
        record("gainmatch_lufs_min", mn);
        record("gainmatch_lufs_max", mx);
        record("gainmatch_lufs_median", med);
        record("gainmatch_lufs_max_dev", dev);
        check(dev <= 1.5, "gain match keeps a 21-point sweep within ±1.5 dB LUFS of the median");
    }

    // -------------------- Clicks on fast automation --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("sidea", 0.6f);
        p.set("sideb", 0.4f);
        p.set("gainmatch", 0);
        p.set("limiter", 0);
        p.snapEngine();
        juce::AudioBuffer<float> b(2, (int)(2.0 * sr));
        fillSine(b, sr, 440, 0.4);
        const int jumpEvery = 128;
        int done = 0;
        float fv = 0;
        double maxJump = 0, prev = 0;
        bool first = true;
        while (done < b.getNumSamples())
        {
            const int k = std::min(64, b.getNumSamples() - done);
            if ((done / jumpEvery) % 2 == 0)
                fv = 0.f;
            else
                fv = 1.f;
            p.set("fever", fv);
            juce::AudioBuffer<float> sl(2, k);
            for (int c = 0; c < 2; ++c)
                sl.copyFrom(c, 0, b, c, done, k);
            run(p, sl);
            for (int i = 0; i < k; ++i)
            {
                const double x = sl.getSample(0, i);
                if (!first)
                    maxJump = std::max(maxJump, std::abs(x - prev));
                prev = x;
                first = false;
                b.setSample(0, done + i, sl.getSample(0, i));
                b.setSample(1, done + i, sl.getSample(1, i));
            }
            long long d2 = 0;
            bad += nonfinite(sl, &d2);
            den += d2;
            done += k;
        }
        record("automation_max_jump", maxJump);
        check(maxJump < 0.18, "fast FEVER automation has no clicks (max sample jump < 0.18)");
        writeWav(folder.getChildFile("automation.wav"), b, sr);
    }

    // -------------------- Limiter ceiling --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 1.0f);
        p.set("sidea", 0.5f);
        p.set("sideb", 0.7f);
        p.set("limiter", 1);
        p.set("gainmatch", 0);
        p.set("output", 12);
        p.snapEngine();
        juce::AudioBuffer<float> b(2, (int)(1.5 * sr));
        fillSine(b, sr, 220, 0.9);
        processN(p, b);
        double pk = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 256; i < b.getNumSamples(); ++i)
                pk = std::max(pk, (double)std::abs(b.getSample(c, i)));
        record("limiter_peak", pk);
        record("limiter_ceiling", fever::kCeiling);
        check(pk <= (double)fever::kCeiling + 1e-4, "limiter holds the -1 dBFS ceiling");

        FeverProcessor q;
        setup(q, sr, 256, 2, true);
        defaults(q);
        q.set("fever", 1.0f);
        q.set("limiter", 0);
        q.set("output", 12);
        q.set("gainmatch", 0);
        q.snapEngine();
        juce::AudioBuffer<float> b2(2, (int)(0.6 * sr));
        fillSine(b2, sr, 220, 0.9);
        processN(q, b2);
        double pkOff = 0;
        for (int i = 256; i < b2.getNumSamples(); ++i)
            pkOff = std::max(pkOff, (double)std::abs(b2.getSample(0, i)));
        record("limiter_off_peak", pkOff);
        check(pkOff > (double)fever::kCeiling + 0.05, "without the limiter the peak exceeds the ceiling");
    }

    // -------------------- NaNs, denormals, bypass, state --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        defaults(p);
        p.set("fever", 0.1f);
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        p.snapEngine();
        juce::AudioBuffer<float> b(2, (int)(1.0 * sr));
        fillMaterial(b, sr, 3);
        processN(p, b);
        long long d2 = 0;
        bad += nonfinite(b, &d2);
        den += d2;

        p.set("bypass", 1);
        p.snapEngine();
        juce::AudioBuffer<float> b2(2, 4096);
        fillSine(b2, sr, 440, 0.4);
        auto dry = b2;
        processN(p, b2);
        double md = 0;
        for (int i = 512; i < 4096; ++i)
            md = std::max(md, (double)std::abs(b2.getSample(0, i) - dry.getSample(0, i)));
        record("bypass_max_diff", md);
        check(md < 1e-4, "bypass is transparent after the fade");

        juce::MemoryBlock mb;
        p.set("bypass", 0);
        p.set("fever", 0.22f);
        p.getStateInformation(mb);
        FeverProcessor q;
        setup(q, sr, 256, 2, true);
        q.setStateInformation(mb.getData(), (int)mb.getSize());
        record("state_fever", q.getSettings()[fever::FeverAmt]);
        check(std::abs(q.getSettings()[fever::FeverAmt] - 0.22f) < 0.002, "state save/load restores FEVER");

        check(fever::presetCount == 12, "twelve factory presets");
    }

    // -------------------- CPU, cloud and rift, under 8% --------------------
    double cpuMedian = 0, cpuMin = 0, cpuMax = 0, worstUs = 0;
    auto cpuOf = [&](int preset, float fv, float a, float b) {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        p.factory(preset);
        p.set("fever", fv);
        p.set("sidea", a);
        p.set("sideb", b);
        p.set("gainmatch", 1);
        p.set("limiter", 1);
        p.snapEngine();
        juce::AudioBuffer<float> mat(2, 256);
        fillMaterial(mat, sr, 99);
        for (int i = 0; i < 32; ++i)
        {
            juce::AudioBuffer<float> w(mat);
            run(p, w);
        }
        const int blocks = (int)(16.0 * sr / 256);
        std::vector<double> runs;
        for (int runi = 0; runi < 5; ++runi)
        {
            double worst = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < blocks; ++i)
            {
                juce::AudioBuffer<float> w(2, 256);
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
        return runs;
    };
    {
        auto cloud = cpuOf(3, 0.0f, 0.95f, 0.5f);
        auto rift = cpuOf(11, 1.0f, 0.6f, 0.8f);
        std::vector<double> both;
        both.insert(both.end(), cloud.begin(), cloud.end());
        both.insert(both.end(), rift.begin(), rift.end());
        std::sort(both.begin(), both.end());
        // Report the heavier of the two medians.
        const double medC = cloud[2], medR = rift[2];
        cpuMedian = std::max(medC, medR);
        cpuMin = both.front();
        cpuMax = both.back();
        record("cpu_cloud_median", medC);
        record("cpu_rift_median", medR);
        record("cpu_median", cpuMedian);
        record("cpu_min", cpuMin);
        record("cpu_max", cpuMax);
        record("cpu_worst_block_us", worstUs);
        check(cpuMedian < 8.0, "CPU is under 8% of one core (heavier of cloud / rift)");
    }

    record("nonfinite", (double)bad);
    record("denorm", (double)den);
    check(bad == 0, "no NaNs or infinities in the output");
    check(den == 0, "no denormals in the output (FTZ/DAZ)");

    // -------------------- Editor snapshot --------------------
    {
        FeverProcessor p;
        setup(p, sr, 256, 2, true);
        p.factory(0);
        auto ed = std::make_unique<FeverEditor>(p);
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
        std::fprintf(stderr, "FeverTest test folder | screenshot file [presetIndex]\n");
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
        FeverProcessor p;
        setup(p);
        failures = 0;
        const int preset = argc > 3 ? std::atoi(argv[3]) : 0;
        p.factory(preset);
        auto ed = std::make_unique<FeverEditor>(p);
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
