#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DrumDSP.h"
#include "PresetManager.h"

// CMPLXTY Drum Player: 8-pad drum / one-shot sample player. MIDI notes trigger the pads (pad 1 = C1 by default); each pad plays a built-in
// synthesised drum sound or a dragged-in sample, with tune, decay, pan, level and a choke group. Kits (incl. the samples) live in presets.
class DrumPlayerProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 872;
    static constexpr const char* kShortName = "Drum Player";

    DrumPlayerProcessor();
    ~DrumPlayerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Drum Player"; }
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

    float outLevel() const noexcept { return engine.outPeak.load (std::memory_order_relaxed); }
    const cw::dsp::Engine& dspEngine() const noexcept { return engine; }
    cw::dsp::Params currentParams() const;
    // pads clicked in the editor / tests: triggered on the audio thread at the next block
    void queuePad (int pad, float vel) { uiPads.push (pad, vel); }

    // user samples (message thread only)
    bool loadSample (int pad, const juce::File&);
    void clearSample (int pad);
    bool hasUserSample (int pad) const { return pad >= 0 && pad < cw::dsp::kPads && user[(size_t) pad] != nullptr; }
    juce::String padSoundName (int pad) const;
    const cw::dsp::Smp* padSample (int pad) const;   // what the pad plays now (nullptr = empty sample slot)
    int sampleChangeCount() const { return sampleChanges; }
    juce::AudioFormatManager& audioFormats() { return formats; }

private:
    struct UiPads
    {
        std::array<std::atomic<int>, 64> q {}; std::atomic<int> w { 0 }; int r = 0;
        void push (int pad, float vel) { const int i = w.load() & 63; q[(size_t) i].store ((pad & 15) | ((int) (juce::jlimit (0.0f, 1.0f, vel) * 127.0f) << 8)); w.store (w.load() + 1); }
    } uiPads;
    struct Dead { std::shared_ptr<const cw::dsp::Smp> s; int gen; };
    void setUser (int pad, std::shared_ptr<const cw::dsp::Smp>, const juce::String& path);
    void writeKit (juce::XmlElement&) const;
    void readKit (const juce::XmlElement*);

    cw::dsp::Engine engine;
    juce::AudioFormatManager formats;
    std::array<std::shared_ptr<const cw::dsp::Smp>, cw::dsp::kPads> user {};
    std::array<juce::String, cw::dsp::kPads> userPath;
    std::vector<Dead> graveyard;
    juce::SpinLock userLock;
    int gen = 0, sampleChanges = 0;
    std::atomic<int> ackGen { 0 };
    std::vector<std::atomic<float>*> raw;
    std::vector<float> scratchR;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void handleMidi (const juce::MidiMessage&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPlayerProcessor)
};
