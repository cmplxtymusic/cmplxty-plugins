#include "PluginProcessor.h"
#include <cstring>
#include "PluginEditor.h"

namespace
{
juce::String safeName(juce::String s)
{
    s = s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    return s.trim().isEmpty() ? "SMOTHER" : s;
}

void writeLane(juce::ValueTree t, const smother::Lane &ln)
{
    t.setProperty("enabled", ln.enabled, nullptr);
    t.setProperty("mute", ln.mute, nullptr);
    t.setProperty("solo", ln.solo, nullptr);
    t.setProperty("double", ln.doubleLen, nullptr);
    t.setProperty("length", ln.length, nullptr);
    t.setProperty("rate", (int)ln.rate, nullptr);
    t.setProperty("drum", (int)ln.drum, nullptr);
    t.setProperty("midi", ln.midiNote, nullptr);
    t.setProperty("kit", ln.kit, nullptr);
    t.setProperty("tune", ln.tune, nullptr);
    t.setProperty("decay", ln.decay, nullptr);
    t.setProperty("tone", ln.tone, nullptr);
    t.setProperty("pan", ln.pan, nullptr);
    t.setProperty("level", ln.level, nullptr);
    t.setProperty("env", (int)ln.env, nullptr);
    t.setProperty("eqlow", ln.eqLow, nullptr);
    t.setProperty("eqmid", ln.eqMid, nullptr);
    t.setProperty("eqhigh", ln.eqHigh, nullptr);
    t.setProperty("hp", ln.hp, nullptr);
    t.setProperty("lp", ln.lp, nullptr);
    t.setProperty("drive", ln.drive, nullptr);
    t.setProperty("crush", ln.crush, nullptr);
    t.setProperty("transient", ln.transient, nullptr);
    t.setProperty("ott", ln.ott, nullptr);
    t.setProperty("chop0", ln.chopStart, nullptr);
    t.setProperty("chop1", ln.chopEnd, nullptr);
    t.setProperty("username", juce::String(ln.userName), nullptr);
    juce::String v;
    for (int i = 0; i < smother::maxSteps; ++i)
    {
        if (i)
            v += ",";
        v += juce::String(ln.vel[(size_t)i], 3);
    }
    t.setProperty("vel", v, nullptr);
    for (int i = 0; i < ln.sleeveCount; ++i)
    {
        const auto &s = ln.sleeves[(size_t)i];
        juce::ValueTree sv("SLEEVE");
        sv.setProperty("start", s.start, nullptr);
        sv.setProperty("length", s.length, nullptr);
        sv.setProperty("type", (int)s.type, nullptr);
        sv.setProperty("att", s.attackMs, nullptr);
        sv.setProperty("rel", s.releaseMs, nullptr);
        sv.setProperty("depth", s.depth, nullptr);
        sv.setProperty("curve", s.curve, nullptr);
        t.appendChild(sv, nullptr);
    }
    if (ln.hasUser && !ln.user.empty() && ln.user.size() < 48000 * 8)
    {
        juce::MemoryBlock mb(ln.user.size() * sizeof(float));
        std::memcpy(mb.getData(), ln.user.data(), mb.getSize());
        t.setProperty("sample", mb.toBase64Encoding(), nullptr);
        t.setProperty("samplesr", ln.userSr, nullptr);
    }
}

void writeSleeveList(juce::ValueTree parent, const smother::Sleeve *list, int n, const char *tag)
{
    for (int i = 0; i < n; ++i)
    {
        const auto &s = list[i];
        juce::ValueTree sv(tag);
        sv.setProperty("start", s.start, nullptr);
        sv.setProperty("length", s.length, nullptr);
        sv.setProperty("type", (int)s.type, nullptr);
        sv.setProperty("att", s.attackMs, nullptr);
        sv.setProperty("rel", s.releaseMs, nullptr);
        sv.setProperty("depth", s.depth, nullptr);
        sv.setProperty("curve", s.curve, nullptr);
        parent.appendChild(sv, nullptr);
    }
}

smother::Sleeve readSleeve(const juce::ValueTree &sv)
{
    smother::Sleeve s;
    s.start = (int)sv.getProperty("start", 0);
    s.length = (int)sv.getProperty("length", 4);
    s.type = (smother::SleeveType)smother::clampi((int)sv.getProperty("type", 0), 0,
                                                  smother::sleeveTypeCount - 1);
    s.attackMs = (float)sv.getProperty("att", 20);
    s.releaseMs = (float)sv.getProperty("rel", 40);
    s.depth = (float)sv.getProperty("depth", 1);
    s.curve = (float)sv.getProperty("curve", 0.35);
    return s;
}

smother::Lane readLane(const juce::ValueTree &t)
{
    smother::Lane ln;
    ln.enabled = (bool)t.getProperty("enabled", true);
    ln.mute = (bool)t.getProperty("mute", false);
    ln.solo = (bool)t.getProperty("solo", false);
    ln.doubleLen = (bool)t.getProperty("double", false);
    ln.length = smother::clampi((int)t.getProperty("length", 16), 1, smother::maxSteps);
    ln.rate = (smother::Rate)smother::clampi((int)t.getProperty("rate", 1), 0, smother::rateCount - 1);
    ln.drum = (smother::DrumType)smother::clampi((int)t.getProperty("drum", 0), 0, smother::drumCount - 1);
    ln.midiNote = smother::clampi((int)t.getProperty("midi", 36), 0, 127);
    ln.kit = smother::clampi((int)t.getProperty("kit", 0), 0, smother::kitCount - 1);
    ln.tune = (float)t.getProperty("tune", 0);
    ln.decay = (float)t.getProperty("decay", 0.55);
    ln.tone = (float)t.getProperty("tone", 0.65);
    ln.pan = (float)t.getProperty("pan", 0);
    ln.level = (float)t.getProperty("level", 0.85);
    ln.env = (smother::EnvShape)smother::clampi((int)t.getProperty("env", 2), 0, smother::envCount - 1);
    ln.eqLow = (float)t.getProperty("eqlow", 0);
    ln.eqMid = (float)t.getProperty("eqmid", 0);
    ln.eqHigh = (float)t.getProperty("eqhigh", 0);
    ln.hp = (float)t.getProperty("hp", 20);
    ln.lp = (float)t.getProperty("lp", 20000);
    ln.drive = (float)t.getProperty("drive", 0);
    ln.crush = (float)t.getProperty("crush", 0);
    ln.transient = (float)t.getProperty("transient", 0);
    ln.ott = (float)t.getProperty("ott", 0);
    ln.chopStart = (float)t.getProperty("chop0", 0);
    ln.chopEnd = (float)t.getProperty("chop1", 1);
    ln.userName = t.getProperty("username", {}).toString().toStdString();
    const auto vel = t.getProperty("vel", {}).toString();
    juce::StringArray parts;
    parts.addTokens(vel, ",", "");
    for (int i = 0; i < std::min(parts.size(), smother::maxSteps); ++i)
        ln.vel[(size_t)i] = parts[i].getFloatValue();
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        auto ch = t.getChild(i);
        if (ch.hasType("SLEEVE") && ln.sleeveCount < smother::maxSleeves)
            ln.sleeves[(size_t)ln.sleeveCount++] = readSleeve(ch);
    }
    const auto b64 = t.getProperty("sample", {}).toString();
    if (b64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding(b64) && mb.getSize() >= sizeof(float))
        {
            const int n = int(mb.getSize() / sizeof(float));
            ln.user.resize((size_t)n);
            std::memcpy(ln.user.data(), mb.getData(), (size_t)n * sizeof(float));
            ln.userSr = (double)t.getProperty("samplesr", 48000);
            ln.hasUser = true;
        }
    }
    return ln;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SmotherProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (const auto &d : smother::definitions)
    {
        if (d.choice)
        {
            juce::StringArray names;
            const juce::String id(d.id);
            if (id == "seq")
                names = juce::StringArray{ "Off", "On" };
            else if (id == "kit")
                for (auto *nm : smother::kitNames)
                    names.add(nm);
            p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(d.id, 1), d.name, names,
                                                               int(d.initial)));
            continue;
        }
        const float span = d.high - d.low;
        const float step = span <= 1.1f ? 0.001f : (span >= 1000 ? 1.f : (span > 50 ? 0.1f : 0.01f));
        juce::NormalisableRange<float> range(d.low, d.high, step);
        if (juce::String(d.id) == "masterhp" || juce::String(d.id) == "masterlp")
            range.setSkewForCentre(std::sqrt(std::max(d.low * d.high, 1.f)));
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(d.id, 1), d.name, range, d.initial,
            juce::AudioParameterFloatAttributes().withLabel(d.unit).withStringFromValueFunction(
                [step](float v, int)
                { return juce::String(v, step < 0.01f ? 3 : (step < 0.1f ? 2 : (step < 1 ? 1 : 0))); })));
    }
    return p;
}

SmotherProcessor::SmotherProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "SMOTHER", parameters())
{
    for (int i = 0; i < smother::Count; ++i)
        values[i] = state.getRawParameterValue(smother::definitions[i].id);
    for (auto &s : steps)
        s.store(-1);
    seq = smother::makePreset(0);
}

bool SmotherProcessor::isBusesLayoutSupported(const BusesLayout &b) const
{
    const auto out = b.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SmotherProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr > 1000 ? sr : 48000;
    engine.prepare(sampleRate);
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        engine.setPattern(seq);
        engineGen = patternGen;
    }
    internalPpq = 0;
}

void SmotherProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals guard;
    smother::Settings s;
    for (int i = 0; i < smother::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v)
                         ? juce::jlimit(smother::definitions[i].low, smother::definitions[i].high, v)
                         : smother::definitions[i].initial;
    }
    engine.set(s);
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        if (engineGen != patternGen)
        {
            engine.setPattern(seq);
            engineGen = patternGen;
        }
    }
    double hostBpm = 120, ppq = 0;
    bool playing = false, haveHost = false;
    if (auto *ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm())
                hostBpm = *pos->getBpm();
            if (pos->getPpqPosition())
            {
                ppq = *pos->getPpqPosition();
                haveHost = true;
            }
            playing = pos->getIsPlaying();
        }
    if (testClock.load())
    {
        hostBpm = testBpm.load();
        playing = true;
        ppq = internalPpq;
        haveHost = true;
    }
    else if (!playing && wrapperType == wrapperType_Standalone && s.value[smother::Seq] >= 0.5f)
    {
        playing = true;
        ppq = internalPpq;
        haveHost = true;
    }
    if (!std::isfinite(hostBpm) || hostBpm < 10 || hostBpm > 400)
        hostBpm = 120;
    bpm.store(hostBpm);
    engine.setTransport(hostBpm, ppq, playing);
    engine.clearMidiQueue();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getVelocity() > 0)
            engine.queueMidi(meta.samplePosition, true, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0))
            engine.queueMidi(meta.samplePosition, false, m.getNoteNumber(), 0);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine.resetVoices();
    }
    midi.clear();
    auto out = getBusBuffer(buffer, false, 0);
    out.clear();
    const int N = out.getNumSamples();
    float *lptr = out.getWritePointer(0);
    float *rptr = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    if (rptr == nullptr)
    {
        // Mono: process into a local right buffer we discard, write left in place.
        monoScratch.resize((size_t)std::max(0, N));
        rptr = monoScratch.data();
    }
    engine.process(lptr, rptr, N);
    float peak = 0;
    for (int n = 0; n < N; ++n)
        peak = std::max({ peak, std::abs(lptr[n]), std::abs(rptr[n]) });
    meter.store(peak);
    reduction.store(engine.reductionDb);
    for (int ln = 0; ln < smother::maxLanes; ++ln)
        steps[(size_t)ln].store(engine.currentStep(ln));
    allStep.store(engine.allPlayStep());
    if (playing)
        internalPpq = ppq + N * hostBpm / (60.0 * sampleRate);
    else
        internalPpq = haveHost ? ppq : 0;
}

void SmotherProcessor::set(const juce::String &id, float v)
{
    if (auto *p = state.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(v));
        p->endChangeGesture();
    }
}

smother::Pattern SmotherProcessor::pattern() const
{
    const juce::SpinLock::ScopedLockType sl(lock);
    return seq;
}

void SmotherProcessor::setPattern(const smother::Pattern &p)
{
    const juce::SpinLock::ScopedLockType sl(lock);
    seq = p;
    seq.numLanes = juce::jlimit(1, smother::maxLanes, seq.numLanes);
    ++patternGen;
}

void SmotherProcessor::addLane()
{
    const juce::SpinLock::ScopedLockType sl(lock);
    if (seq.numLanes >= smother::maxLanes)
        return;
    int i = seq.numLanes;
    auto &ln = seq.lanes[(size_t)i];
    ln = smother::Lane{};
    ln.drum = (smother::DrumType)(i % smother::drumCount);
    ln.midiNote = smother::defaultMidi(ln.drum, i / smother::drumCount);
    ln.kit = juce::jlimit(0, smother::kitCount - 1, int(std::lround(values[smother::Kit]->load())));
    ++seq.numLanes;
    ++patternGen;
}

void SmotherProcessor::removeLane()
{
    const juce::SpinLock::ScopedLockType sl(lock);
    if (seq.numLanes > 1)
    {
        --seq.numLanes;
        ++patternGen;
    }
}

bool SmotherProcessor::loadSample(int lane, const juce::File &f)
{
    if (lane < 0 || lane >= smother::maxLanes || !f.existsAsFile())
        return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr || r->lengthInSamples < 8)
        return false;
    const int n = (int)std::min(r->lengthInSamples, (juce::int64)(r->sampleRate * 12));
    juce::AudioBuffer<float> b((int)r->numChannels, n);
    r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            s += b.getSample(c, i);
        mono[(size_t)i] = s / (float)std::max(1, b.getNumChannels());
    }
    return loadSample(lane, mono.data(), n, r->sampleRate, f.getFileName());
}

bool SmotherProcessor::loadSample(int lane, const float *data, int n, double sr, const juce::String &name)
{
    if (lane < 0 || lane >= smother::maxLanes || data == nullptr || n < 8)
        return false;
    const juce::SpinLock::ScopedLockType sl(lock);
    auto &ln = seq.lanes[(size_t)lane];
    ln.user.assign(data, data + n);
    ln.userSr = sr > 1000 ? sr : 48000;
    ln.hasUser = true;
    ln.userName = name.toStdString();
    ln.chopStart = 0;
    ln.chopEnd = 1;
    ++patternGen;
    return true;
}

void SmotherProcessor::clearSample(int lane)
{
    if (lane < 0 || lane >= smother::maxLanes)
        return;
    const juce::SpinLock::ScopedLockType sl(lock);
    auto &ln = seq.lanes[(size_t)lane];
    ln.hasUser = false;
    ln.user.clear();
    ln.userName.clear();
    ++patternGen;
}

void SmotherProcessor::setTestClock(double b, bool on)
{
    testBpm.store(std::isfinite(b) && b > 10 ? b : 120);
    testClock.store(on);
    if (!on)
        internalPpq = 0;
}

void SmotherProcessor::factory(int n)
{
    n = juce::jlimit(0, smother::presetCount - 1, n);
    lastPreset = n;
    setPattern(smother::makePreset(n));
    set("kit", float((int)smother::makePreset(n).lanes[0].kit));
    if (n == 18) // Late Swing
        set("swing", 0.55f);
    else
        set("swing", 0.f);
    set("human", 0.f);
    set("sleevehuman", 0.f);
    set("seq", 1.f);
    selectedLane = 0;
    selectedSleeve = -1;
    selectedAll = -1;
}

void SmotherProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto t = state.copyState();
    writePattern(t);
    std::unique_ptr<juce::XmlElement> xml(t.createXml());
    if (xml)
        copyXmlToBinary(*xml, dest);
}

void SmotherProcessor::setStateInformation(const void *data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr)
        return;
    auto t = juce::ValueTree::fromXml(*xml);
    if (!t.isValid())
        return;
    state.replaceState(t);
    readPattern(t);
}

void SmotherProcessor::writePattern(juce::ValueTree root) const
{
    auto p = pattern();
    juce::ValueTree pat("PATTERN");
    pat.setProperty("num", p.numLanes, nullptr);
    pat.setProperty("preset", lastPreset, nullptr);
    for (int i = 0; i < p.numLanes; ++i)
    {
        juce::ValueTree ln("LANE");
        writeLane(ln, p.lanes[(size_t)i]);
        pat.appendChild(ln, nullptr);
    }
    writeSleeveList(pat, p.allSleeves.data(), p.allCount, "ALL");
    root.removeChild(root.getChildWithName("PATTERN"), nullptr);
    root.appendChild(pat, nullptr);
}

void SmotherProcessor::readPattern(const juce::ValueTree &root)
{
    auto pat = root.getChildWithName("PATTERN");
    if (!pat.isValid())
        return;
    smother::Pattern p;
    smother::initDefaultLanes(p);
    p.numLanes = juce::jlimit(1, smother::maxLanes, (int)pat.getProperty("num", 8));
    int li = 0;
    for (int i = 0; i < pat.getNumChildren(); ++i)
    {
        auto ch = pat.getChild(i);
        if (ch.hasType("LANE") && li < smother::maxLanes)
            p.lanes[(size_t)li++] = readLane(ch);
        else if (ch.hasType("ALL") && p.allCount < smother::maxSleeves)
            p.allSleeves[(size_t)p.allCount++] = readSleeve(ch);
    }
    if (li > 0)
        p.numLanes = juce::jlimit(1, smother::maxLanes, li);
    lastPreset = (int)pat.getProperty("preset", 0);
    setPattern(p);
}

juce::File SmotherProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CMPLXTY SMOTHER");
}

juce::File SmotherProcessor::exportMidi(juce::String &err) const
{
    auto p = pattern();
    const float swing = juce::jlimit(0.f, 1.f, values[smother::Swing]->load());
    const float human = juce::jlimit(0.f, 1.f, values[smother::Human]->load());
    constexpr int tpq = 960;
    juce::MidiMessageSequence seqq;
    seqq.addEvent(juce::MidiMessage::textMetaEvent(3, "SMOTHER"), 0);
    seqq.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60'000'000.0 / tempo()))), 0);
    seqq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    int notes = 0;
    // Export enough loops so every lane's polymeter completes at least once in a common window
    // of 4 beats (one bar at 1/16 x 16) times the LCM-ish 8 bars, enough for tests.
    const double window = 16.0; // 4 bars
    for (int ln = 0; ln < p.numLanes; ++ln)
    {
        const auto &lane = p.lanes[(size_t)ln];
        if (!lane.enabled || lane.mute)
            continue;
        const int length = juce::jlimit(1, smother::maxSteps, lane.length);
        const double loop = smother::laneLoopBeats(lane);
        if (loop < 1.0e-12)
            continue;
        const int loops = std::max(1, int(std::ceil(window / loop)));
        const int note = juce::jlimit(0, 127, lane.midiNote);
        for (int lp = 0; lp < loops; ++lp)
        {
            for (int s = 0; s < length; ++s)
            {
                if (lane.vel[(size_t)s] <= 0)
                    continue;
                const double tBeats = lp * loop + smother::stepOnset(lane, ln, s, swing, human);
                if (tBeats >= window)
                    continue;
                const double t = std::round(tBeats * tpq);
                const auto vel = (juce::uint8)juce::jlimit(1, 127, int(lane.vel[(size_t)s] * 110 + 0.5f));
                seqq.addEvent(juce::MidiMessage::noteOn(1, note, vel), t);
                const double dur = smother::laneStepBeats(lane) * 0.9 * tpq;
                seqq.addEvent(juce::MidiMessage::noteOff(1, note), t + std::max(1.0, dur));
                ++notes;
            }
        }
    }
    if (notes == 0)
    {
        err = "The pattern is empty";
        return {};
    }
    seqq.addEvent(juce::MidiMessage::endOfTrack(), window * tpq);
    seqq.updateMatchedPairs();
    seqq.sort();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(tpq);
    mf.addTrack(seqq);
    const auto dir = exportFolder();
    if (!dir.createDirectory())
    {
        err = "Can't create " + dir.getFullPathName();
        return {};
    }
    const auto f = dir.getChildFile(safeName("SMOTHER pattern") + ".mid");
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (!os.openedOk() || !mf.writeTo(os, 1))
    {
        err = "Can't write " + f.getFullPathName();
        return {};
    }
    return f;
}

void SmotherProcessor::renderPattern(juce::AudioBuffer<float> &out, double sr, double tempoBpm, int loops)
{
    smother::Settings s;
    for (int i = 0; i < smother::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v)
                         ? juce::jlimit(smother::definitions[i].low, smother::definitions[i].high, v)
                         : smother::definitions[i].initial;
    }
    s.value[smother::Seq] = 1;
    auto p = pattern();
    // Loop length is the ALL-row bar (4 beats) times requested loops, which covers polymeter lanes.
    const double loopBeats = 4.0;
    loops = std::max(1, loops);
    if (!std::isfinite(tempoBpm) || tempoBpm < 10)
        tempoBpm = 120;
    const int N = std::max(16, int(std::llround(loopBeats * loops * 60.0 / tempoBpm * sr)));
    smother::Engine e;
    e.prepare(sr);
    e.set(s);
    e.setPattern(p);
    out.setSize(2, N);
    out.clear();
    e.setTransport(tempoBpm, 0, true);
    e.process(out.getWritePointer(0), out.getWritePointer(1), N);
}

juce::File SmotherProcessor::exportWav(juce::String &err)
{
    const double sr = sampleRate > 1000 ? sampleRate : 48000.0;
    juce::AudioBuffer<float> b;
    renderPattern(b, sr, tempo(), 1);
    if (b.getMagnitude(0, b.getNumSamples()) <= 0.0f)
    {
        err = "The pattern is silent";
        return {};
    }
    const auto dir = exportFolder();
    if (!dir.createDirectory())
    {
        err = "Can't create " + dir.getFullPathName();
        return {};
    }
    const auto f = dir.getChildFile(safeName("SMOTHER pattern") + ".wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream());
    if (os == nullptr)
    {
        err = "Can't write " + f.getFullPathName();
        return {};
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr)
    {
        err = "Can't write a WAV at " + juce::String(sr) + " Hz";
        return {};
    }
    os.release();
    if (!w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()))
    {
        err = "Write failed: " + f.getFullPathName();
        return {};
    }
    return f;
}

void SmotherProcessor::recordHits(bool on)
{
    const juce::SpinLock::ScopedLockType sl(lock);
    engine.recordHits(on);
}

std::vector<smother::Hit> SmotherProcessor::lastHits() const
{
    // Engine hits are read after processing; no lock needed if the audio thread is stopped.
    return engine.getHits();
}

void SmotherProcessor::setRenderMask(int lane, bool includeMaster) { engine.setRenderMask(lane, includeMaster); }

void SmotherProcessor::fireLane(int lane, float vel)
{
    const juce::SpinLock::ScopedLockType sl(lock);
    engine.setPattern(seq);
    engine.fireLane(lane, vel, -1);
}

juce::AudioProcessorEditor *SmotherProcessor::createEditor() { return new SmotherEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new SmotherProcessor(); }
