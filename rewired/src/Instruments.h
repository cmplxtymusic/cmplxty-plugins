#pragma once
// CMPLXTY Rewired instrument labels (1.0.1): which instrument plays the notes of a part, over time.
// Model: YAMNet (Apache-2.0, Google; AudioSet ontology, 521 classes), exported to ONNX as instruments.onnx with the log-mel front end
// baked in (input: |STFT| patches [N, 96, 257] at 16 kHz, 25 ms periodic Hann / 10 ms hop / 512 FFT; output: sigmoid scores [N, 521]).
// The AudioSet classes are pooled into the instrument groups below; every note gets the group that scores highest around it,
// and a part is split into one sub-part per group. Labels are a best guess: a stem with two instruments playing at once is labelled by
// the louder / more typical one (see NOTES-1.0.1.md for measured accuracy).
#include <array>
#include <string>
#include <vector>
#include "OrtModel.h"

namespace rw
{
struct Note;
enum Inst { kInstHorns = 0, kInstWinds, kInstStrings, kInstKeys, kInstOrgan, kInstGuitar, kInstMallets, kInstSynth, kInstVocals,
            kInstBassGuitar, kInstUpright, kNumInst };
using InstScores = std::array<float, kNumInst>;   // one per 0.48 s hop (patch centre = 0.48 * (i + 1) s)

struct SubPart
{
    std::string name;            // "Horns", "Piano / Keys", "Synth Pad" ...
    int inst = -1;               // Inst, or -1 = unlabelled
    float confidence = 0.0f;     // mean pooled score of the winning group over its notes (0..1)
    bool fromName = false;       // label came from the stem's file name, not the model
    std::vector<Note> notes;
};

const char* instName (int inst, int part, double medianNoteSeconds = 0.0);
int instGmProgram (int inst, int part);
int instFromFileName (const std::string& fileName);   // "Trumpets.wav" -> kInstHorns, -1 when the name says nothing

class InstrumentTagger
{
public:
    bool load (ort::Runtime&, const std::string& modelPath, int threads, std::string& err);
    bool loaded() const { return sess.loaded(); }
    // mono 44.1 kHz in -> pooled group scores per 0.48 s hop
    bool tag (const std::vector<float>& mono44k, std::vector<InstScores>& out, std::string& err, bool rawPool = false);
    // raw 521-class scores (tests)
    bool tagRaw (const std::vector<float>& mono16k, std::vector<std::vector<float>>& out, std::string& err,
                 std::vector<std::vector<float>>* groups = nullptr);
private:
    ort::Session sess;
};

// which groups can label which part (bass part: bass guitar / upright / synth; drums: none)
bool instAllowed (int inst, int part);
// splits a part's notes into labelled sub-parts. forcedInst >= 0 labels everything with it (file-name hint).
// otherPart: for melody vs chords from one stem, pass the part they share the audio with so a lead / comping prior can split two
// instruments that score together. Tiny groups (< 8 % of the notes and < 4 notes) are folded into the biggest one.
std::vector<SubPart> labelNotes (const std::vector<Note>& notes, const std::vector<InstScores>& scores, int part, int forcedInst = -1,
                                 bool sharedStem = false);
} // namespace rw
