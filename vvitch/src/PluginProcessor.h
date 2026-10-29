#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VvitchDSP.h"
#include "PresetManager.h"

// CMPLXTY VVITCH: dark polysynth for witch house: detuned saw / square / PWM oscillators with unison, noise, a choral vox formant
// stage, a driven filter, pitch drop and DRAG, arp and glide, CHIP crush, TAPE wow, dark delay and reverb. MIDI in, stereo out.
class VvitchProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 800;
    static constexpr const char* kShortName = "VVITCH";

    VvitchProcessor();
    ~VvitchProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY VVITCH"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
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
    std::atomic<double> hostBpm { 120.0 };   // tempo for the arp and the delay display (host tempo when there is one)
    vv::PresetManager presets;
    void reset() override { engine.reset(); }

    // display
    float outLevel() const noexcept { return engine.outPeak.load (std::memory_order_relaxed); }
    const vv::dsp::Engine& dspEngine() const noexcept { return engine; }
    vv::dsp::Params currentParams() const;
    // on-screen keyboard / tests: queued notes, played on the audio thread at the next block
    void queueNote (int note, float vel) { uiMidi.push (note, vel); }

private:
    struct UiMidi
    {
        std::array<std::atomic<int>, 64> q {}; std::atomic<int> w { 0 }; int r = 0;
        void push (int note, float vel) { const int i = w.load() & 63; q[(size_t) i].store ((note & 127) | ((int) (vel * 127.0f) << 8)); w.store (w.load() + 1); }
    } uiMidi;
    vv::dsp::Engine engine;
    std::vector<std::atomic<float>*> raw;
    std::vector<float> scratchR;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void handleMidi (const juce::MidiMessage&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VvitchProcessor)
};
