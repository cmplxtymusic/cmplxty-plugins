#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>

class StokeProcessor final : public juce::AudioProcessor
{
  public:
    StokeProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY STOKE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    void set(const juce::String &id, float value);
    void factory(int index);
    stoke::Settings getSettings() const;
    void applySettings(const stoke::Settings &);
    static juce::StringArray presetNames();
    void snapEngine();
    stoke::Engine &getEngine() { return engine; }
    const stoke::Engine &getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{ 0 }, reduction{ 0 }, makeup{ 0 };

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void pullSettings(stoke::Settings &) const;
    void pushSettings(const stoke::Settings &);
    stoke::Engine engine;
    std::array<std::atomic<float> *, stoke::Count> values{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StokeProcessor)
};
