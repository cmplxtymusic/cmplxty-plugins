#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <cstring>

namespace
{
juce::String formatValue(const glaze::Definition &d, float v)
{
    const juce::String id(d.id), unit(d.unit);
    if (id == "hpf")
        return v < 12.5f ? juce::String("OFF") : juce::String((int)std::lround(v)) + " Hz";
    if (id == "bassmono")
        return v < 25.f ? juce::String("OFF") : juce::String((int)std::lround(v)) + " Hz";
    if (id == "gratio")
        return juce::String(v, v < 3.f ? 2 : 1) + ":1";
    if (id == "ceiling" || id == "gthr" || unit == "dB")
        return juce::String(v, 1) + " dB";
    if (unit == "Hz")
        return v >= 1000.f ? juce::String(v / 1000.f, 2) + " kHz" : juce::String((int)std::lround(v)) + " Hz";
    if (unit == "ms")
        return v >= 100.f ? juce::String((int)std::lround(v)) + " ms" : juce::String(v, v < 1.f ? 2 : 1) + " ms";
    if (id == "tone")
    {
        const int p = (int)std::lround(v * 100.f);
        return (p > 0 ? "+" : "") + juce::String(p) + " %";
    }
    if (d.high <= 2.01f && d.low >= 0.f)
        return juce::String((int)std::lround(v * 100.f)) + " %";
    return juce::String(v, 2);
}

float parseValue(const glaze::Definition &d, const juce::String &s0)
{
    auto t = s0.trim().toLowerCase();
    const juce::String id(d.id), unit(d.unit);
    if ((id == "hpf" || id == "bassmono") && t.contains("off"))
        return id == "hpf" ? 10.f : 0.f;
    const bool khz = t.contains("k");
    const bool pct = t.contains("%");
    t = t.retainCharacters("0123456789.+-");
    float n = t.getFloatValue();
    if (unit == "Hz")
        return (khz || (n > 0.f && n < 20.f && d.high >= 1000.f)) ? n * 1000.f : n;
    if (d.unit[0] == 0 && d.high <= 2.01f && d.kind == glaze::Kind::Float)
        return (pct || std::abs(n) > d.high) ? n * 0.01f : n;
    return n;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GlazeProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (int i = 0; i < glaze::Count; ++i)
    {
        const auto &d = glaze::definitions[(size_t)i];
        const juce::ParameterID id(d.id, 1);
        if (d.kind == glaze::Kind::Bool)
        {
            p.add(std::make_unique<juce::AudioParameterBool>(id, d.name, d.initial >= 0.5f));
            continue;
        }
        if (d.kind == glaze::Kind::Choice)
        {
            juce::StringArray labels;
            for (int k = 0; k < d.choices; ++k)
                labels.add(d.labels[k]);
            p.add(std::make_unique<juce::AudioParameterChoice>(id, d.name, labels, (int)std::lround(d.initial)));
            continue;
        }
        juce::NormalisableRange<float> range(d.low, d.high, 0.001f);
        if (d.logScale)
            range = juce::NormalisableRange<float>(
                d.low, d.high,
                [](float start, float end, float t) { return start * std::pow(end / start, t); },
                [](float start, float end, float v) {
                    return std::log(std::max(v, start) / start) / std::log(end / start);
                },
                [](float, float, float v) { return v; });
        const auto def = d;
        p.add(std::make_unique<juce::AudioParameterFloat>(
            id, d.name, range, d.initial,
            juce::AudioParameterFloatAttributes()
                .withLabel(d.unit)
                .withStringFromValueFunction([def](float v, int) { return formatValue(def, v); })
                .withValueFromStringFunction([def](const juce::String &s) { return parseValue(def, s); })));
    }
    return p;
}

GlazeProcessor::GlazeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "GLAZE", parameters())
{
    for (int i = 0; i < glaze::Count; ++i)
        values[(size_t)i] = state.getRawParameterValue(glaze::definitions[(size_t)i].id);
}

void GlazeProcessor::prepareToPlay(double sr, int block)
{
    engine.prepare(sr, block);
    glaze::Settings s;
    pullSettings(s);
    engine.set(s);
    engine.snap();
    setLatencySamples(engine.latencySamples());
}

bool GlazeProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void GlazeProcessor::pullSettings(glaze::Settings &s) const
{
    for (int i = 0; i < glaze::Count; ++i)
        s.v[(size_t)i] = values[(size_t)i]->load();
}

void GlazeProcessor::pushSettings(const glaze::Settings &s)
{
    for (int i = 0; i < glaze::Count; ++i)
        set(glaze::definitions[(size_t)i].id, s[i]);
}

glaze::Settings GlazeProcessor::getSettings() const
{
    glaze::Settings s;
    pullSettings(s);
    return s;
}

void GlazeProcessor::applySettings(const glaze::Settings &s)
{
    pushSettings(s);
    engine.set(s);
}

void GlazeProcessor::set(const juce::String &id, float value)
{
    auto *p = state.getParameter(id);
    if (p == nullptr)
        return;
    if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
        *pf = value;
    else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
        *pb = value >= 0.5f;
    else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
        *pc = glaze::clampi((int)std::round(value), 0, pc->choices.size() - 1);
}

void GlazeProcessor::factory(int index)
{
    auto s = glaze::makePreset(index);
    // Bypass is a transport-style switch, not part of a sound: keep it as the user left it.
    s[glaze::Bypass] = values[glaze::Bypass]->load();
    pushSettings(s);
    engine.set(s);
}

juce::StringArray GlazeProcessor::presetNames()
{
    juce::StringArray a;
    for (int i = 0; i < glaze::presetCount; ++i)
        a.add(glaze::presetNames[i]);
    return a;
}

void GlazeProcessor::snapEngine()
{
    glaze::Settings s;
    pullSettings(s);
    engine.set(s);
    engine.snap();
}

void GlazeProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    const int c = buffer.getNumChannels();
    for (int ch = 2; ch < c; ++ch)
        buffer.clear(ch, 0, n);
    if (c <= 0 || n <= 0)
        return;

    glaze::Settings s;
    pullSettings(s);
    engine.set(s);
    float *l = buffer.getWritePointer(0);
    float *r = c > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.process(l, r, n);
}

void GlazeProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void GlazeProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("GLAZE") || xml->hasTagName(state.state.getType()))
        {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            engine.set(getSettings());
        }
}

juce::AudioProcessorEditor *GlazeProcessor::createEditor() { return new GlazeEditor(*this); }

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new GlazeProcessor(); }
