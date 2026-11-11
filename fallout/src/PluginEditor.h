#pragma once
#include "PluginProcessor.h"
class FalloutLook final : public juce::LookAndFeel_V4
{
  public:
    FalloutLook();
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
    explicit GridView(FalloutProcessor &);
    void paint(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    int rowAt(juce::Point<int>) const;
    int colAt(juce::Point<int>) const;
    juce::Rectangle<int> cell(int col, int row) const;
    juce::Rectangle<int> gridBounds() const;

  private:
    FalloutProcessor &proc;
    int downCol = -1, downRow = -1, downNote = -1;
    float startMicro = 0, startRubato = 0;
    bool toggledAccent = false, toggledSlide = false, dragging = false;
};
class DragChip final : public juce::Component
{
  public:
    DragChip(FalloutProcessor &, bool midi);
    void paint(juce::Graphics &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    juce::String lastError;

  private:
    FalloutProcessor &proc;
    bool midi = true, dragging = false;
};
class FalloutEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit FalloutEditor(FalloutProcessor &);
    ~FalloutEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    void timerCallback() override;
    void file(bool save);
    void syncButtons();
    FalloutProcessor &proc;
    FalloutLook look;
    std::array<juce::Slider, fallout::Count> knobs;
    std::array<juce::Label, fallout::Count> labels;
    std::array<juce::ComboBox, fallout::Count> boxes;
    std::vector<std::unique_ptr<SA>> attachments;
    std::vector<std::unique_ptr<CA>> choices;
    juce::ComboBox presets;
    juce::TextButton save{"SAVE"}, load{"LOAD"};
    juce::TextButton randBtn{"RANDOMIZE"}, mutBtn{"MUTATE"}, clearBtn{"CLEAR"};
    juce::TextButton leftBtn{"<<"}, rightBtn{">>"}, copyBtn{"COPY"}, pasteBtn{"PASTE"};
    juce::TextButton octDown{"OCT -"}, octUp{"OCT +"};
    juce::TextButton editBtn{"EDIT"}, microBtn{"MICRO"}, rubatoBtn{"RUBATO"}, ashBtn{"ASH"};
    juce::Label status, octLabel;
    GridView grid;
    DragChip midiChip, wavChip;
    std::unique_ptr<juce::FileChooser> chooser;
    fallout::Pattern clip;
    bool clipValid = false;
    struct Ash
    {
        float x = 0, y = 0, vx = 0, vy = 0, size = 1, alpha = .3f;
    };
    std::array<Ash, 56> ashes{};
    void resetAsh(Ash &, bool spawn);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FalloutEditor)
};
