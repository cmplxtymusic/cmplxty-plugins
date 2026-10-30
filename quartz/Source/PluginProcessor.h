#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include "Engine.h"
#include "SmoothBypass.h"
#include "PresetStore.h"
#include "Version.h"


class QuartzProcessor : public juce::AudioProcessor
{
public:
    QuartzProcessor();
    ~QuartzProcessor() override = default;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }   // SmoothBypass does the latency-aligned crossfade
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Quartz"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    // ---- parameters
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }
    float getRaw (int index) const { return raw[(size_t) index]->load(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- meters / analysis (read by the editor)
    std::atomic<float> inPeak[2] { 0, 0 }, outPeak[2] { 0, 0 };   // max since last read (editor exchanges with 0)
    std::atomic<float> cpuLoad { 0 };
    static constexpr int kScope = 2048;
    std::array<std::atomic<float>, kScope> scope {};
    std::atomic<int> scopePos { 0 };
    int latencySamples() const { return engine.latency(); }

    // ---- presets (message thread)
    qz::PresetStore presets;
    int presetIndex[2] { -1, -1 };      // index into the unified list (factory then user), -1 = none/modified
    juce::String presetName[2];
    bool presetDirty[2] { false, false };
    void loadPreset (int section, int index);
    void stepPreset (int section, int delta);
    bool saveUserPreset (int section, const juce::String& name);
    void deleteCurrentUserPreset (int section);
    void resetSection (int section);
    void markEdited (int section);   // called by UI after the user touches a section knob: preset name shows as modified
    juce::ChangeBroadcaster presetChanged;

    // ---- undo / A-B (message thread)
    struct Snapshot { std::array<float, qz::kNumParams> v {}; juce::String name[2]; int index[2] { -1, -1 }; };
    Snapshot takeSnapshot() const;
    void applySnapshot (const Snapshot&);
    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    int abSlot = 0;                  // 0 = A, 1 = B
    void toggleAB();
    juce::ChangeBroadcaster historyChanged;

    int editorWidth = 1100;          // persisted UI size

private:
    cmx::SmoothBypass bypass;   // master bypass: raised crossfade, dry aligned to engine.latency()
    void setParamValue (int index, float value);   // gesture-wrapped, on the message thread
    qz::Engine engine;
    qz::ParamValues pv;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;
    juce::AudioBuffer<float> scratch;
    double sr = 48000.0;
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot abOther; bool abOtherValid = false;
    juce::String stateVersion { QUARTZ_VERSION_STRING };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuartzProcessor)
};
