#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <cstring>

juce::AudioProcessorValueTreeState::ParameterLayout StokeProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (int i = 0; i < stoke::Count; ++i)
    {
        const auto &d = stoke::definitions[(size_t)i];
        const juce::ParameterID id(d.id, 1);
        if (d.kind == stoke::Kind::Bool)
        {
            p.add(std::make_unique<juce::AudioParameterBool>(id, d.name, d.initial >= 0.5f));
            continue;
        }
        if (std::strcmp(d.id, "centre") == 0)
        {
            juce::NormalisableRange<float> range(
                d.low, d.high,
                [](float start, float end, float t) { return start * std::pow(end / start, t); },
                [](float start, float end, float v) {
                    return std::log(std::max(v, start) / start) / std::log(end / start);
                },
                [](float, float, float v) { return std::round(v * 10.f) / 10.f; });
            p.add(std::make_unique<juce::AudioParameterFloat>(
                id, d.name, range, d.initial,
                juce::AudioParameterFloatAttributes().withLabel(d.unit)
                    .withStringFromValueFunction([](float v, int) {
                        if (v >= 1000.f)
                            return juce::String(v / 1000.f, 2) + " kHz";
                        return juce::String(v, 0) + " Hz";
                    })
                    .withValueFromStringFunction([](const juce::String &s) {
                        auto t = s.trim().toLowerCase();
                        const bool khz = t.contains("khz") || t.contains("k");
                        t = t.retainCharacters("0123456789.+-");
                        const float n = t.getFloatValue();
                        return khz || (n > 0.f && n < 20.f) ? n * 1000.f : n;
                    })));
            continue;
        }
        const float span = d.high - d.low;
        const float step = span <= 1.01f ? 0.001f : (span > 20 ? 0.01f : 0.01f);
        juce::NormalisableRange<float> range(d.low, d.high, step);
        const bool percent = d.low == 0.f && d.high == 1.f && juce::String(d.unit).isEmpty();
        p.add(std::make_unique<juce::AudioParameterFloat>(
            id, d.name, range, d.initial,
            juce::AudioParameterFloatAttributes()
                .withLabel(d.unit)
                .withStringFromValueFunction(
                    [unit = juce::String(d.unit), step, percent](float v, int) {
                        if (percent)
                            return juce::String((int)std::lround(v * 100.f)) + " %";
                        if (unit == "dB")
                            return juce::String(v, 1) + " dB";
                        if (unit == "oct")
                            return juce::String(v, 2) + " oct";
                        const int dec = step < 0.01f ? 3 : (step < 0.1f ? 2 : 1);
                        return juce::String(v, dec);
                    })
                .withValueFromStringFunction(
                    [unit = juce::String(d.unit), percent](const juce::String &s) {
                        auto t = s.trim();
                        if (percent)
                        {
                            if (t.endsWithChar('%'))
                                t = t.dropLastCharacters(1).trim();
                            return t.getFloatValue() * 0.01f;
                        }
                        t = t.retainCharacters("0123456789.+-");
                        return t.getFloatValue();
                    })));
    }
    return p;
}

StokeProcessor::StokeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "STOKE", parameters())
{
    for (int i = 0; i < stoke::Count; ++i)
        values[(size_t)i] = state.getRawParameterValue(stoke::definitions[(size_t)i].id);
}

void StokeProcessor::prepareToPlay(double sr, int block)
{
    engine.prepare(sr, block);
    setLatencySamples(stoke::Engine::latency);
}

bool StokeProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void StokeProcessor::pullSettings(stoke::Settings &s) const
{
    for (int i = 0; i < stoke::Count; ++i)
        s.v[(size_t)i] = values[(size_t)i]->load();
}

void StokeProcessor::pushSettings(const stoke::Settings &s)
{
    for (int i = 0; i < stoke::Count; ++i)
    {
        auto *p = state.getParameter(stoke::definitions[(size_t)i].id);
        if (p == nullptr)
            continue;
        if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
            *pf = s[i];
        else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
            *pb = s[i] >= 0.5f;
        else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
            *pc = stoke::clampi((int)std::round(s[i]), 0, pc->choices.size() - 1);
    }
}

stoke::Settings StokeProcessor::getSettings() const
{
    stoke::Settings s;
    pullSettings(s);
    return s;
}

void StokeProcessor::applySettings(const stoke::Settings &s)
{
    pushSettings(s);
    engine.set(s);
}

void StokeProcessor::set(const juce::String &id, float value)
{
    auto *p = state.getParameter(id);
    if (p == nullptr)
        return;
    if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
        *pf = value;
    else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
        *pb = value >= 0.5f;
    else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
        *pc = stoke::clampi((int)std::round(value), 0, pc->choices.size() - 1);
}

void StokeProcessor::factory(int index)
{
    auto s = stoke::makePreset(index);
    pushSettings(s);
    engine.set(s);
    engine.reset();
    engine.snap();
}

juce::StringArray StokeProcessor::presetNames()
{
    juce::StringArray a;
    for (int i = 0; i < stoke::presetCount; ++i)
        a.add(stoke::presetNames[i]);
    return a;
}

void StokeProcessor::snapEngine()
{
    stoke::Settings s;
    pullSettings(s);
    engine.set(s);
    engine.snap();
}

void StokeProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    const int c = buffer.getNumChannels();
    for (int ch = 2; ch < c; ++ch)
        buffer.clear(ch, 0, n);
    if (c <= 0 || n <= 0)
        return;

    stoke::Settings s;
    pullSettings(s);
    engine.set(s);
    float *l = buffer.getWritePointer(0);
    float *r = c > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.process(l, r, n);
    meter.store(engine.meter);
    reduction.store(engine.reductionDb);
    makeup.store(engine.makeupDb);
}

void StokeProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void StokeProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("STOKE") || xml->hasTagName(state.state.getType()))
        {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            auto s = getSettings();
            engine.set(s);
        }
}

juce::AudioProcessorEditor *StokeProcessor::createEditor() { return new StokeEditor(*this); }

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new StokeProcessor(); }
