#pragma once
#include "Components.h"
#include "UpdateBanner.h"
#include <juce_dsp/juce_dsp.h>

class QuartzEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit QuartzEditor (QuartzProcessor&);
    ~QuartzEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;
    void enableDemo (bool on) { ctx.demo = on; }
    void tickForTest() { timerCallback(); }
    // update notice (see UpdateCheck.h); public so the tests can drive it
    qzui::UpdateBanner* updateBanner() { return banner; }
    void showUpdateNotice (const qz::upd::Result& r, bool ignoreSkip = false);
    void startUpdateCheck (bool force = false, const juce::String& urlOverride = {});
    void showUpdateMenu();
    static constexpr int kW = 1100, kH = 720;

private:
    struct Backdrop : juce::Component { void paint (juce::Graphics&) override; Backdrop() { setInterceptsMouseClicks (false, false); setOpaque (true); } };
    struct Footer : juce::Component
    {
        qzui::Ctx* ctx = nullptr; juce::String hint, right;
        Footer() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    };
    struct Root : juce::Component {};
    struct VersionButton : juce::Component   // the "QUARTZ v1.0.3" footer text: click for update settings
    {
        std::function<void()> onClick; qzui::Ctx* ctx = nullptr;
        VersionButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics& g) override { if (isMouseOver()) { g.setColour (juce::Colour (0xff888888)); g.fillRect (0, getHeight() - 6, getWidth(), 1); } }
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); if (ctx) ctx->hint ("Click for update settings (check for updates on / off, check now)"); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); if (ctx) ctx->hint ({}); }
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateSpectrum();
    template <class T> T* add (T* c, int x, int y, int w, int h) { owned.add (c); root.addAndMakeVisible (c); c->setBounds (x, y, w, h); return c; }

    QuartzProcessor& proc;
    qzui::Ctx ctx;
    ui::QuartzLookAndFeel laf;
    juce::ComponentBoundsConstrainer constrainer;
    Root root;
    Backdrop backdrop;
    Footer footer;
    juce::OwnedArray<juce::Component> owned;
    qzui::KnobComp* gem = nullptr;
    qzui::MeterComp *meterIn = nullptr, *meterOut = nullptr;
    qzui::TextButtonComp *btnAB = nullptr, *btnUndo = nullptr, *btnBypass = nullptr;
    std::unique_ptr<juce::ParameterAttachment> bypassAtt;
    qzui::UpdateBanner* banner = nullptr;
    VersionButton* versionBtn = nullptr;
    qz::upd::Checker checker;
    juce::dsp::FFT fft { 10 };
    std::array<float, 1024> window {};
    std::array<float, 2048> fftData {};
    float smoothIn[2] { 0, 0 }, smoothOut[2] { 0, 0 };
    int tick = 0;
    bool rendererForced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuartzEditor)
};
