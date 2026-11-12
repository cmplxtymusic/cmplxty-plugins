#include "PluginProcessor.h"
#include "PluginEditor.h"
juce::AudioProcessorValueTreeState::ParameterLayout HeftProcessor::parameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for (const auto &d : heft::definitions)
    {
        const float step = d.high <= 1 ? .001f : (d.high > 100 ? 1.f : (d.high > 20 ? .1f : .01f));
        juce::NormalisableRange<float> range(d.low, d.high, step);
        if (d.high > 100)
            range.setSkewForCentre(std::sqrt(d.low * d.high > 0 ? d.low * d.high : 100.f));
        // A bipolar shifter is deliberately linear around zero.
        if (juce::String(d.id) == "shift")
            range = juce::NormalisableRange<float>(d.low, d.high, step);
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(d.id, 1), d.name, range, d.initial,
            juce::AudioParameterFloatAttributes().withLabel(d.unit).withStringFromValueFunction(
                [step](float v, int)
                { return juce::String(v, step < .01f ? 3 : (step < .1f ? 2 : (step < 1 ? 1 : 0))); })));
    }
    for (int i = 0; i < 4; ++i)
    {
        const juce::String id = "mod" + juce::String(i);
        p.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(id + "src", 1), "Route " + juce::String(i + 1) + " source",
            juce::StringArray{"Off", "Envelope", "LFO", "Noise", "Sidechain"}, 0));
        p.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(id + "dst", 1), "Route " + juce::String(i + 1) + " destination",
            juce::StringArray{"Slam", "Fuzz", "Wide", "Disperser", "Freq shift", "Transient", "Tight"}, 0));
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(id + "amt", 1), "Route " + juce::String(i + 1) + " depth",
            juce::NormalisableRange<float>(-1.f, 1.f, .001f), 0.f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return juce::String(v, 3); })));
    }
    return p;
}
HeftProcessor::HeftProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "HEFT", parameters())
{
    for (int i = 0; i < heft::Count; ++i)
        values[i] = state.getRawParameterValue(heft::definitions[i].id);
    for (int i = 0; i < 4; ++i)
    {
        juce::String id = "mod" + juce::String(i);
        routes[i] = {state.getRawParameterValue(id + "src"), state.getRawParameterValue(id + "dst"),
                     state.getRawParameterValue(id + "amt")};
    }
    setLatencySamples(heft::Engine::latency);
}
bool HeftProcessor::isBusesLayoutSupported(const BusesLayout &b) const
{
    const auto main = b.getMainInputChannelSet();
    const auto sc = b.getChannelSet(true, 1);
    return (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo()) &&
           main == b.getMainOutputChannelSet() &&
           (sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo());
}
void HeftProcessor::prepareToPlay(double sr, int)
{
    engine.prepare(sr);
    setLatencySamples(heft::Engine::latency);
}
void HeftProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals guard;
    midi.clear();
    heft::Settings s;
    for (int i = 0; i < heft::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v) ? juce::jlimit(heft::definitions[i].low, heft::definitions[i].high, v)
                                      : heft::definitions[i].initial;
    }
    for (int i = 0; i < 4; ++i)
    {
        auto read = [&](int j, float lo, float hi)
        {
            const float v = routes[i][j]->load();
            return std::isfinite(v) ? juce::jlimit(lo, hi, v) : 0.f;
        };
        s.routes[i] = {juce::roundToInt(read(0, 0, 4)), juce::roundToInt(read(1, 0, 6)), read(2, -1, 1)};
    }
    engine.set(s);
    auto main = getBusBuffer(buffer, false, 0);
    auto side = getBusBuffer(buffer, true, 1);
    float peak = 0;
    for (int n = 0; n < main.getNumSamples(); ++n)
    {
        float sc = 0;
        for (int c = 0; c < side.getNumChannels(); ++c)
            sc = std::max(sc, std::abs(side.getSample(c, n)));
        float l = main.getSample(0, n), r = main.getNumChannels() > 1 ? main.getSample(1, n) : l;
        engine.tick(l, r, sc);
        main.setSample(0, n, l);
        if (main.getNumChannels() > 1)
            main.setSample(1, n, r);
        peak = std::max({peak, std::abs(l), std::abs(r)});
    }
    meter.store(peak);
    reduction.store(engine.reductionDb);
}

heft::Settings HeftProcessor::getSettings() const
{
    heft::Settings s;
    for (int i = 0; i < heft::Count; ++i)
    {
        const float v = values[i]->load();
        s.value[i] = std::isfinite(v) ? juce::jlimit(heft::definitions[i].low, heft::definitions[i].high, v)
                                      : heft::definitions[i].initial;
    }
    for (int i = 0; i < 4; ++i)
    {
        auto read = [&](int j, float lo, float hi)
        {
            const float v = routes[i][j]->load();
            return std::isfinite(v) ? juce::jlimit(lo, hi, v) : 0.f;
        };
        s.routes[i] = {juce::roundToInt(read(0, 0, 4)), juce::roundToInt(read(1, 0, 6)), read(2, -1, 1)};
    }
    return s;
}

void HeftProcessor::set(const juce::String &id, float v)
{
    if (auto *p = state.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(v));
        p->endChangeGesture();
    }
}
juce::StringArray HeftProcessor::presetNames()
{
    return {"01 / Neutral",          "02 / Drum weight",  "03 / Bass iron",   "04 / Air and grit",
            "05 / Wide pressure",    "06 / Smoked tails", "07 / Alloy shift", "08 / Moving mass",
            "09 / Sidechain breath", "10 / Soft glue"};
}
void HeftProcessor::factory(int n)
{
    for (const auto &d : heft::definitions)
        set(d.id, d.initial);
    for (int i = 0; i < 4; ++i)
    {
        juce::String id = "mod" + juce::String(i);
        set(id + "src", 0);
        set(id + "dst", 0);
        set(id + "amt", 0);
    }
    switch (n)
    {
    case 0:
        set("slam", 0);
        set("output", 0);
        break;
    case 1:
        set("slam", .65f);
        set("transient", .45f);
        set("tight", .4f);
        set("mix", .8f);
        break;
    case 2:
        set("slam", .72f);
        set("drive", 2.5f);
        set("threshold", -12);
        set("mix", .75f);
        break;
    case 3:
        set("slam", .4f);
        set("fuzz", .45f);
        set("tone", 4700);
        set("wide", .3f);
        break;
    case 4:
        set("slam", .55f);
        set("wide", .7f);
        set("transient", .3f);
        break;
    case 5:
        set("slam", .5f);
        set("tight", .8f);
        set("smear", .3f);
        set("focus", 350);
        break;
    case 6:
        set("slam", .3f);
        set("shift", 37);
        set("smear", .45f);
        set("mix", .5f);
        break;
    case 7:
        set("slam", .45f);
        set("mod0src", 2);
        set("mod0dst", 3);
        set("mod0amt", .7f);
        set("mod1src", 3);
        set("mod1dst", 1);
        set("mod1amt", .4f);
        set("rate", .35f);
        break;
    case 8:
        set("slam", .3f);
        set("mod0src", 4);
        set("mod0dst", 0);
        set("mod0amt", .8f);
        break;
    case 9:
        set("slam", .25f);
        set("ratio", 2);
        set("attack", 30);
        set("release", 200);
        set("mix", .7f);
        break;
    default:
        break;
    }
}
void HeftProcessor::dice()
{
    auto &r = juce::Random::getSystemRandom();
    for (int i = 0; i < 7; ++i)
        set(heft::definitions[i].id,
            i == heft::Shift ? (r.nextFloat() - .5f) * 180 : r.nextFloat() * (i == heft::Fuzz ? .45f : .85f));
    set("tone", 300 + 6000 * r.nextFloat());
    set("focus", 150 + 1800 * r.nextFloat());
    set("rate", .1f + 3 * r.nextFloat());
    for (int i = 0; i < 4; ++i)
    {
        juce::String id = "mod" + juce::String(i);
        set(id + "src", i == 0 ? 1 + r.nextInt(4) : 0);
        set(id + "dst", r.nextInt(7));
        set(id + "amt", (r.nextFloat() - .5f) * .7f);
    }
    // Mix, output, compression internals and limiter are never randomized.
}
void HeftProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto xml = state.copyState().createXml();
    copyXmlToBinary(*xml, dest);
}
void HeftProcessor::setStateInformation(const void *data, int size)
{
    if (size <= 0 || size > 1024 * 1024)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("HEFT"))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessorEditor *HeftProcessor::createEditor() { return new HeftEditor(*this); }
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new HeftProcessor(); }
