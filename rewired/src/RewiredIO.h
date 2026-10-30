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
