#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
#define DP_SOUNDS "Kick 808|Kick Punch|Snare|Clap|Closed Hat|Open Hat|Low Tom|Mid Tom|Rim|Cowbell|Crash|Shaker|Zap|Sub Boom|Snap|Glitch|Sample"
const cw::PDef kParams[] = {
    { "p1Sound", "Pad 1 Sound", 'c', 0, 16, 1, 1, 1.0f, "", DP_SOUNDS }, { "p1Tune", "Pad 1 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p1Decay", "Pad 1 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p1Pan", "Pad 1 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p1Level", "Pad 1 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p1Choke", "Pad 1 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "p2Sound", "Pad 2 Sound", 'c', 0, 16, 1, 1, 2.0f, "", DP_SOUNDS }, { "p2Tune", "Pad 2 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p2Decay", "Pad 2 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p2Pan", "Pad 2 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p2Level", "Pad 2 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p2Choke", "Pad 2 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "p3Sound", "Pad 3 Sound", 'c', 0, 16, 1, 1, 3.0f, "", DP_SOUNDS }, { "p3Tune", "Pad 3 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p3Decay", "Pad 3 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p3Pan", "Pad 3 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p3Level", "Pad 3 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p3Choke", "Pad 3 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "p4Sound", "Pad 4 Sound", 'c', 0, 16, 1, 1, 4.0f, "", DP_SOUNDS }, { "p4Tune", "Pad 4 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p4Decay", "Pad 4 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p4Pan", "Pad 4 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p4Level", "Pad 4 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p4Choke", "Pad 4 Choke", 'c', 0, 4, 1, 1, 1.0f, "", "Off|1|2|3|4" },
    { "p5Sound", "Pad 5 Sound", 'c', 0, 16, 1, 1, 5.0f, "", DP_SOUNDS }, { "p5Tune", "Pad 5 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p5Decay", "Pad 5 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p5Pan", "Pad 5 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p5Level", "Pad 5 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p5Choke", "Pad 5 Choke", 'c', 0, 4, 1, 1, 1.0f, "", "Off|1|2|3|4" },
    { "p6Sound", "Pad 6 Sound", 'c', 0, 16, 1, 1, 6.0f, "", DP_SOUNDS }, { "p6Tune", "Pad 6 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p6Decay", "Pad 6 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p6Pan", "Pad 6 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p6Level", "Pad 6 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p6Choke", "Pad 6 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "p7Sound", "Pad 7 Sound", 'c', 0, 16, 1, 1, 8.0f, "", DP_SOUNDS }, { "p7Tune", "Pad 7 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p7Decay", "Pad 7 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p7Pan", "Pad 7 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p7Level", "Pad 7 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p7Choke", "Pad 7 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "p8Sound", "Pad 8 Sound", 'c', 0, 16, 1, 1, 10.0f, "", DP_SOUNDS }, { "p8Tune", "Pad 8 Tune", 'f', -24.0f, 24.0f, 0.1f, 1.0f, 0.0f, "st" }, { "p8Decay", "Pad 8 Decay", 'f', 10.0f, 4000.0f, 1.0f, 0.3f, 4000.0f, "ms-full" },
    { "p8Pan", "Pad 8 Pan", 'f', -1.0f, 1.0f, 0.001f, 1.0f, 0.0f, "pan" }, { "p8Level", "Pad 8 Level", 'f', -36.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" }, { "p8Choke", "Pad 8 Choke", 'c', 0, 4, 1, 1, 0.0f, "", "Off|1|2|3|4" },
    { "volume",   "Volume",          'f', -36.0f, 6.0f, 0.1f, 1.0f, -3.0f, "dB" },
    { "velSens",  "Velocity",        'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.7f, "%" },
    { "kitTune",  "Kit Tune",        'f', -12.0f, 12.0f, 0.1f, 1.0f, 0.0f, "st" },
    { "baseNote", "Pad 1 Note",      'c', 0, 3, 1, 1, 1.0f, "", "C0 (24)|C1 (36)|C2 (48)|C3 (60)" },
};
constexpr int kPerPad = 6, kVolume = 8 * kPerPad, kVelSens = kVolume + 1, kKitTune = kVolume + 2, kBaseNote = kVolume + 3, kNumParams = kVolume + 4;
static_assert (sizeof (kParams) / sizeof (kParams[0]) == kNumParams, "parameter table and index enum disagree");
constexpr int kMaxSeconds = 20;   // longer files are cut (keeps the session / preset small)
} // namespace

DrumPlayerProcessor::DrumPlayerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", cw::makeLayout (kParams)),
      presets (apvts, &undoManager, "Drum Player")
{
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
    formats.registerBasicFormats();
    presets.saveExtra = [this] (juce::XmlElement& x) { writeKit (x); };
    presets.loadExtra = [this] (const juce::XmlElement* x) { readKit (x); };
}

DrumPlayerProcessor::~DrumPlayerProcessor() = default;

bool DrumPlayerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet().isDisabled();
}

cw::dsp::Params DrumPlayerProcessor::currentParams() const
{
    cw::dsp::Params p;
    for (int i = 0; i < cw::dsp::kPads; ++i)
    {
        auto& q = p.pad[(size_t) i]; const int b = i * kPerPad;
        q.sound = juce::jlimit (0, cw::dsp::kSampleSound, (int) val (b)); q.tune = val (b + 1); q.decay = val (b + 2); q.pan = val (b + 3);
        q.levelDb = val (b + 4); q.choke = juce::jlimit (0, cw::dsp::kChokeGroups, (int) val (b + 5));
    }
    p.volumeDb = val (kVolume); p.velSens = val (kVelSens); p.kitTune = val (kKitTune); p.baseNote = 24 + 12 * juce::jlimit (0, 3, (int) val (kBaseNote));
    return p;
}

void DrumPlayerProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr > 0 ? sr : 48000.0);
    engine.setParams (currentParams());
    scratchR.assign ((size_t) juce::jmax (block, 4096), 0.0f);
    setLatencySamples (0);
}

double DrumPlayerProcessor::getTailLengthSeconds() const { return 2.0; }

void DrumPlayerProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isAllNotesOff()) engine.allNotesOff();
    else if (m.isAllSoundOff()) engine.allSoundOff();
}

void DrumPlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    engine.setParams (currentParams());
    {   // pick up sample changes from the message thread (never blocks: if it is busy swapping, next block)
        const juce::SpinLock::ScopedTryLockType tl (userLock);
        if (tl.isLocked()) { for (int i = 0; i < cw::dsp::kPads; ++i) engine.setUser (i, user[(size_t) i].get()); ackGen.store (gen); }
    }
    for (const int w = uiPads.w.load(); uiPads.r != w; ++uiPads.r)
    {
        const int e = uiPads.q[(size_t) (uiPads.r & 63)].load();
        engine.trigger (e & 15, (float) (e >> 8) / 127.0f);
    }
    if (nch <= 0 || n <= 0) { midi.clear(); return; }
    if ((int) scratchR.size() < n) { buffer.clear(); midi.clear(); return; }
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

// ---------------------------------------------------------------- user samples (message thread)
void DrumPlayerProcessor::setUser (int pad, std::shared_ptr<const cw::dsp::Smp> s, const juce::String& path)
{
    if (pad < 0 || pad >= cw::dsp::kPads) return;
    {
        const juce::SpinLock::ScopedLockType l (userLock);
        if (user[(size_t) pad] != nullptr) graveyard.push_back ({ user[(size_t) pad], gen + 1 });
        user[(size_t) pad] = std::move (s); ++gen;
    }
    userPath[(size_t) pad] = path;
    // free replaced samples the audio thread has provably stopped using (it acknowledged a later generation)
    const int ack = ackGen.load();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [ack] (const Dead& d) { return d.gen <= ack; }), graveyard.end());
    ++sampleChanges;
}

bool DrumPlayerProcessor::loadSample (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= cw::dsp::kPads) return false;
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    if (r == nullptr || r->lengthInSamples <= 1 || r->sampleRate < 8000) return false;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * kMaxSeconds));
    const int ch = juce::jlimit (1, 2, (int) r->numChannels);
    juce::AudioBuffer<float> b (ch, len);
    if (! r->read (&b, 0, len, 0, true, ch > 1)) return false;
    auto s = std::make_shared<cw::dsp::Smp>();
    s->rate = r->sampleRate; s->name = file.getFileNameWithoutExtension().toStdString();
    s->L.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    if (ch > 1) s->R.assign (b.getReadPointer (1), b.getReadPointer (1) + len);
    for (auto* v : { &s->L, &s->R }) for (auto& x : *v) if (! std::isfinite (x)) x = 0;
    setUser (pad, std::move (s), file.getFullPathName());
    if (auto* p = apvts.getParameter (kParams[pad * kPerPad].id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) cw::dsp::kSampleSound)); p->endChangeGesture(); }
    return true;
}

void DrumPlayerProcessor::clearSample (int pad) { setUser (pad, nullptr, {}); }

juce::String DrumPlayerProcessor::padSoundName (int pad) const
{
    const int s = juce::jlimit (0, cw::dsp::kSampleSound, (int) val (pad * kPerPad));
    if (s < cw::dsp::kSounds) return cw::dsp::soundName (s);
    const auto& u = user[(size_t) pad];
    return u != nullptr ? juce::String (u->name) : juce::String ("(drop a sample)");
}

const cw::dsp::Smp* DrumPlayerProcessor::padSample (int pad) const
{
    const int s = juce::jlimit (0, cw::dsp::kSampleSound, (int) val (pad * kPerPad));
    return s < cw::dsp::kSounds ? &cw::dsp::builtIn (s) : user[(size_t) pad].get();
}

// kit <-> XML: every loaded user sample is embedded (16-bit PCM, gzip, base64) so sessions and presets travel without the files
void DrumPlayerProcessor::writeKit (juce::XmlElement& x) const
{
    for (int i = 0; i < cw::dsp::kPads; ++i)
    {
        const auto& u = user[(size_t) i];
        if (u == nullptr) continue;
        auto* e = x.createNewChildElement ("PAD");
        const int ch = u->R.empty() ? 1 : 2, len = u->len();
        e->setAttribute ("index", i); e->setAttribute ("name", juce::String (u->name)); e->setAttribute ("path", userPath[(size_t) i]);
        e->setAttribute ("rate", u->rate); e->setAttribute ("channels", ch); e->setAttribute ("length", len);
        juce::MemoryOutputStream mo;
        {
            juce::GZIPCompressorOutputStream gz (mo, 6);
            for (int k = 0; k < len; ++k)
                for (int c = 0; c < ch; ++c)
                    gz.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lrint ((c == 0 ? u->L : u->R)[(size_t) k] * 32767.0f)));
        }
        e->setAttribute ("data", mo.getMemoryBlock().toBase64Encoding());
    }
}

void DrumPlayerProcessor::readKit (const juce::XmlElement* x)
{
    for (int i = 0; i < cw::dsp::kPads; ++i) clearSample (i);
    if (x == nullptr) return;
    for (auto* e : x->getChildWithTagNameIterator ("PAD"))
    {
        const int i = e->getIntAttribute ("index", -1), ch = e->getIntAttribute ("channels", 1), len = e->getIntAttribute ("length", 0);
        const double rate = e->getDoubleAttribute ("rate", 48000.0);
        if (i < 0 || i >= cw::dsp::kPads || ch < 1 || ch > 2 || len < 2 || rate < 8000 || rate > 768000 || len > (int) (rate * kMaxSeconds) + 1) continue;
        juce::MemoryBlock zipped;
        bool ok = zipped.fromBase64Encoding (e->getStringAttribute ("data"));
        if (ok)
        {
            juce::MemoryInputStream mi (zipped, false);
            juce::GZIPDecompressorInputStream gz (mi);
            juce::MemoryBlock pcm; gz.readIntoMemoryBlock (pcm, (juce::int64) len * ch * 2 + 2);
            ok = (int) pcm.getSize() >= len * ch * 2;
            if (ok)
            {
                auto s = std::make_shared<cw::dsp::Smp>(); s->rate = rate; s->name = e->getStringAttribute ("name", "Sample").toStdString();
                s->L.resize ((size_t) len); if (ch > 1) s->R.resize ((size_t) len);
                const auto* d = static_cast<const char*> (pcm.getData());
                for (int k = 0; k < len; ++k)
                    for (int c = 0; c < ch; ++c)
                    {
                        const auto o = (size_t) (k * ch + c) * 2;
                        const short v = (short) ((unsigned char) d[o] | ((unsigned char) d[o + 1] << 8));   // writeShort is little-endian
                        (c == 0 ? s->L : s->R)[(size_t) k] = (float) v / 32767.0f;
                    }
                setUser (i, std::move (s), e->getStringAttribute ("path"));
            }
        }
        if (! ok)   // no / broken embedded audio: try the original file
        {
            const juce::File f (e->getStringAttribute ("path"));
            if (f.existsAsFile()) { const auto keep = val (i * kPerPad); loadSample (i, f); if (auto* p = apvts.getParameter (kParams[i * kPerPad].id)) p->setValueNotifyingHost (p->convertTo0to1 (keep)); }
        }
    }
}

void DrumPlayerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    writeKit (*xml->createNewChildElement ("KIT"));
    copyXmlToBinary (*xml, dest);
}

void DrumPlayerProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    if (auto* kit = xml->getChildByName ("KIT")) { readKit (kit); xml->removeChildElement (kit, true); }
    else readKit (nullptr);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DrumPlayerProcessor::createEditor() { return new DrumPlayerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumPlayerProcessor(); }
