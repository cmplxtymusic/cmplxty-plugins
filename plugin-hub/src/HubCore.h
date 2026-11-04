// CMPLXTY Plugin Hub - core logic (no GUI). Shared by the app, the CLI and the tests.
//  * Catalog: the GitHub Releases list of cmplxtymusic/cmplxty-plugins. Only per-plugin tags "<slug>-v<x.y.z>" with a
//    VST3/CLAP installer for this platform are used. Never mac-builds-*, never press-assets, never standalone apps (Yoink).
//  * Latest version: newest per-tag release; cmplxty.music/plugins/<slug>/version.json ("mac" block on macOS, "win" on
//    Windows) is read when it is reachable and only accepted when it points at the same GitHub release.
//  * Every download must come from https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/ and match a sha256
//    published next to it (SHA256SUMS / <file>.sha256 / version.json). No checksum -> no install.
//  * User presets/settings (Windows %APPDATA%\CMPLXTY + %APPDATA%\Quartz, macOS ~/Library/CMPLXTY, ~/Library/Quartz,
//    ~/Library/Application Support/CMPLXTY) are snapshotted + backed up before any install and verified afterwards.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <functional>
#include <map>

#ifndef HUB_VERSION_STRING
 #define HUB_VERSION_STRING "0.0.0"
#endif

namespace hub
{
inline const char* const kRepo = "cmplxtymusic/cmplxty-plugins";
inline const char* const kReleasesApi = "https://api.github.com/repos/cmplxtymusic/cmplxty-plugins/releases?per_page=100";
inline const char* const kDownloadPrefix = "https://github.com/cmplxtymusic/cmplxty-plugins/releases/download/";
inline const char* const kHubVersionJson = "https://cmplxty.music/plugins/plugin-hub/version.json";

enum class Platform { Windows, Mac };
Platform hostPlatform();
juce::String platformName (Platform);

int compareVersions (const juce::String& a, const juce::String& b);   // -1 / 0 / +1, garbage == "0.0.0"
bool isAllowedDownload (const juce::String& url);

// --- networking (WinINet / NSURLSession through juce::URL; curl(1) on Linux, where the JUCE build has no TLS)
using Progress = std::function<void (juce::int64 done, juce::int64 total)>;
bool httpGet (const juce::String& url, juce::String& body, juce::String& error, int timeoutMs = 15000);
bool download (const juce::String& url, const juce::File& dest, juce::String& error, Progress = {});
juce::String sha256Of (const juce::File&);

// --- where things live
struct Layout
{
    Platform platform = hostPlatform();
    juce::Array<juce::File> vst3Dirs, clapDirs;   // [0] is where installs go (staged mode) / the first place looked
    juce::Array<juce::File> presetRoots;          // whole trees that must survive every install
    juce::File work;                              // downloads, backups, logs
    bool staged = false;                          // true: install by unpacking into a prefix (tests), never runs an installer
    static Layout system();
    static Layout prefix (const juce::File& root, Platform asPlatform);
};

// --- catalog
enum class Action { Install, Update, Reinstall, Downgrade };
struct Plugin
{
    juce::String slug, name, bundle, type, tag, latest, notes;
    juce::String assetName, url, sha256; juce::int64 size = 0;
    juce::String installed;            // "" = not installed
    juce::File installedVst3, installedClap;
    bool checksumOk() const { return sha256.length() == 64; }
    Action action() const;
};
juce::String actionName (Action);
// Main button wording for the ticked rows: "Install" (only new / reinstall), "Update" (only updates),
// "Install / Update" (a mix), "" when nothing installable is ticked.
juce::String primaryLabel (const juce::Array<Plugin>&, const juce::Array<bool>& checked);

juce::Array<Plugin> parseReleases (const juce::String& releasesJson, Platform, juce::StringArray* skippedLog = nullptr);
void applyVersionJson (Plugin&, const juce::String& versionJson, Platform);   // only if same release
bool resolveChecksums (juce::Array<Plugin>&, juce::String& log);              // SHA256SUMS / .sha256 assets
juce::String parseSumsFile (const juce::String& text, const juce::String& fileName);
void detectInstalled (Plugin&, const Layout&);
juce::String typeFor (const juce::String& slug);
bool fetchCatalog (juce::Array<Plugin>&, const Layout&, juce::String& error, bool withVersionJson = true);

// --- presets
struct Snapshot { std::map<juce::String, juce::String> files; };   // "root|relative path" -> sha256
Snapshot snapshot (const juce::Array<juce::File>& roots);
juce::File backupPresets (const Layout&, juce::String& error);         // copies every preset root into work/backups/<time>
juce::StringArray diffMissingOrChanged (const Snapshot& before, const Snapshot& after);
bool restoreMissing (const juce::File& backup, const Layout&, const juce::StringArray& missing);

// --- install
struct Result { juce::String slug; bool ok = false; juce::String message; };
using Log = std::function<void (const juce::String&)>;
juce::Array<Result> installSelected (juce::Array<Plugin>& selected, const Layout&, Log log, Progress progress = {});

// --- the hub's own update check (version.json with the mac block on macOS, GitHub release tag plugin-hub-v* as fallback)
struct HubUpdate { bool available = false; juce::String version, page; };
HubUpdate checkHubUpdate (Platform, const juce::String& releasesJson = {});
}
