#pragma once
#include "Engine.h"
#include "Presets.h"
#include <juce_audio_utils/juce_audio_utils.h>

class SmotherProcessor final : public juce::AudioProcessor
{
  public:
    SmotherProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY SMOTHER"; }
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
    static int presetCount() { return smother::presetCount; }
    static juce::StringArray presetNames() { return smother::presetNameArray(); }

    smother::Pattern pattern() const;
    void setPattern(const smother::Pattern &);
    void addLane();
    void removeLane();
    bool loadSample(int lane, const juce::File &);
    bool loadSample(int lane, const float *data, int n, double sr, const juce::String &name = {});
    void clearSample(int lane);

    void setTestClock(double bpm, bool on);
    double tempo() const noexcept { return bpm.load(); }
    int playStep(int lane) const noexcept
    {
        if (lane < 0 || lane >= smother::maxLanes)
            return -1;
        return steps[lane].load();
    }
    int allPlayStep() const noexcept { return allStep.load(); }

    juce::File exportFolder() const;
    juce::File exportMidi(juce::String &err) const;
    juce::File exportWav(juce::String &err);
    void renderPattern(juce::AudioBuffer<float> &out, double sr, double tempoBpm, int loops = 1);

    // Test hooks.
    void recordHits(bool on);
    std::vector<smother::Hit> lastHits() const;
    void setRenderMask(int lane, bool includeMaster);
    void fireLane(int lane, float vel);
    void setSleeveExtents(float lpFullHz, float hpFullHz) { engine.setSleeveExtents(lpFullHz, hpFullHz); }

    juce::AudioProcessorValueTreeState state;
    std::atomic<float> meter{ 0 }, reduction{ 0 };
    std::atomic<int> allStep{ -1 };
    std::array<std::atomic<int>, smother::maxLanes> steps{};
    std::atomic<double> bpm{ 120 };
    int lastPreset = 0;
    int selectedLane = 0, selectedSleeve = -1; // -1 none, -2 ALL sleeve (see selectedAll)
    int selectedAll = -1;
    juce::String statusMessage;

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters();
    void writePattern(juce::ValueTree) const;
    void readPattern(const juce::ValueTree &);
    smother::Engine engine;
    smother::Pattern seq;
    mutable juce::SpinLock lock;
    std::array<std::atomic<float> *, smother::Count> values{};
    double internalPpq = 0, sampleRate = 48000;
    std::atomic<double> testBpm{ 120 };
    std::atomic<bool> testClock{ false };
    uint32_t patternGen = 1, engineGen = 0;
    std::vector<float> monoScratch;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmotherProcessor)
};
