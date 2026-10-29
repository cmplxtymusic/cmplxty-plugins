#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DriftDSP.h"

class DriftProcessor final : public juce::AudioProcessor
{
public:
    // editor window: a fixed-ratio, freely scalable window; the base size is what the layout is designed at
    static constexpr int kBaseW = 900, kBaseH = 590;

    DriftProcessor();
    ~DriftProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CMPLXTY Drift"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;               // behind the editor's UNDO / REDO buttons (declared before apvts)
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // the engine's lock-free sample taps feed the editor's spectrum display
    df::Engine& getEngine() noexcept { return engine; }
    std::atomic<int> heldNote { -1 };            // MIDI note currently driving keytrack (-1: none) - for the editor
    // saved with the preset (not host parameters): editor size in pixels (0 = default)
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

private:
    df::Engine engine;
    struct Ptrs
    {
        std::atomic<float> *coarse, *fine, *shiftOn, *spread, *wide, *mix, *feedback, *keyOn, *keyAmt,
                           *lowBand, *highBand, *xoLow, *xoHigh, *slope, *soloLow, *soloMid, *soloHigh;
    } P {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftProcessor)
};
