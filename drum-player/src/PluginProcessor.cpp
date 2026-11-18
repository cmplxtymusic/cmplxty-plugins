#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"
#include "Patterns.h"

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
    // 1.1.0
    { "noteMap",  "Note Map",        'c', 0, 1, 1, 1, 0.0f, "", "GM / FPC|Chromatic" },
    { "pattern",  "Pattern",         'c', 0, 17, 1, 1, 0.0f, "", "House 4/4|Tech House|Techno Drive|Midtempo|Dubstep Half|Trap|Boom Bap|Breakbeat|Drum & Bass|Hip Hop Lazy|Rock Beat|Funk|Reggaeton|Afro House|Synthwave|Garage 2-Step|Hardstyle|Industrial" },
    { "fillStyle","Fill",            'c', 0, 8, 1, 1, 1.0f, "", "Off|Auto|Roll|Build|Toms|Triplets|Flams|Break|Stutter" },
    { "fillLen",  "Fill Length",     'c', 0, 3, 1, 1, 2.0f, "", "1 beat|2 beats|1 bar|2 bars" },
    { "dragLen",  "Loop Length",     'c', 0, 3, 1, 1, 2.0f, "", "1 bar|2 bars|4 bars|8 bars" },
    { "fillSeed", "Fill Variation",  'f', 1.0f, 999.0f, 1.0f, 1.0f, 1.0f, "" },
    { "patPlay",  "Play With Host",  'b', 0, 1, 1, 1, 0.0f, "" },
    // 1.2.0
    { "bpm",      "Tempo",           'f', 0.0f, 250.0f, 0.1f, 1.0f, 0.0f, "BPM-host" },
    { "softClip", "Soft Clip",       'b', 0, 1, 1, 1, 1.0f, "" },
    { "swing",    "Swing",           'f', 0.5f, 0.75f, 0.01f, 1.0f, 0.5f, "%" },
    { "fillDensity", "Fill Density", 'f', 0.1f, 1.0f, 0.01f, 1.0f, 1.0f, "%" },
    { "fillEnd",  "Fill Ending",     'c', 0, 3, 1, 1, 0.0f, "", "Crash|Ride|Open Hat|None" },
};
constexpr int kPerPad = 6, kVolume = 8 * kPerPad, kVelSens = kVolume + 1, kKitTune = kVolume + 2, kBaseNote = kVolume + 3, kNoteMap = kVolume + 4,
              kPattern = kVolume + 5, kFillStyle = kVolume + 6, kFillLen = kVolume + 7, kDragLen = kVolume + 8, kFillSeed = kVolume + 9, kPatPlay = kVolume + 10,
              kBpm = kVolume + 11, kSoftClip = kVolume + 12, kSwing = kVolume + 13,
              kFillDensity = kVolume + 14, kFillEnd = kVolume + 15, kNumParams = kVolume + 16;
static_assert (kPatPlay == DrumPlayerProcessor::kParamPatPlay && kBpm == DrumPlayerProcessor::kParamBpm && kSoftClip == DrumPlayerProcessor::kParamSoftClip, "header indices");
// pattern / loop settings are not part of a kit: loading a kit preset keeps them
bool notInKit (const juce::String& id) { for (int i = kNoteMap; i < kNumParams; ++i) if (id == kParams[i].id) return true; return false; }
juce::String safeName (juce::String s) { s = juce::File::createLegalFileName (s).trim(); return s.isEmpty() ? juce::String ("Untitled") : s; }
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
    presets.loadExtra = [this] (const juce::XmlElement* x) { readKit (x); snapshotKit ({}); };
    presets.keep = notInKit;
    clipEv.reserve (8192); pvClip.reserve (8192); pvEv.reserve (8192);
    snapshotKit ("Init");
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
    p.gm = (int) val (kNoteMap) == 0;
    p.softClip = val (kSoftClip) > 0.5f;
    return p;
}

void DrumPlayerProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr > 0 ? sr : 48000.0);
    rate.store (sr > 0 ? sr : 48000.0); lastClipKey = -1;
    pvPart = -1; pvPos = 0; pvN = 0; pvKey = -1; pvState.store (-1);
    engine.setParams (currentParams());
    scratchR.assign ((size_t) juce::jmax (block, 4096), 0.0f);
    setLatencySamples (0);
}

double DrumPlayerProcessor::getTailLengthSeconds() const { return 2.0; }


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
    if (const int w = audW.load (std::memory_order_acquire); audR != w)   // sound-swap audition: only the newest one plays
    {
        const auto a = aud[(size_t) ((w - 1) & 15)]; audR = w;
        engine.fadeSource (2, 0.01);
        if (a.pad >= 0) engine.triggerWith (a.pad, a.pp, 0.9f, 0.0f, 2);
    }
    if (nch <= 0 || n <= 0) { midi.clear(); return; }
    if ((int) scratchR.size() < n) { buffer.clear(); midi.clear(); return; }
    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : scratchR.data();

    // host tempo + position (1.1.0: the pattern follows the host BPM, the display shows it). 1.2.0: the last tempo is kept while the host
    // is stopped or sends none, so the preview free-runs at the project tempo.
    double ppq = 0, bpm = 0; bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm()) bpm = *t;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            hostPlaying = pos->getIsPlaying() && pos->getPpqPosition().hasValue() && bpm > 0;
        }
    if (bpm > 0 && bpm < 1000) hostBpm.store (bpm);
    hostRunning.store (hostPlaying);

    struct Hit { int at; int note; float vel; bool gm; int src; };
    std::array<Hit, 1024> hits; int nh = 0;
    const auto cs = clipSettings();
    const double sr = rate.load(), tp = tempo(), spb = 60.0 / tp * sr;

    // preview commands from the editor; the host transport starting or stopping ends the preview (no doubling with the song)
    if (const int cmd = pvCmd.exchange (0); cmd != 0)
    {
        if (cmd < 0) { if (pvPart >= 0) engine.fadeSource (1, 0.03); pvPart = -1; }
        else if (! (hostPlaying && val (kPatPlay) > 0.5f)) { if (pvPart < 0) pvPos = 0; pvPart = cmd - 1; pvKey = -1; }   // a new part keeps its place in the bar
    }
    if (pvPart >= 0 && hostPlaying != wasHostPlaying) { engine.fadeSource (1, 0.03); pvPart = -1; }
    wasHostPlaying = hostPlaying;
    pvState.store (pvPart);

    if (pvPart >= 0)
    {
        // events on whole samples of the loop, the same rounding as renderClip (llround (beat * samples per beat)), so the preview is the drag-out
        const juce::int64 N = std::max<juce::int64> (1, std::llround (cs.bars * 4.0 * spb));
        const juce::int64 key = clipKey (cs) * 8 + pvPart;
        if (key != pvKey || N != pvN || stepVer.load() != pvVer)
        {
            // a step edit rebuilds the events but keeps the place in the loop: sounding voices are untouched, the next hits follow the edit
            if (pvN > 0 && N != pvN) pvPos = (juce::int64) ((double) pvPos * (double) N / (double) pvN);   // tempo change: keep the place in the bar
            buildEvents (cs, pvClip, &pvVer); pvEv.clear();
            for (const auto& e : pvClip)
                if (const auto h = cw::dsp::gmMap (e.note); h.pad >= 0 && partHasPad (pvPart, h.pad))
                    pvEv.push_back ({ juce::jlimit<juce::int64> (0, N - 1, std::llround (e.beat * spb)), e.note, e.vel });
            pvKey = key; pvN = N;
        }
        if (pvPos >= N || pvPos < 0) pvPos = 0;
        for (int done = 0; done < n;)
        {
            const int chunk = (int) std::min<juce::int64> (n - done, N - pvPos);
            for (const auto& e : pvEv)
                if (e.at >= pvPos && e.at < pvPos + chunk && nh < (int) hits.size()) hits[(size_t) nh++] = { done + (int) (e.at - pvPos), e.note, e.vel, true, 1 };
            done += chunk; pvPos += chunk; if (pvPos >= N) pvPos = 0;
        }
        playBeat.store ((double) pvPos / spb);
    }
    else if (val (kPatPlay) > 0.5f && hostPlaying)   // PLAY WITH HOST: locked to the host's song position
    {
        const juce::int64 key = clipKey (cs);
        if (key != lastClipKey || stepVer.load() != clipVer) { buildEvents (cs, clipEv, &clipVer); lastClipKey = key; }
        const double beatsPerSample = 1.0 / spb, len = cs.bars * 4.0;
        for (int pass = 0; pass < 2; ++pass)   // the block may run past the clip end once
        {
            const double base = std::floor (ppq / len) * len + pass * len;
            for (const auto& e : clipEv)
            {
                const auto at = std::llround ((base + e.beat - ppq) / beatsPerSample);
                if (at >= 0 && at < n && nh < (int) hits.size()) hits[(size_t) nh++] = { (int) at, e.note, e.vel, true, 0 };
            }
        }
        playBeat.store (std::fmod (ppq, len));
    }
    else playBeat.store (-1.0);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (nh < (int) hits.size() && (m.isNoteOn() || m.isAllNotesOff() || m.isAllSoundOff()))
            hits[(size_t) nh++] = { juce::jlimit (0, n, meta.samplePosition), m.isNoteOn() ? m.getNoteNumber() : m.isAllNotesOff() ? -1 : -2, m.getFloatVelocity(), false, 0 };
    }
    for (int i = 1; i < nh; ++i) { const Hit h = hits[(size_t) i]; int j = i; while (j > 0 && h.at < hits[(size_t) (j - 1)].at) { hits[(size_t) j] = hits[(size_t) (j - 1)]; --j; } hits[(size_t) j] = h; }   // stable, no allocation (std::stable_sort may allocate)
    int pos = 0;
    for (int k = 0; k < nh; ++k)
    {
        const auto& h = hits[(size_t) k];
        const int at = juce::jlimit (pos, n, h.at);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        if (h.note == -1) engine.allNotesOff(); else if (h.note == -2) engine.allSoundOff();
        else if (h.gm) engine.noteOnGm (h.note, h.vel, h.src); else engine.noteOn (h.note, h.vel);
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
    // 1.2.0: edited patterns (only the edited ones; hex, one byte per step) and the kit the pads were swapped from
    auto* st = xml->createNewChildElement ("STEPS");
    for (int p = 0; p < kNumPatterns; ++p)
        if (patternEdited (p))
        {
            uint8_t g[cw::pat::kGrid]; getGrid (p, g);
            auto* e = st->createNewChildElement ("PAT"); e->setAttribute ("index", p); e->setAttribute ("data", juce::String::toHexString (g, cw::pat::kGrid, 0));
        }
    auto* kr = xml->createNewChildElement ("KITREF");
    kr->setAttribute ("name", kitRefName());
    for (int i = 0; i < cw::dsp::kPads; ++i)
    {
        const auto& q = kitRef[(size_t) i]; auto* e = kr->createNewChildElement ("PAD"); e->setAttribute ("index", i);
        e->setAttribute ("sound", q.sound); e->setAttribute ("tune", q.tune); e->setAttribute ("decay", q.decay); e->setAttribute ("pan", q.pan);
        e->setAttribute ("level", q.levelDb); e->setAttribute ("choke", q.choke);
    }
    copyXmlToBinary (*xml, dest);
}

void DrumPlayerProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    bool hasMap = false;   // sessions from 1.0 had no note map: they keep the chromatic map they were made with
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM")) if (e->getStringAttribute ("id") == "noteMap") hasMap = true;
    if (! hasMap) { auto* e = xml->createNewChildElement ("PARAM"); e->setAttribute ("id", "noteMap"); e->setAttribute ("value", 1.0); }
    bool hasClip = false;   // sessions from 1.0 / 1.1 had no soft clipper: they keep their exact sound (Soft Clip off)
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM")) if (e->getStringAttribute ("id") == "softClip") hasClip = true;
    if (! hasClip) { auto* e = xml->createNewChildElement ("PARAM"); e->setAttribute ("id", "softClip"); e->setAttribute ("value", 0.0); }
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    if (auto* kit = xml->getChildByName ("KIT")) { readKit (kit); xml->removeChildElement (kit, true); }
    else readKit (nullptr);
    // 1.2.0 step edits: a session without them (1.0 / 1.1 / the first 1.2 builds) plays the factory patterns, exactly as before
    stepVer.fetch_add (1);   // (no endStepWrite here: loading a session must not mark it as changed)
    for (auto& e : edited) e.store (false);
    if (auto* st = xml->getChildByName ("STEPS"))
    {
        for (auto* e : st->getChildWithTagNameIterator ("PAT"))
        {
            const int p = e->getIntAttribute ("index", -1);
            juce::MemoryBlock mb; mb.loadFromHexString (e->getStringAttribute ("data"));
            if (p < 0 || p >= kNumPatterns || (int) mb.getSize() != cw::pat::kGrid) continue;
            const auto* d = static_cast<const uint8_t*> (mb.getData());
            for (int k = 0; k < cw::pat::kGrid; ++k) grid[(size_t) (p * cw::pat::kGrid + k)].store ((uint8_t) std::min<int> (d[k], 100));
            edited[(size_t) p].store (true);
        }
        xml->removeChildElement (st, true);
    }
    stepVer.fetch_add (1);
    std::unique_ptr<juce::XmlElement> ref;
    if (auto* kr = xml->getChildByName ("KITREF")) { ref.reset (new juce::XmlElement (*kr)); xml->removeChildElement (kr, true); }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    if (ref != nullptr)
    {
        kitRefTitle = ref->getStringAttribute ("name", "Kit");
        for (auto* e : ref->getChildWithTagNameIterator ("PAD"))
            if (const int i = e->getIntAttribute ("index", -1); i >= 0 && i < cw::dsp::kPads)
            {
                auto& q = kitRef[(size_t) i];
                q.sound = juce::jlimit (0, cw::dsp::kSampleSound, e->getIntAttribute ("sound")); q.tune = (float) e->getDoubleAttribute ("tune");
                q.decay = (float) e->getDoubleAttribute ("decay", cw::dsp::kDecayFull); q.pan = (float) e->getDoubleAttribute ("pan");
                q.levelDb = (float) e->getDoubleAttribute ("level"); q.choke = juce::jlimit (0, cw::dsp::kChokeGroups, e->getIntAttribute ("choke"));
            }
    }
    else   // older sessions: the factory kit named in the session, else the pads as they are
    {
        const auto name = presets.currentName(); int k = -1;
        if (! (bool) apvts.state.getProperty ("presetUser", false)) for (int i = 0; i < numKits(); ++i) if (kitName (i) == name) k = i;
        if (k >= 0) { kitRefTitle = name; for (int i = 0; i < cw::dsp::kPads; ++i) kitRef[(size_t) i] = factoryPad (k, i); }
        else snapshotKit (name);
    }
}

// ---- 1.1.0 patterns / fills / drag-outs
cw::pat::Clip DrumPlayerProcessor::clipSettings() const
{
    static const int fillBeats[] = { 1, 2, 4, 8 }, bars[] = { 1, 2, 4, 8 };
    cw::pat::Clip c;
    c.pattern = juce::jlimit (0, (int) cw::pat::patterns().size() - 1, (int) val (kPattern));
    c.fill = juce::jlimit (0, cw::pat::kNumFill - 1, (int) val (kFillStyle));
    c.fillBeats = fillBeats[juce::jlimit (0, 3, (int) val (kFillLen))];
    c.bars = bars[juce::jlimit (0, 3, (int) val (kDragLen))];
    c.seed = (uint32_t) juce::jlimit (1, 999, juce::roundToInt (val (kFillSeed)));
    c.swing = juce::jlimit (0.5f, 0.75f, std::round (val (kSwing) * 100.0f) / 100.0f);
    c.density = juce::jlimit (0.1f, 1.0f, std::round (val (kFillDensity) * 100.0f) / 100.0f);
    c.ending = juce::jlimit (0, 3, (int) val (kFillEnd));
    return c;
}

juce::int64 DrumPlayerProcessor::clipKey (const cw::pat::Clip& c) const
{
    juce::int64 k = c.pattern;
    k = k * 16 + c.fill; k = k * 16 + c.fillBeats; k = k * 16 + c.bars; k = k * 1024 + (juce::int64) c.seed;
    k = k * 32 + juce::roundToInt (c.swing * 100.0f) - 50; k = k * 128 + juce::roundToInt (c.density * 100.0f); k = k * 4 + c.ending;
    return k;
}

// the clip with this pattern's step edits (any thread: no locks, no allocation beyond what `out` has reserved)
void DrumPlayerProcessor::buildEvents (cw::pat::Clip c, std::vector<cw::pat::Ev>& out, int* version) const
{
    uint8_t g[cw::pat::kGrid];
    const int v0 = stepVer.load (std::memory_order_acquire);
    const bool ed = patternEdited (c.pattern);
    if (ed) for (int k = 0; k < cw::pat::kGrid; ++k) g[k] = grid[(size_t) (c.pattern * cw::pat::kGrid + k)].load (std::memory_order_relaxed);
    c.steps = ed ? g : nullptr;
    cw::pat::build (c, out);
    // a write in progress (odd) or one that landed while copying: report a stale version so the caller rebuilds on its next pass
    if (version != nullptr) *version = ((v0 & 1) != 0 || stepVer.load (std::memory_order_acquire) != v0) ? -1 : v0;
}

// ---- 1.2.0 step editor (message thread)
void DrumPlayerProcessor::endStepWrite()
{
    stepVer.fetch_add (1);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));   // the host marks the project as changed
}

int DrumPlayerProcessor::stepAt (int p, int lane, int step) const
{
    if (p < 0 || p >= kNumPatterns || lane < 0 || lane >= cw::pat::kLanes || step < 0 || step >= cw::pat::kSteps) return 0;
    if (patternEdited (p)) return grid[(size_t) (p * cw::pat::kGrid + lane * cw::pat::kSteps + step)].load();
    uint8_t g[cw::pat::kGrid]; cw::pat::factoryGrid (p, g); return g[lane * cw::pat::kSteps + step];
}

void DrumPlayerProcessor::getGrid (int p, uint8_t* g) const
{
    if (p < 0 || p >= kNumPatterns) { std::fill (g, g + cw::pat::kGrid, (uint8_t) 0); return; }
    if (! patternEdited (p)) { cw::pat::factoryGrid (p, g); return; }
    for (int k = 0; k < cw::pat::kGrid; ++k) g[k] = grid[(size_t) (p * cw::pat::kGrid + k)].load();
}

void DrumPlayerProcessor::setGrid (int p, const uint8_t* g)
{
    if (p < 0 || p >= kNumPatterns) return;
    beginStepWrite();
    for (int k = 0; k < cw::pat::kGrid; ++k) grid[(size_t) (p * cw::pat::kGrid + k)].store ((uint8_t) std::min<int> (g[k], 100));
    edited[(size_t) p].store (true);
    endStepWrite();
}

void DrumPlayerProcessor::setStep (int p, int lane, int step, int vel)
{
    if (p < 0 || p >= kNumPatterns || lane < 0 || lane >= cw::pat::kLanes || step < 0 || step >= cw::pat::kSteps) return;
    uint8_t g[cw::pat::kGrid]; getGrid (p, g);
    g[lane * cw::pat::kSteps + step] = (uint8_t) juce::jlimit (0, 100, vel);
    setGrid (p, g);
}

void DrumPlayerProcessor::clearPattern (int p) { uint8_t g[cw::pat::kGrid] {}; setGrid (p, g); }
void DrumPlayerProcessor::resetPattern (int p)
{
    if (p < 0 || p >= kNumPatterns) return;
    beginStepWrite(); edited[(size_t) p].store (false); endStepWrite();
}

juce::String DrumPlayerProcessor::patternLabel() const
{
    const int p = clipSettings().pattern;
    return juce::String (cw::pat::patterns()[(size_t) p].name) + (patternEdited (p) ? " *" : "");
}

juce::File DrumPlayerProcessor::patternDir() { return cw::upd::dataDir().getChildFile ("Patterns"); }
juce::Array<juce::File> DrumPlayerProcessor::patternFiles()
{
    auto files = patternDir().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPatternExt);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files;
}

namespace { const char* const kPatternIds[] = { "swing", "fillStyle", "fillLen", "fillSeed", "fillDensity", "fillEnd" }; }

bool DrumPlayerProcessor::savePattern (const juce::File& f, const juce::String& name)
{
    const int p = clipSettings().pattern;
    juce::XmlElement x ("CMPLXTY_DRUM_PATTERN");
    x.setAttribute ("version", CW_VERSION_STRING); x.setAttribute ("name", name); x.setAttribute ("base", cw::pat::patterns()[(size_t) p].name);
    uint8_t g[cw::pat::kGrid]; getGrid (p, g);
    x.setAttribute ("steps", juce::String::toHexString (g, cw::pat::kGrid, 0));
    for (auto* id : kPatternIds)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        { auto* e = x.createNewChildElement ("PARAM"); e->setAttribute ("id", id); e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue())); }
    f.getParentDirectory().createDirectory();
    return x.writeTo (f);
}

bool DrumPlayerProcessor::loadPattern (const juce::File& f)
{
    if (! f.existsAsFile() || f.getSize() > 1024 * 1024) return false;
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("CMPLXTY_DRUM_PATTERN")) return false;
    juce::MemoryBlock mb; mb.loadFromHexString (x->getStringAttribute ("steps"));
    if ((int) mb.getSize() != cw::pat::kGrid) return false;
    undoManager.beginNewTransaction ("Load pattern");
    for (auto* e : x->getChildWithTagNameIterator ("PARAM"))
        for (auto* id : kPatternIds)
            if (e->getStringAttribute ("id") == id)
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                {
                    const auto v = (float) e->getDoubleAttribute ("value");
                    if (std::isfinite (v)) { r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (v)); r->endChangeGesture(); }
                }
    setGrid (clipSettings().pattern, static_cast<const uint8_t*> (mb.getData()));
    return true;
}

// ---- 1.2.0 sound swap (message thread)
int DrumPlayerProcessor::numKits() { return (int) cw::factoryPresets().size(); }
juce::String DrumPlayerProcessor::kitName (int k) { return k >= 0 && k < numKits() ? juce::String (cw::factoryPresets()[(size_t) k].name) : juce::String(); }

namespace
{
float kitValue (int kit, const char* id)   // a factory kit's value (real units): its override, else the default
{
    for (auto& v : cw::factoryPresets()[(size_t) kit].values) if (std::strcmp (v.first, id) == 0) return v.second;
    for (auto& d : kParams) if (std::strcmp (d.id, id) == 0) return d.def;
    return 0.0f;
}
}

cw::dsp::PadP DrumPlayerProcessor::factoryPad (int kit, int pad)
{
    cw::dsp::PadP q;
    if (kit < 0 || kit >= numKits() || pad < 0 || pad >= cw::dsp::kPads) return q;
    const int b = pad * kPerPad;
    q.sound = juce::jlimit (0, cw::dsp::kSampleSound, (int) kitValue (kit, kParams[b].id));
    q.tune = kitValue (kit, kParams[b + 1].id);
    q.decay = kitValue (kit, kParams[b + 2].id); q.pan = kitValue (kit, kParams[b + 3].id);
    q.levelDb = kitValue (kit, kParams[b + 4].id); q.choke = (int) kitValue (kit, kParams[b + 5].id);
    return q;
}

cw::dsp::PadP DrumPlayerProcessor::kitPad (int kit, int pad) const
{
    auto q = factoryPad (kit, pad);
    // keep the pitch it has in its own kit: its tune plus the difference between the two kits' KIT TUNE
    if (kit >= 0 && kit < numKits()) q.tune = juce::jlimit (-24.0f, 24.0f, q.tune + kitValue (kit, "kitTune") - val (kKitTune));
    return q;
}

void DrumPlayerProcessor::setPadParams (int pad, const cw::dsp::PadP& q, bool withPanChoke)
{
    const int b = pad * kPerPad;
    auto set = [this] (int i, float real)
    {
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (kParams[i].id)))
        { r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (real)); r->endChangeGesture(); }
    };
    set (b, (float) q.sound); set (b + 1, q.tune); set (b + 2, q.decay); set (b + 4, q.levelDb);
    if (withPanChoke) { set (b + 3, q.pan); set (b + 5, (float) q.choke); }
}

void DrumPlayerProcessor::swapFromKit (int pad, int kit, int srcPad)
{
    if (pad < 0 || pad >= cw::dsp::kPads || kit < 0 || kit >= numKits() || srcPad < 0 || srcPad >= cw::dsp::kPads) return;
    const auto q = kitPad (kit, srcPad);
    if (q.sound >= cw::dsp::kSounds) return;   // factory kits only use built-in sounds
    undoManager.beginNewTransaction ("Swap pad " + juce::String (pad + 1) + " sound");
    setPadParams (pad, q, false);
}

void DrumPlayerProcessor::swapBuiltIn (int pad, int sound)
{
    if (pad < 0 || pad >= cw::dsp::kPads || sound < 0 || sound >= cw::dsp::kSounds) return;
    undoManager.beginNewTransaction ("Swap pad " + juce::String (pad + 1) + " sound");
    cw::dsp::PadP q; q.sound = sound; q.levelDb = val (pad * kPerPad + 4);   // the plain sound, at the pad's level
    setPadParams (pad, q, false);
}

void DrumPlayerProcessor::resetPadToKit (int pad)
{
    if (pad < 0 || pad >= cw::dsp::kPads) return;
    undoManager.beginNewTransaction ("Reset pad " + juce::String (pad + 1) + " to kit");
    setPadParams (pad, kitRef[(size_t) pad], true);
}

cw::dsp::PadP DrumPlayerProcessor::kitRefPad (int pad) const { return pad >= 0 && pad < cw::dsp::kPads ? kitRef[(size_t) pad] : cw::dsp::PadP {}; }

bool DrumPlayerProcessor::padSwapped (int pad) const
{
    if (pad < 0 || pad >= cw::dsp::kPads) return false;
    const auto now = currentParams().pad[(size_t) pad]; const auto& r = kitRef[(size_t) pad];
    return now.sound != r.sound || std::abs (now.tune - r.tune) > 0.05f || std::abs (now.decay - r.decay) > 0.5f || std::abs (now.levelDb - r.levelDb) > 0.05f
        || std::abs (now.pan - r.pan) > 0.001f || now.choke != r.choke;
}

void DrumPlayerProcessor::snapshotKit (const juce::String& name)
{
    const auto P = currentParams();
    for (int i = 0; i < cw::dsp::kPads; ++i) kitRef[(size_t) i] = P.pad[(size_t) i];
    kitRefTitle = name;   // empty = the preset being loaded (its name is set right after loadExtra)
}

void DrumPlayerProcessor::audition (int pad, cw::dsp::PadP q)
{
    q.choke = 0;   // an audition never chokes what is playing
    const int w = audW.load();
    aud[(size_t) (w & 15)] = { q, pad };
    audW.store (w + 1, std::memory_order_release);
}

void DrumPlayerProcessor::newFill()
{
    if (auto* p = apvts.getParameter ("fillSeed"))
    {
        const int next = juce::roundToInt (val (kFillSeed)) % 999 + 1;
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) next)); p->endChangeGesture();
    }
}

juce::String DrumPlayerProcessor::clipTitle() const
{
    const auto c = clipSettings();
    juce::String t = "Drum Player - " + juce::String (cw::pat::patterns()[(size_t) c.pattern].name) + (patternEdited (c.pattern) ? " (edit)" : "") + " - " + juce::String (juce::roundToInt (tempo())) + " BPM - "
                   + juce::String (c.bars) + (c.bars == 1 ? " bar" : " bars");
    if (c.fill != cw::pat::kNoFill) t << " - " << cw::pat::fillName (c.fill) << " fill " << (int) c.seed;
    if (c.fill != cw::pat::kNoFill && c.density < 0.995f) t << " density " << juce::roundToInt (c.density * 100.0f);
    if (c.swing > 0.505f) t << " - swing " << juce::roundToInt (c.swing * 100.0f);
    return t;
}

const char* DrumPlayerProcessor::partName (int p)
{
    static const char* n[] = { "Full Drum Loop", "Kick Loop", "Snare Loop", "Top Loop", "Tom Loop" };
    return p >= 0 && p < kNumParts ? n[p] : "";
}

bool DrumPlayerProcessor::partHasPad (int part, int pad)
{
    switch (part)
    {
        case kPartFull:  return true;
        case kPartKick:  return pad == 0;
        case kPartSnare: return pad == 1 || pad == 2 || pad == 6;   // snare, clap, rim
        case kPartTop:   return pad == 3 || pad == 4 || pad == 7;   // hats, cymbals
        case kPartToms:  return pad == 5;
        default: return false;
    }
}

juce::File DrumPlayerProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("CMPLXTY Drum Player");
}

juce::File DrumPlayerProcessor::exportMidi (juce::String& err) const
{
    const auto c = clipSettings();
    const auto ev = clipEvents();
    if (ev.empty()) { err = "The pattern is empty"; return {}; }
    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Drum Player"), 0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / tempo())), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    for (const auto& e : ev)
    {
        const double t = std::round (e.beat * tpq);
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.vel * 127.0f));
        seq.addEvent (juce::MidiMessage::noteOn (10, e.note, vel), t);
        seq.addEvent (juce::MidiMessage::noteOff (10, e.note), t + tpq / 8);   // 32nd-note length: drums are one-shots
    }
    auto end = juce::MidiMessage::endOfTrack(); seq.addEvent (end, c.bars * 4.0 * tpq);
    seq.updateMatchedPairs(); seq.sort();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq); mf.addTrack (seq);
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const auto f = dir.getChildFile (safeName (clipTitle()) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk() || ! mf.writeTo (os, 1)) { err = "Can't write " + f.getFullPathName(); return {}; }
    return f;
}

void DrumPlayerProcessor::renderClip (int part, juce::AudioBuffer<float>& out, double sr)
{
    // 1.2.0: the clip is rendered several times in a row and the last pass is kept: it holds the tails of the pass before it, with the
    // chokes and voice limits applied the way they are when the loop repeats (1.1 added the tail on top, so an open hat ringing over the
    // loop point was not choked by the closed hat on beat 1). The preview plays exactly these samples from its second pass on.
    const auto c = clipSettings();
    const auto ev = clipEvents();
    const double spb = 60.0 / tempo() * sr;
    const int N = (int) std::max<juce::int64> (1, std::llround (c.bars * 4.0 * spb));
    cw::dsp::Engine e; e.prepare (sr);
    auto P = currentParams(); e.setParams (P);
    std::array<std::shared_ptr<const cw::dsp::Smp>, cw::dsp::kPads> hold;
    { const juce::SpinLock::ScopedLockType l (userLock); hold = user; }
    double tailSecs = 0.0;   // longest sound at its pitch (decay ignored): that much of the previous passes is heard
    for (int i = 0; i < cw::dsp::kPads; ++i)
    {
        e.setUser (i, hold[(size_t) i].get());
        const auto& pp = P.pad[(size_t) i];
        const cw::dsp::Smp* s = pp.sound >= cw::dsp::kSounds ? hold[(size_t) i].get() : &cw::dsp::builtIn (pp.sound);
        if (s != nullptr && s->len() > 1 && partHasPad (part, i)) tailSecs = std::max (tailSecs, s->len() / s->rate / std::pow (2.0, (double) (pp.tune + P.kitTune) / 12.0));
    }
    const int passes = 1 + juce::jlimit (1, 64, (int) std::ceil (std::min (tailSecs, 90.0) * sr / N));
    std::vector<float> L ((size_t) N), R ((size_t) N);
    for (int k = 0; k < passes; ++k)
    {
        int pos = 0;
        auto renderTo = [&] (int at) { if (at > pos) { e.render (L.data() + pos, R.data() + pos, at - pos); pos = at; } };
        for (const auto& x : ev)
        {
            const auto h = cw::dsp::gmMap (x.note);
            if (h.pad < 0 || ! partHasPad (part, h.pad)) continue;
            renderTo (juce::jlimit (0, N - 1, (int) std::llround (x.beat * spb)));
            e.trigger (h.pad, x.vel, h.st, 1);
        }
        renderTo (N);
    }
    out.setSize (2, N); out.clear();
    out.copyFrom (0, 0, L.data(), N); out.copyFrom (1, 0, R.data(), N);
}

juce::File DrumPlayerProcessor::exportPart (int part, juce::String& err)
{
    if (part < 0 || part >= kNumParts) { err = "No such part"; return {}; }
    const double sr = rate.load() > 0 ? rate.load() : 48000.0;
    juce::AudioBuffer<float> b;
    renderClip (part, b, sr);
    if (b.getMagnitude (0, b.getNumSamples()) <= 0.0f) { err = juce::String ("The loop has no ") + partName (part) + " hits"; return {}; }
    const auto dir = exportFolder();
    if (! dir.createDirectory()) { err = "Can't create " + dir.getFullPathName(); return {}; }
    const auto f = dir.getChildFile (safeName (clipTitle() + " - " + partName (part)) + ".wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) { err = "Can't write " + f.getFullPathName(); return {}; }
    juce::WavAudioFormat wav;
    // 24-bit as before; a loop that peaks over 0 dBFS (Soft Clip off) is written as 32-bit float so its peaks are not cut off
    const int bits = b.getMagnitude (0, b.getNumSamples()) > 1.0f ? 32 : 24;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, bits, {}, 0));
    if (w == nullptr) { err = "Can't write a WAV at " + juce::String (sr) + " Hz"; return {}; }
    os.release();
    if (! w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples())) { err = "Write failed: " + f.getFullPathName(); return {}; }
    return f;
}

juce::AudioProcessorEditor* DrumPlayerProcessor::createEditor() { return new DrumPlayerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumPlayerProcessor(); }
