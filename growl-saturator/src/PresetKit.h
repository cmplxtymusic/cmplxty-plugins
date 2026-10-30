#pragma once
// CMPLXTY preset kit (shared by the retrofitted plug-ins): factory + user presets and a preset bar that never takes keyboard focus.
// Factory presets are compiled in (Presets.cpp: name + values that differ from the defaults, in real units). User presets are
// small XML files in <user data>/CMPLXTY/<plug-in>/Presets; the installer never touches that folder. The current preset name
// lives in the ValueTree state (properties presetName / presetUser) so plug-ins can store it with the session.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace pk
{
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> values; };
const std::vector<FactoryPreset>& factoryPresets();

class PresetManager
{
public:
    static constexpr const char* kExt = ".cmplxtypreset";
    juce::StringArray keepIds;   // display / preference parameters that presets never change (e.g. an eco mode)
    PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um, juce::String pluginName, juce::File dataDir, juce::String version)
        : apvts (s), undo (um), plugin (std::move (pluginName)), dir (dataDir.getChildFile ("Presets")), ver (std::move (version)) {}

    juce::File userDir() const { return dir; }
    juce::Array<juce::File> userFiles() const
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExt);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }
    int numFactory() const { return (int) factoryPresets().size(); }
    juce::String currentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
    bool currentIsUser() const { return apvts.state.getProperty ("presetUser", false); }
    void setCurrent (const juce::String& name, bool user) { apvts.state.setProperty ("presetName", name, nullptr); apvts.state.setProperty ("presetUser", user, nullptr); ++changes; }
    int changeCount() const noexcept { return changes; }

    bool loadFactory (int i)
    {
        const auto& f = factoryPresets();
        if (i < 0 || i >= (int) f.size()) return false;
        std::map<juce::String, float> m;
        for (auto& v : f[(size_t) i].values) m[v.first] = v.second;
        beginLoad();
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && ! keepIds.contains (r->getParameterID()))
            {
                auto it = m.find (r->getParameterID());
                set (*r, it != m.end() ? r->convertTo0to1 (it->second) : r->getDefaultValue());
            }
        setCurrent (f[(size_t) i].name, false);
        return true;
    }
    bool loadFile (const juce::File& file)
    {
        if (! file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) return false;
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("CMPLXTY_PRESET") || xml->getStringAttribute ("plugin") != plugin) return false;
        beginLoad();
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && ! keepIds.contains (r->getParameterID()))
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
        setCurrent (xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), true);
        return true;
    }
    bool saveFile (const juce::File& file, const juce::String& name)
    {
        juce::XmlElement x ("CMPLXTY_PRESET");
        x.setAttribute ("plugin", plugin); x.setAttribute ("name", name); x.setAttribute ("version", ver);
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && r->getParameterID() != "bypass")
            {
                auto* e = x.createNewChildElement ("PARAM");
                e->setAttribute ("id", r->getParameterID()); e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue()));
            }
        file.getParentDirectory().createDirectory();
        if (! x.writeTo (file)) return false;
        setCurrent (name, true);
        return true;
    }
    juce::File userFileFor (const juce::String& name) const
    {
        auto clean = juce::File::createLegalFileName (name.trim()).substring (0, 64);
        return dir.getChildFile ((clean.isEmpty() ? juce::String ("Preset") : clean) + kExt);
    }
    juce::File nextFreeUserFile() const { for (int i = 1;; ++i) { auto f = userFileFor ("User " + juce::String (i)); if (! f.exists()) return f; } }
    void step (int d)
    {
        const auto users = userFiles(); const int total = numFactory() + users.size();
        if (total == 0) return;
        int cur = indexOfCurrent (users);
        cur = cur < 0 ? (d > 0 ? 0 : total - 1) : (cur + d + total) % total;
        if (cur < numFactory()) loadFactory (cur); else loadFile (users[cur - numFactory()]);
    }
    int indexOfCurrent (const juce::Array<juce::File>& users) const
    {
        const auto n = currentName();
        if (! currentIsUser()) for (int i = 0; i < numFactory(); ++i) if (n == factoryPresets()[(size_t) i].name) return i;
        for (int i = 0; i < users.size(); ++i) if (users[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (n) || users[i].getFileNameWithoutExtension() == n) return numFactory() + i;
        return -1;
    }
private:
    void beginLoad() { if (undo != nullptr) undo->beginNewTransaction ("Load preset"); }
    static void set (juce::RangedAudioParameter& r, float v01) { r.beginChangeGesture(); r.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01)); r.endChangeGesture(); }
    juce::AudioProcessorValueTreeState& apvts; juce::UndoManager* undo; juce::String plugin; juce::File dir; juce::String ver; int changes = 0;
};

// One generic menu entry; each plug-in maps these onto its own popup overlay.
struct MenuItem { juce::String text; std::function<void()> action; bool enabled = true, ticked = false, separatorAbove = false; };

// Mouse-only menu overlay for the preset list: lays items out in columns when they do not fit, never takes keyboard focus.
class PresetMenu : public juce::Component
{
public:
    juce::Colour bg { 0xff1c1c1c }, line { 0xff5a5a5a }, text { 0xffe8e8e8 }, dim { 0xff8a8a8a }, hi { 0xff3a3a3a }, tick { 0xffffffff };
    std::function<juce::Font (float)> font = [] (float h) { return juce::Font (juce::FontOptions (h)); };
    std::function<void()> onDismiss;
    PresetMenu() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); setVisible (false); setAlwaysOnTop (true); }
    bool isOpen() const noexcept { return open; }
    void show (juce::Rectangle<int> anchor, std::vector<MenuItem> v)
    {
        items = std::move (v); rows.clear();
        const int rowH = 22, avail = juce::jmax (rowH * 4, getHeight() - 16);
        const auto f = font (12.5f); int colW = 170;
        for (auto& it : items) colW = juce::jmax (colW, (int) juce::GlyphArrangement::getStringWidth (f, it.text) + 40);
        int x = 0, y = 0, cols = 1;
        for (auto& it : items)
        {
            const int need = rowH + (it.separatorAbove && y > 0 ? 7 : 0);
            if (y + need > avail - 8 && y > 0) { x += colW; y = 0; ++cols; }
            if (it.separatorAbove && y > 0) y += 7;
            rows.push_back ({ x + 4, y + 4, colW - 8, rowH }); y += rowH;
        }
        int maxY = 0; for (auto& r : rows) maxY = juce::jmax (maxY, r.getBottom());
        panel = { 0, 0, cols * colW, maxY + 4 };
        panel.setPosition (juce::jlimit (4, juce::jmax (4, getWidth() - panel.getWidth() - 4), anchor.getCentreX() - panel.getWidth() / 2),
                           juce::jlimit (4, juce::jmax (4, getHeight() - panel.getHeight() - 4), anchor.getBottom() + 2));
        for (auto& r : rows) r.translate (panel.getX(), panel.getY());
        hover = -1; open = true; setVisible (true); toFront (false); repaint();
    }
    int pressed = -1;
    void dismiss() { if (! open) return; open = false; setVisible (false); if (onDismiss) onDismiss(); }
    int numItems() const { return (int) items.size(); }
    void triggerForTest (int i) { if (i >= 0 && i < (int) items.size()) trigger (i); }
    juce::Rectangle<int> panelBounds() const { return panel; }
    void paint (juce::Graphics& g) override
    {
        if (! open) return;
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (panel.translated (2, 3).toFloat(), 5.0f);
        g.setColour (bg); g.fillRoundedRectangle (panel.toFloat(), 5.0f);
        g.setColour (line); g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setFont (font (12.5f));
        for (size_t i = 0; i < items.size(); ++i)
        {
            auto& it = items[i]; auto r = rows[i];
            if (it.separatorAbove && r.getY() > panel.getY() + 6) { g.setColour (dim.withAlpha (0.4f)); g.fillRect (r.getX() + 4, r.getY() - 4, r.getWidth() - 8, 1); }
            if ((int) i == hover && it.enabled) { g.setColour (hi); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
            if (it.ticked) { juce::Path t; const float x0 = (float) r.getX() + 7, cy = (float) r.getCentreY(); t.startNewSubPath (x0, cy); t.lineTo (x0 + 3.5f, cy + 4); t.lineTo (x0 + 10, cy - 4); g.setColour (tick); g.strokePath (t, juce::PathStrokeType (2.0f)); }
            g.setColour (it.enabled ? text : dim); g.drawText (it.text, r.withTrimmedLeft (24), juce::Justification::centredLeft, true);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = itemAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
    // Standard menu behaviour: a right-click anywhere just closes it; a left click picks an item on release, and only when it was
    // pressed on that same item (so a stray drag never saves, renames or opens a dialog).
    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = -1;
        if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) { dismiss(); return; }
        const int i = itemAt (e.getPosition());
        if (i >= 0 && items[(size_t) i].enabled) pressed = i; else dismiss();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = itemAt (e.getPosition());
        const bool fire = pressed >= 0 && i == pressed && e.getDistanceFromDragStart() < 6;
        pressed = -1;
        if (fire) trigger (i);
    }
    bool hitTest (int, int) override { return open; }
private:
    int itemAt (juce::Point<int> p) const { for (size_t i = 0; i < rows.size(); ++i) if (rows[i].contains (p)) return (int) i; return -1; }
    void trigger (int i) { auto fn = items[(size_t) i].action; dismiss(); if (fn) fn(); }
    std::vector<MenuItem> items; std::vector<juce::Rectangle<int>> rows; juce::Rectangle<int> panel; int hover = -1; bool open = false;
};

// "<  [preset name]  >" bar. Clicking the name opens the preset menu (factory list, user list, save / save as / load / folder).
// Nothing here ever takes keyboard focus; file dialogs are async and hand focus back to the host when they close.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    std::function<void (std::vector<MenuItem>, juce::Rectangle<int> anchorInBar)> showMenu;
    std::function<void()> afterDialog;   // e.g. hostkeys::releaseLater (editor)
    juce::Colour bg { 0xff141414 }, line { 0xff3a3a3a }, text { 0xffe8e8e8 }, dim { 0xff8a8a8a };
    std::function<juce::Font (float)> font = [] (float h) { return juce::Font (juce::FontOptions (h)); };

    explicit PresetBar (PresetManager& m) : pm (m)
    {
        for (auto* b : { &prev, &next }) { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false); }
        prev.onClick = [this] { pm.step (-1); repaint(); };
        next.onClick = [this] { pm.step (+1); repaint(); };
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
        startTimerHz (5);
    }
    void resized() override { auto r = getLocalBounds(); prev.setBounds (r.removeFromLeft (getHeight())); next.setBounds (r.removeFromRight (getHeight())); nameArea = r.reduced (4, 0); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (bg); g.fillRoundedRectangle (nameArea.toFloat(), 3.0f);
        g.setColour (line); g.drawRoundedRectangle (nameArea.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (text); g.setFont (font (12.0f));
        g.drawText (pm.currentName(), nameArea.reduced (8, 0), juce::Justification::centred, true);
        g.setColour (dim); g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), nameArea.reduced (8, 0), juce::Justification::centredRight, false);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (nameArea.contains (e.getPosition())) openMenu(); }
    juce::Rectangle<int> nameBounds() const { return nameArea; }
    std::vector<MenuItem> menuItems()
    {
        std::vector<MenuItem> v; const auto users = pm.userFiles(); const int cur = pm.indexOfCurrent (users);
        for (int i = 0; i < pm.numFactory(); ++i)
        { MenuItem it { factoryPresets()[(size_t) i].name, [this, i] { pm.loadFactory (i); repaint(); } }; it.ticked = cur == i; if (i == 0) it.separatorAbove = false; v.push_back (it); }
        for (int i = 0; i < users.size(); ++i)
        { auto f = users[i]; MenuItem it { "User: " + f.getFileNameWithoutExtension(), [this, f] { pm.loadFile (f); repaint(); } }; it.ticked = cur == pm.numFactory() + i; it.separatorAbove = i == 0; v.push_back (it); }
        MenuItem s { "Save user preset", [this] { auto f = pm.currentIsUser() ? pm.userFileFor (pm.currentName()) : pm.nextFreeUserFile(); pm.saveFile (f, f.getFileNameWithoutExtension()); repaint(); } };
        s.separatorAbove = true; v.push_back (s);
        v.push_back ({ "Save as...", [this] { saveAs(); } });
        v.push_back ({ "Load preset file...", [this] { loadAs(); } });
        v.push_back ({ "Open preset folder", [this] { pm.userDir().createDirectory(); pm.userDir().startAsProcess(); } });
        return v;
    }
    void openMenu() { if (showMenu) showMenu (menuItems(), nameArea); }
private:
    void timerCallback() override { if (pm.changeCount() != seen) { seen = pm.changeCount(); repaint(); } }
    void saveAs()
    {
        pm.userDir().createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save preset", pm.userFileFor (pm.currentName()), juce::String ("*") + PresetManager::kExt);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& c) { auto f = c.getResult(); if (f != juce::File()) { f = f.withFileExtension (PresetManager::kExt); pm.saveFile (f, f.getFileNameWithoutExtension()); } repaint(); if (afterDialog) afterDialog(); });
    }
    void loadAs()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load preset", pm.userDir(), juce::String ("*") + PresetManager::kExt);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& c) { auto f = c.getResult(); if (f.existsAsFile()) pm.loadFile (f); repaint(); if (afterDialog) afterDialog(); });
    }
    PresetManager& pm;
    juce::TextButton prev { "<" }, next { ">" };
    juce::Rectangle<int> nameArea;
    std::unique_ptr<juce::FileChooser> chooser;
    int seen = -1;
};
} // namespace pk
