// CMPLXTY Visualizer: control layout (tabs on the right), the stage on the left and the plug-in specific panels.
#include "VizUI.h"

namespace vzui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY VISUALIZER", "AUDIO-REACTIVE VISUALS  /  IMAGES  /  TEXT  /  LIVE",
        { 16, 56, 800, 632 },
        {
            // VISUAL
            { "LOOK",   nullptr, { 832, 90, 352, 72 },   { { 'c', "mode", "MODE" }, { 'c', "palette", "PALETTE" }, { 'c', "mirror", "MIRROR" } }, 132, 0, 3 },
            { "MOTION", nullptr, { 832, 170, 352, 112 }, { { 'k', "speed", "SPEED" }, { 'k', "zoom", "ZOOM" }, { 'k', "detail", "DETAIL" }, { 'k', "spin", "SPIN" } }, 132, 0 },
            { "COLOUR", nullptr, { 832, 290, 352, 112 }, { { 'k', "hue", "HUE" }, { 'k', "cycle", "CYCLE" }, { 'k', "bright", "BRIGHT" }, { 'k', "glow", "GLOW" } }, 132, 0 },
            { "FEEL",   nullptr, { 832, 410, 352, 112 }, { { 'k', "trails", "TRAILS" }, { 'k', "flash", "FLASH" }, { 'k', "react", "REACT" } }, 132, 0 },
            { "AUDIO",  nullptr, { 832, 530, 352, 120 }, { { 'k', "gain", "GAIN" }, { 'k', "smooth", "SMOOTH" }, { 'c', "beatMode", "BEAT SOURCE" }, { 't', "sync", "HOST SYNC" } }, 132, 0 },
            // IMAGE
            { "IMAGE LAYER", "imgOn", { 832, 290, 352, 124 }, { { 'k', "imgSize", "SIZE" }, { 'k', "imgOpacity", "OPACITY" }, { 'k', "imgSpin", "SPIN" }, { 'c', "imgBlend", "BLEND" }, { 'c', "imgCycle", "CYCLE" } }, 120, 1 },
            { "IMAGE REACT", nullptr, { 832, 422, 352, 112 }, { { 'k', "imgPulse", "PULSE" }, { 'k', "imgGlow", "GLOW" }, { 'k', "imgShake", "SHAKE" }, { 'k', "imgChroma", "CHROMA" } }, 132, 1 },
            // TEXT
            { "TEXT OVERLAY", "textOver", { 832, 360, 352, 124 }, { { 'k', "textSize", "SIZE" }, { 'k', "textGlitch", "GLITCH" }, { 'c', "textAnim", "ANIMATION" }, { 'c', "textCycle", "CYCLE" } }, 140, 2 },
            // FX
            { "TEXTURE", "texOn", { 832, 90, 352, 112 },  { { 'k', "texAmt", "AMOUNT" }, { 'c', "texType", "TYPE" } }, 160, 3 },
            { "DITHER",  "ditOn", { 832, 210, 352, 112 }, { { 'k', "ditSize", "SIZE" }, { 'k', "ditMix", "MIX" }, { 'c', "ditType", "TYPE" } }, 140, 3 },
            { "TRACK",   "trkOn", { 832, 330, 352, 112 }, { { 'k', "trkAmt", "AMOUNT" }, { 'c', "trkStyle", "STYLE" } }, 160, 3 },
            { "GLASS",   "glsOn", { 832, 450, 352, 112 }, { { 'k', "glsAmt", "AMOUNT" }, { 'c', "glsType", "TYPE" } }, 160, 3 },
            // GRID
            { "MULTI-GRID", nullptr, { 832, 90, 352, 120 }, { { 'k', "gridGap", "GAP" }, { 'c', "grid", "LAYOUT" }, { 'c', "gridLink", "LINK" } }, 200, 4 },
            // PERFORM
            { "PERFORMANCE", nullptr, { 832, 90, 352, 120 }, { { 't', "strobe", "STROBE" }, { 't', "blackout", "BLACKOUT" }, { 'c', "strobeRate", "STROBE RATE" },
                                                               { 'c', "quality", "QUALITY" }, { 'c', "fps", "FRAME RATE" } }, 132, 5, 3 },
        },
        "speed",
        { "VISUAL", "IMAGE", "TEXT", "FX", "GRID", "PERFORM" },
        { 832, 56, 352, 26 }
    };
    return s;
}

namespace
{
const juce::Colour kBg (0xff0c0c0c), kPanel (0xff181818), kPri (0xffffffff), kSec (0xffb8b8b8), kDim (0xff9f9f9f), kTrack (0xff2d2d2d);

void drawBtn (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool on = false, bool enabled = true, float fontH = 10.5f)
{
    auto f = r.toFloat().reduced (1.0f);
    g.setColour (on ? kPri : kPanel); g.fillRoundedRectangle (f, 4.0f);
    g.setColour (kPri.withAlpha (on ? 1.0f : (enabled ? 0.35f : 0.12f))); g.drawRoundedRectangle (f.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (on ? kBg : (enabled ? kSec : kDim.withAlpha (0.4f))); g.setFont (uiFont (fontH, true));
    g.drawFittedText (text, r.reduced (4, 0), juce::Justification::centred, 1);
}

void drawFrame (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (kPanel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (kPri.withAlpha (0.35f)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
    g.setColour (kSec.withAlpha (0.9f)); g.fillRect (r.getX() + 1, r.getY() + 8, 2, 12);
    g.setColour (kDim); g.setFont (uiFont (12.0f, true));
    g.drawText (title, r.getX() + 10, r.getY() + 4, r.getWidth() - 20, 16, juce::Justification::centredLeft);
}

juce::String shortName (const juce::String& preset) { return preset.startsWith ("user:") ? preset.substring (5) : preset; }

Stage* stageOf (Panel& p) { return dynamic_cast<Stage*> (p.display()); }

void showMenu (juce::PopupMenu& m, Panel& panel, juce::Component* target)
{
    m.setLookAndFeel (&panel.look);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withMousePosition(),
                     [sp = juce::Component::SafePointer<Panel> (&panel)] (int) { if (sp != nullptr) hostkeys::releaseLater (*sp); });
}

//==============================================================================================================
class PresetBar final : public juce::Component, private juce::Timer
{
public:
    PresetBar (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn) { startTimerHz (5); }
    juce::Rectangle<int> prevR() const { return { 0, 0, 26, getHeight() }; }
    juce::Rectangle<int> nameR() const { return { 28, 0, getWidth() - 28 - 28 - 62, getHeight() }; }
    juce::Rectangle<int> nextR() const { return { getWidth() - 62 - 28, 0, 26, getHeight() }; }
    juce::Rectangle<int> saveR() const { return { getWidth() - 60, 0, 60, getHeight() }; }
    void paint (juce::Graphics& g) override
    {
        drawBtn (g, prevR(), "<", false, true, 13.0f); drawBtn (g, nextR(), ">", false, true, 13.0f); drawBtn (g, saveR(), "SAVE");
        auto r = nameR().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff121212)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kPri.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (kPri); g.setFont (uiFont (12.5f, true));
        g.drawFittedText (shortName (shown).toUpperCase(), nameR().reduced (10, 0).withTrimmedRight (14), juce::Justification::centredLeft, 1);
        juce::Path tri; const float cx = r.getRight() - 12.0f, cy = r.getCentreY();
        tri.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (kSec); g.fillPath (tri);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        if (prevR().contains (pos)) step (-1);
        else if (nextR().contains (pos)) step (1);
        else if (saveR().contains (pos)) saveDialog();
        else if (nameR().contains (pos)) openMenu();
    }
    void step (int d)
    {
        const auto names = proc.allPresetNames();
        if (names.isEmpty()) return;
        const int cur = names.indexOf (proc.currentPresetName());
        const int i = cur < 0 ? (d > 0 ? 0 : names.size() - 1) : (cur + d + names.size()) % names.size();
        proc.applyPreset (names[i]);
        repaint();
    }
    void openMenu()
    {
        juce::Component::SafePointer<PresetBar> sp (this);
        auto m = presetMenu (proc, proc.currentPresetName(), [sp] (const juce::String& n) { if (sp != nullptr) { sp->proc.applyPreset (n); sp->repaint(); } });
        m.addSeparator();
        m.addItem ("Save preset...", [sp] { if (sp != nullptr) sp->saveDialog(); });
        m.addItem ("Load preset file...", [sp] { if (sp != nullptr) sp->loadDialog(); });
        m.addItem ("Open presets folder", [] { auto d = vz::presets::userDir(); d.createDirectory(); d.startAsProcess(); });
        showMenu (m, panel, this);
    }
    void saveDialog()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name your preset. \"With images\" stores the loaded pictures inside the preset file.", juce::MessageBoxIconType::NoIcon);
        w->setLookAndFeel (&panel.look);
        auto nm = shortName (proc.currentPresetName());
        w->addTextEditor ("name", nm == "Init" ? juce::String ("My preset") : nm);
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("SAVE WITH IMAGES", 2);
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBar> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r)
        {
            std::unique_ptr<juce::AlertWindow> owned (w);
            if (sp == nullptr) return;
            if (r > 0)
            {
                const auto name = w->getTextEditorContents ("name").trim();
                const auto f = sp->proc.saveUserPreset (name.isEmpty() ? juce::String ("My preset") : name, r == 2);
                sp->panel.setNote (f.existsAsFile() ? "Saved preset " + f.getFileNameWithoutExtension() + (r == 2 ? " (with images)" : "") : juce::String ("Could not save the preset"));
            }
            hostkeys::releaseLater (sp->panel);
            sp->repaint();
        }), false);
    }
    void loadDialog()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a Visualizer preset", vz::presets::userDir(), juce::String ("*") + vz::presets::kExt);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [sp = juce::Component::SafePointer<PresetBar> (this)] (const juce::FileChooser& fc)
            {
                if (sp == nullptr) return;
                if (fc.getResult().existsAsFile()) sp->panel.setNote (importFiles (sp->proc, { fc.getResult().getFullPathName() }));
                hostkeys::releaseLater (sp->panel);
            });
    }
private:
    void timerCallback() override { const auto n = proc.currentPresetName(); if (n != shown) { shown = n; repaint(); } }
    VisualizerProcessor& proc;
    Panel& panel;
    juce::String shown;
    std::unique_ptr<juce::FileChooser> chooser;
};

class HeaderButtons final : public juce::Component, private juce::Timer
{
public:
    HeaderButtons (Panel& pn) : panel (pn) { startTimerHz (4); }
    juce::Rectangle<int> b (int i) const { return { i * getWidth() / 2, 0, getWidth() / 2 - 4, getHeight() }; }
    void paint (juce::Graphics& g) override
    {
        auto* st = stageOf (panel);
        drawBtn (g, b (0), st != nullptr && st->popOpen() ? "CLOSE POP-OUT" : "POP OUT", st != nullptr && st->popOpen());
        drawBtn (g, b (1), "FULLSCREEN", st != nullptr && st->isFullscreen());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* st = stageOf (panel);
        if (st == nullptr) return;
        if (b (0).contains (e.getPosition())) st->togglePopOut();
        else if (b (1).contains (e.getPosition())) st->setFullscreen (! st->isFullscreen());
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    Panel& panel;
};

//==============================================================================================================
class ImageStrip final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    ImageStrip (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn) { startTimerHz (5); sync(); }
    juce::Rectangle<int> cell (int i) const { const int cw = (getWidth() - 20) / 4; return { 10 + (i % 4) * cw, 26 + (i / 4) * 64, cw - 6, 58 }; }
    juce::Rectangle<int> btn (int i) const { const int y = getHeight() - 34, w = (getWidth() - 20) / 3; return { 10 + i * w, y, w - 6, 26 }; }
    void paint (juce::Graphics& g) override
    {
        drawFrame (g, getLocalBounds(), "IMAGES  " + juce::String ((int) thumbs.size()) + " / " + juce::String (vz::kMaxImages));
        for (int i = 0; i < vz::kMaxImages; ++i)
        {
            auto r = cell (i);
            g.setColour (juce::Colour (0xff101010)); g.fillRoundedRectangle (r.toFloat(), 4.0f);
            if (i < (int) thumbs.size())
            {
                const auto& t = thumbs[(size_t) i];
                if (t.isValid())
                {
                    const float s = juce::jmin ((float) (r.getWidth() - 4) / (float) t.getWidth(), (float) (r.getHeight() - 4) / (float) t.getHeight());
                    auto d = juce::Rectangle<float> ((float) t.getWidth() * s, (float) t.getHeight() * s).withCentre (r.toFloat().getCentre());
                    g.drawImage (t, d);
                }
                g.setColour (i == selected ? kPri : kPri.withAlpha (0.3f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, i == selected ? 2.0f : 1.0f);
                g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRect (r.getX() + 2, r.getY() + 2, 16, 13);
                g.setColour (kPri); g.setFont (uiFont (9.5f, true)); g.drawText (juce::String (i + 1), r.getX() + 2, r.getY() + 2, 16, 13, juce::Justification::centred);
            }
            else
            {
                g.setColour (kPri.withAlpha (dragOver ? 0.6f : 0.12f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
                if (i == (int) thumbs.size()) { g.setColour (kDim); g.setFont (uiFont (10.0f, true)); g.drawText ("+ DROP", r, juce::Justification::centred); }
            }
        }
        drawBtn (g, btn (0), "LOAD IMAGE", false, (int) thumbs.size() < vz::kMaxImages);
        drawBtn (g, btn (1), "REMOVE", false, ! thumbs.empty());
        drawBtn (g, btn (2), "CLEAR ALL", false, ! thumbs.empty());
        g.setColour (kDim); g.setFont (uiFont (9.5f));
        g.drawText ("JPG / PNG. Click to use, right-click for more. Saved with the project.", 10, btn (0).getY() - 18, getWidth() - 20, 14, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        const int n = (int) thumbs.size();
        if (btn (0).contains (pos)) { if (auto* st = stageOf (panel)) st->loadImageDialog(); return; }
        if (btn (1).contains (pos)) { if (n > 0) proc.removeImage (selected); return; }
        if (btn (2).contains (pos)) { if (n > 0) proc.clearImages(); panel.setNote ("Images cleared"); return; }
        for (int i = 0; i < n; ++i)
            if (cell (i).contains (pos))
            {
                if (! e.mods.isPopupMenu()) { proc.setSelectedImage (i); setImgOn(); return; }
                juce::PopupMenu m;
                juce::Component::SafePointer<ImageStrip> sp (this);
                m.addItem ("Use this image", [sp, i] { if (sp != nullptr) { sp->proc.setSelectedImage (i); sp->setImgOn(); } });
                m.addItem ("Move left", i > 0, false, [sp, i] { if (sp != nullptr) sp->proc.moveImage (i, i - 1); });
                m.addItem ("Move right", i < n - 1, false, [sp, i] { if (sp != nullptr) sp->proc.moveImage (i, i + 1); });
                m.addSeparator();
                m.addItem ("Remove", [sp, i] { if (sp != nullptr) sp->proc.removeImage (i); });
                showMenu (m, panel, this);
                return;
            }
        if (cell (n).contains (pos) && n < vz::kMaxImages) if (auto* st = stageOf (panel)) st->loadImageDialog();
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { for (auto& s : f) if (isImportable (s)) return true; return false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { dragOver = false; panel.setNote (importFiles (proc, f)); setImgOn(); }
private:
    void setImgOn() { if (proc.value (vz::pImgOn) < 0.5f && ! proc.getExtras().images.empty()) setParamGesture (proc, vz::pImgOn, 1.0f); }
    void sync()
    {
        ver = proc.extrasVersion();
        const auto ex = proc.getExtras();
        selected = ex.selectedImage;
        thumbs.clear();
        for (auto& im : ex.images)
        {
            auto src = juce::ImageFileFormat::loadFrom (im.data.getData(), im.data.getSize());
            if (src.isValid()) src = src.rescaled (juce::jmax (1, src.getWidth() * 60 / juce::jmax (1, src.getHeight())), 60, juce::Graphics::mediumResamplingQuality);
            thumbs.push_back (src);
        }
        repaint();
    }
    void timerCallback() override { if (proc.extrasVersion() != ver) sync(); }
    VisualizerProcessor& proc;
    Panel& panel;
    std::vector<juce::Image> thumbs;
    int ver = -1, selected = 0;
    bool dragOver = false;
};

//==============================================================================================================
class TextPanel final : public juce::Component, private juce::Timer
{
public:
    TextPanel (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn)
    {
        ed.getProperties().set ("vzTypingTarget", true);     // the one place the plug-in window takes the keyboard: while you type here
        ed.setMultiLine (true, true);
        ed.setReturnKeyStartsNewLine (true);
        ed.setScrollbarsShown (true);
        ed.setFont (uiFont (13.0f));
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101010));
        ed.setColour (juce::TextEditor::textColourId, kPri);
        ed.setColour (juce::TextEditor::outlineColourId, kPri.withAlpha (0.3f));
        ed.setColour (juce::TextEditor::focusedOutlineColourId, kPri);
        ed.setColour (juce::TextEditor::highlightColourId, kPri.withAlpha (0.3f));
        ed.setColour (juce::CaretComponent::caretColourId, kPri);
        ed.setTextToShowWhenEmpty ("Type lyrics or words here - one line per cycle", kDim);
        ed.onTextChange = [this] { pending = true; lastEdit = juce::Time::getMillisecondCounterHiRes(); };
        ed.onEscapeKey = [this] { commit(); juce::Component::unfocusAllComponents(); hostkeys::releaseLater (panel); };
        ed.onFocusLost = [this] { commit(); hostkeys::releaseLater (panel); };
        addAndMakeVisible (ed);
        startTimerHz (10);
        sync (true);
    }
    juce::Rectangle<int> btn (int i) const { const int y = getHeight() - 52, w = (getWidth() - 20) / 3; return { 10 + i * w, y, w - 6, 26 }; }
    void resized() override { ed.setBounds (10, 26, getWidth() - 20, getHeight() - 26 - 60); }
    void paint (juce::Graphics& g) override
    {
        drawFrame (g, getLocalBounds(), "TEXT / LYRICS");
        drawBtn (g, btn (0), "LOAD .TXT");
        drawBtn (g, btn (1), "LOAD FONT");
        drawBtn (g, btn (2), "DEFAULT FONT", false, fontName.isNotEmpty());
        g.setColour (kDim); g.setFont (uiFont (10.0f));
        g.drawText ("FONT: " + (fontName.isNotEmpty() ? fontName : juce::String ("built-in")) + "    -    ESC: done typing",
                    10, getHeight() - 22, getWidth() - 20, 16, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        if (btn (0).contains (pos))
        {
            chooser = std::make_unique<juce::FileChooser> ("Load lyrics (.txt)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.txt");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [sp = juce::Component::SafePointer<TextPanel> (this)] (const juce::FileChooser& fc)
                {
                    if (sp == nullptr) return;
                    if (fc.getResult().existsAsFile()) { sp->panel.setNote (importFiles (sp->proc, { fc.getResult().getFullPathName() })); sp->sync (true); }
                    hostkeys::releaseLater (sp->panel);
                });
        }
        else if (btn (1).contains (pos)) { if (auto* st = stageOf (panel)) st->loadFontDialog(); }
        else if (btn (2).contains (pos)) { proc.clearFont(); panel.setNote ("Built-in font"); }
    }
private:
    void commit()
    {
        if (! pending) return;
        pending = false;
        juce::StringArray lines; lines.addLines (ed.getText()); lines.removeEmptyStrings (true);
        proc.setLines (lines, false);
        ver = proc.extrasVersion();
    }
    void sync (bool force)
    {
        const auto ex = proc.getExtras();
        ver = proc.extrasVersion();
        if (fontName != ex.fontName) { fontName = ex.fontName; repaint(); }
        if (force || ! ed.hasKeyboardFocus (true))
        {
            const auto t = ex.lines.joinIntoString ("\n");
            if (t != ed.getText()) ed.setText (t, false);
        }
    }
    void timerCallback() override
    {
        if (pending && juce::Time::getMillisecondCounterHiRes() - lastEdit > 400.0) commit();
        if (proc.extrasVersion() != ver && ! pending) sync (false);
    }
    VisualizerProcessor& proc;
    Panel& panel;
    juce::TextEditor ed;
    juce::String fontName;
    std::unique_ptr<juce::FileChooser> chooser;
    bool pending = false;
    double lastEdit = 0;
    int ver = -1;
};

//==============================================================================================================
class TileMap final : public juce::Component, private juce::Timer
{
public:
    TileMap (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn) { startTimerHz (5); }
    const vz::GridDef& grid() const { return vz::kGrids[juce::jlimit (0, vz::kNumGrids - 1, (int) proc.value (vz::pGrid))]; }
    juce::Rectangle<int> mapArea() const { return { 10, 26, getWidth() - 20, getHeight() - 26 - 64 }; }
    juce::Rectangle<int> tile (int i) const
    {
        const auto& gd = grid(); auto a = mapArea();
        const int c = i % gd.cols, r = i / gd.cols;
        return { a.getX() + a.getWidth() * c / gd.cols, a.getY() + a.getHeight() * r / gd.rows,
                 a.getWidth() * (c + 1) / gd.cols - a.getWidth() * c / gd.cols, a.getHeight() * (r + 1) / gd.rows - a.getHeight() * r / gd.rows };
    }
    juce::Rectangle<int> btn (int i) const { const int y = getHeight() - 34, w = (getWidth() - 20) / 3; return { 10 + i * w, y, w - 6, 26 }; }
    void paint (juce::Graphics& g) override
    {
        const auto& gd = grid();
        drawFrame (g, getLocalBounds(), "TILES  " + juce::String (gd.cols) + " x " + juce::String (gd.rows));
        const auto tiles = proc.getExtras().tiles;
        if (gd.cols * gd.rows <= 1)
        {
            g.setColour (kDim); g.setFont (uiFont (11.5f));
            g.drawFittedText ("Pick a LAYOUT above (up to 6 x 6). Every tile can run its own preset; empty tiles play an automatic variation of the main look. "
                              "LINK decides how the tiles move together.", mapArea().reduced (20), juce::Justification::centred, 6);
        }
        else
            for (int i = 0; i < gd.cols * gd.rows; ++i)
            {
                auto r = tile (i).reduced (2);
                const bool set = tiles[(size_t) i].isNotEmpty();
                g.setColour (set ? kTrack : juce::Colour (0xff111111)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
                g.setColour (kPri.withAlpha (set ? 0.6f : 0.2f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
                g.setColour (set ? kPri : kDim); g.setFont (uiFont (gd.cols >= 5 ? 8.5f : 10.0f, set));
                g.drawFittedText (set ? shortName (tiles[(size_t) i]).toUpperCase() : juce::String ("AUTO"), r.reduced (3), juce::Justification::centred, 3, 0.7f);
            }
        drawBtn (g, btn (0), "ALL AUTO");
        drawBtn (g, btn (1), "RANDOM");
        drawBtn (g, btn (2), "FILL CURRENT");
        g.setColour (kDim); g.setFont (uiFont (9.5f));
        g.drawText ("Click a tile to choose its preset.", 10, btn (0).getY() - 18, getWidth() - 20, 14, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        const int n = grid().cols * grid().rows;
        if (btn (0).contains (pos)) { for (int i = 0; i < vz::kMaxTiles; ++i) proc.setTile (i, {}); return; }
        if (btn (1).contains (pos))
        {
            const auto& fs = vz::presets::factory();
            if (n <= 1) { setParamGesture (proc, vz::pGrid, 3.0f); }
            const int m = juce::jmax (4, grid().cols * grid().rows);
            for (int i = 0; i < m; ++i) proc.setTile (i, fs[(size_t) (1 + juce::Random::getSystemRandom().nextInt ((int) fs.size() - 1))].name);
            return;
        }
        if (btn (2).contains (pos)) { const auto cur = proc.currentPresetName(); for (int i = 0; i < juce::jmax (1, n); ++i) proc.setTile (i, cur); return; }
        if (n <= 1) return;
        for (int i = 0; i < n; ++i)
            if (tile (i).contains (pos))
            {
                juce::Component::SafePointer<TileMap> sp (this);
                auto m = presetMenu (proc, proc.getExtras().tiles[(size_t) i], [sp, i] (const juce::String& name) { if (sp != nullptr) sp->proc.setTile (i, name); }, true);
                showMenu (m, panel, this);
                return;
            }
    }
private:
    void timerCallback() override
    {
        const int v = proc.extrasVersion(), gi = (int) proc.value (vz::pGrid);
        if (v != ver || gi != lastGrid) { ver = v; lastGrid = gi; repaint(); }
    }
    VisualizerProcessor& proc;
    Panel& panel;
    int ver = -1, lastGrid = -1;
};

//==============================================================================================================
class Setlist final : public juce::Component, private juce::Timer
{
public:
    Setlist (VisualizerProcessor& p, Panel& pn) : proc (p), panel (pn) { startTimerHz (5); }
    juce::Rectangle<int> cell (int i) const
    {
        const int top = 26, h = getHeight() - top - 100, w = getWidth() - 20;
        const int c = i % 6, r = i / 6;
        return { 10 + w * c / 6, top + h * r / 6, w * (c + 1) / 6 - w * c / 6, h * (r + 1) / 6 - h * r / 6 };
    }
    juce::Rectangle<int> btn (int i) const { const int y = getHeight() - 72, w = (getWidth() - 20) / 3; return { 10 + i * w, y, w - 6, 26 }; }
    void paint (juce::Graphics& g) override
    {
        drawFrame (g, getLocalBounds(), "SETLIST  -  KEYS IN THE POP-OUT WINDOW");
        const auto slots = proc.getExtras().slots;
        const int act = proc.activeSlot.load();
        for (int i = 0; i < vz::kNumSlots; ++i)
        {
            auto r = cell (i).reduced (2);
            const bool set = slots[(size_t) i].isNotEmpty(), on = i == act;
            g.setColour (on ? kPri : set ? kTrack : juce::Colour (0xff111111)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (kPri.withAlpha (on ? 1.0f : set ? 0.5f : 0.15f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (on ? kBg : kPri); g.setFont (uiFont (11.0f, true));
            g.drawText (juce::String::charToString (vz::kSlotKeys[i]), r.getX() + 4, r.getY() + 2, 14, 13, juce::Justification::centredLeft);
            if (set)
            {
                g.setColour (on ? kBg : kSec); g.setFont (uiFont (8.5f));
                g.drawFittedText (shortName (slots[(size_t) i]).toUpperCase(), r.withTrimmedTop (14).reduced (2, 1), juce::Justification::centred, 2, 0.7f);
            }
        }
        drawBtn (g, btn (0), "AUTO-FILL");
        drawBtn (g, btn (1), "CLEAR ALL");
        drawBtn (g, btn (2), "OPEN POP-OUT");
        g.setColour (kDim); g.setFont (uiFont (9.5f));
        g.drawFittedText ("Click a slot to play it, right-click to assign. In the pop-out (click into it first): slot keys, LEFT/RIGHT step, "
                          "UP/DOWN mode, hold ENTER strobe, BACKSPACE blackout, F11 fullscreen, F1 help. The plug-in window never takes your DAW's keys.",
                          juce::Rectangle<int> (10, getHeight() - 42, getWidth() - 20, 38), juce::Justification::topLeft, 3, 0.9f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        if (btn (0).contains (pos))
        {
            const auto& fs = vz::presets::factory();
            const auto slots = proc.getExtras().slots;
            int next = 1;
            for (int i = 0; i < vz::kNumSlots; ++i)
                if (slots[(size_t) i].isEmpty() && next < (int) fs.size()) { proc.setSlot (i, fs[(size_t) next].name); next += juce::jmax (1, ((int) fs.size() - 1) / vz::kNumSlots); }
            panel.setNote ("Empty slots filled with factory presets");
            return;
        }
        if (btn (1).contains (pos)) { for (int i = 0; i < vz::kNumSlots; ++i) proc.setSlot (i, {}); proc.activeSlot.store (-1); return; }
        if (btn (2).contains (pos)) { if (auto* st = stageOf (panel)) st->openPopOut (false); return; }
        for (int i = 0; i < vz::kNumSlots; ++i)
            if (cell (i).contains (pos))
            {
                const auto name = proc.getExtras().slots[(size_t) i];
                if (! e.mods.isPopupMenu())
                {
                    if (name.isNotEmpty() && proc.applyPreset (name)) proc.activeSlot.store (i);
                    else if (name.isEmpty()) panel.setNote ("Empty slot - right-click to assign a preset");
                    repaint();
                    return;
                }
                juce::Component::SafePointer<Setlist> sp (this);
                juce::PopupMenu m;
                m.addSubMenu ("Assign preset", presetMenu (proc, name, [sp, i] (const juce::String& n) { if (sp != nullptr) sp->proc.setSlot (i, n); }));
                const auto cur = proc.currentPresetName();
                m.addItem ("Assign current (" + shortName (cur) + ")", [sp, i, cur] { if (sp != nullptr) sp->proc.setSlot (i, cur); });
                m.addItem ("Clear slot", name.isNotEmpty(), false, [sp, i] { if (sp != nullptr) sp->proc.setSlot (i, {}); });
                showMenu (m, panel, this);
                return;
            }
    }
private:
    void timerCallback() override
    {
        const int v = proc.extrasVersion(), a = proc.activeSlot.load();
        if (v != ver || a != lastAct) { ver = v; lastAct = a; repaint(); }
    }
    VisualizerProcessor& proc;
    Panel& panel;
    int ver = -1, lastAct = -2;
};

class InfoNote final : public juce::Component
{
public:
    explicit InfoNote (juce::String t) : text (std::move (t)) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (kDim); g.setFont (uiFont (10.5f));
        g.drawFittedText (text, getLocalBounds().reduced (6, 2), juce::Justification::topLeft, 6, 0.9f);
    }
    juce::String text;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (VisualizerProcessor& p, Panel& panel) { return std::make_unique<Stage> (p, panel); }

std::vector<TabExtra> makeTabExtras (VisualizerProcessor& p, Panel& panel)
{
    std::vector<TabExtra> v;
    v.push_back ({ -1, { 460, 8, 360, 28 }, std::make_unique<PresetBar> (p, panel) });
    v.push_back ({ -1, { 832, 8, 236, 28 }, std::make_unique<HeaderButtons> (panel) });
    v.push_back ({ 1, { 832, 90, 352, 192 }, std::make_unique<ImageStrip> (p, panel) });
    v.push_back ({ 1, { 832, 544, 352, 100 }, std::make_unique<InfoNote> (
        "Images follow the music: PULSE (size on the beat), GLOW, SHAKE and CHROMA split. BLEND sets how the picture sits on the visuals - "
        "over or behind, multiply, screen, as a MASK cut-out or as a TEXTURE the mode is painted with. The IMAGE modes (VISUAL > MODE) are built around your picture.") });
    v.push_back ({ 2, { 832, 90, 352, 262 }, std::make_unique<TextPanel> (p, panel) });
    v.push_back ({ 2, { 832, 492, 352, 120 }, std::make_unique<InfoNote> (
        "The TEXT modes and the overlay show one line at a time. CYCLE steps lines on beats or bars, ANIMATION picks pulse, glitch, typewriter, flash, wave, zoom or shake. "
        "Drop a .ttf / .otf to use your own font - it is saved with the project.") });
    v.push_back ({ 3, { 832, 570, 352, 80 }, std::make_unique<InfoNote> (
        "FX run after the mode and the image, in this order: GLASS, TEXTURE, DITHER, TRACK. Each one has its own switch and can be automated.") });
    v.push_back ({ 4, { 832, 218, 352, 432 }, std::make_unique<TileMap> (p, panel) });
    v.push_back ({ 5, { 832, 218, 352, 432 }, std::make_unique<Setlist> (p, panel) });
    return v;
}

void demoDisplay (juce::Component* c)
{
    if (auto* st = dynamic_cast<Stage*> (c)) { st->stopEngine(); st->step (20, 1.0 / 30.0); }
}
} // namespace vzui
