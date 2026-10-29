#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analysis.h"
#include "Params.h"
#include "Presets.h"

// CMPLXTY Visualizer: an audio-reactive visualizer. The audio thread only measures the signal (level, bands, onsets,
// host tempo) and pushes small frames into lock-free FIFOs; audio passes through untouched. All drawing happens in
// the editor on its own render thread.
class VisualizerProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1200, kBaseH = 720;
    static constexpr const char* kShortName = "Visualizer";

    struct ImageEntry { juce::String name; juce::MemoryBlock data; };
    struct Extras
    {
        std::vector<ImageEntry> images;
        int selectedImage = 0;
        juce::StringArray lines;
        bool linesFromPreset = true;               // false once you type your own text: presets then leave it alone
        juce::MemoryBlock font; juce::String fontName;
        std::array<juce::String, vz::kMaxTiles> tiles;   // per-tile preset names ("" = automatic variation)
        std::array<juce::String, vz::kNumSlots> slots;   // performance setlist (keys 1..0, Q..M)
        juce::String presetName { "Init" };
    };

    VisualizerProcessor();
    ~VisualizerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Visualizer"; }
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

    // ---- analysis (audio thread -> render thread) ----
    vz::ana::Analyzer analyzer;                 // producer: audio thread
    vz::ana::Features features;                 // consumer: the editor's render thread only
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> activeSlot { -1 };          // last setlist slot fired (UI only)

    vz::ParamValues values() const noexcept;
    float value (int p) const noexcept { return raw[(size_t) p]->load (std::memory_order_relaxed); }
    void setParam (int p, float plainValue);    // message thread; notifies the host
    void setValues (const vz::ParamValues& v, bool includeGlobals = false);

    // ---- extras: images, text, font, grid tiles, setlist (message thread; saved with the project) ----
    Extras getExtras() const;
    int extrasVersion() const noexcept { return extrasVer.load(); }
    // Returns "" on success or a short error.
    juce::String addImageFile (const juce::File&);
    juce::String addImageData (const juce::String& name, const juce::MemoryBlock& data, bool keepCopy);
    void removeImage (int index);
    void clearImages();
    void moveImage (int from, int to);
    void setSelectedImage (int index);
    void setLines (const juce::StringArray& lines, bool fromPreset = false);
    juce::String setFontFile (const juce::File&);
    void clearFont();
    void setTile (int index, const juce::String& presetName);
    void setSlot (int index, const juce::String& presetName);

    // ---- presets ----
    // Names: factory preset names, or "user:<file name>" for presets in the user folder.
    bool applyPreset (const juce::String& name);
    void applyFactory (int index);
    bool resolvePreset (const juce::String& name, vz::ParamValues& out, juce::String* text = nullptr) const;
    juce::File saveUserPreset (const juce::String& name, bool withImages);
    bool loadUserPreset (const juce::File&);
    juce::String currentPresetName() const;
    void setPresetName (const juce::String&);
    juce::StringArray allPresetNames() const;  // factory then user

    static juce::String sanitiseImage (juce::MemoryBlock& data, juce::String& name);   // decode check + downscale

private:
    std::array<std::atomic<float>*, vz::kNumParams> raw {};
    mutable juce::CriticalSection extrasLock;
    Extras extras;
    std::atomic<int> extrasVer { 1 };
    void bump() { extrasVer.fetch_add (1); }
    void writeExtras (juce::XmlElement& parent, bool withImages, bool withSession) const;
    void readExtras (const juce::XmlElement& e, bool withSession);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerProcessor)
};
