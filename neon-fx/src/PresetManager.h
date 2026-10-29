#pragma once
// Factory + user presets. Factory presets are compiled in (Presets.cpp: name + the values that differ from the defaults, in real
// units). User presets are small XML files in <user data>/CMPLXTY/<plug-in>/Presets - the installer never touches that folder.
// The current preset name is stored in the plug-in state (property "presetName"), so it comes back with the session.
#include <juce_audio_processors/juce_audio_processors.h>
#include "UpdateCheck.h"
#include <map>

namespace cw
{
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> values; };
const std::vector<FactoryPreset>& factoryPresets();

class PresetManager
{
public:
    static constexpr const char* kExt = ".cmplxtypreset";
    std::function<void (juce::XmlElement&)> saveExtra;            // optional: extra non-parameter data (e.g. sample kits)
    std::function<void (const juce::XmlElement*)> loadExtra;

    PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um, juce::String pluginName)
        : apvts (s), undo (um), plugin (std::move (pluginName)) {}

    static juce::File userDir() { return upd::dataDir().getChildFile ("Presets"); }
    juce::Array<juce::File> userFiles() const
    {
        auto files = userDir().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExt);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }

    int numFactory() const { return (int) factoryPresets().size(); }
    juce::String currentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
    int changeCount() const noexcept { return changes; }

    bool loadFactory (int i)
    {
        const auto& f = factoryPresets();
        if (i < 0 || i >= (int) f.size()) return false;
        std::map<juce::String, float> m;
        for (auto& v : f[(size_t) i].values) m[v.first] = v.second;
        beginLoad();
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                auto it = m.find (r->getParameterID());
                set (*r, it != m.end() ? r->convertTo0to1 (it->second) : r->getDefaultValue());
            }
        if (loadExtra) loadExtra (nullptr);
        finishLoad (f[(size_t) i].name);
        return true;
    }

    bool loadFile (const juce::File& file)
    {
        if (! file.existsAsFile() || file.getSize() > 96 * 1024 * 1024) return false;
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("CMPLXTY_PRESET") || xml->getStringAttribute ("plugin") != plugin) return false;
        beginLoad();
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                float v = r->getDefaultValue();
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == r->getParameterID())
                    {
                        const auto real = (float) e->getDoubleAttribute ("value", r->convertFrom0to1 (v));
                        if (std::isfinite (real)) v = r->convertTo0to1 (real);
                    }
                set (*r, v);
            }
        if (loadExtra) loadExtra (xml->getChildByName ("EXTRA"));
        finishLoad (xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), true);
        return true;
    }

    bool saveFile (const juce::File& file, const juce::String& name)
    {
        juce::XmlElement x ("CMPLXTY_PRESET");
        x.setAttribute ("plugin", plugin); x.setAttribute ("name", name); x.setAttribute ("version", CW_VERSION_STRING);
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                auto* e = x.createNewChildElement ("PARAM");
                e->setAttribute ("id", r->getParameterID()); e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue()));
            }
        if (saveExtra) saveExtra (*x.createNewChildElement ("EXTRA"));
        file.getParentDirectory().createDirectory();
        if (! x.writeTo (file)) return false;
        apvts.state.setProperty ("presetName", name, nullptr); apvts.state.setProperty ("presetUser", true, nullptr); ++changes;
        return true;
    }
    juce::File userFileFor (const juce::String& name) const
    {
        auto clean = juce::File::createLegalFileName (name.trim()).substring (0, 64);
        return userDir().getChildFile ((clean.isEmpty() ? juce::String ("Preset") : clean) + kExt);
    }

    // prev / next walks factory presets then user presets, wrapping around
    void step (int dir)
    {
        const auto users = userFiles();
        const int total = numFactory() + users.size();
        if (total == 0) return;
        int cur = indexOfCurrent (users);
        cur = cur < 0 ? (dir > 0 ? 0 : total - 1) : (cur + dir + total) % total;
        if (cur < numFactory()) loadFactory (cur); else loadFile (users[cur - numFactory()]);
    }
    int indexOfCurrent (const juce::Array<juce::File>& users) const
    {
        const auto n = currentName(); const bool isUser = apvts.state.getProperty ("presetUser", false);
        if (! isUser) for (int i = 0; i < numFactory(); ++i) if (n == factoryPresets()[(size_t) i].name) return i;
        for (int i = 0; i < users.size(); ++i) if (users[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (n) || users[i].getFileNameWithoutExtension() == n) return numFactory() + i;
        return -1;
    }

private:
    void beginLoad() { if (undo != nullptr) undo->beginNewTransaction ("Load preset"); }
    void set (juce::RangedAudioParameter& r, float v01) { r.beginChangeGesture(); r.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01)); r.endChangeGesture(); }
    void finishLoad (const juce::String& name, bool user = false)
    {
        apvts.state.setProperty ("presetName", name, nullptr);
        apvts.state.setProperty ("presetUser", user, nullptr);
        ++changes;
    }
    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager* undo;
    juce::String plugin;
    int changes = 0;
};
} // namespace cw
