#include <string_view>
#include "HubCore.h"
#include <regex>
#include <thread>

namespace hub
{
using juce::String; using juce::File; using juce::var;

Platform hostPlatform()
{
   #if JUCE_MAC
    return Platform::Mac;
   #else
    return Platform::Windows;   // Linux only runs staged tests, which pick a platform explicitly
   #endif
}
String platformName (Platform p) { return p == Platform::Mac ? "mac" : "win"; }

static bool parseVer (String s, int (&o)[3])
{
    s = s.trim(); if (s.startsWithIgnoreCase ("v")) s = s.substring (1);
    auto parts = juce::StringArray::fromTokens (s, ".", "");
    o[0] = o[1] = o[2] = 0;
    if (parts.isEmpty() || parts.size() > 3) return false;
    for (int i = 0; i < parts.size(); ++i)
    {
        if (parts[i].isEmpty() || ! parts[i].containsOnly ("0123456789") || parts[i].length() > 5) return false;
        o[i] = parts[i].getIntValue();
    }
    return true;
}
int compareVersions (const String& a, const String& b)
{
    int x[3], y[3]; parseVer (a, x); parseVer (b, y);
    for (int i = 0; i < 3; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
bool isAllowedDownload (const String& u)
{
    if (! u.startsWith (kDownloadPrefix) || u.containsAnyOf (" \t\r\n\"'<>\\") || u.contains ("..")) return false;
    auto tag = u.fromFirstOccurrenceOf (kDownloadPrefix, false, false).upToFirstOccurrenceOf ("/", false, false);
    return tag.isNotEmpty() && ! tag.startsWith ("mac-builds");
}

// ------------------------------------------------------------------------------------------------ networking
#if JUCE_LINUX
static bool runCurl (juce::StringArray args, String& out, String& error, int timeoutMs, const std::atomic<bool>* cancel = nullptr)
{
    juce::StringArray cmd { "curl", "-fsSL", "--retry", "2", "--max-time", String (juce::jmax (5, timeoutMs / 1000)) };
    cmd.addArray (args);
    juce::ChildProcess p;
    if (! p.start (cmd)) { error = "curl not found"; return false; }
    if (cancel != nullptr)   // downloads write to a file (-o), so there is no stdout to drain while polling
    {
        auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs + 5000;
        while (p.isRunning())
        {
            if (cancel->load() || juce::Time::getMillisecondCounter() > end) { p.kill(); error = cancel->load() ? "cancelled" : "timed out"; return false; }
            juce::Thread::sleep (50);
        }
    }
    else { out = p.readAllProcessOutput(); p.waitForProcessToFinish (timeoutMs + 5000); }
    if (p.getExitCode() != 0) { error = "HTTP request failed (curl exit " + String (p.getExitCode()) + ")"; return false; }
    return true;
}
#endif

bool httpGet (const String& url, String& body, String& error, int timeoutMs)
{
   #if JUCE_LINUX
    return runCurl ({ "-H", "User-Agent: CMPLXTY-Plugin-Hub/" HUB_VERSION_STRING, url }, body, error, timeoutMs);
   #else
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                  .withConnectionTimeoutMs (timeoutMs).withStatusCode (&status).withNumRedirectsToFollow (8)
                  .withExtraHeaders ("User-Agent: CMPLXTY-Plugin-Hub/" HUB_VERSION_STRING);
    auto in = juce::URL (url).createInputStream (opts);
    if (in == nullptr) { error = "no connection"; return false; }
    body = in->readEntireStreamAsString();
    if (status != 0 && (status < 200 || status >= 300)) { error = "HTTP " + String (status); return false; }
    return true;
   #endif
}

bool download (const String& url, const File& dest, String& error, Progress progress, const std::atomic<bool>* cancel)
{
    error = {};
    if (! isAllowedDownload (url)) { error = "refused: not a cmplxty-plugins release asset: " + url; return false; }
    dest.getParentDirectory().createDirectory();
    auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");
    tmp.deleteFile();
   #if JUCE_LINUX
    String out;
    static const std::atomic<bool> never { false };
    if (! runCurl ({ "-o", tmp.getFullPathName(), url }, out, error, 600000, cancel != nullptr ? cancel : &never)) { tmp.deleteFile(); return false; }
    if (progress) progress (tmp.getSize(), tmp.getSize());
   #else
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                  .withConnectionTimeoutMs (20000).withStatusCode (&status).withNumRedirectsToFollow (8);
    auto in = juce::URL (url).createInputStream (opts);
    if (in == nullptr || (status != 0 && status >= 300)) { error = "download failed (HTTP " + String (status) + ")"; return false; }
    {
        juce::FileOutputStream os (tmp);
        if (! os.openedOk()) { error = "cannot write " + tmp.getFullPathName(); return false; }
        auto total = in->getTotalLength(); juce::int64 done = 0;
        juce::HeapBlock<char> buf (1 << 16);
        for (;;)
        {
            if (cancel != nullptr && cancel->load()) { error = "cancelled"; break; }
            auto n = in->read (buf, 1 << 16);
            if (n <= 0) break;
            os.write (buf, (size_t) n); done += n;
            if (progress) progress (done, total);
        }
        os.flush();
        if (os.getStatus().failed()) { error = "disk write failed: " + os.getStatus().getErrorMessage(); }
        else if (total > 0 && done != total) error = "download incomplete";
    }
    if (error.isNotEmpty()) { tmp.deleteFile(); return false; }
   #endif
    dest.deleteFile();
    return tmp.moveFileTo (dest);
}

String sha256Of (const File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    return juce::SHA256 (in).toHexString().toLowerCase();
}

// ------------------------------------------------------------------------------------------------ layout
static File env (const char* name) { auto v = juce::SystemStats::getEnvironmentVariable (name, {}); return v.isEmpty() ? File() : File (v); }

Layout Layout::system()
{
    Layout l; l.platform = hostPlatform();
   #if JUCE_WINDOWS
    auto common = env ("CommonProgramW6432"); if (common == File()) common = File ("C:\\Program Files\\Common Files");
    auto local = File::getSpecialLocation (File::windowsLocalAppData);
    auto roaming = File::getSpecialLocation (File::userApplicationDataDirectory);
    l.vst3Dirs = { common.getChildFile ("VST3"), local.getChildFile ("Programs\\Common\\VST3") };
    l.clapDirs = { common.getChildFile ("CLAP"), local.getChildFile ("Programs\\Common\\CLAP") };
    l.presetRoots = { roaming.getChildFile ("CMPLXTY"), roaming.getChildFile ("Quartz") };
    l.work = local.getChildFile ("CMPLXTY Plugin Hub");
   #else
    auto lib = File ("~/Library");
    l.vst3Dirs = { File ("/Library/Audio/Plug-Ins/VST3"), lib.getChildFile ("Audio/Plug-Ins/VST3") };
    l.clapDirs = { File ("/Library/Audio/Plug-Ins/CLAP"), lib.getChildFile ("Audio/Plug-Ins/CLAP") };
    l.presetRoots = { lib.getChildFile ("CMPLXTY"), lib.getChildFile ("Quartz"),
                      lib.getChildFile ("Application Support/CMPLXTY"), lib.getChildFile ("Application Support/Quartz") };
    l.work = lib.getChildFile ("Caches/CMPLXTY Plugin Hub");
   #endif
    return l;
}

Layout Layout::prefix (const File& root, Platform p)
{
    Layout l; l.platform = p; l.staged = true;
    if (p == Platform::Windows)
    {
        l.vst3Dirs = { root.getChildFile ("Program Files/Common Files/VST3"), root.getChildFile ("AppData/Local/Programs/Common/VST3") };
        l.clapDirs = { root.getChildFile ("Program Files/Common Files/CLAP"), root.getChildFile ("AppData/Local/Programs/Common/CLAP") };
        l.presetRoots = { root.getChildFile ("AppData/Roaming/CMPLXTY"), root.getChildFile ("AppData/Roaming/Quartz") };
        l.work = root.getChildFile ("AppData/Local/CMPLXTY Plugin Hub");
    }
    else
    {
        l.vst3Dirs = { root.getChildFile ("Library/Audio/Plug-Ins/VST3"), root.getChildFile ("Users/me/Library/Audio/Plug-Ins/VST3") };
        l.clapDirs = { root.getChildFile ("Library/Audio/Plug-Ins/CLAP"), root.getChildFile ("Users/me/Library/Audio/Plug-Ins/CLAP") };
        l.presetRoots = { root.getChildFile ("Users/me/Library/CMPLXTY"), root.getChildFile ("Users/me/Library/Quartz"),
                          root.getChildFile ("Users/me/Library/Application Support/CMPLXTY"),
                          root.getChildFile ("Users/me/Library/Application Support/Quartz") };
        l.work = root.getChildFile ("Users/me/Library/Caches/CMPLXTY Plugin Hub");
    }
    return l;
}

// ------------------------------------------------------------------------------------------------ catalog
// Display labels only — a slug does not have to be listed to appear in the catalog (unknown slugs are "FX").
// applyVersionJson can override this from a version.json "type" field, so a new plugin needs no Hub code change.
String typeFor (const String& slug)
{
    static const std::map<String, String> t {
        { "quartz", "Vocal FX" }, { "robot-voice", "Vocal FX" }, { "vocal-aligner", "Vocal Tool" }, { "mix-tools", "Mix Tool" },
        { "cyber-wavetable", "Synth" }, { "vvitch", "Synth" }, { "silt", "Synth" }, { "drum-player", "Drum Tool" },
        { "dr-fill", "Drum Tool" }, { "visualizer", "Visualizer" }, { "rewired", "MIDI Tool" } };
    auto it = t.find (slug);
    return it != t.end() ? it->second : String ("FX");
}

Action Plugin::action() const
{
    if (installed.isEmpty()) return Action::Install;
    if (installed == "unknown") return Action::Unknown;
    auto c = compareVersions (installed, latest);
    return c < 0 ? Action::Update : c == 0 ? Action::Reinstall : Action::Downgrade;
}
String actionName (Action a)
{
    switch (a) { case Action::Install: return "INSTALL"; case Action::Update: return "UPDATE";
                 case Action::Reinstall: return "REINSTALL"; case Action::Downgrade: return "REPLACE (NEWER INSTALLED)";
                 case Action::Unknown: return "REINSTALL (VERSION ?)"; }
    return {};
}

bool needsAction (const Plugin& p)
{
    if (! p.checksumOk()) return false;
    auto a = p.action();
    return a == Action::Install || a == Action::Update;
}

bool shouldSkipAsUpToDate (const Plugin& p)
{
    return p.action() == Action::Reinstall && ! p.force;
}

juce::Array<bool> defaultSelection (const juce::Array<Plugin>& list)
{
    juce::Array<bool> out;
    for (auto& p : list) out.add (needsAction (p));
    return out;
}

String primaryLabel (const juce::Array<Plugin>& list, const juce::Array<bool>& checked)
{
    int inst = 0, upd = 0, needed = 0;
    for (int i = 0; i < list.size(); ++i)
    {
        auto& p = list.getReference (i);
        if (! p.checksumOk()) continue;
        if (needsAction (p)) ++needed;
        if (i >= checked.size() || ! checked[i] || shouldSkipAsUpToDate (p)) continue;
        (p.action() == Action::Update ? upd : inst)++;
    }
    if (inst + upd == 0) return needed == 0 ? String ("Everything up to date") : String();
    juce::StringArray parts;
    if (inst > 0) parts.add ("Install " + String (inst));
    if (upd > 0) parts.add ("Update " + String (upd));
    return parts.joinIntoString (" · ");
}

static bool isPlatformInstaller (const String& n, Platform p)
{
    if (n.endsWithIgnoreCase (".sha256")) return false;
    if (! n.containsIgnoreCase ("Setup")) return false;
    return p == Platform::Mac ? n.endsWithIgnoreCase (".pkg") : n.endsWithIgnoreCase (".exe");
}

juce::Array<Plugin> parseReleases (const String& json, Platform p, juce::StringArray* skipped)
{
    static const std::regex tagRe ("^([a-z0-9]+(?:-[a-z0-9]+)*)-v([0-9]+\\.[0-9]+\\.[0-9]+)$");
    static const juce::StringArray notPlugins { "yoink", "yoink-mac", "plugin-hub", "press-assets" };
    std::map<String, Plugin> best;
    auto arr = juce::JSON::parse (json);
    if (! arr.isArray()) return {};
    for (auto& r : *arr.getArray())
    {
        auto tag = r["tag_name"].toString();
        auto note = [&] (const String& why) { if (skipped) skipped->add (tag + ": " + why); };
        if ((bool) r["draft"] || (bool) r["prerelease"]) { note ("draft/prerelease"); continue; }
        if (tag.startsWith ("mac-builds")) { note ("mac-builds release (never used)"); continue; }
        std::smatch m; auto s = tag.toStdString();
        if (! std::regex_match (s, m, tagRe)) { note ("not a <plugin>-v<x.y.z> tag"); continue; }
        String slug (m[1].str()), ver (m[2].str());
        if (notPlugins.contains (slug)) { note ("not a VST3/CLAP plugin"); continue; }
        Plugin pl; pl.slug = slug; pl.tag = tag; pl.latest = ver; pl.type = typeFor (slug);
        if (auto* assets = r["assets"].getArray())
            for (auto& a : *assets)
                if (isPlatformInstaller (a["name"].toString(), p))
                {
                    pl.assetName = a["name"].toString();
                    pl.url = a["browser_download_url"].toString();
                    pl.size = (juce::int64) a["size"];
                }
        if (pl.assetName.isEmpty()) { note ("no " + platformName (p) + " VST3/CLAP installer"); continue; }
        // "CMPLXTY-Dr-Fill-Setup-1.0.0-win.exe" -> bundle "CMPLXTY Dr Fill"; "Quartz-Setup-1.0.5.exe" -> "Quartz"
        auto stem = pl.assetName.upToFirstOccurrenceOf ("-Setup", false, true).replace ("-", " ");
        pl.bundle = slug == "quartz" ? String ("Quartz") : stem;
        pl.name = stem.startsWith ("CMPLXTY ") ? stem.substring (8) : stem;
        auto it = best.find (slug);
        if (it == best.end() || compareVersions (ver, it->second.latest) > 0) best[slug] = pl;
    }
    juce::Array<Plugin> out;
    for (auto& [k, v] : best) out.add (v);
    return out;
}

void applyVersionJson (Plugin& pl, const String& text, Platform p)
{
    auto v = juce::JSON::parse (text);
    if (! v.isObject()) return;
    auto block = v[platformName (p).toRawUTF8()];
    if (! block.isObject()) { if (p == Platform::Mac) return; block = v; }   // macOS reads only the "mac" block
    auto url = block["url"].toString(), sha = block["sha256"].toString().toLowerCase();
    if (url != pl.url || compareVersions (block["version"].toString(), pl.latest) != 0) return;   // stale / other release
    if (sha.length() == 64 && sha.containsOnly ("0123456789abcdef")) pl.sha256 = sha;
    pl.notes = block["notes"].toString().substring (0, 200);
    auto typ = block["type"].toString().trim();
    if (typ.isEmpty()) typ = v["type"].toString().trim();
    if (typ.isNotEmpty()) pl.type = typ.substring (0, 40);
}

String parseSumsFile (const String& text, const String& fileName)
{
    for (auto line : juce::StringArray::fromLines (text))
    {
        auto t = line.trim(); if (t.isEmpty()) continue;
        auto hash = t.upToFirstOccurrenceOf (" ", false, false).toLowerCase();
        auto name = t.fromFirstOccurrenceOf (" ", false, false).trim().trimCharactersAtStart ("*").trim();
        if ((name == fileName || name.isEmpty()) && hash.length() == 64 && hash.containsOnly ("0123456789abcdef")) return hash;
    }
    return {};
}

static void resolveChecksum (Plugin& pl)
{
    if (pl.checksumOk()) return;
    auto base = String (kDownloadPrefix) + pl.tag + "/";
    for (auto cand : { base + "SHA256SUMS", base + pl.assetName + ".sha256" })
    {
        String body, err;
        if (httpGet (cand, body, err, 6000) && (pl.sha256 = parseSumsFile (body, pl.assetName)).isNotEmpty()) break;
    }
}
template <typename Fn> static void forEachParallel (juce::Array<Plugin>& list, Fn fn)
{
    std::vector<std::thread> ts;
    for (auto& pl : list) ts.emplace_back ([&pl, &fn] { fn (pl); });
    for (auto& t : ts) t.join();
}
bool resolveChecksums (juce::Array<Plugin>& list, String& log)
{
    forEachParallel (list, [] (Plugin& pl) { resolveChecksum (pl); });
    bool all = true;
    for (auto& pl : list) if (! pl.checksumOk()) { all = false; log << pl.slug << ": no published sha256 (install disabled)\n"; }
    return all;
}

static String readModuleInfoVersion (const File& bundle)
{
    auto mi = bundle.getChildFile ("Contents/Resources/moduleinfo.json");
    if (! mi.existsAsFile()) return {};
    // moduleinfo.json uses JSON5-style trailing commas; read the first "Version" by hand
    auto t = mi.loadFileAsString();
    auto after = t.fromFirstOccurrenceOf ("\"Version\"", false, false).fromFirstOccurrenceOf ("\"", false, false);
    return after.upToFirstOccurrenceOf ("\"", false, false).substring (0, 20);
}
static String readPlistVersion (const File& bundle)
{
    auto pl = bundle.getChildFile ("Contents/Info.plist");
    if (! pl.existsAsFile()) return {};
    auto t = pl.loadFileAsString();
    return t.fromFirstOccurrenceOf ("CFBundleShortVersionString", false, false).fromFirstOccurrenceOf ("<string>", false, false)
            .upToFirstOccurrenceOf ("</string>", false, false).trim().substring (0, 20);
}

// Windows: the DLL's VERSIONINFO resource (JUCE writes ProductVersion from the CMake VERSION). Quartz's Windows bundle
// ships only the DLL (no moduleinfo.json), so without this it always read "unknown". Portable so it is testable anywhere.
String readPeVersion (const File& binary)
{
    if (! binary.existsAsFile() || binary.getSize() > 256 * 1024 * 1024) return {};
    juce::MemoryBlock mb; if (! binary.loadFileAsData (mb)) return {};
    auto* d = static_cast<const char*> (mb.getData()); auto n = mb.getSize();
    for (auto* key : { "ProductVersion", "FileVersion" })
    {
        std::string k16; for (auto* c = key; *c; ++c) { k16 += *c; k16 += '\0'; } k16.append (2, '\0');
        std::string_view hay (d, n);
        for (auto pos = hay.find (k16); pos != std::string_view::npos; pos = hay.find (k16, pos + 2))
        {
            auto s = pos + k16.size();
            while (s + 1 < n && d[s] == 0 && d[s + 1] == 0) s += 2;            // DWORD padding
            String v;
            for (; s + 1 < n && (d[s] || d[s + 1]) && v.length() < 24; s += 2) v += (juce::juce_wchar) (uint8_t) d[s];
            v = v.trim();
            if (v.isNotEmpty() && v.containsOnly ("0123456789.")) return v;
        }
    }
    return {};
}

void detectInstalled (Plugin& pl, const Layout& l)
{
    pl.installed = {}; pl.installedVst3 = pl.installedClap = File();
    juce::StringArray names { pl.bundle };
    if (! pl.bundle.startsWith ("CMPLXTY ")) names.add ("CMPLXTY " + pl.bundle); else names.add (pl.bundle.substring (8));
    for (auto& d : l.vst3Dirs) for (auto& n : names)
    {
        auto b = d.getChildFile (n + ".vst3");
        if (! b.exists() || pl.installedVst3 != File()) continue;
        pl.installedVst3 = b;
        auto v = l.platform == Platform::Mac ? readPlistVersion (b) : String();
        if (v.isEmpty()) v = readModuleInfoVersion (b);
        if (v.isEmpty() && l.platform == Platform::Windows) v = readPeVersion (b.isDirectory() ? b.getChildFile ("Contents/x86_64-win/" + n + ".vst3") : b);
        pl.installed = v.isNotEmpty() ? v : String ("unknown");
    }
    for (auto& d : l.clapDirs) for (auto& n : names)
    {
        auto c = d.getChildFile (n + ".clap");
        if (! c.exists() || pl.installedClap != File()) continue;
        pl.installedClap = c;
        if (pl.installed.isEmpty())
        {
            auto v = l.platform == Platform::Mac ? readPlistVersion (c) : readPeVersion (c);
            pl.installed = v.isNotEmpty() ? v : String ("unknown");
        }
    }
}

// GitHub returns at most 100 releases per page; walk pages so plug-ins never drop off the list as releases pile up.
static bool fetchAllReleases (String& body, String& error)
{
    juce::Array<var> all;
    for (int page = 1; page <= 10; ++page)
    {
        String chunk;
        if (! httpGet (String (kReleasesApi) + "&page=" + String (page), chunk, error, kApiTimeoutMs)) return false;
        auto v = juce::JSON::parse (chunk);
        auto* a = v.getArray();
        if (a == nullptr) { error = "unexpected reply from GitHub (rate limited?)"; return false; }
        all.addArray (*a);
        if (a->size() < 100) break;
    }
    body = juce::JSON::toString (var (all), true);
    return true;
}

bool fetchCatalog (juce::Array<Plugin>& out, const Layout& l, String& error, bool withVersionJson)
{
    String body;
    auto cache = l.work.getChildFile ("releases-cache.json");
    if (fetchAllReleases (body, error)) { l.work.createDirectory(); cache.replaceWithText (body); }
    else if (cache.existsAsFile()) { body = cache.loadFileAsString(); error = "offline - using the last saved plugin list"; }
    else return false;
    out = parseReleases (body, l.platform);
    // per plug-in side lookups (version.json, then SHA256SUMS if still needed) all run at once
    forEachParallel (out, [&] (Plugin& pl)
    {
        String vj, e;
        if (withVersionJson && httpGet ("https://cmplxty.music/plugins/" + pl.slug + "/version.json", vj, e, kSideTimeoutMs))
            applyVersionJson (pl, vj, l.platform);
        resolveChecksum (pl);
    });
    for (auto& pl : out) detectInstalled (pl, l);
    return true;
}

// ------------------------------------------------------------------------------------------------ presets
Snapshot snapshot (const juce::Array<File>& roots)
{
    Snapshot s;
    for (auto& r : roots)
        if (r.isDirectory())
            for (auto& e : juce::RangedDirectoryIterator (r, true, "*", File::findFiles | File::ignoreHiddenFiles))
                s.files[r.getFullPathName() + "|" + e.getFile().getRelativePathFrom (r)] = sha256Of (e.getFile());
    return s;
}
juce::StringArray diffMissingOrChanged (const Snapshot& a, const Snapshot& b)
{
    juce::StringArray out;
    for (auto& [k, h] : a.files) { auto it = b.files.find (k); if (it == b.files.end() || it->second != h) out.add (k); }
    return out;
}
File backupPresets (const Layout& l, String& error)
{
    auto dir = l.work.getChildFile ("preset-backups").getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));
    int i = 0; while (dir.exists()) dir = dir.getSiblingFile (dir.getFileName() + "-" + String (++i));
    for (int k = 0; k < l.presetRoots.size(); ++k)
        if (l.presetRoots[k].isDirectory() && ! l.presetRoots[k].copyDirectoryTo (dir.getChildFile (String (k))))
        { error = "could not back up " + l.presetRoots[k].getFullPathName(); return {}; }
    dir.createDirectory();
    return dir;
}
bool restoreMissing (const File& backup, const Layout& l, const juce::StringArray& keys)
{
    bool ok = true;
    for (auto& k : keys)
    {
        auto root = k.upToFirstOccurrenceOf ("|", false, false), rel = k.fromFirstOccurrenceOf ("|", false, false);
        int idx = -1; for (int i = 0; i < l.presetRoots.size(); ++i) if (l.presetRoots[i].getFullPathName() == root) idx = i;
        auto src = backup.getChildFile (String (idx)).getChildFile (rel), dst = File (root).getChildFile (rel);
        dst.getParentDirectory().createDirectory();
        ok = idx >= 0 && src.copyFileTo (dst) && ok;
    }
    return ok;
}

// ------------------------------------------------------------------------------------------------ install
static String find7z()
{
    auto e = juce::SystemStats::getEnvironmentVariable ("HUB_7Z", {});
    if (e.isNotEmpty()) return e;
    for (auto c : { "/workspace/7z/7zz", "/usr/bin/7zz", "/usr/bin/7z" }) if (File (c).existsAsFile()) return c;
    return "7z";
}
static bool run (const juce::StringArray& cmd, String& out, int timeoutMs = 600000)
{
    juce::ChildProcess p;
    if (! p.start (cmd)) { out = "cannot start " + cmd[0]; return false; }
    out = p.readAllProcessOutput();
    if (! p.waitForProcessToFinish (timeoutMs)) { p.kill(); out << "\ntimed out"; return false; }
    return p.getExitCode() == 0;
}
static bool unpack (const File& archive, const File& dest, String& err)
{
    dest.deleteRecursively(); dest.createDirectory();
    return run ({ find7z(), "x", "-y", "-bd", "-o" + dest.getFullPathName(), archive.getFullPathName() }, err);
}
static void replaceWith (const File& src, const File& dst)
{
    dst.deleteRecursively(); dst.getParentDirectory().createDirectory();
    src.isDirectory() ? (void) src.copyDirectoryTo (dst) : (void) src.copyFileTo (dst);
}

// Staged (test) install: unpack the very installer a user would run and lay its plug-in files into the prefix exactly where
// that installer puts them; only the plug-in's own bundle names are replaced.
static bool stagedInstall (const Plugin& pl, const File& installer, const Layout& l, String& msg)
{
    auto tmp = l.work.getChildFile ("unpack").getChildFile (pl.slug);
    if (! unpack (installer, tmp, msg)) return false;
    int placed = 0;
    if (l.platform == Platform::Windows)
    {
        for (auto& e : juce::RangedDirectoryIterator (tmp, true, "*", File::findFiles))
        {
            auto f = e.getFile();
            if (f.getFileExtension() == ".vst3" && f.getParentDirectory().getFileName() == "x86_64-win")
            {
                auto bundleName = f.getFileName();   // "<B>.vst3"
                auto dst = l.vst3Dirs[0].getChildFile (bundleName);
                dst.deleteRecursively();
                replaceWith (f.getParentDirectory().getParentDirectory(), dst.getChildFile ("Contents"));
                ++placed;
            }
            else if (f.getFileExtension() == ".clap") { replaceWith (f, l.clapDirs[0].getChildFile (f.getFileName())); ++placed; }
        }
    }
    else
    {
        // xar -> Payload (gzip) -> cpio ; component pkgs may nest Payload one level down
        for (int depth = 0; depth < 4; ++depth)
        {
            File payload;
            for (auto& e : juce::RangedDirectoryIterator (tmp, true, "Payload*", File::findFiles)) { payload = e.getFile(); break; }
            if (payload == File()) break;
            auto next = tmp.getSiblingFile (tmp.getFileName() + "-" + String (depth));
            if (! unpack (payload, next, msg)) return false;
            payload.deleteFile();
            for (auto& e : juce::RangedDirectoryIterator (next, false, "*", File::findFilesAndDirectories))
                e.getFile().moveFileTo (tmp.getChildFile (e.getFile().getFileName()));
            next.deleteRecursively();
        }
        for (auto [sub, dir] : { std::pair<String, File> { "VST3", l.vst3Dirs[0] }, { "CLAP", l.clapDirs[0] } })
        {
            auto src = tmp.getChildFile ("Library/Audio/Plug-Ins/" + sub);
            if (src.isDirectory())
                for (auto& e : juce::RangedDirectoryIterator (src, false, "*", File::findFilesAndDirectories))
                { replaceWith (e.getFile(), dir.getChildFile (e.getFile().getFileName())); ++placed; }
        }
    }
    tmp.deleteRecursively();
    if (placed == 0) { msg = "installer contained no VST3/CLAP files"; return false; }
    msg = "placed " + String (placed) + " plug-in file(s)";
    return true;
}

#if JUCE_WINDOWS
static bool windowsInstall (const File& installer, String& msg)
{
    // the hub runs elevated (requireAdministrator manifest): /ALLUSERS puts VST3/CLAP in Common Files; /S = silent.
    // Every CMPLXTY installer leaves %APPDATA%\CMPLXTY\<plugin> (settings + presets) alone.
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { installer.getFullPathName(), "/S", "/ALLUSERS" })) { msg = "could not start installer"; return false; }
    if (! p.waitForProcessToFinish (10 * 60 * 1000)) { p.kill(); msg = "installer timed out"; return false; }
    msg = "installer exit code " + String (p.getExitCode());
    return p.getExitCode() == 0;
}
#endif

#if JUCE_MAC
// one administrator prompt for the batch, but each pkg runs on its own and its exit code is recorded, so one bad
// package never marks the others failed. Returns per-pkg exit codes (-1 = never ran).
static juce::Array<int> macInstallBatch (const juce::Array<File>& pkgs, const Layout& l, String& msg)
{
    // one administrator prompt for the whole batch; `installer` does not apply Gatekeeper to the pkg (it was not
    // downloaded by a browser, so it carries no quarantine flag); the pkgs only write /Library/Audio/Plug-Ins + /Applications.
    auto script = l.work.getChildFile ("install-batch.sh");
    auto status = l.work.getChildFile ("install-batch.status");
    status.deleteFile();
    auto q = [] (const File& f) { return "'" + f.getFullPathName().replace ("'", "'\\''") + "'"; };
    String s = "#!/bin/sh\n";
    for (int i = 0; i < pkgs.size(); ++i)
        s << "/usr/sbin/installer -pkg " << q (pkgs[i]) << " -target / ; echo \"" << i << " $?\" >> " << q (status) << "\n";
    script.replaceWithText (s);
    script.setExecutePermission (true);
    auto shellCmd = ("/bin/sh '" + script.getFullPathName() + "'").replace ("\\", "\\\\").replace ("\"", "\\\"");
    String out;
    run ({ "/usr/bin/osascript", "-e", "do shell script \"" + shellCmd + "\" with administrator privileges" }, out, 30 * 60 * 1000);
    msg = out.trim().substring (0, 300);
    juce::Array<int> codes; for (int i = 0; i < pkgs.size(); ++i) codes.add (-1);
    for (auto& line : juce::StringArray::fromLines (status.loadFileAsString()))
    {
        auto idx = line.upToFirstOccurrenceOf (" ", false, false).getIntValue();
        if (line.isNotEmpty() && juce::isPositiveAndBelow (idx, codes.size())) codes.set (idx, line.fromFirstOccurrenceOf (" ", false, false).getIntValue());
    }
    return codes;
}
#endif

juce::Array<Result> installSelected (juce::Array<Plugin>& sel, const Layout& l, Log log, Progress progress, const std::atomic<bool>* cancel)
{
    juce::Array<Result> results;
    auto say = [&] (const String& s) { if (log) log (s); };
    auto cancelled = [&] { return cancel != nullptr && cancel->load(); };
    l.work.createDirectory();

    // Up-to-date rows are skipped here (even if ticked) — no download and no file changes — unless force is set.
    juce::Array<int> todo;
    for (int i = 0; i < sel.size(); ++i)
    {
        auto& pl = sel.getReference (i);
        Result r; r.slug = pl.slug;
        if (shouldSkipAsUpToDate (pl))
        {
            r.ok = true; r.message = "already up to date, skipped";
            say ((pl.name.isNotEmpty() ? pl.name : pl.slug) + ": already up to date, skipped");
        }
        else todo.add (i);
        results.add (r);
    }
    if (todo.isEmpty()) return results;

    // batch-wide progress: downloads are 85% of the bar (weighted by size), installs the last 15%
    juce::int64 totalBytes = 0; for (auto i : todo) totalBytes += juce::jmax ((juce::int64) 1, sel.getReference (i).size);
    juce::int64 bytesDone = 0; int installsDone = 0, installsTotal = 0;
    auto report = [&] (juce::int64 cur)
    {
        if (! progress) return;
        double f = 0.85 * (double) (bytesDone + cur) / (double) juce::jmax ((juce::int64) 1, totalBytes)
                 + 0.15 * (installsTotal > 0 ? (double) installsDone / installsTotal : 0.0);
        progress ((juce::int64) (juce::jlimit (0.0, 1.0, f) * 1000.0), 1000);
    };

    // 1. download + verify everything first, so nothing is replaced unless every file is good
    juce::Array<File> files; juce::Array<int> ready;
    for (auto i : todo)
    {
        auto& pl = sel.getReference (i);
        auto& r = results.getReference (i);
        auto dest = l.work.getChildFile ("downloads").getChildFile (pl.tag).getChildFile (pl.assetName);
        String err;
        if (cancelled()) r.message = "cancelled";
        else if (! pl.checksumOk()) r.message = "no published checksum - skipped";
        else if (dest.existsAsFile() && sha256Of (dest) == pl.sha256) { r.ok = true; say (pl.name + ": cached download verified"); }
        else
        {
            say ("Downloading " + pl.name + " " + pl.latest + "...");
            if (! download (pl.url, dest, err, [&] (juce::int64 d, juce::int64) { report (juce::jmin (d, juce::jmax ((juce::int64) 1, pl.size))); }, cancel)) r.message = err;
            else if (sha256Of (dest) != pl.sha256) { r.message = "checksum mismatch - deleted"; dest.deleteFile(); }
            else r.ok = true;
        }
        bytesDone += juce::jmax ((juce::int64) 1, pl.size); report (0);
        if (r.ok) { files.add (dest); ready.add (i); } else say (pl.name + ": " + r.message);
    }
    if (cancelled())
    {
        for (auto i : todo) { auto& r = results.getReference (i); if (r.ok) { r.ok = false; r.message = "cancelled"; } }
        say ("Cancelled - nothing was changed."); return results;
    }
    if (ready.isEmpty()) return results;
    installsTotal = ready.size();

    // 2. presets: snapshot + backup
    auto before = snapshot (l.presetRoots);
    String err; auto backup = backupPresets (l, err);
    if (backup == File()) { for (auto i : ready) { auto& r = results.getReference (i); if (r.ok) { r.ok = false; r.message = err; } } say (err); return results; }
    say ("Presets backed up (" + String ((int) before.files.size()) + " files) to " + backup.getFullPathName());

    // 3. install (cancel stops before the next plug-in; one that is already running finishes)
   #if JUCE_MAC
    if (! l.staged)
    {
        say ("Installing " + String (files.size()) + " package(s) - macOS will ask for your password once...");
        String msg; auto codes = macInstallBatch (files, l, msg);
        for (int k = 0; k < ready.size(); ++k)
        {
            auto& r = results.getReference (ready[k]); auto c = codes[k];
            r.ok = c == 0; r.message = c == 0 ? "installed" : c < 0 ? "not run (" + (msg.isNotEmpty() ? msg : String ("password prompt cancelled?")) + ")" : "macOS installer exit " + String (c);
        }
        installsDone = installsTotal; report (0);
    }
    else
   #endif
    for (int k = 0; k < ready.size(); ++k)
    {
        auto& pl = sel.getReference (ready[k]); auto& r = results.getReference (ready[k]);
        if (cancelled()) { r.ok = false; r.message = "cancelled"; continue; }
        say ("Installing " + pl.name + " " + pl.latest + "...");
        String msg;
       #if JUCE_WINDOWS
        r.ok = l.staged ? stagedInstall (pl, files[k], l, msg) : windowsInstall (files[k], msg);
       #else
        r.ok = stagedInstall (pl, files[k], l, msg);
       #endif
        r.message = msg;
        ++installsDone; report (0);
    }

    // 4. presets intact?
    auto missing = diffMissingOrChanged (before, snapshot (l.presetRoots));
    if (missing.isEmpty()) say ("Presets verified: all " + String ((int) before.files.size()) + " files intact");
    else
    {
        bool restored = restoreMissing (backup, l, missing);
        say ("WARNING: " + String (missing.size()) + " preset file(s) changed during install - " + (restored ? "restored from backup" : "restore FAILED, backup kept at " + backup.getFullPathName()));
    }

    // 5. confirm version on disk, per plug-in
    for (auto i : ready)
    {
        auto& pl = sel.getReference (i); auto& r = results.getReference (i);
        if (r.message == "cancelled") continue;
        detectInstalled (pl, l);
        if (r.ok && compareVersions (pl.installed, pl.latest) != 0) { r.ok = false; r.message = "installed version reads " + pl.installed; }
        say (pl.name + ": " + (r.ok ? "OK, now " + pl.installed : "FAILED - " + r.message));
    }
    if (progress) progress (1000, 1000);
    return results;
}

// ------------------------------------------------------------------------------------------------ uninstall
bool isSafeToRemove (const File& f, const Plugin& pl, const Layout& l)
{
    if (f == File() || ! f.exists()) return false;
    auto ext = f.getFileExtension().toLowerCase();
    auto& dirs = ext == ".vst3" ? l.vst3Dirs : l.clapDirs;
    if (ext != ".vst3" && ext != ".clap") return false;
    bool inDir = false; for (auto& d : dirs) inDir = inDir || f.getParentDirectory() == d;
    if (! inDir) return false;
    auto stem = f.getFileNameWithoutExtension();
    if (pl.bundle.isEmpty() || (stem != pl.bundle && stem != "CMPLXTY " + pl.bundle && "CMPLXTY " + stem != pl.bundle)) return false;
    for (auto& r : l.presetRoots) if (f == r || f.isAChildOf (r) || r.isAChildOf (f)) return false;
    return true;
}

juce::Array<Result> uninstallSelected (juce::Array<Plugin>& sel, const Layout& l, Log log)
{
    juce::Array<Result> results;
    auto say = [&] (const String& s) { if (log) log (s); };
    l.work.createDirectory();
    auto before = snapshot (l.presetRoots);
    juce::Array<File> targets; juce::Array<int> owner;
    for (int i = 0; i < sel.size(); ++i)
    {
        auto& pl = sel.getReference (i); detectInstalled (pl, l);
        Result r; r.slug = pl.slug;
        int n = 0;
        for (auto f : { pl.installedVst3, pl.installedClap })
            if (f != File())
            {
                if (isSafeToRemove (f, pl, l)) { targets.add (f); owner.add (i); ++n; }
                else say (pl.name + ": refusing to remove " + f.getFullPathName());
            }
        r.message = n == 0 ? "not installed" : String();
        results.add (r);
    }
   #if JUCE_MAC
    if (! l.staged && ! targets.isEmpty())
    {
        String cmd; for (auto& f : targets) cmd << "/bin/rm -rf '" << f.getFullPathName().replace ("'", "'\\''") << "' ; ";
        String out;
        run ({ "/usr/bin/osascript", "-e", "do shell script \"" + cmd.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\" with administrator privileges" }, out, 5 * 60 * 1000);
    }
    else
   #endif
    for (auto& f : targets) { say ("Removing " + f.getFullPathName()); f.isDirectory() ? (void) f.deleteRecursively() : (void) f.deleteFile(); }

    for (int i = 0; i < sel.size(); ++i)
    {
        auto& pl = sel.getReference (i); auto& r = results.getReference (i);
        if (r.message == "not installed") continue;
        detectInstalled (pl, l);
        r.ok = pl.installed.isEmpty();
        r.message = r.ok ? "removed" : "still installed (permission?)";
        say (pl.name + ": " + r.message);
    }
    auto changed = diffMissingOrChanged (before, snapshot (l.presetRoots));
    say (changed.isEmpty() ? "Presets verified: untouched" : "WARNING: " + String (changed.size()) + " preset file(s) changed");
    return results;
}

// ------------------------------------------------------------------------------------------------ self update
HubUpdate checkHubUpdate (Platform p, const String& releasesJson)
{
    HubUpdate u;
    String body, err;
    if (httpGet (kHubVersionJson, body, err, kSideTimeoutMs))
    {
        auto v = juce::JSON::parse (body);
        auto block = v[platformName (p).toRawUTF8()];
        if (! block.isObject() && p != Platform::Mac) block = v;
        auto ver = block["version"].toString();
        if (ver.isNotEmpty())
        {
            u.version = ver; u.page = "https://cmplxty.music/plugins/plugin-hub/";
            u.available = compareVersions (ver, HUB_VERSION_STRING) > 0;
            if (releasesJson.isEmpty()) return u;   // no extra GitHub call unless the caller already has the list
        }
    }
    auto json = releasesJson;
    if (json.isEmpty() && ! httpGet (kReleasesApi, json, err, kApiTimeoutMs)) return u;
    auto arr = juce::JSON::parse (json);
    if (auto* a = arr.getArray())
        for (auto& r : *a)
        {
            auto tag = r["tag_name"].toString();
            if (! tag.startsWith ("plugin-hub-v") || (bool) r["draft"] || (bool) r["prerelease"]) continue;
            auto ver = tag.fromFirstOccurrenceOf ("plugin-hub-v", false, false);
            if (u.version.isEmpty() || compareVersions (ver, u.version) > 0)
            { u.version = ver; u.page = String ("https://github.com/") + kRepo + "/releases/tag/" + tag; }
        }
    u.available = u.version.isNotEmpty() && compareVersions (u.version, HUB_VERSION_STRING) > 0;
    return u;
}
}
