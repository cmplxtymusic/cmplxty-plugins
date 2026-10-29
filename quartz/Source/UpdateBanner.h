// Small monochrome "Quartz X.Y.Z is available" bar in the header (1.0.2). Shown only when UpdateCheck found a newer version.
#pragma once
#include "Components.h"
#include "UpdateCheck.h"

namespace qzui
{
class UpdateBanner : public juce::Component
{
public:
    std::function<void()> onDismiss;                       // editor hides + deletes nothing; it just calls setVisible(false)
    std::function<void (const juce::String&)> onSkipVersion;

    explicit UpdateBanner (Ctx& c) : ctx (c)
    {
        for (auto* b : { &update, &later, &skip })
        {
            addAndMakeVisible (b); b->ctx = &ctx; b->font = 9.0f; b->spacing = 1.2f; b->bg = juce::Colours::transparentBlack;
        }
        update.text = "UPDATE"; later.text = "LATER"; skip.text = "SKIP THIS VERSION";
        update.on = true;
        update.onClick = [this] { openPage(); if (onDismiss) onDismiss(); };
        later.onClick = [this] { if (onDismiss) onDismiss(); };
        skip.onClick = [this] { if (onSkipVersion) onSkipVersion (info.version); if (onDismiss) onDismiss(); };
        setVisible (false);
    }
    void show (const qz::upd::Info& i)
    {
        info = i;
        title = "QUARTZ " + info.version + " IS AVAILABLE";
        update.hint = "Open the Quartz page in your browser to get " + info.version + " (close FL Studio before running the installer)";
        later.hint = "Hide this until the next time the editor opens";
        skip.hint = "Don't remind me about " + info.version + " again (a newer version will still be announced)";
        hint = info.notes.isNotEmpty() ? info.notes : juce::String();
        layoutNow(); setVisible (true); toFront (false); repaint();
    }
    // the page the Update button opens (already validated as https://cmplxty.music/... by UpdateCheck::parseInfo)
    juce::String targetUrl() const { return info.url; }
    std::function<void (const juce::String&)> openUrl = [] (const juce::String& u) { juce::URL (u).launchInDefaultBrowser(); };
    void openPage() { if (qz::upd::isTrustedUrl (info.url)) openUrl (info.url); }
    TextButtonComp update, later, skip;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101010)); g.fillRect (r);
        g.setColour (juce::Colour (0xff6a6a6a)); g.drawRect (r, 1.0f);
        g.setColour (juce::Colour (0xffeeeeee));                       // small gem diamond
        juce::Path d; const float cx = 14.0f, cy = r.getCentreY(); d.addTriangle (cx - 5, cy, cx, cy - 5, cx + 5, cy); d.addTriangle (cx - 5, cy, cx, cy + 5, cx + 5, cy);
        g.fillPath (d);
        ui::text (g, title, { 26.0f, 0.0f, titleW, r.getHeight() }, Fonts::ui (9.5f, 1.6f), juce::Colour (0xffe0e0e0), juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { if (hint.isNotEmpty()) ctx.hint (hint); }
    void mouseExit (const juce::MouseEvent&) override { ctx.hint ({}); }
    void resized() override { layoutButtons(); }
    int idealWidth() const { return 26 + juce::roundToInt (titleW) + 10 + bw[0] + 6 + bw[1] + 6 + bw[2] + 6; }
    static constexpr int kH = 26;

private:
    void layoutNow()
    {
        titleW = ui::textWidth (Fonts::ui (9.5f, 1.6f), title) + 2.0f;
        const juce::Font f = Fonts::ui (9.0f, 1.2f);
        bw[0] = juce::roundToInt (ui::textWidth (f, update.text)) + 20; bw[1] = juce::roundToInt (ui::textWidth (f, later.text)) + 20; bw[2] = juce::roundToInt (ui::textWidth (f, skip.text)) + 20;
        setSize (idealWidth(), kH); layoutButtons();
    }
    void layoutButtons()
    {
        int x = getWidth() - 6 - bw[2];
        skip.setBounds (x, 3, bw[2], kH - 6); x -= 6 + bw[1];
        later.setBounds (x, 3, bw[1], kH - 6); x -= 6 + bw[0];
        update.setBounds (x, 3, bw[0], kH - 6);
    }
    Ctx& ctx; qz::upd::Info info; juce::String title, hint; float titleW = 150.0f; int bw[3] { 60, 54, 130 };
};
}
