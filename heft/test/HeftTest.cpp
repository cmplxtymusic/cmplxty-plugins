#include "PluginEditor.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
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
void setup(HeftProcessor &p, double sr = 48000, int block = 256, bool sc = false, int channels = 2)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0, layout.inputBuses[0]);
    layout.inputBuses.set(1, sc ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    check(p.setBusesLayout(layout), "bus configuration accepted");
    p.setRateAndBufferSizeDetails(sr, block);
    p.prepareToPlay(sr, block);
}
void warm(HeftProcessor &p, int n = 24000)
{
    for (int i = 0; i < n; i += 256)
    {
        juce::AudioBuffer<float> b(p.getTotalNumInputChannels(), 256);
        b.clear();
        juce::MidiBuffer m;
        p.processBlock(b, m);
    }
}
float sine(int n, double sr = 48000, double f = 220)
{
    return .3f * float(std::sin(2 * juce::MathConstants<double>::pi * f * n / sr));
}
int tests(const juce::File &folder)
{
    float ceiling = 0;
    long long bad = 0, denormal = 0;
    float maxJumpError = 0, maxJumpStep = 0;
    for (double sr : {44100., 48000., 96000.})
        for (int bs : {1, 17, 64, 256, 1024})
        {
            HeftProcessor p;
            setup(p, sr, bs, true);
            p.factory(4);
            p.set("fuzz", 1);
            p.set("smear", 1);
            p.set("shift", 400);
            p.set("output", 6);
            p.set("mod0src", 4);
            p.set("mod0dst", 0);
            p.set("mod0amt", 1);
            for (int pos = 0; pos < int(sr * .12); pos += bs)
            {
                juce::AudioBuffer<float> b(4, bs);
                for (int i = 0; i < bs; ++i)
                {
                    b.setSample(0, i, 4 * sine(pos + i, sr));
                    b.setSample(1, i, 3 * sine(pos + i, sr, 311));
                    b.setSample(2, i, sine(pos + i, sr, 50));
                    b.setSample(3, i, 0);
                }
                juce::MidiBuffer m;
                p.processBlock(b, m);
                for (int c = 0; c < 2; ++c)
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
    check(ceiling <= .891252f, "safety ceiling under stressed input and +6 dB output");
    {
        HeftProcessor p;
        setup(p);
        p.factory(0);
        p.set("mix", 0);
        warm(p);
        juce::AudioBuffer<float> b(2, 512);
        b.clear();
        b.setSample(0, 100, .5f);
        b.setSample(1, 100, .5f);
        juce::MidiBuffer m;
        p.processBlock(b, m);
        float error = 0;
        for (int i = 0; i < 512; ++i)
            error = std::max(error, std::abs(b.getSample(0, i) - (i == 164 ? .5f : 0.f)));
        check(error < 1.e-6f && p.getLatencySamples() == 64,
              "mix zero is unity dry with exact reported latency");
    }
    {
        HeftProcessor a, b;
        setup(a);
        setup(b);
        a.factory(0);
        b.factory(0);
        b.set("wide", 1);
        warm(a);
        warm(b);
        float mono = 0, side = 0;
        for (int j = 0; j < 100; ++j)
        {
            juce::AudioBuffer<float> x(2, 256), y(2, 256);
            for (int i = 0; i < 256; ++i)
            {
                float v = sine(j * 256 + i);
                x.setSample(0, i, v);
                x.setSample(1, i, v);
            }
            y.makeCopyOf(x);
            juce::MidiBuffer m;
            a.processBlock(x, m);
            b.processBlock(y, m);
            for (int i = 0; i < 256; ++i)
            {
                mono = std::max(mono,
                                std::abs(.5f * (y.getSample(0, i) + y.getSample(1, i)) - x.getSample(0, i)));
                side = std::max(side, std::abs(y.getSample(0, i) - y.getSample(1, i)));
            }
        }
        std::printf("width mono null %.9g; side peak %.6f\n", mono, side);
        check(mono < 1.e-6f && side > 1.e-4f, "width creates side while centre/mono nulls");
    }
    {
        HeftProcessor a, b;
        setup(a, 48000, 256, true);
        setup(b, 48000, 256, true);
        a.factory(8);
        b.factory(8);
        warm(a);
        warm(b);
        double diff = 0;
        float silentScLeak = 0;
        for (int j = 0; j < 150; ++j)
        {
            juce::AudioBuffer<float> x(4, 256), y(4, 256);
            x.clear();
            y.clear();
            for (int i = 0; i < 256; ++i)
            {
                x.setSample(0, i, sine(j * 256 + i));
                x.setSample(1, i, sine(j * 256 + i));
                x.setSample(2, i, .8f);
            }
            y.makeCopyOf(x);
            y.clear(2, 0, 256);
            juce::MidiBuffer m;
            a.processBlock(x, m);
            b.processBlock(y, m);
            for (int i = 0; i < 256; ++i)
                diff += std::abs(x.getSample(0, i) - y.getSample(0, i));
        }
        // Main silence with an active external key must remain silent.
        warm(a, 480000);
        juce::AudioBuffer<float> z(4, 256);
        z.clear();
        for (int i = 0; i < 256; ++i)
            z.setSample(2, i, 1);
        juce::MidiBuffer m;
        a.processBlock(z, m);
        for (int i = 0; i < 256; ++i)
            silentScLeak = std::max(silentScLeak, std::abs(z.getSample(0, i)));
        std::printf("sidechain absolute difference sum %.5f; leak %.8g\n", diff, silentScLeak);
        check(diff > 10 && silentScLeak < 1.e-10f, "external sidechain modulates, never leaks into audio");
    }
    {
        HeftProcessor p;
        setup(p, 48000, 256, false, 1);
        p.factory(7);
        warm(p);
        juce::AudioBuffer<float> b(1, 256);
        for (int i = 0; i < 256; ++i)
            b.setSample(0, i, sine(i));
        juce::MidiBuffer m;
        p.processBlock(b, m);
        bool finite = true;
        for (int i = 0; i < 256; ++i)
            finite &= std::isfinite(b.getSample(0, i));
        check(finite, "mono effect layout processes safely");
    }
    {
        HeftProcessor a, b;
        setup(a);
        setup(b);
        a.factory(7);
        a.set("output", -7);
        juce::MemoryBlock state;
        a.getStateInformation(state);
        b.setStateInformation(state.getData(), int(state.getSize()));
        bool equal = true;
        for (int i = 0; i < a.getParameters().size(); ++i)
            equal &= std::abs(a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) < 1.e-6f;
        check(equal, "all controls and modulation routes survive state restore");
        const char junk[] = {0, 1, 2, 3};
        b.setStateInformation(junk, 4);
        check(b.state.getRawParameterValue("output")->load() == -7, "bad state is ignored");
    }
    {
        HeftProcessor p;
        setup(p);
        p.factory(0);
        p.set("mix", .8f);
        p.set("output", -9);
        bool safe = true;
        float pk = 0;
        for (int trial = 0; trial < 128; ++trial)
        {
            p.dice();
            for (int j = 0; j < 20; ++j)
            {
                juce::AudioBuffer<float> b(2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    b.setSample(0, i, 2 * sine((trial * 20 + j) * 256 + i));
                    b.setSample(1, i, sine((trial * 20 + j) * 256 + i, 48000, 333));
                }
                juce::MidiBuffer m;
                p.processBlock(b, m);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                    {
                        float x = b.getSample(c, i);
                        safe &= std::isfinite(x) && std::abs(x) <= .891252f;
                        pk = std::max(pk, std::abs(x));
                    }
            }
        }
        std::printf("128 dice peak %.8f\n", pk);
        check(safe && p.state.getRawParameterValue("mix")->load() == .8f &&
                  p.state.getRawParameterValue("output")->load() == -9,
              "128 dice results safe; mix and output unchanged");
    }
    {
        for (int parameter = 0; parameter < heft::Count; ++parameter)
        {
            float previous = 0;
            HeftProcessor a, b;
            setup(a);
            setup(b);
            a.factory(0);
            b.factory(0);
            warm(a);
            warm(b);
            for (int block = 0; block < 100; ++block)
            {
                juce::AudioBuffer<float> x(2, 256), y(2, 256);
                for (int i = 0; i < 256; ++i)
                {
                    x.setSample(0, i, sine(block * 256 + i, 48000, 80));
                    x.setSample(1, i, sine(block * 256 + i, 48000, 80));
                }
                y.makeCopyOf(x);
                if (block == 50)
                    a.set(heft::definitions[parameter].id, heft::definitions[parameter].high);
                juce::MidiBuffer m;
                a.processBlock(x, m);
                b.processBlock(y, m);
                if (block == 50)
                    maxJumpError = std::max(maxJumpError, std::abs(x.getSample(0, 0) - y.getSample(0, 0)));
                for (int i = 0; i < 256; ++i)
                {
                    if (block >= 50)
                        maxJumpStep = std::max(maxJumpStep, std::abs(x.getSample(0, i) - previous));
                    previous = x.getSample(0, i);
                }
            }
        }
        std::printf("parameter jump first-sample deviation %.8f\n", maxJumpError);
        std::printf("parameter jump max adjacent step %.8f\n", maxJumpStep);
        check(maxJumpError < .002f && maxJumpStep < .15f,
              "parameter step dezippering: boundary and adjacent-sample bounds");
    }
    {
        HeftProcessor p;
        setup(p);
        p.factory(3);
        p.set("smear", 1);
        p.set("shift", -400);
        warm(p, 96000);
        juce::AudioBuffer<float> b(2, 256);
        for (int i = 0; i < 256; ++i)
        {
            b.setSample(0, i, 1);
            b.setSample(1, i, 1);
        }
        juce::MidiBuffer m;
        p.processBlock(b, m);
        warm(p, 480000);
        b.clear();
        p.processBlock(b, m);
        float pk = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                pk = std::max(pk, std::abs(b.getSample(c, i)));
        check(pk < 1.e-20f, "tails settle to zero without persistent self-noise");
    }
    {
        HeftProcessor p;
        setup(p);
        juce::AudioBuffer<float> b(2, 256);
        b.clear();
        b.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());
        b.setSample(1, 12, std::numeric_limits<float>::infinity());
        juce::MidiBuffer m;
        p.processBlock(b, m);
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                finite &= std::isfinite(b.getSample(c, i));
        check(finite, "NaN/Inf input is sanitized at the audio boundary");
    }
    std::ofstream cpu(folder.getChildFile("cpu.csv").getFullPathName().toStdString());
    cpu << "run,audio_seconds,process_seconds,one_core_percent,worst_block_us\n";
    for (int run = 0; run < 5; ++run)
    {
        HeftProcessor p;
        setup(p);
        p.factory(7);
        p.set("fuzz", .5f);
        p.set("wide", .7f);
        p.set("shift", 120);
        p.set("smear", .8f);
        warm(p);
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer m;
        double elapsed = 0, worst = 0;
        float checksum = 0;
        int blocks = 1875;
        for (int j = 0; j < blocks; ++j)
        {
            for (int i = 0; i < 256; ++i)
            {
                b.setSample(0, i, sine(j * 256 + i));
                b.setSample(1, i, sine(j * 256 + i, 48000, 311));
            }
            auto start = std::chrono::steady_clock::now();
            p.processBlock(b, m);
            double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            elapsed += dt;
            worst = std::max(worst, dt);
            checksum += b.getSample(0, 100);
        }
        cpu << run << ",10," << elapsed << "," << elapsed * 10 << "," << worst * 1.e6 << "\n";
        std::printf("CPU run %d: %.3f%% one core; %.3f us worst; checksum %.5f\n", run, elapsed * 10,
                    worst * 1.e6, checksum);
        check(std::isfinite(checksum), "timed processor output consumed");
    }
    std::ofstream report(folder.getChildFile("test-results.json").getFullPathName().toStdString());
    report << "{\"failures\":" << failures << ",\"nonfinite\":" << bad << ",\"denormal\":" << denormal
           << ",\"stress_peak\":" << ceiling << ",\"jump_first_sample_error\":" << maxJumpError
           << ",\"jump_max_step\":" << maxJumpStep << ",\"dice_trials\":128}";
    return failures;
}
} // namespace
int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf(stderr, "HeftTest test folder | screenshot file [advanced] | render input output preset "
                             "[parameter value]\n");
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
        HeftProcessor p;
        setup(p);
        p.factory(1);
        auto ed = std::make_unique<HeftEditor>(p);
        if (argc > 3)
            ed->showAdvanced(true);
        ed->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto image = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::File imageFile(argv[2]);
        imageFile.deleteFile();
        juce::FileOutputStream out{imageFile};
        return juce::PNGImageFormat().writeImageToStream(image, out) ? 0 : 1;
    }
    if (mode == "render" && argc >= 5)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(juce::File(argv[2])));
        if (!reader)
            return 3;
        HeftProcessor p;
        setup(p, reader->sampleRate);
        p.factory(std::atoi(argv[4]));
        if (argc > 6)
            p.set(argv[5], float(std::atof(argv[6])));
        warm(p, int(reader->sampleRate * .5));
        int count = int(reader->lengthInSamples);
        juce::AudioBuffer<float> whole(2, count);
        reader->read(&whole, 0, count, 0, true, true);
        for (int at = 0; at < count; at += 256)
        {
            int n = std::min(256, count - at);
            juce::AudioBuffer<float> b(2, n);
            for (int c = 0; c < 2; ++c)
                b.copyFrom(c, 0, whole, c, at, n);
            juce::MidiBuffer m;
            p.processBlock(b, m);
            for (int c = 0; c < 2; ++c)
                whole.copyFrom(c, at, b, c, 0, n);
        }
        juce::File outputFile(argv[3]);
        outputFile.deleteFile();
        auto out = std::make_unique<juce::FileOutputStream>(outputFile);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(out.release(), reader->sampleRate, 2, 24, {}, 0));
        return writer && writer->writeFromAudioSampleBuffer(whole, 0, count) ? 0 : 4;
    }
    return 2;
}
