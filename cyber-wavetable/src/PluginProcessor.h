#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CyberWavetableDSP.h"
#include "PresetManager.h"

// CMPLXTY Cyber Wavetable: polyphonic wavetable synth. 2 morphing wavetable oscillators with unison, sub and noise, a multimode
// filter, amp + mod envelopes, 2 LFOs and a 4-slot mod matrix. MIDI in, stereo out.
class CyberWavetableProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 740;
    static constexpr const char* kShortName = "Cyber Wavetable";

    CyberWavetableProcessor();
    ~CyberWavetableProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Cyber Wavetable"; }
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
    cw::PresetManager presets;
    void reset() override { engine.reset(); }

    // display
    float outLevel() const noexcept { return engine.outPeak.load (std::memory_order_relaxed); }
    const cw::dsp::Engine& dspEngine() const noexcept { return engine; }
    cw::dsp::Params currentParams() const;
    // on-screen keyboard / tests: queued notes, played on the audio thread at the next block
    void queueNote (int note, float vel) { uiMidi.push (note, vel); }

private:
    struct UiMidi
    {
        std::array<std::atomic<int>, 64> q {}; std::atomic<int> w { 0 }; int r = 0;
        void push (int note, float vel) { const int i = w.load() & 63; q[(size_t) i].store ((note & 127) | ((int) (vel * 127.0f) << 8)); w.store (w.load() + 1); }
    } uiMidi;
    cw::dsp::Engine engine;
    std::vector<std::atomic<float>*> raw;
    std::vector<float> scratchR;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void handleMidi (const juce::MidiMessage&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CyberWavetableProcessor)
};
