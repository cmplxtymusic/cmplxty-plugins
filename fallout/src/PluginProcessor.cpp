#include "PluginProcessor.h"
#include "PluginEditor.h"
namespace
{
juce::String safeName(juce::String s)
{
    s = s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    return s.trim().isEmpty() ? "FALLOUT" : s;
}
fallout::Pattern makePat(int length, std::initializer_list<int> notes, const char *flags = nullptr)
{
    fallout::Pattern p;
    p.length = juce::jlimit(1, fallout::maxSteps, length);
    int i = 0;
    for (int n : notes)
    {
        if (i >= fallout::maxSteps)
            break;
        p.steps[(size_t)i].note = fallout::clampNote(n);
        if (flags != nullptr && flags[i] != 0)
        {
            const char c = flags[i];
            p.steps[(size_t)i].accent = (c == 'a' || c == 'b' || c == 'A' || c == 'B');
            p.steps[(size_t)i].slide = (c == 's' || c == 'b' || c == 'S' || c == 'B');
        }
        ++i;
    }
    return p;
}
} // namespace
juce::AudioProcessorValueTreeState::ParameterLayout FalloutProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (const auto &d : fallout::definitions)
    {
        if (d.choice)
        {
            juce::StringArray names;
            const juce::String id(d.id);
            if (id == "osc")
                for (auto *nm : fallout::oscNames)
                    names.add(nm);
            else if (id == "rate" || id == "delaydiv")
                for (auto *nm : fallout::rateNames)
                    names.add(nm);
            else if (id == "seq")
                names = juce::StringArray{"Off", "On"};
            else if (id == "midimode")
                for (auto *nm : fallout::midiNames)
                    names.add(nm);
            p.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID(d.id, 1), d.name, names, int(d.initial)));
            continue;
        }
        const float span = d.high - d.low;
        const float step = juce::String(d.id) == "length" ? 1.f
                          : (span <= 1.1f ? .001f : (span >= 1000 ? 1.f : (span > 50 ? .1f : .01f)));
        juce::NormalisableRange<float> range(d.low, d.high, step);
        if (juce::String(d.id) == "cutoff" || juce::String(d.id) == "delaytone" || juce::String(d.id) == "decay")
            range.setSkewForCentre(std::sqrt(std::max(d.low * d.high, 1.f)));
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(d.id, 1), d.name, range, d.initial,
            juce::AudioParameterFloatAttributes().withLabel(d.unit).withStringFromValueFunction(
                [step](float v, int)
                { return juce::String(v, step < .01f ? 3 : (step < .1f ? 2 : (step < 1 ? 1 : 0))); })));
    }
    return p;
}
FalloutProcessor::FalloutProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "FALLOUT", parameters())
{
    for (int i = 0; i < fallout::Count; ++i)
        values[i] = state.getRawParameterValue(fallout::definitions[i].id);
    seq = makePat(16, {48, 48, 51, 48, 55, 48, 51, 43, 48, 48, 51, 55, 48, 43, 46, 51}, "a..s.a.s.a.b..as");
}
bool FalloutProcessor::isBusesLayoutSupported(const BusesLayout &b) const
{
    const auto out = b.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}
void FalloutProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr > 1000 ? sr : 48000;
    engine.prepare(sampleRate);
    internalPpq = 0;
}
void FalloutProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals guard;
    fallout::Settings s;
    for (int i = 0; i < fallout::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v)
                         ? juce::jlimit(fallout::definitions[i].low, fallout::definitions[i].high, v)
                         : fallout::definitions[i].initial;
    }
    engine.set(s);
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        engine.setPattern(seq);
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
    else if (!playing && wrapperType == wrapperType_Standalone && s.value[fallout::Seq] >= .5f)
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
        if (m.isNoteOn())
            engine.queueMidi(meta.samplePosition, true, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0))
            engine.queueMidi(meta.samplePosition, false, m.getNoteNumber(), 0);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            engine.resetVoice();
        }
    }
    midi.clear();
    auto out = getBusBuffer(buffer, false, 0);
    out.clear();
    float peak = 0;
    for (int n = 0; n < out.getNumSamples(); ++n)
    {
        float l = 0, r = 0;
        engine.tick(l, r, n);
        out.setSample(0, n, l);
        if (out.getNumChannels() > 1)
            out.setSample(1, n, r);
        peak = std::max({peak, std::abs(l), std::abs(r)});
    }
    meter.store(peak);
    reduction.store(engine.reductionDb);
    step.store(engine.currentStep());
    const int N = out.getNumSamples();
    if (playing)
        internalPpq = ppq + N * hostBpm / (60.0 * sampleRate);
    else
        internalPpq = haveHost ? ppq : 0;
}
void FalloutProcessor::set(const juce::String &id, float v)
{
    if (auto *p = state.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(v));
        p->endChangeGesture();
    }
}
fallout::Pattern FalloutProcessor::pattern() const
{
    const juce::SpinLock::ScopedLockType sl(lock);
    return seq;
}
void FalloutProcessor::setPattern(const fallout::Pattern &p)
{
    const juce::SpinLock::ScopedLockType sl(lock);
    seq = p;
    seq.length = juce::jlimit(1, fallout::maxSteps, seq.length);
}
void FalloutProcessor::clearPattern()
{
    fallout::Pattern p;
    p.length = juce::jlimit(1, fallout::maxSteps, int(std::lround(values[fallout::Length]->load())));
    setPattern(p);
}
void FalloutProcessor::shiftPattern(int steps)
{
    auto p = pattern();
    const int n = p.length;
    if (n <= 1)
        return;
    const int k = ((steps % n) + n) % n;
    fallout::Pattern q = p;
    for (int i = 0; i < n; ++i)
        q.steps[(size_t)i] = p.steps[(size_t)((i - k + n) % n)];
    setPattern(q);
}
void FalloutProcessor::mutatePattern(bool randomize)
{
    auto p = pattern();
    auto &rng = juce::Random::getSystemRandom();
    const int root = fallout::baseNote + 12; // C2
    const int scale[] = {0, 0, 1, 3, 3, 5, 6, 7, 7, 8, 10, 12, 15, 17};
    auto pick = [&] { return root + scale[rng.nextInt(int(sizeof(scale) / sizeof(scale[0])))]; };
    if (randomize)
    {
        for (int i = 0; i < p.length; ++i)
        {
            auto &st = p.steps[(size_t)i];
            if (rng.nextFloat() < .22f)
            {
                st = {};
                continue;
            }
            st.note = pick();
            st.accent = rng.nextFloat() < .28f;
            st.slide = rng.nextFloat() < .22f;
            st.micro = 0;
            st.rubato = 0;
        }
    }
    else
    {
        for (int i = 0; i < p.length; ++i)
        {
            if (rng.nextFloat() > .35f)
                continue;
            auto &st = p.steps[(size_t)i];
            if (st.note < 0)
            {
                if (rng.nextFloat() < .5f)
                    st.note = pick();
                continue;
            }
            const int kind = rng.nextInt(5);
            if (kind == 0)
                st.note = fallout::clampNote(st.note + (rng.nextBool() ? 1 : -1));
            else if (kind == 1)
                st.accent = !st.accent;
            else if (kind == 2)
                st.slide = !st.slide;
            else if (kind == 3)
                st.note = -1;
            else
                st.micro = juce::jlimit(-100.f, 100.f, st.micro + (rng.nextFloat() - .5f) * 30);
        }
    }
    setPattern(p);
}
void FalloutProcessor::setTestClock(double tempoBpm, bool on)
{
    testBpm.store(std::isfinite(tempoBpm) && tempoBpm > 10 ? tempoBpm : 120.0);
    testClock.store(on);
    if (on)
        internalPpq = 0;
}
juce::StringArray FalloutProcessor::presetNames()
{
    return {"01 / Ash Init",        "02 / Residue Line",    "03 / Sump Crawler",  "04 / Glow Worm",
            "05 / Night Drain",     "06 / Rusted Slide",    "07 / Black Rain",    "08 / Bunker Pulse",
            "09 / Ember Horde",     "10 / Folded Waste",    "11 / Snarl Pit",     "12 / Half-Life",
            "13 / Geiger Loop",     "14 / Vault Sixteen",   "15 / Isotope",       "16 / Shelter",
            "17 / Contagion",       "18 / Melted Cable",    "19 / Dosimeter",     "20 / Aftershock",
            "21 / Lead Cloud",      "22 / Siren Bass",      "23 / Dry Well",      "24 / Mutant March",
            "25 / Last Broadcast",  "26 / Fallout Shelter"};
}
void FalloutProcessor::factory(int n)
{
    for (const auto &d : fallout::definitions)
        set(d.id, d.initial);
    n = juce::jlimit(0, presetCount() - 1, n);
    const int C = 48, D = 50, Eb = 51, E = 52, F = 53, Fs = 54, G = 55, Ab = 56, A = 57, Bb = 58, B = 59,
              C3 = 60, D3 = 62, Eb3 = 63, F3 = 65, G3 = 67, C2 = 36, G2 = 43, A2 = 45, Bb2 = 46;
    auto apply = [&](int osc, float cut, float res, float env, float dec, float acc, float drv, float gld,
                     fallout::Pattern pat, float dmix = .18f, int ddiv = 4, float human = 0)
    {
        set("osc", float(osc));
        set("cutoff", cut);
        set("res", res);
        set("envmod", env);
        set("decay", dec);
        set("accent", acc);
        set("drive", drv);
        set("glide", gld);
        set("delaymix", dmix);
        set("delaydiv", float(ddiv));
        set("human", human);
        set("length", float(pat.length));
        setPattern(pat);
    };
    switch (n)
    {
    case 0:
        apply(0, 820, .4f, .55f, 260, .65f, .22f, .18f,
              makePat(16, {C, C, C, G2, C, Eb, C, G2, C, C, Eb, G, C, Bb2, C, G2}, "a...a.s..a.a...s"));
        break;
    case 1:
        apply(0, 540, .62f, .78f, 180, .8f, .35f, .3f,
              makePat(16, {C, C, Eb, C, F, C, Eb, G2, C, Bb2, C, Eb, G, F, Eb, C}, "a.s.a.sb.a.s.a.s"));
        break;
    case 2:
        apply(1, 430, .7f, .7f, 320, .75f, .4f, .4f,
              makePat(16, {C2, C, C2, Eb, C2, C, G2, C, C2, C, Bb2, C, G2, Eb, C2, C}, "a.s.a..b.a.s.a.s"));
        break;
    case 3:
        apply(2, 1100, .5f, .45f, 140, .6f, .2f, .12f,
              makePat(16, {G, G, C3, G, Eb, G, C, G, G, Bb, C3, G, F, Eb, C, G}, "a..sa..s.a.s...."));
        set("pwm", .28f);
        break;
    case 4:
        apply(0, 360, .8f, .85f, 420, .85f, .5f, .35f,
              makePat(16, {C, G2, C, Ab, C, G2, Eb, C, C, Bb2, C, F, Eb, C, G2, C}, "as.bas.b.as.a.s."));
        break;
    case 5:
        apply(0, 680, .55f, .66f, 200, .7f, .3f, .7f,
              makePat(16, {C, D, Eb, F, G, F, Eb, D, C, Bb2, A2, Bb2, C, Eb, G, F}, "bsssssss.sssssb."));
        break;
    case 6:
        apply(1, 900, .35f, .5f, 160, .5f, .15f, .2f,
              makePat(16, {C, -1, C, -1, Eb, -1, C, G2, C, -1, F, -1, Eb, -1, C, -1}, "a...a..s.a......"));
        set("delaymix", .28f);
        break;
    case 7:
        apply(2, 1500, .3f, .35f, 90, .55f, .18f, .08f,
              makePat(16, {C3, C, G, C, C3, Eb, G, C, C3, C, F, C, Bb, G, Eb, C}, "a.a.a.a.a.a.a.a."));
        set("pwm", .18f);
        break;
    case 8:
        apply(3, 980, .42f, .5f, 280, .6f, .25f, .2f,
              makePat(16, {C, C, G, C, Eb3, C, G, C, C, Bb, G, C, F3, Eb, C, G2}, "a..s.a.s.a.s.a.."));
        set("detune", .55f);
        set("delaymix", .22f);
        break;
    case 9:
        apply(4, 620, .58f, .72f, 350, .8f, .45f, .28f,
              makePat(16, {C, Eb, C, F, C, G2, C, Eb, C, Ab, G, F, Eb, C, Bb2, C}, "a.s.a..b.a.s...."));
        set("fold", .7f);
        break;
    case 10:
        apply(5, 480, .65f, .6f, 190, .75f, .55f, .25f,
              makePat(16, {C, C, C, Eb, C, C, G2, C, C, Bb2, C, F, C, Eb, G2, C}, "aa.saa.s.as.a.s."));
        break;
    case 11:
        apply(0, 240, .88f, .9f, 520, .9f, .6f, .45f,
              makePat(16, {C2, C2, C, C2, Eb, C2, G2, C2, C2, C, Bb2, C2, F, C2, Eb, C2}, "asbasbas.sb.asb."));
        break;
    case 12:
        apply(1, 1300, .25f, .4f, 80, .4f, .12f, .05f,
              makePat(8, {C, Eb, G, Bb, C3, Bb, G, Eb}, "a.s.a.s."));
        set("length", 8);
        set("rate", 3); // 1/8T
        break;
    case 13:
        apply(0, 700, .5f, .6f, 220, .7f, .3f, .22f,
              makePat(16, {C, C, C, C, G, C, C, C, Eb, C, C, C, Bb, C, G2, C}, "a...a...a...a.s."));
        break;
    case 14:
        apply(2, 2000, .55f, .8f, 110, .85f, .33f, .15f,
              makePat(16, {F, F, Ab, F, C3, F, Ab, Eb, F, F, Bb, F, C3, Ab, F, Eb}, "as.bas.b.as.a.s."));
        set("pwm", .62f);
        break;
    case 15:
        apply(0, 500, .6f, .5f, 400, .55f, .2f, .5f,
              makePat(16, {C, D, Eb, C, G2, C, Bb2, C, C, Eb, F, G, F, Eb, D, C}, ".ssss.s..ssssss."));
        set("human", .35f);
        break;
    case 16:
        apply(5, 700, .72f, .75f, 160, .8f, .5f, .2f,
              makePat(16, {C, -1, Eb, C, -1, G, C, -1, Bb2, C, -1, Eb, G, -1, C, G2}, "a..a.a..s.a.a..s"));
        break;
    case 17:
        apply(3, 400, .75f, .85f, 600, .7f, .4f, .6f,
              makePat(16, {C2, G2, C, G2, Eb, G2, C, C2, C2, Bb2, C, F, C, G2, Eb, C2}, "bsssbs.s.sss.ss."));
        set("detune", .7f);
        set("delaymix", .3f);
        set("delaydiv", 6);
        break;
    case 18:
        apply(4, 1600, .2f, .3f, 70, .4f, .15f, .1f,
              makePat(16, {C3, G, C3, Eb3, C3, G, C, G, C3, F3, Eb3, C3, G, Eb, C, G}, "a.a.a.a.a.a.a.a."));
        set("fold", .85f);
        set("rate", 0); // 1/32
        break;
    case 19:
        apply(0, 300, .82f, .95f, 140, .95f, .65f, .3f,
              makePat(16, {C, C, C, C, C, C, C, G, C, C, C, C, Eb, C, G2, C}, "aaaaaaa.aaaaa.s."));
        break;
    case 20:
        apply(1, 250, .9f, .7f, 800, .6f, .45f, .4f,
              makePat(16, {C2, C2, C2, C, C2, C2, G2, C2, C2, C2, Bb2, C2, C2, C, G2, C2}, "a..sa..s.a.s.a.s"));
        set("delaymix", .35f);
        set("delayfb", .6f);
        break;
    case 21:
        apply(2, 880, .48f, .68f, 210, .7f, .28f, .25f,
              makePat(16, {C, Eb, Fs, G, C, Eb, Fs, Bb, C, G, Fs, Eb, C, Bb2, G2, C}, "a.s.a.s.a.s.a.s."));
        set("pwm", .4f);
        break;
    case 22:
        apply(0, 180, .55f, .4f, 900, .5f, .1f, .15f,
              makePat(16, {C2, -1, -1, -1, C2, -1, -1, G2, C2, -1, -1, C, G2, -1, Bb2, -1}, "a.......a......."));
        set("delaymix", .15f);
        break;
    case 23:
        apply(3, 750, .5f, .55f, 180, .65f, .3f, .2f,
              makePat(16, {C, Eb, G, C3, Eb3, C3, G, Eb, C, G2, C, Eb, G, Bb, C3, G}, "a.s.a..s.a.s.a.s"));
        set("detune", .4f);
        break;
    case 24:
        apply(5, 1050, .38f, .5f, 120, .6f, .4f, .18f,
              makePat(16, {C, G, C, Bb, C, G, Eb, C, C, F, C, G, C, Eb, Bb2, C}, "a.a.a.a.a.a.a.s."));
        set("delaymix", .25f);
        set("human", .2f);
        break;
    default:
        apply(0, 640, .66f, .8f, 200, .8f, .38f, .33f,
              makePat(16, {C, C, Eb, F, G, F, Eb, C, C, Bb2, C, Eb, G, Ab, G, C}, "as.bas.b.as.a.sb"));
        set("delaymix", .2f);
        break;
    }
}
void FalloutProcessor::writePattern(juce::ValueTree tree) const
{
    auto p = pattern();
    tree.setProperty("length", p.length, nullptr);
    for (int i = 0; i < fallout::maxSteps; ++i)
    {
        auto c = tree.getOrCreateChildWithName("s" + juce::String(i), nullptr);
        const auto &st = p.steps[(size_t)i];
        c.setProperty("note", st.note, nullptr);
        c.setProperty("accent", st.accent, nullptr);
        c.setProperty("slide", st.slide, nullptr);
        c.setProperty("micro", st.micro, nullptr);
        c.setProperty("rubato", st.rubato, nullptr);
    }
}
void FalloutProcessor::readPattern(const juce::ValueTree &tree)
{
    fallout::Pattern p;
    p.length = juce::jlimit(1, fallout::maxSteps, int(tree.getProperty("length", 16)));
    for (int i = 0; i < fallout::maxSteps; ++i)
    {
        auto c = tree.getChildWithName("s" + juce::String(i));
        if (!c.isValid())
            continue;
        p.steps[(size_t)i].note = int(c.getProperty("note", -1));
        p.steps[(size_t)i].accent = bool(c.getProperty("accent", false));
        p.steps[(size_t)i].slide = bool(c.getProperty("slide", false));
        p.steps[(size_t)i].micro = float(c.getProperty("micro", 0));
        p.steps[(size_t)i].rubato = float(c.getProperty("rubato", 0));
    }
    setPattern(p);
}
void FalloutProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto tree = state.copyState();
    writePattern(tree.getOrCreateChildWithName("PATTERN", nullptr));
    tree.setProperty("ash", ashEnabled, nullptr);
    tree.setProperty("gridOctave", gridOctave, nullptr);
    tree.setProperty("editMode", editMode, nullptr);
    auto xml = tree.createXml();
    copyXmlToBinary(*xml, dest);
}
void FalloutProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("FALLOUT"))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            auto pat = tree.getChildWithName("PATTERN");
            if (pat.isValid())
                readPattern(pat);
            ashEnabled = bool(tree.getProperty("ash", true));
            gridOctave = juce::jlimit(-2, 2, int(tree.getProperty("gridOctave", 0)));
            editMode = juce::jlimit(0, 2, int(tree.getProperty("editMode", 0)));
            tree.removeChild(pat, nullptr);
            tree.removeProperty("ash", nullptr);
            tree.removeProperty("gridOctave", nullptr);
            tree.removeProperty("editMode", nullptr);
            state.replaceState(tree);
        }
}
juce::File FalloutProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CMPLXTY FALLOUT");
}
juce::File FalloutProcessor::exportMidi(juce::String &err) const
{
    auto p = pattern();
    const int rateIdx = juce::jlimit(0, 7, int(std::lround(values[fallout::Rate]->load())));
    const double stepBeats = fallout::rateBeats[rateIdx];
    const double human = juce::jlimit(0.f, 1.f, values[fallout::Human]->load());
    constexpr int tpq = 960;
    juce::MidiMessageSequence seqq;
    seqq.addEvent(juce::MidiMessage::textMetaEvent(3, "FALLOUT"), 0);
    seqq.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60'000'000.0 / tempo()))), 0);
    seqq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    int notes = 0;
    for (int i = 0; i < p.length; ++i)
    {
        const auto &st = p.steps[(size_t)i];
        if (st.note < 0)
            continue;
        const double nudge = juce::jlimit(-.45, .45, (double)st.rubato) * human * stepBeats;
        const double t = std::round((i * stepBeats + nudge) * tpq);
        const auto vel = (juce::uint8)juce::jlimit(1, 127, st.accent ? 116 : 80);
        // Per-note micro: pitch bend just before the note (±100 cents = ±4096 at default range).
        if (std::abs(st.micro) > .5f)
        {
            const int bend = juce::jlimit(0, 16383, 8192 + int(std::lround(st.micro / 100.f * 4096.f)));
            seqq.addEvent(juce::MidiMessage::pitchWheel(1, bend), std::max(0.0, t - 1));
        }
        seqq.addEvent(juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, st.note), vel), t);
        int run = 1;
        while (st.slide && i + run < p.length && p.steps[(size_t)(i + run)].note >= 0 &&
               p.steps[(size_t)(i + run - 1)].slide)
            ++run;
        // If this step slides, hold until the next note onset so a DAW can hear the tie.
        const double end = st.slide ? ((i + 1) * stepBeats) * tpq : t + stepBeats * tpq * .9;
        seqq.addEvent(juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, st.note)), end);
        if (std::abs(st.micro) > .5f)
            seqq.addEvent(juce::MidiMessage::pitchWheel(1, 8192), end);
        ++notes;
        juce::ignoreUnused(run);
    }
    if (notes == 0)
    {
        err = "The pattern is empty";
        return {};
    }
    const double loopEnd = p.length * stepBeats * tpq;
    seqq.addEvent(juce::MidiMessage::endOfTrack(), loopEnd);
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
    const auto f = dir.getChildFile(safeName("FALLOUT pattern") + ".mid");
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (!os.openedOk() || !mf.writeTo(os, 1))
    {
        err = "Can't write " + f.getFullPathName();
        return {};
    }
    return f;
}
void FalloutProcessor::renderPattern(juce::AudioBuffer<float> &out, double sr, double tempoBpm, int loops)
{
    fallout::Settings s;
    for (int i = 0; i < fallout::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v)
                         ? juce::jlimit(fallout::definitions[i].low, fallout::definitions[i].high, v)
                         : fallout::definitions[i].initial;
    }
    s.value[fallout::Seq] = 1;
    auto p = pattern();
    const int rateIdx = juce::jlimit(0, 7, int(std::lround(s.value[fallout::Rate])));
    const double stepBeats = fallout::rateBeats[rateIdx];
    const double loopBeats = p.length * stepBeats;
    loops = std::max(1, loops);
    if (!std::isfinite(tempoBpm) || tempoBpm < 10)
        tempoBpm = 120;
    const int N = std::max(16, int(std::llround(loopBeats * loops * 60.0 / tempoBpm * sr)));
    fallout::Engine e;
    e.prepare(sr);
    e.set(s);
    e.setPattern(p);
    out.setSize(2, N);
    out.clear();
    const double inc = tempoBpm / (60.0 * sr);
    double ppq = 0;
    for (int i = 0; i < N; ++i)
    {
        e.setTransport(tempoBpm, ppq, true);
        float l = 0, r = 0;
        e.tick(l, r, i);
        out.setSample(0, i, l);
        out.setSample(1, i, r);
        ppq += inc;
    }
}
juce::File FalloutProcessor::exportWav(juce::String &err)
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
    const auto f = dir.getChildFile(safeName("FALLOUT pattern") + ".wav");
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
juce::AudioProcessorEditor *FalloutProcessor::createEditor() { return new FalloutEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new FalloutProcessor(); }
