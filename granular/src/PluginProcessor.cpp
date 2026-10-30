#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const gr::PDef kParams[] = {
    { "size",       "Grain Size",    'f', 10.0f, 1000.0f, 0.1f, 0.4f, 120.0f, "ms" },
    { "density",    "Density",       'f', 1.0f, 80.0f, 0.01f, 0.5f, 16.0f, "Hz" },
    { "position",   "Position",      'f', 0.0f, 2000.0f, 0.1f, 0.4f, 150.0f, "ms" },
    { "spray",      "Spray",         'f', 0.0f, 1000.0f, 0.1f, 0.4f, 80.0f, "ms" },
    { "pitch",      "Pitch",         'f', -24.0f, 24.0f, 0.01f, 1.0f, 0.0f, "st" },
    { "spread",     "Pitch Spread",  'f', 0.0f, 24.0f, 0.01f, 0.5f, 0.0f, "st" },
    { "spreadMode", "Spread Mode",   'c', 0, 3, 1, 1, 0.0f, "", "Free|Semitones|Fifths|Octaves" },
    { "reverse",    "Reverse",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "chaos",      "Chaos",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "shape",      "Grain Shape",   'c', 0, 3, 1, 1, 0.0f, "", "Smooth|Triangle|Perc|Glitch" },
    { "width",      "Width",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.7f, "%" },
    { "feedback",   "Feedback",      'f', 0.0f, 0.95f, 0.001f, 1.0f, 0.0f, "%" },
    { "tone",       "Tone",          'f', 500.0f, 20000.0f, 1.0f, 0.3f, 16000.0f, "Hz" },
    { "freeze",     "Freeze",        'b', 0, 1, 1, 1, 0.0f, "" },
    { "mix",        "Mix",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "gain",       "Output",        'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "bypass",     "Bypass",        'b', 0, 1, 1, 1, 0.0f, "" },
};
} // namespace

GranularProcessor::GranularProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", gr::makeLayout (kParams)),
      presets (apvts, &undoManager, "Granular")
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds.addIfNotAlreadyThere ("bypass");   // presets never bypass / un-bypass
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSize = g ("size"); pDens = g ("density"); pPos = g ("position"); pSpray = g ("spray"); pPitch = g ("pitch"); pSpread = g ("spread");
    pMode = g ("spreadMode"); pRev = g ("reverse"); pChaos = g ("chaos"); pShape = g ("shape"); pWidth = g ("width"); pFb = g ("feedback");
    pTone = g ("tone"); pFreeze = g ("freeze"); pMix = g ("mix"); pGain = g ("gain");
    startTimerHz (2);
}

bool GranularProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

gr::dsp::Params GranularProcessor::currentParams() const
{
    gr::dsp::Params p;
    p.sizeMs = pSize->load(); p.density = pDens->load(); p.positionMs = pPos->load(); p.sprayMs = pSpray->load();
    p.pitch = pPitch->load(); p.spread = pSpread->load(); p.spreadMode = (gr::dsp::SpreadMode) juce::jlimit (0, 3, (int) pMode->load());
    p.reverse = pRev->load(); p.chaos = pChaos->load(); p.shape = (gr::dsp::Shape) juce::jlimit (0, 3, (int) pShape->load());
    p.width = pWidth->load(); p.feedback = pFb->load(); p.toneHz = pTone->load();
    p.freeze = pFreeze->load() > 0.5f; p.mix = pMix->load(); p.gainDb = pGain->load();
    return p;
}

void GranularProcessor::prepareToPlay (double sr, int maxBlock)
{
    bypass.prepare (sr, maxBlock, juce::jmax (1, getMainBusNumOutputChannels()), 0);
    engine.setParams (currentParams());
    engine.prepare (sr > 0 ? sr : 48000.0, juce::jmax (1, getTotalNumOutputChannels()));
}

void GranularProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

void GranularProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void GranularProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* GranularProcessor::createEditor() { return new GranularEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GranularProcessor(); }
