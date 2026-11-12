#pragma once
#include "PluginProcessor.h"
#include <memory>

class FeverLook final : public juce::LookAndFeel_V4
{
  public:
    FeverLook();
    juce::Typeface::Ptr face, faceB;
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    void drawToggleButton(juce::Graphics &, juce::ToggleButton &, bool, bool) override;
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
    void drawComboBox(juce::Graphics &, int, int, bool, int, int, int, int, juce::ComboBox &) override;
    void drawPopupMenuBackground(juce::Graphics &, int, int) override;
};

class FeverEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit FeverEditor(FeverProcessor &);
    ~FeverEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void revealPreset(int i);

  private:
    void timerCallback() override;
    void file(bool write);
    void paintMood(juce::Graphics &, juce::Rectangle<float> area, float fever, float pulse, bool motion);
    void updateSideLabels();
    FeverProcessor &proc;
    FeverLook look;
    juce::Slider fever, sideA, sideB, mix, output, depth;
    juce::Label sideAName, sideBName, mixName, outName, depthName, status, brand, title;
    juce::ToggleButton match, limit, heart, reduce;
    juce::ComboBox rate, presets;
    juce::TextButton save, load;
    juce::SliderParameterAttachment aFever, aSideA, aSideB, aMix, aOut, aDepth;
    juce::ButtonParameterAttachment aMatch, aLimit, aHeart, aReduce;
    std::unique_ptr<juce::ComboBoxParameterAttachment> aRate;
    std::unique_ptr<juce::FileChooser> chooser;
    float anim = 0;
    int lastSide = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FeverEditor)
};
