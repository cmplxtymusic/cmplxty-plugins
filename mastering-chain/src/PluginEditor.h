#pragma once
#include "PluginProcessor.h"
#include "UpdateCheck.h"
#include "Ui.h"

class GlazeLook final : public juce::LookAndFeel_V4
{
  public:
    GlazeLook();
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

class GlazeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    static constexpr int kW = 1240, kH = 770;
    explicit GlazeEditor(GlazeProcessor &);
    ~GlazeEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent &) override;
    void revealPreset(int index);

    // test and screenshot hooks
    gzui::PopupOverlay &popupOverlay() { return popup; }
    juce::String auditKeyboard();
    void showParamMenuFor(const juce::String &id);
    void tick() { timerCallback(); }

  private:
    static constexpr int kBins = 256;
    struct Cell
    {
        gzui::Knob knob;
        juce::Label name, val;
        std::unique_ptr<juce::SliderParameterAttachment> att;
        int param = 0;
    };
    struct Switch
    {
        gzui::Btn btn;
        std::unique_ptr<juce::ButtonParameterAttachment> att;
        int param = 0;
    };
    struct Module
    {
        const char *title;
        juce::Rectangle<int> area;
        int onParam; // -1 for none
        std::vector<int> knobs;
    };
    void timerCallback() override;
    void file(bool write);
    void updateDim();
    void paintSpectrum(juce::Graphics &, juce::Rectangle<float>);
    void paintMeters(juce::Graphics &, juce::Rectangle<float>);
    void analyseScope(const std::array<float, glaze::Engine::kScope> &ring, int writePos,
                      std::array<float, kBins> &dest);
    void addCell(int param, const char *tip);
    void addSwitch(int param, const char *text, const char *tip);
    void makeLab(juce::Label &, const char *t, float h, bool bold);
    void showParamMenu(const juce::String &id, gzui::Knob *knob, juce::Point<int> pos);
    void resetAll();
    void startUpdateCheck(bool force = false);
    void showUpdateMenu();
    juce::Rectangle<int> updateLinkBounds() const;
    void layoutKnob(int param, int x, int y);

    GlazeProcessor &proc;
    GlazeLook look;
    std::array<std::unique_ptr<Cell>, glaze::Count> cells;
    std::array<std::unique_ptr<Switch>, glaze::Count> switches;
    std::vector<Module> modules;
    juce::Label brand, title, footer;
    gzui::NoFocusCombo presets, satType;
    std::unique_ptr<juce::ComboBoxParameterAttachment> aSatType;
    juce::TextButton prev{ "<" }, next{ ">" }, save{ "SAVE" }, load{ "LOAD" }, resetMeters{ "RESET" };
    juce::TooltipWindow tooltip{ this, 700 };
    gzui::PopupOverlay popup;
    gz::upd::Checker checker;
    gz::upd::Info updateInfo;
    bool updateReady = false;
    juce::String footerNote;
    int currentPreset = 0, tickCount = 0;
    std::array<float, kBins> binsIn{}, binsOut{};
    juce::Rectangle<int> specArea, meterArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlazeEditor)
};
