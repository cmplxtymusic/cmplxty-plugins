// Vocal Aligner: control layout + the take display (capture / align / load / export / drag-out, three waveform lanes).
#include "PluginEditor.h"

namespace vaui
{
const PanelSpec& panelSpec()
{
    static const PanelSpec s {
        "CMPLXTY VOCAL ALIGNER", "DUB-TO-GUIDE TIMING + PITCH  /  OFFLINE",
        { 16, 56, 868, 300 },
        {
            { "ALIGN",  nullptr,   { 16, 366, 352, 162 },  { { 'k', "tight", "TIGHTNESS" }, { 'k', "maxShift", "MAX SHIFT" }, { 't', "autoAlign", "AUTO ALIGN" } }, 120 },
            { "PITCH",  "pitchOn", { 378, 366, 180, 162 }, { { 'k', "pitchAmt", "AMOUNT" } } },
            { "OUTPUT", nullptr,   { 568, 366, 316, 162 }, { { 'k', "gain", "OUTPUT" }, { 'c', "monitor", "MONITOR" } }, 140 },
        },
        "tight"
    };
    return s;
}

namespace
{
const juce::Colour cBg (0xff101010), cLane (0xff161616), cLine (0xff2d2d2d), cPri (0xffffffff), cSec (0xffb8b8b8), cDim (0xff8a8a8a);

class TakeDisplay final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    TakeDisplay (VocalAlignerProcessor& p) : proc (p)
    {
        for (auto* b : { &capture, &align, &loadGuide, &loadDub, &clear, &exportBtn })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false); b->setMouseClickGrabsKeyboardFocus (false);
        }
        capture.onClick   = [this] { if (proc.isCapturing()) proc.stopCapture(); else proc.startCapture(); refresh(); };
        align.onClick     = [this] { if (proc.status() == VocalAlignerProcessor::Status::aligning) proc.cancelAlign(); else proc.startAlign(); refresh(); };
        clear.onClick     = [this] { proc.clearTakes(); refresh(); };
        loadGuide.onClick = [this] { choose (VocalAlignerProcessor::Which::guide); };
        loadDub.onClick   = [this] { choose (VocalAlignerProcessor::Which::dub); };
        exportBtn.onClick = [this]
        {
            juce::String err; auto f = proc.exportAligned (err);
            note = err.isNotEmpty() ? err : "Saved " + f.getFileName() + " in Documents/CMPLXTY Vocal Aligner (starts at bar 1)";
            if (f.existsAsFile()) f.revealToUser();
            repaint();
        };
        startTimerHz (30);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8).removeFromTop (26);
        capture.setBounds (r.removeFromLeft (104)); r.removeFromLeft (6);
        align.setBounds (r.removeFromLeft (86)); r.removeFromLeft (16);
        loadGuide.setBounds (r.removeFromLeft (104)); r.removeFromLeft (6);
        loadDub.setBounds (r.removeFromLeft (92)); r.removeFromLeft (6);
        clear.setBounds (r.removeFromLeft (70)); r.removeFromLeft (16);
        exportBtn.setBounds (r.removeFromLeft (100));
        dragArea = r.withTrimmedLeft (8);
        auto body = getLocalBounds().reduced (10, 0).withTrimmedTop (42).withTrimmedBottom (30);
        const int lh = body.getHeight() / 3;
        for (int i = 0; i < 3; ++i) lanes[i] = body.removeFromTop (lh).reduced (0, 2);
        status = getLocalBounds().reduced (10, 0).removeFromBottom (28);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (cBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (cPri.withAlpha (0.25f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);

        // drag-out chip
        const bool ready = proc.hasTake (VocalAlignerProcessor::Which::aligned);
        g.setColour (ready ? cPri.withAlpha (0.9f) : cLine);
        {
            juce::Path p; p.addRoundedRectangle (dragArea.toFloat().reduced (0.5f), 4.0f);
            g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
        g.setFont (uiFont (10.5f, true));
        g.setColour (ready ? cPri : cDim.withAlpha (0.6f));
        g.drawText (ready ? "DRAG ALIGNED WAV" : "ALIGNED WAV: -", dragArea, juce::Justification::centred);

        double t0 = 0.0, t1 = 0.0;
        const bool any = proc.songRange (t0, t1);
        if (proc.isCapturing()) { t0 = 0.0; t1 = 1.0; }
        if (any && t1 - t0 < 1.0) t1 = t0 + 1.0;
        static const char* names[3] = { "GUIDE", "DUB", "ALIGNED" };
        for (int i = 0; i < 3; ++i)
        {
            const auto r = lanes[i];
            g.setColour (cLane); g.fillRect (r);
            g.setColour (cLine); g.drawRect (r);
            const auto w = (VocalAlignerProcessor::Which) i;
            const bool has = proc.hasTake (w);
            auto wave = r.withTrimmedLeft (74).reduced (2, 4);
            if (has && any && ! proc.isCapturing())
            {
                std::vector<std::pair<float, float>> ov ((size_t) wave.getWidth());
                proc.overview (w, t0, t1, ov);
                float mx = 0.02f; for (auto& o : ov) mx = std::max ({ mx, -o.first, o.second });
                const float mid = (float) wave.getCentreY(), half = wave.getHeight() * 0.5f;
                g.setColour (i == 2 ? cPri.withAlpha (0.9f) : (i == 1 ? cSec.withAlpha (0.75f) : cDim.withAlpha (0.8f)));
                for (int x = 0; x < wave.getWidth(); ++x)
                {
                    const float a = mid - ov[(size_t) x].second / mx * half, b = mid - ov[(size_t) x].first / mx * half;
                    g.fillRect ((float) (wave.getX() + x), a, 1.0f, std::max (1.0f, b - a));
                }
            }
            else
            {
                g.setColour (cDim.withAlpha (0.55f)); g.setFont (uiFont (11.0f));
                juce::String hint;
                if (proc.isCapturing() && i < 2) hint = (i == 0 && ! proc.sidechainConnected()) ? "no side-chain - load or drop the guide file" : "recording  " + juce::String (proc.capturedSeconds (w), 1) + " s";
                else if (i == 0) hint = "route the guide vocal to the side-chain input, or drop / load a file";
                else if (i == 1) hint = "the track this plug-in is on - CAPTURE while the song plays, or drop / load a file";
                else hint = proc.status() == VocalAlignerProcessor::Status::aligning ? "aligning..." : "aligned result plays here in place of the dub";
                g.drawText (hint, wave, juce::Justification::centred);
                if (proc.isCapturing() && i == 1)
                {
                    const float lv = juce::jlimit (0.0f, 1.0f, proc.inputLevel());
                    g.setColour (cSec.withAlpha (0.8f)); g.fillRect (wave.getX(), wave.getBottom() - 3, (int) (wave.getWidth() * lv), 3);
                }
            }
            g.setColour (has ? cPri : cDim); g.setFont (uiFont (11.0f, true));
            g.drawText (names[i], r.getX() + 8, r.getY(), 66, r.getHeight(), juce::Justification::centredLeft);
            if (has && ! proc.isCapturing())
            {
                g.setColour (cDim); g.setFont (uiFont (10.0f));
                g.drawText (juce::String (proc.capturedSeconds (w), 1) + " s", r.getX() + 8, r.getY() + r.getHeight() / 2 + 6, 60, 12, juce::Justification::centredLeft);
            }
            if (any && ! proc.isCapturing())
            {
                const double ph = proc.playheadSeconds();
                if (ph >= t0 && ph <= t1)
                {
                    const float x = (float) wave.getX() + (float) ((ph - t0) / (t1 - t0)) * (float) wave.getWidth();
                    g.setColour (cPri.withAlpha (0.5f)); g.fillRect (x, (float) r.getY(), 1.0f, (float) r.getHeight());
                }
            }
        }
        // status + progress
        g.setFont (uiFont (11.5f));
        g.setColour (cSec);
        const bool busy = proc.status() == VocalAlignerProcessor::Status::aligning;
        const auto msg = note.isNotEmpty() ? note : proc.lastMessage();
        g.drawFittedText (msg, status.withTrimmedRight (busy ? 170 : 0), juce::Justification::centredLeft, 1);
        if (busy)
        {
            auto bar = status.removeFromRight (160).withSizeKeepingCentre (160, 6);
            g.setColour (cLine); g.fillRect (bar);
            g.setColour (cPri); g.fillRect (bar.withWidth ((int) (bar.getWidth() * proc.progress())));
        }
    }

    // ---- files in / out
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int y) override
    {
        using W = VocalAlignerProcessor::Which;
        W w = y < lanes[0].getBottom() ? W::guide : (y < lanes[1].getBottom() ? W::dub : (proc.hasTake (W::guide) ? W::dub : W::guide));
        int k = 0;
        for (auto& f : files)
        {
            const auto err = proc.loadFile (w, juce::File (f));
            note = err;
            if (++k == 2) break;
            w = w == W::guide ? W::dub : W::guide;   // two files at once: guide + dub
        }
        refresh();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || ! dragArea.contains (e.getMouseDownPosition()) || ! proc.hasTake (VocalAlignerProcessor::Which::aligned)) return;
        juce::String err;
        if (dragFile == juce::File() || ! dragFile.existsAsFile() || dragVersion != proc.contentVersion())
        {
            dragFile = proc.exportAligned (err); dragVersion = proc.contentVersion();
        }
        if (! dragFile.existsAsFile()) { note = err; repaint(); return; }
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ dragFile.getFullPathName() }, false, this, [this] { dragging = false; });
    }

private:
    void choose (VocalAlignerProcessor::Which w)
    {
        chooser = std::make_unique<juce::FileChooser> (w == VocalAlignerProcessor::Which::guide ? "Load the guide vocal" : "Load the dub vocal",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<TakeDisplay> safe (this);
        hostkeys::rememberForDialog (*this);   // which host control had the keyboard before the dialog
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, w] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)   // picked or cancelled: Space goes to the host again once the dialog has gone
                juce::Timer::callAfterDelay (60, [sp = juce::Component::SafePointer<juce::Component> (safe.getComponent())] { if (sp != nullptr) hostkeys::restoreAfterDialog (*sp); });
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            safe->note = safe->proc.loadFile (w, fc.getResult());
            safe->refresh();
        });
    }
    void refresh()
    {
        const auto st = proc.status();
        capture.setButtonText (proc.isCapturing() ? "STOP" : "CAPTURE");
        capture.setToggleState (proc.isCapturing(), juce::dontSendNotification);
        align.setButtonText (st == VocalAlignerProcessor::Status::aligning ? "CANCEL" : "ALIGN");
        align.setEnabled (st == VocalAlignerProcessor::Status::aligning || (proc.hasTake (VocalAlignerProcessor::Which::guide) && proc.hasTake (VocalAlignerProcessor::Which::dub) && ! proc.isCapturing()));
        exportBtn.setEnabled (proc.hasTake (VocalAlignerProcessor::Which::aligned));
        lastVersion = proc.contentVersion();
        repaint();
    }
    void timerCallback() override
    {
        if (proc.contentVersion() != lastVersion) { note.clear(); refresh(); return; }
        const auto st = proc.status();
        const double ph = proc.playheadSeconds();
        if (st == VocalAlignerProcessor::Status::capturing || st == VocalAlignerProcessor::Status::aligning || ph != lastPlay) { lastPlay = ph; refresh(); }
    }

    VocalAlignerProcessor& proc;
    juce::TextButton capture { "CAPTURE" }, align { "ALIGN" }, loadGuide { "LOAD GUIDE..." }, loadDub { "LOAD DUB..." }, clear { "CLEAR" }, exportBtn { "EXPORT WAV" };
    juce::Rectangle<int> lanes[3], status, dragArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note;
    juce::File dragFile;
    int dragVersion = -1, lastVersion = -1;
    double lastPlay = -1.0;
    bool dragging = false;
};
} // namespace

std::unique_ptr<juce::Component> makeDisplay (VocalAlignerProcessor& p, Panel&) { return std::make_unique<TakeDisplay> (p); }
} // namespace vaui
