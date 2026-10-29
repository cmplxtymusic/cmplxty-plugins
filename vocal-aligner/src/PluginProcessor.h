#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "AlignDSP.h"
#include "PresetKit.h"
#include "UpdateCheck.h"

// CMPLXTY Vocal Aligner.  Put it on the dub (double / harmony) track.
//  - CAPTURE: while the host plays, the dub on the main input and the guide on the side-chain input are recorded against the song position
//    (no side-chain? drop or load the guide as an audio file instead - files can also replace either take)
//  - ALIGN:   the dub is time-warped (and, if asked, pitch-matched) to the guide on a background thread
//  - then the plug-in plays the aligned dub in place of the original whenever the song position is inside the captured range,
//    and EXPORT / drag-out gives you the aligned take as a WAV that starts at the song's beginning.
class VocalAlignerProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Vocal Aligner";
    static constexpr double kMaxSeconds = 300.0;   // longest take (5 minutes)

    VocalAlignerProcessor();
    ~VocalAlignerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CMPLXTY Vocal Aligner"; }
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
    pk::PresetManager presets { apvts, &undoManager, "CMPLXTY Vocal Aligner", va::upd::dataDir(), VA_VERSION_STRING };
    std::atomic<int> uiWidth { 0 }, uiHeight { 0 };

    // ---- takes (message thread API) --------------------------------------------------------------------------------------------
    enum class Which { guide = 0, dub = 1, aligned = 2 };
    enum class Status { idle, capturing, aligning, ready };
    struct Take { std::vector<float> data; juce::int64 origin = 0; int length = 0; double sr = 0.0; };

    void startCapture();                 // arms both takes (guide only if the side-chain is connected)
    void stopCapture();                  // and aligns straight away if "Auto Align" is on and both takes exist
    bool isCapturing() const noexcept { return capturing.load(); }
    bool sidechainConnected() const noexcept { return scConnected.load(); }
    void startAlign();
    void cancelAlign();
    void clearTakes();
    juce::String loadFile (Which, const juce::File&);   // returns an error text or empty
    juce::File exportAligned (juce::String& error);     // writes Documents/CMPLXTY Vocal Aligner/<name>.wav
    juce::File exportFolder() const;

    Status status() const noexcept;
    float progress() const noexcept { return alignProgress.load(); }
    double capturedSeconds (Which) const;
    float inputLevel() const noexcept { return level.load(); }
    bool hasTake (Which w) const { return takeLen[(int) w].load() > 0; }
    int contentVersion() const noexcept { return version.load(); }
    juce::String lastMessage() const { const juce::SpinLock::ScopedLockType l (msgLock); return message; }
    // overview of a take for the display: min/max per bin over [t0, t1) song seconds
    void overview (Which, double t0, double t1, std::vector<std::pair<float, float>>& out) const;
    bool songRange (double& t0, double& t1) const;   // union of all takes, in song seconds
    double playheadSeconds() const noexcept { return playSec.load(); }

    // test hooks
    void testSetTake (Which, const std::vector<float>& data, juce::int64 originSamples, double sr);
    bool testWaitAlign (int timeoutMs);
    std::vector<float> testAligned() const;

private:
    friend struct AlignJob;
    void setMessage (const juce::String&);
    void runAlign();
    juce::MemoryBlock encodeTake (const Take&) const;
    bool decodeTake (const juce::MemoryBlock&, Take&) const;

    double sampleRate = 48000.0;
    Take takes[3];
    juce::SpinLock takeLock;                       // audio thread only ever try-locks it
    std::atomic<int> takeLen[3] {};
    std::atomic<bool> capturing { false }, armGuide { false }, armDub { false }, scConnected { false };
    std::atomic<float> level { 0.0f }, alignProgress { 0.0f };
    std::atomic<double> playSec { 0.0 };
    std::atomic<int> version { 0 };
    std::atomic<bool> aligning { false }, cancel { false };
    std::unique_ptr<juce::Thread> worker;
    mutable juce::SpinLock msgLock;
    juce::String message;
    std::atomic<float> *pTight = nullptr, *pShift = nullptr, *pPitchOn = nullptr, *pPitchAmt = nullptr, *pMonitor = nullptr, *pGain = nullptr, *pAuto = nullptr;
    float gainSmooth = 1.0f;
    // display overview (one min/max pair per 10 ms) - never touched by the audio thread
    struct PeakStore { juce::CriticalSection lock; std::vector<std::pair<float, float>> peaks[3]; juce::int64 origin[3] {}; int bin = 480; };
    mutable PeakStore peakStore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalAlignerProcessor)
};
