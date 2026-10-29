#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout GlitchCrusherProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const auto ver = 1;
    auto pid = [ver] (const char* id) { return ParameterID { id, ver }; };

    p.push_back (std::make_unique<AudioParameterFloat> (pid ("bits"), "Bit Depth",
        NormalisableRange<float> (1.0f, 16.0f, 0.01f), 8.0f, AudioParameterFloatAttributes().withLabel ("bit")));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("downsample"), "Downsample",
        NormalisableRange<float> (1.0f, 64.0f, 0.01f, 0.4f), 4.0f, AudioParameterFloatAttributes().withLabel ("x")));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("drive"), "Drive",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f, AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)); })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; })));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("glitchProb"), "Glitch Probability",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f, AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)); })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; })));
    p.push_back (std::make_unique<AudioParameterChoice> (pid ("glitchRate"), "Glitch Slice",
        StringArray { "1/4", "1/8", "1/16", "1/32" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (pid ("reverse"), "Glitch Reverse",
        StringArray { "Forward", "Reverse", "Random" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (pid ("filterType"), "Filter Type",
        StringArray { "Lowpass", "Highpass" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("cutoff"), "Filter Cutoff",
        NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.3f), 8000.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("resonance"), "Filter Resonance",
        NormalisableRange<float> (0.5f, 10.0f, 0.01f, 0.5f), 0.707f, AudioParameterFloatAttributes().withLabel ("Q")));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("mix"), "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)); })
            .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; })));
    p.push_back (std::make_unique<AudioParameterFloat> (pid ("outGain"), "Output Gain",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    // section on/off switches (cross-faded in the DSP, so automating them does not click)
    p.push_back (std::make_unique<AudioParameterBool> (pid ("crushOn"), "Crush On", true));
    p.push_back (std::make_unique<AudioParameterBool> (pid ("glitchOn"), "Glitch On", true));
    p.push_back (std::make_unique<AudioParameterBool> (pid ("filterOn"), "Filter On", false));
    return { p.begin(), p.end() };
}

GlitchCrusherProcessor::GlitchCrusherProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pBits  = apvts.getRawParameterValue ("bits");       pDown = apvts.getRawParameterValue ("downsample");
    pDrive = apvts.getRawParameterValue ("drive");      pProb = apvts.getRawParameterValue ("glitchProb");
    pRate  = apvts.getRawParameterValue ("glitchRate"); pRev  = apvts.getRawParameterValue ("reverse");
    pFType = apvts.getRawParameterValue ("filterType"); pCut  = apvts.getRawParameterValue ("cutoff");
    pRes   = apvts.getRawParameterValue ("resonance");  pMix  = apvts.getRawParameterValue ("mix");
    pOut   = apvts.getRawParameterValue ("outGain");
    pCrushOn = apvts.getRawParameterValue ("crushOn");  pGlitchOn = apvts.getRawParameterValue ("glitchOn");
    pFilterOn = apvts.getRawParameterValue ("filterOn");
}

bool GlitchCrusherProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == l.getMainInputChannelSet();
}

void GlitchCrusherProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr, block, juce::jmax (1, getMainBusNumOutputChannels()));
    firstBlock = true;
}

void GlitchCrusherProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buffer.getNumChannels(), gc::kMaxChannels);
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    gc::Params p;
    p.bits = pBits->load(); p.downsample = pDown->load(); p.drive = pDrive->load();
    p.glitchProb = pProb->load(); p.glitchRate = (int) pRate->load(); p.reverseMode = (int) pRev->load();
    p.filterType = 1 + (int) pFType->load(); p.cutoffHz = pCut->load(); p.resonance = pRes->load();
    p.mix = pMix->load(); p.outGainDb = pOut->load();
    p.crushOn = pCrushOn->load() > 0.5f; p.glitchOn = pGlitchOn->load() > 0.5f; p.filterOn = pFilterOn->load() > 0.5f;
    engine.setParams (p);
    if (firstBlock) { engine.snapSmoothers(); firstBlock = false; }

    gc::Transport tp;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tp.bpm = *bpm;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) { tp.ppq = *ppq; tp.hasPpq = true; }
        }

    engine.process (buffer.getArrayOfWritePointers(), nCh, n, tp);
}

juce::AudioProcessorEditor* GlitchCrusherProcessor::createEditor() { return new GlitchCrusherEditor (*this); }

// State = flat XML of "parameter id -> plain value" read straight from the parameter objects,
// so it is deterministic and does not depend on the ValueTree being in sync.
void GlitchCrusherProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("GlitchCrusherState");
    xml.setAttribute ("version", 2);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    copyXmlToBinary (xml, dest);
}

void GlitchCrusherProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("GlitchCrusherState"))
        {
            // version 1 (pre-release builds) had filterType = Off / Lowpass / Highpass and no section switches
            const bool legacy = xml->getIntAttribute ("version", 1) < 2;
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (xml->hasAttribute (r->paramID))
                    {
                        double v = xml->getDoubleAttribute (r->paramID);
                        if (legacy && r->paramID == "filterType") v = juce::jmax (0.0, v - 1.0);
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) v));
                    }
            presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
            if (legacy)
            {
                if (auto* f = apvts.getParameter ("filterOn")) f->setValueNotifyingHost (xml->getIntAttribute ("filterType", 0) != 0 ? 1.0f : 0.0f);
                for (auto* id : { "crushOn", "glitchOn" }) if (auto* b = apvts.getParameter (id)) b->setValueNotifyingHost (1.0f);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlitchCrusherProcessor(); }
