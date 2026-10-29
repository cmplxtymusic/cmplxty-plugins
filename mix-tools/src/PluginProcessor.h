#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MixToolsDSP.h"
#include "PresetManager.h"

// CMPLXTY Mix Tools: stereo widener + transient shaper + true-peak limiter in one strip. Each module has its own power switch,
// widener and shaper can swap order, the limiter is always last. Constant latency (1.5 ms lookahead + true-peak filter).
class MixToolsProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Mix Tools";

    MixToolsProcessor();
    ~MixToolsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Mix Tools"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };
    mt::PresetManager presets;
    void reset() override { engine.reset(); }

    // display
    float inLevel() const noexcept { return engine.inLevel.load (std::memory_order_relaxed); }
    float outLevel() const noexcept { return engine.outLevel.load (std::memory_order_relaxed); }
    const mt::dsp::Engine& dspEngine() const noexcept { return engine; }
    mt::dsp::Params currentParams() const;

private:
    mt::dsp::Engine engine;
    std::atomic<float> *pOrder, *pWOn, *pWidth, *pMono, *pTOn, *pAtt, *pSus, *pSpeed, *pLOn, *pDrive, *pCeil, *pRel, *pGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixToolsProcessor)
};
