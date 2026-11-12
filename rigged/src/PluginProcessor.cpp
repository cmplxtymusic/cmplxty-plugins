#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

juce::AudioProcessorValueTreeState::ParameterLayout RiggedProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (int i = 0; i < rigged::Count; ++i)
    {
        const auto &d = rigged::definitions[(size_t)i];
        const juce::ParameterID id(d.id, 1);
        if (d.kind == rigged::Kind::Bool)
        {
            p.add(std::make_unique<juce::AudioParameterBool>(id, d.name, d.initial >= 0.5f));
            continue;
        }
        if (d.kind == rigged::Kind::Choice)
        {
            juce::StringArray labels;
            for (int k = 0; k < d.choices; ++k)
                labels.add(d.labels[k]);
            p.add(std::make_unique<juce::AudioParameterChoice>(id, d.name, labels,
                                                               rigged::clampi((int)std::round(d.initial), 0,
                                                                              d.choices - 1)));
            continue;
        }
        const float span = d.high - d.low;
        const float step = span <= 1.01f ? 0.001f : (span > 100 ? 0.01f : (span > 20 ? 0.01f : 0.001f));
        juce::NormalisableRange<float> range(d.low, d.high, step);
        if (d.skew && d.low > 0 && d.high > 0)
            range.setSkewForCentre(std::sqrt(d.low * d.high));
        p.add(std::make_unique<juce::AudioParameterFloat>(
            id, d.name, range, d.initial,
            juce::AudioParameterFloatAttributes().withLabel(d.unit).withStringFromValueFunction(
                [step, i](float v, int) {
                    const auto &def = rigged::definitions[(size_t)i];
                    const int dec = step < 0.01f ? 3 : (step < 0.1f ? 2 : (step < 1 ? 1 : 0));
                    if (juce::String(def.id) == "crsh_bits" || juce::String(def.id) == "crsh_rate")
                        return juce::String((int)std::round(v));
                    return juce::String(v, def.high > 100 ? (def.high > 1000 ? 0 : 1) : dec);
                })));
    }
    return p;
}

RiggedProcessor::RiggedProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "RIGGED", parameters())
{
    for (int i = 0; i < rigged::Count; ++i)
        values[(size_t)i] = state.getRawParameterValue(rigged::definitions[(size_t)i].id);
    rng = (uint32_t)juce::Random::getSystemRandom().nextInt();
    if (rng == 0)
        rng = 0xC31D4E5Bu;
}

void RiggedProcessor::prepareToPlay(double sr, int block)
{
    engine.prepare(sr, block);
    lastRandomize = values[(size_t)rigged::Randomize]->load();
    pendingRoll.store(false);
}

bool RiggedProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void RiggedProcessor::pullSettings(rigged::Settings &s) const
{
    for (int i = 0; i < rigged::Count; ++i)
        s.v[(size_t)i] = values[(size_t)i]->load();
    // Order lives in the ValueTree; keep whatever s already has if the property is missing.
    auto t = state.state;
    if (t.hasProperty("order"))
        rigged::parseOrder(s, t.getProperty("order").toString().toStdString());
}

void RiggedProcessor::pushSettings(const rigged::Settings &s)
{
    for (int i = 0; i < rigged::Count; ++i)
    {
        // Don't write the trigger itself, or it can retrigger.
        if (i == rigged::Randomize)
            continue;
        auto *p = state.getParameter(rigged::definitions[(size_t)i].id);
        if (p == nullptr)
            continue;
        if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
        {
            *pf = s[i];
        }
        else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
        {
            *pb = s[i] >= 0.5f;
        }
        else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
        {
            *pc = rigged::clampi((int)std::round(s[i]), 0, pc->choices.size() - 1);
        }
    }
    state.state.setProperty("order", juce::String(rigged::orderString(s)), nullptr);
}

rigged::Settings RiggedProcessor::getSettings() const
{
    rigged::Settings s;
    pullSettings(s);
    return s;
}

void RiggedProcessor::applySettings(const rigged::Settings &s, bool recordHistory)
{
    if (recordHistory)
    {
        undoStack.push_back(getSettings());
        if (undoStack.size() > (size_t)rigged::kHistory)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    pushSettings(s);
    engine.set(s);
}

void RiggedProcessor::set(const juce::String &id, float value)
{
    auto *p = state.getParameter(id);
    if (p == nullptr)
        return;
    if (auto *pf = dynamic_cast<juce::AudioParameterFloat *>(p))
        *pf = value;
    else if (auto *pb = dynamic_cast<juce::AudioParameterBool *>(p))
        *pb = value >= 0.5f;
    else if (auto *pc = dynamic_cast<juce::AudioParameterChoice *>(p))
        *pc = rigged::clampi((int)std::round(value), 0, pc->choices.size() - 1);
}

void RiggedProcessor::setOrder(const std::array<int, rigged::kFxCount> &o)
{
    std::array<bool, rigged::kFxCount> seen{};
    for (int i = 0; i < rigged::kFxCount; ++i)
    {
        if (o[(size_t)i] < 0 || o[(size_t)i] >= rigged::kFxCount || seen[(size_t)o[(size_t)i]])
            return;
        seen[(size_t)o[(size_t)i]] = true;
    }
    state.state.setProperty("order", juce::String(rigged::orderString([&] {
                                                    rigged::Settings s;
                                                    s.order = o;
                                                    return s;
                                                }())),
                            nullptr);
    auto s = getSettings();
    s.order = o;
    engine.set(s);
}

void RiggedProcessor::factory(int index)
{
    auto s = rigged::makePreset(index);
    undoStack.clear();
    redoStack.clear();
    pushSettings(s);
    engine.set(s);
    engine.reset();
}

void RiggedProcessor::randomize()
{
    auto now = getSettings();
    undoStack.push_back(now);
    if (undoStack.size() > (size_t)rigged::kHistory)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
    auto next = rigged::smartRoll(now, now[rigged::Amount], rng);
    pushSettings(next);
    engine.set(next);
    rollCount.fetch_add(1);
    diceKick.fetch_add(1);
}

bool RiggedProcessor::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back(getSettings());
    auto s = undoStack.back();
    undoStack.pop_back();
    pushSettings(s);
    engine.set(s);
    return true;
}

bool RiggedProcessor::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back(getSettings());
    auto s = redoStack.back();
    redoStack.pop_back();
    pushSettings(s);
    engine.set(s);
    return true;
}

juce::StringArray RiggedProcessor::presetNames()
{
    juce::StringArray a;
    for (int i = 0; i < rigged::presetCount; ++i)
        a.add(rigged::presetNames[i]);
    return a;
}

void RiggedProcessor::snapEngine()
{
    rigged::Settings s;
    pullSettings(s);
    engine.set(s);
    engine.snap();
}

void RiggedProcessor::syncTransport()
{
    double bpm = 120, ppq = 0;
    bool playing = false;
    if (auto *ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            if (auto q = pos->getPpqPosition())
                ppq = *q;
            playing = pos->getIsPlaying();
        }
    }
    engine.setTempo(bpm, playing, ppq);
}

void RiggedProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    const int c = buffer.getNumChannels();
    for (int ch = 2; ch < c; ++ch)
        buffer.clear(ch, 0, n);
    if (c <= 0 || n <= 0)
        return;

    syncTransport();

    rigged::Settings s;
    pullSettings(s);

    const float rnd = s[rigged::Randomize];
    const bool rise = lastRandomize < 0.5f && rnd >= 0.5f;
    lastRandomize = rnd;
    const bool flagged = pendingRoll.exchange(false);
    if (rise || flagged)
    {
        s = rigged::smartRoll(s, s[rigged::Amount], rng);
        // Engine takes the new targets immediately (they are smoothed). APVTS and
        // undo history are updated on the message thread — clap_host_params::request_flush
        // is not legal on the audio thread.
        {
            const juce::SpinLock::ScopedLockType sl(uiLock);
            pendingUi = s;
            hasPendingUi.store(true);
        }
        triggerAsyncUpdate();
        rollCount.fetch_add(1);
        diceKick.fetch_add(1);
    }

    engine.set(s);
    float *l = buffer.getWritePointer(0);
    float *r = c > 1 ? buffer.getWritePointer(1) : nullptr;
    engine.process(l, r, n);
    meter.store(engine.meter);
    reduction.store(engine.reductionDb);
    makeup.store(engine.makeupDb);
}

void RiggedProcessor::processBlockBypassed(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    midi.clear();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
}

void RiggedProcessor::handleAsyncUpdate()
{
    rigged::Settings s;
    {
        const juce::SpinLock::ScopedLockType sl(uiLock);
        if (!hasPendingUi.exchange(false))
            return;
        s = pendingUi;
    }
    undoStack.push_back(getSettings());
    if (undoStack.size() > (size_t)rigged::kHistory)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
    pushSettings(s);
}

void RiggedProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto xml = state.copyState().createXml();
    if (xml)
    {
        xml->setAttribute("order", state.state.getProperty("order", "0,1,2,3,4,5,6,7,8,9,10").toString());
        copyXmlToBinary(*xml, dest);
    }
}

void RiggedProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("RIGGED") || xml->hasTagName(state.state.getType()))
        {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            auto s = getSettings();
            if (xml->hasAttribute("order"))
                rigged::parseOrder(s, xml->getStringAttribute("order").toStdString());
            engine.set(s);
        }
}

juce::AudioProcessorEditor *RiggedProcessor::createEditor() { return new RiggedEditor(*this); }

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new RiggedProcessor(); }
