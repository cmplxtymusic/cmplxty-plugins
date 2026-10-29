#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vz;

VisualizerProcessor::VisualizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", vz::makeLayout (vz::kParams))
{
    for (int i = 0; i < kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (kParams[i].id);
    const auto& f = presets::factory();
    for (int i = 0; i < kNumSlots; ++i) extras.slots[(size_t) i] = f[(size_t) (1 + i % ((int) f.size() - 1))].name;
    extras.lines = juce::StringArray::fromLines ("CMPLXTY\nVISUALIZER\nTYPE YOUR LYRICS\nIN THE TEXT TAB");
    analyzer.prepare (48000.0);
    features.setSampleRate (48000.0);
}

bool VisualizerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

void VisualizerProcessor::prepareToPlay (double sr, int)
{
    const double s = sr > 0 ? sr : 48000.0;
    analyzer.prepare (s);
    sampleRate.store (s);
}

void VisualizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    juce::ignoreUnused (midi);

    ana::Host h;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            h.valid = true;
            h.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            if (bpm.hasValue() && ppq.hasValue() && *bpm > 1.0) { h.hasTempo = true; h.bpm = *bpm; h.ppq = *ppq; }
        }
    hostPlaying.store (h.playing, std::memory_order_relaxed);
    const int nch = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    if (nch > 0) analyzer.process (buffer.getArrayOfReadPointers(), nch, n, h);   // read only: audio passes through
}

// ---- parameters ------------------------------------------------------------------------------------------------
ParamValues VisualizerProcessor::values() const noexcept
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = raw[(size_t) i]->load (std::memory_order_relaxed);
    return v;
}

void VisualizerProcessor::setParam (int p, float plain)
{
    if (auto* prm = apvts.getParameter (kParams[p].id))
    {
        const float norm = prm->convertTo0to1 (plain);
        if (std::abs (prm->getValue() - norm) > 1.0e-6f) prm->setValueNotifyingHost (norm);
    }
}

void VisualizerProcessor::setValues (const ParamValues& v, bool includeGlobals)
{
    for (int i = 0; i < kNumParams; ++i)
        if (includeGlobals || ! presets::isGlobalParam (i)) setParam (i, v[(size_t) i]);
}

// ---- extras ----------------------------------------------------------------------------------------------------
VisualizerProcessor::Extras VisualizerProcessor::getExtras() const
{
    const juce::ScopedLock sl (extrasLock);
    return extras;
}

juce::String VisualizerProcessor::sanitiseImage (juce::MemoryBlock& data, juce::String& name)
{
    if (data.getSize() == 0) return "empty file";
    if (data.getSize() > (size_t) 64 * 1024 * 1024) return "image is larger than 64 MB";
    auto img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
    if (! img.isValid()) return "not a JPG or PNG image";
    const int maxDim = 1920;
    const bool big = juce::jmax (img.getWidth(), img.getHeight()) > maxDim || data.getSize() > (size_t) 3 * 1024 * 1024;
    if (big)
    {
        const double s = juce::jmin (1.0, (double) maxDim / (double) juce::jmax (img.getWidth(), img.getHeight()));
        const int w = juce::jmax (1, juce::roundToInt (img.getWidth() * s)), h = juce::jmax (1, juce::roundToInt (img.getHeight() * s));
        auto scaled = img.rescaled (w, h, juce::Graphics::highResamplingQuality);
        juce::MemoryOutputStream mo;
        if (img.hasAlphaChannel()) { juce::PNGImageFormat png; png.writeImageToStream (scaled, mo); name = juce::File::createLegalFileName (juce::File (name).getFileNameWithoutExtension() + ".png"); }
        else { juce::JPEGImageFormat jpg; jpg.setQuality (0.9f); jpg.writeImageToStream (scaled, mo); name = juce::File::createLegalFileName (juce::File (name).getFileNameWithoutExtension() + ".jpg"); }
        data = mo.getMemoryBlock();
    }
    return {};
}

juce::String VisualizerProcessor::addImageFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") return "only JPG and PNG files";
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return "could not read " + f.getFileName();
    return addImageData (f.getFileName(), mb, ! f.isAChildOf (presets::imagesDir()));
}

juce::String VisualizerProcessor::addImageData (const juce::String& name0, const juce::MemoryBlock& data0, bool keepCopy)
{
    {
        const juce::ScopedLock sl (extrasLock);
        if ((int) extras.images.size() >= kMaxImages) return "up to " + juce::String (kMaxImages) + " images";
    }
    juce::MemoryBlock data (data0);
    juce::String name (name0.isEmpty() ? juce::String ("image.png") : name0);
    const auto err = sanitiseImage (data, name);
    if (err.isNotEmpty()) return err;
    if (keepCopy)
    {
        // keep a copy next to the presets so the image survives even if the original is moved (the installer keeps this folder)
        auto dir = presets::imagesDir();
        if (dir.createDirectory())
        {
            auto dst = dir.getChildFile (juce::File::createLegalFileName (name0.isEmpty() ? name : name0));
            if (! dst.existsAsFile()) dst.replaceWithData (data0.getData(), data0.getSize());
        }
    }
    {
        const juce::ScopedLock sl (extrasLock);
        extras.images.push_back ({ name, data });
        extras.selectedImage = (int) extras.images.size() - 1;
    }
    // loading an image switches the image layer on so you see it straight away
    if (value (pImgOn) < 0.5f && kModes[juce::jlimit (0, kNumModes - 1, (int) value (pMode))].cat != cImage) setParam (pImgOn, 1.0f);
    bump();
    return {};
}

void VisualizerProcessor::removeImage (int i)
{
    {
        const juce::ScopedLock sl (extrasLock);
        if (i < 0 || i >= (int) extras.images.size()) return;
        extras.images.erase (extras.images.begin() + i);
        extras.selectedImage = juce::jlimit (0, juce::jmax (0, (int) extras.images.size() - 1), extras.selectedImage >= i ? extras.selectedImage - 1 : extras.selectedImage);
    }
    bump();
}

void VisualizerProcessor::clearImages()
{
    { const juce::ScopedLock sl (extrasLock); extras.images.clear(); extras.selectedImage = 0; }
    bump();
}

void VisualizerProcessor::moveImage (int from, int to)
{
    {
        const juce::ScopedLock sl (extrasLock);
        const int n = (int) extras.images.size();
        if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
        auto e = extras.images[(size_t) from];
        extras.images.erase (extras.images.begin() + from);
        extras.images.insert (extras.images.begin() + to, e);
        extras.selectedImage = to;
    }
    bump();
}

void VisualizerProcessor::setSelectedImage (int i)
{
    { const juce::ScopedLock sl (extrasLock); extras.selectedImage = juce::jlimit (0, juce::jmax (0, (int) extras.images.size() - 1), i); }
    bump();
}

void VisualizerProcessor::setLines (const juce::StringArray& l, bool fromPreset)
{
    {
        const juce::ScopedLock sl (extrasLock);
        if (extras.lines == l && extras.linesFromPreset == fromPreset) return;
        extras.lines = l; extras.linesFromPreset = fromPreset;
    }
    bump();
}

juce::String VisualizerProcessor::setFontFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext != ".ttf" && ext != ".otf") return "only TTF and OTF fonts";
    juce::MemoryBlock mb;
    if (f.getSize() > 24 * 1024 * 1024 || ! f.loadFileAsData (mb)) return "could not read " + f.getFileName();
    if (juce::Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize()) == nullptr) return "not a usable font";
    { const juce::ScopedLock sl (extrasLock); extras.font = mb; extras.fontName = f.getFileNameWithoutExtension(); }
    bump();
    return {};
}

void VisualizerProcessor::clearFont()
{
    { const juce::ScopedLock sl (extrasLock); extras.font.reset(); extras.fontName.clear(); }
    bump();
}

void VisualizerProcessor::setTile (int i, const juce::String& n)
{
    if (i < 0 || i >= kMaxTiles) return;
    { const juce::ScopedLock sl (extrasLock); extras.tiles[(size_t) i] = n; }
    bump();
}

void VisualizerProcessor::setSlot (int i, const juce::String& n)
{
    if (i < 0 || i >= kNumSlots) return;
    { const juce::ScopedLock sl (extrasLock); extras.slots[(size_t) i] = n; }
    bump();
}

// ---- presets ---------------------------------------------------------------------------------------------------
juce::String VisualizerProcessor::currentPresetName() const { const juce::ScopedLock sl (extrasLock); return extras.presetName; }
void VisualizerProcessor::setPresetName (const juce::String& n) { { const juce::ScopedLock sl (extrasLock); extras.presetName = n; } bump(); }

juce::StringArray VisualizerProcessor::allPresetNames() const
{
    juce::StringArray s;
    for (auto& f : presets::factory()) s.add (f.name);
    for (auto& f : presets::userPresets()) s.add ("user:" + f.getFileNameWithoutExtension());
    return s;
}

bool VisualizerProcessor::resolvePreset (const juce::String& name, ParamValues& out, juce::String* text) const
{
    if (name.startsWith ("user:"))
    {
        auto f = presets::userDir().getChildFile (name.substring (5) + presets::kExt);
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("CVZPRESET")) return false;
        out = presets::defaults();
        if (auto* p = xml->getChildByName ("PARAMS"))
            for (int i = 0; i < kNumParams; ++i)
                if (p->hasAttribute (kParams[i].id)) out[(size_t) i] = presets::parseValue (i, p->getStringAttribute (kParams[i].id));
        if (text != nullptr) if (auto* t = xml->getChildByName ("EXTRA")) *text = t->getChildElementAllSubText ("TEXT", {});
        return true;
    }
    for (auto& f : presets::factory())
        if (f.name == name) { out = presets::valuesFor (f); if (text != nullptr) *text = f.text; return true; }
    return false;
}

void VisualizerProcessor::applyFactory (int index)
{
    const auto& fs = presets::factory();
    if (index < 0 || index >= (int) fs.size()) return;
    const auto& f = fs[(size_t) index];
    undoManager.beginNewTransaction ("Preset");
    setValues (presets::valuesFor (f));
    bool linesFromPreset;
    { const juce::ScopedLock sl (extrasLock); linesFromPreset = extras.linesFromPreset || extras.lines.isEmpty(); extras.presetName = f.name; }
    if (f.text.isNotEmpty() && linesFromPreset) setLines (juce::StringArray::fromLines (f.text), true);
    bump();
}

bool VisualizerProcessor::applyPreset (const juce::String& name)
{
    if (name.startsWith ("user:")) return loadUserPreset (presets::userDir().getChildFile (name.substring (5) + presets::kExt));
    const auto& fs = presets::factory();
    for (size_t i = 0; i < fs.size(); ++i)
        if (fs[i].name == name) { applyFactory ((int) i); return true; }
    return false;
}

void VisualizerProcessor::writeExtras (juce::XmlElement& parent, bool withImages, bool withSession) const
{
    const juce::ScopedLock sl (extrasLock);
    auto* e = parent.createNewChildElement ("EXTRA");
    e->setAttribute ("version", 1);
    e->setAttribute ("selectedImage", extras.selectedImage);
    e->setAttribute ("linesFromPreset", extras.linesFromPreset ? 1 : 0);
    e->createNewChildElement ("TEXT")->addTextElement (extras.lines.joinIntoString ("\n"));
    if (extras.font.getSize() > 0)
    {
        auto* f = e->createNewChildElement ("FONT");
        f->setAttribute ("name", extras.fontName);
        f->addTextElement (extras.font.toBase64Encoding());
    }
    if (withImages)
        for (auto& im : extras.images)
        {
            auto* x = e->createNewChildElement ("IMAGE");
            x->setAttribute ("name", im.name);
            x->addTextElement (im.data.toBase64Encoding());
        }
    if (withSession)
    {
        e->setAttribute ("preset", extras.presetName);
        for (int i = 0; i < kMaxTiles; ++i)
            if (extras.tiles[(size_t) i].isNotEmpty()) { auto* t = e->createNewChildElement ("TILE"); t->setAttribute ("i", i); t->setAttribute ("preset", extras.tiles[(size_t) i]); }
        for (int i = 0; i < kNumSlots; ++i)
        { auto* t = e->createNewChildElement ("SLOT"); t->setAttribute ("i", i); t->setAttribute ("preset", extras.slots[(size_t) i]); }
    }
}

void VisualizerProcessor::readExtras (const juce::XmlElement& e, bool withSession)
{
    Extras x;
    { const juce::ScopedLock sl (extrasLock); x = extras; }
    if (auto* t = e.getChildByName ("TEXT")) { x.lines = juce::StringArray::fromLines (t->getAllSubText()); x.linesFromPreset = e.getIntAttribute ("linesFromPreset", 1) != 0; }
    if (auto* f = e.getChildByName ("FONT"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (f->getAllSubText().trim()) && mb.getSize() > 0) { x.font = mb; x.fontName = f->getStringAttribute ("name"); }
    }
    else if (withSession) { x.font.reset(); x.fontName.clear(); }
    bool sawImage = false;
    std::vector<ImageEntry> imgs;
    for (auto* im : e.getChildWithTagNameIterator ("IMAGE"))
    {
        sawImage = true;
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (im->getAllSubText().trim()) && mb.getSize() > 0 && (int) imgs.size() < kMaxImages)
            imgs.push_back ({ im->getStringAttribute ("name", "image"), mb });
    }
    if (sawImage || withSession) { x.images = std::move (imgs); x.selectedImage = juce::jlimit (0, juce::jmax (0, (int) x.images.size() - 1), e.getIntAttribute ("selectedImage", 0)); }
    if (withSession)
    {
        x.presetName = e.getStringAttribute ("preset", x.presetName);
        for (auto& t : x.tiles) t.clear();
        for (auto* t : e.getChildWithTagNameIterator ("TILE"))
        { const int i = t->getIntAttribute ("i", -1); if (i >= 0 && i < kMaxTiles) x.tiles[(size_t) i] = t->getStringAttribute ("preset"); }
        for (auto* t : e.getChildWithTagNameIterator ("SLOT"))
        { const int i = t->getIntAttribute ("i", -1); if (i >= 0 && i < kNumSlots) x.slots[(size_t) i] = t->getStringAttribute ("preset"); }
    }
    { const juce::ScopedLock sl (extrasLock); extras = std::move (x); }
    bump();
}

juce::File VisualizerProcessor::saveUserPreset (const juce::String& name0, bool withImages)
{
    const auto name = juce::File::createLegalFileName (name0.trim()).substring (0, 64);
    if (name.isEmpty()) return {};
    juce::XmlElement root ("CVZPRESET");
    root.setAttribute ("name", name);
    root.setAttribute ("version", VZ_VERSION_STRING);
    auto* p = root.createNewChildElement ("PARAMS");
    const auto v = values();
    for (int i = 0; i < kNumParams; ++i)
        if (! presets::isGlobalParam (i)) p->setAttribute (kParams[i].id, presets::formatValue (i, v[(size_t) i]));
    writeExtras (root, withImages, false);
    auto dir = presets::userDir();
    dir.createDirectory();
    auto f = dir.getChildFile (name + presets::kExt);
    if (! root.writeTo (f)) return {};
    setPresetName ("user:" + name);
    return f;
}

bool VisualizerProcessor::loadUserPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("CVZPRESET")) return false;
    undoManager.beginNewTransaction ("Preset");
    auto v = presets::defaults();
    if (auto* p = xml->getChildByName ("PARAMS"))
        for (int i = 0; i < kNumParams; ++i)
            if (p->hasAttribute (kParams[i].id)) v[(size_t) i] = presets::parseValue (i, p->getStringAttribute (kParams[i].id));
    setValues (v);
    if (auto* e = xml->getChildByName ("EXTRA")) readExtras (*e, false);
    const bool inUserDir = f.getParentDirectory() == presets::userDir();
    setPresetName (inUserDir ? "user:" + f.getFileNameWithoutExtension() : f.getFileNameWithoutExtension());
    return true;
}

// ---- state -----------------------------------------------------------------------------------------------------
void VisualizerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr) return;
    if (uiWidth.load() > 0 && uiHeight.load() > 0) { xml->setAttribute ("uiW", uiWidth.load()); xml->setAttribute ("uiH", uiHeight.load()); }
    writeExtras (*xml, true, true);
    copyXmlToBinary (*xml, dest);
}

void VisualizerProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    uiWidth.store (xml->getIntAttribute ("uiW", 0)); uiHeight.store (xml->getIntAttribute ("uiH", 0));
    if (auto* e = xml->getChildByName ("EXTRA"))
    {
        readExtras (*e, true);
        xml->removeChildElement (e, true);   // the parameter tree must not carry the (large) extras
    }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VisualizerProcessor::createEditor() { return new VisualizerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VisualizerProcessor(); }
