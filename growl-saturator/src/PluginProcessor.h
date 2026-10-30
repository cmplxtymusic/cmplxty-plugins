#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GrowlDSP.h"
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "SmoothBypass.h"

class GrowlProcessor final : public juce::AudioProcessor
{
public:
    GrowlProcessor();
    ~GrowlProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // host bypass: a real parameter with a short, latency-matched crossfade (no click, no timing jump)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CMPLXTY Growl"; }
    bool acceptsMidi() const override { return false; }
    int getLatencySamples() const { return gw::kLatency; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }   // formant resonators / filters ring briefly

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    pk::PresetManager presets { apvts, nullptr, "CMPLXTY Growl", gw::upd::dataDir(), GW_VERSION_STRING };

    // live vowel position (incl. LFO) for the editor's formant display; written by the audio thread, read by the UI
    std::atomic<float> vowelDisplay { 0.4f };
    // editor window size (resizable, fixed aspect), kept with the session
    std::atomic<int> uiWidth { 840 }, uiHeight { 540 };

private:
    gw::Engine engine;
    struct Ptrs
    {
        std::atomic<float> *inGain, *xover, *subLevel, *subMono, *driveOn, *drive, *satType, *bias, *os,
                           *formantOn, *vowel, *shift, *focus, *fAmt, *lfoRate, *lfoShape, *lfoDepth,
                           *postOn, *postType, *postCut, *postRes, *mix, *outGain, *limiter;
    } P {};
    bool firstBlock = true;
    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrowlProcessor)
};
