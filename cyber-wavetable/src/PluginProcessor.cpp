#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
#define CW_BANKS "Analog|Digital|Vocal|Sync|Metal|Chip"
#define CW_SRC "Off|LFO 1|LFO 2|Mod Env|Velocity|Mod Wheel|Note|Pressure"
#define CW_DST "Off|A Position|B Position|Both Positions|A Pitch|B Pitch|Pitch|Cutoff|Resonance|A Level|B Level|Detune|Pan|Amp|Noise"
const cw::PDef kParams[] = {
    { "aOn",     "Osc A On",        'b', 0, 1, 1, 1, 1.0f, "" },
    { "aBank",   "Osc A Table",     'c', 0, 5, 1, 1, 0.0f, "", CW_BANKS },
    { "aPos",    "Osc A Position",  'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "aSemi",   "Osc A Semitone",  'f', -24.0f, 24.0f, 1.0f, 1.0f, 0.0f, "st" },
    { "aFine",   "Osc A Fine",      'f', -100.0f, 100.0f, 0.1f, 1.0f, 0.0f, "ct" },
    { "aUni",    "Osc A Unison",    'f', 1.0f, 7.0f, 1.0f, 1.0f, 1.0f, "int" },
    { "aDetune", "Osc A Detune",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.25f, "%" },
    { "aLevel",  "Osc A Level",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.8f, "%" },
    { "bOn",     "Osc B On",        'b', 0, 1, 1, 1, 0.0f, "" },
    { "bBank",   "Osc B Table",     'c', 0, 5, 1, 1, 1.0f, "", CW_BANKS },
    { "bPos",    "Osc B Position",  'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "bSemi",   "Osc B Semitone",  'f', -24.0f, 24.0f, 1.0f, 1.0f, 0.0f, "st" },
    { "bFine",   "Osc B Fine",      'f', -100.0f, 100.0f, 0.1f, 1.0f, 0.0f, "ct" },
    { "bUni",    "Osc B Unison",    'f', 1.0f, 7.0f, 1.0f, 1.0f, 1.0f, "int" },
    { "bDetune", "Osc B Detune",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.25f, "%" },
    { "bLevel",  "Osc B Level",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "sub",     "Sub",             'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "noise",   "Noise",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "width",   "Unison Width",    'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fType",   "Filter Type",     'c', 0, 3, 1, 1, 1.0f, "", "LP 12|LP 24|Band Pass|High Pass" },
    { "cutoff",  "Cutoff",          'f', 20.0f, 20000.0f, 0.1f, 0.25f, 12000.0f, "Hz" },
    { "reso",    "Resonance",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.15f, "%" },
    { "fDrive",  "Drive",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "fEnv",    "Filter Env",      'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "fKey",    "Key Track",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "ampA",    "Amp Attack",      'f', 1.0f, 10000.0f, 0.1f, 0.25f, 5.0f, "ms" },
    { "ampD",    "Amp Decay",       'f', 1.0f, 10000.0f, 0.1f, 0.25f, 300.0f, "ms" },
    { "ampS",    "Amp Sustain",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.8f, "%" },
    { "ampR",    "Amp Release",     'f', 1.0f, 10000.0f, 0.1f, 0.25f, 300.0f, "ms" },
    { "modA",    "Mod Attack",      'f', 1.0f, 10000.0f, 0.1f, 0.25f, 5.0f, "ms" },
    { "modD",    "Mod Decay",       'f', 1.0f, 10000.0f, 0.1f, 0.25f, 400.0f, "ms" },
    { "modS",    "Mod Sustain",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "modR",    "Mod Release",     'f', 1.0f, 10000.0f, 0.1f, 0.25f, 300.0f, "ms" },
    { "l1Shape", "LFO 1 Shape",     'c', 0, 4, 1, 1, 0.0f, "", "Sine|Triangle|Saw|Square|S&H" },
    { "l1Rate",  "LFO 1 Rate",      'f', 0.01f, 30.0f, 0.01f, 0.35f, 2.0f, "Hz" },
    { "l2Shape", "LFO 2 Shape",     'c', 0, 4, 1, 1, 1.0f, "", "Sine|Triangle|Saw|Square|S&H" },
    { "l2Rate",  "LFO 2 Rate",      'f', 0.01f, 30.0f, 0.01f, 0.35f, 0.3f, "Hz" },
    { "voices",  "Voices",          'f', 1.0f, 16.0f, 1.0f, 1.0f, 8.0f, "int" },
    { "glide",   "Glide",           'f', 0.0f, 2000.0f, 0.1f, 0.35f, 0.0f, "ms" },
    { "volume",  "Volume",          'f', -36.0f, 6.0f, 0.1f, 1.0f, -6.0f, "dB" },
    { "m1Src", "Mod 1 Source", 'c', 0, 7, 1, 1, 0.0f, "", CW_SRC }, { "m1Dst", "Mod 1 Target", 'c', 0, 14, 1, 1, 0.0f, "", CW_DST }, { "m1Amt", "Mod 1 Amount", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "m2Src", "Mod 2 Source", 'c', 0, 7, 1, 1, 0.0f, "", CW_SRC }, { "m2Dst", "Mod 2 Target", 'c', 0, 14, 1, 1, 0.0f, "", CW_DST }, { "m2Amt", "Mod 2 Amount", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "m3Src", "Mod 3 Source", 'c', 0, 7, 1, 1, 0.0f, "", CW_SRC }, { "m3Dst", "Mod 3 Target", 'c', 0, 14, 1, 1, 0.0f, "", CW_DST }, { "m3Amt", "Mod 3 Amount", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "m4Src", "Mod 4 Source", 'c', 0, 7, 1, 1, 0.0f, "", CW_SRC }, { "m4Dst", "Mod 4 Target", 'c', 0, 14, 1, 1, 0.0f, "", CW_DST }, { "m4Amt", "Mod 4 Amount", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
};
enum I { aOn, aBank, aPos, aSemi, aFine, aUni, aDetune, aLevel, bOn, bBank, bPos, bSemi, bFine, bUni, bDetune, bLevel, sub, noise, width,
         fType, cutoff, reso, fDrive, fEnv, fKey, ampA, ampD, ampS, ampR, modA, modD, modS, modR, l1Shape, l1Rate, l2Shape, l2Rate,
         voices, glide, volume, m1Src, kNumParams = m1Src + 12 };
static_assert (sizeof (kParams) / sizeof (kParams[0]) == kNumParams, "parameter table and index enum disagree");
} // namespace

CyberWavetableProcessor::CyberWavetableProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", cw::makeLayout (kParams)),
      presets (apvts, &undoManager, "Cyber Wavetable")
{
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
}

bool CyberWavetableProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet().isDisabled();
}

double CyberWavetableProcessor::getTailLengthSeconds() const { return val (ampR) / 1000.0; }

cw::dsp::Params CyberWavetableProcessor::currentParams() const
{
    cw::dsp::Params p;
    auto osc = [this] (int b) { cw::dsp::OscP o; o.on = val (b) > 0.5f; o.bank = juce::jlimit (0, 5, (int) val (b + 1)); o.pos = val (b + 2); o.semi = val (b + 3);
                                o.fine = val (b + 4); o.uni = juce::jlimit (1, 7, juce::roundToInt (val (b + 5))); o.detune = val (b + 6); o.level = val (b + 7); return o; };
    p.a = osc (aOn); p.b = osc (bOn);
    p.sub = val (sub); p.noise = val (noise); p.width = val (width);
    p.fType = (cw::dsp::FilterType) juce::jlimit (0, 3, (int) val (fType)); p.cutoff = val (cutoff); p.reso = val (reso); p.drive = val (fDrive); p.fEnv = val (fEnv); p.fKey = val (fKey);
    p.aA = val (ampA) / 1000.0f; p.aD = val (ampD) / 1000.0f; p.aS = val (ampS); p.aR = val (ampR) / 1000.0f;
    p.mA = val (modA) / 1000.0f; p.mD = val (modD) / 1000.0f; p.mS = val (modS); p.mR = val (modR) / 1000.0f;
    p.l1Shape = juce::jlimit (0, 4, (int) val (l1Shape)); p.l1Rate = val (l1Rate); p.l2Shape = juce::jlimit (0, 4, (int) val (l2Shape)); p.l2Rate = val (l2Rate);
    p.voices = juce::jlimit (1, 16, juce::roundToInt (val (voices))); p.glide = val (glide) / 1000.0f; p.volumeDb = val (volume);
    for (int s = 0; s < cw::dsp::kSlots; ++s)
        p.mod[(size_t) s] = { juce::jlimit (0, 7, (int) val (m1Src + 3 * s)), juce::jlimit (0, 14, (int) val (m1Src + 3 * s + 1)), val (m1Src + 3 * s + 2) };
    return p;
}

void CyberWavetableProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr > 0 ? sr : 48000.0);
    engine.setParams (currentParams());
    scratchR.assign ((size_t) juce::jmax (block, 4096), 0.0f);
    setLatencySamples (0);
}

void CyberWavetableProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn()) engine.setSustain (true);
    else if (m.isSustainPedalOff()) engine.setSustain (false);
    else if (m.isAllNotesOff()) engine.allNotesOff();
    else if (m.isAllSoundOff()) engine.allSoundOff();
    else if (m.isPitchWheel()) engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController() && m.getControllerNumber() == 1) engine.setModWheel ((float) m.getControllerValue() / 127.0f);
    else if (m.isChannelPressure()) engine.setPressure ((float) m.getChannelPressureValue() / 127.0f);
    else if (m.isAftertouch()) engine.setPressure ((float) m.getAfterTouchValue() / 127.0f);
}

void CyberWavetableProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    engine.setParams (currentParams());
    for (const int w = uiMidi.w.load(); uiMidi.r != w; ++uiMidi.r)
    {
        const int e = uiMidi.q[(size_t) (uiMidi.r & 63)].load();
        if ((e >> 8) > 0) engine.noteOn (e & 127, (float) (e >> 8) / 127.0f); else engine.noteOff (e & 127);
    }
    if (nch <= 0 || n <= 0) { midi.clear(); return; }
    if ((int) scratchR.size() < n) { buffer.clear(); midi.clear(); return; }   // host broke its promised block size: stay silent
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : scratchR.data();
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);
    if (nch == 1) for (int i = 0; i < n; ++i) L[i] = (L[i] + R[i]) * 0.70710678f;
    for (int c = 2; c < nch; ++c) buffer.clear (c, 0, n);
    midi.clear();
}

void CyberWavetableProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void CyberWavetableProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CyberWavetableProcessor::createEditor() { return new CyberWavetableEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CyberWavetableProcessor(); }
