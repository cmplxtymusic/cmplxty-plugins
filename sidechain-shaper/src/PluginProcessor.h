#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ShaperDSP.h"
#include "PresetManager.h"

// CMPLXTY Sidechain Shaper: a drawable volume curve, synced to the host tempo or restarted by a sidechain (or input) transient detector.
// Pumping, ducking, trance gates, tremolo, swells. Zero latency. The curve lives in the plug-in state (property "curve"), so it is saved
// with the session and in presets, and curve edits are undoable.
class SidechainShaperProcessor final : public juce::AudioProcessor, private juce::ValueTree::Listener
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Sidechain Shaper";

    SidechainShaperProcessor();
    ~SidechainShaperProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Sidechain Shaper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
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
    sc::PresetManager presets;
    void reset() override { engine.reset(); }

    // ---- the curve (message thread)
    sc::dsp::Curve curve() const { return sc::dsp::parseCurve (curveText().toStdString()); }
    juce::String curveText() const { return apvts.state.getProperty ("curve").toString(); }
    void setCurve (const sc::dsp::Curve&, bool undoable, const juce::String& transaction = "Edit curve");
    void previewCurve (const sc::dsp::Curve& c) { apvts.state.setProperty ("curve", juce::String (sc::dsp::formatCurve (sc::dsp::sanitize (c))), nullptr); }  // while dragging
    void commitCurve (const juce::String& before, const sc::dsp::Curve& after);   // end of a drag: one undo step from `before`
    int grid() const { return (int) apvts.state.getProperty ("grid", 16); }
    void setGrid (int g) { apvts.state.setProperty ("grid", g, nullptr); }

    // ---- display
    float inLevel() const noexcept { return engine.inLevel.load (std::memory_order_relaxed); }
    float outLevel() const noexcept { return engine.outLevel.load (std::memory_order_relaxed); }
    const sc::dsp::Engine& dspEngine() const noexcept { return engine; }
    sc::dsp::Params currentParams() const;
    std::atomic<float> hostBpm { 0.0f };
    std::atomic<bool> hostPlaying { false };

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override { ensureCurve(); refreshCurve(); }
    void ensureCurve();
    void refreshCurve() { engine.setCurve (curve()); }

    sc::dsp::Engine engine;
    std::atomic<float> *pMode, *pRate, *pOffset, *pDepth, *pSmooth, *pStereo, *pSource, *pThr, *pFocus, *pHold, *pMix, *pGain;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainShaperProcessor)
};
