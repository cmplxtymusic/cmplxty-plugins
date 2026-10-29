#pragma once
// CMPLXTY Visualizer - factory presets (compact "id=value;..." specs over the defaults) and user preset folders.
#include "Params.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace vz::presets
{
struct Factory
{
    juce::String name, group, spec, text;   // text: optional lyric lines ("\n" separated) for text presets
};

const std::vector<Factory>& factory();
ParamValues defaults();
int paramIndex (const juce::String& id);                         // -1 if unknown
float parseValue (int param, const juce::String& value);         // choice names, on/off, numbers
juce::String formatValue (int param, float value);               // inverse of parseValue (choice names)
// Applies "id=value;id=value" over `base`. Returns the number of keys that were not understood.
int applySpec (ParamValues& base, const juce::String& spec);
ParamValues valuesFor (const Factory& f);
// Params a preset never changes (performance / engine settings that belong to the user's setup).
bool isGlobalParam (int param);

juce::File dataDir();      // .../CMPLXTY/Visualizer
juce::File userDir();      // .../Presets  (*.cvzpreset)
juce::File imagesDir();    // .../Images   (copies of images you load)
juce::Array<juce::File> userPresets();
constexpr const char* kExt = ".cvzpreset";
} // namespace vz::presets
