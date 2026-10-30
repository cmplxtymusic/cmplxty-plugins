#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const cw::PDef kParams[] = {
    { "drOn",       "Drive On",        'b', 0, 1, 1, 1, 0.0f, "" },
    { "drDrive",    "Drive",           'f', 0.0f, 36.0f, 0.1f, 1.0f, 12.0f, "dB" },
    { "drMode",     "Drive Mode",      'c', 0, 3, 1, 1, 0.0f, "", "Soft|Hard|Fold|Crush" },
    { "drTone",     "Drive Tone",      'f', 1000.0f, 20000.0f, 1.0f, 0.35f, 12000.0f, "Hz" },
    { "flOn",       "Filter On",       'b', 0, 1, 1, 1, 0.0f, "" },
    { "flMode",     "Filter Mode",     'c', 0, 3, 1, 1, 0.0f, "", "Low-pass|High-pass|Band-pass|Formant" },
    { "flCut",      "Cutoff",          'f', 20.0f, 20000.0f, 0.1f, 0.25f, 1200.0f, "Hz" },
    { "flReso",     "Resonance",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "flVowel",    "Vowel",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "flLfoRate",  "Filter LFO Rate", 'c', 0, 6, 1, 1, 2.0f, "", "4 bars|2 bars|1 bar|1/2|1/4|1/8|1/16" },
    { "flLfoDepth", "Filter LFO Depth",'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "gtOn",       "Gate On",         'b', 0, 1, 1, 1, 0.0f, "" },
    { "gtRate",     "Gate Rate",       'c', 0, 4, 1, 1, 1.0f, "", "1/8|1/16|1/32|1/8T|1/16T" },
    { "gtPattern",  "Gate Pattern",    'c', 0, 7, 1, 1, 3.0f, "", "Straight|Halves|Offbeat|Trance 1|Trance 2|Syncopate|Build|Stair" },
    { "gtDepth",    "Gate Depth",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "gtSmooth",   "Gate Smooth",     'f', 0.5f, 50.0f, 0.1f, 0.5f, 4.0f, "ms" },
    { "stOn",       "Stutter",         'b', 0, 1, 1, 1, 0.0f, "" },
    { "stLen",      "Stutter Length",  'c', 0, 7, 1, 1, 3.0f, "", "1/2|1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T" },
    { "stGate",     "Stutter Gate",    'f', 0.05f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "stDecay",    "Stutter Decay",   'f', 0.0f, 0.9f, 0.001f, 1.0f, 0.0f, "%" },
    { "tsOn",       "Tape Stop",       'b', 0, 1, 1, 1, 0.0f, "" },
    { "tsStop",     "Stop Time",       'f', 50.0f, 4000.0f, 1.0f, 0.5f, 800.0f, "ms" },
    { "tsStart",    "Start Time",      'f', 0.0f, 4000.0f, 1.0f, 0.5f, 300.0f, "ms" },
    { "djFilter",   "DJ Filter",       'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "dj" },
    { "intensity",  "Intensity",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "mix",        "Mix",             'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "output",     "Output",          'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "bypass",       "Bypass",        'b', 0, 1, 1, 1, 0.0f, "" },
};
enum { kDrOn, kDrDrive, kDrMode, kDrTone, kFlOn, kFlMode, kFlCut, kFlReso, kFlVowel, kFlLfoRate, kFlLfoDepth, kGtOn, kGtRate, kGtPattern, kGtDepth, kGtSmooth,
       kStOn, kStLen, kStGate, kStDecay, kTsOn, kTsStop, kTsStart, kDj, kIntensity, kMix, kOutput, kBypass, kNumParams };
static_assert (sizeof (kParams) / sizeof (kParams[0]) == kNumParams, "parameter table and index enum disagree");
} // namespace

NeonFXProcessor::NeonFXProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", cw::makeLayout (kParams)),
      presets (apvts, &undoManager, "Neon FX")
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds.addIfNotAlreadyThere ("bypass");   // presets never bypass / un-bypass
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
}

NeonFXProcessor::~NeonFXProcessor() = default;

bool NeonFXProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet() == out;
}

cw::dsp::Params NeonFXProcessor::currentParams() const
{
    cw::dsp::Params p;
    auto b = [this] (int i) { return val (i) > 0.5f; };
    auto c = [this] (int i, int hi) { return juce::jlimit (0, hi, (int) val (i)); };
    p.drOn = b (kDrOn); p.drDb = val (kDrDrive); p.drMode = c (kDrMode, 3); p.drTone = val (kDrTone);
    p.flOn = b (kFlOn); p.flMode = c (kFlMode, 3); p.flCut = val (kFlCut); p.flReso = val (kFlReso); p.flVowel = val (kFlVowel); p.flLfoRate = c (kFlLfoRate, 6); p.flLfoDepth = val (kFlLfoDepth);
    p.gtOn = b (kGtOn); p.gtRate = c (kGtRate, 4); p.gtPattern = c (kGtPattern, 7); p.gtDepth = val (kGtDepth); p.gtSmooth = val (kGtSmooth);
    p.stOn = b (kStOn); p.stLen = c (kStLen, 7); p.stGate = val (kStGate); p.stDecay = val (kStDecay);
    p.tsOn = b (kTsOn); p.tsStop = val (kTsStop); p.tsStart = val (kTsStart);
    p.dj = val (kDj); p.intensity = val (kIntensity); p.mix = val (kMix); p.outDb = val (kOutput);
    return p;
}

void NeonFXProcessor::prepareToPlay (double sr, int block)
{
    bypass.prepare (sr, block, juce::jmax (1, getMainBusNumOutputChannels()), 0);
    engine.prepare (sr > 0 ? sr : 48000.0);
    engine.setParams (currentParams());
    scratchR.assign ((size_t) juce::jmax (block, 4096), 0.0f);
    setLatencySamples (0);
}

void NeonFXProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (nch <= 0 || n <= 0) return;
    if ((int) scratchR.size() < n) scratchR.assign ((size_t) n, 0.0f);   // host broke its block-size promise: allocate once rather than drop audio
    bypass.pushDry (buffer);
    engine.setParams (currentParams());
    cw::dsp::Transport t;
    if (testTransport) t = *testTransport;
    else if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (auto q = pos->getPpqPosition()) { t.ppq = *q; t.playing = pos->getIsPlaying(); }
        }
    hostBpm.store ((float) t.bpm, std::memory_order_relaxed); hostPlaying.store (t.playing, std::memory_order_relaxed);
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : scratchR.data();
    if (nch == 1) std::copy (L, L + n, R);
    engine.render (L, R, n, t);
    if (testTransport && testTransport->playing) testTransport->ppq += n * t.bpm / 60.0 / getSampleRate();
    if (nch == 1) for (int i = 0; i < n; ++i) L[i] = (L[i] + R[i]) * 0.5f;
    for (int c = 2; c < nch; ++c) buffer.clear (c, 0, n);
    bypass.mix (buffer, bypassParam->get());
}

void NeonFXProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void NeonFXProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NeonFXProcessor::createEditor() { return new NeonFXEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeonFXProcessor(); }
