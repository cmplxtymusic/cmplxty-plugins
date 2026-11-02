#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SmoothBypass.h"
#include "SiltDSP.h"
#include "SiltSources.h"
#include "PresetKit.h"
#include "UpdateCheck.h"

// CMPLXTY Silt: a smart granular synth.
//  - drop any sample on layer A (and optionally a second one on layer B); it is analysed on a worker thread:
//    root key (file name / pitch detection, so C plays C), transients (Magnet + slices), pitch marks (Cohere)
//  - 16-voice grain engine with glide, MPE, two LFOs, a mod envelope, 4 macros and an 8-slot mod matrix
//  - SLICE mode: one key per chop from C1 (36) up, and a host-synced 16-step sequencer
//  - FX rack in any order: beat repeat, compressor, delay, reverb
//  - the samples live in the session and in user presets (FLAC), so nothing goes missing when files move
class SiltProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kBaseW = 1180, kBaseH = 760;
    static constexpr const char* kShortName = "Silt";
    static constexpr double kMaxSampleSeconds = 60.0;
    enum Layer { kA = 0, kB = 1 };

    SiltProcessor();
    ~SiltProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { processBlock (b, m); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Silt"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool supportsMPE() const override { return true; }
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
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Silt", slt::upd::dataDir(), SLT_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };
    juce::MidiKeyboardState keyboard;   // the on-screen keys (mouse only)

    // ---- samples (message thread)
    juce::String loadFile (int layer, const juce::File&);                          // error text or empty
    juce::String loadAudio (int layer, slt::Audio a, const juce::String& name, int builtIn = -1);   // also the test hook
    void loadBuiltIn (int layer, int k);
    void clearLayer (int layer);
    juce::String sampleName (int layer) const;
    int builtInOf (int layer) const;
    bool hasSample (int layer) const;
    // manual slice points (source samples of layer A); editing one switches SLICES to Manual
    void setManualSlices (std::vector<int> starts);
    std::vector<int> manualSlices() const;

    // ---- analysis (published by the worker)
    std::shared_ptr<const slt::Source> source (int layer) const { const juce::SpinLock::ScopedLockType l (pubLock); return srcPub[(size_t) juce::jlimit (0, 1, layer)]; }
    std::vector<int> slices() const { const juce::SpinLock::ScopedLockType l (pubLock); return slicesPub; }
    int contentVersion() const noexcept { return version.load(); }
    bool busy() const noexcept { return pending.load() || working.load(); }
    bool waitIdle (int timeoutMs);                                                  // test hook
    float effectiveRoot() const;                                                    // the root key layer A plays at (Tune mode)
    juce::String rootDescription() const;                                           // "C3 +4 ct (file name)" ...
    void useDetectedRoot();                                                         // Tune -> Manual at the detected root

    // ---- engine (display reads its atomics; tests read the probes)
    const slt::Engine& engine() const noexcept { return eng; }
    slt::Params currentParams() const;
    double hostTempo() const noexcept { return hostBpm.load(); }
    bool hostPlaying() const noexcept { return hostRun.load(); }

    // ---- mod matrix helpers (message thread): drag a source onto a knob
    int modSlotFor (int src, int dst) const;                                        // -1 if none
    int assignMod (int src, int dst, float amount = 0.35f);                         // slot used, -1 when all 8 are taken
    void removeMod (int slot);
    static int modDstForParam (const juce::String& paramId);                        // knob -> matrix destination (0 = none)

    juce::String lastMessage() const { const juce::SpinLock::ScopedLockType l (msgLock); return message; }
    void setMessage (const juce::String&);
    static bool readAudio (const juce::File& f, slt::Audio& a, juce::String& err);

private:
    friend struct SiltWorker;
    void parameterChanged (const juce::String&, float) override { request(); }
    void request() { pending.store (true); wake.signal(); }
    void workerLoop();
    void collectGarbage();
    float val (int i) const noexcept { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    void savePresetExtra (juce::XmlElement&);
    void loadPresetExtra (const juce::XmlElement*);
    void factoryLoaded (int index);
    juce::MemoryBlock flacOf (int layer) const;
    static bool audioFromFlac (const void* data, size_t size, slt::Audio& a);

    std::vector<std::atomic<float>*> raw;
    juce::AudioParameterBool* bypassParam = nullptr;
    cmx::SmoothBypass bypass;

    // inputs (message thread writes, worker reads under inLock)
    struct Slot { std::shared_ptr<const slt::Audio> audio; juce::String name; int builtIn = -1; int version = 0; };
    mutable juce::CriticalSection inLock;
    std::array<Slot, 2> slot;
    std::vector<int> manual;
    int manualVersion = 0;
    mutable juce::CriticalSection blobLock;
    mutable std::array<juce::MemoryBlock, 2> blobs;
    mutable std::array<int, 2> blobVersion { -1, -1 };

    // published by the worker
    mutable juce::SpinLock pubLock;
    std::array<std::shared_ptr<const slt::Source>, 2> srcPub;
    std::vector<int> slicesPub;
    int pubVersion = 0;
    std::vector<std::shared_ptr<const slt::Source>> keepAlive;   // freed on the worker, never on the audio thread

    std::unique_ptr<juce::Thread> worker;
    juce::WaitableEvent wake;
    std::atomic<bool> pending { false }, working { false }, quit { false };
    std::atomic<int> version { 0 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostRun { false };

    // audio thread
    slt::Engine eng;
    std::array<std::shared_ptr<const slt::Source>, 2> cur;
    std::array<std::shared_ptr<const slt::Source>, 8> retired;
    int seenPub = -1;
    std::array<int, slt::kMaxSlices> sliceBuf {};
    std::vector<float> bufL, bufR;
    double sr = 48000.0;
    bool wasPlaying = false;

    mutable juce::SpinLock msgLock;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SiltProcessor)
};
