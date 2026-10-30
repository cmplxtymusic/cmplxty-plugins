#pragma once
#include "SmoothBypass.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlitchCrusherDSP.h"
#include "PresetKit.h"
#include "UpdateCheck.h"

class GlitchCrusherProcessor final : public juce::AudioProcessor
{
public:
    GlitchCrusherProcessor();
    ~GlitchCrusherProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // host bypass: a real parameter with a short, latency-matched crossfade (no click, no timing jump)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CMPLXTY Glitch Crusher"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // a held stutter slice can repeat for one more slice after the input stops (+ filter ring): report that, at least 0.25 s
    double getTailLengthSeconds() const override
    {
        static const int div[4] = { 4, 8, 16, 32 };
        const double slice = pGlitchOn->load() > 0.5f ? (4.0 / div[juce::jlimit (0, 3, (int) pRate->load())]) * 60.0 / juce::jlimit (30.0, 300.0, lastBpm.load()) : 0.0;
        return juce::jmax (0.25, slice + 0.05);
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    pk::PresetManager presets { apvts, nullptr, "CMPLXTY Glitch Crusher", gc::upd::dataDir(), GC_VERSION_STRING };

    // editor window size (resizable, fixed aspect), kept with the session
    std::atomic<int> uiWidth { 720 }, uiHeight { 384 };

private:
    std::atomic<double> lastBpm { 120.0 };
    gc::Engine engine;
    std::atomic<float>* pBits; std::atomic<float>* pDown; std::atomic<float>* pDrive;
    std::atomic<float>* pProb; std::atomic<float>* pRate; std::atomic<float>* pRev;
    std::atomic<float>* pFType; std::atomic<float>* pCut; std::atomic<float>* pRes;
    std::atomic<float>* pMix; std::atomic<float>* pOut;
    std::atomic<float>* pCrushOn; std::atomic<float>* pGlitchOn; std::atomic<float>* pFilterOn;
    bool firstBlock = true;

    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchCrusherProcessor)
};
