#pragma once
// Parameters are described by one table (PDef) per plug-in; this builds the JUCE layout from it with unit-aware text in/out.
#include <juce_audio_processors/juce_audio_processors.h>

namespace slt
{
struct PDef
{
    const char* id; const char* name;
    char type;                        // 'f' float, 'b' bool, 'c' choice
    float lo, hi, step, skew, def;    // for 'c': def = default index; for 'b': def 0/1
    const char* unit;                 // "%" (0..1 shown as 0..100), "dB", "Hz", "ms", "s", "st", "ct", "" ...
    const char* choices = "";         // 'c': "A|B|C"
};

namespace ptab
{
inline float num (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    const float v = t.retainCharacters ("0123456789.-+").getFloatValue();
    return (t.endsWithChar ('k') || t.endsWith ("khz")) ? v * 1000.0f : v;
}
// MIDI note names with C3 = 60 (the CMPLXTY / Ableton convention; FL Studio calls the same key C5)
inline juce::String noteText (float v)
{
    static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = juce::roundToInt (v); const int ct = juce::roundToInt ((v - (float) n) * 100.0f);
    juce::String s = juce::String (nm[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 2);
    if (ct != 0) s << (ct > 0 ? " +" : " ") << ct << "ct";
    return s;
}
inline float noteParse (const juce::String& in)
{
    const auto t = in.trim().toUpperCase();
    if (t.isEmpty() || ! juce::String ("ABCDEFG").containsChar (t[0])) return num (in);
    static const int pc[] = { 9, 11, 0, 2, 4, 5, 7 };
    int n = pc[t[0] - 'A']; int i = 1;
    if (t[i] == '#') { ++n; ++i; } else if (t[i] == 'B' && t.length() > i + 1) { --n; ++i; }
    const auto rest = t.substring (i).trim();
    const int oct = rest.isEmpty() ? 3 : rest.upToFirstOccurrenceOf (" ", false, false).getIntValue();
    float cents = 0; if (rest.containsChar (' ')) cents = rest.fromFirstOccurrenceOf (" ", false, false).retainCharacters ("0123456789.-+").getFloatValue();
    return (float) ((oct + 2) * 12 + n) + cents / 100.0f;
}
inline juce::String fmt (float v, const juce::String& unit)
{
    if (unit == "note") return noteText (v);
    if (unit == "int") return juce::String (juce::roundToInt (v));
    if (unit == "/s") return v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
    if (unit == "x" || unit == ":1") return juce::String (v, unit == "x" ? 2 : 1);
    if (unit == "%")  return juce::String (juce::roundToInt (v * 100.0f));
    if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + "k" : juce::String (v < 100.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)));
    if (unit == "s")  return juce::String (v, v < 10.0f ? 2 : 1);
    if (unit == "BPM") return v < 20.0f ? juce::String ("AUTO") : juce::String (v, 1);   // below 20 = follow the host / the loops
    if (unit == "ms") return v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
    if (unit == "dB" || unit == "st" || unit == "ct")
    {
        const float r = std::round (v * 10.0f) / 10.0f + 0.0f;   // + 0.0f turns -0.0 into 0.0
        return (r > 0.0f ? "+" : "") + juce::String (r, 1);
    }
    return juce::String (v, 2);
}
inline float parse (const juce::String& s, const juce::String& unit) { if (unit == "note") return noteParse (s); if (unit == "BPM" && s.trim().startsWithIgnoreCase ("a")) return 0.0f; return unit == "%" ? num (s) / 100.0f : num (s); }
} // namespace ptab

template <size_t N>
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (const PDef (&defs)[N])
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    for (auto& d : defs)
    {
        const ParameterID pid { d.id, 1 };
        if (d.type == 'b') p.push_back (std::make_unique<AudioParameterBool> (pid, d.name, d.def > 0.5f));
        else if (d.type == 'c') p.push_back (std::make_unique<AudioParameterChoice> (pid, d.name, StringArray::fromTokens (d.choices, "|", ""), (int) d.def));
        else
        {
            const String unit (d.unit);
            auto a = AudioParameterFloatAttributes().withLabel (unit == "note" || unit == "int" ? String() : unit)
                         .withStringFromValueFunction ([unit] (float v, int) { return ptab::fmt (v, unit); })
                         .withValueFromStringFunction ([unit] (const String& s) { return ptab::parse (s, unit); });
            p.push_back (std::make_unique<AudioParameterFloat> (pid, d.name, NormalisableRange<float> (d.lo, d.hi, d.step, d.skew), d.def, a));
        }
    }
    return { p.begin(), p.end() };
}
} // namespace slt
