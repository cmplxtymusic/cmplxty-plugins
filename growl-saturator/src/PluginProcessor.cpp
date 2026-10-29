#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); }
float fromPct (const juce::String& s) { return s.getFloatValue() / 100.0f; }
const char* const kVowelNames[5] = { "OO", "OH", "AH", "EH", "EE" };
juce::String vowelText (float v, int)
{
    const float x = juce::jlimit (0.0f, 1.0f, v) * 4.0f;
    const int i = juce::jmin ((int) x, 3);
    const float f = x - (float) i;
    if (f < 0.08f) return kVowelNames[i];
    if (f > 0.92f) return kVowelNames[i + 1];
    return juce::String (kVowelNames[i]) + ">" + kVowelNames[i + 1];
}
float vowelFromText (const juce::String& s)
{
    const auto t = s.trim().toUpperCase();
    auto idx = [] (const juce::String& n) { for (int i = 0; i < 5; ++i) if (n.trim() == kVowelNames[i]) return i; return -1; };
    if (const int i = idx (t); i >= 0) return (float) i * 0.25f;
    if (t.contains (">"))   // "OO>OH": somewhere between the two vowels - the middle (the text shows a blend for 8..92 % of a step)
    {
        const int a0 = idx (t.upToFirstOccurrenceOf (">", false, false)), b0 = idx (t.fromFirstOccurrenceOf (">", false, false));
        if (a0 >= 0 && b0 >= 0) return ((float) a0 + (float) b0) * 0.125f;
    }
    return juce::jlimit (0.0f, 1.0f, s.getFloatValue());
}
} // namespace

APVTS::ParameterLayout GrowlProcessor::createLayout()
{
    using namespace juce;
    using A = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pid = [] (const char* id) { return ParameterID { id, 1 }; };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float step, float skew, float def, A a)
    { p.push_back (std::make_unique<AudioParameterFloat> (pid (id), name, NormalisableRange<float> (lo, hi, step, skew), def, a)); };
    auto choice = [&] (const char* id, const char* name, StringArray s, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (pid (id), name, s, def)); };
    auto toggle = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (pid (id), name, def)); };
    auto percent = [] { return A().withLabel ("%").withStringFromValueFunction (pct).withValueFromStringFunction (fromPct); };

    // input / split
    flt ("inGain", "Input Gain", -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, A().withLabel ("dB"));
    flt ("crossover", "Crossover", 60.0f, 200.0f, 1.0f, 1.0f, 100.0f, A().withLabel ("Hz"));
    flt ("subLevel", "Sub Level", -48.0f, 6.0f, 0.1f, 1.0f, 0.0f, A().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return v <= -47.95f ? String ("-inf") : String (v, 1); })
        .withValueFromStringFunction ([] (const String& s) { return s.trim().startsWithIgnoreCase ("-inf") || s.trim().startsWithIgnoreCase ("off") ? -48.0f : s.getFloatValue(); }));
    toggle ("subMono", "Sub Mono", true);
    // drive
    toggle ("driveOn", "Drive On", true);
    flt ("drive", "Drive", 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, percent());
    choice ("satType", "Saturation Type", StringArray { "Tanh", "Tube", "Fold", "Hard Clip", "Diode" }, 0);
    flt ("bias", "Bias", -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, A().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)); })
        .withValueFromStringFunction (fromPct));
    choice ("oversampling", "Oversampling", StringArray { "Off", "2x", "4x" }, 2);
    // formant
    toggle ("formantOn", "Formant On", true);
    flt ("vowel", "Vowel", 0.0f, 1.0f, 0.001f, 1.0f, 0.4f, A().withStringFromValueFunction (vowelText).withValueFromStringFunction (vowelFromText));
    flt ("shift", "Formant Shift", -12.0f, 12.0f, 0.1f, 1.0f, 0.0f, A().withLabel ("st"));
    flt ("focus", "Focus", 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, percent());
    flt ("formantAmount", "Formant Amount", 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, percent());
    choice ("lfoRate", "Vowel LFO Rate", StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 3);
    choice ("lfoShape", "Vowel LFO Shape", StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square" }, 0);
    flt ("lfoDepth", "Vowel LFO Depth", 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, percent());
    // post filter
    toggle ("postOn", "Post Filter On", false);
    choice ("postType", "Post Filter Type", StringArray { "Lowpass", "Highpass" }, 0);
    flt ("postCut", "Post Cutoff", 20.0f, 20000.0f, 0.1f, 0.3f, 9000.0f, A().withLabel ("Hz"));
    flt ("postRes", "Post Resonance", 0.5f, 10.0f, 0.01f, 0.5f, 0.707f, A().withLabel ("Q"));
    // out
    flt ("mix", "Mix", 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, percent());
    flt ("outGain", "Output Gain", -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, A().withLabel ("dB"));
    toggle ("limiter", "Safety Limiter", true);
    return { p.begin(), p.end() };
}

GrowlProcessor::GrowlProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    P = { g ("inGain"), g ("crossover"), g ("subLevel"), g ("subMono"), g ("driveOn"), g ("drive"), g ("satType"), g ("bias"), g ("oversampling"),
          g ("formantOn"), g ("vowel"), g ("shift"), g ("focus"), g ("formantAmount"), g ("lfoRate"), g ("lfoShape"), g ("lfoDepth"),
          g ("postOn"), g ("postType"), g ("postCut"), g ("postRes"), g ("mix"), g ("outGain"), g ("limiter") };
    setLatencySamples (gw::kLatency);
}

bool GrowlProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == l.getMainInputChannelSet();
}

void GrowlProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr, block, juce::jmax (1, getMainBusNumOutputChannels()));
    setLatencySamples (gw::kLatency);
    firstBlock = true;
}

void GrowlProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buffer.getNumChannels(), gw::kMaxChannels);
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    gw::Params p;
    auto on = [] (std::atomic<float>* a) { return a->load() > 0.5f; };
    p.inGainDb = P.inGain->load();  p.crossoverHz = P.xover->load(); p.subLevelDb = P.subLevel->load(); p.subMono = on (P.subMono);
    p.driveOn = on (P.driveOn); p.drive = P.drive->load(); p.satType = (int) P.satType->load(); p.bias = P.bias->load();
    p.oversampling = (int) P.os->load();
    p.formantOn = on (P.formantOn); p.vowel = P.vowel->load(); p.formantShift = P.shift->load(); p.focus = P.focus->load();
    p.formantAmount = P.fAmt->load(); p.lfoRate = (int) P.lfoRate->load(); p.lfoShape = (int) P.lfoShape->load(); p.lfoDepth = P.lfoDepth->load();
    p.postOn = on (P.postOn); p.postType = (int) P.postType->load(); p.postCutHz = P.postCut->load(); p.postRes = P.postRes->load();
    p.mix = P.mix->load(); p.outGainDb = P.outGain->load(); p.limiter = on (P.limiter);
    engine.setParams (p);
    if (firstBlock) { engine.snapSmoothers(); firstBlock = false; }

    gw::Transport tp;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tp.bpm = *bpm;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) { tp.ppq = *ppq; tp.hasPpq = true; }
        }

    engine.process (buffer.getArrayOfWritePointers(), nCh, n, tp);
    vowelDisplay.store (engine.currentVowel(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GrowlProcessor::createEditor() { return new GrowlEditor (*this); }

// State = flat XML of "parameter id -> plain value" read straight from the parameter objects,
// so it is deterministic and does not depend on the ValueTree being in sync.
void GrowlProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("GrowlState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    copyXmlToBinary (xml, dest);
}

void GrowlProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("GrowlState"))
        {
            presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (xml->hasAttribute (r->paramID))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GrowlProcessor(); }
