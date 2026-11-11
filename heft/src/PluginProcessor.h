#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>
class HeftProcessor final : public juce::AudioProcessor
{
  public:
    HeftProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY HEFT"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return .2; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    void set(const juce::String &, float);
    void factory(int);
    void dice();
    static juce::StringArray presetNames();
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{0}, reduction{0};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    heft::Engine engine;
    std::array<std::atomic<float> *, heft::Count> values{};
    std::array<std::array<std::atomic<float> *, 3>, 4> routes{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeftProcessor)
};
