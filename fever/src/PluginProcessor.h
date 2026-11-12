#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>

class FeverProcessor final : public juce::AudioProcessor
{
  public:
    FeverProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    void processBlockBypassed(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY FEVER"; }
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
    void factory(int index);
    fever::Settings getSettings() const;
    void applySettings(const fever::Settings &);
    static juce::StringArray presetNames();
    void snapEngine();
    fever::Engine &getEngine() { return engine; }
    const fever::Engine &getEngine() const { return engine; }

    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{ 0 }, reduction{ 0 }, makeup{ 0 };
    std::atomic<float> heartPulse{ 0 }, viewFever{ 0.5f };

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void pullSettings(fever::Settings &) const;
    void pushSettings(const fever::Settings &);
    void syncTransport();
    fever::Engine engine;
    std::array<std::atomic<float> *, fever::Count> values{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeverProcessor)
};
