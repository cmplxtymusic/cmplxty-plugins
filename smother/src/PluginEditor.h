#pragma once
#include "PluginProcessor.h"

class SmotherLook final : public juce::LookAndFeel_V4
{
  public:
    SmotherLook();
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    juce::Typeface::Ptr face, bold;
};

class GridView final : public juce::Component
{
  public:
    explicit GridView(SmotherProcessor &);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;
    bool sleeveMode = false;

    // Layout geometry, used by hit testing.
    juce::Rectangle<int> rowBounds(int visualRow) const;
    juce::Rectangle<int> stepCell(int visualRow, int step, int length) const;
    int headerW = 132, allRows = 1;

  private:
    enum DragKind
    {
        None,
        Step,
        NewSleeve,
        MoveSleeve,
        ResizeL,
        ResizeR
    };
    SmotherProcessor &proc;
    DragKind drag = None;
    int downRow = -1, downStep = -1, dragSleeve = -1, dragAll = 0;
    int startStart = 0, startLen = 0;
    int lastToggle = -1;
};

class DragChip final : public juce::Component
{
  public:
    DragChip(SmotherProcessor &, bool midi);
    void paint(juce::Graphics &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    juce::String lastError;

  private:
    SmotherProcessor &proc;
    bool midi = true, dragging = false;
};

class SampleDrop final : public juce::Component, public juce::FileDragAndDropTarget
{
  public:
    explicit SampleDrop(SmotherProcessor &);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    bool isInterestedInFileDrag(const juce::StringArray &) override;
    void filesDropped(const juce::StringArray &, int, int) override;
    bool hover = false;

  private:
    SmotherProcessor &proc;
};

class SmotherEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit SmotherEditor(SmotherProcessor &);
    ~SmotherEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    void timerCallback() override;
    void file(bool save);
    void syncInspector();
    void writeInspector();
    void layoutKnob(juce::Slider &s, juce::Label &l, juce::Rectangle<int> r);
    SmotherProcessor &proc;
    SmotherLook look;
    std::array<juce::Slider, smother::Count> knobs;
    std::array<juce::Label, smother::Count> labels;
    std::array<juce::ComboBox, smother::Count> boxes;
    std::vector<std::unique_ptr<SA>> attachments;
    std::vector<std::unique_ptr<CA>> choices;
    juce::ComboBox presets;
    juce::TextButton save{ "SAVE" }, load{ "LOAD" };
    juce::TextButton addLane{ "+ LANE" }, remLane{ "- LANE" };
    juce::TextButton stepMode{ "STEPS" }, sleeveMode{ "SLEEVES" };
    juce::TextButton delSleeve{ "REMOVE SLEEVE" };
    juce::TextButton clearSample{ "CLEAR SAMPLE" };
    juce::Label status, inspTitle, laneTitle;
    GridView grid;
    DragChip midiChip, wavChip;
    SampleDrop drop;
    // Per-lane inspector (not APVTS).
    juce::Slider tune, decay, tone, pan, level, eqLo, eqMid, eqHi, hp, lp, drive, crush, trans, ott;
    juce::Slider chop0, chop1, att, rel, depth, curve, length, midiNote;
    juce::Label tuneL, decayL, toneL, panL, levelL, eqLoL, eqMidL, eqHiL, hpL, lpL, driveL, crushL,
        transL, ottL, chop0L, chop1L, attL, relL, depthL, curveL, lengthL, midiL;
    juce::ComboBox rateBox, envBox, drumBox, typeBox, kitLane;
    juce::ToggleButton dbl{ "DOUBLE" }, mute{ "MUTE" }, solo{ "SOLO" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool syncing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmotherEditor)
};
