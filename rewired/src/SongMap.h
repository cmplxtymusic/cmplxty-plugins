#pragma once
// CMPLXTY Rewired 1.1.0 MAP mode: a song map (stencil) of a reference track.
//   - beat grid + first downbeat, the bar count, and the sections (Intro / Build / Drop / Break / Outro)
//   - one lane per instrument (stems, drum pieces, instrument sub-parts, a vocal lane) with a block wherever that part plays
// Plain C++17, no JUCE. Built from the engine's Result plus the separated (or loaded) stem audio, see buildSongMap() in RewiredEngine.h.
#include <string>
#include <vector>

namespace rw
{
enum SectionType { kSecIntro = 0, kSecBuild, kSecDrop, kSecBreak, kSecOutro, kNumSecTypes };
inline const char* sectionTypeName (int t)
{
    static const char* n[] = { "Intro", "Build", "Drop", "Break", "Outro" };
    return (t >= 0 && t < kNumSecTypes) ? n[t] : "Section";
}

// lane groups: drawn in their own grey (the UI is monochrome) and kept together in the exported file
enum MapGroup { kGrpDrums = 0, kGrpBass, kGrpLead, kGrpChords, kGrpMusic, kGrpVocals, kNumMapGroups };

struct MapBlock { double start = 0, end = 0; int velocity = 100; };   // seconds in the reference audio; velocity = how loud / busy

struct MapLane
{
    std::string name;               // "Drums", "Kick", "Bass", "Chords - Strings", "Vocals" ...
    int group = kGrpDrums;
    int part = -1, sub = -1;        // real MIDI source: Result::parts[part] or Result::subs[part][sub]; -1 = none / see midiParts
    std::vector<int> midiParts;     // stem lanes: the parts merged for "real MIDI" (Drums -> Kick..Cymbals, Music -> Chords ...)
    bool fromAudio = false;         // activity measured on stem audio (otherwise on the transcribed notes)
    bool child = false;             // shown indented under the lane above it
    int drumNote = -1;              // GM drum note for the stencil blocks (channel 10); -1 = melodic lane
    int stem = -1;                  // fromAudio lanes: which Preview stem holds its audio (SOLO); -1 = none
    std::vector<MapBlock> blocks;
    std::vector<unsigned char> barOn;   // per bar (bar 0 = first downbeat): 1 = plays in that bar
};

// Per-stem loudness over time (mean power per 10 ms frame). Kept with the result (and in the saved project) so the map can be
// re-gridded on another tempo without the audio. ~18 k values per stem for a 3-minute song.
struct MapEnvelope
{
    static constexpr double kFps = 100.0;
    int group = kGrpDrums;          // the lane it measures
    int stem = -1;                  // Preview stem index of the same audio
    std::vector<float> pow;
};
struct MapFeatures
{
    std::vector<MapEnvelope> env;
    bool leadIsVoice = false;       // stems mode: the melody stem is a voice (it is the vocal lane, there is no lead lane)
    const MapEnvelope* get (int group) const { for (auto& e : env) if (e.group == group && ! e.pow.empty()) return &e; return nullptr; }
};

struct MapSection { int type = kSecIntro; std::string label; int startBar = 0, endBar = 0; };

struct SongMap
{
    bool ok = false;
    double bpm = 120.0;             // whole bpm: refined on the whole song (phase-coherent), or the one the user set
    double autoBpm = 0.0;           // the tempo AUTO gives (= bpm unless the user set one); not saved
    double firstDownbeat = 0.0;     // seconds into the reference audio where bar 1 starts
    int bars = 0;
    double buildMs = 0.0;           // time taken to build the map (tests)
    std::vector<MapSection> sections;
    std::vector<MapLane> lanes;
    double barSeconds() const { return 240.0 / bpm; }
    double barTime (double bar) const { return firstDownbeat + bar * barSeconds(); }
    double toBar (double seconds) const { return (seconds - firstDownbeat) / barSeconds(); }
};
} // namespace rw
