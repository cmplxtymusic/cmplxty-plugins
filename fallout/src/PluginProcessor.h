#pragma once
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>
class FalloutProcessor final : public juce::AudioProcessor
{
  public:
    FalloutProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY FALLOUT"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
    void set(const juce::String &, float);
    void factory(int);
    static int presetCount() { return 26; }
    static juce::StringArray presetNames();
    fallout::Pattern pattern() const;
    void setPattern(const fallout::Pattern &);
    void mutatePattern(bool randomize);
    void clearPattern();
    void shiftPattern(int steps);
    void setTestClock(double bpm, bool on);
    double tempo() const noexcept { return bpm.load(); }
    int playStep() const noexcept { return step.load(); }
    bool ashEnabled = true;
    int gridOctave = 0; // -2..2, shifts the visible 2-octave window
    int editMode = 0;   // 0 edit, 1 micro, 2 rubato
    juce::File exportFolder() const;
    juce::File exportMidi(juce::String &err) const;
    juce::File exportWav(juce::String &err);
    void renderPattern(juce::AudioBuffer<float> &out, double sr, double tempoBpm, int loops = 1);
    float voiceMidi() const { return engine.currentMidi; }
    bool voiceGate() const { return engine.gate; }
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{0}, reduction{0};
    std::atomic<int> step{-1};
    std::atomic<double> bpm{120};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void writePattern(juce::ValueTree) const;
    void readPattern(const juce::ValueTree &);
    fallout::Engine engine;
    fallout::Pattern seq;
    mutable juce::SpinLock lock;
    std::array<std::atomic<float> *, fallout::Count> values{};
    double internalPpq = 0, sampleRate = 48000;
    std::atomic<double> testBpm{120};
    std::atomic<bool> testClock{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FalloutProcessor)
};
