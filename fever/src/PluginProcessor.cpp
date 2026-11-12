#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

juce::AudioProcessorValueTreeState::ParameterLayout FeverProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (int i = 0; i < fever::Count; ++i)
    {
        const auto &d = fever::definitions[(size_t)i];
        const juce::ParameterID id(d.id, 1);
        if (d.kind == fever::Kind::Bool)
        {
            p.add(std::make_unique<juce::AudioParameterBool>(id, d.name, d.initial >= 0.5f));
            continue;
        }
        if (d.kind == fever::Kind::Choice)
        {
            juce::StringArray labels;
            for (int k = 0; k < d.choices; ++k)
                labels.add(d.labels[k]);
            p.add(std::make_unique<juce::AudioParameterChoice>(
                id, d.name, labels, fever::clampi((int)std::round(d.initial), 0, d.choices - 1)));
            continue;
        }
        const float span = d.high - d.low;
        const float step = span <= 1.01f ? 0.001f : (span > 20 ? 0.01f : 0.001f);
        juce::NormalisableRange<float> range(d.low, d.high, step);
        p.add(std::make_unique<juce::AudioParameterFloat>(
            id, d.name, range, d.initial,
            juce::AudioParameterFloatAttributes().withLabel(d.unit).withStringFromValueFunction(
                [step](float v, int) {
                    const int dec = step < 0.01f ? 3 : (step < 0.1f ? 2 : 1);
                    return juce::String(v, dec);
                })));
    }
    return p;
}

FeverProcessor::FeverProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "FEVER", parameters())
{
    for (int i = 0; i < fever::Count; ++i)
        values[(size_t)i] = state.getRawParameterValue(fever::definitions[(size_t)i].id);
}

void FeverProcessor::prepareToPlay(double sr, int block) { engine.prepare(sr, block); }

bool FeverProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FeverProcessor::pullSettings(fever::Settings &s) const
{
    for (int i = 0; i < fever::Count; ++i)
        s.v[(size_t)i] = values[(size_t)i]->load();
}

void FeverProcessor::pushSettings(const fever::Settings &s)
{
    for (int i = 0; i < fever::Count; ++i)
    {
        auto *p = state.getParameter(fever::definitions[(size_t)i].id);
        if (p == nullptr)
            continue;
        if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
            *pf = s[i];
        else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
            *pb = s[i] >= 0.5f;
        else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
            *pc = fever::clampi((int)std::round(s[i]), 0, pc->choices.size() - 1);
    }
}

fever::Settings FeverProcessor::getSettings() const
{
    fever::Settings s;
    pullSettings(s);
    return s;
}

void FeverProcessor::applySettings(const fever::Settings &s)
{
    pushSettings(s);
    engine.set(s);
}

void FeverProcessor::set(const juce::String &id, float value)
{
    auto *p = state.getParameter(id);
    if (p == nullptr)
        return;
    if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
        *pf = value;
    else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
        *pb = value >= 0.5f;
    else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
        *pc = fever::clampi((int)std::round(value), 0, pc->choices.size() - 1);
}

void FeverProcessor::factory(int index)
{
    auto s = fever::makePreset(index);
    pushSettings(s);
    engine.set(s);
    engine.reset();
}

juce::StringArray FeverProcessor::presetNames()
{
    juce::StringArray a;
    for (int i = 0; i < fever::presetCount; ++i)
        a.add(fever::presetNames[i]);
    return a;
}

void FeverProcessor::snapEngine()
{
    fever::Settings s;
    pullSettings(s);
    engine.set(s);
    engine.snap();
}

void FeverProcessor::syncTransport()
{
    double bpm = 120, ppq = 0;
    bool playing = false;
    if (auto *ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            if (auto q = pos->getPpqPosition())
                ppq = *q;
            playing = pos->getIsPlaying();
        }
    }
    engine.setTempo(bpm, playing, ppq);
}

void FeverProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    const int c = buffer.getNumChannels();
    for (int ch = 2; ch < c; ++ch)
        buffer.clear(ch, 0, n);
    if (c <= 0 || n <= 0)
        return;

    syncTransport();
    fever::Settings s;
    pullSettings(s);
    engine.set(s);
    float *l = buffer.getWritePointer(0);
    float *r = c > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.process(l, r, n);
    meter.store(engine.meter);
    reduction.store(engine.reductionDb);
    makeup.store(engine.makeupDb);
    heartPulse.store(engine.heartPulse);
    viewFever.store(engine.effectiveFever);
}

void FeverProcessor::processBlockBypassed(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

void FeverProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void FeverProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("FEVER") || xml->hasTagName(state.state.getType()))
        {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            auto s = getSettings();
            engine.set(s);
        }
}

juce::AudioProcessorEditor *FeverProcessor::createEditor() { return new FeverEditor(*this); }

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new FeverProcessor(); }
