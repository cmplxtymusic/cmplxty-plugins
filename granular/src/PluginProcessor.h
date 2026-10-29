#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GranularDSP.h"
#include "PresetManager.h"

// CMPLXTY Granular: glitchy grain clouds and textures from the incoming audio - freeze, reverse grains, pitch + pitch spread
// (free / semitones / fifths / octaves), four grain shapes, feedback. Zero latency.
class GranularProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Granular";

    GranularProcessor();
    ~GranularProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Granular"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return pFreeze->load() > 0.5f ? 60.0 : 4.0 + (double) pFb->load() * 20.0; }
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
    gr::PresetManager presets;
    void reset() override { engine.reset(); }

    // display
    float inLevel() const noexcept { return engine.inLevel.load (std::memory_order_relaxed); }
    float outLevel() const noexcept { return engine.outLevel.load (std::memory_order_relaxed); }
    const gr::dsp::Engine& dspEngine() const noexcept { return engine; }
    float freezeAmount() const noexcept { return engine.freezeAmt.load (std::memory_order_relaxed); }
    gr::dsp::Params currentParams() const;

private:
    gr::dsp::Engine engine;
    std::atomic<float> *pSize, *pDens, *pPos, *pSpray, *pPitch, *pSpread, *pMode, *pRev, *pChaos, *pShape, *pWidth, *pFb, *pTone, *pFreeze, *pMix, *pGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularProcessor)
};
