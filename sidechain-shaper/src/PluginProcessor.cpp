#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const sc::PDef kParams[] = {
    { "mode",      "Mode",            'c', 0, 1, 1, 1, 0.0f, "", "Tempo|Sidechain" },
    { "rate",      "Length",          'c', 0, sc::dsp::kNumRates - 1, 1, 1, 3.0f, "", sc::dsp::rateNames() },
    { "offset",    "Offset",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "depth",     "Depth",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "smooth",    "Smooth",          'f', 0.0f, 50.0f, 0.01f, 0.4f, 1.5f, "ms" },
    { "stereo",    "Stereo Offset",   'f', 0.0f, 0.5f, 0.001f, 1.0f, 0.0f, "%" },
    { "source",    "Trigger Source",  'c', 0, 1, 1, 1, 0.0f, "", "Sidechain|Input" },
    { "threshold", "Threshold",       'f', -60.0f, 0.0f, 0.1f, 1.0f, -24.0f, "dB" },
    { "focus",     "Focus",           'f', 40.0f, 20000.0f, 1.0f, 0.3f, 20000.0f, "Hz" },
    { "hold",      "Hold",            'f', 10.0f, 500.0f, 0.1f, 0.5f, 60.0f, "ms" },
    { "mix",       "Mix",             'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "gain",      "Output",          'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "bypass",    "Bypass",          'b', 0, 1, 1, 1, 0.0f, "" },
};
} // namespace

SidechainShaperProcessor::SidechainShaperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "STATE", sc::makeLayout (kParams)),
      presets (apvts, &undoManager, "Sidechain Shaper")
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pMode = g ("mode"); pRate = g ("rate"); pOffset = g ("offset"); pDepth = g ("depth"); pSmooth = g ("smooth"); pStereo = g ("stereo");
    pSource = g ("source"); pThr = g ("threshold"); pFocus = g ("focus"); pHold = g ("hold"); pMix = g ("mix"); pGain = g ("gain");
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds.addIfNotAlreadyThere ("bypass");   // presets never bypass / un-bypass
    ensureCurve();
    refreshCurve();
    apvts.state.addListener (this);

    // presets carry the curve: user presets as text, factory presets as a shape name; nullptr (Init) = the default curve
    presets.saveExtra = [this] (juce::XmlElement& x) { x.setAttribute ("curve", curveText()); };
    presets.loadExtra = [this] (const juce::XmlElement* x)
    {
        sc::dsp::Curve c = sc::dsp::defaultCurve();
        if (x != nullptr && x->hasAttribute ("curve")) c = sc::dsp::parseCurve (x->getStringAttribute ("curve").toStdString());
        else if (x != nullptr && x->hasAttribute ("shape")) c = sc::dsp::shapeByName (x->getStringAttribute ("shape").toStdString());
        apvts.state.setProperty ("curve", juce::String (sc::dsp::formatCurve (c)), &undoManager);   // part of the "Load preset" undo step
    };
}

SidechainShaperProcessor::~SidechainShaperProcessor() { apvts.state.removeListener (this); }

void SidechainShaperProcessor::ensureCurve()
{
    if (curveText().isEmpty()) apvts.state.setProperty ("curve", juce::String (sc::dsp::formatCurve (sc::dsp::defaultCurve())), nullptr);
}

void SidechainShaperProcessor::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& id)
{
    if (t == apvts.state && id == juce::Identifier ("curve")) refreshCurve();
}

void SidechainShaperProcessor::setCurve (const sc::dsp::Curve& c, bool undoable, const juce::String& transaction)
{
    if (undoable) undoManager.beginNewTransaction (transaction);
    apvts.state.setProperty ("curve", juce::String (sc::dsp::formatCurve (sc::dsp::sanitize (c))), undoable ? &undoManager : nullptr);
}

void SidechainShaperProcessor::commitCurve (const juce::String& before, const sc::dsp::Curve& after)
{
    const juce::String now (sc::dsp::formatCurve (sc::dsp::sanitize (after)));
    apvts.state.setProperty ("curve", before, nullptr);
    if (now == before) return;
    undoManager.beginNewTransaction ("Edit curve");
    apvts.state.setProperty ("curve", now, &undoManager);
}

bool SidechainShaperProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (! ((out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet())) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto& s = l.inputBuses.getReference (1);
        if (! (s.isDisabled() || s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo())) return false;
    }
    return true;
}

sc::dsp::Params SidechainShaperProcessor::currentParams() const
{
    sc::dsp::Params p;
    p.mode = juce::jlimit (0, 1, (int) pMode->load()); p.rate = juce::jlimit (0, sc::dsp::kNumRates - 1, (int) pRate->load());
    p.offset = pOffset->load(); p.depth = pDepth->load(); p.smoothMs = pSmooth->load(); p.stereo = pStereo->load();
    p.source = juce::jlimit (0, 1, (int) pSource->load()); p.thrDb = pThr->load(); p.focusHz = pFocus->load(); p.holdMs = pHold->load();
    p.mix = pMix->load(); p.gainDb = pGain->load();
    return p;
}

void SidechainShaperProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.setParams (currentParams());
    engine.prepare (sr > 0 ? sr : 48000.0, juce::jmax (1, getMainBusNumOutputChannels()));
    bypass.prepare (sr, maxBlock, juce::jmax (1, getMainBusNumOutputChannels()), 0);
}

void SidechainShaperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midi.clear();
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    for (int c = mainIn.getNumChannels(); c < mainOut.getNumChannels(); ++c) mainOut.clear (c, 0, n);
    const int nch = juce::jmin (2, mainOut.getNumChannels());
    if (nch <= 0 || n <= 0) return;
    bypass.pushDry (mainOut);

    const float* scPtr[2] = { nullptr, nullptr }; int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
        {
            auto scb = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scb.getNumChannels());
            for (int c = 0; c < scCh; ++c) scPtr[c] = scb.getReadPointer (c);
        }

    sc::dsp::TimeInfo ti;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) ti.bpm = *b;
            if (auto q = pos->getPpqPosition()) { ti.ppq = *q; ti.hasPpq = true; }
            ti.playing = pos->getIsPlaying();
            if (auto ts = pos->getTimeSignature()) if (ts->denominator > 0 && ts->numerator > 0) ti.barBeats = ts->numerator * 4.0 / ts->denominator;
        }
    hostBpm.store ((float) ti.bpm, std::memory_order_relaxed); hostPlaying.store (ti.playing, std::memory_order_relaxed);

    engine.setParams (currentParams());
    engine.process (mainOut.getArrayOfWritePointers(), nch, n, scCh > 0 ? scPtr : nullptr, scCh, ti);
    bypass.mix (mainOut, bypassParam->get());
}

void SidechainShaperProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void SidechainShaperProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    ensureCurve();
    refreshCurve();
}

juce::AudioProcessorEditor* SidechainShaperProcessor::createEditor() { return new SidechainShaperEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SidechainShaperProcessor(); }
