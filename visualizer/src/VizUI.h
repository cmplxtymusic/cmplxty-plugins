#pragma once
// CMPLXTY Visualizer - the plug-in specific parts of the editor: render engine (own thread, frame cap), the stage
// (canvas + mode strip + meters), the pop-out / fullscreen performance window and the right-column panels.
#include "PluginEditor.h"
#include "Visual.h"

namespace vzui
{
void setParamGesture (VisualizerProcessor&, int p, float plain);
// Preset chooser: factory presets grouped by category, then user presets. `withAuto` adds an "Automatic" (empty name) entry.
juce::PopupMenu presetMenu (VisualizerProcessor&, const juce::String& current, std::function<void (const juce::String&)> pick, bool withAuto = false);
// Handles dropped / chosen files: images (.jpg/.png), fonts (.ttf/.otf), presets (.cvzpreset), lyrics (.txt). Returns a short note.
juce::String importFiles (VisualizerProcessor&, const juce::StringArray& paths);
bool isImportable (const juce::String& path);

//==============================================================================================================
class RenderEngine final : private juce::Thread
{
public:
    explicit RenderEngine (VisualizerProcessor&);
    ~RenderEngine() override;
    void start();
    void stop();
    bool running() const { return isThreadRunning(); }
    void setAssets (std::shared_ptr<const vz::Assets>);
    void setAspect (float a) { aspect.store (juce::jlimit (0.3f, 4.0f, a)); }
    void setActive (bool b) { active.store (b); }
    juce::Image frame() const;
    vz::ana::Snapshot snapshot() const;
    int frameCounter() const { return frames.load(); }
    float fps() const { return measuredFps.load(); }
    float renderMs() const { return lastMs.load(); }
    // One frame: drain the analysis FIFOs, render, publish. Called by the thread, or by the test harness while stopped.
    void renderOnce (double dt);

private:
    void run() override;
    VisualizerProcessor& proc;
    vz::Renderer renderer;
    mutable juce::SpinLock frameLock, assetLock, snapLock;
    juce::Image front, back;
    std::shared_ptr<const vz::Assets> assets;
    vz::ana::Snapshot snap;
    std::atomic<float> aspect { 16.0f / 9.0f };
    std::atomic<bool> active { true };
    std::atomic<int> frames { 0 };
    std::atomic<float> measuredFps { 0.0f }, lastMs { 0.0f };
    double lastSr = 0;
};

class Stage;

// The separate top-level performance window (never a child of the plug-in window). It only takes the keyboard
// when you click into it; then the setlist keys, strobe, blackout and fullscreen work there.
class PopView final : public juce::Component, private juce::Timer
{
public:
    PopView (VisualizerProcessor&, RenderEngine&, Stage&);
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void fireSlot (int i);
    void showOsd (const juce::String& s) { osd = s; osdUntil = juce::Time::getMillisecondCounterHiRes() + 1600.0; repaint(); }
    bool helpShown() const { return help; }

private:
    void timerCallback() override;
    VisualizerProcessor& proc;
    RenderEngine& engine;
    Stage& stage;
    int lastFrame = -1;
    bool help = false, strobeHeld = false;
    juce::String osd;
    double osdUntil = 0, lastMouse = 0;
};

class PopWindow final : public juce::DocumentWindow
{
public:
    PopWindow (VisualizerProcessor&, RenderEngine&, Stage&);
    void closeButtonPressed() override;
    PopView view;
    Stage& stage;
};

//==============================================================================================================
// Left side of the editor: live canvas, mode browser, analysis meters. Drop images / fonts / presets anywhere here.
class Stage final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    Stage (VisualizerProcessor&, Panel&);
    ~Stage() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void togglePopOut();
    void openPopOut (bool fullscreen);
    void closePopOut();
    void setFullscreen (bool);
    bool isFullscreen() const { return fullscreen; }
    bool popOpen() const { return pop != nullptr; }
    void loadImageDialog();
    void loadFontDialog();
    void saveFrameDialog();
    void note (const juce::String& s) { panel.setNote (s); }
    void showCanvasMenu (juce::Point<int> screenPos);

    RenderEngine& engine() { return eng; }
    VisualizerProcessor& processor() { return proc; }
    Panel& getPanel() { return panel; }
    // test harness: stop the thread and render frames synchronously
    void stopEngine() { eng.stop(); }
    PopView* popView() { return pop != nullptr ? &pop->view : nullptr; }
    juce::DocumentWindow* popWindow() { return pop.get(); }
    void step (int frames, double dt);
    void rebuildAssets();

private:
    void timerCallback() override;
    VisualizerProcessor& proc;
    Panel& panel;
    RenderEngine eng;
    std::unique_ptr<juce::Component> canvas, strip, meters;
    std::unique_ptr<PopWindow> pop;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, juce::Image> imageCache;
    juce::Typeface::Ptr fontCache; juce::String fontKey;
    int builtVersion = -1;
    bool dragOver = false, fullscreen = false;
    JUCE_DECLARE_NON_COPYABLE (Stage)
};
} // namespace vzui
