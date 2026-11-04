// CMPLXTY Plugin Hub - pick the CMPLXTY VST3/CLAP plug-ins to install or update, in one go.
#include <juce_gui_basics/juce_gui_basics.h>
#include "HubCore.h"
#include "BinaryData.h"
#include <thread>

namespace
{
const juce::Colour kBg (0xff0a0a0a), kPanel (0xff141414), kLine (0xff2a2a2a), kDim (0xff8a8a8a), kFg (0xfff2f2f2);

juce::Font font (float h, bool bold = false)
{
    static auto reg = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize);
    static auto semi = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? semi : reg).withHeight (h));
}

struct MonoLook : juce::LookAndFeel_V4
{
    MonoLook()
    {
        setColour (juce::TextButton::buttonColourId, kBg); setColour (juce::TextButton::textColourOffId, kFg);
        setColour (juce::ListBox::backgroundColourId, kBg); setColour (juce::ScrollBar::thumbColourId, kLine.brighter (0.3f));
        setColour (juce::ProgressBar::foregroundColourId, kFg); setColour (juce::ProgressBar::backgroundColourId, kPanel);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        bool primary = b.getProperties()["primary"];
        g.setColour (primary ? (b.isEnabled() ? kFg : kDim) : (down ? kLine : over ? kPanel : kBg));
        g.fillRect (r);
        g.setColour (primary ? kFg : (over ? kFg : kLine.brighter (0.4f))); g.drawRect (r, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        bool primary = b.getProperties()["primary"];
        g.setColour (primary ? kBg : (b.isEnabled() ? kFg : kDim));
        g.setFont (font (13.0f, true));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, true); }
};
}

class HubComponent : public juce::Component, private juce::ListBoxModel, private juce::Thread
{
public:
    HubComponent (hub::Layout layout, juce::String screenshotPath)
        : juce::Thread ("hub worker"), layout_ (std::move (layout)), shotPath (std::move (screenshotPath))
    {
        setLookAndFeel (&look);
        list.setModel (this); list.setRowHeight (44); list.setOutlineThickness (0);
        addAndMakeVisible (list);
        for (auto* b : { &refreshBtn, &uninstallBtn, &cancelBtn, &goBtn, &hubUpdBtn }) addAndMakeVisible (*b);
        goBtn.getProperties().set ("primary", true);
        hubUpdBtn.setVisible (false); cancelBtn.setVisible (false);
        uninstallBtn.onClick = [this] { confirmUninstall(); };
        cancelBtn.onClick = [this] { cancelFlag = true; cancelBtn.setEnabled (false); setStatus ("Cancelling after the current step..."); };
        refreshBtn.onClick = [this] { startJob (Job::Refresh); };
        goBtn.onClick = [this] { startJob (Job::Install); };
        hubUpdBtn.onClick = [this] { juce::URL (hubUpdate.page).launchInDefaultBrowser(); };
        addAndMakeVisible (progress);
        setSize (760, 640);
        startJob (Job::Refresh);
    }
    ~HubComponent() override { stopThread (60000); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBg);
        g.setColour (kFg); g.setFont (font (22.0f, true));
        g.drawText ("CMPLXTY  //  PLUGIN HUB", 24, 18, 400, 28, juce::Justification::left);
        g.setColour (kDim); g.setFont (font (13.0f));
        g.drawText ("v" HUB_VERSION_STRING "   " + juce::String (layout_.staged ? "TEST PREFIX" : "VST3 + CLAP")
                    + "   " + juce::String (layout_.platform == hub::Platform::Mac ? "MACOS" : "WINDOWS"), 24, 44, 400, 18, juce::Justification::left);
        auto hdr = juce::Rectangle<int> (24, 74, getWidth() - 48, 22);
        g.setFont (font (11.0f, true)); g.setColour (kDim);
        // select-all box, lined up with the row boxes (list x 24 + row box x 12)
        auto st = selectState();
        g.setColour (st != 0 ? kFg : kDim); g.drawRect (selectAllBox().toFloat(), 1.2f);
        if (st == 2) g.fillRect (selectAllBox().toFloat().reduced (4.0f));
        else if (st == 1) g.fillRect (selectAllBox().toFloat().reduced (4.0f, 7.0f));
        g.setColour (kDim);
        g.drawText ("PLUGIN", hdr.withX (64).withWidth (200), juce::Justification::left);
        g.drawText ("TYPE", hdr.withX (270).withWidth (100), juce::Justification::left);
        g.drawText ("INSTALLED", hdr.withX (390).withWidth (90), juce::Justification::left);
        g.drawText ("LATEST", hdr.withX (480).withWidth (80), juce::Justification::left);
        g.drawText ("ACTION", hdr.withX (570).withWidth (160), juce::Justification::left);
        g.setColour (kLine); g.drawHorizontalLine (98, 24.0f, (float) getWidth() - 24.0f);
        g.drawHorizontalLine (getHeight() - 118, 24.0f, (float) getWidth() - 24.0f);
        g.setColour (kDim); g.setFont (font (12.5f));
        g.drawFittedText (status, 24, getHeight() - 58, getWidth() - 48, 34, juce::Justification::topLeft, 2);
    }
    void resized() override
    {
        list.setBounds (24, 99, getWidth() - 48, getHeight() - 99 - 119);
        int y = getHeight() - 108, x = 24;
        refreshBtn.setBounds (x, y, 96, 30);
        uninstallBtn.setBounds (x + 104, y, 140, 30);
        cancelBtn.setBounds (getWidth() - 24 - 210 - 112, y, 104, 30);
        goBtn.setBounds (getWidth() - 24 - 210, y, 210, 30);
        progress.setBounds (24, getHeight() - 70, getWidth() - 48, 6);
        hubUpdBtn.setBounds (getWidth() - 24 - 230, 22, 230, 28);
    }

private:
    enum class Job { Refresh, Install, Uninstall };
    void confirmUninstall()
    {
        juce::StringArray names;
        for (int i = 0; i < plugins.size(); ++i) if (checked[i] && plugins[i].installed.isNotEmpty()) names.add (plugins[i].name);
        if (names.isEmpty()) return;
        auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Uninstall")
                      .withMessage ("Remove the VST3 + CLAP files for:\n" + names.joinIntoString (", ")
                                    + "\n\nYour presets and settings are kept.").withButton ("Uninstall").withButton ("Cancel")
                      .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [safe = juce::Component::SafePointer<HubComponent> (this)] (int r) { if (safe && r == 1) safe->startJob (Job::Uninstall); });
    }
    int getNumRows() override { return plugins.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, plugins.size())) return;
        auto& p = plugins.getReference (row); bool on = checked[row];
        if (on) { g.setColour (kPanel); g.fillRect (0, 0, w, h); }
        g.setColour (kLine); g.drawHorizontalLine (h - 1, 0.0f, (float) w);
        auto box = juce::Rectangle<float> (12.0f, h / 2.0f - 8.0f, 16.0f, 16.0f);
        g.setColour (on ? kFg : kDim); g.drawRect (box, 1.2f);
        if (on) g.fillRect (box.reduced (4.0f));
        g.setColour (kFg); g.setFont (font (15.5f, true));
        g.drawText (p.name.toUpperCase(), 40, 0, 220, h, juce::Justification::centredLeft);
        g.setFont (font (10.5f, true));
        auto tw = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), p.type.toUpperCase()) + 14;
        auto pill = juce::Rectangle<float> (246.0f, h / 2.0f - 9.0f, (float) tw, 18.0f);
        g.setColour (kDim); g.drawRoundedRectangle (pill, 9.0f, 1.0f);
        g.setColour (kFg.withAlpha (0.85f)); g.drawText (p.type.toUpperCase(), pill.toNearestInt(), juce::Justification::centred);
        g.setFont (font (13.5f)); g.setColour (p.installed.isEmpty() ? kDim : kFg);
        g.drawText (p.installed.isEmpty() ? juce::String ("-") : p.installed == "unknown" ? juce::String ("? (unreadable)") : p.installed, 366, 0, 90, h, juce::Justification::centredLeft);
        g.setColour (kFg); g.drawText (p.latest, 456, 0, 80, h, juce::Justification::centredLeft);
        auto a = p.action();
        juce::String act = ! p.checksumOk() ? "NO CHECKSUM" : a == hub::Action::Reinstall && ! on ? "UP TO DATE" : hub::actionName (a);
        g.setFont (font (12.0f, true)); g.setColour (a == hub::Action::Update || (on && p.checksumOk()) ? kFg : kDim);
        g.drawText (act, 546, 0, w - 546, h, juce::Justification::centredLeft);
        if (resultText.count (p.slug)) { g.setFont (font (10.5f)); g.setColour (kDim); g.drawText (resultText[p.slug], 546, h - 16, w - 546, 14, juce::Justification::centredLeft); }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { toggleRow (row); }
    void returnKeyPressed (int row) override { toggleRow (row); }
    void toggleRow (int row)
    {
        if (busy || ! juce::isPositiveAndBelow (row, plugins.size()) || ! plugins[row].checksumOk()) return;
        checked.set (row, ! checked[row]); changed();
    }
    juce::Rectangle<int> selectAllBox() const { return { 36, 77, 16, 16 }; }
    // 0 = none ticked, 1 = some, 2 = every installable row ticked
    int selectState() const
    {
        int can = 0, on = 0;
        for (int i = 0; i < plugins.size(); ++i) if (plugins.getReference (i).checksumOk()) { ++can; on += checked[i] ? 1 : 0; }
        return on == 0 ? 0 : on == can ? 2 : 1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (busy || ! selectAllBox().expanded (6).contains (e.getPosition())) return;
        bool all = selectState() != 2;   // empty or partial -> tick all; full -> clear
        for (int i = 0; i < plugins.size(); ++i) checked.set (i, all && plugins.getReference (i).checksumOk());
        changed();
    }
    void changed()
    {
        int n = 0; for (int i = 0; i < plugins.size(); ++i) n += checked[i] && plugins.getReference (i).checksumOk() ? 1 : 0;
        auto label = hub::primaryLabel (plugins, checked);
        goBtn.setButtonText (n == 0 ? juce::String ("Install") : label + " (" + juce::String (n) + ")");
        goBtn.setEnabled (n > 0 && ! busy);
        int inst = 0; for (int i = 0; i < plugins.size(); ++i) inst += checked[i] && plugins.getReference (i).installed.isNotEmpty() ? 1 : 0;
        uninstallBtn.setButtonText (inst == 0 ? juce::String ("Uninstall") : "Uninstall (" + juce::String (inst) + ")");
        uninstallBtn.setEnabled (inst > 0 && ! busy);
        list.repaint(); repaint();
    }
    void setStatus (const juce::String& s) { juce::MessageManager::callAsync ([this, s, safe = juce::Component::SafePointer<HubComponent> (this)] { if (safe) { status = s; repaint(); } }); }

    void startJob (Job j)
    {
        if (busy) return;
        job = j; busy = true; changed();
        refreshBtn.setEnabled (false);
        cancelFlag = false; progressValue = 0.0;
        cancelBtn.setVisible (j == Job::Install); cancelBtn.setEnabled (true);
        toInstall.clear();
        for (int i = 0; i < plugins.size(); ++i)
            if (checked[i] && (j == Job::Install ? plugins[i].checksumOk() : plugins[i].installed.isNotEmpty())) toInstall.add (plugins[i]);
        startThread();
    }
    void run() override
    {
        if (job == Job::Refresh)
        {
            setStatus ("Reading the plugin list from GitHub...");
            juce::Array<hub::Plugin> got; juce::String err;
            hub::HubUpdate upd;
            std::thread selfCheck ([&upd, p = layout_.platform] { upd = hub::checkHubUpdate (p); });   // runs alongside the catalog
            bool ok = hub::fetchCatalog (got, layout_, err);
            selfCheck.join();
            juce::MessageManager::callAsync ([this, got, ok, err, upd, safe = juce::Component::SafePointer<HubComponent> (this)]
            {
                if (! safe) return;
                if (ok) { plugins = got; checked.clear(); for (auto& p : plugins) checked.add (p.action() == hub::Action::Update); }
                int inst = 0, upds = 0; for (auto& p : plugins) { inst += p.installed.isNotEmpty(); upds += p.action() == hub::Action::Update; }
                status = ok ? juce::String (plugins.size()) + " plugins available  /  " + juce::String (inst) + " installed  /  " + juce::String (upds) + " update(s)"
                               + (err.isNotEmpty() ? "  (" + err + ")" : juce::String())
                               + "\n" + (lastResult.isNotEmpty() ? lastResult : juce::String ("Your presets are backed up and checked on every install."))
                            : "Could not reach GitHub: " + err;
                hubUpdate = upd;
                hubUpdBtn.setButtonText ("HUB " + upd.version + " AVAILABLE - GET IT"); hubUpdBtn.setVisible (upd.available);
                finishJob();
                if (shotPath.isNotEmpty()) juce::Timer::callAfterDelay (400, [this] { takeShot(); });
            });
        }
        else
        {
            bool uninstall = job == Job::Uninstall;
            auto say = [this] (const juce::String& s) { setStatus (s); };
            auto results = uninstall ? hub::uninstallSelected (toInstall, layout_, say)
                                     : hub::installSelected (toInstall, layout_, say,
                                           [this] (juce::int64 d, juce::int64 t) { progressValue = t > 0 ? (double) d / (double) t : -1.0; }, &cancelFlag);
            juce::MessageManager::callAsync ([this, results, uninstall, safe = juce::Component::SafePointer<HubComponent> (this)]
            {
                if (! safe) return;
                int ok = 0, cancelled = 0;
                for (auto& r : results) { ok += r.ok; cancelled += r.message == "cancelled"; resultText[r.slug] = r.ok ? (uninstall ? "removed" : "done") : r.message.substring (0, 60); }
                lastResult = juce::String (ok) + " of " + juce::String (results.size()) + (uninstall ? " removed." : " done.")
                         + (cancelled > 0 ? " " + juce::String (cancelled) + " cancelled." : juce::String())
                         + (ok < results.size() ? " See the row notes for what failed." : juce::String()) + " Rescan your plug-ins in the DAW.";
                status = lastResult;
                progressValue = 0.0;
                finishJob(); startJob (Job::Refresh);
            });
        }
    }
    void finishJob()
    {
        busy = false;
        refreshBtn.setEnabled (true); cancelBtn.setVisible (false);
        list.updateContent(); changed();
    }
    void takeShot()
    {
        auto img = createComponentSnapshot (getLocalBounds(), true, 2.0f);
        juce::File f (shotPath); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os); os.flush();
        juce::JUCEApplication::quit();
    }

    MonoLook look;
    hub::Layout layout_;
    juce::String shotPath, status, lastResult;
    juce::ListBox list;
    juce::TextButton refreshBtn { "Refresh" }, uninstallBtn { "Uninstall" }, cancelBtn { "Cancel" }, goBtn { "Install" }, hubUpdBtn;
    std::atomic<bool> cancelFlag { false };
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    juce::Array<hub::Plugin> plugins, toInstall;
    juce::Array<bool> checked;
    std::map<juce::String, juce::String> resultText;
    hub::HubUpdate hubUpdate;
    Job job = Job::Refresh;
    std::atomic<bool> busy { false };
};

class HubApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "CMPLXTY Plugin Hub"; }
    const juce::String getApplicationVersion() override { return HUB_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const juce::String& cmd) override
    {
        // test/demo switches:  --prefix <dir> [--as win|mac]  --screenshot <out.png>
        auto args = juce::StringArray::fromTokens (cmd, true); for (auto& a : args) a = a.unquoted();
        auto opt = [&] (const char* k) { auto i = args.indexOf (k); return i >= 0 ? args[i + 1] : juce::String(); };
        auto layout = hub::Layout::system();
        if (opt ("--prefix").isNotEmpty())
            layout = hub::Layout::prefix (juce::File (opt ("--prefix")), opt ("--as") == "mac" ? hub::Platform::Mac : hub::Platform::Windows);
        window.reset (new Window (new HubComponent (layout, opt ("--screenshot"))));
    }
    void shutdown() override { window = nullptr; }
    struct Window : juce::DocumentWindow
    {
        explicit Window (juce::Component* c) : juce::DocumentWindow ("CMPLXTY Plugin Hub", kBg, allButtons)
        {
            setUsingNativeTitleBar (true); setContentOwned (c, true); setResizable (true, false);
            setResizeLimits (720, 480, 1400, 1400); centreWithSize (getWidth(), getHeight()); setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION (HubApp)
