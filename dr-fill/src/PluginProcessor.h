#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SmoothBypass.h"
#include "FillEngine.h"
#include "PresetKit.h"
#include "UpdateCheck.h"

// CMPLXTY Dr Fill: drum loops in, one fill out.
//  - drop kick, snare and hat / cymbal loops in their boxes (several files at once are sorted by name)
//  - each loop is sliced at its hits; the fill engine builds ONE fill from all three (roll, build, toms, triplets, flams, break, stutter)
//  - the fill follows the host tempo (Standalone: the tempo found in the loops, or the TEMPO knob)
//  - PREVIEW (lined up to end on the next bar when the host plays) or MIDI: C1 (36) kick part, D1 (38) snare, F#1 (42) hats, any other note the full fill
//  - drag KICK / SNARE / HATS / FULL FILL out as WAV files, or SAVE WAVS to Documents/CMPLXTY Dr Fill
//  - the track's own audio passes straight through
class DrFillProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kBaseW = 1000, kBaseH = 620;
    static constexpr const char* kShortName = "Dr Fill";
    static constexpr double kMaxLoopSeconds = 60.0;
    enum Part { kPartKick = 0, kPartSnare = 1, kPartHats = 2, kPartFull = 3, kNumParts = 4 };
    static const char* partName (int p);

    DrFillProcessor();
    ~DrFillProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // host bypass: a real parameter with a short crossfade (no click)
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Dr Fill"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Dr Fill", drf::upd::dataDir(), DRF_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

    // ---- loops (message thread)
    juce::String loadLoop (int lane, const juce::File&);       // returns an error text or empty
    juce::String loadLoopAudio (int lane, drf::Audio a, const juce::String& name);   // (also the test hook)
    void clearLoop (int lane);
    void clearAll();
    juce::String loopName (int lane) const;
    bool hasLoop (int lane) const;
    bool hasAnyLoop() const { for (int l = 0; l < drf::kLanes; ++l) if (hasLoop (l)) return true; return false; }
    static int laneFromFileName (const juce::String& fileName);    // "Kick 01.wav" -> kick ... (-1 when the name says nothing)
    static std::vector<int> assignLanes (const juce::StringArray& files, const std::array<bool, 3>& occupied, int preferred = -1);

    // ---- analysis + fill (worker thread, published here)
    std::shared_ptr<const drf::Kit> kit() const { const juce::SpinLock::ScopedLockType l (pubLock); return kitPub; }
    std::shared_ptr<const drf::Render> render() const { const juce::SpinLock::ScopedLockType l (pubLock); return renderPub; }
    drf::Fill fill() const { const juce::SpinLock::ScopedLockType l (pubLock); return fillPub; }
    drf::TempoResult detectedTempo() const { const juce::SpinLock::ScopedLockType l (pubLock); return tempoPub; }
    double hostTempo() const noexcept { return hostBpm.load(); }   // 0 = the host gives none (Standalone)
    double fillTempo() const;                                      // what the fill is rendered at
    juce::String tempoSource() const;
    int contentVersion() const noexcept { return version.load(); }
    bool busy() const noexcept { return pending.load() || working.load(); }
    drf::FillSettings currentSettings() const;
    void regenerate();                                             // new random seed
    bool waitIdle (int timeoutMs);                                 // test hook

    // ---- playback
    void preview (int part = kPartFull);                           // from the UI; ends on the next bar line when the host plays
    void stopPreview() { stopReq.store (true); }
    bool isPlaying() const noexcept { return playingNow.load(); }
    float playPosition() const noexcept { return playPos.load(); } // 0..1 of the fill, -1 when idle

    // ---- export
    juce::File exportFolder() const;                               // Documents/CMPLXTY Dr Fill
    juce::String fillTitle() const;                                // "Dr Fill - Snare Roll - 1 Bar - 140 BPM - s1234"
    juce::File exportPart (int part, juce::String& err);           // 24-bit WAV of one part or the full fill
    juce::StringArray exportAll (juce::String& err);               // all four

    juce::String lastMessage() const { const juce::SpinLock::ScopedLockType l (msgLock); return message; }
    void setMessage (const juce::String&);

private:
    friend struct DrFillWorker;
    void parameterChanged (const juce::String&, float) override { requestRender(); }
    void requestRender() { pending.store (true); wake.signal(); }
    void workerLoop();
    void publishGarbage();
    double tempoFor (const drf::TempoResult&) const;

    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    // inputs (message thread writes, worker reads under inLock)
    mutable juce::CriticalSection inLock;
    std::array<drf::Audio, drf::kLanes> loops;
    std::array<juce::String, drf::kLanes> names;
    int inputVersion = 0;
    // state blobs (FLAC of each loop), re-encoded only when the loops change: hosts ask for the state often
    mutable juce::CriticalSection blobLock;
    mutable std::array<juce::MemoryBlock, drf::kLanes> blobs;
    mutable int blobVersion = -1;

    // published by the worker
    mutable juce::SpinLock pubLock;
    std::shared_ptr<const drf::Kit> kitPub;
    std::shared_ptr<const drf::Render> renderPub;
    std::vector<std::shared_ptr<const drf::Render>> keepAlive;   // freed on the worker, never on the audio thread
    drf::Fill fillPub;
    drf::TempoResult tempoPub;

    std::unique_ptr<juce::Thread> worker;
    juce::WaitableEvent wake;
    std::atomic<bool> pending { false }, working { false }, quit { false };
    std::atomic<int> version { 0 };
    std::atomic<double> hostBpm { 0.0 }, rate { 48000.0 }, renderedBpm { 0.0 }, renderedRate { 0.0 };

    // audio thread
    struct Voice { const drf::Render* r = nullptr; int part = kPartFull; int64_t pos = 0; int startIn = 0; float fade = 1.0f, fadeStep = 0.0f; bool on = false; };
    std::shared_ptr<const drf::Render> playing;      // the audio thread's own reference (swapped under a try-lock)
    std::shared_ptr<const drf::Render> playingOld;
    Voice voice, tail;
    std::atomic<int> previewReq { -1 };
    std::atomic<bool> stopReq { false }, playingNow { false };
    std::atomic<float> playPos { -1.0f };
    juce::AudioBuffer<float> scratch;

    mutable juce::SpinLock msgLock;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrFillProcessor)
};
