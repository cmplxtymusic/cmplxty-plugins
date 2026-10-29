#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
#define VV_WAVES "Saw|Square|PWM|Triangle"
const vv::PDef kParams[] = {
    { "w1",       "Osc 1 Wave",       'c', 0, 3, 1, 1, 0.0f, "", VV_WAVES },
    { "oct1",     "Osc 1 Octave",     'f', -2.0f, 2.0f, 1.0f, 1.0f, 0.0f, "int" },
    { "lvl1",     "Osc 1 Level",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.8f, "%" },
    { "w2",       "Osc 2 Wave",       'c', 0, 3, 1, 1, 1.0f, "", VV_WAVES },
    { "semi2",    "Osc 2 Semitone",   'f', -24.0f, 24.0f, 1.0f, 1.0f, 0.0f, "st" },
    { "fine2",    "Osc 2 Fine",       'f', -50.0f, 50.0f, 0.1f, 1.0f, 7.0f, "ct" },
    { "lvl2",     "Osc 2 Level",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "pw",       "Pulse Width",      'f', 0.05f, 0.95f, 0.001f, 1.0f, 0.5f, "%" },
    { "pwm",      "PWM Depth",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "pwmRate",  "PWM Rate",         'f', 0.05f, 10.0f, 0.01f, 0.4f, 0.6f, "Hz" },
    { "uni",      "Unison",           'f', 1.0f, 7.0f, 1.0f, 1.0f, 3.0f, "int" },
    { "detune",   "Detune",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.25f, "%" },
    { "width",    "Width",            'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "noise",    "Noise",            'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "noiseTone","Noise Tone",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "vox",      "Vox",              'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "vowel",    "Vowel",            'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "voxMove",  "Vowel Motion",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "fType",    "Filter Type",      'c', 0, 3, 1, 1, 0.0f, "", "LP 24|LP 12|Band Pass|High Pass" },
    { "cutoff",   "Cutoff",           'f', 20.0f, 20000.0f, 0.1f, 0.25f, 3000.0f, "Hz" },
    { "reso",     "Resonance",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "drive",    "Drive",            'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "fEnv",     "Filter Env",       'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "fKey",     "Key Track",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.3f, "%" },
    { "fA",       "Filter Attack",    'f', 1.0f, 10000.0f, 0.1f, 0.25f, 2.0f, "ms" },
    { "fD",       "Filter Decay",     'f', 1.0f, 10000.0f, 0.1f, 0.25f, 500.0f, "ms" },
    { "fS",       "Filter Sustain",   'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "ampA",     "Amp Attack",       'f', 1.0f, 10000.0f, 0.1f, 0.25f, 5.0f, "ms" },
    { "ampD",     "Amp Decay",        'f', 1.0f, 10000.0f, 0.1f, 0.25f, 400.0f, "ms" },
    { "ampS",     "Amp Sustain",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.8f, "%" },
    { "ampR",     "Amp Release",      'f', 1.0f, 10000.0f, 0.1f, 0.25f, 400.0f, "ms" },
    { "drop",     "Pitch Drop",       'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" },
    { "dropTime", "Drop Time",        'f', 10.0f, 4000.0f, 0.1f, 0.35f, 400.0f, "ms" },
    { "drag",     "Drag",             'b', 0, 1, 1, 1, 0.0f, "" },
    { "dragAmt",  "Drag Amount",      'f', 0.0f, 24.0f, 0.1f, 1.0f, 5.0f, "st" },
    { "wow",      "Wow",              'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "flutter",  "Flutter",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "age",      "Tape Age",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "bits",     "Bits",             'f', 2.0f, 16.0f, 1.0f, 1.0f, 16.0f, "int" },
    { "crushHz",  "Crush Rate",       'f', 200.0f, 48000.0f, 1.0f, 0.3f, 48000.0f, "Hz" },
    { "chipMix",  "Chip Mix",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "lfoShape", "LFO Shape",        'c', 0, 4, 1, 1, 0.0f, "", "Sine|Triangle|Saw|Square|S&H" },
    { "lfoRate",  "LFO Rate",         'f', 0.02f, 20.0f, 0.01f, 0.35f, 4.0f, "Hz" },
    { "lfoCut",   "LFO to Cutoff",    'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "lfoPitch", "LFO to Pitch",     'f', 0.0f, 2.0f, 0.01f, 1.0f, 0.0f, "st" },
    { "arp",      "Arp",              'b', 0, 1, 1, 1, 0.0f, "" },
    { "arpMode",  "Arp Mode",         'c', 0, 4, 1, 1, 0.0f, "", "Up|Down|Up-Down|Random|Chop" },
    { "arpDiv",   "Arp Rate",         'c', 0, 5, 1, 1, 3.0f, "", "1/4|1/8|1/8T|1/16|1/16T|1/32" },
    { "arpOct",   "Arp Octaves",      'f', 1.0f, 4.0f, 1.0f, 1.0f, 1.0f, "int" },
    { "arpGate",  "Arp Gate",         'f', 0.05f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "voices",   "Voices",           'f', 1.0f, 12.0f, 1.0f, 1.0f, 8.0f, "int" },
    { "glide",    "Glide",            'f', 0.0f, 2000.0f, 0.1f, 0.35f, 0.0f, "ms" },
    { "dlyTime",  "Delay Time",       'f', 10.0f, 2000.0f, 0.1f, 0.4f, 375.0f, "ms" },
    { "dlyFb",    "Delay Feedback",   'f', 0.0f, 0.95f, 0.001f, 1.0f, 0.35f, "%" },
    { "dlyMix",   "Delay Mix",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "revSize",  "Reverb Size",      'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "revDark",  "Reverb Darkness",  'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.6f, "%" },
    { "revMix",   "Reverb Mix",       'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.2f, "%" },
    { "volume",   "Volume",           'f', -36.0f, 6.0f, 0.1f, 1.0f, -6.0f, "dB" },
};
enum I { w1, oct1, lvl1, w2, semi2, fine2, lvl2, pw, pwm, pwmRate, uni, detune, width, noise, noiseTone, vox, vowel, voxMove,
         fType, cutoff, reso, drive, fEnv, fKey, fA, fD, fS, ampA, ampD, ampS, ampR, drop, dropTime, drag, dragAmt, wow, flutter, age,
         bits, crushHz, chipMix, lfoShape, lfoRate, lfoCut, lfoPitch, arp, arpMode, arpDiv, arpOct, arpGate, voices, glide,
         dlyTime, dlyFb, dlyMix, revSize, revDark, revMix, volume, kNumParams };
static_assert (sizeof (kParams) / sizeof (kParams[0]) == kNumParams, "parameter table and index enum disagree");
} // namespace

VvitchProcessor::VvitchProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", vv::makeLayout (kParams)),
      presets (apvts, &undoManager, "VVITCH")
{
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
}

bool VvitchProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet().isDisabled();
}

double VvitchProcessor::getTailLengthSeconds() const { return val (ampR) / 1000.0 * 4.0 + (val (revMix) > 0 ? 1.0 + 9.0 * val (revSize) * val (revSize) : 0.0) + (val (dlyMix) > 0 ? 3.0 : 0.0); }

vv::dsp::Params VvitchProcessor::currentParams() const
{
    using namespace vv::dsp;
    Params p;
    auto ci = [this] (int i, int hi) { return juce::jlimit (0, hi, (int) val (i)); };
    p.w1 = ci (w1, 3); p.oct1 = (float) juce::roundToInt (val (oct1)); p.lvl1 = val (lvl1);
    p.w2 = ci (w2, 3); p.semi2 = val (semi2); p.fine2 = val (fine2); p.lvl2 = val (lvl2);
    p.pw = val (pw); p.pwm = val (pwm); p.pwmRate = val (pwmRate);
    p.uni = juce::jlimit (1, kMaxUni, juce::roundToInt (val (uni))); p.detune = val (detune); p.width = val (width);
    p.noise = val (noise); p.noiseTone = val (noiseTone); p.vox = val (vox); p.vowel = val (vowel); p.voxMove = val (voxMove);
    p.fType = (FilterType) ci (fType, 3); p.cutoff = val (cutoff); p.reso = val (reso); p.drive = val (drive); p.fEnv = val (fEnv); p.fKey = val (fKey);
    p.fA = val (fA) / 1000.0f; p.fD = val (fD) / 1000.0f; p.fS = val (fS);
    p.aA = val (ampA) / 1000.0f; p.aD = val (ampD) / 1000.0f; p.aS = val (ampS); p.aR = val (ampR) / 1000.0f;
    p.drop = val (drop); p.dropTime = val (dropTime) / 1000.0f; p.drag = val (drag) > 0.5f; p.dragAmt = val (dragAmt);
    p.wow = val (wow); p.flutter = val (flutter); p.age = val (age);
    p.bits = val (bits); p.crushHz = val (crushHz); p.chipMix = val (chipMix);
    p.lfoShape = ci (lfoShape, 4); p.lfoRate = val (lfoRate); p.lfoCut = val (lfoCut); p.lfoPitch = val (lfoPitch);
    p.arp = val (arp) > 0.5f; p.arpMode = ci (arpMode, 4); p.arpDiv = ci (arpDiv, 5); p.arpOct = juce::jlimit (1, 4, juce::roundToInt (val (arpOct))); p.arpGate = val (arpGate);
    p.voices = juce::jlimit (1, kMaxVoices, juce::roundToInt (val (voices))); p.glide = val (glide) / 1000.0f;
    p.dlyTime = val (dlyTime) / 1000.0f; p.dlyFb = val (dlyFb); p.dlyMix = val (dlyMix);
    p.revSize = val (revSize); p.revDark = val (revDark); p.revMix = val (revMix); p.volumeDb = val (volume);
    p.bpm = (float) hostBpm.load (std::memory_order_relaxed);
    return p;
}

void VvitchProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr > 0 ? sr : 48000.0);
    engine.setParams (currentParams());
    scratchR.assign ((size_t) juce::jmax (block, 4096), 0.0f);
    setLatencySamples (0);
}

void VvitchProcessor::handleMidi (const juce::MidiMessage& m)
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

void VvitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) if (*bpm > 1.0) hostBpm.store (*bpm, std::memory_order_relaxed);
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

void VvitchProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    copyXmlToBinary (*xml, dest);
}

void VvitchProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VvitchProcessor::createEditor() { return new VvitchEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VvitchProcessor(); }
