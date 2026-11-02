#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"
#include "SiltParams.h"

namespace
{
const char* fxOrderChoices()
{
    static const std::string s = [] { std::string r; for (int i = 0; i < 24; ++i) { if (i) r += "|"; r += slt::fxPermName (i); } return r; }();
    return s.c_str();
}
const char* stepChoices()
{
    static const std::string s = [] { std::string r = "-"; for (int i = 1; i <= slt::kMaxSlices; ++i) r += "|" + std::to_string (i); return r; }();
    return s.c_str();
}
const char* sliceSrcChoices() { return "Transients|Grid 4|Grid 8|Grid 16|Grid 32|Manual"; }

const slt::PDef kParams[] = {
#define SLT_X(id, name, type, lo, hi, step, skew, def, unit, choices) { #id, name, type, lo, hi, step, skew, def, unit, choices },
    SLT_PARAM_LIST (SLT_X)
#undef SLT_X
};
static_assert (sizeof (kParams) / sizeof (kParams[0]) == (size_t) slt::pid::kNumParams, "param table");

// built-in sources are generated once per process (they are pure functions of the index)
std::shared_ptr<const slt::Audio> builtInAudio (int k)
{
    static std::mutex m;
    static std::array<std::shared_ptr<const slt::Audio>, slt::kNumBuiltIn> cache;
    k = juce::jlimit (0, slt::kNumBuiltIn - 1, k);
    const std::lock_guard<std::mutex> l (m);
    if (! cache[(size_t) k]) cache[(size_t) k] = std::make_shared<const slt::Audio> (slt::makeBuiltIn (k));
    return cache[(size_t) k];
}
} // namespace

struct SiltWorker final : juce::Thread
{
    explicit SiltWorker (SiltProcessor& p) : juce::Thread ("CMPLXTY Silt worker"), proc (p) {}
    void run() override { proc.workerLoop(); }
    SiltProcessor& proc;
};

bool SiltProcessor::readAudio (const juce::File& f, slt::Audio& a, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate < 8000.0) { err = "Can't read " + f.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)"; return false; }
    if ((double) r->lengthInSamples / r->sampleRate > kMaxSampleSeconds) { err = f.getFileName() + " is longer than 60 seconds - trim it first"; return false; }
    const int n = (int) r->lengthInSamples, ch = juce::jmin (2, (int) r->numChannels);
    juce::AudioBuffer<float> b (juce::jmax (1, ch), n);
    r->read (&b, 0, n, 0, true, ch > 1);
    a = {}; a.rate = r->sampleRate;
    a.L.assign (b.getReadPointer (0), b.getReadPointer (0) + n);
    if (ch > 1)
    {
        a.R.assign (b.getReadPointer (1), b.getReadPointer (1) + n);
        bool same = true; for (int i = 0; i < n && same; ++i) same = a.L[(size_t) i] == a.R[(size_t) i];
        if (same) a.R.clear();   // dual-mono file: keep it mono
    }
    return true;
}

SiltProcessor::SiltProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", slt::makeLayout (kParams))
{
    for (auto& d : kParams) raw.push_back (apvts.getRawParameterValue (d.id));
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds.addIfNotAlreadyThere ("bypass");    // presets never bypass / un-bypass
    presets.keepIds.addIfNotAlreadyThere ("keepSmp");   // the "keep my sample" switch is a preference
    presets.maxFileBytes = 64 * 1024 * 1024;            // presets carry the sample (FLAC, base64)
    presets.saveExtra = [this] (juce::XmlElement& x) { savePresetExtra (x); };
    presets.loadExtra = [this] (const juce::XmlElement& x) { loadPresetExtra (&x); };
    presets.onFactory = [this] (int i) { factoryLoaded (i); };
    apvts.addParameterListener ("sens", this);
    apvts.addParameterListener ("slSrc", this);
    slot[0].builtIn = slt::biGlass; slot[0].name = slt::builtInName (slt::biGlass); slot[0].version = 1;
    slot[1].builtIn = slt::biChoir; slot[1].name = slt::builtInName (slt::biChoir); slot[1].version = 1;
    setMessage ("Drop any sample on the display (layer A), or on the B strip for layer B");
    worker = std::make_unique<SiltWorker> (*this);
    worker->startThread();
    request();
}

SiltProcessor::~SiltProcessor()
{
    apvts.removeParameterListener ("sens", this);
    apvts.removeParameterListener ("slSrc", this);
    quit.store (true); wake.signal();
    if (worker) worker->stopThread (5000);
}

bool SiltProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return l.getMainInputChannelSet().isDisabled();
}

double SiltProcessor::getTailLengthSeconds() const { return juce::jlimit (1.0, 20.0, (double) val (slt::pid::aR) + 6.0); }

void SiltProcessor::prepareToPlay (double rate, int maxBlock)
{
    sr = rate;
    eng.prepare (rate);
    eng.setSources (nullptr, nullptr);
    cur = {}; retired = {}; seenPub = -1;
    setLatencySamples (eng.latency());
    bypass.prepare (rate, juce::jmax (1, maxBlock), juce::jmax (1, getTotalNumOutputChannels()), 0);
    if (bypassParam) bypass.snap (bypassParam->get());
    bufL.assign ((size_t) juce::jmax (256, maxBlock), 0.0f); bufR.assign (bufL.size(), 0.0f);
    keyboard.reset();
}

void SiltProcessor::setMessage (const juce::String& m) { const juce::SpinLock::ScopedLockType l (msgLock); message = m; }

slt::Params SiltProcessor::currentParams() const
{
    using namespace slt::pid;
    slt::Params P;
    auto I = [this] (int i) { return juce::roundToInt (val (i)); };
    auto B = [this] (int i) { return val (i) >= 0.5f; };
    P.pos = val (pos); P.scan = val (scan); P.density = val (density); P.size = val (size) / 1000.0f; P.sizeRand = val (sizeRand);
    P.pitch = val (pitch); P.fine = val (fine); P.pitchJit = val (pjit); P.spray = val (spray); P.spread = val (spread); P.shape = val (shape);
    P.reverse = val (reverse); P.magnet = val (magnet); P.gcomp = val (gcomp);
    P.tune = I (tune); P.root = val (root); P.scale = I (scale); P.key = I (key); P.cohere = val (cohere); P.glide = val (glide);
    P.voices = I (voices); P.bendRange = val (bend); P.mpe = B (mpe);
    P.bOn = B (bOn); P.morph = val (morph); P.morphMode = I (morphMode); P.bOffset = val (bOffset); P.bPitch = val (bPitch); P.keyMatch = B (keyMatch);
    P.fOn = B (fOn); P.fType = I (fType); P.cutoff = val (cutoff); P.res = val (res); P.fRand = val (fRand); P.fKey = val (fKey);
    P.aA = val (aA); P.aD = val (aD); P.aS = val (aS); P.aR = val (aR); P.velSens = val (velSens); P.levelDb = val (level);
    P.l1Rate = val (l1Rate); P.l1Sync = I (l1Sync); P.l1Shape = I (l1Shape); P.l2Rate = val (l2Rate); P.l2Sync = I (l2Sync); P.l2Shape = I (l2Shape);
    P.e2A = val (e2A); P.e2D = val (e2D); P.e2S = val (e2S); P.e2R = val (e2R);
    for (int i = 0; i < slt::kMacros; ++i) P.macro[i] = val (m1 + i);
    for (int i = 0; i < slt::kModSlots; ++i) { P.modSrc[i] = I (ms1 + i); P.modDst[i] = I (md1 + i); P.modAmt[i] = val (ma1 + i); }
    P.mode = I (mode); P.slPlay = I (slPlay); P.slNote = I (slNote);
    P.seq = B (seq); P.seqRate = I (seqRate); P.seqLen = I (seqLen); P.seqSwing = val (seqSwing); P.seqGate = val (seqGate); P.seqTrig = I (seqTrig);
    P.fxOrder = I (fxOrder);
    P.rpOn = B (rpOn); P.rpLen = I (rpLen); P.rpChance = val (rpChance); P.rpMix = val (rpMix);
    P.cpOn = B (cpOn); P.cpThr = val (cpThr); P.cpRatio = val (cpRatio); P.cpGain = val (cpGain);
    P.dlOn = B (dlOn); P.dlTime = I (dlTime); P.dlFb = val (dlFb); P.dlMix = val (dlMix);
    P.rvOn = B (rvOn); P.rvSize = val (rvSize); P.rvDark = val (rvDark); P.rvMix = val (rvMix);
    return P;
}

// ------------------------------------------------------------------------------------------------ audio thread
void SiltProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = b.getNumSamples(), nch = b.getNumChannels();
    b.clear();
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    const bool byp = bypassParam != nullptr && bypassParam->get();
    bypass.pushDry (b);   // a synth: the "dry" signal is silence

    slt::Transport tr; tr.bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) if (*bpm > 1.0) tr.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) tr.ppq = *ppq;
            tr.playing = pos->getIsPlaying();
            hostBpm.store (pos->getBpm() ? *pos->getBpm() : 0.0);
        }
    hostRun.store (tr.playing);

    eng.setParams (currentParams());
    int pat[slt::kSteps];
    for (int i = 0; i < slt::kSteps; ++i) pat[i] = juce::roundToInt (val (slt::pid::st1 + i)) - 1;
    eng.setPattern (pat);

    // new sources / slices from the worker (never blocks: try again next block if the worker holds the lock)
    {
        const juce::SpinLock::ScopedTryLockType l (pubLock);
        if (l.isLocked() && pubVersion != seenPub)
        {
            int freeSlots = 0; for (auto& r : retired) freeSlots += r ? 0 : 1;
            if (freeSlots >= 2)
            {
                for (size_t k = 0; k < 2; ++k)
                    if (cur[k] != srcPub[k])
                    {
                        for (auto& r : retired) if (! r) { r = std::move (cur[k]); break; }
                        cur[k] = srcPub[k];
                    }
                const int ns = juce::jmin ((int) slicesPub.size(), slt::kMaxSlices);
                for (int i = 0; i < ns; ++i) sliceBuf[(size_t) i] = slicesPub[(size_t) i];
                seenPub = pubVersion;
                eng.setSources (cur[0].get(), cur[1].get());
                eng.setSlices (sliceBuf.data(), ns);
            }
        }
    }

    int done = 0;
    auto renderTo = [&] (int end)
    {
        while (done < end)
        {
            const int m = juce::jmin (end - done, (int) bufL.size());
            slt::Transport t = tr; t.ppq = tr.ppq + (double) done / sr * tr.bpm / 60.0;
            eng.render (bufL.data(), bufR.data(), m, t);
            if (nch >= 2) { b.copyFrom (0, done, bufL.data(), m); b.copyFrom (1, done, bufR.data(), m); }
            else if (nch == 1) { auto* o = b.getWritePointer (0, done); for (int i = 0; i < m; ++i) o[i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]); }
            done += m;
        }
    };
    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        const auto msg = meta.getMessage();
        const int ch = juce::jlimit (0, 15, msg.getChannel() - 1);
        if (msg.isNoteOn()) eng.noteOn (ch, msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) eng.noteOff (ch, msg.getNoteNumber());
        else if (msg.isPitchWheel()) eng.pitchBend (ch, juce::jlimit (-1.0f, 1.0f, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f));
        else if (msg.isChannelPressure()) eng.channelPressure (ch, (float) msg.getChannelPressureValue() / 127.0f);
        else if (msg.isAftertouch()) eng.polyPressure (ch, msg.getNoteNumber(), (float) msg.getAfterTouchValue() / 127.0f);
        else if (msg.isController()) eng.controller (ch, msg.getControllerNumber(), (float) msg.getControllerValue() / 127.0f);
    }
    renderTo (n);
    midi.clear();

    for (auto& r : retired) if (r && ! eng.uses (r.get())) r.reset();   // the worker still holds a reference: never freed here
    bypass.mix (b, byp);
    if (byp && bypass.fullyBypassed()) eng.allNotesOff (false);
}

// ------------------------------------------------------------------------------------------------ samples
juce::String SiltProcessor::loadFile (int layer, const juce::File& f)
{
    slt::Audio a; juce::String err;
    if (! readAudio (f, a, err)) { setMessage (err); return err; }
    err = loadAudio (layer, std::move (a), f.getFileNameWithoutExtension());
    if (err.isEmpty() && layer == kB)
        if (auto* p = apvts.getParameter ("bOn")) if (p->getValue() < 0.5f) p->setValueNotifyingHost (1.0f);
    return err;
}

juce::String SiltProcessor::loadAudio (int layer, slt::Audio a, const juce::String& name, int builtIn)
{
    layer = juce::jlimit (0, 1, layer);
    if (a.len() < 64 || a.rate < 8000.0) { const juce::String e = "That sample is empty or too short"; setMessage (e); return e; }
    if ((double) a.len() / a.rate > kMaxSampleSeconds + 0.5) { const juce::String e = "Samples can be up to 60 seconds long"; setMessage (e); return e; }
    if (a.stereo() && a.R.size() != a.L.size()) a.R.resize (a.L.size(), 0.0f);
    for (auto* ch : { &a.L, &a.R }) for (auto& x : *ch) if (! std::isfinite (x)) x = 0.0f;
    {
        const juce::ScopedLock l (inLock);
        auto& s = slot[(size_t) layer];
        s.audio = std::make_shared<const slt::Audio> (std::move (a)); s.name = name; s.builtIn = builtIn; ++s.version;
        if (layer == kA) { manual.clear(); ++manualVersion; }
    }
    if (layer == kA && juce::roundToInt (val (slt::pid::slSrc)) == slt::kSlManual)
        if (auto* p = apvts.getParameter ("slSrc")) p->setValueNotifyingHost (p->convertTo0to1 ((float) slt::kSlTransients));
    setMessage (juce::String (layer == kA ? "A: " : "B: ") + name);
    request(); ++version;
    return {};
}

void SiltProcessor::loadBuiltIn (int layer, int k)
{
    layer = juce::jlimit (0, 1, layer); k = juce::jlimit (0, slt::kNumBuiltIn - 1, k);
    {
        const juce::ScopedLock l (inLock);
        auto& s = slot[(size_t) layer];
        if (s.builtIn == k && s.audio == nullptr) return;
        if (s.builtIn == k && s.audio != nullptr) return;
        s.audio = nullptr; s.builtIn = k; s.name = slt::builtInName (k); ++s.version;
        if (layer == kA) { manual.clear(); ++manualVersion; }
    }
    if (layer == kA && juce::roundToInt (val (slt::pid::slSrc)) == slt::kSlManual)
        if (auto* p = apvts.getParameter ("slSrc")) p->setValueNotifyingHost (p->convertTo0to1 ((float) slt::kSlTransients));
    request(); ++version;
}

void SiltProcessor::clearLayer (int layer)
{
    layer = juce::jlimit (0, 1, layer);
    { const juce::ScopedLock l (inLock); auto& s = slot[(size_t) layer]; s = Slot { nullptr, {}, -1, s.version + 1 }; if (layer == kA) { manual.clear(); ++manualVersion; } }
    request(); ++version;
}

juce::String SiltProcessor::sampleName (int layer) const { const juce::ScopedLock l (inLock); return slot[(size_t) juce::jlimit (0, 1, layer)].name; }
int SiltProcessor::builtInOf (int layer) const { const juce::ScopedLock l (inLock); return slot[(size_t) juce::jlimit (0, 1, layer)].builtIn; }
bool SiltProcessor::hasSample (int layer) const { const juce::ScopedLock l (inLock); const auto& s = slot[(size_t) juce::jlimit (0, 1, layer)]; return s.audio != nullptr || s.builtIn >= 0; }

void SiltProcessor::setManualSlices (std::vector<int> starts)
{
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    starts.erase (std::remove_if (starts.begin(), starts.end(), [] (int v) { return v < 0; }), starts.end());
    if ((int) starts.size() > slt::kMaxSlices) starts.resize ((size_t) slt::kMaxSlices);
    { const juce::ScopedLock l (inLock); manual = std::move (starts); ++manualVersion; }
    if (auto* p = apvts.getParameter ("slSrc")) if (juce::roundToInt (val (slt::pid::slSrc)) != slt::kSlManual) p->setValueNotifyingHost (p->convertTo0to1 ((float) slt::kSlManual));
    request(); ++version;
}
std::vector<int> SiltProcessor::manualSlices() const { const juce::ScopedLock l (inLock); return manual; }

float SiltProcessor::effectiveRoot() const
{
    const int t = juce::roundToInt (val (slt::pid::tune));
    if (t == slt::tuneOff) return 60.0f;
    if (t == slt::tuneManual) return val (slt::pid::root);
    const auto s = source (kA);
    return s ? s->root : 60.0f;
}

juce::String SiltProcessor::rootDescription() const
{
    const int t = juce::roundToInt (val (slt::pid::tune));
    const auto s = source (kA);
    if (t == slt::tuneOff) return "Tune off: C3 plays the sample as is";
    if (t == slt::tuneManual) return "Root " + slt::ptab::noteText (val (slt::pid::root)) + " (manual)";
    if (! s) return busy() ? "Analysing..." : "No sample";
    return "Root " + slt::ptab::noteText (s->root) + " (" + juce::String (s->rootHow) + ")";
}

void SiltProcessor::useDetectedRoot()
{
    const auto s = source (kA); if (! s) return;
    if (auto* p = apvts.getParameter ("root")) p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (24.0f, 96.0f, s->root)));
    if (auto* p = apvts.getParameter ("tune")) p->setValueNotifyingHost (p->convertTo0to1 ((float) slt::tuneManual));
}

// ------------------------------------------------------------------------------------------------ mod matrix
int SiltProcessor::modDstForParam (const juce::String& id)
{
    static const std::pair<const char*, int> m[] = { { "pos", slt::mdPos }, { "scan", slt::mdScan }, { "density", slt::mdDensity }, { "size", slt::mdSize },
        { "pitch", slt::mdPitch }, { "pjit", slt::mdPJit }, { "spray", slt::mdSpray }, { "spread", slt::mdSpread }, { "shape", slt::mdShape },
        { "reverse", slt::mdReverse }, { "magnet", slt::mdMagnet }, { "cohere", slt::mdCohere }, { "morph", slt::mdMorph }, { "cutoff", slt::mdCutoff },
        { "res", slt::mdRes }, { "level", slt::mdLevel } };
    for (auto& e : m) if (id == e.first) return e.second;
    return slt::mdNone;
}
int SiltProcessor::modSlotFor (int src, int dst) const
{
    for (int i = 0; i < slt::kModSlots; ++i)
        if (juce::roundToInt (val (slt::pid::ms1 + i)) == src && juce::roundToInt (val (slt::pid::md1 + i)) == dst) return i;
    return -1;
}
int SiltProcessor::assignMod (int src, int dst, float amount)
{
    if (src <= slt::msNone || src >= slt::kNumModSrc || dst <= slt::mdNone || dst >= slt::kNumModDst) return -1;
    int s = modSlotFor (src, dst);
    if (s < 0) for (int i = 0; i < slt::kModSlots && s < 0; ++i) if (juce::roundToInt (val (slt::pid::ms1 + i)) == 0 || juce::roundToInt (val (slt::pid::md1 + i)) == 0) s = i;
    if (s < 0) { setMessage ("All 8 modulation slots are in use - remove one first (MOD page)"); return -1; }
    auto set = [this] (int idx, float real) { auto* p = dynamic_cast<juce::RangedAudioParameter*> (getParameters()[idx]); p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (real)); p->endChangeGesture(); };
    set (slt::pid::ms1 + s, (float) src); set (slt::pid::md1 + s, (float) dst);
    if (std::fabs (val (slt::pid::ma1 + s)) < 0.005f || modSlotFor (src, dst) != s) set (slt::pid::ma1 + s, amount);
    return s;
}
void SiltProcessor::removeMod (int s)
{
    if (s < 0 || s >= slt::kModSlots) return;
    for (int idx : { slt::pid::ms1 + s, slt::pid::md1 + s, slt::pid::ma1 + s })
    { auto* p = dynamic_cast<juce::RangedAudioParameter*> (getParameters()[idx]); p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

// ------------------------------------------------------------------------------------------------ worker
void SiltProcessor::workerLoop()
{
    std::array<int, 2> doneSlot { -1, -1 };
    std::array<std::shared_ptr<const slt::Source>, 2> have;
    float doneSens = -1; int doneSl = -1, doneManual = -1;
    while (! quit.load() && ! juce::Thread::currentThreadShouldExit())
    {
        wake.wait (100);
        if (quit.load()) break;
        if (! pending.exchange (false)) { collectGarbage(); continue; }
        working.store (true);
        const float sens = val (slt::pid::sens);
        const int slSrc = juce::roundToInt (val (slt::pid::slSrc));
        std::array<Slot, 2> in; std::vector<int> man; int manV;
        { const juce::ScopedLock l (inLock); in = slot; man = manual; manV = manualVersion; }
        bool changed = false;
        for (size_t k = 0; k < 2; ++k)
        {
            if (in[k].version != doneSlot[k])
            {
                auto audio = in[k].audio;
                if (! audio && in[k].builtIn >= 0) audio = builtInAudio (in[k].builtIn);
                if (! audio) have[k] = nullptr;
                else
                {
                    auto s = std::make_shared<slt::Source>();
                    s->audio = audio; s->name = in[k].name.toStdString();
                    slt::analyse (*s, sens);
                    have[k] = s;
                }
                doneSlot[k] = in[k].version; changed = true;
            }
            else if (sens != doneSens && have[k])
            {
                auto s = std::make_shared<slt::Source> (*have[k]);
                slt::analyseOnsets (*s, sens);
                have[k] = s; changed = true;
            }
        }
        doneSens = sens;
        if (changed || slSrc != doneSl || manV != doneManual)
        {
            std::vector<int> sl;
            if (have[0])
            {
                if (slSrc == slt::kSlManual)
                {
                    for (int v : man) if (v >= 0 && v < have[0]->len() - 64) sl.push_back (v);
                    if (sl.empty() || sl.front() > 0) sl.insert (sl.begin(), 0);
                }
                else sl = slt::autoSlices (*have[0], slSrc);
                if ((int) sl.size() > slt::kMaxSlices) sl.resize ((size_t) slt::kMaxSlices);
            }
            doneSl = slSrc; doneManual = manV;
            const juce::SpinLock::ScopedLockType l (pubLock);
            srcPub = have; slicesPub = std::move (sl); ++pubVersion;
            for (auto& h : have) if (h && std::find (keepAlive.begin(), keepAlive.end(), h) == keepAlive.end()) keepAlive.push_back (h);
        }
        collectGarbage();
        working.store (false);
        ++version;
    }
}

// sources nobody uses any more are freed here (the audio thread only ever drops references, it never holds the last one)
void SiltProcessor::collectGarbage()
{
    std::vector<std::shared_ptr<const slt::Source>> dead;
    {
        const juce::SpinLock::ScopedLockType l (pubLock);
        for (size_t i = 0; i < keepAlive.size();)
            if (keepAlive[i] != srcPub[0] && keepAlive[i] != srcPub[1] && keepAlive[i].use_count() == 1) { dead.push_back (std::move (keepAlive[i])); keepAlive.erase (keepAlive.begin() + (long) i); }
            else ++i;
    }
}

bool SiltProcessor::waitIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    juce::Thread::sleep (20);
    while ((pending.load() || working.load()) && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (5);
    return ! pending.load() && ! working.load();
}

// ------------------------------------------------------------------------------------------------ FLAC helpers
juce::MemoryBlock SiltProcessor::flacOf (int layer) const
{
    const juce::ScopedLock l (inLock);
    const juce::ScopedLock bl (blobLock);
    const auto& s = slot[(size_t) layer];
    auto& mb = blobs[(size_t) layer];
    if (blobVersion[(size_t) layer] != s.version)
    {
        mb.reset();
        if (s.audio != nullptr && s.builtIn < 0)
        {
            const auto& a = *s.audio;
            const int ch = a.stereo() ? 2 : 1;
            juce::AudioBuffer<float> b (ch, a.len());
            b.copyFrom (0, 0, a.L.data(), a.len()); if (ch > 1) b.copyFrom (1, 0, a.R.data(), a.len());
            juce::FlacAudioFormat flac;
            auto* os = new juce::MemoryOutputStream (mb, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, a.rate, (unsigned) ch, 24, {}, 0));
            if (w == nullptr) { delete os; mb.reset(); }
            else w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        blobVersion[(size_t) layer] = s.version;
    }
    return mb;
}

bool SiltProcessor::audioFromFlac (const void* data, size_t size, slt::Audio& a)
{
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate < 8000.0 || (double) r->lengthInSamples / r->sampleRate > kMaxSampleSeconds + 1.0) return false;
    const int n = (int) r->lengthInSamples, ch = juce::jmin (2, (int) r->numChannels);
    juce::AudioBuffer<float> b (juce::jmax (1, ch), n);
    r->read (&b, 0, n, 0, true, ch > 1);
    a = {}; a.rate = r->sampleRate;
    a.L.assign (b.getReadPointer (0), b.getReadPointer (0) + n);
    if (ch > 1) a.R.assign (b.getReadPointer (1), b.getReadPointer (1) + n);
    return true;
}

// ------------------------------------------------------------------------------------------------ presets with samples
// User presets carry both layers: <SAMPLE layer="0" name=".." builtin="2"/> or <SAMPLE layer="0" name="..">base64 FLAC</SAMPLE>.
// "Keep My Sample" (on by default): a sample you dropped in stays when you browse presets; built-in sources follow the preset.
void SiltProcessor::savePresetExtra (juce::XmlElement& x)
{
    for (int k = 0; k < 2; ++k)
    {
        juce::String name; int bi; bool has;
        { const juce::ScopedLock l (inLock); name = slot[(size_t) k].name; bi = slot[(size_t) k].builtIn; has = slot[(size_t) k].audio != nullptr || bi >= 0; }
        if (! has) continue;
        auto* e = x.createNewChildElement ("SAMPLE");
        e->setAttribute ("layer", k); e->setAttribute ("name", name);
        if (bi >= 0) e->setAttribute ("builtin", bi);
        else { const auto mb = flacOf (k); if (mb.getSize() > 0) e->addTextElement (mb.toBase64Encoding()); }
    }
}

void SiltProcessor::loadPresetExtra (const juce::XmlElement* x)
{
    const bool keep = val (slt::pid::keepSmp) >= 0.5f;
    for (int k = 0; k < 2; ++k)
    {
        const juce::XmlElement* e = nullptr;
        if (x != nullptr) for (auto* c : x->getChildWithTagNameIterator ("SAMPLE")) if (c->getIntAttribute ("layer", -1) == k) e = c;
        if (e == nullptr) continue;
        bool userSample; { const juce::ScopedLock l (inLock); userSample = slot[(size_t) k].audio != nullptr && slot[(size_t) k].builtIn < 0; }
        if (keep && userSample) continue;
        if (e->hasAttribute ("builtin")) { loadBuiltIn (k, e->getIntAttribute ("builtin")); continue; }
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (e->getAllSubText().trim())) continue;
        slt::Audio a;
        if (audioFromFlac (mb.getData(), mb.getSize(), a)) loadAudio (k, std::move (a), e->getStringAttribute ("name", "Preset sample"));
    }
}

void SiltProcessor::factoryLoaded (int i)
{
    const bool keep = val (slt::pid::keepSmp) >= 0.5f;
    for (int k = 0; k < 2; ++k)
    {
        const int want = siltFactorySource (i, k);
        if (want < 0) continue;
        bool userSample; { const juce::ScopedLock l (inLock); userSample = slot[(size_t) k].audio != nullptr && slot[(size_t) k].builtIn < 0; }
        if (keep && userSample) continue;
        loadBuiltIn (k, want);
    }
}

// ------------------------------------------------------------------------------------------------ state
// "SLT1" + int + XML (parameters, sample names / built-ins, manual slices, UI size) + per layer: int size + FLAC (dropped samples only)
void SiltProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("SiltState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml.setAttribute ("uiW", uiWidth.load()); xml.setAttribute ("uiH", uiHeight.load()); }
    {
        const juce::ScopedLock l (inLock);
        for (int k = 0; k < 2; ++k)
        {
            xml.setAttribute ("name" + juce::String (k), slot[(size_t) k].name);
            xml.setAttribute ("builtin" + juce::String (k), slot[(size_t) k].builtIn);
            xml.setAttribute ("has" + juce::String (k), slot[(size_t) k].audio != nullptr || slot[(size_t) k].builtIn >= 0);
        }
        juce::StringArray m; for (int v : manual) m.add (juce::String (v));
        xml.setAttribute ("manual", m.joinIntoString (","));
    }
    std::array<juce::MemoryBlock, 2> bl { flacOf (0), flacOf (1) };
    const auto text = xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    juce::MemoryOutputStream out (dest, false);
    out.write ("SLT1", 4);
    out.writeInt ((int) text.getNumBytesAsUTF8());
    out.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
    for (auto& mb : bl) { out.writeInt ((int) mb.getSize()); if (mb.getSize() > 0) out.write (mb.getData(), mb.getSize()); }
}

void SiltProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size < 8) return;
    auto applyParams = [this] (const juce::XmlElement& x)
    {
        for (auto* prm : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (x.hasAttribute (r->paramID)) r->setValueNotifyingHost (r->convertTo0to1 ((float) x.getDoubleAttribute (r->paramID)));
        presets.setCurrent (x.getStringAttribute ("presetName", "Init"), x.getBoolAttribute ("presetUser"));
        uiWidth.store (x.getIntAttribute ("uiW")); uiHeight.store (x.getIntAttribute ("uiH"));
    };
    auto applySamples = [this] (const juce::XmlElement& x, std::array<slt::Audio, 2>& got)
    {
        const float slSrcWas = val (slt::pid::slSrc);   // loading a sample resets Manual slices: restore the session's choice after
        for (int k = 0; k < 2; ++k)
        {
            const auto ks = juce::String (k);
            const int bi = x.getIntAttribute ("builtin" + ks, -2);
            if (bi == -2) continue;   // older state without sample info: keep the defaults
            if (bi >= 0) loadBuiltIn (k, bi);
            else if (got[(size_t) k].len() > 0) loadAudio (k, std::move (got[(size_t) k]), x.getStringAttribute ("name" + ks));
            else clearLayer (k);
        }
        std::vector<int> m; for (auto& t : juce::StringArray::fromTokens (x.getStringAttribute ("manual"), ",", "")) if (t.isNotEmpty()) m.push_back (t.getIntValue());
        { const juce::ScopedLock l (inLock); manual = m; ++manualVersion; }
        if (auto* p = apvts.getParameter ("slSrc")) p->setValueNotifyingHost (p->convertTo0to1 (slSrcWas));
    };
    if (std::memcmp (data, "SLT1", 4) != 0)
    {   // plain JUCE binary XML (parameters only): foreign / hand-made sessions
        auto x = getXmlFromBinary (data, size);
        if (x != nullptr && x->hasTagName ("SiltState")) { applyParams (*x); request(); ++version; }
        return;
    }
    juce::MemoryInputStream in (data, (size_t) size, false);
    in.skipNextBytes (4);
    const int xb = in.readInt();
    if (xb <= 0 || xb > size - 8) return;
    juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, xb);
    auto xml = juce::parseXML (xm.toString());
    if (xml == nullptr || ! xml->hasTagName ("SiltState")) return;
    applyParams (*xml);
    std::array<slt::Audio, 2> got;
    for (int k = 0; k < 2; ++k)
    {
        if (in.getNumBytesRemaining() < 4) break;
        const int bs = in.readInt();
        if (bs <= 0 || bs > in.getNumBytesRemaining()) continue;
        juce::MemoryBlock mb; in.readIntoMemoryBlock (mb, bs);
        audioFromFlac (mb.getData(), mb.getSize(), got[(size_t) k]);
    }
    applySamples (*xml, got);
    request(); ++version;
}

juce::AudioProcessorEditor* SiltProcessor::createEditor() { return new SiltEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SiltProcessor(); }
