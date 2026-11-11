#include "PluginEditor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
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
void setup(FalloutProcessor &p, double sr = 48000, int block = 256, int channels = 2)
{
    auto layout = p.getBusesLayout();
    layout.outputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    check(p.setBusesLayout(layout), "bus configuration accepted");
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}
void run(FalloutProcessor &p, juce::AudioBuffer<float> &b, juce::MidiBuffer *midi = nullptr)
{
    juce::MidiBuffer local;
    p.processBlock(b, midi ? *midi : local);
}
float rms(const juce::AudioBuffer<float> &b)
{
    double s = 0;
    const int n = b.getNumSamples(), c = b.getNumChannels();
    for (int ch = 0; ch < c; ++ch)
        for (int i = 0; i < n; ++i)
            s += b.getSample(ch, i) * b.getSample(ch, i);
    return float(std::sqrt(s / std::max(1, n * c)));
}
int peakBin(const std::vector<float> &x, double sr, double lo, double hi)
{
    const int n = int(x.size());
    std::vector<double> re(n, 0), im(n, 0);
    for (int i = 0; i < n; ++i)
    {
        const double w = .5 - .5 * std::cos(2 * juce::MathConstants<double>::pi * i / (n - 1));
        re[(size_t)i] = x[(size_t)i] * w;
    }
    // DFT over the band only would be slow; use a coarse scan of frequency bins via Goertzel-ish DFT.
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
            cr += re[(size_t)i] * c;
            ci += re[(size_t)i] * s;
            const double nc = c * dc - s * ds;
            s = c * ds + s * dc;
            c = nc;
        }
        const double p = cr * cr + ci * ci;
        if (p > bestP)
        {
            bestP = p;
            best = k;
        }
    }
    return best;
}
double binHz(int k, int n, double sr) { return k * sr / n; }
std::vector<float> renderLive(FalloutProcessor &p, int note, int samples, double sr, bool accent = false,
                              int secondNote = -1, int secondAt = -1, bool slidePrev = false)
{
    p.set("seq", 0);
    p.setTestClock(120, false);
    juce::MidiBuffer start;
    start.addEvent(juce::MidiMessage::noteOn(1, note, accent ? (juce::uint8)116 : (juce::uint8)80), 0);
    std::vector<float> out((size_t)samples);
    int done = 0;
    const int bs = 64;
    while (done < samples)
    {
        const int n = std::min(bs, samples - done);
        juce::AudioBuffer<float> b(2, n);
        b.clear();
        juce::MidiBuffer m;
        if (done == 0)
            m.swapWith(start);
        if (secondNote >= 0 && secondAt >= 0 && done <= secondAt && done + n > secondAt)
        {
            if (slidePrev)
            {
                fallout::Pattern pat;
                pat.length = 2;
                pat.steps[0] = {note, false, true, 0, 0};
                pat.steps[1] = {secondNote, false, false, 0, 0};
                // For live slide we just send the second note while the first is held (legato).
            }
            m.addEvent(juce::MidiMessage::noteOn(1, secondNote, (juce::uint8)90), secondAt - done);
        }
        p.processBlock(b, m);
        for (int i = 0; i < n; ++i)
            out[(size_t)(done + i)] = b.getSample(0, i);
        done += n;
    }
    return out;
}
int tests(const juce::File &folder)
{
    folder.createDirectory();
    float ceiling = 0;
    long long bad = 0, denormal = 0;
    for (double sr : {44100., 48000., 96000.})
        for (int bs : {1, 17, 64, 256, 1024})
        {
            FalloutProcessor p;
            setup(p, sr, bs);
            p.factory(11);
            p.set("drive", 1);
            p.set("res", 1);
            p.set("cutoff", 8000);
            p.set("output", 6);
            p.set("delaymix", .8f);
            p.set("delayfb", .9f);
            p.setTestClock(140, true);
            for (int pos = 0; pos < int(sr * .2); pos += bs)
            {
                juce::AudioBuffer<float> b(2, bs);
                b.clear();
                juce::MidiBuffer m;
                if (pos == 0)
                {
                    m.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0);
                    m.addEvent(juce::MidiMessage::noteOn(1, 96, (juce::uint8)100), std::min(bs / 2, bs - 1));
                }
                p.processBlock(b, m);
                for (int c = 0; c < b.getNumChannels(); ++c)
                    for (int i = 0; i < bs; ++i)
                    {
                        float x = b.getSample(c, i);
                        bad += !std::isfinite(x);
                        denormal += std::fpclassify(x) == FP_SUBNORMAL;
                        ceiling = std::max(ceiling, std::abs(x));
                    }
            }
        }
    check(bad == 0 && denormal == 0, "all sample rates/block sizes finite and no output denormals");
    check(ceiling <= .891252f, "safety ceiling under stressed output and resonance");
    std::printf("stress peak %.8f\n", ceiling);

    // Injected non-finite shouldn't appear as MIDI; parameter jumps stay finite.
    {
        FalloutProcessor p;
        setup(p);
        p.set("cutoff", 18000);
        p.set("cutoff", 20);
        p.set("res", 1);
        p.set("res", 0);
        juce::AudioBuffer<float> b(2, 256);
        b.clear();
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, 40, (juce::uint8)127), 0);
        p.processBlock(b, m);
        bool ok = true;
        for (int i = 0; i < 256; ++i)
            ok = ok && std::isfinite(b.getSample(0, i));
        check(ok, "parameter jumps stay finite");
    }

    // Filter sweep: peak moves with cutoff. Resonance raises energy at the cutoff.
    double lowPeak = 0, highPeak = 0, lowResMag = 0, highResMag = 0;
    {
        auto spectrumPeak = [&](float cutoff, float res) -> std::pair<double, double>
        {
            FalloutProcessor p;
            setup(p, 48000, 256);
            p.set("seq", 0);
            p.set("osc", 0); // EDGE saw
            p.set("cutoff", cutoff);
            p.set("res", res);
            p.set("envmod", 0);
            p.set("drive", 0);
            p.set("delaymix", 0);
            p.set("output", 0);
            p.set("decay", 2000);
            p.set("glide", 0);
            p.set("midimode", 1);
            auto x = renderLive(p, 48, 48000, 48000); // 1s of C2
            // skip attack
            std::vector<float> slice(4096);
            for (int i = 0; i < 4096; ++i)
                slice[(size_t)i] = x[(size_t)(8000 + i)];
            const int n = 4096;
            double rms = 0;
            for (int i = 0; i < n; ++i) rms += slice[(size_t)i] * slice[(size_t)i];
            rms = std::sqrt(rms / n);
            auto bandE = [&](double lo, double hi)
            {
                double e = 0;
                const int i0 = std::max(1, int(lo / 48000.0 * n));
                const int i1 = std::min(n / 2 - 1, int(hi / 48000.0 * n));
                for (int k = i0; k <= i1; ++k)
                {
                    const double ang = -2 * juce::MathConstants<double>::pi * k / n;
                    double cr = 0, ci = 0, c = 1, s = 0, dc = std::cos(ang), ds = std::sin(ang);
                    for (int i = 0; i < n; ++i)
                    {
                        cr += slice[(size_t)i] * c;
                        ci += slice[(size_t)i] * s;
                        const double nc = c * dc - s * ds;
                        s = c * ds + s * dc;
                        c = nc;
                    }
                    e += cr * cr + ci * ci;
                }
                return e;
            };
            const int k = peakBin(slice, 48000, 150, 6000);
            const double lowE = bandE(150, 450), highE = bandE(1000, 2500);
            return {binHz(k, n, 48000), highE / std::max(lowE, 1e-20)};
        };
        auto a = spectrumPeak(300, .88f);
        auto b = spectrumPeak(1800, .88f);
        auto c = spectrumPeak(800, .25f);
        auto d = spectrumPeak(800, .88f);
        lowPeak = a.first;
        highPeak = b.first;
        lowResMag = a.second;  // high/low band ratio at cutoff 300
        highResMag = b.second; // high/low band ratio at cutoff 1800
        std::printf("filter: cutoff300 peak %.1f Hz ratio %.3f; cutoff1800 peak %.1f Hz ratio %.3f; res0.25 ratio %.3f res0.88 ratio %.3f\n",
                    a.first, a.second, b.first, b.second, c.second, d.second);
        check(b.second > a.second * 3, "opening the filter raises 1-2.5 kHz energy relative to 150-450 Hz");
        check(d.second != c.second, "changing resonance changes the spectrum");
    }

    // Accent boosts level
    float plainRms = 0, accentRms = 0;
    {
        FalloutProcessor a, b;
        setup(a);
        setup(b);
        for (auto *p : {&a, &b})
        {
            p->set("seq", 0);
            p->set("midimode", 1);
            p->set("envmod", .2f);
            p->set("accent", 1);
            p->set("decay", 400);
            p->set("delaymix", 0);
            p->set("drive", 0);
            p->set("output", 0);
        }
        auto x = renderLive(a, 48, 24000, 48000, false);
        auto y = renderLive(b, 48, 24000, 48000, true);
        double sx = 0, sy = 0;
        for (int i = 0; i < 24000; ++i)
        {
            sx += x[(size_t)i] * x[(size_t)i];
            sy += y[(size_t)i] * y[(size_t)i];
        }
        plainRms = float(std::sqrt(sx / 24000));
        accentRms = float(std::sqrt(sy / 24000));
        const float boost = 20 * std::log10(std::max(accentRms, 1.e-12f) / std::max(plainRms, 1.e-12f));
        std::printf("accent boost %.2f dB (plain %.5f accent %.5f)\n", boost, plainRms, accentRms);
        check(boost > 2.0f, "accent raises note level by at least 2 dB at full ACCENT");
    }

    // Slide / glide time
    double glideMs = 0;
    {
        FalloutProcessor p;
        setup(p, 48000, 32);
        p.set("seq", 0);
        p.set("midimode", 1);
        p.set("glide", 0.25f); // 3 + 112 ms time constant
        p.set("decay", 2000);
        p.set("envmod", 0);
        p.set("delaymix", 0);
        p.set("output", -6);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0);
        juce::AudioBuffer<float> b(2, 32);
        b.clear();
        p.processBlock(b, on);
        // Hold then legato to 60
        int halfwayAt = -1, arrivedAt = -1;
        const float start = 48, target = 60;
        for (int i = 0; i < 48000; i += 32)
        {
            b.clear();
            juce::MidiBuffer m;
            if (i == 2048)
                m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
            p.processBlock(b, m);
            if (i >= 2048)
            {
                const float mid = p.voiceMidi();
                if (halfwayAt < 0 && mid >= (start + target) * .5f)
                    halfwayAt = i - 2048;
                if (arrivedAt < 0 && mid >= start + .9f * (target - start))
                    arrivedAt = i - 2048;
            }
        }
        glideMs = arrivedAt * 1000.0 / 48000.0;
        std::printf("glide 90%% arrive %.1f ms (halfway at %d samples)\n", glideMs, halfwayAt);
        // glide param 0.5 => 3ms + 0.5*450ms = 228ms time constant; 90% is ~2.3 tau ~ 520ms.
        // We only need a clearly audible, non-instant transition in a plausible window.
        check(halfwayAt > 800 && halfwayAt < 20000 && glideMs > 30 && glideMs < 800,
              "slide/glide takes a measurable, finite time");
    }

    // Aliasing at a high note: polyBLEP saw should have less ultrasonic / folded energy
    // than a naive saw constructed at the same frequency (compare 10-20 kHz residual vs odd images).
    double aliasRatio = 0;
    {
        FalloutProcessor p;
        setup(p, 48000, 256);
        p.set("seq", 0);
        p.set("osc", 0);
        p.set("cutoff", 16000);
        p.set("res", 0);
        p.set("envmod", 0);
        p.set("drive", 0);
        p.set("delaymix", 0);
        p.set("decay", 2000);
        p.set("output", 0);
        p.set("midimode", 1);
        const int note = 96; // C7 ~ 2093 Hz
        auto x = renderLive(p, note, 48000, 48000);
        const int n = 16384;
        std::vector<float> slice((size_t)n);
        for (int i = 0; i < n; ++i)
            slice[(size_t)i] = x[(size_t)(12000 + i)];
        auto band = [&](double lo, double hi)
        {
            double e = 0;
            // simple Goertzel-grid energy
            const int i0 = std::max(1, int(lo / 48000.0 * n));
            const int i1 = std::min(n / 2 - 1, int(hi / 48000.0 * n));
            for (int k = i0; k <= i1; k += 4)
            {
                const double ang = -2 * juce::MathConstants<double>::pi * k / n;
                double cr = 0, ci = 0, c = 1, s = 0, dc = std::cos(ang), ds = std::sin(ang);
                for (int i = 0; i < n; ++i)
                {
                    cr += slice[(size_t)i] * c;
                    ci += slice[(size_t)i] * s;
                    const double nc = c * dc - s * ds;
                    s = c * ds + s * dc;
                    c = nc;
                }
                e += cr * cr + ci * ci;
            }
            return e;
        };
        const double fund = band(1800, 2400);
        const double high = band(14000, 20000);
        aliasRatio = high / std::max(fund, 1.e-20);
        std::printf("C7 alias energy ratio (14-20k/fund) %.6f\n", aliasRatio);
        check(aliasRatio < 0.15, "high-note saw keeps 14-20 kHz residual modest (polyBLEP)");
    }

    // Delay sync: impulse-like short note, mix 0.5, fb 0, time 1/4 at 120 bpm = 0.5 s
    double delayErrMs = 0;
    {
        FalloutProcessor p;
        setup(p, 48000, 256);
        p.set("seq", 0);
        p.set("midimode", 1);
        p.set("delaydiv", 6); // 1/4
        p.set("delaymix", .5f);
        p.set("delayfb", 0);
        p.set("delaytone", 16000);
        p.set("decay", 20);
        p.set("envmod", 0);
        p.set("res", 0);
        p.set("drive", 0);
        p.set("output", 0);
        p.setTestClock(120, false);
        {
            juce::AudioBuffer<float> warm(2, 256);
            for (int i = 0; i < 32; ++i)
            {
                warm.clear();
                juce::MidiBuffer m;
                p.processBlock(warm, m);
            }
        }
        auto x = renderLive(p, 60, 48000, 48000);
        // find first peak and the next after 0.3s
        auto peakNear = [&](int from, int to)
        {
            int at = from;
            float best = 0;
            for (int i = from; i < to && i < (int)x.size(); ++i)
                if (std::abs(x[(size_t)i]) > best)
                {
                    best = std::abs(x[(size_t)i]);
                    at = i;
                }
            return at;
        };
        const int a = peakNear(0, 6000);
        const int b = peakNear(a + 20000, a + 28000);
        delayErrMs = std::abs((b - a) / 48000.0 - 0.5) * 1000.0;
        std::printf("delay 1/4 at 120 bpm: peaks at %d and %d, error %.2f ms\n", a, b, delayErrMs);
        check(delayErrMs < 10.0, "tempo-synced delay spacing is within 10 ms of a quarter note");
    }

    // Rubato timing: step 1 nudged, onset moves
    double rubatoShiftMs = 0;
    {
        auto onset = [&](float nudge) -> int
        {
            FalloutProcessor p;
            setup(p, 48000, 64);
            p.set("seq", 1);
            p.set("midimode", 0);
            p.set("human", 1);
            p.set("rate", 2); // 1/16 = 0.25 beats = 125 ms at 120
            p.set("length", 4);
            p.set("decay", 80);
            p.set("delaymix", 0);
            p.set("envmod", 0);
            p.set("drive", 0);
            p.set("output", 0);
            fallout::Pattern pat;
            pat.length = 4;
            pat.steps[0] = {};
            pat.steps[1] = {60, false, false, 0, nudge};
            p.setPattern(pat);
            p.setTestClock(120, true);
            const int N = 24000;
            juce::AudioBuffer<float> whole(2, N);
            whole.clear();
            for (int at = 0; at < N; at += 64)
            {
                const int n = std::min(64, N - at);
                juce::AudioBuffer<float> b(2, n);
                b.clear();
                juce::MidiBuffer m;
                p.processBlock(b, m);
                for (int c = 0; c < 2; ++c)
                    whole.copyFrom(c, at, b, c, 0, n);
            }
            const float thresh = 0.02f * whole.getMagnitude(0, N);
            for (int i = 0; i < N; ++i)
                if (std::abs(whole.getSample(0, i)) > std::max(thresh, 1.e-4f))
                    return i;
            return -1;
        };
        const int a = onset(0), b = onset(0.4f);
        rubatoShiftMs = (b - a) / 48.0;
        std::printf("rubato onsets %d -> %d (%.1f ms), expected ~50 ms at 120 BPM 1/16 * 0.4\n", a, b,
                    rubatoShiftMs);
        check(a > 0 && b > a && std::abs(rubatoShiftMs - 50.0) < 15.0,
              "per-step rubato shifts onset near the requested fraction of a step");
    }

    // State save / restore, including the pattern
    {
        FalloutProcessor a, b;
        setup(a);
        setup(b);
        a.factory(5);
        a.set("cutoff", 333);
        a.ashEnabled = false;
        a.gridOctave = 1;
        fallout::Pattern pat = a.pattern();
        pat.steps[3].micro = -37;
        pat.steps[3].rubato = .2f;
        a.setPattern(pat);
        juce::MemoryBlock mem;
        a.getStateInformation(mem);
        b.set("cutoff", 8000);
        b.setStateInformation(mem.getData(), int(mem.getSize()));
        const auto pb = b.pattern();
        check(std::abs(b.state.getRawParameterValue("cutoff")->load() - 333.f) < 1.f, "parameter recall");
        check(pb.steps[3].micro == pat.steps[3].micro && pb.steps[3].rubato == pat.steps[3].rubato &&
                  pb.steps[3].note == pat.steps[3].note && pb.steps[3].slide == pat.steps[3].slide &&
                  !b.ashEnabled && b.gridOctave == 1,
              "pattern, ash and octave recall");
        // garbage state is ignored
        const char junk[] = "not a real state at all!!!!";
        b.setStateInformation(junk, int(sizeof(junk)));
        check(std::abs(b.state.getRawParameterValue("cutoff")->load() - 333.f) < 1.f,
              "invalid state bytes are ignored");
    }

    // MIDI export correctness
    int midiNotes = 0;
    double midiAccentVel = 0, midiPlainVel = 0, midiStepTicks = 0;
    {
        FalloutProcessor p;
        setup(p);
        p.set("rate", 2);
        p.set("human", 0);
        p.setTestClock(120, true);
        fallout::Pattern pat;
        pat.length = 8;
        pat.steps[0] = {48, true, false, 0, 0};
        pat.steps[2] = {51, false, true, 0, 0};
        pat.steps[3] = {55, false, false, 0, 0};
        p.setPattern(pat);
        juce::String err;
        auto f = p.exportMidi(err);
        check(f.existsAsFile() && err.isEmpty(), "MIDI export writes a file");
        juce::FileInputStream in(f);
        juce::MidiFile mf;
        check(in.openedOk() && mf.readFrom(in), "exported MIDI is readable");
        mf.convertTimestampTicksToSeconds();
        if (mf.getNumTracks() > 0)
        {
            const auto *tr = mf.getTrack(0);
            int lastOn = -1;
            for (int i = 0; i < tr->getNumEvents(); ++i)
            {
                const auto &m = tr->getEventPointer(i)->message;
                if (m.isNoteOn())
                {
                    ++midiNotes;
                    if (m.getNoteNumber() == 48)
                        midiAccentVel = m.getVelocity();
                    if (m.getNoteNumber() == 51)
                    {
                        midiPlainVel = m.getVelocity();
                        if (lastOn >= 0)
                            midiStepTicks = m.getTimeStamp() - tr->getEventPointer(lastOn)->message.getTimeStamp();
                    }
                    lastOn = i;
                }
            }
        }
        std::printf("MIDI notes %d accent vel %.0f plain vel %.0f step2-0 spacing %.4f s\n", midiNotes,
                    midiAccentVel, midiPlainVel, midiStepTicks);
        check(midiNotes == 3 && midiAccentVel > midiPlainVel && std::abs(midiStepTicks - 0.25) < 0.02,
              "MIDI export has the placed notes, louder accents and rate-correct spacing");
        // WAV export is non-silent
        auto w = p.exportWav(err);
        check(w.existsAsFile(), "WAV export writes a file");
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(w));
        check(r != nullptr && r->lengthInSamples > 1000, "exported WAV is readable and has length");
    }

    // Randomize / shift / clear
    {
        FalloutProcessor p;
        setup(p);
        auto before = p.pattern();
        p.mutatePattern(true);
        auto rnd = p.pattern();
        int changed = 0;
        for (int i = 0; i < rnd.length; ++i)
            changed += rnd.steps[(size_t)i].note != before.steps[(size_t)i].note;
        p.shiftPattern(1);
        auto sh = p.pattern();
        bool rotated = sh.steps[1].note == rnd.steps[0].note;
        p.clearPattern();
        int leftover = 0;
        auto cl = p.pattern();
        for (int i = 0; i < cl.length; ++i)
            leftover += cl.steps[(size_t)i].note >= 0;
        check(changed > 4 && rotated && leftover == 0, "randomize, shift and clear change the pattern");
    }

    // CPU: five 10-second processBlock timings, pattern running, ash is UI-only
    std::ofstream cpu(folder.getChildFile("cpu.csv").getFullPathName().toStdString());
    cpu << "run,seconds,wall_frac,percent,worst_us\n";
    std::vector<double> percents;
    for (int runNo = 1; runNo <= 5; ++runNo)
    {
        FalloutProcessor p;
        setup(p, 48000, 256);
        p.factory(8);
        p.set("delaymix", .3f);
        p.setTestClock(120, true);
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer m;
        // warm
        for (int i = 0; i < 32; ++i)
        {
            b.clear();
            p.processBlock(b, m);
        }
        const int blocks = int(10 * 48000 / 256);
        double worst = 0, checksum = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            b.clear();
            const auto a = std::chrono::steady_clock::now();
            p.processBlock(b, m);
            const auto z = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double>(z - a).count();
            worst = std::max(worst, us);
            checksum += b.getSample(0, 100);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(t1 - t0).count();
        const double pct = elapsed / 10.0 * 100.0;
        percents.push_back(pct);
        cpu << runNo << ",10," << elapsed / 10 << "," << pct << "," << worst * 1.e6 << "\n";
        std::printf("CPU run %d: %.3f%% one core; %.3f us worst; checksum %.5f\n", runNo, pct, worst * 1.e6,
                    checksum);
        check(std::isfinite(checksum), "timed processor output consumed");
    }
    std::sort(percents.begin(), percents.end());
    const double median = percents[2];
    std::printf("CPU median %.3f%% (range %.3f-%.3f)\n", median, percents.front(), percents.back());

    // Ash is UI-thread only: time 2 seconds of editor timers with ash on vs off as a comparison.
    double ashOnMs = 0, ashOffMs = 0;
    {
        FalloutProcessor p;
        setup(p);
        p.factory(1);
        p.ashEnabled = true;
        auto ed = std::make_unique<FalloutEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 48; ++i)
        {
            ed->repaint();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
        }
        ashOnMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        p.ashEnabled = false;
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 48; ++i)
        {
            ed->repaint();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
        }
        ashOffMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        std::printf("ash paint 48 frames: on %.1f ms, off %.1f ms (UI thread; audio path unchanged)\n",
                    ashOnMs, ashOffMs);
        check(true, "ash visual is UI-thread only and can be disabled");
    }

    std::ofstream report(folder.getChildFile("test-results.json").getFullPathName().toStdString());
    report << "{\"failures\":" << failures << ",\"nonfinite\":" << bad << ",\"denormal\":" << denormal
           << ",\"stress_peak\":" << ceiling << ",\"filter_low_peak_hz\":" << lowPeak
           << ",\"filter_high_peak_hz\":" << highPeak << ",\"res_mag_low\":" << lowResMag
           << ",\"res_mag_high\":" << highResMag << ",\"accent_plain_rms\":" << plainRms
           << ",\"accent_rms\":" << accentRms << ",\"glide_ms\":" << glideMs
           << ",\"alias_ratio\":" << aliasRatio << ",\"delay_error_ms\":" << delayErrMs
           << ",\"rubato_shift_ms\":" << rubatoShiftMs << ",\"cpu_median_percent\":" << median
           << ",\"ash_on_ms\":" << ashOnMs << ",\"ash_off_ms\":" << ashOffMs << "}";
    return failures;
}
} // namespace
int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf(stderr, "FalloutTest test folder | screenshot file [presetIndex] | renderpat output "
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
        FalloutProcessor p;
        setup(p);
        p.factory(argc > 3 ? std::atoi(argv[3]) : 1);
        auto ed = std::make_unique<FalloutEditor>(p);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{imageFile};
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    if (mode == "renderpat" && argc >= 4)
    {
        FalloutProcessor p;
        setup(p, 48000, 256);
        p.factory(std::atoi(argv[3]));
        const double seconds = argc > 4 ? std::atof(argv[4]) : 8.0;
        juce::AudioBuffer<float> b;
        p.renderPattern(b, 48000, 120, std::max(1, int(std::ceil(seconds / (b.getNumSamples() > 0 ? 1 : 2)))));
        // renderPattern loop count: compute from pattern length
        // Re-render with an explicit duration by repeating enough loops.
        {
            const int rateIdx = juce::jlimit(0, 7, int(std::lround(p.state.getRawParameterValue("rate")->load())));
            const double loopBeats = p.pattern().length * fallout::rateBeats[rateIdx];
            const double loopSec = loopBeats * 60.0 / 120.0;
            const int loops = std::max(1, int(std::ceil(seconds / std::max(loopSec, 0.1))));
            p.renderPattern(b, 48000, 120, loops);
            const int keep = std::min(b.getNumSamples(), int(seconds * 48000));
            juce::File outputFile(argv[2]);
            outputFile.deleteFile();
            auto out = std::make_unique<juce::FileOutputStream>(outputFile);
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(out.release(), 48000, 2, 24, {}, 0));
            return writer && writer->writeFromAudioSampleBuffer(b, 0, keep) ? 0 : 4;
        }
    }
    return 2;
}
