#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

class RiggedProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater
{
  public:
    RiggedProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY RIGGED"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    void set(const juce::String &id, float value);
    void setOrder(const std::array<int, rigged::kFxCount> &);
    void factory(int index);
    void randomize();
    bool undo();
    bool redo();
    rigged::Settings getSettings() const;
    void applySettings(const rigged::Settings &, bool recordHistory = false);
    static juce::StringArray presetNames();
    int canUndo() const { return (int)undoStack.size(); }
    int canRedo() const { return (int)redoStack.size(); }
    // Test hook: force a roll at the start of the next processBlock (audio-thread path).
    void requestRoll() { pendingRoll.store(true); }
    void snapEngine();


    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{ 0 }, reduction{ 0 }, makeup{ 0 };
    std::atomic<int> rollCount{ 0 };
    // Dice animation nudge: incremented on each roll so the editor can spin.
    std::atomic<int> diceKick{ 0 };

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void pullSettings(rigged::Settings &) const;
    void pushSettings(const rigged::Settings &);
    void syncTransport();
    rigged::Engine engine;
    std::array<std::atomic<float> *, rigged::Count> values{};
    std::vector<rigged::Settings> undoStack, redoStack;
    std::atomic<bool> pendingRoll{ false };
    float lastRandomize = 0;
    uint32_t rng = 0xC31D4E5Bu;
    void handleAsyncUpdate() override;
    juce::SpinLock uiLock;
    rigged::Settings pendingUi;
    std::atomic<bool> hasPendingUi{ false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiggedProcessor)
};
