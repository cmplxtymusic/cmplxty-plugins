// Factory + user presets for the two independent sections. User presets are plain XML files in
// %APPDATA%\Quartz\Presets\{Quartz|ALTER}\*.qzp (upgrades never touch that folder).
#pragma once
#include <juce_core/juce_core.h>
#include "FactoryPresets.h"
#include "ParamTable.h"

namespace qz
{
enum Section { secQuartz = 0, secAlter = 1 };

inline int sectionFirst (int s) { return s == secQuartz ? pIn : pMode; }
inline int sectionLast  (int s) { return s == secQuartz ? pIntensity : pAOut; }
inline bool inSectionPreset (int s, int p)   // params stored in a section preset (never the on/off diamond or master)
{
    if (p == pQon || p == pAon || p == pBypass) return false;
    return sectionOf (p) == s;
}
inline const FactoryPreset* factoryList (int s) { return s == secQuartz ? kQuartzPresets : kAlterPresets; }
inline int factoryCount (int s) { return s == secQuartz ? 48 : 40; }

struct UserPreset { juce::String name; juce::File file; };

class PresetStore
{
public:
    static juce::File rootDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Quartz").getChildFile ("Presets");
    }
    static juce::File sectionDir (int s) { return rootDir().getChildFile (s == secQuartz ? "Quartz" : "ALTER"); }

    void rescan (int s)
    {
        auto& v = user[s]; v.clear();
        auto files = sectionDir (s).findChildFiles (juce::File::findFiles, false, "*.qzp");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), f });
    }
    int numFactory (int s) const { return factoryCount (s); }
    int numTotal (int s) const { return factoryCount (s) + (int) user[s].size(); }
    juce::String nameAt (int s, int i) const
    {
        if (i < factoryCount (s)) return factoryList (s)[i].name;
        return user[s][(size_t) (i - factoryCount (s))].name;
    }
    bool isUser (int s, int i) const { return i >= factoryCount (s); }
    int indexOfName (int s, const juce::String& n) const
    {
        for (int i = 0; i < numTotal (s); ++i) if (nameAt (s, i) == n) return i;
        return -1;
    }
    const std::vector<UserPreset>& users (int s) const { return user[s]; }

    // Fills values[] (only section params). Returns false if a user file is unreadable.
    bool read (int s, int i, float* values, int count) const
    {
        for (int p = 0; p < count; ++p) if (inSectionPreset (s, p)) values[p] = kParams[p].def;
        if (i < 0 || i >= numTotal (s)) return false;
        if (i < factoryCount (s))
        {
            const auto& fp = factoryList (s)[i];
            for (int k = 0; k < fp.count; ++k) values[fp.e[k].index] = fp.e[k].value;
            return true;
        }
        auto xml = juce::XmlDocument::parse (user[s][(size_t) (i - factoryCount (s))].file);
        if (xml == nullptr || ! xml->hasTagName ("QuartzPreset")) return false;
        for (auto* e : xml->getChildIterator())
        {
            const auto id = e->getStringAttribute ("id");
            for (int p = 0; p < count; ++p)
                if (inSectionPreset (s, p) && id == kParams[p].id) values[p] = juce::jlimit (kParams[p].mn, kParams[p].mx, (float) e->getDoubleAttribute ("v", kParams[p].def));
        }
        return true;
    }
    // Writes a user preset; returns its new index (or -1).
    int save (int s, const juce::String& rawName, const float* values, int count)
    {
        auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty()) return -1;
        sectionDir (s).createDirectory();
        auto f = sectionDir (s).getChildFile (name + ".qzp");
        juce::XmlElement xml ("QuartzPreset");
        xml.setAttribute ("section", s == secQuartz ? "quartz" : "alter");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (int p = 0; p < count; ++p)
            if (inSectionPreset (s, p)) { auto* e = xml.createNewChildElement ("P"); e->setAttribute ("id", kParams[p].id); e->setAttribute ("v", (double) values[p]); }
        if (! xml.writeTo (f)) return -1;
        rescan (s);
        return indexOfName (s, name);
    }
    bool removeUser (int s, int i)
    {
        if (! isUser (s, i)) return false;
        const bool ok = user[s][(size_t) (i - factoryCount (s))].file.deleteFile();
        rescan (s); return ok;
    }
private:
    std::vector<UserPreset> user[2];
};
} // namespace qz
