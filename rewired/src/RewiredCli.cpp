// RewiredCli: command-line transcription (used by the accuracy tests, also handy for batch work).
//   RewiredCli [--models DIR] (--song FILE | --bass F --melody F --chords F --drums F) --out OUT.mid [--json OUT.json]
//              [--onset X] [--frame X] [--minms X] [--drums-sens X] [--melody-src 0|1|2] [--quantize 0..3] [--bpm X]
#include <juce_audio_formats/juce_audio_formats.h>
#include "RewiredEngine.h"
#include "RewiredIO.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::StringPairArray a; juce::String song, out, json, dump; std::array<juce::String, rw::kNumStemSlots> stems;
    rw::Settings s; juce::String models;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const juce::String k (argv[i]), v (argv[i + 1]);
        if (k == "--models") models = v; else if (k == "--song") song = v; else if (k == "--out") out = v; else if (k == "--json") json = v; else if (k == "--dump-stems") dump = v;
        else if (k == "--bass") stems[rw::kStemBass] = v; else if (k == "--melody") stems[rw::kStemMelody] = v;
        else if (k == "--chords") stems[rw::kStemChords] = v; else if (k == "--drums") stems[rw::kStemDrums] = v;
        else if (k == "--onset") s.onsetThreshold = v.getFloatValue(); else if (k == "--frame") s.frameThreshold = v.getFloatValue();
        else if (k == "--minms") s.minNoteMs = v.getFloatValue(); else if (k == "--drums-sens") s.drumSensitivity = v.getFloatValue();
        else if (k == "--melody-src") s.melodySource = v.getIntValue(); else if (k == "--quantize") s.quantize = v.getIntValue();
        else if (k == "--bpm") s.bpm = v.getFloatValue();
        else if (k == "--melgap") s.melodyGap = v.getIntValue();
        else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (out.isEmpty()) { std::fprintf (stderr, "usage: see source header\n"); return 2; }
    juce::File dir = models.isNotEmpty() ? juce::File (models) : rw::io::findModelDir();
    rw::Engine eng; std::string err;
    if (! eng.load (dir.getFullPathName().toStdString(), err)) { std::fprintf (stderr, "load: %s\n", err.c_str()); return 1; }
    auto prog = [] (float p, const char* what) { std::fprintf (stderr, "\r%3d%% %-30s", (int) (p * 100), what); };
    rw::Result r; juce::String e;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (song.isNotEmpty())
    {
        rw::Audio au; if (! rw::io::loadAudio (juce::File (song), au, e)) { std::fprintf (stderr, "%s\n", e.toRawUTF8()); return 1; }
        if (dump.isNotEmpty())
        {
            std::array<std::vector<float>, 4> st; std::string se;
            if (! eng.separate (au, st, se, nullptr, prog)) { std::fprintf (stderr, "%s\n", se.c_str()); return 1; }
            const char* nm[] = { "vocals", "drums", "bass", "other" };
            juce::File d (dump); d.createDirectory();
            for (int i = 0; i < 4; ++i)
            {
                juce::WavAudioFormat wav; auto f = d.getChildFile (juce::String (nm[i]) + ".wav"); f.deleteFile();
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100.0, 1, 24, {}, 0));
                const float* p = st[(size_t) i].data(); w->writeFromFloatArrays (&p, 1, (int) st[(size_t) i].size());
            }
        }
        r = eng.transcribeSong (au, s, nullptr, prog);
    }
    else
    {
        std::array<rw::Audio, rw::kNumStemSlots> st;
        for (int i = 0; i < rw::kNumStemSlots; ++i)
            if (stems[(size_t) i].isNotEmpty() && ! rw::io::loadAudio (juce::File (stems[(size_t) i]), st[(size_t) i], e)) { std::fprintf (stderr, "%s\n", e.toRawUTF8()); return 1; }
        r = eng.transcribeStems (st, s, nullptr, prog);
    }
    std::fprintf (stderr, "\n");
    if (! r.ok) { std::fprintf (stderr, "error: %s\n", r.error.c_str()); return 1; }
    std::vector<int> parts; for (int p = 0; p < rw::kNumParts; ++p) parts.push_back (p);
    if (! rw::io::writeMidi (juce::File::getCurrentWorkingDirectory().getChildFile (out), r, parts, "Rewired", e)) { std::fprintf (stderr, "%s\n", e.toRawUTF8()); return 1; }
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::printf ("bpm %.2f  audio %.1fs  time %.1fs  melody-from %s\n", r.bpm, r.seconds, secs, r.melodyFrom.c_str());
    for (int p = 0; p < rw::kNumParts; ++p) std::printf ("  %-8s %zu notes\n", rw::partName (p), r.parts[(size_t) p].size());
    if (json.isNotEmpty())
    {
        juce::String j; j << "{\"bpm\":" << r.bpm << ",\"seconds\":" << r.seconds << ",\"time\":" << secs << ",\"melodyFrom\":\"" << r.melodyFrom << "\"}";
        juce::File::getCurrentWorkingDirectory().getChildFile (json).replaceWithText (j);
    }
    return 0;
}
