#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const mt::PDef kParams[] = {
    { "order",    "Order",          'c', 0, 1, 1, 1, 0.0f, "", "Widen > Shape > Limit|Shape > Widen > Limit" },
    { "wOn",      "Widener On",     'b', 0, 1, 1, 1, 1.0f, "" },
    { "width",    "Width",          'f', 0.0f, 2.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "monoBass", "Mono Bass",      'f', 20.0f, 500.0f, 1.0f, 0.5f, 20.0f, "Hz-off" },
    { "tOn",      "Shaper On",      'b', 0, 1, 1, 1, 1.0f, "" },
    { "attack",   "Attack",         'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "sustain",  "Sustain",        'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "speed",    "Shaper Speed",   'c', 0, 2, 1, 1, 1.0f, "", "Fast|Medium|Slow" },
    { "lOn",      "Limiter On",     'b', 0, 1, 1, 1, 1.0f, "" },
    { "drive",    "Drive",          'f', 0.0f, 24.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "ceiling",  "Ceiling",        'f', -12.0f, 0.0f, 0.1f, 1.0f, -1.0f, "dB" },
    { "release",  "Release",        'f', 1.0f, 1000.0f, 0.1f, 0.4f, 80.0f, "ms" },
    { "gain",     "Output",         'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
};
} // namespace

MixToolsProcessor::MixToolsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", mt::makeLayout (kParams)),
      presets (apvts, &undoManager, "Mix Tools")
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pOrder = g ("order"); pWOn = g ("wOn"); pWidth = g ("width"); pMono = g ("monoBass"); pTOn = g ("tOn"); pAtt = g ("attack"); pSus = g ("sustain");
    pSpeed = g ("speed"); pLOn = g ("lOn"); pDrive = g ("drive"); pCeil = g ("ceiling"); pRel = g ("release"); pGain = g ("gain");
}

bool MixToolsProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

mt::dsp::Params MixToolsProcessor::currentParams() const
{
    mt::dsp::Params p;
    p.order = (mt::dsp::Order) juce::jlimit (0, 1, (int) pOrder->load());
    p.wOn = pWOn->load() > 0.5f; p.width = pWidth->load(); p.monoHz = pMono->load();
    p.tOn = pTOn->load() > 0.5f; p.attack = pAtt->load(); p.sustain = pSus->load(); p.speed = (mt::dsp::Speed) juce::jlimit (0, 2, (int) pSpeed->load());
    p.lOn = pLOn->load() > 0.5f; p.driveDb = pDrive->load(); p.ceilingDb = pCeil->load(); p.releaseMs = pRel->load(); p.gainDb = pGain->load();
    return p;
}

void MixToolsProcessor::prepareToPlay (double sr, int)
{
    engine.setParams (currentParams());
    engine.prepare (sr > 0 ? sr : 48000.0, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (engine.latency());
}

void MixToolsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    midi.clear();
    engine.setParams (currentParams());
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (nch <= 0) return;
    engine.process (buffer.getArrayOfWritePointers(), n);
    juce::ignoreUnused (nch);
}

void MixToolsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void MixToolsProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MixToolsProcessor::createEditor() { return new MixToolsEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MixToolsProcessor(); }
