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
    { "bpm",      "Tempo",           'f', 0.0f, 250.0f, 1.0f, 1.0f, 0.0f, "bpm" },   // 1.1.0: whole bpm (was 0.1 steps), 0 = auto
};
juce::String noteText (const std::vector<rw::Note>& v)
{
    juce::String t; t.preallocateBytes (v.size() * 24);
    for (auto& n : v) t << juce::String (n.start, 4) << ',' << juce::String (n.end, 4) << ',' << n.pitch << ',' << n.velocity << ';';
    return t;
}
std::vector<rw::Note> parseNotes (const juce::String& text)
{
    std::vector<rw::Note> out;
    for (auto& tok : juce::StringArray::fromTokens (text, ";", ""))
    {
        auto f = juce::StringArray::fromTokens (tok, ",", "");
        if (f.size() != 4) continue;
        rw::Note n { f[0].getDoubleValue(), f[1].getDoubleValue(), juce::jlimit (0, 127, f[2].getIntValue()), juce::jlimit (1, 127, f[3].getIntValue()) };
        if (n.end >= n.start && n.start >= 0.0) out.push_back (n);
    }
    return out;
}
// map loudness envelopes in the project: 0.5 dB steps from -120 dBFS, one byte per 10 ms frame, base64 (~70 kB for 4 stems x 3 minutes)
juce::String envText (const std::vector<float>& pw)
{
    juce::MemoryBlock mb (pw.size());
    auto* d = static_cast<juce::uint8*> (mb.getData());
    for (size_t i = 0; i < pw.size(); ++i)
        d[i] = pw[i] <= 0.0f ? 0 : (juce::uint8) juce::jlimit (1, 255, (int) std::lround ((10.0 * std::log10 ((double) pw[i]) + 120.0) * 2.0));
    return mb.toBase64Encoding();
}
std::vector<float> parseEnv (const juce::String& text)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (text)) return {};
    std::vector<float> pw (mb.getSize());
    const auto* d = static_cast<const juce::uint8*> (mb.getData());
    for (size_t i = 0; i < pw.size(); ++i) pw[i] = d[i] == 0 ? 0.0f : (float) std::pow (10.0, (d[i] / 2.0 - 120.0) / 10.0);
    return pw;
}

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
struct PreviewJob final : juce::Thread
{
    explicit PreviewJob (RewiredProcessor& p) : juce::Thread ("CMPLXTY Rewired preview"), proc (p) {}
    void run() override { proc.loadPreview(); }
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
    apvts.addParameterListener ("bpm", this);
    apvts.addParameterListener ("quantize", this);
}

RewiredProcessor::~RewiredProcessor()
{
    apvts.removeParameterListener ("bpm", this);
    apvts.removeParameterListener ("quantize", this);
    cancelPendingUpdate();
    cancel.store (true);
    if (worker) worker->stopThread (20000);
    if (loader) loader->stopThread (20000);
}

bool RewiredProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

// audio passes through untouched (Rewired is an offline tool that can live on any track); 1.1.0: the PLAY / SOLO preview is added on top
void RewiredProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) b.clear (c, 0, b.getNumSamples());
    // the DAW starting its own playback stops the preview (two things playing at once is never what anyone wants)
    if (! isPlaying.load()) { hostWasPlaying.store (true); return; }   // idle: pass-through only (a DAW already playing when PLAY is pressed is fine)
    bool hostPlays = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) hostPlays = pos->getIsPlaying();
    if (hostPlays && ! hostWasPlaying.load()) stopping.store (true);
    hostWasPlaying.store (hostPlays);
    renderPreview (b);
}

void RewiredProcessor::renderPreview (juce::AudioBuffer<float>& b)
{
    const juce::SpinLock::ScopedTryLockType l (playLock);
    const int n = b.getNumSamples(), nch = b.getNumChannels();
    const double sr = hostRate.load(), t0 = playPos.load();
    if (! l.isLocked() || playPv == nullptr || n <= 0) return;
    const auto& P = *playPv;
    const std::vector<int16_t>* mono = nullptr;
    if (soloActive) mono = soloStem >= 0 && P.hasStem (soloStem) ? &P.stems[(size_t) soloStem] : soloBuf.get();
    const double rate = soloActive ? rw::Preview::kStemRate : rw::Preview::kMixRate;
    const auto* L = soloActive ? mono : &P.mixL;
    const auto* R = soloActive ? mono : (P.mixR.empty() ? &P.mixL : &P.mixR);
    const double len = P.seconds();
    const float fadeStep = (float) (1.0 / (0.008 * sr));   // 8 ms fades: no click on start / stop
    auto at = [] (const std::vector<int16_t>* v, double x)
    {
        if (v == nullptr || v->empty() || x < 0.0) return 0.0f;
        const size_t i = (size_t) x; if (i + 1 >= v->size()) return 0.0f;
        const float f = (float) (x - (double) i);
        return ((1.0f - f) * (float) (*v)[i] + f * (float) (*v)[i + 1]) * (1.0f / 32768.0f);
    };
    const bool stop = stopping.load();
    for (int i = 0; i < n; ++i)
    {
        fade = stop ? std::max (0.0f, fade - fadeStep) : std::min (1.0f, fade + fadeStep);
        const double x = (t0 + i / sr) * rate;
        const float g = 0.9f * fade;
        const float sl = at (L, x) * g, sr2 = at (R, x) * g;
        if (nch == 1) b.addSample (0, i, 0.5f * (sl + sr2));
        else if (nch > 1) { b.addSample (0, i, sl); b.addSample (1, i, sr2); }
    }
    const double t1 = t0 + n / sr;
    playPos.store (std::min (t1, len));
    if (t1 >= len) { isPlaying.store (false); stopping.store (false); fade = 0.0f; }
    else if (stop && fade <= 0.0f)
    {
        bool was = true;   // a play() that came in during this block cleared stopping: keep playing
        if (stopping.compare_exchange_strong (was, false)) { isPlaying.store (false); fade = 0.0f; }
    }
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
    dropPreview();   // the preview was the old file
    ++version;
    return {};
}

void RewiredProcessor::clearInput (int slot)
{
    bool had; { const juce::ScopedLock l (inLock); had = inputs[slot] != juce::File(); inputs[slot] = juce::File(); }
    if (had) dropPreview();
    ++version;
}

void RewiredProcessor::clearAll()
{
    cancelProcess();
    { const juce::ScopedLock l (inLock); for (auto& f : inputs) f = juce::File(); }
    { const juce::ScopedLock l (resLock); res = {}; }
    dropPreview(); playPos.store (0.0);
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
        auto sn = s;   // a stem called "Horns.wav" / "Strings.flac" names its own instrument; the model only guesses for the others
        for (int i = 0; i < rw::kNumStemSlots; ++i)
            if (in[i + 1] != juce::File()) sn.stemInst[(size_t) i] = rw::instFromFileName (in[i + 1].getFileNameWithoutExtension().toStdString());
        r = engine.transcribeStems (stems, sn, &cancel, progress);
    }
    if (! r.ok) { fail (r.error == "cancelled" ? juce::String ("Cancelled") : juce::String (r.error)); return; }
    int total = 0; for (auto& p : r.parts) total += (int) p.size();
    { const juce::ScopedLock l (resLock); res = std::move (r); }
    prog.store (1.0f);
    const auto secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const auto rr = result();
    setMessage (juce::String (total) + " notes in " + juce::String (secs, 1) + " s"
                + (rr.map.ok ? "  -  " + juce::String ((int) rr.map.sections.size()) + " sections, " + juce::String (rr.map.bars) + " bars" : juce::String())
                + (songMode() ? "  -  melody from " + juce::String (rr.melodyFrom) : juce::String()));
    st.store ((int) Status::ready); ++version;
    triggerAsyncUpdate();   // message thread: take the preview audio, re-grid if the tempo / quantize changed during the run
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

juce::File RewiredProcessor::exportSub (int part, int sub, juce::String& err)
{
    const auto r = result();
    if (! r.ok) { err = "Nothing to export yet - press PROCESS first"; return {}; }
    if (part < 0 || part >= rw::kNumParts || sub < 0 || sub >= (int) r.subs[(size_t) part].size()) { err = "No such instrument"; return {}; }
    const auto& sp = r.subs[(size_t) part][(size_t) sub];
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const juce::String name = juce::String (rw::partName (part)) + " - " + juce::String (sp.name);
    const auto f = dir.getChildFile (safeName (title()) + " - " + safeName (name) + ".mid");
    rw::io::MidiTrack t; t.name = name; t.channel = 1; t.program = rw::instGmProgram (sp.inst, part); t.notes = &sp.notes;
    if (! rw::io::writeTracks (f, r, { t }, title(), err)) return {};
    return f;
}

void RewiredProcessor::setMapMidi (const juce::String& lane, bool on)
{
    { const juce::ScopedLock l (inLock); if (on) mapMidiLanes.addIfNotAlreadyThere (lane); else mapMidiLanes.removeString (lane); }
    if (lane == solo()) rebuildSolo();   // SOLO plays what the file will hold: the stem for a block lane, the notes for a REAL MIDI one
    ++version;
}

std::vector<bool> RewiredProcessor::mapMidiFlags (const rw::SongMap& m) const
{
    std::vector<bool> f (m.lanes.size(), false);
    const juce::ScopedLock l (inLock);
    for (size_t i = 0; i < m.lanes.size(); ++i) f[i] = mapMidiLanes.contains (juce::String (m.lanes[i].name));
    return f;
}

juce::File RewiredProcessor::exportMap (juce::String& err)
{
    const auto r = result();
    if (! r.ok || ! r.map.ok) { err = "No song map yet - press PROCESS first"; return {}; }
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const auto f = dir.getChildFile (safeName (title()) + " - Map.mid");
    if (! rw::io::writeMapMidi (f, r, mapMidiFlags (r.map), title(), err)) return {};
    return f;
}

// ---------------------------------------------------------------------------------------------------- 1.1.0 tempo
double RewiredProcessor::effectiveBpm() const
{
    { const juce::ScopedLock l (resLock); if (res.ok) return res.bpm; }
    return std::round (apvts.getRawParameterValue ("bpm")->load());
}
bool RewiredProcessor::bpmIsAuto() const { return apvts.getRawParameterValue ("bpm")->load() < 0.5f; }

void RewiredProcessor::setBpm (double bpm)
{
    auto* p = apvts.getParameter ("bpm");
    const float v = bpm <= 0.0 ? 0.0f : (float) juce::jlimit (20.0, 250.0, std::round (bpm));
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    handleUpdateNowIfNeeded();   // re-grid now (the UI shows the new bars at once)
}

bool RewiredProcessor::detectionChanged() const
{
    const juce::ScopedLock l (resLock);
    if (! res.ok) return false;
    const auto a = res.settings, b = currentSettings();
    auto ne = [] (float x, float y) { return std::abs (x - y) > 1e-4f; };
    return ne (a.onsetThreshold, b.onsetThreshold) || ne (a.frameThreshold, b.frameThreshold) || ne (a.minNoteMs, b.minNoteMs)
        || a.melodySource != b.melodySource || a.melodyGap != b.melodyGap || ne (a.chordMinNotes, b.chordMinNotes) || ne (a.drumSensitivity, b.drumSensitivity);
}

void RewiredProcessor::syncGrid()
{
    if (status() != Status::ready) return;
    const double want = std::round (apvts.getRawParameterValue ("bpm")->load());
    const int q = (int) apvts.getRawParameterValue ("quantize")->load();
    {
        const juce::ScopedLock l (resLock);
        if (! res.ok || (std::abs (res.forcedBpm - (want < 0.5 ? 0.0 : want)) < 1e-9 && res.quantize == q)) return;
        const bool edit = res.forcedBpm >= 0.0, tempoEdit = edit && std::abs (res.forcedBpm - (want < 0.5 ? 0.0 : want)) > 1e-9;
        rw::regrid (res, want, q);
        if (edit)   // say what changed (a result that just arrived keeps its own message)
        {
            static const char* qn[] = { "Off", "1/16", "1/8", "1/4" };
            const juce::String bars = juce::String (res.map.ok ? res.map.bars : 0) + " bars";
            const juce::String det = juce::String (juce::roundToInt (res.map.autoBpm > 0.0 ? res.map.autoBpm : res.detectedBpm));
            setMessage (tempoEdit ? (want > 0.5 ? "Tempo " + juce::String (juce::roundToInt (want)) + " bpm (AUTO is " + det + ")  -  " + bars + ", NOTES and MIDI follow"
                                                : "Tempo AUTO: " + juce::String (juce::roundToInt (res.bpm)) + " bpm  -  " + bars)
                                  : "Quantize " + juce::String (qn[juce::jlimit (0, 3, q)]) + " on the " + juce::String (juce::roundToInt (res.bpm)) + " bpm grid");
        }
    }
    ++version;
    rebuildSolo();
}

void RewiredProcessor::handleAsyncUpdate()
{
    syncGrid();
    std::shared_ptr<const rw::Preview> fresh;
    { const juce::ScopedLock l (resLock); if (res.ok && res.preview != nullptr && res.preview != pv) fresh = res.preview; }
    { const juce::ScopedLock l (inLock); if (loaded != nullptr) { fresh = std::move (loaded); loaded.reset(); } }
    if (fresh != nullptr) setPreview (std::move (fresh));
    const double from = pendingPlay.load();
    if (from >= 0.0 && pv != nullptr && ! loading.load()) { pendingPlay.store (-1.0); play (from); }
    else if (! loading.load()) pendingPlay.store (-1.0);
}

// ---------------------------------------------------------------------------------------------------- 1.1.0 preview
bool RewiredProcessor::canPlay() const { return pv != nullptr || hasAnyInput(); }

double RewiredProcessor::playLength() const
{
    if (pv != nullptr) return pv->seconds();
    const juce::ScopedLock l (resLock); return res.ok ? res.seconds : 0.0;
}

void RewiredProcessor::setPreview (std::shared_ptr<const rw::Preview> p)
{
    pv = std::move (p);
    std::shared_ptr<const rw::Preview> old;
    { const juce::SpinLock::ScopedLockType l (playLock); old = playPv; playPv = pv; }
    if (pv == nullptr) { isPlaying.store (false); stopping.store (false); }
    rebuildSolo();
    ++version;
}

void RewiredProcessor::dropPreview()
{
    ++previewGen;   // a load still running is for the old inputs: thrown away when it finishes
    pendingPlay.store (-1.0);
    stop(); setPreview (nullptr);
    const juce::ScopedLock l (resLock); res.preview.reset();
}

void RewiredProcessor::loadPreview()
{
    const int gen = previewGen.load();
    juce::File in[kNumSlots]; { const juce::ScopedLock l (inLock); for (int i = 0; i < kNumSlots; ++i) in[i] = inputs[i]; }
    auto p = std::make_shared<rw::Preview>();
    juce::String e;
    if (in[kSong] != juce::File())
    {
        rw::Audio a;
        if (rw::io::loadAudio (in[kSong], a, e)) { p->mixL = rw::toPcm16 (a.left); if (! a.right.empty()) p->mixR = rw::toPcm16 (a.right); }
    }
    else
    {
        std::array<rw::Audio, rw::kNumStemSlots> au; size_t n = 0; bool stereo = false;
        for (int k = 0; k < rw::kNumStemSlots && ! threadShouldExit(); ++k)
            if (in[k + 1] != juce::File() && rw::io::loadAudio (in[k + 1], au[(size_t) k], e)) { n = std::max (n, au[(size_t) k].size()); stereo |= ! au[(size_t) k].right.empty(); }
        std::vector<float> L (n, 0.0f), R (stereo ? n : 0, 0.0f);
        for (int k = 0; k < rw::kNumStemSlots; ++k)
        {
            const auto& a = au[(size_t) k];
            if (a.empty()) continue;
            std::vector<float> m (a.left);
            for (size_t i = 0; i < a.size(); ++i)
            {
                const float r = a.right.empty() ? a.left[i] : a.right[i];
                m[i] = 0.5f * (a.left[i] + r);
                L[i] += a.left[i]; if (stereo) R[i] += r;
            }
            p->stems[(size_t) k] = rw::decimate2 (m);
        }
        p->mixL = rw::toPcm16 (L); if (stereo) p->mixR = rw::toPcm16 (R);
    }
    if (gen != previewGen.load()) p.reset();   // the inputs changed while loading
    else if (p->mixL.empty()) { setMessage ("Can't play: " + (e.isNotEmpty() ? e : juce::String ("no audio"))); p.reset(); }
    { const juce::ScopedLock l (inLock); loaded = p; }
    loading.store (false);
    triggerAsyncUpdate();
}

bool RewiredProcessor::threadShouldExit() const { return loader != nullptr && loader->threadShouldExit(); }

void RewiredProcessor::play (double from)
{
    if (! canPlay()) return;
    if (pv == nullptr)
    {
        pendingPlay.store (std::max (0.0, from));
        if (! loading.load())
        {
            if (loader) loader->stopThread (20000);
            loading.store (true);
            loader = std::make_unique<PreviewJob> (*this);
            loader->startThread();
        }
        ++version;
        return;
    }
    playPos.store (juce::jlimit (0.0, pv->seconds(), from));
    stopping.store (false);
    isPlaying.store (true);
    ++version;
}

void RewiredProcessor::stop()
{
    pendingPlay.store (-1.0);
    if (isPlaying.load()) stopping.store (true);
    ++version;
}

void RewiredProcessor::seek (double t)
{
    playPos.store (juce::jlimit (0.0, std::max (0.0, playLength()), t));
    ++version;
}

int RewiredProcessor::laneSoloSource (const rw::MapLane& L, const rw::Result& r) const
{
    if (L.fromAudio && L.stem >= 0 && pv != nullptr && pv->hasStem (L.stem) && ! mapMidi (juce::String (L.name))) return 1;
    if (L.part >= 0)
    {
        const auto& v = L.sub >= 0 && (size_t) L.sub < r.subs[(size_t) L.part].size() ? r.subs[(size_t) L.part][(size_t) L.sub].notes : r.parts[(size_t) L.part];
        if (! v.empty()) return 2;
    }
    for (int p : L.midiParts) if (! r.parts[(size_t) p].empty()) return 2;
    return 0;
}

void RewiredProcessor::setSolo (const juce::String& lane)
{
    { const juce::ScopedLock l (inLock); soloLane = lane; }
    rebuildSolo();
    ++version;
}

void RewiredProcessor::rebuildSolo()
{
    const auto lane = solo();
    const auto r = result();
    const rw::MapLane* L = nullptr;
    if (lane.isNotEmpty() && r.ok) for (auto& x : r.map.lanes) if (juce::String (x.name) == lane) L = &x;
    int stem = -1; std::shared_ptr<const std::vector<int16_t>> buf; bool active = false;
    if (L != nullptr)
    {
        const int src = laneSoloSource (*L, r);
        if (src == 1) { stem = L->stem; active = true; }
        else if (src == 2)
        {
            std::vector<const std::vector<rw::Note>*> v;
            if (L->part >= 0) v.push_back (L->sub >= 0 && (size_t) L->sub < r.subs[(size_t) L->part].size() ? &r.subs[(size_t) L->part][(size_t) L->sub].notes : &r.parts[(size_t) L->part]);
            else for (int p : L->midiParts) v.push_back (&r.parts[(size_t) p]);
            const bool drums = L->drumNote >= 0 || (L->part >= 0 && rw::isDrumPart (L->part));
            buf = std::make_shared<const std::vector<int16_t>> (rw::renderNotes (v, drums, std::max (r.seconds, pv != nullptr ? pv->seconds() : 0.0)));
            active = true;
        }
    }
    std::shared_ptr<const std::vector<int16_t>> old;
    { const juce::SpinLock::ScopedLockType l (playLock); old = soloBuf; soloBuf = buf; soloStem = stem; soloActive = active; }
}

bool RewiredProcessor::testWaitPreview (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (loading.load() && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (10);
    handleUpdateNowIfNeeded();
    return pv != nullptr;
}

bool RewiredProcessor::testWaitProcess (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (status() == Status::working && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (20);
    handleUpdateNowIfNeeded();
    return status() == Status::ready;
}

void RewiredProcessor::testSetResult (const rw::Result& r)
{
    { const juce::ScopedLock l (resLock); res = r; res.ok = true; res.forcedBpm = -1.0; }   // -1: syncGrid puts it on the knob's tempo
    int total = 0; for (auto& p : r.parts) total += (int) p.size();
    setMessage (juce::String (total) + " notes  -  drag parts into your DAW");
    st.store ((int) Status::ready); ++version;
    syncGrid();
    setPreview (r.preview);
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
    xml.setAttribute ("view", mapView() ? "map" : "notes");
    xml.setAttribute ("solo", solo());
    { const juce::ScopedLock l (inLock); xml.setAttribute ("mapMidi", mapMidiLanes.joinIntoString ("|")); }
    const auto r = result();
    if (r.ok)
    {
        auto* rx = xml.createNewChildElement ("Result");
        rx->setAttribute ("bpm", r.bpm); rx->setAttribute ("seconds", r.seconds); rx->setAttribute ("from", juce::String (r.melodyFrom));
        // 1.1.0: what a later tempo / quantize change needs (re-grid without the models): the detected tempo, the grid it is on,
        // the unquantized notes when they differ, the settings it was made with and the map's loudness envelopes
        rx->setAttribute ("detected", r.detectedBpm); rx->setAttribute ("forced", r.forcedBpm); rx->setAttribute ("quantize", r.quantize);
        {
            auto* u = rx->createNewChildElement ("Used"); const auto& q = r.settings;
            u->setAttribute ("onset", q.onsetThreshold); u->setAttribute ("frame", q.frameThreshold); u->setAttribute ("minNote", q.minNoteMs);
            u->setAttribute ("melSrc", q.melodySource); u->setAttribute ("melGap", q.melodyGap); u->setAttribute ("chordMin", q.chordMinNotes); u->setAttribute ("drumSens", q.drumSensitivity);
        }
        if (r.hasRaw && r.quantize > 0)
        {
            auto* raw = rx->createNewChildElement ("Raw");
            for (int p = 0; p < rw::kNumParts; ++p) raw->createNewChildElement (rw::partName (p))->addTextElement (noteText (r.rawParts[(size_t) p]));
            for (int p = 0; p < rw::kNumParts; ++p)
                for (size_t k = 0; k < r.rawSubs[(size_t) p].size(); ++k)
                { auto* e = raw->createNewChildElement ("Sub"); e->setAttribute ("part", p); e->setAttribute ("k", (int) k); e->addTextElement (noteText (r.rawSubs[(size_t) p][k].notes)); }
        }
        {
            auto* fx = rx->createNewChildElement ("Feat"); fx->setAttribute ("leadIsVoice", r.mapFeat.leadIsVoice);
            for (auto& e : r.mapFeat.env)
            { auto* ex = fx->createNewChildElement ("Env"); ex->setAttribute ("group", e.group); ex->setAttribute ("stem", e.stem); ex->addTextElement (envText (e.pow)); }
        }
        for (int p = 0; p < rw::kNumParts; ++p)
        {
            juce::String t; t.preallocateBytes (r.parts[(size_t) p].size() * 24);
            for (auto& n : r.parts[(size_t) p]) t << juce::String (n.start, 4) << ',' << juce::String (n.end, 4) << ',' << n.pitch << ',' << n.velocity << ';';
            rx->createNewChildElement (rw::partName (p))->addTextElement (t);
        }
        // 1.0.1: instrument sub-parts (older versions ignore the extra child; a 1.0.0 state simply has none)
        for (int p = 0; p < rw::kNumParts; ++p)
            for (auto& sp : r.subs[(size_t) p])
            {
                auto* sx = rx->createNewChildElement ("Sub");
                sx->setAttribute ("part", p); sx->setAttribute ("inst", sp.inst); sx->setAttribute ("name", juce::String (sp.name));
                sx->setAttribute ("conf", sp.confidence); sx->setAttribute ("fromName", sp.fromName);
                juce::String t; for (auto& n : sp.notes) t << juce::String (n.start, 4) << ',' << juce::String (n.end, 4) << ',' << n.pitch << ',' << n.velocity << ';';
                sx->addTextElement (t);
            }
        // 1.1.0: the song map (older versions ignore it)
        if (r.map.ok)
        {
            const auto& m = r.map;
            auto* mx = rx->createNewChildElement ("Map");
            mx->setAttribute ("bpm", m.bpm); mx->setAttribute ("bar1", m.firstDownbeat); mx->setAttribute ("bars", m.bars);
            for (auto& sc : m.sections)
            {
                auto* e = mx->createNewChildElement ("Sec");
                e->setAttribute ("type", sc.type); e->setAttribute ("label", juce::String (sc.label)); e->setAttribute ("a", sc.startBar); e->setAttribute ("b", sc.endBar);
            }
            for (auto& L : m.lanes)
            {
                auto* e = mx->createNewChildElement ("Lane");
                e->setAttribute ("name", juce::String (L.name)); e->setAttribute ("group", L.group); e->setAttribute ("part", L.part); e->setAttribute ("sub", L.sub);
                juce::String mp; for (int p : L.midiParts) mp << p << ' ';
                e->setAttribute ("midiParts", mp.trim()); e->setAttribute ("audio", L.fromAudio); e->setAttribute ("child", L.child); e->setAttribute ("drum", L.drumNote);
                e->setAttribute ("stem", L.stem);
                juce::String on; for (auto b : L.barOn) on << (b ? '1' : '0');
                e->setAttribute ("bars", on);
                juce::String t; for (auto& b : L.blocks) t << juce::String (b.start, 4) << ',' << juce::String (b.end, 4) << ',' << b.velocity << ';';
                e->addTextElement (t);
            }
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
    mapViewOn.store (xml->getStringAttribute ("view") == "map");
    { const juce::ScopedLock l (inLock); soloLane = xml->getStringAttribute ("solo"); }
    { const juce::ScopedLock l (inLock); mapMidiLanes = juce::StringArray::fromTokens (xml->getStringAttribute ("mapMidi"), "|", ""); mapMidiLanes.removeEmptyStrings(); }
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
        for (auto* sx : rx->getChildWithTagNameIterator ("Sub"))
        {
            const int p = sx->getIntAttribute ("part", -1), inst = sx->getIntAttribute ("inst", -1);
            if (p < 0 || p >= rw::kNumParts || inst < 0 || inst >= rw::kNumInst || r.subs[(size_t) p].size() >= 8) continue;
            rw::SubPart sp; sp.inst = inst; sp.name = sx->getStringAttribute ("name", rw::instName (inst, p)).toStdString();
            sp.confidence = (float) juce::jlimit (0.0, 1.0, sx->getDoubleAttribute ("conf")); sp.fromName = sx->getBoolAttribute ("fromName");
            for (auto& tok : juce::StringArray::fromTokens (sx->getAllSubText(), ";", ""))
            {
                auto f = juce::StringArray::fromTokens (tok, ",", "");
                if (f.size() != 4) continue;
                rw::Note n { f[0].getDoubleValue(), f[1].getDoubleValue(), juce::jlimit (0, 127, f[2].getIntValue()), juce::jlimit (1, 127, f[3].getIntValue()) };
                if (n.end >= n.start && n.start >= 0.0) sp.notes.push_back (n);
            }
            r.subs[(size_t) p].push_back (std::move (sp));
        }
        if (auto* mx = rx->getChildByName ("Map"))
        {
            auto& m = r.map;
            m.bpm = juce::jlimit (20.0, 400.0, mx->getDoubleAttribute ("bpm", r.bpm)); m.firstDownbeat = mx->getDoubleAttribute ("bar1");
            m.bars = juce::jlimit (0, 10000, mx->getIntAttribute ("bars"));
            for (auto* e : mx->getChildWithTagNameIterator ("Sec"))
            {
                rw::MapSection sc; sc.type = juce::jlimit (0, rw::kNumSecTypes - 1, e->getIntAttribute ("type")); sc.label = e->getStringAttribute ("label").toStdString();
                sc.startBar = juce::jlimit (0, m.bars, e->getIntAttribute ("a")); sc.endBar = juce::jlimit (sc.startBar, m.bars, e->getIntAttribute ("b"));
                m.sections.push_back (sc);
            }
            for (auto* e : mx->getChildWithTagNameIterator ("Lane"))
            {
                if (m.lanes.size() >= 64) break;
                rw::MapLane L; L.name = e->getStringAttribute ("name").toStdString(); L.group = juce::jlimit (0, rw::kNumMapGroups - 1, e->getIntAttribute ("group"));
                L.part = juce::jlimit (-1, rw::kNumParts - 1, e->getIntAttribute ("part", -1)); L.sub = e->getIntAttribute ("sub", -1);
                if (L.part < 0 || L.sub >= (int) r.subs[(size_t) L.part].size()) L.sub = -1;
                for (auto& tok : juce::StringArray::fromTokens (e->getStringAttribute ("midiParts"), " ", "")) { const int p = tok.getIntValue(); if (tok.isNotEmpty() && p >= 0 && p < rw::kNumParts) L.midiParts.push_back (p); }
                L.fromAudio = e->getBoolAttribute ("audio"); L.child = e->getBoolAttribute ("child"); L.drumNote = juce::jlimit (-1, 127, e->getIntAttribute ("drum", -1));
                L.stem = juce::jlimit (-1, 3, e->getIntAttribute ("stem", -1));
                const auto bars = e->getStringAttribute ("bars");
                for (int b = 0; b < m.bars; ++b) L.barOn.push_back (b < bars.length() && bars[b] == '1');
                for (auto& tok : juce::StringArray::fromTokens (e->getAllSubText(), ";", ""))
                {
                    auto f = juce::StringArray::fromTokens (tok, ",", "");
                    if (f.size() != 3) continue;
                    rw::MapBlock b { f[0].getDoubleValue(), f[1].getDoubleValue(), juce::jlimit (1, 127, f[2].getIntValue()) };
                    if (b.end > b.start && b.start >= 0.0) L.blocks.push_back (b);
                }
                m.lanes.push_back (std::move (L));
            }
            m.ok = m.bars > 0 && ! m.sections.empty();
        }
        // 1.1.0 re-grid data (a state without it: the notes as they are, on the tempo / quantize the knobs had)
        r.detectedBpm = rx->getDoubleAttribute ("detected", r.bpm);
        r.forcedBpm = rx->hasAttribute ("forced") ? rx->getDoubleAttribute ("forced") : std::round (apvts.getRawParameterValue ("bpm")->load());
        r.quantize = rx->hasAttribute ("quantize") ? rx->getIntAttribute ("quantize") : (int) apvts.getRawParameterValue ("quantize")->load();
        if (auto* u = rx->getChildByName ("Used"))
        {
            auto& q = r.settings;
            q.onsetThreshold = (float) u->getDoubleAttribute ("onset", q.onsetThreshold); q.frameThreshold = (float) u->getDoubleAttribute ("frame", q.frameThreshold);
            q.minNoteMs = (float) u->getDoubleAttribute ("minNote", q.minNoteMs); q.melodySource = u->getIntAttribute ("melSrc", q.melodySource);
            q.melodyGap = u->getIntAttribute ("melGap", q.melodyGap); q.chordMinNotes = (float) u->getDoubleAttribute ("chordMin", q.chordMinNotes);
            q.drumSensitivity = (float) u->getDoubleAttribute ("drumSens", q.drumSensitivity);
        }
        else r.settings = currentSettings();
        r.rawParts = r.parts; r.rawSubs = r.subs; r.hasRaw = true;
        if (auto* raw = rx->getChildByName ("Raw"))
        {
            for (int p = 0; p < rw::kNumParts; ++p) if (auto* px = raw->getChildByName (rw::partName (p))) r.rawParts[(size_t) p] = parseNotes (px->getAllSubText());
            for (auto* e : raw->getChildWithTagNameIterator ("Sub"))
            {
                const int p = e->getIntAttribute ("part", -1), k = e->getIntAttribute ("k", -1);
                if (p >= 0 && p < rw::kNumParts && k >= 0 && k < (int) r.rawSubs[(size_t) p].size()) r.rawSubs[(size_t) p][(size_t) k].notes = parseNotes (e->getAllSubText());
            }
        }
        if (auto* fx = rx->getChildByName ("Feat"))
        {
            r.mapFeat.leadIsVoice = fx->getBoolAttribute ("leadIsVoice");
            for (auto* e : fx->getChildWithTagNameIterator ("Env"))
            {
                if (r.mapFeat.env.size() >= 6) break;
                rw::MapEnvelope en; en.group = juce::jlimit (0, rw::kNumMapGroups - 1, e->getIntAttribute ("group")); en.stem = juce::jlimit (-1, 3, e->getIntAttribute ("stem", -1));
                en.pow = parseEnv (e->getAllSubText());
                if (en.pow.size() <= 200000) r.mapFeat.env.push_back (std::move (en));
            }
        }
    }
    dropPreview();   // reloaded from the files on the first PLAY
    { const juce::ScopedLock l (resLock); res = r; }
    st.store ((int) (r.ok ? Status::ready : Status::idle));
    if (r.ok) setMessage ("Restored the last result - drag parts into your DAW");
    rebuildSolo();
    ++version;
}

juce::AudioProcessorEditor* RewiredProcessor::createEditor() { return new RewiredEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RewiredProcessor(); }
