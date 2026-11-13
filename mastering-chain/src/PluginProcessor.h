#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>

class GlazeProcessor final : public juce::AudioProcessor
{
  public:
    GlazeProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY GLAZE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    void set(const juce::String &id, float value);
    void factory(int index);
    glaze::Settings getSettings() const;
    void applySettings(const glaze::Settings &);
    static juce::StringArray presetNames();
    void snapEngine();
    glaze::Engine &getEngine() { return engine; }
    const glaze::Engine &getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState state;

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void pullSettings(glaze::Settings &) const;
    void pushSettings(const glaze::Settings &);
    glaze::Engine engine;
    std::array<std::atomic<float> *, glaze::Count> values{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlazeProcessor)
};
