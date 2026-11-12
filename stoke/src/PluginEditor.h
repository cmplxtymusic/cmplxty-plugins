#pragma once
#include "PluginProcessor.h"

class StokeLook final : public juce::LookAndFeel_V4
{
  public:
    StokeLook();
    juce::Typeface::Ptr face, faceB;
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    juce::Font getPopupMenuFont() override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float,
                          juce::Slider &) override;
    void drawToggleButton(juce::Graphics &, juce::ToggleButton &, bool, bool) override;
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool,
                              bool) override;
    void drawComboBox(juce::Graphics &, int, int, bool, int, int, int, int, juce::ComboBox &) override;
    void drawPopupMenuBackground(juce::Graphics &, int, int) override;
};

class StokeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit StokeEditor(StokeProcessor &);
    ~StokeEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void revealPreset(int index);

  private:
    void timerCallback() override;
    void file(bool write);
    void updateDim();
    void paintSpectrum(juce::Graphics &, juce::Rectangle<float>);
    void makeKnob(juce::Slider &, const char *tip);
    void makeLab(juce::Label &, const char *t, float h, bool bold);

    StokeProcessor &proc;
    StokeLook look;
    juce::Slider focus, centre, width, glow, smooth, mix, output;
    juce::Label brand, title, focusName, centreName, widthName, glowName, smoothName, mixName,
        outName, status, focusVal, centreVal, widthVal, glowVal, smoothVal, mixVal, outVal;
    juce::ToggleButton midOnly, match, limit;
    juce::ComboBox presets;
    juce::TextButton prev{ "<" }, next{ ">" }, save{ "SAVE" }, load{ "LOAD" };
    juce::SliderParameterAttachment aFocus, aCentre, aWidth, aGlow, aSmooth,
        aMix, aOut;
    juce::ButtonParameterAttachment aMid, aMatch, aLimit;
    int currentPreset = 0;
    static constexpr int kBins = 256;
    std::array<float, kBins> bins{};
    float binPeak = 1.0e-6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StokeEditor)
};
