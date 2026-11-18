#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DrumDSP.h"
#include "PresetManager.h"
#include "Patterns.h"

// CMPLXTY Drum Player: 8-pad drum / one-shot sample player. MIDI notes trigger the pads (pad 1 = C1 by default); each pad plays a built-in
// synthesised drum sound or a dragged-in sample, with tune, decay, pan, level and a choke group. Kits (incl. the samples) live in presets.
class DrumPlayerProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int kBaseW = 1100, kBaseH = 1126;
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

    // 1.1.0 patterns, fills, tempo and drag-outs
    double hostTempo() const noexcept { return hostBpm.load(); }          // last tempo the host sent (kept while it is stopped); 0 = never (Standalone)
    // 1.2.0: the TEMPO knob (20-250 BPM) overrides the host; at "Host" the host's last tempo is used, 120 if the host never sent one
    double tempo() const noexcept { const float m = val (kParamBpm); return m >= 20.0f ? (double) m : hostBpm.load() > 0 ? hostBpm.load() : 120.0; }
    juce::String tempoSource() const { return val (kParamBpm) >= 20.0f ? "manual" : hostBpm.load() > 0 ? "host" : "default"; }
    double playPosition() const noexcept { return playBeat.load(); }      // beat in the clip, < 0 = not playing
    // 1.2.0 preview: plays the clip (or one part of it) through the plug-in's output, free-running at tempo(), looping, without the host
    // transport. Stops by itself when the host transport starts or stops. Hits land on the same samples as in the drag-out WAV.
    void startPreview (int part) { pvCmd.store (1 + juce::jlimit (0, kNumParts - 1, part)); }
    void stopPreview() { pvCmd.store (-1); }
    int previewPart() const noexcept { const int c = pvCmd.load(); return c > 0 ? c - 1 : c < 0 ? -1 : pvState.load(); }   // -1 = stopped
    bool hostIsPlaying() const noexcept { return hostRunning.load(); }
    bool syncedToHost() const noexcept { return hostRunning.load() && val (kParamPatPlay) > 0.5f; }   // PLAY WITH HOST is sounding
    cw::pat::Clip clipSettings() const;
    std::vector<cw::pat::Ev> clipEvents() const { std::vector<cw::pat::Ev> v; v.reserve (8192); buildEvents (clipSettings(), v); return v; }
    juce::String clipTitle() const;
    void newFill();                                                       // next fill variation
    enum Part { kPartFull = 0, kPartKick, kPartSnare, kPartTop, kPartToms, kNumParts };
    static const char* partName (int);
    static bool partHasPad (int part, int pad);
    juce::File exportFolder() const;
    juce::File exportMidi (juce::String& err) const;                      // GM notes, host tempo, 960 ppq, the clip length
    juce::File exportPart (int part, juce::String& err);                  // 24-bit WAV, whole bars, tail wrapped to the start (loops)
    void renderClip (int part, juce::AudioBuffer<float>& out, double sr); // offline render through this kit (message thread): the loop as it
                                                                          // sounds when it repeats (tails, chokes and voices from the previous pass)
    static constexpr int kParamPatPlay = 58, kParamBpm = 59, kParamSoftClip = 60;

    // 1.2.0 step editor: each factory pattern can be edited (11 lanes x 32 steps, velocity 1..100 %). Edits are saved with the session,
    // feed the preview, PLAY WITH HOST and the drag-outs, and "Reset to factory" brings the original back (the factory table never changes).
    // Message thread writes; the audio thread copies the grid lock-free (seqlock on stepVer) and rebuilds the clip when the version moves.
    static constexpr int kNumPatterns = 18;
    bool patternEdited (int p) const { return p >= 0 && p < kNumPatterns && edited[(size_t) p].load(); }
    int stepAt (int p, int lane, int step) const;                 // what plays (the factory step if the pattern is not edited)
    void setStep (int p, int lane, int step, int vel);            // vel 0 = off
    void clearPattern (int p);                                    // edited, every step off
    void resetPattern (int p);                                    // back to the factory pattern
    void getGrid (int p, uint8_t* g) const;                       // kGrid cells
    void setGrid (int p, const uint8_t* g);
    int stepVersion() const noexcept { return stepVer.load(); }
    // user patterns: grid + groove / fill settings as small XML files in <user data>/Patterns (kit presets do not touch patterns)
    static constexpr const char* kPatternExt = ".dppattern";
    static juce::File patternDir();
    static juce::Array<juce::File> patternFiles();
    bool savePattern (const juce::File&, const juce::String& name);
    bool loadPattern (const juce::File&);                         // into the selected pattern slot (+ its swing / fill settings)
    juce::String patternLabel() const;                            // pattern name, " *" when edited

    // 1.2.0 sound swap: any pad can take a sound from another factory kit (its sound, tune, decay and level; pan and choke stay) or a
    // built-in sound; "Reset to kit" brings back what the last loaded kit had on that pad (saved with the session).
    static int numKits();
    static juce::String kitName (int kit);
    cw::dsp::PadP kitPad (int kit, int pad) const;                // that kit's pad, tune adjusted to this kit's KIT TUNE
    void swapFromKit (int pad, int kit, int srcPad);
    void swapBuiltIn (int pad, int sound);
    void resetPadToKit (int pad);
    bool padSwapped (int pad) const;                              // differs from the kit reference
    juce::String kitRefName() const { return kitRefTitle.isNotEmpty() ? kitRefTitle : presets.currentName(); }
    cw::dsp::PadP kitRefPad (int pad) const;
    void audition (int pad, cw::dsp::PadP);                       // plays the pad slot with other settings (audio thread, next block)
    void stopAudition() { audition (-1, {}); }

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
    void snapshotKit (const juce::String& name);
    static cw::dsp::PadP factoryPad (int kit, int pad);
    void setPadParams (int pad, const cw::dsp::PadP&, bool withPanChoke);
    void buildEvents (cw::pat::Clip, std::vector<cw::pat::Ev>&, int* version = nullptr) const;
    juce::int64 clipKey (const cw::pat::Clip&) const;
    void beginStepWrite() { stepVer.fetch_add (1); }
    void endStepWrite();
    std::array<std::atomic<uint8_t>, (size_t) kNumPatterns * cw::pat::kGrid> grid {};
    std::array<std::atomic<bool>, kNumPatterns> edited {};
    std::atomic<int> stepVer { 0 };   // odd while the message thread writes
    int pvVer = -1, clipVer = -1;
    std::array<cw::dsp::PadP, cw::dsp::kPads> kitRef {};
    juce::String kitRefTitle { "Init" };
    struct Aud { cw::dsp::PadP pp; int pad = -1; };
    std::array<Aud, 16> aud {}; std::atomic<int> audW { 0 }; int audR = 0;

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
    std::atomic<double> hostBpm { 0.0 }, rate { 48000.0 }, playBeat { -1.0 };
    juce::int64 lastClipKey = -1;
    std::vector<cw::pat::Ev> clipEv;
    // preview (audio thread)
    struct PvEv { juce::int64 at; int note; float vel; };
    std::atomic<int> pvCmd { 0 }, pvState { -1 };
    std::atomic<bool> hostRunning { false };
    bool wasHostPlaying = false;
    int pvPart = -1; juce::int64 pvPos = 0, pvN = 0;
    juce::int64 pvKey = -1;
    std::vector<PvEv> pvEv;
    std::vector<cw::pat::Ev> pvClip;
    float val (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPlayerProcessor)
};
