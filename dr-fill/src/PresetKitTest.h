// Shared preset tests (test-only header; used by each plugin's Screenshot tool: modes "proc" and "keys").
#pragma once
#include "PresetKit.h"
#include <set>
#include <cmath>

namespace pk::test
{
// The preset bar / menu never take keyboard focus, prev / next / menu clicks load presets, Space is never consumed.
inline juce::String auditPresets (juce::Component& editor, PresetBar& bar, PresetMenu& menu, PresetManager& mgr)
{
    juce::String r; int bad = 0;
    auto chk = [&] (bool c, const juce::String& w) { r << (c ? "  ok   " : "  FAIL ") << w << "\n"; bad += c ? 0 : 1; };
    bool anyFocus = bar.getWantsKeyboardFocus() || bar.getMouseClickGrabsKeyboardFocus() || menu.getWantsKeyboardFocus();
    for (auto* c : bar.getChildren()) anyFocus = anyFocus || c->getWantsKeyboardFocus() || c->getMouseClickGrabsKeyboardFocus();
    chk (! anyFocus, "preset bar, its buttons and the preset menu never want keyboard focus");
    mgr.loadFactory (0);
    chk (mgr.currentName() == factoryPresets()[0].name, "factory preset 0 loads");
    mgr.step (1);
    chk (mgr.currentName() == factoryPresets()[1].name, "next steps to preset 2");
    mgr.step (-1); mgr.step (-1);
    chk (mgr.indexOfCurrent (mgr.userFiles()) == mgr.numFactory() + mgr.userFiles().size() - 1, "prev wraps around to the last preset");
    bar.openMenu();
    chk (menu.isOpen() && menu.numItems() >= mgr.numFactory() + 4 && editor.getLocalBounds().contains (menu.panelBounds()), "menu opens with every preset + save / load, inside the window");
    chk (! menu.getWantsKeyboardFocus() && ! menu.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)), "open menu does not take focus or eat Space");
    menu.triggerForTest (3);
    chk (! menu.isOpen() && mgr.currentName() == factoryPresets()[3].name, "clicking a menu item loads it and closes the menu");
    r << (bad == 0 ? "PRESET AUDIT OK\n" : "PRESET AUDIT FAILED\n");
    return r;
}

inline float realValue (juce::AudioProcessor& q, const juce::String& id)
{
    for (auto* p : q.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) if (r->getParameterID() == id) return r->convertFrom0to1 (r->getValue());
    return 0.0f;
}

// make(): new processor; mgr(p): its PresetManager.  Returns the number of failures.
template <typename Proc, typename GetMgr>
// extractXml: for plug-ins whose state is not plain JUCE binary XML (e.g. XML + audio blobs), returns the XML part; the stripped XML is
// then fed back through setStateInformation as plain binary XML, which such plug-ins must also accept (older / foreign sessions).
int procTests (GetMgr mgrOf, const juce::File& expectedUserDir, int numIn = 2, float peakLimit = 4.0f, double silentBelow = 1.0,
               std::function<std::unique_ptr<juce::XmlElement> (const juce::MemoryBlock&)> extractXml = {})
{
    int fails = 0; auto check = [&] (bool c, const char* w) { std::printf ("%s %s\n", c ? "ok:  " : "FAIL:", w); fails += c ? 0 : 1; };
    int bad = 0, silent = 0; juce::String names;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Proc q; q.setRateAndBufferSizeDetails (rate, 512); q.prepareToPlay (rate, 512);
        auto& m = mgrOf (q);
        const int nch = juce::jmax (numIn, q.getTotalNumOutputChannels());
        for (int i = 0; i < m.numFactory(); ++i)
        {
            m.loadFactory (i);
            juce::Random rng (i + 1); juce::AudioBuffer<float> b (nch, 512); juce::MidiBuffer midi; float pk = 0; bool fin = true; double e = 0; long ph = 0;
            for (int blk = 0; blk < (int) (rate * 3 / 512); ++blk)
            {
                b.clear();
                for (int c = 0; c < numIn; ++c) for (int k = 0; k < 512; ++k)
                {
                    const double t = (double) (ph + k) / rate;
                    b.setSample (c, k, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 150.0 * t) * (0.6f + 0.4f * (float) std::sin (t * 9.0))
                                     + 0.15f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1100.0 * t) + 0.1f * (rng.nextFloat() - 0.5f)
                                     + (k == 0 && blk % 40 == 0 ? 0.8f : 0.0f));
                }
                ph += 512; q.processBlock (b, midi);
                for (int c = 0; c < q.getTotalNumOutputChannels(); ++c) for (int k = 0; k < 512; ++k) { const float x = b.getSample (c, k); fin = fin && std::isfinite (x); pk = std::max (pk, std::fabs (x)); e += (double) x * x; }
            }
            if (! fin || pk > peakLimit) { ++bad; names << factoryPresets()[(size_t) i].name << " @" << (int) rate << " pk=" << pk << "; "; }
            if (e < silentBelow) { ++silent; names << factoryPresets()[(size_t) i].name << " @" << (int) rate << " silent e=" << e << "; "; }
        }
    }
    std::printf ("  %d presets x 3 rates: %d bad, %d silent %s\n", (int) factoryPresets().size(), bad, silent, names.toRawUTF8());
    check (bad == 0, "every factory preset: no NaN / inf, peak within limit at 44.1 / 48 / 96 kHz");
    check (silent == 0, "every factory preset passes sound");
    check (factoryPresets().size() >= 12, ">= 12 factory presets");
    {
        Proc tmp; std::set<juce::String> ids;
        for (auto* prm : tmp.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) ids.insert (r->getParameterID());
        bool ok = true; std::set<juce::String> seen;
        for (auto& fp : factoryPresets())
        {
            if (! seen.insert (fp.name).second) { ok = false; std::printf ("  duplicate name %s\n", fp.name); }
            for (auto& kv : fp.values) if (! ids.count (kv.first)) { ok = false; std::printf ("  unknown id %s in %s\n", kv.first, fp.name); }
        }
        check (ok, "factory presets: unique names, only real parameter ids");
    }
    {   // every parameter to a random non-default value, save as user preset, load into another instance
        Proc a; juce::Random rng (7); std::vector<std::pair<juce::String, float>> want;
        for (auto* prm : a.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        { r->setValueNotifyingHost (r->convertTo0to1 (r->convertFrom0to1 (rng.nextFloat()))); want.push_back ({ r->getParameterID(), r->convertFrom0to1 (r->getValue()) }); }
        auto& ma = mgrOf (a);
        const auto f = ma.userFileFor ("Round Trip");
        check (ma.saveFile (f, "Round Trip") && f.getParentDirectory() == ma.userDir() && ma.userDir() == expectedUserDir, "user preset saved in the plug-in's user preset folder");
        Proc b; auto& mb = mgrOf (b);
        bool same = mb.loadFile (f);
        for (auto& w : want) { if (mb.keepIds.contains (w.first)) continue; const float got = realValue (b, w.first);
            if (std::fabs (got - w.second) > 1e-3f * juce::jmax (1.0f, std::fabs (w.second))) { same = false; std::printf ("  %s: %f != %f\n", w.first.toRawUTF8(), got, w.second); } }
        check (same && mb.currentName() == "Round Trip" && mb.currentIsUser(), "user preset (all parameters) loads back into another instance");
        check (mb.userFiles().contains (f), "user presets are listed");
        juce::File bogus = ma.userDir().getChildFile ("Bogus.cmplxtypreset"); bogus.replaceWithText ("<CMPLXTY_PRESET plugin=\"Something Else\"/>");
        check (! mb.loadFile (juce::File ("/nonexistent.cmplxtypreset")) && ! mb.loadFile (bogus) && mb.currentName() == "Round Trip", "missing / foreign preset files refused, state untouched");
        f.deleteFile(); bogus.deleteFile();
    }
    {   // preset name in the session state; old sessions without one still load
        Proc a; auto& ma = mgrOf (a); ma.loadFactory (5);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        Proc b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (std::fabs (realValue (b, r->getParameterID()) - r->convertFrom0to1 (r->getValue())) > 1e-3f * juce::jmax (1.0f, std::fabs (r->convertFrom0to1 (r->getValue())))) same = false;
        check (same, "parameters survive save / load");
        check (mgrOf (b).currentName() == factoryPresets()[5].name, "preset name survives save / load");
        auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (x == nullptr && extractXml) x = extractXml (mb);
        if (x != nullptr)
        {
            std::function<void (juce::XmlElement&)> strip = [&] (juce::XmlElement& e) { e.removeAttribute ("presetName"); e.removeAttribute ("presetUser"); for (auto* c : e.getChildIterator()) strip (*c); };
            strip (*x); juce::MemoryBlock ob; juce::AudioProcessor::copyXmlToBinary (*x, ob);
            Proc c; c.setStateInformation (ob.getData(), (int) ob.getSize());
            check (mgrOf (c).currentName() == "Init" && std::fabs (realValue (c, a.getParameters()[0] ? dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[0])->getParameterID() : "") - realValue (a, dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[0])->getParameterID())) < 1e-3f,
                   "older session (no preset name) still loads, shows Init");
        }
        else check (false, "state is XML");
    }
    std::printf (fails == 0 ? "PROC TESTS PASSED\n" : "%d PROC TEST(S) FAILED\n", fails);
    return fails;
}
} // namespace pk::test
