#pragma once
#include "SmoothBypass.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "NeonDSP.h"
#include "PresetManager.h"

// CMPLXTY Neon FX: performance multi-FX for live tweaking. Drive -> filter (LP / HP / BP / formant with a tempo LFO) -> trance gate
// -> stutter -> tape stop -> DJ filter macro -> mix. Everything follows the host tempo (120 BPM free-run when there is none).
class NeonFXProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 624;
    static constexpr const char* kShortName = "Neon FX";

    NeonFXProcessor();
    ~NeonFXProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // host bypass: a real parameter with a short, latency-matched crossfade (no click, no timing jump)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Neon FX"; }
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
    cw::PresetManager presets;
    void reset() override { engine.reset(); }

    float outLevel() const noexcept { return engine.outPeak.load (std::memory_order_relaxed); }
    const cw::dsp::Engine& dspEngine() const noexcept { return engine; }
    cw::dsp::Params currentParams() const;
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<bool> hostPlaying { false };
    // tests: force a transport when there is no play head
    std::optional<cw::dsp::Transport> testTransport;

private:
    cw::dsp::Engine engine;
    std::vector<std::atomic<float>*> raw;
    std::vector<float> scratchR;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonFXProcessor)
};
