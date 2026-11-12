// Real end-to-end test of the hub logic on Linux: live GitHub release list, real installers unpacked into a temp prefix
// laid out like Windows and like macOS. Usage: PluginHubTest [win|mac|both] [workdir]
#include "../src/HubCore.h"
#include <iostream>
#include <cstdlib>
using juce::String; using juce::File;
static int fails = 0;
#define CHECK(c, what) do { bool ok_ = (c); std::cout << (ok_ ? "  PASS  " : "  FAIL  ") << what << std::endl; if (! ok_) ++fails; } while (0)

static String treeHash (const File& f)
{
    if (! f.exists()) return "missing";
    if (f.existsAsFile()) return hub::sha256Of (f);
    String all;
    for (auto& e : juce::RangedDirectoryIterator (f, true, "*", File::findFiles)) all << e.getFile().getRelativePathFrom (f) << ":" << hub::sha256Of (e.getFile()) << ";";
    return juce::SHA256 (all.toUTF8()).toHexString();
}
static hub::Plugin* find (juce::Array<hub::Plugin>& l, const String& slug) { for (auto& p : l) if (p.slug == slug) return &p; return nullptr; }

static void runPlatform (hub::Platform plat, const File& root)
{
    auto pn = hub::platformName (plat);
    std::cout << "\n=== " << pn << " layout, prefix " << root.getFullPathName() << " ===" << std::endl;
    root.deleteRecursively();
    auto L = hub::Layout::prefix (root, plat);

    // A. catalog
    juce::Array<hub::Plugin> cat; String err;
    CHECK (hub::fetchCatalog (cat, L, err), "fetch live release list " + err);
    std::cout << "  detected " << cat.size() << " plugins:" << std::endl;
    bool allowed = true, noApps = true; juce::StringArray noSum;
    for (auto& p : cat)
    {
        std::cout << "    " << p.slug.paddedRight (' ', 18) << p.name.paddedRight (' ', 18) << p.type.paddedRight (' ', 11) << p.latest.paddedRight (' ', 7)
                  << p.assetName << "  sha " << p.sha256.substring (0, 12) << (p.notes.isNotEmpty() ? "  [version.json]" : "") << std::endl;
        allowed &= hub::isAllowedDownload (p.url) && ! p.url.contains ("mac-builds");
        if (! p.checksumOk()) noSum.add (p.slug);
        noApps &= ! p.slug.startsWith ("yoink");
    }
    CHECK (cat.size() >= 17, "at least 17 VST3/CLAP plugins listed (" + String (cat.size()) + ")");
    CHECK (allowed, "every download URL is a per-tag cmplxty-plugins release asset (no mac-builds)");
    CHECK (noSum.size() <= 1 && (noSum.isEmpty() || noSum[0] == "quartz"), "sha256 published for all but: [" + noSum.joinIntoString (",") + "] (known: Quartz win exe missing from its SHA256SUMS)");
    if (auto* q = find (cat, "quartz"); q && ! q->checksumOk())
    {
        juce::Array<hub::Plugin> qs { *q };
        auto r = hub::installSelected (qs, L, [] (const String&) {});
        CHECK (! r[0].ok && ! q->installedVst3.exists(), "plugin without a checksum is refused (shown as NO CHECKSUM): " + r[0].message);
    }
    CHECK (noApps, "Yoink / standalone apps left out");
    for (auto& p : cat) CHECK (p.installed.isEmpty(), "fresh prefix: " + p.slug + " not installed");

    // presets the user already has
    auto pr = L.presetRoots[0];
    pr.getChildFile ("Dr Fill/Presets/My Kit.cmplxtypreset").create(); pr.getChildFile ("Dr Fill/Presets/My Kit.cmplxtypreset").replaceWithText ("CMPLXTY_PRESET my kit 1");
    pr.getChildFile ("Dr Fill/settings.json").replaceWithText ("{\"checkForUpdates\":true}");
    pr.getChildFile ("Silt/Presets/Dust.cmplxtypreset").create(); pr.getChildFile ("Silt/Presets/Dust.cmplxtypreset").replaceWithText ("CMPLXTY_PRESET dust");
    L.presetRoots[1].getChildFile ("Presets/Vox.qpreset").create(); L.presetRoots[1].getChildFile ("Presets/Vox.qpreset").replaceWithText ("quartz vox");
    auto presets0 = hub::snapshot (L.presetRoots);

    // B. fresh install of two plugins
    auto* df = find (cat, "dr-fill"); auto* si = find (cat, "silt");
    CHECK (df && si, "dr-fill and silt in catalog");
    if (! df || ! si) return;
    juce::Array<hub::Plugin> sel { *df, *si };
    auto log = [] (const String& s) { std::cout << "    | " << s << std::endl; };
    auto res = hub::installSelected (sel, L, log);
    CHECK (res.size() == 2 && res[0].ok && res[1].ok, "fresh install dr-fill + silt");
    juce::Array<hub::Plugin> cat2; hub::fetchCatalog (cat2, L, err, false);
    df = find (cat2, "dr-fill"); si = find (cat2, "silt");
    CHECK (df->installed == df->latest && df->action() == hub::Action::Reinstall, "dr-fill shows installed " + df->installed + " = latest -> REINSTALL offered");
    CHECK (si->installed == si->latest, "silt installed " + si->installed);
    CHECK (df->installedVst3.exists() && df->installedClap.exists(), "dr-fill VST3 + CLAP in the standard folders: " + df->installedVst3.getFullPathName());
    int otherInstalled = 0; for (auto& p : cat2) if (p.slug != "dr-fill" && p.slug != "silt") otherInstalled += p.installed.isNotEmpty();
    CHECK (otherInstalled == 0, "nothing else was installed");
    CHECK (hub::diffMissingOrChanged (presets0, hub::snapshot (L.presetRoots)).isEmpty(), "presets intact after fresh install");

    // C. pretend an old Dr Fill is installed -> UPDATE; silt stays unchecked and must not be touched
    auto vfile = plat == hub::Platform::Mac ? df->installedVst3.getChildFile ("Contents/Info.plist") : df->installedVst3.getChildFile ("Contents/Resources/moduleinfo.json");
    auto t = vfile.loadFileAsString();
    vfile.replaceWithText (plat == hub::Platform::Mac ? t.replace ("<string>" + df->latest + "</string>", "<string>0.9.0</string>")
                                                     : t.replace ("\"Version\": \"" + df->latest + "\"", "\"Version\": \"0.9.0\""));
    df->installedVst3.getChildFile ("Contents/stale-file-from-0.9.txt").replaceWithText ("old");
    pr.getChildFile ("Dr Fill/Presets/Second.cmplxtypreset").replaceWithText ("CMPLXTY_PRESET second");
    auto presets1 = hub::snapshot (L.presetRoots);
    hub::detectInstalled (*df, L);
    CHECK (df->installed == "0.9.0" && df->action() == hub::Action::Update, "old dr-fill detected as 0.9.0 -> UPDATE offered");
    auto siltV = treeHash (si->installedVst3), siltC = treeHash (si->installedClap);
    juce::Array<hub::Plugin> sel2 { *df };
    res = hub::installSelected (sel2, L, log);
    CHECK (res.size() == 1 && res[0].ok, "update dr-fill 0.9.0 -> " + df->latest);
    hub::detectInstalled (*df, L);
    CHECK (df->installed == df->latest, "dr-fill now " + df->installed);
    CHECK (! df->installedVst3.getChildFile ("Contents/stale-file-from-0.9.txt").exists(), "old bundle replaced, not merged");
    CHECK (treeHash (si->installedVst3) == siltV && treeHash (si->installedClap) == siltC, "unchecked silt VST3 + CLAP byte-identical");
    auto diff = hub::diffMissingOrChanged (presets1, hub::snapshot (L.presetRoots));
    CHECK (diff.isEmpty() && presets1.files.size() == 5, "all 5 preset/settings files intact after update (sha256 compared)");
    auto backups = L.work.getChildFile ("preset-backups").findChildFiles (File::findDirectories, false);
    CHECK (backups.size() == 2, "a preset backup was made before each install run (" + String (backups.size()) + ")");

    // C2. a ticked up-to-date plugin is skipped: no download, files byte-identical
    {
        auto siltV2 = treeHash (si->installedVst3), siltC2 = treeHash (si->installedClap);
        auto pre = hub::snapshot (L.presetRoots);
        auto dead = *si; dead.url = "https://127.0.0.1:1/CMPLXTY-Silt-Setup-nope.exe";
        dead.assetName = "CMPLXTY-Silt-Setup-nope.exe";
        CHECK (dead.action() == hub::Action::Reinstall, "silt is up to date -> REINSTALL");
        juce::Array<hub::Plugin> skip { dead };
        auto rs = hub::installSelected (skip, L, log);
        CHECK (rs.size() == 1 && rs[0].ok && rs[0].message.containsIgnoreCase ("already up to date"),
               "up-to-date ticked row skipped: " + rs[0].message);
        CHECK (treeHash (si->installedVst3) == siltV2 && treeHash (si->installedClap) == siltC2, "skip left silt files byte-identical");
        CHECK (hub::diffMissingOrChanged (pre, hub::snapshot (L.presetRoots)).isEmpty(), "skip left presets intact");
        CHECK (! L.work.getChildFile ("downloads").getChildFile (dead.tag).getChildFile (dead.assetName).exists(),
               "skip did not download");
        dead.force = true;
        juce::Array<hub::Plugin> forced { dead };
        auto rf = hub::installSelected (forced, L, log);
        CHECK (rf.size() == 1 && ! rf[0].ok, "force reinstall with a dead URL attempted a download and failed: " + rf[0].message);
        CHECK (treeHash (si->installedVst3) == siltV2, "failed force reinstall left silt untouched");
    }

    // D. preset damage guard: a file the installer clobbered is put back from the backup
    auto bk = hub::backupPresets (L, err);
    auto snap = hub::snapshot (L.presetRoots);
    pr.getChildFile ("Dr Fill/Presets/My Kit.cmplxtypreset").deleteFile();
    auto miss = hub::diffMissingOrChanged (snap, hub::snapshot (L.presetRoots));
    CHECK (miss.size() == 1 && hub::restoreMissing (bk, L, miss) && hub::diffMissingOrChanged (snap, hub::snapshot (L.presetRoots)).isEmpty(), "deleted preset restored from backup");

    // E. safety: wrong checksum / non-release URL never install
    // Use an Update (not Reinstall) so installSelected doesn't skip it as already up to date.
    auto bad = *si; bad.installed = "0.9.0"; bad.sha256 = String::repeatedString ("0", 64); bad.tag += "-badsum";
    juce::Array<hub::Plugin> sel3 { bad };
    res = hub::installSelected (sel3, L, log);
    CHECK (! res[0].ok && res[0].message.contains ("checksum"), "checksum mismatch refused: " + res[0].message);
    CHECK (treeHash (si->installedVst3) == siltV, "silt untouched after refused install");
    CHECK (! hub::isAllowedDownload ("https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/mac-builds-6/x.pkg"), "mac-builds URL refused");
    CHECK (! hub::isAllowedDownload ("https://example.com/x.exe"), "foreign URL refused");

    // F. cancel before start: nothing downloaded or changed (use an Update so it isn't skipped as up-to-date)
    {
        std::atomic<bool> stop { true };
        auto upd = *si; upd.installed = "0.0.1";
        juce::Array<hub::Plugin> s4 { upd };
        auto r4 = hub::installSelected (s4, L, log, {}, &stop);
        CHECK (r4.size() == 1 && ! r4[0].ok && r4[0].message == "cancelled", "cancel -> reported cancelled");
        CHECK (treeHash (si->installedVst3) == siltV, "cancel -> silt untouched");
    }
    // G. batch-wide progress runs 0 -> 1000 without going backwards (force, so up-to-date rows aren't skipped)
    {
        auto a = *df, b = *si; a.force = b.force = true;
        juce::Array<hub::Plugin> s5 { a, b }; juce::int64 last = -1; bool mono = true;
        hub::installSelected (s5, L, {}, [&] (juce::int64 d, juce::int64 t) { mono = mono && t == 1000 && d >= last; last = d; });
        CHECK (mono && last == 1000, "batch progress monotonic, ends at 1000 (" + String (last) + ")");
    }
    // H. unreadable version -> shown as unknown, offered as reinstall (not a forced update)
    {
        auto mi = df->installedVst3.getChildFile ("Contents/Resources/moduleinfo.json"), ip = df->installedVst3.getChildFile ("Contents/Info.plist");
        auto keep = mi.loadFileAsString(), keepP = ip.loadFileAsString(); bool hadP = ip.existsAsFile();
        mi.deleteFile(); ip.deleteFile();
        // 1.0.3: on Windows the DLL's VERSIONINFO is now a fallback (the Quartz fix) -> take it away too, to get a truly unreadable bundle
        juce::MemoryBlock keepDll; File dll;
        if (plat == hub::Platform::Windows)
            for (auto f : df->installedVst3.findChildFiles (File::findFiles, true, "*.vst3")) { dll = f; f.loadFileAsData (keepDll); f.replaceWithText ("no version here"); break; }
        auto t2 = *df; hub::detectInstalled (t2, L);
        if (dll != File()) dll.replaceWithData (keepDll.getData(), keepDll.getSize());
        if (hadP) ip.replaceWithText (keepP);
        CHECK (t2.installed == "unknown" && t2.action() == hub::Action::Unknown, "missing version info -> unknown / REINSTALL (VERSION ?) [" + t2.installed + "]");
        mi.replaceWithText (keep);
    }
    // I. uninstall dr-fill: its bundles go, silt + presets stay
    {
        auto pre = hub::snapshot (L.presetRoots);
        hub::detectInstalled (*df, L);
        auto v3 = df->installedVst3, cl = df->installedClap;
        CHECK (hub::isSafeToRemove (v3, *df, L) && hub::isSafeToRemove (cl, *df, L), "dr-fill bundles are removable");
        CHECK (! hub::isSafeToRemove (si->installedVst3, *df, L), "silt bundle never removable as dr-fill");
        CHECK (! hub::isSafeToRemove (L.presetRoots[0], *df, L), "preset folder never removable");
        auto decoy = L.vst3Dirs[0].getSiblingFile ("CMPLXTY Dr Fill.vst3"); decoy.createDirectory();
        CHECK (! hub::isSafeToRemove (decoy, *df, L), "same name outside the VST3 folders never removable");
        juce::Array<hub::Plugin> s6 { *df };
        auto r6 = hub::uninstallSelected (s6, L, log);
        CHECK (r6.size() == 1 && r6[0].ok && ! v3.exists() && ! cl.exists() && decoy.exists(), "uninstall dr-fill removed VST3 + CLAP only");
        CHECK (treeHash (si->installedVst3) == siltV && treeHash (si->installedClap) == siltC, "uninstall left silt alone");
        CHECK (hub::diffMissingOrChanged (pre, hub::snapshot (L.presetRoots)).isEmpty(), "uninstall left presets intact");
        juce::Array<hub::Plugin> cat3; hub::fetchCatalog (cat3, L, err, false);
        CHECK (find (cat3, "dr-fill")->installed.isEmpty(), "dr-fill shows not installed after uninstall");
        decoy.deleteRecursively();
    }
    // J. offline refresh is fast and falls back to the saved list
    {
        setenv ("https_proxy", "http://127.0.0.1:9", 1);
        setenv ("HTTPS_PROXY", "http://127.0.0.1:9", 1);
        auto t0 = juce::Time::getMillisecondCounter();
        juce::Array<hub::Plugin> off; String e2;
        bool ok = hub::fetchCatalog (off, L, e2);
        auto ms = juce::Time::getMillisecondCounter() - t0;
        unsetenv ("https_proxy"); unsetenv ("HTTPS_PROXY");
        CHECK (ok && off.size() >= 18 && e2.contains ("offline"), "offline -> saved list used (" + e2 + ")");
        CHECK (ms < 15000, "offline refresh took " + String ((int) ms) + " ms (< 15 s)");
    }
}

int main (int argc, char** argv)
{
    
    String which = argc > 1 ? argv[1] : "both";
    File work = argc > 2 ? File (argv[2]) : File ("/tmp/plugin-hub-test");

    std::cout << "=== offline logic ===" << std::endl;
    CHECK (hub::compareVersions ("1.0.10", "1.0.9") > 0 && hub::compareVersions ("v1.0.0", "1.0") == 0, "version compare");
    String rel = R"([{"tag_name":"mac-builds-6","assets":[{"name":"CMPLXTY-Rewired-Setup-1.0.1-mac.pkg","browser_download_url":"x"}]},
      {"tag_name":"yoink-v2.1.2","assets":[{"name":"Yoink-Setup.exe"}]},
      {"tag_name":"drift-shifter-v1.0.2","assets":[{"name":"CMPLXTY-Drift-Setup-1.0.2-win.exe","browser_download_url":"a"}]},
      {"tag_name":"drift-shifter-v1.0.3","assets":[{"name":"CMPLXTY-Drift-Setup-1.0.3-win.exe","browser_download_url":"b"},{"name":"CMPLXTY-Drift-Setup-1.0.3-mac.pkg"}]},
      {"tag_name":"plugin-hub-v9.9.9","assets":[]}])";
    juce::StringArray skipped;
    auto pl = hub::parseReleases (rel, hub::Platform::Windows, &skipped);
    CHECK (pl.size() == 1 && pl[0].latest == "1.0.3" && pl[0].bundle == "CMPLXTY Drift" && pl[0].url == "b", "parse: newest per-tag release, mac-builds + yoink + hub skipped");
    for (auto& s : skipped) std::cout << "    skipped " << s << std::endl;
    String fresh = R"([
      {"tag_name":"fallout-v1.0.0","assets":[{"name":"CMPLXTY-FALLOUT-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/fallout-v1.0.0/x.exe"}]},
      {"tag_name":"heft-v1.0.0","assets":[{"name":"CMPLXTY-HEFT-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/heft-v1.0.0/x.exe"}]},
      {"tag_name":"smother-v1.0.0","assets":[{"name":"CMPLXTY-SMOTHER-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/smother-v1.0.0/x.exe"}]},
      {"tag_name":"rigged-v1.0.0","assets":[{"name":"CMPLXTY-RIGGED-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/rigged-v1.0.0/x.exe"}]},
      {"tag_name":"fever-v1.0.0","assets":[{"name":"CMPLXTY-FEVER-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/fever-v1.0.0/x.exe"}]},
      {"tag_name":"yoink-v3.0.0","assets":[{"name":"Yoink-Setup-3.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/yoink-v3.0.0/x.exe"}]},
      {"tag_name":"plugin-hub-v1.0.4","assets":[{"name":"CMPLXTY-Plugin-Hub-Setup-1.0.4-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/plugin-hub-v1.0.4/x.exe"}]},
      {"tag_name":"mac-builds-99","assets":[{"name":"CMPLXTY-FALLOUT-Setup-1.0.0-win.exe","browser_download_url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/mac-builds-99/x.exe"}]}
    ])";
    juce::StringArray skippedFresh;
    auto nw = hub::parseReleases (fresh, hub::Platform::Windows, &skippedFresh);
    juce::StringArray slugs;
    bool allInstall = true, allNamed = true;
    for (auto& p : nw) { slugs.add (p.slug); allInstall = allInstall && p.action() == hub::Action::Install && p.installed.isEmpty(); allNamed = allNamed && p.name.isNotEmpty() && p.bundle.isNotEmpty(); }
    slugs.sortNatural();
    CHECK (nw.size() == 5 && slugs.joinIntoString (",") == "fallout,fever,heft,rigged,smother",
           "brand-new slugs appear from <slug>-vX.Y.Z tags with no hardcoded catalog (" + slugs.joinIntoString (",") + ")");
    CHECK (allInstall && allNamed, "new slugs show as Install with name/bundle taken from the installer filename");
    bool excluded = true;
    for (auto& s : slugs) excluded = excluded && s != "yoink" && s != "plugin-hub";
    CHECK (excluded && skippedFresh.joinIntoString (" ").contains ("yoink") && skippedFresh.joinIntoString (" ").contains ("plugin-hub")
           && skippedFresh.joinIntoString (" ").contains ("mac-builds"), "yoink, plugin-hub and mac-builds still excluded");
    auto typ = nw[0]; typ.slug = "fallout"; typ.type = hub::typeFor ("fallout"); typ.url = "https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/fallout-v1.0.0/x.exe"; typ.latest = "1.0.0";
    hub::applyVersionJson (typ, R"({"version":"1.0.0","url":"https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/fallout-v1.0.0/x.exe","sha256":")" + String::repeatedString ("ab", 32) + R"(","type":"Synth"})", hub::Platform::Windows);
    CHECK (typ.type == "Synth" && typ.sha256.length() == 64, "version.json type field overrides the default FX label (" + typ.type + ")");
    CHECK (hub::parseSumsFile ("aaaa  x\n" + String::repeatedString ("ab", 32) + "  CMPLXTY-Drift-Setup-1.0.3-win.exe\n", "CMPLXTY-Drift-Setup-1.0.3-win.exe") == String::repeatedString ("ab", 32), "SHA256SUMS parse");
    auto up = hub::checkHubUpdate (hub::Platform::Mac, rel);
    CHECK (up.available && up.version == "9.9.9", "hub self-update: newer plugin-hub-v9.9.9 release detected (" + up.page + ")");
    auto up2 = hub::checkHubUpdate (hub::Platform::Mac, "[]");
    CHECK (! up2.available, "hub self-update: nothing newer -> no banner");

    {
        std::cout << "\n=== button label ===" << std::endl;
        juce::Array<hub::Plugin> l; String sum = String::repeatedString ("a", 64);
        hub::Plugin n; n.latest = "1.0.1"; n.sha256 = sum;                       // not installed
        hub::Plugin u = n; u.installed = "1.0.0";                                // update
        hub::Plugin r = n; r.installed = "1.0.1";                                // reinstall
        hub::Plugin x = n; x.sha256 = {};                                         // no checksum
        l.add (n); l.add (u); l.add (r); l.add (x);
        auto lab = [&] (std::initializer_list<bool> c) { juce::Array<bool> a; for (auto b : c) a.add (b); return hub::primaryLabel (l, a); };
        CHECK (lab ({ false, false, false, false }).isEmpty(), "nothing ticked (but work remains) -> no label");
        CHECK (lab ({ true, false, false, false }) == "Install 1", "new only -> Install 1");
        CHECK (lab ({ false, true, false, false }) == "Update 1", "update only -> Update 1");
        CHECK (lab ({ true, true, false, false }) == "Install 1 · Update 1", "mix -> Install 1 · Update 1");
        CHECK (lab ({ false, true, true, false }) == "Update 1", "update + up-to-date ticked -> Update 1 (up-to-date ignored)");
        CHECK (lab ({ false, false, true, false }).isEmpty(), "only an up-to-date row ticked (work remains) -> no label");
        CHECK (lab ({ false, false, false, true }).isEmpty(), "no-checksum row never counts");
        hub::Plugin n2 = n; n2.slug = "two";
        juce::Array<hub::Plugin> mix { n, n2, u, r, x };
        juce::Array<bool> mixOn { true, true, true, true, true };
        CHECK (hub::primaryLabel (mix, mixOn) == "Install 2 · Update 1", "two new + one update + up-to-date ticked -> Install 2 · Update 1");
        hub::Plugin k = n; k.installed = "unknown"; l.add (k);
        CHECK (k.action() == hub::Action::Unknown && lab ({ false, false, false, false, true }) == "Install 1", "unknown version -> Install 1, not Update");
        auto sel = hub::defaultSelection (l);
        CHECK (sel.size() == 5 && sel[0] && sel[1] && ! sel[2] && ! sel[3] && ! sel[4],
               "defaultSelection ticks Install + Update only, not up-to-date / no-checksum / unknown");
        juce::Array<hub::Plugin> current { r };
        juce::Array<bool> curOn { true };
        CHECK (hub::primaryLabel (current, curOn) == "Everything up to date", "catalog all current -> Everything up to date");
        CHECK (hub::defaultSelection (current) == juce::Array<bool> { false }, "up-to-date is not auto-ticked");
        auto forced = r; forced.force = true;
        juce::Array<hub::Plugin> fl { forced }; juce::Array<bool> fon { true };
        CHECK (hub::primaryLabel (fl, fon) == "Install 1", "force reinstall of one plugin counts as Install 1");
        CHECK (! hub::needsAction (r) && hub::shouldSkipAsUpToDate (r) && ! hub::shouldSkipAsUpToDate (forced),
               "needsAction / shouldSkipAsUpToDate respect force");
    }
    {
        std::cout << "\n=== skip up-to-date (no download) ===" << std::endl;
        auto root = work.getChildFile ("skip-uptodate"); root.deleteRecursively();
        auto L = hub::Layout::prefix (root, hub::Platform::Windows);
        auto bundle = L.vst3Dirs[0].getChildFile ("CMPLXTY Skip.vst3/Contents/Resources/moduleinfo.json");
        bundle.getParentDirectory().createDirectory();
        bundle.replaceWithText ("{ \"Version\": \"1.0.0\" }");
        // a marker file so we can prove nothing was rewritten
        auto marker = L.vst3Dirs[0].getChildFile ("CMPLXTY Skip.vst3/KEEP");
        marker.replaceWithText ("leave me");
        auto before = hub::sha256Of (marker);
        hub::Plugin p; p.slug = "skip-me"; p.name = "Skip Me"; p.bundle = "CMPLXTY Skip";
        p.latest = "1.0.0"; p.installed = "1.0.0"; p.sha256 = String::repeatedString ("ab", 32);
        p.assetName = "CMPLXTY-Skip-Setup-1.0.0-win.exe"; p.tag = "skip-me-v1.0.0";
        p.url = "https://127.0.0.1:1/CMPLXTY-Skip-Setup-1.0.0-win.exe"; p.size = 99;
        CHECK (p.action() == hub::Action::Reinstall && hub::shouldSkipAsUpToDate (p), "fixture is up to date");
        juce::Array<hub::Plugin> sel { p };
        auto r = hub::installSelected (sel, L, [] (const String&) {});
        CHECK (r.size() == 1 && r[0].ok && r[0].message == "already up to date, skipped", "offline skip message: " + r[0].message);
        CHECK (marker.existsAsFile() && hub::sha256Of (marker) == before, "skip left installed files byte-identical");
        CHECK (! L.work.getChildFile ("downloads").getChildFile (p.tag).getChildFile (p.assetName).existsAsFile(),
               "skip did not attempt a download (dead URL would have created a dest or failed the result)");
        p.force = true;
        juce::Array<hub::Plugin> selF { p };
        auto rf = hub::installSelected (selF, L, [] (const String&) {});
        CHECK (rf.size() == 1 && ! rf[0].ok && rf[0].message != "already up to date, skipped",
               "force reinstall is not skipped and hits the dead URL: " + rf[0].message);
        CHECK (marker.existsAsFile() && hub::sha256Of (marker) == before, "failed force left files byte-identical");
        root.deleteRecursively();
    }
    {   // regression (Sam, Nov 5): Quartz's Windows bundle has no moduleinfo.json -> was "unknown"/REINSTALL (VERSION ?)
        auto root = work.getChildFile ("quartz-pe"); root.deleteRecursively();
        auto L = hub::Layout::prefix (root, hub::Platform::Windows);
        auto dll = L.vst3Dirs[1].getChildFile ("Quartz.vst3/Contents/x86_64-win/Quartz.vst3");
        dll.getParentDirectory().createDirectory();
        auto u16 = [] (juce::MemoryOutputStream& o, const char* t) { for (; *t; ++t) { o.writeByte (*t); o.writeByte (0); } o.writeByte (0); o.writeByte (0); };
        juce::MemoryOutputStream o; o.writeRepeatedByte ('M', 4096);
        u16 (o, "FileVersion"); o.writeRepeatedByte (0, 2); u16 (o, "1.0.5");
        u16 (o, "ProductVersion"); u16 (o, "1.0.5"); o.writeRepeatedByte (0, 512);
        dll.replaceWithData (o.getData(), o.getDataSize());
        hub::Plugin q; q.slug = "quartz"; q.bundle = "Quartz"; q.latest = "1.0.5";
        hub::detectInstalled (q, L);
        CHECK (q.installed == "1.0.5" && q.action() == hub::Action::Reinstall, "quartz (win, DLL only, no moduleinfo) reads " + q.installed + " -> REINSTALL, not VERSION ?");
        q.latest = "1.0.6";
        CHECK (q.action() == hub::Action::Update, "quartz 1.0.5 installed vs 1.0.6 -> UPDATE");
        File real ("/tmp/q105/$0/Contents/x86_64-win/Quartz.vst3");
        if (real.existsAsFile()) CHECK (hub::readPeVersion (real) == "1.0.5", "real Quartz 1.0.5 DLL from the released installer reads " + hub::readPeVersion (real));
        CHECK (hub::readPeVersion (work.getChildFile ("nope.dll")).isEmpty(), "missing file -> empty");
        root.deleteRecursively();
    }
    if (which == "win" || which == "both") runPlatform (hub::Platform::Windows, work.getChildFile ("win"));
    if (which == "mac" || which == "both") runPlatform (hub::Platform::Mac, work.getChildFile ("mac"));
    std::cout << "\n" << (fails == 0 ? "ALL PASSED" : String (fails) + " FAILED") << std::endl;
    return fails == 0 ? 0 : 1;
}
