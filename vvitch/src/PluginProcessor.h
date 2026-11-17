#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VvitchDSP.h"
#include "PresetManager.h"
#define VV_HAS_MACRO_MAP 1   // 1.2.0 (the tests check for it)
#define VV_HAS_PRESET_MACROS 1   // 1.3.0

// CMPLXTY VVITCH: dark polysynth for witch house: detuned saw / square / PWM oscillators with unison, noise, a choral vox formant
// stage, a driven filter, pitch drop and DRAG, arp and glide, CHIP crush, TAPE wow, dark delay and reverb. MIDI in, stereo out.
class VvitchProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 1040;
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
    // 1.1.0 macros: 8 knobs with user-editable names (kept in the session state and in user presets)
    static constexpr int kMacros = 8;
    static const char* defaultMacroName (int m);
    juce::String macroName (int m) const;
    void setMacroName (int m, const juce::String& name);
    // 1.2.0 macro assignment: every macro drives up to kMaxTargets parameters, each by -100..+100 % of that knob's travel at full macro.
    // The stock targets of 1.1.x are the default map (factory presets, Init, and every older session / user preset without a map),
    // so old sounds and old macro moves stay exactly as they were. The map lives in the session state and in user presets.
    // Message thread: edit with the functions below. Audio thread: reads the published copy (atomics, no locks, no allocation).
    static constexpr int kMaxTargets = 8;
    struct MacroTarget { juce::String id; float amount = 0.0f; };
    std::vector<MacroTarget> macroTargets (int m) const;              // in order
    bool isAssignable (const juce::String& paramId) const;            // continuous knobs except the macros themselves
    float macroAmount (int m, const juce::String& paramId) const;     // 0 when not assigned
    bool assignMacro (int m, const juce::String& paramId, float amount);   // add or change; false when the macro is full / not assignable
    void removeMacroTarget (int m, const juce::String& paramId);
    void clearMacro (int m);
    void restoreDefaultMacro (int m);
    bool macroIsDefault (int m) const;
    juce::String macroMapString() const;                              // "id:amount,id:amount|...|..." (8 groups)
    void setMacroMapString (const juce::String&);                     // empty = the 1.1.x stock map
    // 1.3.0 per-preset macros. A spec is 8 groups "NAME>id:amount,id:amount" joined by '|'. Every factory preset carries its own
    // (vv::factoryMacroSpec); loading one sets the names and the map. "Restore preset targets" goes back to the loaded factory
    // preset's own targets (the stock ones for Init, user presets and older sessions). Macros at 0 never change the sound.
    void applyMacroSpec (const juce::String& spec);
    const juce::String& presetMacroSpec() const noexcept { return presetSpec; }
    std::vector<MacroTarget> defaultTargets (int m) const;           // what "restore" brings back for macro m
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
    std::vector<juce::NormalisableRange<float>> ranges;
    float mval (int i, const float* off) const;   // value with the macro offsets applied
    // published macro map for the audio thread: param index (-1 = empty slot) + amount, per macro and slot
    std::array<std::atomic<int>, kMacros * kMaxTargets> mapParam;
    std::array<std::atomic<float>, kMacros * kMaxTargets> mapAmount;
    std::array<std::vector<MacroTarget>, kMacros> mapEdit;          // message-thread copy
    void publishMap();
    juce::String presetSpec;                                          // the loaded factory preset's macro spec ("" = stock)
    int paramIndex (const juce::String& id) const;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void handleMidi (const juce::MidiMessage&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VvitchProcessor)
};
