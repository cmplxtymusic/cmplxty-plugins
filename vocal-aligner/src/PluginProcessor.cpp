#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"

namespace
{
const va::PDef kParams[] = {
    { "tight",    "Tightness",     'f', 0.0f, 1.0f, 0.001f, 1.0f, 0.75f, "%" },
    { "maxShift", "Max Shift",     'f', 40.0f, 1000.0f, 1.0f, 0.5f, 300.0f, "ms" },
    { "pitchOn",  "Pitch Align",   'b', 0, 1, 1, 1, 0.0f, "" },
    { "pitchAmt", "Pitch Amount",  'f', 0.0f, 1.0f, 0.001f, 1.0f, 1.0f, "%" },
    { "autoAlign","Auto Align",    'b', 0, 1, 1, 1, 1.0f, "" },
    { "monitor",  "Monitor",       'c', 0, 2, 1, 1, 0.0f, "", "Aligned|Original|Guide" },
    { "gain",     "Output",        'f', -24.0f, 12.0f, 0.1f, 1.0f, 0.0f, "dB" },
};

juce::File docsDir() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("CMPLXTY Vocal Aligner"); }

std::vector<std::pair<float, float>> makePeaks (const std::vector<float>& x, int len, int bin)
{
    std::vector<std::pair<float, float>> p ((size_t) ((len + bin - 1) / std::max (1, bin)));
    for (size_t b = 0; b < p.size(); ++b)
    {
        float lo = 0.0f, hi = 0.0f;
        for (int i = (int) b * bin, e = std::min (len, (int) (b + 1) * bin); i < e; ++i) { lo = std::min (lo, x[(size_t) i]); hi = std::max (hi, x[(size_t) i]); }
        p[b] = { lo, hi };
    }
    return p;
}
} // namespace

struct AlignJob final : juce::Thread
{
    explicit AlignJob (VocalAlignerProcessor& p) : juce::Thread ("CMPLXTY Vocal Aligner align"), proc (p) {}
    void run() override { proc.runAlign(); }
    VocalAlignerProcessor& proc;
};

VocalAlignerProcessor::VocalAlignerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Guide (side-chain)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "STATE", va::makeLayout (kParams))
{
    presets.keepIds = { "monitor" };   // what you listen to is a workflow switch, not part of a preset
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pTight = g ("tight"); pShift = g ("maxShift"); pPitchOn = g ("pitchOn"); pPitchAmt = g ("pitchAmt");
    pMonitor = g ("monitor"); pGain = g ("gain"); pAuto = g ("autoAlign");
    setMessage ("Press CAPTURE and play the song (guide on the side-chain), or drop audio files here");
}

VocalAlignerProcessor::~VocalAlignerProcessor()
{
    cancel.store (true);
    if (worker) worker->stopThread (10000);
}

bool VocalAlignerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != l.getMainInputChannelSet()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto& sc = l.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void VocalAlignerProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    { const juce::ScopedLock l (peakStore.lock); peakStore.bin = std::max (1, (int) std::lround (sampleRate / 100.0)); }
    gainSmooth = juce::Decibels::decibelsToGain (pGain->load());
}

void VocalAlignerProcessor::setMessage (const juce::String& m) { const juce::SpinLock::ScopedLockType l (msgLock); message = m; }

VocalAlignerProcessor::Status VocalAlignerProcessor::status() const noexcept
{
    if (capturing.load()) return Status::capturing;
    if (aligning.load()) return Status::aligning;
    return takeLen[2].load() > 0 ? Status::ready : Status::idle;
}

void VocalAlignerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const int nCh = main.getNumChannels();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    const bool sc = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    scConnected.store (sc, std::memory_order_relaxed);

    float pk = 0.0f;
    for (int c = 0; c < nCh; ++c) pk = std::max (pk, main.getMagnitude (c, 0, n));
    level.store (std::max (pk, level.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);

    bool playing = false; juce::int64 pos = -1;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying();
            if (auto t = info->getTimeInSamples()) pos = *t;
        }
    if (pos >= 0) playSec.store ((double) pos / sampleRate, std::memory_order_relaxed);

    if (capturing.load() && playing && pos >= 0)
    {
        const juce::SpinLock::ScopedTryLockType l (takeLock);
        if (l.isLocked())
        {
            auto write = [&] (Take& t, std::atomic<int>& len, const juce::AudioBuffer<float>& src)
            {
                if (t.data.empty() || src.getNumChannels() == 0) return;
                if (t.origin < 0) t.origin = pos;
                const float g = 1.0f / (float) src.getNumChannels();
                for (int i = 0; i < n; ++i)
                {
                    const juce::int64 idx = pos - t.origin + i;
                    if (idx < 0 || idx >= (juce::int64) t.data.size()) continue;
                    float s = 0.0f; for (int c = 0; c < src.getNumChannels(); ++c) s += src.getSample (c, i);
                    t.data[(size_t) idx] = s * g;
                    t.length = std::max (t.length, (int) idx + 1);
                }
                len.store (t.length, std::memory_order_relaxed);
            };
            if (armDub.load()) write (takes[1], takeLen[1], main);
            if (armGuide.load() && sc) { auto side = getBusBuffer (buffer, true, 1); write (takes[0], takeLen[0], side); }
        }
    }
    else if (! capturing.load() && playing && pos >= 0)
    {
        const int mon = (int) std::lround (pMonitor->load());
        const int which = mon == 0 ? 2 : (mon == 2 ? 0 : -1);
        if (which >= 0)
        {
            const juce::SpinLock::ScopedTryLockType l (takeLock);
            if (l.isLocked())
            {
                const Take& t = takes[which];
                if (t.length > 0)
                    for (int i = 0; i < n; ++i)
                    {
                        const juce::int64 idx = pos - t.origin + i;
                        if (idx < 0 || idx >= t.length) continue;   // outside the take: the track plays as it is
                        const float s = t.data[(size_t) idx];
                        for (int c = 0; c < nCh; ++c) main.setSample (c, i, s);
                    }
            }
        }
    }

    const float target = juce::Decibels::decibelsToGain (pGain->load());
    for (int i = 0; i < n; ++i)
    {
        gainSmooth += 0.002f * (target - gainSmooth);
        for (int c = 0; c < nCh; ++c) main.setSample (c, i, main.getSample (c, i) * gainSmooth);
    }
}

// ---------------------------------------------------------------------------------------------------- capture / align
void VocalAlignerProcessor::startCapture()
{
    if (capturing.load()) return;
    cancelAlign();
    const bool sc = scConnected.load();
    const size_t cap = (size_t) (kMaxSeconds * sampleRate);
    Take fresh[2];
    fresh[1].data.assign (cap, 0.0f); fresh[1].origin = -1; fresh[1].sr = sampleRate;
    if (sc) { fresh[0].data.assign (cap, 0.0f); fresh[0].origin = -1; fresh[0].sr = sampleRate; }
    Take old[3];
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        std::swap (takes[1], fresh[1]);
        if (sc) std::swap (takes[0], fresh[0]);
        std::swap (takes[2], old[2]);
        takeLen[1] = 0; if (sc) takeLen[0] = 0; takeLen[2] = 0;
    }
    armDub = true; armGuide = sc;
    capturing = true;
    setMessage (sc ? "Capturing dub + guide - play the part, then press STOP" : "Capturing the dub - play the part, then press STOP (no side-chain: load the guide as a file)");
    ++version;
}

void VocalAlignerProcessor::stopCapture()
{
    if (! capturing.load()) return;
    capturing = false;
    const bool which[2] = { armGuide.load(), armDub.load() };
    armGuide = false; armDub = false;
    auto& ps = peakStore;
    for (int w = 0; w < 2; ++w)
    {
        if (! which[w]) continue;
        Take t;
        { const juce::SpinLock::ScopedLockType l (takeLock); std::swap (t, takes[w]); }
        t.data.resize ((size_t) std::max (0, t.length)); t.data.shrink_to_fit();
        if (t.length == 0) t.origin = 0;
        auto pk = makePeaks (t.data, t.length, ps.bin);
        { const juce::ScopedLock l (ps.lock); ps.peaks[w] = std::move (pk); ps.origin[w] = t.origin; }
        { const juce::SpinLock::ScopedLockType l (takeLock); std::swap (t, takes[w]); takeLen[w] = takes[w].length; }
    }
    ++version;
    if (! hasTake (Which::dub)) { setMessage ("Nothing captured - the host has to be playing while CAPTURE is on"); return; }
    if (! hasTake (Which::guide)) { setMessage ("Dub captured (" + juce::String (capturedSeconds (Which::dub), 1) + " s) - now load or drop the guide"); return; }
    if (pAuto->load() > 0.5f) startAlign();
    else setMessage ("Captured - press ALIGN");
}

void VocalAlignerProcessor::startAlign()
{
    if (capturing.load() || aligning.load()) return;
    if (! hasTake (Which::guide) || ! hasTake (Which::dub)) { setMessage ("Need both a guide and a dub take first"); return; }
    if (worker) worker->stopThread (10000);
    cancel = false; aligning = true; alignProgress = 0.0f;
    setMessage ("Aligning...");
    worker = std::make_unique<AlignJob> (*this);
    worker->startThread();
    ++version;
}

void VocalAlignerProcessor::cancelAlign()
{
    if (worker) { cancel = true; worker->stopThread (10000); worker.reset(); }
    aligning = false;
}

void VocalAlignerProcessor::runAlign()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::vector<float> g, d; juce::int64 gO, dO; int dLen;
    double sr;
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        const juce::int64 a = std::min (takes[0].origin, takes[1].origin);
        const juce::int64 b = std::max (takes[0].origin + takes[0].length, takes[1].origin + takes[1].length);
        g.assign ((size_t) (b - a), 0.0f); d.assign ((size_t) (b - a), 0.0f);
        std::copy (takes[0].data.begin(), takes[0].data.begin() + takes[0].length, g.begin() + (takes[0].origin - a));
        std::copy (takes[1].data.begin(), takes[1].data.begin() + takes[1].length, d.begin() + (takes[1].origin - a));
        gO = a; dO = takes[1].origin - a; dLen = takes[1].length; sr = sampleRate;
    }
    va::dsp::Settings s;
    s.tightness = pTight->load(); s.maxShiftMs = pShift->load();
    s.pitchAmount = pPitchOn->load() > 0.5f ? pPitchAmt->load() : 0.0f;
    auto R = va::dsp::align (g.data(), (int) g.size(), d.data(), (int) d.size(), sr, s, &cancel,
                             [this] (float p) { alignProgress.store (p); });
    if (! R.ok || cancel.load())
    {
        aligning = false;
        setMessage (cancel.load() ? "Align cancelled" : "Could not align these takes (too short?)");
        ++version; return;
    }
    // the result replaces the dub over the dub's own captured range
    Take out;
    out.data.assign (R.audio.begin() + dO, R.audio.begin() + dO + dLen);
    out.origin = gO + dO; out.length = dLen; out.sr = sr;
    double maxMove = 0.0;
    for (size_t i = 0; i < R.map.size(); ++i) maxMove = std::max (maxMove, std::fabs ((double) R.map[i] - (double) i) * 10.0);
    auto& ps = peakStore;
    auto pk = makePeaks (out.data, out.length, ps.bin);
    { const juce::ScopedLock l (ps.lock); ps.peaks[2] = std::move (pk); ps.origin[2] = out.origin; }
    { const juce::SpinLock::ScopedLockType l (takeLock); std::swap (takes[2], out); takeLen[2] = takes[2].length; }
    aligning = false; alignProgress = 1.0f;
    setMessage ("Aligned " + juce::String (dLen / sr, 1) + " s in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1)
                + " s - biggest move " + juce::String (juce::roundToInt (maxMove)) + " ms" + (s.pitchAmount > 0.0f ? ", pitch matched" : ""));
    ++version;
}

void VocalAlignerProcessor::clearTakes()
{
    if (capturing.load()) { capturing = false; armDub = false; armGuide = false; }
    cancelAlign();
    Take old[3];
    { const juce::SpinLock::ScopedLockType l (takeLock); for (int i = 0; i < 3; ++i) { std::swap (takes[i], old[i]); takeLen[i] = 0; } }
    auto& ps = peakStore;
    { const juce::ScopedLock l (ps.lock); for (auto& p : ps.peaks) p.clear(); }
    setMessage ("Cleared");
    ++version;
}

double VocalAlignerProcessor::capturedSeconds (Which w) const { return takeLen[(int) w].load() / sampleRate; }

juce::String VocalAlignerProcessor::loadFile (Which w, const juce::File& f)
{
    if (w == Which::aligned) return "Only the guide or the dub can be loaded";
    if (capturing.load()) return "Stop capturing first";
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return "Can't read " + f.getFileName();
    if (r->lengthInSamples / r->sampleRate > kMaxSeconds) return f.getFileName() + " is longer than 5 minutes";
    const int len = (int) r->lengthInSamples, ch = (int) juce::jmax (1u, r->numChannels);
    juce::AudioBuffer<float> b (ch, len);
    r->read (&b, 0, len, 0, true, true);
    // mono, resampled (linear) to the session rate
    const double ratio = r->sampleRate / sampleRate;
    Take t; t.sr = sampleRate; t.length = (int) std::floor (len / ratio);
    t.data.assign ((size_t) t.length, 0.0f);
    for (int i = 0; i < t.length; ++i)
    {
        const double p = i * ratio; const int k = (int) p; const float fr = (float) (p - k);
        float s = 0.0f;
        for (int c = 0; c < ch; ++c) { const float a = b.getSample (c, std::min (k, len - 1)), bb = b.getSample (c, std::min (k + 1, len - 1)); s += a + fr * (bb - a); }
        t.data[(size_t) i] = s / (float) ch;
    }
    // a file has no song position: it lines up with the other take (or the song start)
    const int other = w == Which::guide ? 1 : 0;
    { const juce::SpinLock::ScopedLockType l (takeLock); t.origin = takes[other].length > 0 ? takes[other].origin : 0; }
    cancelAlign();
    auto& ps = peakStore;
    auto pk = makePeaks (t.data, t.length, ps.bin);
    { const juce::ScopedLock l (ps.lock); ps.peaks[(int) w] = std::move (pk); ps.origin[(int) w] = t.origin; ps.peaks[2].clear(); }
    Take old, oldA;
    { const juce::SpinLock::ScopedLockType l (takeLock); std::swap (takes[(int) w], t); std::swap (takes[2], oldA); takeLen[(int) w] = takes[(int) w].length; takeLen[2] = 0; }
    setMessage ((w == Which::guide ? "Guide: " : "Dub: ") + f.getFileName());
    ++version;
    if (hasTake (Which::guide) && hasTake (Which::dub) && pAuto->load() > 0.5f) startAlign();
    return {};
}

juce::File VocalAlignerProcessor::exportFolder() const { return docsDir(); }

juce::File VocalAlignerProcessor::exportAligned (juce::String& error)
{
    std::vector<float> data; juce::int64 origin = 0; double sr = sampleRate;
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        if (takes[2].length <= 0) { error = "Nothing aligned yet"; return {}; }
        data.assign (takes[2].data.begin(), takes[2].data.begin() + takes[2].length); origin = takes[2].origin;
    }
    const auto dir = docsDir();
    if (! dir.createDirectory()) { error = "Can't create " + dir.getFullPathName(); return {}; }
    auto file = dir.getNonexistentChildFile ("Aligned dub " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav");
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (os.get())->failedToOpen()) { error = "Can't write " + file.getFullPathName(); return {}; }
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
    if (w == nullptr) { error = "Can't write a WAV file"; return {}; }
    // starts at the song's beginning, so it lines up when dropped at bar 1 (lead-in capped at 30 minutes)
    const juce::int64 lead = juce::jlimit<juce::int64> (0, (juce::int64) (1800.0 * sr), origin);
    std::vector<float> zeros (8192, 0.0f);
    for (juce::int64 done = 0; done < lead;)
    {
        const int k = (int) std::min<juce::int64> (8192, lead - done);
        const float* p = zeros.data(); w->writeFromFloatArrays (&p, 1, k); done += k;
    }
    const float* p = data.data();
    if (! w->writeFromFloatArrays (&p, 1, (int) data.size())) { error = "Write failed"; return {}; }
    w.reset();
    return file;
}

void VocalAlignerProcessor::overview (Which w, double t0, double t1, std::vector<std::pair<float, float>>& out) const
{
    auto& ps = peakStore;
    const juce::ScopedLock l (ps.lock);
    const auto& p = ps.peaks[(int) w];
    const double binSec = ps.bin / sampleRate;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double a = t0 + (t1 - t0) * (double) i / (double) out.size(), b = t0 + (t1 - t0) * (double) (i + 1) / (double) out.size();
        const auto ba = (juce::int64) std::floor ((a - ps.origin[(int) w] / sampleRate) / binSec), bb = (juce::int64) std::ceil ((b - ps.origin[(int) w] / sampleRate) / binSec);
        float lo = 0.0f, hi = 0.0f;
        for (auto k = std::max<juce::int64> (0, ba); k < std::min<juce::int64> ((juce::int64) p.size(), std::max (bb, ba + 1)); ++k) { lo = std::min (lo, p[(size_t) k].first); hi = std::max (hi, p[(size_t) k].second); }
        out[i] = { lo, hi };
    }
}

bool VocalAlignerProcessor::songRange (double& t0, double& t1) const
{
    auto& ps = peakStore;
    const juce::ScopedLock l (ps.lock);
    bool any = false;
    for (int w = 0; w < 3; ++w)
    {
        if (ps.peaks[w].empty()) continue;
        const double a = ps.origin[w] / sampleRate, b = a + ps.peaks[w].size() * ps.bin / sampleRate;
        t0 = any ? std::min (t0, a) : a; t1 = any ? std::max (t1, b) : b; any = true;
    }
    return any;
}

// ---------------------------------------------------------------------------------------------------- test hooks
void VocalAlignerProcessor::testSetTake (Which w, const std::vector<float>& data, juce::int64 origin, double sr)
{
    Take t; t.data = data; t.length = (int) data.size(); t.origin = origin; t.sr = sr;
    auto& ps = peakStore;
    auto pk = makePeaks (t.data, t.length, ps.bin);
    { const juce::ScopedLock l (ps.lock); ps.peaks[(int) w] = std::move (pk); ps.origin[(int) w] = origin; }
    { const juce::SpinLock::ScopedLockType l (takeLock); std::swap (takes[(int) w], t); takeLen[(int) w] = takes[(int) w].length; }
    ++version;
}
bool VocalAlignerProcessor::testWaitAlign (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (aligning.load() && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (10);
    return ! aligning.load() && takeLen[2].load() > 0;
}
std::vector<float> VocalAlignerProcessor::testAligned() const
{
    const juce::SpinLock::ScopedLockType l (takeLock);
    return { takes[2].data.begin(), takes[2].data.begin() + takes[2].length };
}

// ---------------------------------------------------------------------------------------------------- state
// Layout: "VAL1" | uint32 xmlBytes | xml | 3 x (uint32 bytes | FLAC of the take).  The XML holds parameters, take origins and the editor size.
juce::MemoryBlock VocalAlignerProcessor::encodeTake (const Take& t) const
{
    juce::MemoryBlock mb;
    if (t.length <= 0) return mb;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (mb, false);
    auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (t.sr > 0 ? t.sr : sampleRate).withNumChannels (1).withBitsPerSample (24));
    if (w == nullptr) return {};
    const float* p = t.data.data();
    w->writeFromFloatArrays (&p, 1, t.length);
    w.reset();
    return mb;
}

bool VocalAlignerProcessor::decodeTake (const juce::MemoryBlock& mb, Take& t) const
{
    if (mb.getSize() == 0) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
    if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) (kMaxSeconds * 192000.0)) return false;
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, false);
    t.data.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    t.length = b.getNumSamples(); t.sr = r->sampleRate;
    return true;
}

void VocalAlignerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("VocalAlignerState");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("presetName", presets.currentName()); xml.setAttribute ("presetUser", presets.currentIsUser());
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            xml.setAttribute (r->paramID, (double) r->convertFrom0to1 (r->getValue()));
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml.setAttribute ("uiW", uiWidth.load()); xml.setAttribute ("uiH", uiHeight.load()); }
    Take copy[3];
    if (! capturing.load())
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        for (int i = 0; i < 3; ++i) { copy[i].origin = takes[i].origin; copy[i].sr = takes[i].sr; copy[i].length = takes[i].length;
                                      copy[i].data.assign (takes[i].data.begin(), takes[i].data.begin() + std::max (0, takes[i].length)); }
    }
    for (int i = 0; i < 3; ++i) xml.setAttribute ("origin" + juce::String (i), juce::String (copy[i].origin));
    const auto text = xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    juce::MemoryOutputStream out (dest, false);
    out.write ("VAL1", 4);
    out.writeInt ((int) text.getNumBytesAsUTF8());
    out.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
    for (auto& t : copy) { const auto mb = encodeTake (t); out.writeInt ((int) mb.getSize()); out.write (mb.getData(), mb.getSize()); }
}

void VocalAlignerProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml;
    Take loaded[3]; bool haveTakes = false;
    if (size >= 8 && std::memcmp (data, "VAL1", 4) == 0)
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        in.skipNextBytes (4);
        const int xb = in.readInt();
        if (xb <= 0 || xb > size) return;
        juce::MemoryBlock xm; in.readIntoMemoryBlock (xm, xb);
        xml = juce::parseXML (xm.toString());
        for (int i = 0; i < 3 && ! in.isExhausted(); ++i)
        {
            const int nb = in.readInt();
            if (nb < 0 || nb > in.getNumBytesRemaining()) break;
            juce::MemoryBlock mb; if (nb > 0) in.readIntoMemoryBlock (mb, nb);
            decodeTake (mb, loaded[i]);
        }
        haveTakes = true;
    }
    else xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("VocalAlignerState")) return;
    presets.setCurrent (xml->getStringAttribute ("presetName", "Init"), xml->getBoolAttribute ("presetUser", false));
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (xml->hasAttribute (r->paramID))
                r->setValueNotifyingHost (r->convertTo0to1 ((float) xml->getDoubleAttribute (r->paramID)));
    const int w = xml->getIntAttribute ("uiW", 0), h = xml->getIntAttribute ("uiH", 0);
    if (w >= 300 && w <= 8000 && h >= 200 && h <= 6000) { uiWidth.store (w); uiHeight.store (h); }
    if (haveTakes)
    {
        if (capturing.load()) { capturing = false; armDub = false; armGuide = false; }
        cancelAlign();
        auto& ps = peakStore;
        for (int i = 0; i < 3; ++i)
        {
            loaded[i].origin = xml->getStringAttribute ("origin" + juce::String (i), "0").getLargeIntValue();
            auto pk = makePeaks (loaded[i].data, loaded[i].length, ps.bin);
            const juce::ScopedLock l (ps.lock); ps.peaks[i] = std::move (pk); ps.origin[i] = loaded[i].origin;
        }
        { const juce::SpinLock::ScopedLockType l (takeLock); for (int i = 0; i < 3; ++i) { std::swap (takes[i], loaded[i]); takeLen[i] = takes[i].length; } }
        if (takeLen[2].load() > 0) setMessage ("Aligned take restored (" + juce::String (capturedSeconds (Which::aligned), 1) + " s)");
        ++version;
    }
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::AudioProcessorEditor> (getActiveEditor())]
                                     { if (safe != nullptr) if (auto* e = dynamic_cast<VocalAlignerEditor*> (safe.getComponent())) e->applySavedSize(); });
}

juce::AudioProcessorEditor* VocalAlignerProcessor::createEditor() { return new VocalAlignerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalAlignerProcessor(); }
