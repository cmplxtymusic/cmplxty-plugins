#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RewiredEngine.h"
#include "PresetKit.h"
#include "UpdateCheck.h"
#include "Preview.h"

// CMPLXTY Rewired: offline audio-to-MIDI.
//  - load a whole song (it is split into vocals / drums / bass / other with Spleeter) or your own stems
//  - PROCESS runs on a background thread: Bass, Melody, Chords, Kick, Snare, Hats, Cymbals come out as MIDI parts
//  - drag any part (or all of them) into the DAW, or EXPORT one type-1 .mid with a named track per part
//  - audio passes straight through, so it can sit on any track (or run as the Standalone app)
class RewiredProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    static constexpr int kBaseW = 900, kBaseH = 560;
    static constexpr const char* kShortName = "Rewired";

    RewiredProcessor();
    ~RewiredProcessor() override;

    void prepareToPlay (double sr, int) override { hostRate.store (sr > 0 ? sr : 44100.0); }
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
    juce::File exportPart (int part, juce::String& err);     // <title> - <Part>.mid (for drag-out); a part with instruments gets one track each
    juce::File exportSub (int part, int sub, juce::String& err);   // <title> - <Part> - <Instrument>.mid (one instrument sub-part)

    // ---- 1.1.0 MAP mode: the song map view, and which lanes carry the real transcribed MIDI instead of empty blocks
    bool mapView() const noexcept { return mapViewOn.load(); }
    void setMapView (bool on) { mapViewOn.store (on); ++version; }
    bool mapMidi (const juce::String& lane) const { const juce::ScopedLock l (inLock); return mapMidiLanes.contains (lane); }
    void setMapMidi (const juce::String& lane, bool on);
    std::vector<bool> mapMidiFlags (const rw::SongMap&) const;   // per lane of the map
    juce::File exportMap (juce::String& err);                   // <title> - Map.mid

    // ---- 1.1.0 tempo: the BPM box / TEMPO knob ("bpm" parameter, 0 = auto) and QUANTIZE re-grid the result live (no PROCESS)
    double effectiveBpm() const;                     // the tempo everything is on: the result's (whole bpm), else the knob's, else 0
    void setBpm (double bpm);                        // 0 = auto; whole bpm, 20..250 (an undoable parameter change)
    bool bpmIsAuto() const;
    bool detectionChanged() const;                   // a detection setting differs from the one the result was made with (PROCESS again)

    // ---- 1.1.0 PLAY / SOLO preview: plays the song (or one lane) through the plug-in's output
    bool canPlay() const;                            // a result or a loaded song / stems
    void play (double fromSeconds);                  // (loads the audio first when the project was just reopened)
    void stop();
    void togglePlay() { if (playing()) stop(); else play (playPosition() >= playLength() - 0.05 ? 0.0 : playPosition()); }
    bool playing() const noexcept { return (isPlaying.load() && ! stopping.load()) || pendingPlay.load() >= 0.0; }   // (a stop fading out counts as stopped)
    double playPosition() const noexcept { return playPos.load(); }
    void seek (double seconds);
    double playLength() const;
    bool previewLoading() const noexcept { return loading.load(); }
    void setSolo (const juce::String& lane);          // "" = the whole song
    juce::String solo() const { const juce::ScopedLock l (inLock); return soloLane; }
    int laneSoloSource (const rw::MapLane&, const rw::Result&) const;   // what SOLO plays for a lane: 1 its stem audio, 2 its notes (synth), 0 nothing

    // test hooks
    bool testWaitProcess (int timeoutMs);
    void testSetResult (const rw::Result&);
    void testSetPreview (std::shared_ptr<const rw::Preview> p) { setPreview (std::move (p)); }
    bool testWaitPreview (int timeoutMs);

private:
    friend struct TranscribeJob;
    friend struct PreviewJob;
    void run();
    void loadPreview();                               // PreviewJob thread: the inputs -> Preview
    void dropPreview();                               // message thread: inputs changed / cleared / reloaded
    bool threadShouldExit() const;
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;                // re-grid on a new tempo / quantize; start a play that waited for the audio
    void syncGrid();
    void setPreview (std::shared_ptr<const rw::Preview>);
    void rebuildSolo();                               // message thread: the solo buffer for the current solo lane
    void renderPreview (juce::AudioBuffer<float>&);
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
    std::atomic<bool> cancel { false }, mapViewOn { false };
    juce::StringArray mapMidiLanes;   // lane names (guarded by inLock)
    juce::String soloLane;            // guarded by inLock

    // preview transport. The audio thread reads pv / soloBuf / soloStem only inside a try-lock of playLock; the message thread
    // swaps them under it (and lets the old buffers go after unlocking), so nothing is freed on the audio thread.
    std::shared_ptr<const rw::Preview> pv;            // message thread copy
    juce::SpinLock playLock;
    std::shared_ptr<const rw::Preview> playPv;
    std::shared_ptr<const std::vector<int16_t>> soloBuf;
    int soloStem = -1;
    bool soloActive = false;
    std::atomic<bool> isPlaying { false }, stopping { false }, loading { false }, hostWasPlaying { false };
    std::atomic<double> playPos { 0.0 }, pendingPlay { -1.0 }, hostRate { 44100.0 };
    float fade = 0.0f;                                // audio thread
    std::unique_ptr<juce::Thread> loader;
    std::atomic<int> previewGen { 0 };
    std::shared_ptr<const rw::Preview> loaded;        // PreviewJob result (guarded by inLock)
    std::unique_ptr<juce::Thread> worker;
    mutable juce::SpinLock msgLock;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RewiredProcessor)
};
