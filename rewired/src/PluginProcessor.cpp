#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"
#include "RewiredIO.h"

namespace
{
// detection settings; every one is in factory and user presets
const rw::PDef kParams[] = {
    { "onset",    "Note Onset",      'f', 0.1f, 0.9f, 0.01f, 1.0f, 0.5f, "%" },
    { "frame",    "Note Hold",       'f', 0.1f, 0.7f, 0.01f, 1.0f, 0.3f, "%" },
    { "minNote",  "Shortest Note",   'f', 30.0f, 400.0f, 1.0f, 0.6f, 120.0f, "ms" },
    { "melSrc",   "Melody From",     'c', 0, 2, 1, 1, 0.0f, "", "Auto|Vocals|Instruments" },
    { "melGap",   "Melody Gap",      'f', 3.0f, 12.0f, 1.0f, 1.0f, 7.0f, "st" },
    { "chordMin", "Chord Notes",     'f', 2.0f, 4.0f, 1.0f, 1.0f, 2.0f, "" },
    { "drumSens", "Drum Sensitivity",'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%" },
    { "quantize", "Quantize",        'c', 0, 3, 1, 1, 0.0f, "", "Off|1/16|1/8|1/4" },
    { "bpm",      "Tempo",           'f', 0.0f, 250.0f, 0.1f, 1.0f, 0.0f, "" },
};
juce::String safeName (juce::String s)
{
    s = juce::File::createLegalFileName (s).trim();
    return s.isEmpty() ? juce::String ("Untitled") : s;
}
} // namespace

struct TranscribeJob final : juce::Thread
{
    explicit TranscribeJob (RewiredProcessor& p) : juce::Thread ("CMPLXTY Rewired transcribe"), proc (p) {}
    void run() override { proc.run(); }
    RewiredProcessor& proc;
};

const char* RewiredProcessor::slotName (int s)
{
    static const char* n[] = { "Song", "Bass stem", "Melody stem", "Chords stem", "Drums stem" };
    return (s >= 0 && s < kNumSlots) ? n[s] : "?";
}

RewiredProcessor::RewiredProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", rw::makeLayout (kParams))
{
    for (auto& p : partEnabled) p.store (true);
    setMessage ("Drop a song (or your stems) here, then press PROCESS");
}

RewiredProcessor::~RewiredProcessor()
{
    cancel.store (true);
    if (worker) worker->stopThread (20000);
}

bool RewiredProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

// audio passes through untouched: Rewired is an offline tool that can live on any track
void RewiredProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) b.clear (c, 0, b.getNumSamples());
}

void RewiredProcessor::setMessage (const juce::String& m) { const juce::SpinLock::ScopedLockType l (msgLock); message = m; }

juce::String RewiredProcessor::setInput (int slot, const juce::File& f)
{
    if (slot < 0 || slot >= kNumSlots) return "bad slot";
    if (status() == Status::working) return "Wait for the current run to finish (or cancel it)";
    if (! f.existsAsFile()) return "Can't find " + f.getFileName();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return "Can't read " + f.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)";
    if ((double) r->lengthInSamples / r->sampleRate > 900.0) return f.getFileName() + " is longer than 15 minutes";
    {
        const juce::ScopedLock l (inLock);
        if (slot == kSong) for (int i = 1; i < kNumSlots; ++i) inputs[i] = juce::File();
        else inputs[kSong] = juce::File();
        inputs[slot] = f;
    }
    setMessage (juce::String (slotName (slot)) + ": " + f.getFileName() + "  -  press PROCESS");
    ++version;
    return {};
}

void RewiredProcessor::clearInput (int slot) { { const juce::ScopedLock l (inLock); inputs[slot] = juce::File(); } ++version; }

void RewiredProcessor::clearAll()
{
    cancelProcess();
    { const juce::ScopedLock l (inLock); for (auto& f : inputs) f = juce::File(); }
    { const juce::ScopedLock l (resLock); res = {}; }
    st.store ((int) Status::idle);
    setMessage ("Drop a song (or your stems) here, then press PROCESS");
    ++version;
}

juce::File RewiredProcessor::input (int slot) const { const juce::ScopedLock l (inLock); return inputs[slot]; }
bool RewiredProcessor::hasAnyInput() const { const juce::ScopedLock l (inLock); for (auto& f : inputs) if (f != juce::File()) return true; return false; }

juce::String RewiredProcessor::title() const
{
    const juce::ScopedLock l (inLock);
    for (auto& f : inputs) if (f != juce::File()) return f.getFileNameWithoutExtension();
    return "Untitled";
}

rw::Settings RewiredProcessor::currentSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    rw::Settings s;
    s.onsetThreshold = v ("onset"); s.frameThreshold = v ("frame"); s.minNoteMs = v ("minNote");
    s.melodySource = (int) v ("melSrc"); s.melodyGap = juce::roundToInt (v ("melGap")); s.chordMinNotes = v ("chordMin");
    s.drumSensitivity = v ("drumSens"); s.quantize = (int) v ("quantize"); s.bpm = v ("bpm");
    return s;
}

juce::File RewiredProcessor::modelDir() const { return rw::io::findModelDir(); }

void RewiredProcessor::startProcess()
{
    if (status() == Status::working || ! hasAnyInput()) return;
    if (worker) worker->stopThread (20000);
    cancel.store (false); prog.store (0.0f);
    st.store ((int) Status::working); ++version;
    setMessage ("Starting...");
    worker = std::make_unique<TranscribeJob> (*this);
    worker->startThread();
}

void RewiredProcessor::cancelProcess() { cancel.store (true); if (worker) worker->stopThread (20000); }

void RewiredProcessor::run()
{
    auto fail = [this] (const juce::String& e) { setMessage (e); st.store ((int) Status::failed); ++version; };
    if (! engineLoaded)
    {
        const auto dir = modelDir();
        if (dir == juce::File()) { fail ("Models not found - reinstall Rewired (they live in " + rw::io::modelDirCandidates().getLast().getFullPathName() + ")"); return; }
        setMessage ("Loading models...");
        std::string err;
        if (! engine.load (dir.getFullPathName().toStdString(), err)) { fail ("Could not load the models: " + juce::String (err)); return; }
        engineLoaded = true;
    }
    const auto s = currentSettings();
    juce::File in[kNumSlots]; { const juce::ScopedLock l (inLock); for (int i = 0; i < kNumSlots; ++i) in[i] = inputs[i]; }
    auto progress = [this] (float p, const char* what) { prog.store (p); setMessage (juce::String (what) + "..."); };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    rw::Result r; juce::String e;
    if (in[kSong] != juce::File())
    {
        setMessage ("Reading " + in[kSong].getFileName() + "...");
        rw::Audio a; if (! rw::io::loadAudio (in[kSong], a, e)) { fail (e); return; }
        if (cancel.load()) { fail ("Cancelled"); return; }
        r = engine.transcribeSong (a, s, &cancel, progress);
    }
    else
    {
        std::array<rw::Audio, rw::kNumStemSlots> stems;
        for (int i = 0; i < rw::kNumStemSlots; ++i)
            if (in[i + 1] != juce::File() && ! rw::io::loadAudio (in[i + 1], stems[(size_t) i], e)) { fail (e); return; }
        r = engine.transcribeStems (stems, s, &cancel, progress);
    }
    if (! r.ok) { fail (r.error == "cancelled" ? juce::String ("Cancelled") : juce::String (r.error)); return; }
    int total = 0; for (auto& p : r.parts) total += (int) p.size();
    { const juce::ScopedLock l (resLock); res = std::move (r); }
    prog.store (1.0f);
    const auto secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const auto rr = result();
    setMessage (juce::String (total) + " notes in " + juce::String (secs, 1) + " s  -  " + juce::String (rr.bpm, 1) + " BPM"
                + (songMode() ? "  -  melody from " + juce::String (rr.melodyFrom) : juce::String()) + "  -  drag parts into your DAW");
    st.store ((int) Status::ready); ++version;
}

juce::File RewiredProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("CMPLXTY Rewired").getChildFile (safeName (title()));
}

juce::File RewiredProcessor::writeParts (const std::vector<int>& parts, const juce::String& suffix, juce::String& err)
{
    const auto r = result();
    if (! r.ok) { err = "Nothing to export yet - press PROCESS first"; return {}; }
    if (parts.empty()) { err = "No parts selected"; return {}; }
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const auto f = dir.getChildFile (safeName (title()) + suffix + ".mid");
    if (! rw::io::writeMidi (f, r, parts, title(), err)) return {};
    return f;
}

juce::File RewiredProcessor::exportMidi (bool selectedOnly, juce::String& err)
{
    std::vector<int> parts;
    for (int p = 0; p < rw::kNumParts; ++p) if (! selectedOnly || partOn (p)) parts.push_back (p);
    return writeParts (parts, selectedOnly ? " - Selected" : "", err);
}

juce::File RewiredProcessor::exportPart (int part, juce::String& err) { return writeParts ({ part }, juce::String (" - ") + rw::partName (part), err); }

bool RewiredProcessor::testWaitProcess (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (status() == Status::working && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (20);
    return status() == Status::ready;
}

void RewiredProcessor::testSetResult (const rw::Result& r)
{
    { const juce::ScopedLock l (resLock); res = r; res.ok = true; }
    int total = 0; for (auto& p : r.parts) total += (int) p.size();
    setMessage (juce::String (total) + " notes  -  " + juce::String (r.bpm, 1) + " BPM  -  drag parts into your DAW");
    st.store ((int) Status::ready); ++version;
}

// ---- state: "RWR1" + int + XML (parameters, inputs, part switches and the last result, so a reopened project keeps its MIDI)
void RewiredProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("RewiredState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml.setAttribute ("uiW", uiWidth.load()); xml.setAttribute ("uiH", uiHeight.load()); }
    for (int i = 0; i < kNumSlots; ++i) xml.setAttribute ("in" + juce::String (i), input (i).getFullPathName());
    juce::String on; for (int p = 0; p < rw::kNumParts; ++p) on << (partOn (p) ? "1" : "0");
    xml.setAttribute ("parts", on);
    const auto r = result();
    if (r.ok)
    {
        auto* rx = xml.createNewChildElement ("Result");
        rx->setAttribute ("bpm", r.bpm); rx->setAttribute ("seconds", r.seconds); rx->setAttribute ("from", juce::String (r.melodyFrom));
        for (int p = 0; p < rw::kNumParts; ++p)
        {
            juce::String t; t.preallocateBytes (r.parts[(size_t) p].size() * 24);
            for (auto& n : r.parts[(size_t) p]) t << juce::String (n.start, 4) << ',' << juce::String (n.end, 4) << ',' << n.pitch << ',' << n.velocity << ';';
            rx->createNewChildElement (rw::partName (p))->addTextElement (t);
        }
    }
    const auto text = xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    juce::MemoryOutputStream out (dest, false);
    out.write ("RWR1", 4);
    out.writeInt ((int) text.getNumBytesAsUTF8());
    out.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void RewiredProcessor::setStateInformation (const void* data, int size)
{
    if (size < 8 || std::memcmp (data, "RWR1", 4) != 0) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    in.skipNextBytes (4);
    const int xb = in.readInt();
    if (xb <= 0 || xb > size - 8) return;
    juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, xb);
    auto xml = juce::parseXML (xm.toString());
    if (xml == nullptr || ! xml->hasTagName ("RewiredState")) return;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (xml->hasAttribute (r->paramID)) r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)));
    presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser"));
    uiWidth.store (xml->getIntAttribute ("uiW")); uiHeight.store (xml->getIntAttribute ("uiH"));
    {
        const juce::ScopedLock l (inLock);
        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto p = xml->getStringAttribute ("in" + juce::String (i));
            inputs[i] = juce::File::isAbsolutePath (p) ? juce::File (p) : juce::File();
        }
    }
    const auto on = xml->getStringAttribute ("parts", "1111111");
    for (int p = 0; p < rw::kNumParts; ++p) partEnabled[(size_t) p].store (p >= on.length() || on[p] != '0');
    rw::Result r;
    if (auto* rx = xml->getChildByName ("Result"))
    {
        r.ok = true; r.bpm = juce::jlimit (20.0, 400.0, rx->getDoubleAttribute ("bpm", 120.0)); r.seconds = rx->getDoubleAttribute ("seconds");
        r.melodyFrom = rx->getStringAttribute ("from").toStdString();
        for (int p = 0; p < rw::kNumParts; ++p)
            if (auto* px = rx->getChildByName (rw::partName (p)))
                for (auto& tok : juce::StringArray::fromTokens (px->getAllSubText(), ";", ""))
                {
                    auto f = juce::StringArray::fromTokens (tok, ",", "");
                    if (f.size() != 4) continue;
                    rw::Note n { f[0].getDoubleValue(), f[1].getDoubleValue(), juce::jlimit (0, 127, f[2].getIntValue()), juce::jlimit (1, 127, f[3].getIntValue()) };
                    if (n.end >= n.start && n.start >= 0.0) r.parts[(size_t) p].push_back (n);
                }
    }
    { const juce::ScopedLock l (resLock); res = r; }
    st.store ((int) (r.ok ? Status::ready : Status::idle));
    if (r.ok) setMessage ("Restored the last result - drag parts into your DAW");
    ++version;
}

juce::AudioProcessorEditor* RewiredProcessor::createEditor() { return new RewiredEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RewiredProcessor(); }
