#pragma once
// JUCE side of the engine: decode audio files, write MIDI files, find the model folder.
#include <juce_audio_formats/juce_audio_formats.h>
#include "RewiredEngine.h"

namespace rw::io
{
inline const char* kExtensions = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

// decodes and converts to 44.1 kHz stereo (mono files stay mono); limit in seconds
inline bool loadAudio (const juce::File& f, Audio& out, juce::String& err, double maxSeconds = 900.0)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { err = "Can't read " + f.getFileName(); return false; }
    if (r->sampleRate <= 0 || r->lengthInSamples <= 0) { err = f.getFileName() + " is empty"; return false; }
    if ((double) r->lengthInSamples / r->sampleRate > maxSeconds) { err = f.getFileName() + " is longer than " + juce::String ((int) (maxSeconds / 60)) + " minutes"; return false; }
    const int len = (int) r->lengthInSamples, ch = (int) juce::jlimit (1u, 2u, r->numChannels);
    juce::AudioBuffer<float> b (ch, len);
    r->read (&b, 0, len, 0, true, ch > 1);
    out.left.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    out.right.clear();
    if (ch > 1) out.right.assign (b.getReadPointer (1), b.getReadPointer (1) + len);
    if (std::abs (r->sampleRate - Engine::kSampleRate) > 0.5)
    {
        out.left = resample (out.left, r->sampleRate, Engine::kSampleRate);
        if (! out.right.empty()) out.right = resample (out.right, r->sampleRate, Engine::kSampleRate);
    }
    return true;
}

inline int gmProgram (int part) { return part == kBass ? 33 : part == kMelody ? 80 : 89; }   // 0-based: Finger Bass, Square Lead, Warm Pad

// type-1 file: track 0 = tempo / 4-4 / title, then one named track per part (drums on channel 10).
// 1.0.1: with splitInstruments, a part with instrument sub-parts is written as one track per instrument ("Chords - Strings"),
// with a matching GM program; a part with one label keeps one track, named "Chords (Piano / Keys)".
struct MidiTrack { juce::String name; int channel = 1, program = -1; const std::vector<Note>* notes = nullptr; };

inline juce::MidiFile makeMidi (const Result& r, const std::vector<MidiTrack>& tracks, const juce::String& title)
{
    constexpr int ppq = 960;
    juce::MidiFile mf; mf.setTicksPerQuarterNote (ppq);
    const double bpm = r.bpm > 0 ? r.bpm : 120.0;
    auto ticks = [&] (double sec) { return std::max (0.0, std::round (sec * bpm / 60.0 * ppq)); };
    juce::MidiMessageSequence head;
    auto nm = juce::MidiMessage::textMetaEvent (3, title); nm.setTimeStamp (0); head.addEvent (nm);
    auto tm = juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)); tm.setTimeStamp (0); head.addEvent (tm);
    auto ts = juce::MidiMessage::timeSignatureMetaEvent (4, 4); ts.setTimeStamp (0); head.addEvent (ts);
    mf.addTrack (head);
    for (const auto& t : tracks)
    {
        juce::MidiMessageSequence seq;
        const int chn = t.channel;
        auto n = juce::MidiMessage::textMetaEvent (3, t.name); n.setTimeStamp (0); seq.addEvent (n);
        if (t.program >= 0) { auto pc = juce::MidiMessage::programChange (chn, t.program); pc.setTimeStamp (0); seq.addEvent (pc); }
        for (const auto& note : *t.notes)
        {
            const int pitch = juce::jlimit (0, 127, note.pitch), v = juce::jlimit (1, 127, note.velocity);
            const double t0 = ticks (note.start), t1 = std::max (t0 + 1, ticks (note.end));
            auto on = juce::MidiMessage::noteOn (chn, pitch, (juce::uint8) v); on.setTimeStamp (t0); seq.addEvent (on);
            auto off = juce::MidiMessage::noteOff (chn, pitch); off.setTimeStamp (t1); seq.addEvent (off);
        }
        seq.sort(); seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    return mf;
}

inline std::vector<MidiTrack> partTracks (const Result& r, const std::vector<int>& parts, bool splitInstruments)
{
    std::vector<MidiTrack> t;
    int nextChn = 1;
    for (int p : parts)
    {
        const auto& subs = r.subs[(size_t) p];
        if (isDrumPart (p)) { t.push_back ({ partName (p), 10, -1, &r.parts[(size_t) p] }); continue; }
        if (splitInstruments && subs.size() > 1)
            for (const auto& sp : subs)
            {
                t.push_back ({ juce::String (partName (p)) + " - " + juce::String (sp.name), nextChn, instGmProgram (sp.inst, p), &sp.notes });
                if (++nextChn == 10) ++nextChn; if (nextChn > 16) nextChn = 1;
            }
        else
        {
            const bool one = splitInstruments && subs.size() == 1;
            t.push_back ({ one ? juce::String (partName (p)) + " (" + juce::String (subs[0].name) + ")" : juce::String (partName (p)), nextChn,
                           one ? instGmProgram (subs[0].inst, p) : gmProgram (p), &r.parts[(size_t) p] });
            if (++nextChn == 10) ++nextChn; if (nextChn > 16) nextChn = 1;
        }
    }
    return t;
}

inline juce::MidiFile makeMidi (const Result& r, const std::vector<int>& parts, const juce::String& title, bool splitInstruments = true)
{
    return makeMidi (r, partTracks (r, parts, splitInstruments), title);
}

inline bool writeTracks (const juce::File& f, const Result& r, const std::vector<MidiTrack>& tracks, const juce::String& title, juce::String& err)
{
    auto mf = makeMidi (r, tracks, title);
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.failedToOpen() || ! mf.writeTo (os, 1)) { err = "Can't write " + f.getFullPathName(); return false; }
    return true;
}

inline bool writeMidi (const juce::File& f, const Result& r, const std::vector<int>& parts, const juce::String& title, juce::String& err, bool splitInstruments = true)
{
    auto mf = makeMidi (r, parts, title, splitInstruments);
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.failedToOpen() || ! mf.writeTo (os, 1)) { err = "Can't write " + f.getFullPathName(); return false; }
    return true;
}

// ---------------------------------------------------------------------------------------------------- 1.1.0 MAP mode file
// Type-1 file for FL Studio (or any DAW): track 0 = title / tempo / 4-4 / one marker per section (+ "Song start" when the
// reference audio does not start on bar 1); then a "Sections" track (one block per section, a text event with its name);
// then one named track per lane. A lane holds an empty block (one long note) wherever that part plays, or with realMidi[lane]
// the transcribed notes instead. The map's bar 1 lands on a bar line of the file: when the reference starts later than 0 s
// the file starts with a bar of lead-in and the "Song start" marker shows where to drop the reference audio.
inline int sectionPitch (int type) { static const int p[] = { 48, 50, 52, 53, 55 }; return type >= 0 && type < kNumSecTypes ? p[type] : 48; }

inline int mapLeadInBars (const SongMap& m)
{
    return std::max (0, (int) std::ceil ((m.firstDownbeat - 0.03) / m.barSeconds()));   // bar 1 within 30 ms of the start: no lead-in
}

inline juce::MidiFile makeMapMidi (const Result& r, const std::vector<bool>& realMidi, const juce::String& title)
{
    const auto& m = r.map;
    constexpr int ppq = 960;
    juce::MidiFile mf; mf.setTicksPerQuarterNote (ppq);
    const double bpm = m.bpm > 0 ? m.bpm : 120.0, shift = mapLeadInBars (m) * m.barSeconds() - m.firstDownbeat;
    auto ticks = [&] (double sec) { return std::max (0.0, std::round ((sec + shift) * bpm / 60.0 * ppq)); };
    auto barTicks = [&] (double bar) { return ticks (m.barTime (bar)); };
    auto meta = [] (juce::MidiMessageSequence& q, int type, const juce::String& text, double t)
    { auto e = juce::MidiMessage::textMetaEvent (type, text); e.setTimeStamp (t); q.addEvent (e); };

    juce::MidiMessageSequence head;
    meta (head, 3, title + " - Map", 0);
    { auto e = juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)); e.setTimeStamp (0); head.addEvent (e); }
    { auto e = juce::MidiMessage::timeSignatureMetaEvent (4, 4); e.setTimeStamp (0); head.addEvent (e); }
    if (shift > 0.03) meta (head, 6, "Song start", ticks (0.0));
    for (auto& s : m.sections) meta (head, 6, juce::String (s.label), barTicks (s.startBar));
    meta (head, 6, "End", barTicks (m.bars));
    head.sort(); mf.addTrack (head);

    juce::MidiMessageSequence sec;
    meta (sec, 3, "Sections", 0);
    for (auto& s : m.sections)
    {
        const double t0 = barTicks (s.startBar), t1 = barTicks (s.endBar);
        meta (sec, 1, juce::String (s.label), t0);
        auto on = juce::MidiMessage::noteOn (16, sectionPitch (s.type), (juce::uint8) 100); on.setTimeStamp (t0); sec.addEvent (on);
        auto off = juce::MidiMessage::noteOff (16, sectionPitch (s.type)); off.setTimeStamp (std::max (t0 + 1, t1 - 1)); sec.addEvent (off);
    }
    sec.sort(); sec.updateMatchedPairs(); mf.addTrack (sec);

    int nextChn = 1;
    for (size_t li = 0; li < m.lanes.size(); ++li)
    {
        const auto& L = m.lanes[li];
        const bool drum = L.drumNote >= 0, real = li < realMidi.size() && realMidi[li];
        int chn = 10;
        if (! drum) { chn = nextChn; if (++nextChn == 10) ++nextChn; if (nextChn > 15) nextChn = 1; }
        juce::MidiMessageSequence q;
        meta (q, 3, juce::String (L.name), 0);
        auto addNote = [&] (int pitch, int vel, double s0, double s1)
        {
            const double t0 = ticks (s0), t1 = std::max (t0 + 1, ticks (s1));
            auto on = juce::MidiMessage::noteOn (chn, juce::jlimit (0, 127, pitch), (juce::uint8) juce::jlimit (1, 127, vel)); on.setTimeStamp (t0); q.addEvent (on);
            auto off = juce::MidiMessage::noteOff (chn, juce::jlimit (0, 127, pitch)); off.setTimeStamp (t1); q.addEvent (off);
        };
        if (real)
        {
            std::vector<const std::vector<Note>*> src;
            if (L.part >= 0) src.push_back (L.sub >= 0 && (size_t) L.sub < r.subs[(size_t) L.part].size() ? &r.subs[(size_t) L.part][(size_t) L.sub].notes : &r.parts[(size_t) L.part]);
            else for (int p : L.midiParts) src.push_back (&r.parts[(size_t) p]);
            if (! drum)
            {
                const int part = L.part >= 0 ? L.part : (L.midiParts.empty() ? (int) kChords : L.midiParts[0]);
                const int prog = L.sub >= 0 && (size_t) L.sub < r.subs[(size_t) part].size() ? instGmProgram (r.subs[(size_t) part][(size_t) L.sub].inst, part) : gmProgram (part);
                auto pc = juce::MidiMessage::programChange (chn, prog); pc.setTimeStamp (0); q.addEvent (pc);
            }
            for (auto* v : src) for (auto& n : *v) addNote (n.pitch, n.velocity, n.start, n.end);
        }
        else
            for (auto& b : L.blocks) addNote (drum ? L.drumNote : 60, b.velocity, b.start, b.end);
        q.sort(); q.updateMatchedPairs();
        mf.addTrack (q);
    }
    return mf;
}

inline bool writeMapMidi (const juce::File& f, const Result& r, const std::vector<bool>& realMidi, const juce::String& title, juce::String& err)
{
    if (! r.map.ok) { err = "No song map yet - press PROCESS first"; return false; }
    auto mf = makeMapMidi (r, realMidi, title);
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.failedToOpen() || ! mf.writeTo (os, 1)) { err = "Can't write " + f.getFullPathName(); return false; }
    return true;
}

// where the models and the ONNX Runtime library live (installed once, shared by VST3 / CLAP / Standalone)
inline juce::Array<juce::File> modelDirCandidates()
{
    juce::Array<juce::File> c;
    const auto env = juce::SystemStats::getEnvironmentVariable ("RW_MODEL_DIR", {});
    if (env.isNotEmpty()) c.add (juce::File (env));
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    c.add (self.getParentDirectory().getChildFile ("rewired-models"));
    c.add (self.getParentDirectory().getParentDirectory().getChildFile ("Resources").getChildFile ("rewired-models"));
#if JUCE_WINDOWS
    auto common = juce::SystemStats::getEnvironmentVariable ("CommonProgramW6432", juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files"));
    c.add (juce::File (common).getChildFile ("CMPLXTY").getChildFile ("Rewired"));
    c.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs\\Common\\CMPLXTY\\Rewired"));   // per-user install
#elif JUCE_MAC
    c.add (juce::File ("/Library/Application Support/CMPLXTY/Rewired"));
    c.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/CMPLXTY/Rewired"));
#else
    c.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/CMPLXTY/Rewired"));
    c.add (juce::File ("/usr/share/cmplxty-rewired"));
#endif
    return c;
}

inline juce::File findModelDir()
{
    for (auto& d : modelDirCandidates())
        if (d.getChildFile ("spleeter4.onnx").existsAsFile() && d.getChildFile ("nmp.onnx").existsAsFile()) return d;
    return {};
}
} // namespace rw::io
