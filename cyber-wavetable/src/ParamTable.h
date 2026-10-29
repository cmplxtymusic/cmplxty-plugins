#pragma once
// Parameters are described by one table (PDef) per plug-in; this builds the JUCE layout from it with unit-aware text in/out.
#include <juce_audio_processors/juce_audio_processors.h>

namespace cw
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
    auto t = s.removeCharacters (" ").toLowerCase();
    if (t.endsWith ("hz")) t = t.dropLastCharacters (2);
    const float v = t.retainCharacters ("0123456789.-+").getFloatValue();
    return t.endsWithChar ('k') ? v * 1000.0f : v;
}
inline juce::String fmt (float v, const juce::String& unit)
{
    if (unit == "int") return juce::String (juce::roundToInt (v));
    if (unit == "%")  return juce::String (juce::roundToInt (v * 100.0f));
    if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + "k" : juce::String (v < 100.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)));
    if (unit == "s")  return juce::String (v, v < 10.0f ? 2 : 1);
    if (unit == "ms") return v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v));
    if (unit == "dB" || unit == "st" || unit == "ct")
    {
        const float r = std::round (v * 10.0f) / 10.0f + 0.0f;   // + 0.0f turns -0.0 into 0.0
        return (r > 0.0f ? "+" : "") + juce::String (r, 1);
    }
    return juce::String (v, 2);
}
inline float parse (const juce::String& s, const juce::String& unit) { return unit == "%" ? num (s) / 100.0f : num (s); }
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
            // unit "Hz-off": a frequency whose minimum means the feature is off (shown and typed as "Off")
            const bool offAtMin = String (d.unit) == "Hz-off";
            const String unit (offAtMin ? "Hz" : d.unit);
            const String label (unit == "int" ? "" : unit);
            const float lo = d.lo;
            auto a = AudioParameterFloatAttributes().withLabel (label)
                         .withStringFromValueFunction ([unit, offAtMin, lo] (float v, int) { return offAtMin && v <= lo + 0.5f ? String ("Off") : ptab::fmt (v, unit); })
                         .withValueFromStringFunction ([unit, offAtMin, lo] (const String& s) { return offAtMin && s.trim().equalsIgnoreCase ("off") ? lo : ptab::parse (s, unit); });
            p.push_back (std::make_unique<AudioParameterFloat> (pid, d.name, NormalisableRange<float> (d.lo, d.hi, d.step, d.skew), d.def, a));
        }
    }
    return { p.begin(), p.end() };
}
} // namespace cw
