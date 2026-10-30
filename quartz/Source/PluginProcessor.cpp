#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>

using namespace qz;

static juce::String formatValue (const ParamSpec& s, float v)
{
    const juce::String u (s.unit);
    if (s.kind == Kind::Choice) { auto c = juce::StringArray::fromTokens (juce::String (s.choices), "|", ""); return c[juce::jlimit (0, c.size() - 1, (int) std::lround (v))]; }
    if (s.kind == Kind::Bool) return v > 0.5f ? "On" : "Off";
    if (u == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (u == "dB") { const bool whole = std::string (s.id) == "gate" || std::string (s.id) == "drive"; return (v > (whole ? 0.5f : 0.05f) ? "+" : "") + (whole ? juce::String (juce::roundToInt (v)) : juce::String (v, 1)) + " dB"; }
    if (u == "st") return (v > -0.05f ? "+" : "") + juce::String (v, 1) + " st";
    if (u == "ms") return juce::String (juce::roundToInt (v)) + " ms";
    if (u == "s")  return juce::String (v, 1) + " s";
    if (u == "ct") return juce::String (juce::roundToInt (v)) + " ct";
    if (u == "bit") return juce::String (juce::roundToInt (v)) + " bit";
    if (u == "%") return juce::String (juce::roundToInt (v)) + " %";
    if (std::string (s.id) == "tone") return (v > 0.05f ? "+" : "") + juce::String (v, 1);
    if (std::string (s.id) == "gender") return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v));
    if (std::string (s.id) == "adrive") return juce::String (v, 1);
    return juce::String (v, 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout QuartzProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& s = kParams[i];
        const juce::ParameterID pid { s.id, 1 };
        if (s.kind == Kind::Bool)
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
        else if (s.kind == Kind::Choice)
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, juce::StringArray::fromTokens (juce::String (s.choices), "|", ""), (int) s.def));
        else
        {
            juce::NormalisableRange<float> r (s.mn, s.mx, s.step);
            if (s.skewMid > s.mn && s.skewMid < s.mx) r.setSkewForCentre (s.skewMid);
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, r, s.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([s] (float v, int) { return formatValue (s, v); })
                                                    .withValueFromStringFunction ([s] (const juce::String& t) { return t.retainCharacters ("0123456789.-+").getFloatValue() * (t.containsIgnoreCase ("k") && std::string (s.unit) == "Hz" ? 1000.0f : 1.0f); })));
        }
    }
    return layout;
}

QuartzProcessor::QuartzProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QUARTZ", createLayout())
{
    params.resize ((size_t) kNumParams); raw.resize ((size_t) kNumParams);
    for (int i = 0; i < kNumParams; ++i)
    {
        params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kParams[i].id));
        raw[(size_t) i] = apvts.getRawParameterValue (kParams[i].id);
    }
    for (auto& s : scope) s.store (0.0f);
    presets.rescan (secQuartz); presets.rescan (secAlter);
    // New instances start on the "Init" presets (Quartz neutral at full INTENSITY, ALTER neutral)
    for (int s = 0; s < 2; ++s) { presetIndex[s] = 0; presetName[s] = presets.nameAt (s, 0); }
    setParamValue (pIntensity, kQuartzPresets[0].e[0].value);
}

bool QuartzProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet(), i = l.getMainInputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && i == o;
}

void QuartzProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    // the wet path measures one sample later than engine.latency() (2144 vs 2143 at 48 kHz); report and align to the measured value
    const int lat = engine.latency() + 1;
    setLatencySamples (lat);
    bypass.prepare (sampleRate, std::max (samplesPerBlock, 16384), 2, lat);
    scratch.setSize (2, std::max (samplesPerBlock, 16384), false, true, true);
}

void QuartzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    for (int c = 0; c < kNumParams; ++c) pv.v[c] = raw[(size_t) c]->load (std::memory_order_relaxed);
    for (int c = nch; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    if (nch == 0 || n == 0) return;
    const bool bypassed = pv.v[pBypass] > 0.5f;
    pv.v[pBypass] = 0.0f;   // the engine always runs; SmoothBypass below crossfades to the aligned dry signal
    bypass.pushDry (buffer);

    float ip[2] = { 0, 0 };
    for (int c = 0; c < std::min (nch, 2); ++c) ip[c] = buffer.getMagnitude (c, 0, n);
    if (nch == 1) ip[1] = ip[0];

    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (n - pos, scratch.getNumSamples());
        float* L = scratch.getWritePointer (0); float* R = scratch.getWritePointer (1);
        std::copy (buffer.getReadPointer (0) + pos, buffer.getReadPointer (0) + pos + len, L);
        std::copy (buffer.getReadPointer (nch > 1 ? 1 : 0) + pos, buffer.getReadPointer (nch > 1 ? 1 : 0) + pos + len, R);
        engine.process (L, R, len, pv);
        std::copy (L, L + len, buffer.getWritePointer (0) + pos);
        if (nch > 1) std::copy (R, R + len, buffer.getWritePointer (1) + pos);
        int w = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < len; ++i) { scope[(size_t) (w & (kScope - 1))].store (0.5f * (L[i] + R[i]), std::memory_order_relaxed); ++w; }
        scopePos.store (w, std::memory_order_release);
        pos += len;
    }
    bypass.mix (buffer, bypassed);
    const float op0 = buffer.getMagnitude (0, 0, n), op1 = nch > 1 ? buffer.getMagnitude (1, 0, n) : op0;
    auto bump = [] (std::atomic<float>& a, float v) { float o = a.load (std::memory_order_relaxed); if (v > o) a.store (v, std::memory_order_relaxed); };
    bump (inPeak[0], ip[0]); bump (inPeak[1], ip[1]); bump (outPeak[0], op0); bump (outPeak[1], op1);
    const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const float load = (float) (dt / ((double) n / sr));
    cpuLoad.store (cpuLoad.load (std::memory_order_relaxed) * 0.9f + 0.1f * load, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* QuartzProcessor::createEditor() { return new QuartzEditor (*this); }

// ------------------------------------------------------------------ state
void QuartzProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("qzVersion", QUARTZ_VERSION_STRING, nullptr);
    state.setProperty ("qPresetName", presetName[0], nullptr);
    state.setProperty ("aPresetName", presetName[1], nullptr);
    state.setProperty ("qPresetDirty", presetDirty[0], nullptr);
    state.setProperty ("aPresetDirty", presetDirty[1], nullptr);
    state.setProperty ("editorWidth", editorWidth, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void QuartzProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (! xml->hasTagName (apvts.state.getType())) return;
        auto vt = juce::ValueTree::fromXml (*xml);
        apvts.replaceState (vt);
        presets.rescan (secQuartz); presets.rescan (secAlter);
        presetName[0] = vt.getProperty ("qPresetName", "").toString();
        presetName[1] = vt.getProperty ("aPresetName", "").toString();
        presetDirty[0] = (bool) vt.getProperty ("qPresetDirty", false);
        presetDirty[1] = (bool) vt.getProperty ("aPresetDirty", false);
        for (int s = 0; s < 2; ++s) presetIndex[s] = presets.indexOfName (s, presetName[s]);
        editorWidth = juce::jlimit (660, 2200, (int) vt.getProperty ("editorWidth", 1100));
        undoStack.clear(); redoStack.clear();
        presetChanged.sendChangeMessage(); historyChanged.sendChangeMessage();
    }
}

// ------------------------------------------------------------------ presets / history
void QuartzProcessor::setParamValue (int index, float value)
{
    auto* p = params[(size_t) index];
    const float n = p->convertTo0to1 (value);
    if (std::abs (n - p->getValue()) < 1.0e-7f) return;
    p->beginChangeGesture(); p->setValueNotifyingHost (n); p->endChangeGesture();
}

void QuartzProcessor::loadPreset (int section, int index)
{
    if (index < 0 || index >= presets.numTotal (section)) return;
    pushUndo();
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = kParams[i].def;
    if (! presets.read (section, index, v, kNumParams)) return;
    for (int i = 0; i < kNumParams; ++i) if (inSectionPreset (section, i)) setParamValue (i, v[i]);
    presetIndex[section] = index; presetName[section] = presets.nameAt (section, index); presetDirty[section] = false;
    presetChanged.sendChangeMessage();
}

void QuartzProcessor::stepPreset (int section, int delta)
{
    const int n = presets.numTotal (section);
    int cur = presetIndex[section] < 0 ? (delta > 0 ? -1 : 0) : presetIndex[section];
    loadPreset (section, ((cur + delta) % n + n) % n);
}

void QuartzProcessor::resetSection (int section) { loadPreset (section, 0); }

bool QuartzProcessor::saveUserPreset (int section, const juce::String& name)
{
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = params[(size_t) i]->convertFrom0to1 (params[(size_t) i]->getValue());
    const int idx = presets.save (section, name, v, kNumParams);
    if (idx < 0) return false;
    presetIndex[section] = idx; presetName[section] = presets.nameAt (section, idx); presetDirty[section] = false;
    presetChanged.sendChangeMessage();
    return true;
}

void QuartzProcessor::deleteCurrentUserPreset (int section)
{
    if (presets.isUser (section, presetIndex[section])) { presets.removeUser (section, presetIndex[section]); presetIndex[section] = -1; presetDirty[section] = true; presetChanged.sendChangeMessage(); }
}

void QuartzProcessor::markEdited (int section)
{
    if (section < 0 || section > 1 || presetDirty[section]) return;
    presetDirty[section] = true; presetChanged.sendChangeMessage();
}

QuartzProcessor::Snapshot QuartzProcessor::takeSnapshot() const
{
    Snapshot s;
    for (int i = 0; i < kNumParams; ++i) s.v[(size_t) i] = params[(size_t) i]->convertFrom0to1 (params[(size_t) i]->getValue());
    for (int k = 0; k < 2; ++k) { s.name[k] = presetName[k]; s.index[k] = presetIndex[k]; }
    return s;
}

void QuartzProcessor::applySnapshot (const Snapshot& s)
{
    for (int i = 0; i < kNumParams; ++i) if (i != pBypass) setParamValue (i, s.v[(size_t) i]);
    for (int k = 0; k < 2; ++k) { presetName[k] = s.name[k]; presetIndex[k] = presets.indexOfName (k, s.name[k]); }
    presetChanged.sendChangeMessage();
}

void QuartzProcessor::pushUndo()
{
    auto s = takeSnapshot();
    if (! undoStack.empty() && undoStack.back().v == s.v) return;
    undoStack.push_back (s); if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear(); historyChanged.sendChangeMessage();
}

void QuartzProcessor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (takeSnapshot());
    auto s = undoStack.back(); undoStack.pop_back(); applySnapshot (s); historyChanged.sendChangeMessage();
}

void QuartzProcessor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (takeSnapshot());
    auto s = redoStack.back(); redoStack.pop_back(); applySnapshot (s); historyChanged.sendChangeMessage();
}

void QuartzProcessor::toggleAB()
{
    auto cur = takeSnapshot();
    if (! abOtherValid) abOther = cur;      // first switch: B starts as a copy of A
    auto other = abOther; abOther = cur; abOtherValid = true;
    abSlot = 1 - abSlot;
    applySnapshot (other);
    historyChanged.sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new QuartzProcessor(); }
