#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <limits>
#include <vector>

namespace
{
int failures = 0;
void check(bool good, const char *text)
{
    std::printf("%s %s\n", good ? "PASS" : "FAIL", text);
    if (!good)
        ++failures;
}
void checkNear(double v, double expect, double tol, const char *text)
{
    const bool good = std::isfinite(v) && std::abs(v - expect) <= tol;
    std::printf("%s %s  (got %.6g, expected %.6g ± %.6g)\n", good ? "PASS" : "FAIL", text, v, expect, tol);
    if (!good)
        ++failures;
}

void setup(SmotherProcessor &p, double sr = 48000, int block = 256, int channels = 2)
{
    auto layout = p.getBusesLayout();
    layout.outputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    check(p.setBusesLayout(layout), "bus configuration accepted");
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}

void process(SmotherProcessor &p, juce::AudioBuffer<float> &b, juce::MidiBuffer *midi = nullptr)
{
    juce::MidiBuffer local;
    p.processBlock(b, midi ? *midi : local);
}

float rms(const juce::AudioBuffer<float> &b, int start = 0, int count = -1)
{
    if (count < 0)
        count = b.getNumSamples() - start;
    double s = 0;
    const int n = std::max(0, count), c = b.getNumChannels();
    long k = 0;
    for (int ch = 0; ch < c; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float x = b.getSample(ch, start + i);
            s += x * x;
            ++k;
        }
    return float(std::sqrt(s / std::max(1L, k)));
}

float peak(const juce::AudioBuffer<float> &b)
{
    float m = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            m = std::max(m, std::abs(b.getSample(ch, i)));
    return m;
}

std::vector<float> sine(int n, double sr, double hz, double amp = 0.5)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = float(amp * std::sin(2 * juce::MathConstants<double>::pi * hz * i / sr));
    return v;
}

std::vector<float> noise(int n, uint32_t seed = 0xC0FFEEu)
{
    std::vector<float> v((size_t)n);
    uint32_t r = seed;
    for (int i = 0; i < n; ++i)
    {
        r = r * 1664525u + 1013904223u;
        v[(size_t)i] = float(int(r) >> 8) * (1.f / 8388608.f);
    }
    return v;
}

juce::AudioBuffer<float> renderClock(SmotherProcessor &p, int samples, double bpm, int block = 64)
{
    p.set("seq", 1);
    p.setTestClock(bpm, true);
    juce::AudioBuffer<float> out(2, samples);
    out.clear();
    int done = 0;
    while (done < samples)
    {
        const int n = std::min(block, samples - done);
        juce::AudioBuffer<float> b(2, n);
        b.clear();
        process(p, b);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, done, b, ch, 0, n);
        done += n;
    }
    return out;
}

smother::Pattern blank(int lanes = 1)
{
    smother::Pattern p;
    smother::initDefaultLanes(p);
    p.numLanes = lanes;
    for (int i = 0; i < smother::maxLanes; ++i)
    {
        p.lanes[(size_t)i].vel.fill(0);
        p.lanes[(size_t)i].sleeveCount = 0;
        p.lanes[(size_t)i].mute = false;
        p.lanes[(size_t)i].solo = false;
        p.lanes[(size_t)i].enabled = true;
        p.lanes[(size_t)i].doubleLen = false;
        p.lanes[(size_t)i].length = 16;
        p.lanes[(size_t)i].rate = smother::Rate::Sixteenth;
        p.lanes[(size_t)i].decay = 1;
        p.lanes[(size_t)i].level = 1;
        p.lanes[(size_t)i].hp = 20;
        p.lanes[(size_t)i].lp = 20000;
        p.lanes[(size_t)i].drive = 0;
        p.lanes[(size_t)i].crush = 0;
        p.lanes[(size_t)i].transient = 0;
        p.lanes[(size_t)i].ott = 0;
        p.lanes[(size_t)i].eqLow = p.lanes[(size_t)i].eqMid = p.lanes[(size_t)i].eqHigh = 0;
        p.lanes[(size_t)i].tone = 1;
        p.lanes[(size_t)i].pan = 0;
        p.lanes[(size_t)i].env = smother::EnvShape::Linear;
    }
    p.allCount = 0;
    return p;
}

int peakBin(const std::vector<float> &x, double sr, double lo, double hi)
{
    const int n = int(x.size());
    int best = 0;
    double bestP = -1;
    const int i0 = std::max(1, int(lo / sr * n));
    const int i1 = std::min(n / 2 - 1, int(hi / sr * n));
    for (int k = i0; k <= i1; ++k)
    {
        const double ang = -2 * juce::MathConstants<double>::pi * k / n;
        double cr = 0, ci = 0, c = 1, s = 0, dc = std::cos(ang), ds = std::sin(ang);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / (n - 1));
            const double v = x[(size_t)i] * w;
            cr += v * c;
            ci += v * s;
            const double nc = c * dc - s * ds;
            s = c * ds + s * dc;
            c = nc;
        }
        const double pwr = cr * cr + ci * ci;
        if (pwr > bestP)
        {
            bestP = pwr;
            best = k;
        }
    }
    return best;
}

double binHz(int k, int n, double sr) { return k * sr / n; }

double bandPower(const std::vector<float> &x, double sr, double lo, double hi)
{
    const int n = int(x.size());
    const int i0 = std::max(1, int(lo / sr * n));
    const int i1 = std::min(n / 2 - 1, int(hi / sr * n));
    double sum = 0;
    for (int k = i0; k <= i1; ++k)
    {
        const double ang = -2 * juce::MathConstants<double>::pi * k / n;
        double cr = 0, ci = 0, c = 1, s = 0, dc = std::cos(ang), ds = std::sin(ang);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / (n - 1));
            const double v = x[(size_t)i] * w;
            cr += v * c;
            ci += v * s;
            const double nc = c * dc - s * ds;
            s = c * ds + s * dc;
            c = nc;
        }
        sum += cr * cr + ci * ci;
    }
    return sum;
}

int tests(const juce::File &folder)
{
    folder.createDirectory();
    const double sr = 48000;
    const double bpm = 120;
    // 1/16 at 120 BPM = 0.125 s = 6000 samples. A 16-step loop is 96000 samples (2 s, 4 beats).

    // ---------- MUTE sleeve attenuation, attack/release, no clicks ----------
    double muteDb = 0, attMs = 0, relMs = 0, maxJump = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 64);
        p.set("swing", 0);
        p.set("human", 0);
        p.set("sleevehuman", 0);
        p.set("masterott", 0);
        p.set("masterhp", 20);
        p.set("masterlp", 20000);
        p.set("output", 0);
        p.set("seq", 1);
        auto sinev = sine(int(sr * 8), sr, 220, 0.5);
        auto pat = blank(1);
        pat.lanes[0].vel[0] = 1; // single trigger: a continuous sine, no retrigger phase
        pat.lanes[0].decay = 1;
        pat.lanes[0].env = smother::EnvShape::Linear;
        pat.lanes[0].length = 16;
        p.setPattern(pat);
        p.loadSample(0, sinev.data(), (int)sinev.size(), sr, "sine");
        // Unmuted reference (4 beats = 2 seconds at 120).
        const int N = int(sr * 2);
        auto dry = renderClock(p, N, bpm);
        // MUTE sleeve over steps 4-11 (1 beat, 0.5 s), attack 50 ms, release 80 ms, linear, full depth.
        smother::Sleeve sv;
        sv.start = 4;
        sv.length = 8;
        sv.type = smother::SleeveType::Mute;
        sv.attackMs = 50;
        sv.releaseMs = 80;
        sv.depth = 1;
        sv.curve = 0;
        auto patW = p.pattern(); // keeps the loaded sine
        patW.lanes[0].sleeves[0] = sv;
        patW.lanes[0].sleeveCount = 1;
        p.setPattern(patW);
        p.prepareToPlay(sr, 64); // reset engine voices / playhead
        p.setTestClock(bpm, true);
        auto wet = renderClock(p, N, bpm);

        // Write WAVs for the report.
        auto writeWav = [&](const juce::File &f, const juce::AudioBuffer<float> &b) {
            f.deleteFile();
            auto os = std::make_unique<juce::FileOutputStream>(f);
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.release(), sr, 2, 24, {}, 0));
            if (w)
                w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        };
        writeWav(folder.getChildFile("mute-dry.wav"), dry);
        writeWav(folder.getChildFile("mute-wet.wav"), wet);

        // Step 4 starts at 4 * 0.25 = 1 beat = 0.5 s = 24000 samples. Ends at 12*0.25=3 beats=1.5s=72000.
        const int start = int(0.5 * sr);
        const int attS = int(0.050 * sr);
        const int end = int(1.5 * sr);
        const int relS = int(0.080 * sr);
        // Sustain region: after attack, before end.
        const int sust0 = start + attS + int(0.02 * sr);
        const int sustN = std::max(64, end - start - attS - int(0.04 * sr));
        const float dryR = rms(dry, sust0, sustN);
        const float wetR = rms(wet, sust0, sustN);
        muteDb = wetR > 0 && dryR > 0 ? 20.0 * std::log10(wetR / dryR) : (wetR == 0 ? -240 : 0);
        check(muteDb <= -60.0, "MUTE sleeve full-depth attenuation is at least 60 dB");
        std::printf("    mute attenuation = %.2f dB (dry rms %.6f, wet rms %.6f)\n", muteDb, dryR, wetR);

        // Envelope from short RMS windows, mapped through gain = 10^(-9*e).
        const int win = int(0.002 * sr); // 2 ms
        auto envAt = [&](int i) -> double {
            if (i < 0 || i + win >= N)
                return -1;
            const double d = rms(dry, i, win), w = rms(wet, i, win);
            if (d < 1.0e-5)
                return -1;
            const double g = std::min(1.0, std::max(1.0e-12, w / d));
            return std::min(1.0, std::max(0.0, -std::log10(g) / 9.0));
        };
        auto cross = [&](int from, int to, double target, bool rising) -> int {
            for (int i = from; i < to; i += std::max(1, win / 4))
            {
                const double e = envAt(i);
                if (e < 0)
                    continue;
                if (rising ? e >= target : e <= target)
                    return i;
            }
            return -1;
        };
        const int a10 = cross(start - win, start + attS * 3, 0.10, true);
        const int a30 = cross(start - win, start + attS * 3, 0.30, true);
        if (a10 >= 0 && a30 > a10)
            attMs = (a30 - a10) * 1000.0 / sr / 0.2; // 10-30% is 20% of a linear ramp
        else
            std::printf("    attack crossings a10=%d a30=%d  e@start=%.3f e@+50ms=%.3f\n", a10, a30,
                        envAt(start), envAt(start + attS));
        checkNear(attMs, 50.0, 15.0, "MUTE attack time matches 50 ms setting (10-30% scaled)");
        const int r90 = cross(end - win, end + relS * 3, 0.90, false);
        const int r10 = cross(end - win, end + relS * 3, 0.10, false);
        if (r90 >= 0 && r10 > r90)
            relMs = (r10 - r90) * 1000.0 / sr / 0.8;
        else
            std::printf("    release crossings r90=%d r10=%d  e@end=%.3f e@+80ms=%.3f\n", r90, r10,
                        envAt(end), envAt(end + relS));
        checkNear(relMs, 80.0, 20.0, "MUTE release time matches 80 ms setting (90-10% scaled)");

        // Clicks: first difference of the wet signal around the ramps, compared with a bound
        // consistent with a 50 ms ramp (not a step). A unit step would be ~0.5.
        maxJump = 0;
        const int lo = std::max(1, start - 64);
        const int hi = std::min(N - 1, end + relS + 64);
        for (int i = lo; i < hi; ++i)
        {
            const float a = wet.getSample(0, i), b = wet.getSample(0, i - 1);
            maxJump = std::max(maxJump, (double)std::abs(a - b));
        }
        // 50 ms linear mute of a 0.5 amp sine: max d(gain)/dn is small; the sine itself diffs ~2*pi*220/sr*0.5≈0.014.
        // A click would be tens of times larger. Bound 0.08 is generous but well below a discontinuity.
        check(maxJump < 0.08, "MUTE ramps have no clicks (max first-difference below bound)");
        std::printf("    attack=%.2f ms  release=%.2f ms  maxJump=%.5f\n", attMs, relMs, maxJump);
    }

    // ---------- LP / HP sleeve cutoff (spectrum) ----------
    double lpCutoff = 0, hpCutoff = 0;
    {
        auto ratioAt = [&](smother::SleeveType type, double hz, double lpFull = 500, double hpFull = 2000) -> double {
            SmotherProcessor p;
            setup(p, sr, 64);
            p.set("swing", 0);
            p.set("human", 0);
            p.set("sleevehuman", 0);
            p.set("masterott", 0);
            p.set("masterhp", 20);
            p.set("masterlp", 20000);
            p.set("output", 0);
            auto tone = sine(int(sr * 6), sr, hz, 0.5);
            auto pat = blank(1);
            pat.lanes[0].vel[0] = 1;
            pat.lanes[0].decay = 1;
            pat.lanes[0].tone = 1;
            pat.lanes[0].env = smother::EnvShape::Linear;
            p.setPattern(pat);
            p.loadSample(0, tone.data(), (int)tone.size(), sr, "tone");
            const int N = int(sr * 2);
            auto dry = renderClock(p, N, bpm);
            smother::Sleeve sv;
            sv.start = 0;
            sv.length = 16;
            sv.type = type;
            sv.attackMs = 5;
            sv.releaseMs = 5;
            sv.depth = 1;
            sv.curve = 0;
            auto patW = p.pattern(); // keeps the loaded tone
            patW.lanes[0].sleeves[0] = sv;
            patW.lanes[0].sleeveCount = 1;
            p.setPattern(patW);
            p.prepareToPlay(sr, 64);
            p.setSleeveExtents((float)lpFull, (float)hpFull);
            p.setTestClock(bpm, true);
            auto wet = renderClock(p, N, bpm);
            // Settled window (after attack, mid-bar).
            const int w0 = int(0.4 * sr), wN = int(0.8 * sr);
            const float d = rms(dry, w0, wN), w = rms(wet, w0, wN);
            return d > 1e-8 ? (double)w / (double)d : 0;
        };
        auto findCut = [&](smother::SleeveType type, bool lowpass, double lo, double hi, double lpFull,
                           double hpFull) -> double {
            // Binary search the -3 dB (amplitude ratio ~ 0.707) point with sine probes.
            double a = lo, b = hi;
            for (int i = 0; i < 10; ++i)
            {
                const double mid = 0.5 * (a + b);
                const double r = ratioAt(type, mid, lpFull, hpFull);
                const bool below = r < 0.707;
                if (lowpass)
                    (below ? b : a) = mid;
                else
                    (below ? a : b) = mid;
            }
            return 0.5 * (a + b);
        };
        lpCutoff = findCut(smother::SleeveType::Lowpass, true, 80, 4000, 500, 2000);
        hpCutoff = findCut(smother::SleeveType::Highpass, false, 200, 8000, 500, 2000);
        const double lp250 = ratioAt(smother::SleeveType::Lowpass, 250);
        const double lp500 = ratioAt(smother::SleeveType::Lowpass, 500);
        const double lp2000 = ratioAt(smother::SleeveType::Lowpass, 2000);
        const double hp500 = ratioAt(smother::SleeveType::Highpass, 500);
        const double hp2000 = ratioAt(smother::SleeveType::Highpass, 2000);
        const double hp6000 = ratioAt(smother::SleeveType::Highpass, 6000);
        std::printf("    LP ratios 250=%.3f 500=%.3f 2000=%.3f   HP ratios 500=%.3f 2000=%.3f 6000=%.3f\n",
                    lp250, lp500, lp2000, hp500, hp2000, hp6000);
        checkNear(lpCutoff, 500.0, 25.0, "LOWPASS sleeve -3 dB cutoff within 5% of 500 Hz");
        checkNear(hpCutoff, 2000.0, 100.0, "HIGHPASS sleeve -3 dB cutoff within 5% of 2000 Hz");
        std::printf("    default extents: LP cutoff=%.1f Hz  HP cutoff=%.1f Hz\n", lpCutoff, hpCutoff);

        // Additional cutoffs at 200, 1000, 5000 Hz (full-depth extents set to the target).
        struct Cut { const char *name; double hz; double lpM; double hpM; };
        std::vector<Cut> extraCuts;
        for (double target : { 200.0, 1000.0, 5000.0 })
        {
            const double tol = target * 0.05;
            const double lpM = findCut(smother::SleeveType::Lowpass, true, target * 0.4, target * 2.5, target, target);
            const double hpM = findCut(smother::SleeveType::Highpass, false, target * 0.4, target * 2.5, target, target);
            extraCuts.push_back({ "", target, lpM, hpM });
            char buf[96];
            std::snprintf(buf, sizeof buf, "LOWPASS -3 dB within 5%% of %.0f Hz", target);
            checkNear(lpM, target, tol, buf);
            std::snprintf(buf, sizeof buf, "HIGHPASS -3 dB within 5%% of %.0f Hz", target);
            checkNear(hpM, target, tol, buf);
            std::printf("    extents=%.0f Hz  LP measured=%.1f  HP measured=%.1f  (tol +/-%.1f)\n", target, lpM, hpM, tol);
        }
        juce::ignoreUnused(lp250, lp500, lp2000, hp500, hp2000, hp6000, extraCuts);
    }

    // ---------- Polymeter timing vs host PPQ ----------
    double polyMaxErr = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 32);
        p.set("swing", 0);
        p.set("human", 0);
        p.set("sleevehuman", 0);
        p.set("masterott", 0);
        p.set("output", -6);
        auto pat = blank(2);
        pat.lanes[0].length = 16;
        pat.lanes[0].rate = smother::Rate::Sixteenth;
        pat.lanes[1].length = 5;
        pat.lanes[1].rate = smother::Rate::Sixteenth;
        pat.lanes[1].drum = smother::DrumType::ClosedHat;
        for (int i = 0; i < 16; ++i)
            pat.lanes[0].vel[(size_t)i] = 1;
        for (int i = 0; i < 5; ++i)
            pat.lanes[1].vel[(size_t)i] = 1;
        pat.lanes[0].decay = 0.2f;
        pat.lanes[1].decay = 0.15f;
        p.setPattern(pat);
        p.prepareToPlay(sr, 32);
        p.recordHits(true);
        p.setTestClock(bpm, true);
        // 2 bars = 8 beats = 4 seconds, enough for several 5-step loops (1.25 beats each).
        auto buf = renderClock(p, int(sr * 4), bpm, 32);
        juce::ignoreUnused(buf);
        auto hits = p.lastHits();
        int n0 = 0, n1 = 0;
        for (auto &h : hits)
        {
            const int ln = h.lane;
            const auto &lane = pat.lanes[(size_t)ln];
            const double loop = smother::laneLoopBeats(lane);
            const double expect = std::floor(h.ppq / loop) * loop + smother::stepOnset(lane, ln, h.step, 0, 0);
            const double errBeats = std::abs(h.ppq - expect);
            const double errSamp = errBeats * (60.0 / bpm) * sr;
            polyMaxErr = std::max(polyMaxErr, std::abs(errSamp));
            if (ln == 0)
                ++n0;
            else
                ++n1;
        }
        // 16 steps/bar * 2 bars = 32 on lane 0; lane 1 is 5 steps per 1.25 beats, 8/1.25=6.4 loops * 5 ≈ 32.
        check(n0 >= 30 && n1 >= 28, "polymeter fires both 16-step and 5-step lanes");
        check(polyMaxErr <= 2.0, "polymeter onsets are sample-accurate against host PPQ (≤ 2 samples)");
        std::printf("    poly hits lane0=%d lane1=%d  max err=%.3f samples\n", n0, n1, polyMaxErr);
    }

    // ---------- Swing offsets ----------
    double swingMs = 0, swingExpect = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 32);
        const float swing = 0.5f;
        p.set("swing", swing);
        p.set("human", 0);
        p.set("sleevehuman", 0);
        p.set("masterott", 0);
        p.set("output", -6);
        auto pat = blank(1);
        for (int i = 0; i < 16; ++i)
            pat.lanes[0].vel[(size_t)i] = 1;
        pat.lanes[0].decay = 0.15f;
        p.setPattern(pat);
        p.prepareToPlay(sr, 32);
        p.recordHits(true);
        p.setTestClock(bpm, true);
        renderClock(p, int(sr * 2), bpm, 32);
        auto hits = p.lastHits();
        // Odd steps delayed by swing * 0.5 * stepBeats = 0.5*0.5*0.25 = 0.0625 beats = 31.25 ms at 120.
        swingExpect = swing * 0.5 * 0.25 * (60.0 / bpm) * 1000.0;
        std::vector<double> delays;
        for (auto &h : hits)
        {
            if (h.step < 0 || (h.step % 2) == 0)
                continue;
            const double raw = h.step * 0.25;
            const double delayBeats = h.ppq - (std::floor(h.ppq / 4.0) * 4.0) - raw;
            // Also accept wrapped comparison via stepOnset.
            const double expect = smother::stepOnset(pat.lanes[0], 0, h.step, swing, 0);
            const double loopPos = smother::wrap(h.ppq, 4.0);
            delays.push_back((loopPos - (h.step * 0.25)) * (60.0 / bpm) * 1000.0);
            juce::ignoreUnused(expect, delayBeats);
        }
        if (!delays.empty())
        {
            std::sort(delays.begin(), delays.end());
            swingMs = delays[delays.size() / 2];
        }
        checkNear(swingMs, swingExpect, 2.0, "swing 0.5 delays off-beat 16ths by half of half a step");
        std::printf("    swing measured=%.3f ms  expected=%.3f ms  n=%zu\n", swingMs, swingExpect, delays.size());
    }

    // ---------- MIDI drag-out note times ----------
    double midiMaxErrMs = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 64);
        const float swing = 0.4f;
        p.set("swing", swing);
        p.set("human", 0);
        p.set("output", -6);
        auto pat = blank(2);
        pat.lanes[0].vel[0] = pat.lanes[0].vel[4] = pat.lanes[0].vel[8] = pat.lanes[0].vel[12] = 1;
        pat.lanes[1].length = 5;
        pat.lanes[1].vel[0] = pat.lanes[1].vel[2] = pat.lanes[1].vel[4] = 1;
        pat.lanes[1].midiNote = 42;
        p.setPattern(pat);
        p.setTestClock(bpm, true);
        // Need tempo() to be 120 — process a tiny block so bpm is stored.
        renderClock(p, 64, bpm, 64);
        juce::String err;
        auto f = p.exportMidi(err);
        check(f.existsAsFile() && err.isEmpty(), "MIDI drag-out writes a file");
        juce::FileInputStream is(f);
        juce::MidiFile mf;
        const bool ok = is.openedOk() && mf.readFrom(is);
        check(ok, "MIDI file parses");
        if (ok && mf.getNumTracks() > 0)
        {
            mf.convertTimestampTicksToSeconds();
            auto *tr = mf.getTrack(0);
            for (int i = 0; i < tr->getNumEvents(); ++i)
            {
                const auto msg = tr->getEventPointer(i)->message;
                if (!msg.isNoteOn())
                    continue;
                const double beats = msg.getTimeStamp() * bpm / 60.0;
                // Find closest expected onset across both lanes, several loops.
                double best = 1e9;
                for (int ln = 0; ln < 2; ++ln)
                {
                    const auto &lane = pat.lanes[(size_t)ln];
                    if (msg.getNoteNumber() != lane.midiNote)
                        continue;
                    const double loop = smother::laneLoopBeats(lane);
                    for (int lp = 0; lp < 20; ++lp)
                        for (int s = 0; s < lane.length; ++s)
                        {
                            if (lane.vel[(size_t)s] <= 0)
                                continue;
                            const double t = lp * loop + smother::stepOnset(lane, ln, s, swing, 0);
                            best = std::min(best, std::abs(beats - t));
                        }
                }
                midiMaxErrMs = std::max(midiMaxErrMs, best * (60.0 / bpm) * 1000.0);
            }
        }
        check(midiMaxErrMs <= 0.6, "MIDI drag-out note times match sequencer onsets (≤ 0.6 ms, 960 PPQ rounding)");
        std::printf("    MIDI max onset error=%.4f ms\n", midiMaxErrMs);
        f.copyFileTo(folder.getChildFile("drag-out.mid"));
    }

    // ---------- WAV render matches realtime ----------
    double wavCorr = 0, wavMaxDiff = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 64);
        p.set("swing", 0);
        p.set("human", 0);
        p.set("sleevehuman", 0);
        p.set("output", -3);
        p.set("masterott", 0);
        p.set("masterhp", 20);
        p.set("masterlp", 20000);
        p.set("seq", 1);
        auto pat = blank(1);
        for (int i = 0; i < 16; i += 4)
            pat.lanes[0].vel[(size_t)i] = 1;
        pat.lanes[0].decay = 0.6f;
        p.setPattern(pat);
        auto sinev = sine(int(sr * 2), sr, 220, 0.5);
        p.loadSample(0, sinev.data(), (int)sinev.size(), sr, "sine");
        // Realtime via test clock, 1 bar (4 beats = 2 s). Fresh engine, then both paths.
        p.prepareToPlay(sr, 64);
        p.setTestClock(bpm, true);
        auto live = renderClock(p, int(sr * 2), bpm, 64);
        juce::AudioBuffer<float> rendered;
        p.renderPattern(rendered, sr, bpm, 1);
        const int n = std::min(live.getNumSamples(), rendered.getNumSamples());
        // Lookahead limiter delays by kLookahead; both paths use the same engine so they should align.
        double num = 0, d1 = 0, d2 = 0;
        float md = 0;
        for (int i = 0; i < n; ++i)
        {
            const float a = live.getSample(0, i), b = rendered.getSample(0, i);
            num += a * b;
            d1 += a * a;
            d2 += b * b;
            md = std::max(md, std::abs(a - b));
        }
        wavCorr = (d1 > 0 && d2 > 0) ? num / std::sqrt(d1 * d2) : 0;
        wavMaxDiff = md;
        check(wavCorr > 0.999, "WAV render matches realtime output (correlation > 0.999)");
        check(wavMaxDiff < 0.02, "WAV render max abs difference is small (same engine, block vs offline)");
        std::printf("    wav corr=%.8f  maxDiff=%.6g\n", wavCorr, wavMaxDiff);
        auto f = folder.getChildFile("render-live.wav");
        f.deleteFile();
        auto os = std::make_unique<juce::FileOutputStream>(f);
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.release(), sr, 2, 24, {}, 0));
        if (w)
            w->writeFromAudioSampleBuffer(live, 0, n);
    }

    // ---------- Limiter ceiling ----------
    double limPeak = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 64);
        p.set("output", 6); // +6 dB, will clip the ceiling
        p.set("masterott", 0);
        auto pat = blank(1);
        for (int i = 0; i < 16; ++i)
            pat.lanes[0].vel[(size_t)i] = 1;
        pat.lanes[0].decay = 1;
        pat.lanes[0].level = 1;
        p.setPattern(pat);
        auto s = sine(int(sr * 4), sr, 80, 0.95);
        p.loadSample(0, s.data(), (int)s.size(), sr, "hot");
        p.prepareToPlay(sr, 64);
        p.setTestClock(bpm, true);
        auto buf = renderClock(p, int(sr * 2), bpm, 64);
        limPeak = peak(buf);
        const double ceilDb = 20.0 * std::log10(std::max(limPeak, 1e-12));
        check(limPeak <= smother::kCeiling + 1.0e-5, "limiter ceiling holds at -1 dBFS");
        std::printf("    limiter peak=%.6f (%.4f dBFS), ceiling=%.6f\n", limPeak, ceilDb, smother::kCeiling);
    }

    // ---------- CPU, 16 busy lanes ----------
    double cpuMedian = 0, cpuMin = 0, cpuMax = 0, worstUs = 0;
    {
        SmotherProcessor p;
        setup(p, sr, 256);
        auto pat = blank(16);
        pat.numLanes = 16;
        for (int ln = 0; ln < 16; ++ln)
        {
            auto &l = pat.lanes[(size_t)ln];
            l.drum = (smother::DrumType)(ln % 8);
            l.midiNote = smother::defaultMidi(l.drum, ln / 8);
            l.length = 16;
            l.decay = 0.7f;
            l.ott = 0.3f;
            l.drive = 0.2f;
            l.level = 0.7f;
            for (int s = 0; s < 16; ++s)
                l.vel[(size_t)s] = 1;
            smother::addSleeve(l, smother::slv(4, 4, (smother::SleeveType)(ln % 8), 15, 30, 0.7f, 0.3f));
            smother::addSleeve(l, smother::slv(10, 4, smother::SleeveType::Reverb, 20, 40, 0.4f, 0.4f));
        }
        smother::addAllSleeve(pat, smother::slv(8, 4, smother::SleeveType::Lowpass, 20, 40, 0.5f, 0.3f));
        p.setPattern(pat);
        p.set("masterott", 0.3f);
        p.set("seq", 1);
        p.prepareToPlay(sr, 256);
        p.setTestClock(bpm, true);
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer m;
        // Warmup
        for (int i = 0; i < 32; ++i)
        {
            b.clear();
            p.processBlock(b, m);
        }
        std::vector<double> runs;
        const int blocks = int(sr / 256 * 2); // 2 seconds
        for (int r = 0; r < 5; ++r)
        {
            const auto t0 = std::chrono::steady_clock::now();
            double worst = 0;
            for (int i = 0; i < blocks; ++i)
            {
                b.clear();
                const auto a = std::chrono::steady_clock::now();
                p.processBlock(b, m);
                const auto z = std::chrono::steady_clock::now();
                const double us = std::chrono::duration<double, std::micro>(z - a).count();
                worst = std::max(worst, us);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double sec = std::chrono::duration<double>(t1 - t0).count();
            const double audio = 2.0;
            const double pct = 100.0 * sec / audio;
            runs.push_back(pct);
            worstUs = std::max(worstUs, worst);
        }
        std::sort(runs.begin(), runs.end());
        cpuMedian = runs[2];
        cpuMin = runs.front();
        cpuMax = runs.back();
        check(cpuMedian < 10.0, "CPU with 16 busy lanes is under 10% of one core");
        std::printf("    CPU median=%.3f%%  min=%.3f  max=%.3f  worst block=%.1f us\n", cpuMedian, cpuMin,
                    cpuMax, worstUs);
    }

    // ---------- Stress: many rates/blocks, no NaNs, state round-trip, MIDI trigger, presets ----------
    long long bad = 0, denorm = 0;
    float stressPeak = 0;
    for (double r : { 44100., 48000., 96000. })
        for (int bs : { 1, 17, 64, 256, 1024 })
        {
            SmotherProcessor p;
            setup(p, r, bs);
            p.factory(3);
            p.set("output", 0);
            p.setTestClock(140, true);
            juce::AudioBuffer<float> b(2, bs);
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)100), 0);
            for (int i = 0; i < 8; ++i)
            {
                b.clear();
                if (i == 3)
                {
                    m.clear();
                    m.addEvent(juce::MidiMessage::noteOn(1, 38, (juce::uint8)90), 0);
                }
                else if (i == 4)
                    m.clear();
                p.processBlock(b, m);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < bs; ++s)
                    {
                        const float x = b.getSample(ch, s);
                        if (!std::isfinite(x))
                            ++bad;
                        else
                        {
                            stressPeak = std::max(stressPeak, std::abs(x));
                            if (x != 0 && std::abs(x) < std::numeric_limits<float>::min())
                                ++denorm;
                        }
                    }
            }
            juce::MemoryBlock mb;
            p.getStateInformation(mb);
            SmotherProcessor p2;
            setup(p2, r, bs);
            p2.setStateInformation(mb.getData(), (int)mb.getSize());
            check(p2.pattern().numLanes == p.pattern().numLanes, "state restores lane count");
        }
    check(bad == 0, "no non-finite samples across rates and block sizes");
    std::printf("    stress peak=%.4f  nonfinite=%lld  denorm=%lld\n", stressPeak, bad, denorm);

    // Factory presets all produce audio and have unique names.
    {
        juce::StringArray names = SmotherProcessor::presetNames();
        check(names.size() >= 20, "at least 20 factory presets");
        juce::StringArray uniq(names);
        uniq.removeEmptyStrings();
        // uniqueness
        bool unique = true;
        for (int i = 0; i < names.size(); ++i)
            for (int j = i + 1; j < names.size(); ++j)
                if (names[i] == names[j])
                    unique = false;
        check(unique, "preset names are unique");
        int silent = 0;
        for (int i = 0; i < names.size(); ++i)
        {
            SmotherProcessor p;
            setup(p, 48000, 128);
            p.factory(i);
            p.set("output", 0);
            p.prepareToPlay(48000, 128);
            p.setTestClock(120, true);
            auto b = renderClock(p, 48000, 120, 128); // 1 second
            if (rms(b) < 1.0e-5f)
                ++silent;
        }
        check(silent == 0, "every factory preset produces audio");
        std::printf("    presets=%d  silent=%d\n", names.size(), silent);
    }

    // Editor constructs and snapshots (for the screenshot path smoke).
    {
        SmotherProcessor p;
        setup(p, 48000, 256);
        p.factory(0);
        auto ed = std::make_unique<SmotherEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        check(img.getWidth() >= 900 && img.getHeight() >= 500, "editor snapshot has a real size");
        auto f = folder.getChildFile("editor-smoke.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // JSON summary for the report.
    {
        juce::File j = folder.getChildFile("results.json");
        juce::FileOutputStream os(j);
        os.setPosition(0);
        os.truncate();
        os << "{\n"
           << "  \"failures\": " << failures << ",\n"
           << "  \"mute_db\": " << muteDb << ",\n"
           << "  \"attack_ms\": " << attMs << ",\n"
           << "  \"release_ms\": " << relMs << ",\n"
           << "  \"max_jump\": " << maxJump << ",\n"
           << "  \"lp_cutoff_hz\": " << lpCutoff << ",\n"
           << "  \"hp_cutoff_hz\": " << hpCutoff << ",\n"
           << "  \"poly_max_err_samples\": " << polyMaxErr << ",\n"
           << "  \"swing_ms\": " << swingMs << ",\n"
           << "  \"swing_expect_ms\": " << swingExpect << ",\n"
           << "  \"midi_max_err_ms\": " << midiMaxErrMs << ",\n"
           << "  \"wav_corr\": " << wavCorr << ",\n"
           << "  \"wav_max_diff\": " << wavMaxDiff << ",\n"
           << "  \"limiter_peak\": " << limPeak << ",\n"
           << "  \"limiter_ceiling\": " << smother::kCeiling << ",\n"
           << "  \"cpu_median\": " << cpuMedian << ",\n"
           << "  \"cpu_min\": " << cpuMin << ",\n"
           << "  \"cpu_max\": " << cpuMax << ",\n"
           << "  \"cpu_worst_block_us\": " << worstUs << ",\n"
           << "  \"stress_peak\": " << stressPeak << ",\n"
           << "  \"nonfinite\": " << bad << ",\n"
           << "  \"denorm\": " << denorm << "\n"
           << "}\n";
    }
    return failures;
}
} // namespace

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf(stderr, "SmotherTest test folder | screenshot file [presetIndex] | renderpat output "
                             "preset [seconds]\n");
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
        SmotherProcessor p;
        setup(p);
        p.factory(argc > 3 ? std::atoi(argv[3]) : 0);
        auto ed = std::make_unique<SmotherEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{ imageFile };
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    if (mode == "renderpat" && argc >= 4)
    {
        SmotherProcessor p;
        setup(p, 48000, 256);
        p.factory(std::atoi(argv[3]));
        const double seconds = argc > 4 ? std::atof(argv[4]) : 8.0;
        juce::AudioBuffer<float> b;
        const int loops = std::max(1, int(std::ceil(seconds / 2.0)));
        p.renderPattern(b, 48000, 120, loops);
        const int keep = std::min(b.getNumSamples(), int(seconds * 48000));
        juce::File outputFile(argv[2]);
        outputFile.deleteFile();
        auto out = std::make_unique<juce::FileOutputStream>(outputFile);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(out.release(), 48000, 2, 24, {}, 0));
        return writer && writer->writeFromAudioSampleBuffer(b, 0, keep) ? 0 : 4;
    }
    return 2;
}
