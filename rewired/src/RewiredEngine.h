#pragma once
// CMPLXTY Rewired transcription engine (no JUCE): audio in, notes out.
//   song  -> Spleeter 4-stem (vocals / drums / bass / other, MIT, Deezer) ratio-mask separation
//         -> basic-pitch (Apache-2.0, Spotify) polyphonic note tracking on bass / vocals / other
//         -> part logic: Bass (lowest monophonic line), Melody (top voice), Chords (the rest of the harmony)
//         -> drum onsets on the drum stem, classified into Kick / Snare / Hats / Cymbals
//   stems -> the same, minus separation.
#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "OrtModel.h"
#include "Instruments.h"

namespace rw
{
enum Part { kBass = 0, kMelody, kChords, kKick, kSnare, kHats, kCymbals, kNumParts };
inline const char* partName (int p)
{
    static const char* n[] = { "Bass", "Melody", "Chords", "Kick", "Snare", "Hats", "Cymbals" };
    return (p >= 0 && p < kNumParts) ? n[p] : "?";
}
inline bool isDrumPart (int p) { return p >= kKick; }
inline int drumNote (int p) { return p == kKick ? 36 : p == kSnare ? 38 : p == kHats ? 42 : 49; }   // General MIDI

struct Note { double start = 0, end = 0; int pitch = 60; int velocity = 100; };

struct Settings
{
    float onsetThreshold = 0.5f;   // basic-pitch onset probability needed to start a note
    float frameThreshold = 0.3f;   // basic-pitch frame probability needed to keep a note going
    float minNoteMs = 120.0f;      // shorter notes are dropped
    float drumSensitivity = 0.5f;  // 0..1, higher finds quieter hits
    int melodySource = 0;          // 0 auto, 1 vocals stem, 2 top voice of the instruments
    float chordMinNotes = 2.0f;    // a chord hit needs at least this many notes starting together
    int melodyGap = 7;             // semitones the top note of a chord hit must clear the voicing by to count as melody
    int quantize = 0;              // 0 off, 1 = 1/16, 2 = 1/8, 3 = 1/4
    float bpm = 0.0f;              // 0 = detect
    bool instruments = true;       // 1.0.1: label instruments inside Bass / Melody / Chords (needs instruments.onnx)
    std::array<int, 4> stemInst { -1, -1, -1, -1 };   // per StemSlot: instrument named by the stem's file name (-1 = ask the model)
};

// input audio: 44.1 kHz, planar; mono buffers are fine (right = empty)
struct Audio { std::vector<float> left, right; bool empty() const { return left.empty(); } size_t size() const { return left.size(); } };

enum StemSlot { kStemBass = 0, kStemMelody, kStemChords, kStemDrums, kNumStemSlots };

struct Result
{
    bool ok = false;
    std::string error;
    std::array<std::vector<Note>, kNumParts> parts;
    double bpm = 120.0;
    double seconds = 0.0;
    std::string melodyFrom;   // "vocals" / "instruments" / "stem"
    std::array<std::vector<SubPart>, kNumParts> subs;   // 1.0.1: instrument sub-parts of Bass / Melody / Chords (empty = not labelled)
};

using Progress = std::function<void (float, const char*)>;

class Engine
{
public:
    // dir holds the runtime library + spleeter4.onnx + nmp.onnx
    bool load (const std::string& dir, std::string& err, int threads = 0);
    bool loaded() const { return spleeter.loaded() && pitch.loaded(); }
    bool instrumentsLoaded() const { return tagger.loaded(); }
    bool tagInstruments (const std::vector<float>& mono44k, std::vector<InstScores>& out, std::string& err) { return tagger.tag (mono44k, out, err); }

    static constexpr double kSampleRate = 44100.0;
    Result transcribeSong (const Audio& song, const Settings&, const std::atomic<bool>* cancel = nullptr, Progress = {});
    Result transcribeStems (const std::array<Audio, kNumStemSlots>& stems, const Settings&, const std::atomic<bool>* cancel = nullptr, Progress = {});

    // building blocks (public for tests)
    bool separate (const Audio& song, std::array<std::vector<float>, 4>& stemsMono, std::string& err, const std::atomic<bool>* cancel, Progress);
    bool notes (const std::vector<float>& mono44k, const Settings&, float minHz, float maxHz, std::vector<Note>& out, std::string& err);

private:
    ort::Session spleeter, pitch;
    InstrumentTagger tagger;
    // runs the tagger on each part's source audio (after quantize, so sub-parts match the parts exactly)
    void labelParts (Result&, const Settings&, const std::array<const std::vector<float>*, kNumParts>& src, const std::array<int, kNumParts>& forced, bool sharedMelodyChords, Progress);
};

// pure helpers
std::vector<float> resample (const std::vector<float>& x, double fromRate, double toRate);
double detectTempo (const std::vector<float>& mono44k);
std::array<std::vector<Note>, 4> detectDrums (const std::vector<float>& drums44k, float sensitivity);
std::vector<Note> monophonic (std::vector<Note> notes, bool lowest);
void splitMelodyChords (const std::vector<Note>& all, std::vector<Note>& melody, std::vector<Note>& chords, float chordMinNotes, int melodyGap = 7);
void quantizeNotes (std::vector<Note>& notes, double bpm, int division);
} // namespace rw
