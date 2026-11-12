#pragma once
#include "PluginProcessor.h"
#include <array>
#include <memory>

class RiggedLook final : public juce::LookAndFeel_V4
{
  public:
    RiggedLook();
    juce::Typeface::Ptr face, faceB;
    juce::Font getLabelFont(juce::Label &) override;
    juce::Font getComboBoxFont(juce::ComboBox &) override;
    juce::Font getTextButtonFont(juce::TextButton &, int) override;
    void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float, juce::Slider &) override;
    void drawToggleButton(juce::Graphics &, juce::ToggleButton &, bool, bool) override;
    void drawButtonBackground(juce::Graphics &, juce::Button &, const juce::Colour &, bool, bool) override;
};

class DiceView final : public juce::Component, private juce::Timer
{
  public:
    DiceView();
    void paint(juce::Graphics &) override;
    void kick();
    void setReduce(bool r);
    void setFace(juce::Typeface::Ptr f) { face = std::move(f); }

  private:
    void timerCallback() override;
    juce::Typeface::Ptr face;
    float angle = 0.35f, speed = 0.018f, spin = 0;
    bool reduce = false;
    int pip = 5;
};

class ModulePanel final : public juce::Component
{
  public:
    ModulePanel(RiggedProcessor &, int fx, RiggedLook &);
    void paint(juce::Graphics &) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    int fxIndex = 0;
    std::function<void(ModulePanel *, juce::Point<int>)> onDragStart, onDrag, onDragEnd;
    juce::Rectangle<int> headerBounds() const { return getLocalBounds().removeFromTop(28); }
    void updateDim();

  private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    RiggedProcessor &proc;
    RiggedLook &look;
    juce::ToggleButton on{ "ON" }, lock{ "LOCK" };
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::ComboBox>> boxes;
    std::vector<std::unique_ptr<SA>> sAtt;
    std::vector<std::unique_ptr<BA>> bAtt;
    std::vector<std::unique_ptr<CA>> cAtt;
};

class RiggedEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit RiggedEditor(RiggedProcessor &);
    ~RiggedEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    void timerCallback() override;
    void file(bool save);
    void layoutModules();
    void applyOrder(const std::array<int, rigged::kFxCount> &);
    RiggedProcessor &proc;
    RiggedLook look;
    DiceView dice;
    std::array<std::unique_ptr<ModulePanel>, rigged::kFxCount> modules;
    juce::Slider amount, mix, output;
    juce::Label amountL, mixL, outputL, status;
    juce::ToggleButton match{ "MATCH" }, limit{ "LIMIT" }, bypass{ "BYPASS" }, reduce{ "REDUCE MOTION" };
    juce::TextButton roll{ "RANDOMIZE" }, undo{ "UNDO" }, redo{ "REDO" }, save{ "SAVE" }, load{ "LOAD" };
    juce::ComboBox presets;
    std::vector<std::unique_ptr<SA>> sAtt;
    std::vector<std::unique_ptr<BA>> bAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastKick = 0;
    int dragFx = -1, hoverSlot = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiggedEditor)
};
