#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "ReverbDSP.h"

// CMPLXTY Dark Reverb: a big, dark, metallic reverb (8-line feedback delay network) with a pitch-shifted shimmer loop
// and an infinite freeze. Zero latency.
class DarkReverbProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Dark Reverb";

    DarkReverbProcessor();
    ~DarkReverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Dark Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return pFreeze->load() > 0.5f ? 60.0 : (double) pDecay->load() * 1.5 + (double) pPre->load() * 0.001; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Dark Reverb", dr::upd::dataDir(), DR_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

    // display
    float inLevel() const noexcept { return engine.inLevel.load (std::memory_order_relaxed); }
    float wetLevel() const noexcept { return engine.wetLevel.load (std::memory_order_relaxed); }
    float freezeAmount() const noexcept { return engine.freezeAmt.load (std::memory_order_relaxed); }
    dr::dsp::Params currentParams() const;

private:
    dr::dsp::Engine engine;
    std::atomic<float> *pPre, *pSize, *pDecay, *pDiff, *pDark, *pLow, *pMetal, *pMod, *pShOn, *pShP, *pShAmt, *pFreeze, *pWidth, *pMix, *pGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DarkReverbProcessor)
};
