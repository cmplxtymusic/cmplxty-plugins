#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RewiredEngine.h"
#include "PresetKit.h"
#include "UpdateCheck.h"

// CMPLXTY Rewired: offline audio-to-MIDI.
//  - load a whole song (it is split into vocals / drums / bass / other with Spleeter) or your own stems
//  - PROCESS runs on a background thread: Bass, Melody, Chords, Kick, Snare, Hats, Cymbals come out as MIDI parts
//  - drag any part (or all of them) into the DAW, or EXPORT one type-1 .mid with a named track per part
//  - audio passes straight through, so it can sit on any track (or run as the Standalone app)
class RewiredProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Rewired";

    RewiredProcessor();
    ~RewiredProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Rewired"; }
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
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Rewired", rw::upd::dataDir(), RW_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

    // ---- inputs (message thread)
    enum Slot { kSong = 0, kBassStem, kMelodyStem, kChordsStem, kDrumsStem, kNumSlots };
    static const char* slotName (int s);
    juce::String setInput (int slot, const juce::File&);   // returns an error text or empty; a song clears the stems and vice versa
    void clearInput (int slot);
    void clearAll();
    juce::File input (int slot) const;
    bool songMode() const { return input (kSong) != juce::File(); }
    bool hasAnyInput() const;
    juce::String title() const;                             // file name of the song / first stem, used for the export folder

    // ---- processing
    enum class Status { idle, working, ready, failed };
    void startProcess();
    void cancelProcess();
    Status status() const noexcept { return (Status) st.load(); }
    float progress() const noexcept { return prog.load(); }
    juce::String lastMessage() const { const juce::SpinLock::ScopedLockType l (msgLock); return message; }
    int contentVersion() const noexcept { return version.load(); }
    rw::Result result() const { const juce::ScopedLock l (resLock); return res; }
    bool hasResult() const { const juce::ScopedLock l (resLock); return res.ok; }
    rw::Settings currentSettings() const;
    juce::File modelDir() const;

    // ---- parts + export
    bool partOn (int p) const { return partEnabled[(size_t) p].load(); }
    void setPartOn (int p, bool on) { partEnabled[(size_t) p].store (on); ++version; }
    juce::File exportFolder() const;                         // Documents/CMPLXTY Rewired/<title>
    juce::File exportMidi (bool selectedOnly, juce::String& err);   // <title>.mid (all parts) or <title> - Selected.mid
    juce::File exportPart (int part, juce::String& err);     // <title> - <Part>.mid (for drag-out)

    // test hooks
    bool testWaitProcess (int timeoutMs);
    void testSetResult (const rw::Result&);

private:
    friend struct TranscribeJob;
    void run();
    void setMessage (const juce::String&);
    juce::File writeParts (const std::vector<int>& parts, const juce::String& suffix, juce::String& err);

    juce::File inputs[kNumSlots];
    mutable juce::CriticalSection inLock, resLock;
    rw::Result res;
    rw::Engine engine;
    bool engineLoaded = false;
    std::array<std::atomic<bool>, rw::kNumParts> partEnabled;
    std::atomic<int> st { 0 }, version { 0 };
    std::atomic<float> prog { 0.0f };
    std::atomic<bool> cancel { false };
    std::unique_ptr<juce::Thread> worker;
    mutable juce::SpinLock msgLock;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RewiredProcessor)
};
