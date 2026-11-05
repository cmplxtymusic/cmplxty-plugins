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
    // 1.1.0: FX RACK (4 insert slots) and 8 macros. Appended so existing parameter indices / host automation stay put.
    { "fx1Type",   "FX 1 Type",        'c', 0, 7, 1, 1, 0.0f, "", "Off|Chorus|Phaser|Flanger|Distortion|Compressor|Tilt EQ|Widener" },
    { "fx1A",      "FX 1 A",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx1B",      "FX 1 B",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx1Mix",    "FX 1 Mix",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx2Type",   "FX 2 Type",        'c', 0, 7, 1, 1, 0.0f, "", "Off|Chorus|Phaser|Flanger|Distortion|Compressor|Tilt EQ|Widener" },
    { "fx2A",      "FX 2 A",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx2B",      "FX 2 B",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx2Mix",    "FX 2 Mix",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx3Type",   "FX 3 Type",        'c', 0, 7, 1, 1, 0.0f, "", "Off|Chorus|Phaser|Flanger|Distortion|Compressor|Tilt EQ|Widener" },
    { "fx3A",      "FX 3 A",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx3B",      "FX 3 B",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx3Mix",    "FX 3 Mix",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx4Type",   "FX 4 Type",        'c', 0, 7, 1, 1, 0.0f, "", "Off|Chorus|Phaser|Flanger|Distortion|Compressor|Tilt EQ|Widener" },
    { "fx4A",      "FX 4 A",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx4B",      "FX 4 B",           'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "fx4Mix",    "FX 4 Mix",         'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.5f, "%" },
    { "macro1",   "Macro 1",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro2",   "Macro 2",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro3",   "Macro 3",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro4",   "Macro 4",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro5",   "Macro 5",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro6",   "Macro 6",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro7",   "Macro 7",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
    { "macro8",   "Macro 8",          'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.0f, "%" },
};
enum I { w1, oct1, lvl1, w2, semi2, fine2, lvl2, pw, pwm, pwmRate, uni, detune, width, noise, noiseTone, vox, vowel, voxMove,
         fType, cutoff, reso, drive, fEnv, fKey, fA, fD, fS, ampA, ampD, ampS, ampR, drop, dropTime, drag, dragAmt, wow, flutter, age,
         bits, crushHz, chipMix, lfoShape, lfoRate, lfoCut, lfoPitch, arp, arpMode, arpDiv, arpOct, arpGate, voices, glide,
         dlyTime, dlyFb, dlyMix, revSize, revDark, revMix, volume,
         fx1Type, fx1A, fx1B, fx1Mix, fx2Type, fx2A, fx2B, fx2Mix, fx3Type, fx3A, fx3B, fx3Mix, fx4Type, fx4A, fx4B, fx4Mix,
         macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8, kNumParams };
static_assert (sizeof (kParams) / sizeof (kParams[0]) == kNumParams, "parameter table and index enum disagree");
// Macros: each one pushes a fixed set of targets by a normalised (0..1 of the knob's travel) amount, ON TOP of whatever the
// preset stores. Nothing stored is changed, so every factory and user preset (old or new) responds to all eight macros.
struct MacroTarget { int param; float depth; };
const std::vector<MacroTarget>& macroTargets (int m)
{
    static const std::vector<MacroTarget> t[VvitchProcessor::kMacros] = {
        { { cutoff, 0.35f }, { reso, 0.2f } },                                  // Bright
        { { revMix, 0.5f }, { revSize, 0.3f }, { dlyMix, 0.3f } },              // Space
        { { wow, 0.5f }, { flutter, 0.4f }, { age, 0.6f } },                    // Tape
        { { chipMix, 0.8f }, { bits, -0.6f }, { crushHz, -0.5f } },             // Crush
        { { drop, -0.25f } },                                                   // Drop (down to -12 st)
        { { lfoCut, 0.4f }, { voxMove, 0.5f }, { vox, 0.3f } },                 // Motion
        { { detune, 0.3f }, { width, 0.5f } },                                  // Width
        { { fx1Mix, 0.6f }, { fx2Mix, 0.6f }, { fx3Mix, 0.6f }, { fx4Mix, 0.6f } } // FX
    };
    return t[juce::jlimit (0, VvitchProcessor::kMacros - 1, m)];
}
} // namespace

const char* VvitchProcessor::defaultMacroName (int m)
{
    static const char* n[kMacros] = { "BRIGHT", "SPACE", "TAPE", "CRUSH", "DROP", "MOTION", "WIDTH", "FX" };
    return n[juce::jlimit (0, kMacros - 1, m)];
}
juce::String VvitchProcessor::macroName (int m) const
{
    const auto parts = juce::StringArray::fromTokens (apvts.state.getProperty ("macroNames").toString(), "|", "");
    const auto s = m >= 0 && m < parts.size() ? parts[m].trim() : juce::String();
    return s.isNotEmpty() ? s : juce::String (defaultMacroName (m));
}
void VvitchProcessor::setMacroName (int m, const juce::String& name)
{
    if (m < 0 || m >= kMacros) return;
    juce::StringArray parts; for (int i = 0; i < kMacros; ++i) parts.add (macroName (i));
    parts.set (m, name.removeCharacters ("|").trim().substring (0, 16));
    apvts.state.setProperty ("macroNames", parts.joinIntoString ("|"), nullptr);
}

VvitchProcessor::VvitchProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", vv::makeLayout (kParams)),
      presets (apvts, &undoManager, "VVITCH")
{
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
    for (auto& d : kParams) ranges.push_back (apvts.getParameterRange (d.id));
    // macro names travel with user presets; a factory preset (nullptr) restores the default names
    presets.saveExtra = [this] (juce::XmlElement& x) { for (int m = 0; m < kMacros; ++m) x.setAttribute ("macro" + juce::String (m + 1), macroName (m)); };
    presets.loadExtra = [this] (const juce::XmlElement* x)
    {
        juce::StringArray parts;
        for (int m = 0; m < kMacros; ++m) parts.add (x != nullptr ? x->getStringAttribute ("macro" + juce::String (m + 1), defaultMacroName (m)) : juce::String (defaultMacroName (m)));
        apvts.state.setProperty ("macroNames", parts.joinIntoString ("|"), nullptr);
    };
}

float VvitchProcessor::mval (int i) const
{
    float off = 0;
    for (int m = 0; m < kMacros; ++m)
    {
        const float a = val (macro1 + m);
        if (a <= 0.0f) continue;
        for (auto& t : macroTargets (m)) if (t.param == i) off += a * t.depth;
    }
    if (off == 0.0f) return val (i);
    const auto& r = ranges[(size_t) i];
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, r.convertTo0to1 (r.snapToLegalValue (val (i))) + off));
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
    p.w1 = ci (w1, 3); p.oct1 = (float) juce::roundToInt (mval (oct1)); p.lvl1 = mval (lvl1);
    p.w2 = ci (w2, 3); p.semi2 = mval (semi2); p.fine2 = mval (fine2); p.lvl2 = mval (lvl2);
    p.pw = mval (pw); p.pwm = mval (pwm); p.pwmRate = mval (pwmRate);
    p.uni = juce::jlimit (1, kMaxUni, juce::roundToInt (mval (uni))); p.detune = mval (detune); p.width = mval (width);
    p.noise = mval (noise); p.noiseTone = mval (noiseTone); p.vox = mval (vox); p.vowel = mval (vowel); p.voxMove = mval (voxMove);
    p.fType = (FilterType) ci (fType, 3); p.cutoff = mval (cutoff); p.reso = mval (reso); p.drive = mval (drive); p.fEnv = mval (fEnv); p.fKey = mval (fKey);
    p.fA = mval (fA) / 1000.0f; p.fD = mval (fD) / 1000.0f; p.fS = mval (fS);
    p.aA = mval (ampA) / 1000.0f; p.aD = mval (ampD) / 1000.0f; p.aS = mval (ampS); p.aR = mval (ampR) / 1000.0f;
    p.drop = mval (drop); p.dropTime = mval (dropTime) / 1000.0f; p.drag = mval (drag) > 0.5f; p.dragAmt = mval (dragAmt);
    p.wow = mval (wow); p.flutter = mval (flutter); p.age = mval (age);
    p.bits = mval (bits); p.crushHz = mval (crushHz); p.chipMix = mval (chipMix);
    p.lfoShape = ci (lfoShape, 4); p.lfoRate = mval (lfoRate); p.lfoCut = mval (lfoCut); p.lfoPitch = mval (lfoPitch);
    p.arp = mval (arp) > 0.5f; p.arpMode = ci (arpMode, 4); p.arpDiv = ci (arpDiv, 5); p.arpOct = juce::jlimit (1, 4, juce::roundToInt (mval (arpOct))); p.arpGate = mval (arpGate);
    p.voices = juce::jlimit (1, kMaxVoices, juce::roundToInt (mval (voices))); p.glide = mval (glide) / 1000.0f;
    p.dlyTime = mval (dlyTime) / 1000.0f; p.dlyFb = mval (dlyFb); p.dlyMix = mval (dlyMix);
    p.revSize = mval (revSize); p.revDark = mval (revDark); p.revMix = mval (revMix); p.volumeDb = mval (volume);
    p.bpm = (float) hostBpm.load (std::memory_order_relaxed);
    for (int k = 0; k < vv::fx::kSlots; ++k)
    {
        auto& f = p.fx[(size_t) k]; const int b0 = fx1Type + 4 * k;
        f.type = ci (b0, vv::fx::kNumTypes - 1); f.a = mval (b0 + 1); f.b = mval (b0 + 2); f.mix = mval (b0 + 3);
    }
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
