#pragma once
#include "SmoothBypass.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "RobotDSP.h"

// CMPLXTY Robot Voice: hard pitch snap to a scale, a channel vocoder whose carrier follows the snapped pitch (or MIDI notes),
// and a ring modulator - in real time. Snap adds 15 ms (reported to the host); zero latency when the snap is off.
class RobotVoiceProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Robot Voice";

    RobotVoiceProcessor();
    ~RobotVoiceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // host bypass: a real parameter with a short, latency-matched crossfade (no click, no timing jump)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Robot Voice"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Robot Voice", rv::upd::dataDir(), RV_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

    // display
    float detectedHz() const noexcept { return engine.detHz.load (std::memory_order_relaxed); }
    float targetHz() const noexcept { return engine.outHz.load (std::memory_order_relaxed); }
    float bandLevel (int b) const noexcept { return engine.levels[(size_t) b].load (std::memory_order_relaxed); }
    int numBands() const noexcept { return rv::dsp::Engine::bandsFor ((int) pBands->load()); }
    int heldNote() const noexcept { return midiNote.load(); }
    rv::dsp::Params currentParams() const;
    // the snap pitch shifter's two-tap delay sits at 15 ms + 1 sample; reported so the DAW compensates (0 with Snap off)
    double curSr = 48000.0;
    int snapLatency() const { return pSnap->load() > 0.5f ? (int) std::lround (0.015 * curSr) + 1 : 0; }
    void syncLatency() { const int L = snapLatency(); if (L != getLatencySamples()) setLatencySamples (L); bypass.setLatency (L); }

private:
    rv::dsp::Engine engine;
    std::atomic<int> midiNote { -1 };
    std::vector<int> held;
    std::atomic<float> *pSnap, *pKey, *pScale, *pSpeed, *pVoc, *pCar, *pBands, *pOct, *pVocMix, *pRing, *pRingHz, *pRingMix, *pMix, *pGain;
    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RobotVoiceProcessor)
};
