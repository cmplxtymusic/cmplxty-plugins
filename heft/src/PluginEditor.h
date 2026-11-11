#pragma once
#include "PluginProcessor.h"
class HeftLook final : public juce::LookAndFeel_V4
{
  public:
    HeftLook();
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    juce::Typeface::Ptr face;
};
class HeftEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit HeftEditor(HeftProcessor &);
    ~HeftEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void showAdvanced(bool);

  private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    void timerCallback() override;
    void file(bool save);
    HeftProcessor &proc;
    HeftLook look;
    bool advanced = false;
    std::array<juce::Slider, heft::Count> knobs;
    std::array<juce::Label, heft::Count> labels;
    std::vector<std::unique_ptr<SA>> attachments;
    std::array<juce::ComboBox, 4> sources, destinations;
    std::array<juce::Slider, 4> depths;
    std::vector<std::unique_ptr<CA>> choices;
    juce::ComboBox presets;
    juce::TextButton diceButton{"DICE"}, cover{"UNDER THE HOOD +"}, save{"SAVE"}, load{"LOAD"};
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeftEditor)
};
