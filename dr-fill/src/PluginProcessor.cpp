#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
// every one is in factory and user presets except bypass, tempo and hit sensitivity (they belong to the track / the loops)
const drf::PDef kParams[] = {
    { "length",   "Length",       'c', 0, 3, 1, 1, 2.0f, "", "1 Beat|2 Beats|1 Bar|2 Bars" },
    { "style",    "Fill Style",   'c', 0, 7, 1, 1, 0.0f, "", "Auto|Snare Roll|Build-Up|Tom Run|Triplets|Flams|Break Chop|Stutter" },
    { "density",  "Density",      'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.55f, "%" },
    { "complex",  "Complexity",   'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.4f, "%" },
    { "swing",    "Swing",        'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.0f, "%" },
    { "humanize", "Humanize",     'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.1f, "%" },
    { "ramp",     "Ramp",         'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.6f, "%" },
    { "flam",     "Flams",        'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.2f, "%" },
    { "pitch",    "Pitch Glide",  'f', -12.0f, 12.0f, 0.1f, 1.0f, 0.0f, "st" },
    { "kick",     "Kick Amount",  'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%" },
    { "hats",     "Hats Amount",  'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.35f, "%" },
    { "ending",   "Ending",       'c', 0, 2, 1, 1, 2.0f, "", "Off|Open Hat|Crash" },
    { "level",    "Level",        'f', -24.0f, 6.0f, 0.1f, 1.0f, 0.0f, "dB" },
    { "seed",     "Seed",         'f', 1.0f, 9999.0f, 1.0f, 1.0f, 1.0f, "" },
    { "bpm",      "Tempo",        'f', 0.0f, 250.0f, 0.1f, 1.0f, 0.0f, "BPM" },
    { "sens",     "Hit Sensitivity", 'f', 0.0f, 1.0f, 0.01f, 1.0f, 0.5f, "%" },
    { "bypass",   "Bypass",       'b', 0, 1, 1, 1, 0.0f, "" },
};
const int kLengthBeats[] = { 1, 2, 4, 8 };
const char* kLengthName[] = { "1 Beat", "2 Beats", "1 Bar", "2 Bars" };

juce::String safeName (juce::String s)
{
    s = juce::File::createLegalFileName (s).trim();
    return s.isEmpty() ? juce::String ("Untitled") : s;
}

bool readAudio (const juce::File& f, drf::Audio& a, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate < 8000.0) { err = "Can't read " + f.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)"; return false; }
    if ((double) r->lengthInSamples / r->sampleRate > DrFillProcessor::kMaxLoopSeconds) { err = f.getFileName() + " is longer than 60 seconds - use a loop or a one-shot"; return false; }
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
} // namespace

struct DrFillWorker final : juce::Thread
{
    explicit DrFillWorker (DrFillProcessor& p) : juce::Thread ("CMPLXTY Dr Fill worker"), proc (p) {}
    void run() override { proc.workerLoop(); }
    DrFillProcessor& proc;
};

const char* DrFillProcessor::partName (int p)
{
    static const char* n[] = { "Kick", "Snare", "Hats", "Full Fill" };
    return (p >= 0 && p < kNumParts) ? n[p] : "?";
}

DrFillProcessor::DrFillProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", drf::makeLayout (kParams))
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    presets.keepIds.addIfNotAlreadyThere ("bypass");   // presets never bypass / un-bypass
    presets.keepIds.addIfNotAlreadyThere ("bpm");      // nor change the tempo or how the loops are sliced
    presets.keepIds.addIfNotAlreadyThere ("sens");
    for (auto& d : kParams) if (juce::String (d.id) != "bypass") apvts.addParameterListener (d.id, this);
    kitPub = std::make_shared<drf::Kit>();
    renderPub = std::make_shared<drf::Render>();
    keepAlive.push_back (renderPub);
    setMessage ("Drop kick, snare and hat loops in the boxes (or several files anywhere)");
    worker = std::make_unique<DrFillWorker> (*this);
    worker->startThread();
}

DrFillProcessor::~DrFillProcessor()
{
    for (auto& d : kParams) if (juce::String (d.id) != "bypass") apvts.removeParameterListener (d.id, this);
    quit.store (true); wake.signal();
    if (worker) worker->stopThread (5000);
}

bool DrFillProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto& in = l.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

void DrFillProcessor::prepareToPlay (double sr, int maxBlock)
{
    rate.store (sr);
    bypass.prepare (sr, maxBlock, juce::jmax (1, getTotalNumOutputChannels()), 0);
    if (bypassParam) bypass.snap (bypassParam->get());
    voice = {}; tail = {};
    requestRender();
}

void DrFillProcessor::setMessage (const juce::String& m) { const juce::SpinLock::ScopedLockType l (msgLock); message = m; }

// ------------------------------------------------------------------------------------------------ audio thread
void DrFillProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = b.getNumSamples(), nOut = getTotalNumOutputChannels();
    for (int c = getTotalNumInputChannels(); c < nOut; ++c) b.clear (c, 0, n);
    bypass.pushDry (b);   // the dry path = the track's own audio

    // host tempo + position
    double ppq = 0, bpm = 0, lastBar = 0, barLen = 4; bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm()) bpm = *t;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            if (auto lb = pos->getPpqPositionOfLastBarStart()) lastBar = *lb; else lastBar = ppq;
            if (auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) barLen = ts->numerator * 4.0 / ts->denominator;
            hostPlaying = pos->getIsPlaying() && pos->getPpqPosition().hasValue() && bpm > 0;
        }
    hostBpm.store (bpm > 0 && bpm < 1000 ? bpm : 0.0);

    // newest render (never freed here: keepAlive holds every published render until nothing else does)
    {
        const juce::SpinLock::ScopedTryLockType l (pubLock);
        if (l.isLocked() && renderPub != playing && ! voice.on && ! tail.on) playing = renderPub;
    }
    auto trigger = [&] (int part, int at, float gain)
    {
        {
            const juce::SpinLock::ScopedTryLockType l (pubLock);
            if (l.isLocked() && renderPub != playing)
            {
                if (voice.on) { playingOld = playing; tail = voice; tail.r = playingOld.get(); }
                playing = renderPub;
            }
            else if (voice.on) { tail = voice; }
        }
        if (tail.on) tail.fadeStep = 1.0f / (float) juce::jmax (16.0, 0.005 * getSampleRate());   // 5 ms fade of the one cut off
        voice = {}; voice.r = playing.get(); voice.part = part; voice.startIn = at; voice.fade = gain; voice.on = voice.r != nullptr && voice.r->total > 0;
    };

    const int pr = previewReq.exchange (-1);
    if (stopReq.exchange (false)) { if (voice.on) { tail = voice; tail.r = voice.r; playingOld = playing; tail.fadeStep = 1.0f / (float) juce::jmax (16.0, 0.005 * getSampleRate()); } voice.on = false; }
    if (pr >= 0)
    {
        int at = 0;
        if (hostPlaying)
        {
            // line the fill up so it ENDS on a bar line (the next one it fits before)
            const double Lb = playing ? (double) playing->fillLen / juce::jmax (1.0, playing->rate) * playing->bpm / 60.0 : 4.0;
            const double k = std::ceil ((ppq + Lb - lastBar) / barLen - 1e-9);
            const double start = lastBar + k * barLen - Lb;
            const double delaySec = (start - ppq) * 60.0 / bpm;
            at = (int) juce::jlimit (0.0, 30.0 * getSampleRate(), std::round (delaySec * getSampleRate()));
        }
        trigger (pr, at, 1.0f);
    }
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOn()) continue;
        const int note = msg.getNoteNumber();
        const int part = note == 36 ? kPartKick : note == 38 ? kPartSnare : note == 42 ? kPartHats : kPartFull;
        trigger (part, juce::jlimit (0, n - 1, m.samplePosition), msg.getFloatVelocity());
    }

    auto play = [&] (Voice& v, bool fadingOut)
    {
        if (! v.on || v.r == nullptr) return;
        const auto& r = *v.r;
        int i0 = 0;
        if (v.startIn > 0) { const int skip = juce::jmin (n, v.startIn); v.startIn -= skip; i0 = skip; if (i0 >= n) return; }
        for (int i = i0; i < n; ++i)
        {
            if (v.pos >= r.total) { v.on = false; break; }
            float l = 0, rr = 0;
            const size_t k = (size_t) v.pos;
            if (v.part == kPartFull) for (int lane = 0; lane < drf::kLanes; ++lane) { l += r.L[(size_t) lane][k]; rr += r.R[(size_t) lane][k]; }
            else { l = r.L[(size_t) v.part][k]; rr = r.R[(size_t) v.part][k]; }
            float g = v.fade;
            if (fadingOut) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.on = false; break; } }
            if (nOut >= 2) { b.addSample (0, i, g * l); b.addSample (1, i, g * rr); }
            else if (nOut == 1) b.addSample (0, i, 0.5f * g * (l + rr));
            ++v.pos;
        }
    };
    play (tail, true);
    play (voice, false);
    if (! tail.on) playingOld.reset();   // still in keepAlive: no free here
    playingNow.store (voice.on);
    playPos.store (voice.on && voice.r && voice.r->fillLen > 0 && voice.startIn == 0 ? juce::jmin (1.0f, (float) voice.pos / (float) voice.r->fillLen) : -1.0f);
    midi.clear();
    bypass.mix (b, bypassParam != nullptr && bypassParam->get());
}

void DrFillProcessor::preview (int part) { previewReq.store (juce::jlimit (0, (int) kNumParts - 1, part)); }

// ------------------------------------------------------------------------------------------------ loops
int DrFillProcessor::laneFromFileName (const juce::String& fileName)
{
    const auto n = " " + fileName.toLowerCase().replaceCharacters ("_-.()[]", "       ") + " ";
    auto word = [&n] (const char* w) { return n.contains (" " + juce::String (w) + " ") || n.contains (" " + juce::String (w)) ; };
    for (auto* k : { "hat", "hh", "hihat", "cym", "crash", "ride", "ohh", "chh", "top", "shaker", "perc" }) if (n.contains (k) && (juce::String (k).length() > 2 || word (k))) return drf::kHats;
    for (auto* k : { "snare", "snr", "clap", "rim" }) if (n.contains (k)) return drf::kSnare;
    if (word ("sd") || word ("sn") || word ("cp")) return drf::kSnare;
    for (auto* k : { "kick", "kik", "kck", "bassdrum", "bass drum" }) if (n.contains (k)) return drf::kKick;
    if (word ("bd") || word ("kd") || word ("808")) return drf::kKick;
    return -1;
}

std::vector<int> DrFillProcessor::assignLanes (const juce::StringArray& files, const std::array<bool, 3>& occupied, int preferred)
{
    std::vector<int> out ((size_t) files.size(), -1);
    std::array<bool, 3> taken {};
    for (int i = 0; i < files.size(); ++i)
    {
        const int l = laneFromFileName (juce::File (files[i]).getFileNameWithoutExtension());
        if (l >= 0 && ! taken[(size_t) l]) { out[(size_t) i] = l; taken[(size_t) l] = true; }
    }
    if (preferred >= 0 && preferred < 3 && ! taken[(size_t) preferred])   // dropped on one box: the first file whose name says nothing goes there
        for (int i = 0; i < files.size(); ++i)
            if (out[(size_t) i] < 0) { out[(size_t) i] = preferred; taken[(size_t) preferred] = true; break; }
    for (int pass = 0; pass < 2; ++pass)   // empty boxes first, then ones that hold an older loop
        for (int i = 0; i < files.size(); ++i)
        {
            if (out[(size_t) i] >= 0) continue;
            for (int l = 0; l < 3; ++l) if (! taken[(size_t) l] && (pass == 1 || ! occupied[(size_t) l])) { out[(size_t) i] = l; taken[(size_t) l] = true; break; }
        }
    return out;
}

juce::String DrFillProcessor::loadLoop (int lane, const juce::File& f)
{
    if (lane < 0 || lane >= drf::kLanes) return "bad lane";
    if (! f.existsAsFile()) return "Can't find " + f.getFileName();
    drf::Audio a; juce::String err;
    if (! readAudio (f, a, err)) return err;
    return loadLoopAudio (lane, std::move (a), f.getFileNameWithoutExtension());
}

juce::String DrFillProcessor::loadLoopAudio (int lane, drf::Audio a, const juce::String& name)
{
    if (lane < 0 || lane >= drf::kLanes) return "bad lane";
    if (a.len() == 0) return name + " is empty";
    {
        const juce::ScopedLock l (inLock);
        loops[(size_t) lane] = std::move (a); names[(size_t) lane] = name; ++inputVersion;
    }
    setMessage (juce::String (lane == drf::kKick ? "Kicks" : lane == drf::kSnare ? "Snares" : "Hats / cymbals") + ": " + name);
    requestRender();
    ++version;
    return {};
}

void DrFillProcessor::clearLoop (int lane)
{
    if (lane < 0 || lane >= drf::kLanes) return;
    { const juce::ScopedLock l (inLock); loops[(size_t) lane] = {}; names[(size_t) lane] = {}; ++inputVersion; }
    requestRender(); ++version;
}

void DrFillProcessor::clearAll()
{
    { const juce::ScopedLock l (inLock); for (auto& a : loops) a = {}; for (auto& s : names) s = {}; ++inputVersion; }
    setMessage ("Drop kick, snare and hat loops in the boxes (or several files anywhere)");
    requestRender(); ++version;
}

juce::String DrFillProcessor::loopName (int lane) const { const juce::ScopedLock l (inLock); return lane >= 0 && lane < drf::kLanes ? names[(size_t) lane] : juce::String(); }
bool DrFillProcessor::hasLoop (int lane) const { const juce::ScopedLock l (inLock); return lane >= 0 && lane < drf::kLanes && loops[(size_t) lane].len() > 0; }

// ------------------------------------------------------------------------------------------------ worker: analysis + fill
drf::FillSettings DrFillProcessor::currentSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    drf::FillSettings s;
    s.lengthBeats = kLengthBeats[juce::jlimit (0, 3, (int) v ("length"))];
    s.style = juce::jlimit (0, (int) drf::kNumStyles - 1, (int) v ("style"));
    s.density = v ("density"); s.complexity = v ("complex"); s.swing = v ("swing"); s.humanize = v ("humanize"); s.ramp = v ("ramp");
    s.flam = v ("flam"); s.pitch = v ("pitch"); s.kick = v ("kick"); s.hats = v ("hats");
    s.ending = juce::jlimit (0, 2, (int) v ("ending"));
    s.level = juce::Decibels::decibelsToGain (v ("level"));
    s.seed = (uint32_t) juce::jlimit (1, 9999, juce::roundToInt (v ("seed")));
    return s;
}

double DrFillProcessor::tempoFor (const drf::TempoResult& t) const
{
    const double knob = apvts.getRawParameterValue ("bpm")->load();
    if (knob >= 20.0) return knob;
    if (hostBpm.load() > 0) return hostBpm.load();
    return t.bpm > 0 ? t.bpm : 120.0;
}
double DrFillProcessor::fillTempo() const { return tempoFor (detectedTempo()); }
juce::String DrFillProcessor::tempoSource() const
{
    if (apvts.getRawParameterValue ("bpm")->load() >= 20.0) return "TEMPO knob";
    if (hostBpm.load() > 0) return "host";
    const auto t = detectedTempo();
    return t.bpm > 0 ? (t.fromName ? "file name" : "loops") : "default";
}

void DrFillProcessor::regenerate()
{
    auto* p = apvts.getParameter ("seed");
    const int cur = juce::roundToInt (apvts.getRawParameterValue ("seed")->load());
    int s = cur; juce::Random r; while (s == cur) s = 1 + r.nextInt (9999);
    p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) s)); p->endChangeGesture();
}

void DrFillProcessor::workerLoop()
{
    std::shared_ptr<drf::Kit> analysed = std::make_shared<drf::Kit>();
    int doneInput = -1; float doneSens = -1;
    while (! quit.load() && ! juce::Thread::currentThreadShouldExit())
    {
        wake.wait (100);
        if (quit.load()) break;
        // host tempo / rate changes are picked up here (no signalling from the audio thread)
        if (! pending.load())
        {
            const double want = tempoFor (detectedTempo());
            if (renderedBpm.load() > 0 && (std::abs (want - renderedBpm.load()) > 1e-3 || rate.load() != renderedRate.load())) pending.store (true);
        }
        if (! pending.exchange (false)) { publishGarbage(); continue; }
        working.store (true);
        const float sens = apvts.getRawParameterValue ("sens")->load();
        int iv; { const juce::ScopedLock l (inLock); iv = inputVersion; }
        if (iv != doneInput || sens != doneSens)
        {
            auto k = std::make_shared<drf::Kit>();
            {
                const juce::ScopedLock l (inLock);
                for (int lane = 0; lane < drf::kLanes; ++lane) { (*k)[(size_t) lane].audio = loops[(size_t) lane]; (*k)[(size_t) lane].name = names[(size_t) lane].toStdString(); }
            }
            for (int lane = 0; lane < drf::kLanes; ++lane) drf::analyse ((*k)[(size_t) lane], lane, sens);
            const auto t = drf::estimateTempo (*k);
            analysed = k; doneInput = iv; doneSens = sens;
            const juce::SpinLock::ScopedLockType l (pubLock);
            kitPub = analysed; tempoPub = t;
        }
        const auto s = currentSettings();
        const double bpm = tempoFor (detectedTempo()), sr = rate.load();
        const auto f = drf::generate (*analysed, s);
        auto r = std::make_shared<drf::Render> (drf::render (*analysed, f, bpm, sr, s));
        {
            const juce::SpinLock::ScopedLockType l (pubLock);
            renderPub = r; fillPub = f; keepAlive.push_back (r);
        }
        renderedBpm.store (bpm); renderedRate.store (sr);
        publishGarbage();
        working.store (false);
        ++version;
    }
}

// renders nobody plays any more are freed here (the audio thread only ever drops references, it never holds the last one)
void DrFillProcessor::publishGarbage()
{
    std::vector<std::shared_ptr<const drf::Render>> dead;
    {
        const juce::SpinLock::ScopedLockType l (pubLock);
        for (size_t i = 0; i < keepAlive.size();)
            if (keepAlive[i] != renderPub && keepAlive[i].use_count() == 1) { dead.push_back (std::move (keepAlive[i])); keepAlive.erase (keepAlive.begin() + (long) i); }
            else ++i;
    }
}   // freed here, outside the lock

bool DrFillProcessor::waitIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    juce::Thread::sleep (150);   // the worker polls the host tempo every 100 ms
    while ((pending.load() || working.load()) && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (5);
    return ! pending.load() && ! working.load();
}

// ------------------------------------------------------------------------------------------------ export
juce::File DrFillProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("CMPLXTY Dr Fill");
}

juce::String DrFillProcessor::fillTitle() const
{
    const auto f = fill(); const auto r = render();
    const int li = f.lengthBeats <= 1 ? 0 : f.lengthBeats <= 2 ? 1 : f.lengthBeats <= 4 ? 2 : 3;
    return "Dr Fill - " + juce::String (drf::styleName (f.style)) + " - " + kLengthName[li] + " - " + juce::String (juce::roundToInt (r->bpm)) + " BPM - s"
           + juce::String (juce::roundToInt (apvts.getRawParameterValue ("seed")->load()));
}

juce::File DrFillProcessor::exportPart (int part, juce::String& err)
{
    const auto r = render();
    if (part < 0 || part >= kNumParts) { err = "No such part"; return {}; }
    if (r == nullptr || r->events == 0 || r->total <= 0) { err = "Nothing to save yet - drop some loops first"; return {}; }
    bool any = false;
    juce::AudioBuffer<float> b (2, r->total);
    for (int k = 0; k < r->total; ++k)
    {
        float l = 0, rr = 0;
        for (int lane = 0; lane < drf::kLanes; ++lane)
            if (part == kPartFull || part == lane) { l += r->L[(size_t) lane][(size_t) k]; rr += r->R[(size_t) lane][(size_t) k]; }
        b.setSample (0, k, l); b.setSample (1, k, rr); any = any || l != 0.0f || rr != 0.0f;
    }
    if (! any) { err = juce::String ("The fill has no ") + partName (part) + " hits"; return {}; }
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const auto f = dir.getChildFile (safeName (fillTitle() + " - " + partName (part)) + ".wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) { err = "Can't write " + f.getFullPathName(); return {}; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), r->rate, 2, 24, {}, 0));
    if (w == nullptr) { err = "Can't write a WAV at " + juce::String (r->rate) + " Hz"; return {}; }
    os.release();
    if (! w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { err = "Write failed: " + f.getFullPathName(); return {}; }
    w.reset();
    return f;
}

juce::StringArray DrFillProcessor::exportAll (juce::String& err)
{
    juce::StringArray out;
    for (int p = kNumParts - 1; p >= 0; --p) { juce::String e; auto f = exportPart (p, e); if (f.existsAsFile()) out.add (f.getFullPathName()); else if (p == kPartFull) err = e; }
    return out;
}

// ------------------------------------------------------------------------------------------------ state
// "DRF1" + int + XML (parameters, loop names, UI size) + per loop: int size + FLAC (so a reopened project keeps its loops even if the files moved)
void DrFillProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("DrFillState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml.setAttribute ("uiW", uiWidth.load()); xml.setAttribute ("uiH", uiHeight.load()); }
    std::array<juce::MemoryBlock, drf::kLanes> bl;
    {
        const juce::ScopedLock l (inLock);
        const juce::ScopedLock bl2 (blobLock);
        for (int lane = 0; lane < drf::kLanes; ++lane) xml.setAttribute ("name" + juce::String (lane), names[(size_t) lane]);
        if (blobVersion != inputVersion)
        {
            for (int lane = 0; lane < drf::kLanes; ++lane)
            {
                auto& mb = blobs[(size_t) lane]; mb.reset();
                const auto& a = loops[(size_t) lane];
                if (a.len() == 0) continue;
                const int ch = a.stereo() ? 2 : 1;
                juce::AudioBuffer<float> b (ch, a.len());
                b.copyFrom (0, 0, a.L.data(), a.len()); if (ch > 1) b.copyFrom (1, 0, a.R.data(), a.len());
                juce::FlacAudioFormat flac;
                auto* os = new juce::MemoryOutputStream (mb, false);
                std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, a.rate, (unsigned) ch, 24, {}, 0));
                if (w == nullptr) { delete os; mb.reset(); continue; }
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            blobVersion = inputVersion;
        }
        bl = blobs;
    }
    const auto text = xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    juce::MemoryOutputStream out (dest, false);
    out.write ("DRF1", 4);
    out.writeInt ((int) text.getNumBytesAsUTF8());
    out.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
    for (auto& mb : bl) { out.writeInt ((int) mb.getSize()); if (mb.getSize() > 0) out.write (mb.getData(), mb.getSize()); }
}

void DrFillProcessor::setStateInformation (const void* data, int size)
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
    if (std::memcmp (data, "DRF1", 4) != 0)
    {   // plain JUCE binary XML (parameters only, no loops): foreign / hand-made sessions
        auto x = getXmlFromBinary (data, size);
        if (x != nullptr && x->hasTagName ("DrFillState")) { applyParams (*x); requestRender(); ++version; }
        return;
    }
    juce::MemoryInputStream in (data, (size_t) size, false);
    in.skipNextBytes (4);
    const int xb = in.readInt();
    if (xb <= 0 || xb > size - 8) return;
    juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, xb);
    auto xml = juce::parseXML (xm.toString());
    if (xml == nullptr || ! xml->hasTagName ("DrFillState")) return;
    applyParams (*xml);
    std::array<drf::Audio, drf::kLanes> got;
    for (int lane = 0; lane < drf::kLanes; ++lane)
    {
        if (in.getNumBytesRemaining() < 4) break;
        const int bs = in.readInt();
        if (bs <= 0 || bs > in.getNumBytesRemaining()) continue;
        juce::MemoryBlock mb; in.readIntoMemoryBlock (mb, bs);
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
        if (r == nullptr || r->lengthInSamples <= 0 || (double) r->lengthInSamples / r->sampleRate > kMaxLoopSeconds + 1.0) continue;
        const int n = (int) r->lengthInSamples, ch = juce::jmin (2, (int) r->numChannels);
        juce::AudioBuffer<float> b (juce::jmax (1, ch), n);
        r->read (&b, 0, n, 0, true, ch > 1);
        auto& a = got[(size_t) lane]; a.rate = r->sampleRate;
        a.L.assign (b.getReadPointer (0), b.getReadPointer (0) + n);
        if (ch > 1) a.R.assign (b.getReadPointer (1), b.getReadPointer (1) + n);
    }
    {
        const juce::ScopedLock l (inLock);
        for (int lane = 0; lane < drf::kLanes; ++lane)
        {
            loops[(size_t) lane] = std::move (got[(size_t) lane]);
            names[(size_t) lane] = loops[(size_t) lane].len() > 0 ? xml->getStringAttribute ("name" + juce::String (lane)) : juce::String();
        }
        ++inputVersion;
    }
    if (hasAnyLoop()) setMessage ("Loops restored with the project");
    requestRender(); ++version;
}

juce::AudioProcessorEditor* DrFillProcessor::createEditor() { return new DrFillEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrFillProcessor(); }
