#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const rv::PDef kParams[] = {
    { "snapOn",  "Snap",          'b', 0, 1, 1, 1, 1.0f, "" },
    { "key",     "Key",           'c', 0, 11, 1, 1, 0.0f, "", "C|C#|D|D#|E|F|F#|G|G#|A|A#|B" },
    { "scale",   "Scale",         'c', 0, 4, 1, 1, 0.0f, "", "Chromatic|Major|Minor|Pentatonic|Blues" },
    { "speed",   "Retune Speed",  'f', 0.0f, 200.0f, 0.1f, 0.5f, 0.0f, "ms" },
    { "vocOn",   "Vocoder",       'b', 0, 1, 1, 1, 1.0f, "" },
    { "carrier", "Carrier",       'c', 0, 3, 1, 1, 0.0f, "", "Saw|Square|Buzz|Whisper" },
    { "bands",   "Bands",         'c', 0, 2, 1, 1, 1.0f, "", "8|16|24" },
    { "octave",  "Octave",        'c', 0, 3, 1, 1, 2.0f, "", "-2 oct|-1 oct|0|+1 oct" },
    { "vocMix",  "Vocoder Mix",   'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.7f, "%" },
    { "ringOn",  "Ring Mod",      'b', 0, 1, 1, 1, 0.0f, "" },
    { "ringHz",  "Ring Freq",     'f', 20.0f, 2000.0f, 0.1f, 0.3f, 90.0f, "Hz" },
    { "ringMix", "Ring Mix",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "mix",     "Mix",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "gain",    "Output",        'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
};
} // namespace

RobotVoiceProcessor::RobotVoiceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", rv::makeLayout (kParams))
{
    presets.keepIds = { "key" };   // the song key is yours; presets change the sound, not the key
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSnap = g ("snapOn"); pKey = g ("key"); pScale = g ("scale"); pSpeed = g ("speed"); pVoc = g ("vocOn"); pCar = g ("carrier");
    pBands = g ("bands"); pOct = g ("octave"); pVocMix = g ("vocMix"); pRing = g ("ringOn"); pRingHz = g ("ringHz"); pRingMix = g ("ringMix");
    pMix = g ("mix"); pGain = g ("gain");
    held.reserve (128);
}

bool RobotVoiceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

rv::dsp::Params RobotVoiceProcessor::currentParams() const
{
    rv::dsp::Params p;
    p.snapOn = pSnap->load() > 0.5f; p.key = (int) pKey->load(); p.scale = (int) pScale->load(); p.speedMs = pSpeed->load();
    p.vocOn = pVoc->load() > 0.5f; p.carrier = (int) pCar->load(); p.bandsIdx = (int) pBands->load(); p.octave = (int) pOct->load() - 2;
    p.vocMix = pVocMix->load(); p.ringOn = pRing->load() > 0.5f; p.ringHz = pRingHz->load(); p.ringMix = pRingMix->load();
    p.mix = pMix->load(); p.gainDb = pGain->load();
    return p;
}

void RobotVoiceProcessor::prepareToPlay (double sr, int)
{
    engine.setParams (currentParams());
    engine.prepare (sr > 0 ? sr : 48000.0, juce::jmax (1, getTotalNumOutputChannels()));
    held.clear(); midiNote.store (-1); engine.setMidiNote (-1);
}

void RobotVoiceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { if (held.size() < 128) held.push_back (msg.getNoteNumber()); }
        else if (msg.isNoteOff()) held.erase (std::remove (held.begin(), held.end(), msg.getNoteNumber()), held.end());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) held.clear();
    }
    const int note = held.empty() ? -1 : held.back();
    midiNote.store (note); engine.setMidiNote (note);
    engine.setParams (currentParams());
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (nch <= 0) return;
    engine.process (buffer.getArrayOfWritePointers(), n);
    juce::ignoreUnused (nch);
}

void RobotVoiceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    xml->setAttribute ("presetName", presets.currentName()); xml->setAttribute ("presetUser", presets.currentIsUser());
    copyXmlToBinary (*xml, dest);
}

void RobotVoiceProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
}

juce::AudioProcessorEditor* RobotVoiceProcessor::createEditor() { return new RobotVoiceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RobotVoiceProcessor(); }
