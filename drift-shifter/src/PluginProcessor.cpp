#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
juce::String signedHz (float v, int)
{
    const int r = juce::roundToInt (v);
    return (r > 0 ? "+" : "") + juce::String (r);
}
juce::String signedHz1 (float v, int)
{
    const float r = std::round (v * 10.0f) / 10.0f + 0.0f;   // + 0.0f turns -0.0 into 0.0 (no "-0.0" text)
    return (r > 0.0f ? "+" : "") + juce::String (r, 1);
}
float parseNumber (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    const float v = t.retainCharacters ("0123456789.-+").getFloatValue();
    return t.endsWithChar ('k') || t.endsWith ("khz") ? v * 1000.0f : v;   // "+1.5k" = +1500
}
juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); }
float fromPct (const juce::String& s) { return s.getFloatValue() / 100.0f; }
juce::String hzText (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + "k" : juce::String (juce::roundToInt (v)); }
float hzFromText (const juce::String& s)
{
    return parseNumber (s);   // parseNumber already applies the "k" suffix
}
// symmetric range around zero with finer resolution near the middle: x in 0..1 -> v = sign * |2x-1|^curve * max
juce::NormalisableRange<float> symmetric (float maxV, float curve, float step)
{
    return { -maxV, maxV,
             [=] (float, float, float x) { const float v = 2.0f * x - 1.0f; return std::copysign (std::pow (std::fabs (v), curve), v) * maxV; },
             [=] (float, float, float v) { const float n = juce::jlimit (-1.0f, 1.0f, v / maxV); return 0.5f + 0.5f * std::copysign (std::pow (std::fabs (n), 1.0f / curve), n); },
             [=] (float, float, float v) { return juce::jlimit (-maxV, maxV, step > 0.0f ? std::round (v / step) * step : v); } };
}
} // namespace

APVTS::ParameterLayout DriftProcessor::createLayout()
{
    using namespace juce;
    using A = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pid = [] (const char* id) { return ParameterID { id, 1 }; };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float step, float skew, float def, A a)
    { p.push_back (std::make_unique<AudioParameterFloat> (pid (id), name, NormalisableRange<float> (lo, hi, step, skew), def, a)); };
    auto sym = [&] (const char* id, const char* name, float maxV, float curve, float step, float def, A a)
    { p.push_back (std::make_unique<AudioParameterFloat> (pid (id), name, symmetric (maxV, curve, step), def, a)); };
    auto choice = [&] (const char* id, const char* name, StringArray s, int def)
    { p.push_back (std::make_unique<AudioParameterChoice> (pid (id), name, s, def)); };
    auto toggle = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (pid (id), name, def)); };
    auto percent = [] { return A().withLabel ("%").withStringFromValueFunction (pct).withValueFromStringFunction (fromPct); };

    // shifter
    sym ("coarse", "Coarse Shift", 10000.0f, 2.0f, 1.0f, 0.0f, A().withLabel ("Hz").withStringFromValueFunction (signedHz).withValueFromStringFunction (parseNumber));
    sym ("fine", "Fine Shift", 500.0f, 1.0f, 0.1f, 0.0f, A().withLabel ("Hz").withStringFromValueFunction (signedHz1).withValueFromStringFunction (parseNumber));
    toggle ("shiftOn", "Shift On", true);
    sym ("spread", "Stereo Spread", 500.0f, 1.5f, 0.1f, 0.0f, A().withLabel ("Hz").withStringFromValueFunction (signedHz1).withValueFromStringFunction (parseNumber));
    toggle ("wide", "Wide", true);
    flt ("mix", "Mix", 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, percent());
    flt ("feedback", "Feedback", 0.0f, 0.95f, 0.001f, 1.0f, 0.0f, percent());
    // keytrack
    toggle ("keyOn", "Keytrack On", false);
    sym ("keyAmt", "Keytrack Amount", 100.0f, 2.0f, 0.1f, 10.0f, A().withLabel ("Hz/st").withStringFromValueFunction (signedHz1).withValueFromStringFunction (parseNumber));
    // bands
    toggle ("lowBand", "Low Band", true);
    toggle ("highBand", "High Band", true);
    flt ("xoLow", "Low Crossover", 40.0f, 8000.0f, 1.0f, 0.35f, 200.0f, A().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (hzFromText));
    flt ("xoHigh", "High Crossover", 80.0f, 16000.0f, 1.0f, 0.35f, 4000.0f, A().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (hzFromText));
    choice ("slope", "Crossover Slope", StringArray { "24 dB", "48 dB", "96 dB" }, 1);
    toggle ("soloLow", "Solo Low", false);
    toggle ("soloMid", "Solo Mid", false);
    toggle ("soloHigh", "Solo High", false);
    // display
    toggle ("eco", "Eco Mode", false);
    return { p.begin(), p.end() };
}

DriftProcessor::DriftProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", createLayout())
{
    presets.keepIds = { "eco" };   // a display preference, not part of the sound
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    P = { g ("coarse"), g ("fine"), g ("shiftOn"), g ("spread"), g ("wide"), g ("mix"), g ("feedback"), g ("keyOn"), g ("keyAmt"),
          g ("lowBand"), g ("highBand"), g ("xoLow"), g ("xoHigh"), g ("slope"), g ("soloLow"), g ("soloMid"), g ("soloHigh") };
    setLatencySamples (df::kLatency);
}

bool DriftProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == l.getMainInputChannelSet();
}

void DriftProcessor::prepareToPlay (double sr, int)
{
    engine.prepare (sr, juce::jmax (1, getMainBusNumOutputChannels()));
    heldNote.store (-1);
    setLatencySamples (df::kLatency);
}

void DriftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buffer.getNumChannels(), df::kMaxChannels);
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    df::Params p;
    auto on = [] (std::atomic<float>* a) { return a->load() > 0.5f; };
    p.coarseHz = P.coarse->load(); p.fineHz = P.fine->load(); p.shiftOn = on (P.shiftOn);
    p.spreadHz = P.spread->load(); p.wide = on (P.wide); p.mix = P.mix->load(); p.feedback = P.feedback->load();
    p.keyOn = on (P.keyOn); p.keyHzPerSemi = P.keyAmt->load();
    p.lowBand = on (P.lowBand); p.highBand = on (P.highBand); p.xoLowHz = P.xoLow->load(); p.xoHighHz = P.xoHigh->load();
    p.slope = juce::jlimit (0, (int) df::NumSlopes - 1, (int) std::lround (P.slope->load()));
    p.soloLow = on (P.soloLow); p.soloMid = on (P.soloMid); p.soloHigh = on (P.soloHigh);
    engine.setParams (p);

    // MIDI notes drive keytrack; the block is split at every note event so that the offset follows sample-accurately
    float* ptrs[df::kMaxChannels] = { nullptr, nullptr };
    for (int c = 0; c < nCh; ++c) ptrs[c] = buffer.getWritePointer (c);
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        float* off[df::kMaxChannels] = { ptrs[0] ? ptrs[0] + pos : nullptr, ptrs[1] ? ptrs[1] + pos : nullptr };
        engine.process (off, nCh, end - pos);
        pos = end;
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! (m.isNoteOn() || m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())) continue;
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else engine.allNotesOff();
    }
    renderTo (n);
    heldNote.store (engine.currentNote(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* DriftProcessor::createEditor() { return new DriftEditor (*this); }

// State = flat XML of "parameter id -> plain value" read straight from the parameter objects (so it is deterministic and does not
// depend on the ValueTree being in sync), plus the editor size.  Band layout (bands on/off, solos, crossovers, slope) are parameters.
void DriftProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("DriftState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml.setAttribute ("uiW", uiWidth.load()); xml.setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (xml, dest);
}

void DriftProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("DriftState"))
        {
            presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (xml->hasAttribute (r->paramID))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)));
            const int w = xml->getIntAttribute ("uiW", 0), h = xml->getIntAttribute ("uiH", 0);
            if (w >= 300 && w <= 8000 && h >= 200 && h <= 6000) { uiWidth.store (w); uiHeight.store (h); }
            // the editor, if open, follows the saved size
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::AudioProcessorEditor> (getActiveEditor())]
                                             { if (safe != nullptr) if (auto* e = dynamic_cast<DriftEditor*> (safe.getComponent())) e->applySavedSize(); });
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DriftProcessor(); }
