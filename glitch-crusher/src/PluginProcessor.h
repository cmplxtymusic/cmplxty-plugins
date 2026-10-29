#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlitchCrusherDSP.h"

class GlitchCrusherProcessor final : public juce::AudioProcessor
{
public:
    GlitchCrusherProcessor();
    ~GlitchCrusherProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CMPLXTY Glitch Crusher"; }
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

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    gc::Engine engine;
    std::atomic<float>* pBits; std::atomic<float>* pDown; std::atomic<float>* pDrive;
    std::atomic<float>* pProb; std::atomic<float>* pRate; std::atomic<float>* pRev;
    std::atomic<float>* pFType; std::atomic<float>* pCut; std::atomic<float>* pRes;
    std::atomic<float>* pMix; std::atomic<float>* pOut;
    std::atomic<float>* pCrushOn; std::atomic<float>* pGlitchOn; std::atomic<float>* pFilterOn;
    bool firstBlock = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchCrusherProcessor)
};
