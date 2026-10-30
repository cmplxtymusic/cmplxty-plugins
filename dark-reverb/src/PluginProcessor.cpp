#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const dr::PDef kParams[] = {
    { "preDelay",  "Pre-Delay",     'f', 0.0f, 500.0f, 0.1f, 0.5f, 20.0f, "ms" },
    { "size",      "Size",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "decay",     "Decay",         'f', 0.3f, 30.0f, 0.01f, 0.35f, 6.0f, "s" },
    { "diffusion", "Diffusion",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.7f, "%" },
    { "dark",      "Darkness",      'f', 800.0f, 16000.0f, 1.0f, 0.3f, 3500.0f, "Hz" },
    { "lowCut",    "Low Cut",       'f', 20.0f, 1000.0f, 0.1f, 0.3f, 80.0f, "Hz" },
    { "metal",     "Metal",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.25f, "%" },
    { "mod",       "Modulation",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "shimOn",    "Shimmer",       'b', 0, 1, 1, 1, 0.0f, "" },
    { "shimPitch", "Shimmer Pitch", 'c', 0, 4, 1, 1, 0.0f, "", "+12 st|+7 st|+19 st|+24 st|-12 st" },
    { "shimAmt",   "Shimmer Amount",'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "freeze",    "Freeze",        'b', 0, 1, 1, 1, 0.0f, "" },
    { "width",     "Width",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "mix",       "Mix",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.35f, "%" },
    { "gain",      "Output",        'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "bypass",    "Bypass",       'b', 0, 1, 1, 1, 0.0f, "" },
};
const float kShimSemis[] = { 12.0f, 7.0f, 19.0f, 24.0f, -12.0f };
} // namespace

DarkReverbProcessor::DarkReverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", dr::makeLayout (kParams))
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds = { "freeze", "bypass" };   // performance switches: changing presets never freezes / un-freezes the tail or (un)bypasses
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pPre = g ("preDelay"); pSize = g ("size"); pDecay = g ("decay"); pDiff = g ("diffusion"); pDark = g ("dark"); pLow = g ("lowCut");
    pMetal = g ("metal"); pMod = g ("mod"); pShOn = g ("shimOn"); pShP = g ("shimPitch"); pShAmt = g ("shimAmt"); pFreeze = g ("freeze");
    pWidth = g ("width"); pMix = g ("mix"); pGain = g ("gain");
}

bool DarkReverbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

dr::dsp::Params DarkReverbProcessor::currentParams() const
{
    dr::dsp::Params p;
    p.preDelayMs = pPre->load(); p.size = pSize->load(); p.decayS = pDecay->load(); p.diffusion = pDiff->load();
    p.darkHz = pDark->load(); p.lowCutHz = pLow->load(); p.metal = pMetal->load(); p.mod = pMod->load();
    p.shimOn = pShOn->load() > 0.5f; p.shimSemis = kShimSemis[juce::jlimit (0, 4, (int) pShP->load())]; p.shimAmt = pShAmt->load();
    p.freeze = pFreeze->load() > 0.5f; p.width = pWidth->load(); p.mix = pMix->load(); p.gainDb = pGain->load();
    return p;
}

void DarkReverbProcessor::prepareToPlay (double sr, int maxBlock)
{
    bypass.prepare (sr, maxBlock, juce::jmax (1, getMainBusNumOutputChannels()), 0);
    engine.setParams (currentParams());
    engine.prepare (sr > 0 ? sr : 48000.0, juce::jmax (1, getTotalNumOutputChannels()));
}

void DarkReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    bypass.pushDry (buffer);
    midi.clear();
    engine.setParams (currentParams());
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (nch <= 0) return;
    engine.process (buffer.getArrayOfWritePointers(), n);
    juce::ignoreUnused (nch);
    bypass.mix (buffer, bypassParam->get());
}

void DarkReverbProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    xml->setAttribute ("presetName", presets.currentName()); xml->setAttribute ("presetUser", presets.currentIsUser());
    copyXmlToBinary (*xml, dest);
}

void DarkReverbProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
}

juce::AudioProcessorEditor* DarkReverbProcessor::createEditor() { return new DarkReverbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DarkReverbProcessor(); }
