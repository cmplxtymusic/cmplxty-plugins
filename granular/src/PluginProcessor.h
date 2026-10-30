#pragma once
#include "SmoothBypass.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "GranularDSP.h"
#include "PresetManager.h"

// CMPLXTY Granular: glitchy grain clouds and textures from the incoming audio - freeze, reverse grains, pitch + pitch spread
// (free / semitones / fifths / octaves), four grain shapes, feedback. Zero latency.
class GranularProcessor final : public juce::AudioProcessor, private juce::Timer
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
    // host bypass: a real parameter with a short, latency-matched crossfade (no click, no timing jump)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Granular"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // tail: the grain buffer span (position + spray + size) repeated by the feedback until it is 60 dB down; Freeze holds forever
    double getTailLengthSeconds() const override
    {
        if (pFreeze->load() > 0.5f) return 60.0;
        const double span = (pPos->load() + pSpray->load() + pSize->load()) * 0.001, fb = std::clamp ((double) pFb->load(), 0.0, 0.95);
        const double periods = fb > 1.0e-3 ? 3.0 / -std::log10 (fb) : 0.0;
        return std::clamp (std::ceil ((span * (1.0 + periods) + 0.25) * 4.0) / 4.0, 0.25, 30.0);
    }
    void timerCallback() override   // hosts cache the tail: tell them when it changes (message thread)
    {
        const double t = getTailLengthSeconds();
        if (t != lastTail) { lastTail = t; updateHostDisplay(); }
    }
    double lastTail = -1;
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
    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularProcessor)
};
