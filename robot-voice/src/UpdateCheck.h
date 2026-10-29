// Robot Voice update notice.
//  * One plain-HTTPS GET of https://cmplxty.music/plugins/robot-voice/version.json, at most once per 24 hours
//    (attempt time is cached in the settings folder), on a background thread - never on the audio thread - with a 4 s connect timeout.
//  * Silent on any failure (offline, 404, garbage, too large, wrong host in the "url" field): nothing is shown, nothing is logged.
//  * Sends no identifiers: no query string, no body, no cookies, no custom headers - only what any HTTPS GET carries (IP, TLS, default user agent).
//  * Can be switched off from the plugin UI (stored in settings.json -> "checkForUpdates", shared by all projects/instances; not a host parameter).
//  * Nothing is downloaded or run: [Update] only opens the plugin's page in the default browser.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <memory>

#ifndef RV_VERSION_STRING
 #define RV_VERSION_STRING "0.0.0"
#endif

namespace rv::upd
{
inline const char* const kVersionUrl = "https://cmplxty.music/plugins/robot-voice/version.json";
inline constexpr int kRefreshSeconds = 24 * 3600, kTimeoutMs = 4000, kMaxBytes = 16384;

struct Info { juce::String version, url, installer, notes; };

// "1.0.2" / "v1.0.2" / "1.2" -> up to 4 numeric parts. Anything else is rejected.
inline bool parseVersion (const juce::String& s0, int (&out)[4])
{
    auto s = s0.trim();
    if (s.startsWithIgnoreCase ("v")) s = s.substring (1);
    if (s.isEmpty() || s.length() > 24) return false;
    const auto parts = juce::StringArray::fromTokens (s, ".", "");
    if (parts.size() < 1 || parts.size() > 4 || s.startsWithChar ('.') || s.endsWithChar ('.') || s.contains ("..")) return false;
    for (int i = 0; i < 4; ++i) out[i] = 0;
    for (int i = 0; i < parts.size(); ++i)
    {
        if (parts[i].isEmpty() || parts[i].length() > 4 || ! parts[i].containsOnly ("0123456789")) return false;
        out[i] = parts[i].getIntValue();
    }
    return true;
}
// -1 / 0 / +1; an unparsable string compares as "equal" (garbage never produces a banner)
inline int compareVersions (const juce::String& a, const juce::String& b)
{
    int x[4], y[4];
    if (! parseVersion (a, x) || ! parseVersion (b, y)) return 0;
    for (int i = 0; i < 4; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
inline bool isNewer (const juce::String& candidate, const juce::String& current) { return compareVersions (candidate, current) > 0; }

// only our own site, only https: a damaged / hijacked version.json can never point the Update button anywhere else
inline bool isTrustedUrl (const juce::String& u)
{
    if (! u.startsWith ("https://") || u.length() > 300 || u.containsAnyOf (" \t\r\n\"'<>\\")) return false;
    const auto host = juce::URL (u).getDomain().toLowerCase();
    return host == "cmplxty.music" || host.endsWith (".cmplxty.music");
}
// the page the Update button opens: "url" is the installer file; a browser would start a download,
// so open the folder / listing page that contains it instead.
inline juce::String pageForUrl (const juce::String& url)
{
    const auto last = url.fromLastOccurrenceOf ("/", false, false).toLowerCase();
    if (last.endsWith (".exe") || last.endsWith (".pkg") || last.endsWith (".dmg") || last.endsWith (".zip") || last.endsWith (".json"))
        return url.upToLastOccurrenceOf ("/", true, false);
    return url;
}

// true on macOS; tests can flip it to exercise the "mac" block on other platforms
inline bool& useMacBlock()
{
   #if JUCE_MAC
    static bool b = true;
   #else
    static bool b = false;
   #endif
    return b;
}

inline bool parseInfo (const juce::String& text, Info& out, bool requireTrustedUrl = true)
{
    juce::var v;
    if (text.length() > kMaxBytes || ! juce::JSON::parse (text, v).wasOk() || ! v.isObject()) return false;
    // On macOS a "mac" block { version, url, installer, sha256, size, notes } wins when it has a version and url;
    // otherwise (and on Windows/Linux) the top-level fields are used.
    juce::var src = v;
    if (useMacBlock())
    {
        const auto m = v.getProperty ("mac", {});
        if (m.isObject() && m.getProperty ("version", {}).toString().trim().isNotEmpty()
                         && m.getProperty ("url", {}).toString().trim().isNotEmpty())
            src = m;
    }
    Info i;
    i.version = src.getProperty ("version", {}).toString().trim();
    i.url = src.getProperty ("url", {}).toString().trim();
    i.installer = src.getProperty ("installer", {}).toString().trim();
    i.notes = src.getProperty ("notes", {}).toString().trim().substring (0, 400);
    int dummy[4];
    if (! parseVersion (i.version, dummy)) return false;
    if (requireTrustedUrl && ! isTrustedUrl (i.url)) return false;
    out = i; return true;
}

// ------------------------------------------------------------------------ settings files (never a host parameter)
inline juce::File& dirOverride() { static juce::File f; return f; }   // tests only
inline juce::File dataDir()
{
    if (dirOverride() != juce::File()) return dirOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CMPLXTY").getChildFile ("Robot Voice");
}
inline juce::var readJson (const juce::File& f)
{
    juce::var v;
    if (f.existsAsFile() && f.getSize() < 65536) juce::JSON::parse (f.loadFileAsString(), v);
    return v;
}
inline void writeJson (const juce::File& f, const juce::var& v)
{
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (v, false));   // failures (read-only profile etc.) are ignored on purpose
}

struct Settings
{
    bool enabled = true;
    juce::String skipVersion;
    static juce::File file() { return dataDir().getChildFile ("settings.json"); }
    static Settings load()
    {
        Settings s; const auto v = readJson (file());
        if (v.isObject()) { s.enabled = (bool) v.getProperty ("checkForUpdates", true); s.skipVersion = v.getProperty ("skipVersion", {}).toString(); }
        return s;
    }
    void save() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("checkForUpdates", enabled); o->setProperty ("skipVersion", skipVersion);
        writeJson (file(), juce::var (o));
    }
};

struct Result
{
    bool ok = false;        // version.json was obtained (network or cache) and parsed
    bool fromCache = false;
    Info info;
};

// ------------------------------------------------------------------------ background checker (owned by the editor)
class Checker : private juce::Thread
{
public:
    Checker() : juce::Thread ("Robot Voice update check") {}
    ~Checker() override { cancelAndWait(); }

    // onResult runs on the message thread, only while this Checker is alive. Does nothing when update checks are off (unless force).
    void start (std::function<void (const Result&)> onResult, bool force = false, juce::String urlOverride = {})
    {
        if (isThreadRunning()) return;
        if (! force && ! Settings::load().enabled) return;
        cb = std::move (onResult); forced = force; url = urlOverride.isNotEmpty() ? urlOverride : juce::String (kVersionUrl);
        startThread (juce::Thread::Priority::low);
    }
    void cancelAndWait()
    {
        signalThreadShouldExit();
        { const juce::ScopedLock sl (lock); if (stream != nullptr) stream->cancel(); }
        stopThread (kTimeoutMs + 3000);
    }
    // synchronous variant for tests (never call from a UI / audio thread)
    static Result checkNow (const juce::String& url, bool force, bool requireTrustedUrl = true)
    {
        Checker c; c.forced = force; c.url = url; c.trusted = requireTrustedUrl; return c.work();
    }

private:
    Result work()
    {
        Result r;
        const auto cacheFile = dataDir().getChildFile ("update-cache.json");
        const auto nowS = (juce::int64) (juce::Time::currentTimeMillis() / 1000);
        if (! forced)
        {
            const auto c = readJson (cacheFile);
            if (c.isObject())
            {
                const auto age = nowS - (juce::int64) c.getProperty ("checked", 0);
                if (age >= 0 && age < kRefreshSeconds)      // an attempt (successful or not) was made within the last 24 h
                {
                    r.fromCache = true;
                    if ((bool) c.getProperty ("ok", false)) { Info i; r.ok = parseInfo (juce::JSON::toString (c.getProperty ("info", {}), true), i, trusted); r.info = i; }
                    return r;
                }
            }
        }
        // record the attempt first, so a second instance starting right now (or a crash mid-request) does not ask again
        { auto* o = new juce::DynamicObject(); o->setProperty ("checked", nowS); o->setProperty ("ok", false); writeJson (cacheFile, juce::var (o)); }

        juce::String body; bool got = false;
        if (! threadShouldExit())
        {
            juce::WebInputStream wis (juce::URL (url), false);
            wis.withConnectionTimeout (kTimeoutMs);
            { const juce::ScopedLock sl (lock); stream = &wis; }
            if (wis.connect (nullptr) && wis.getStatusCode() == 200 && ! threadShouldExit())
            {
                juce::MemoryBlock mb; char buf[2048];
                for (;;)
                {
                    const int n = wis.read (buf, (int) sizeof (buf));
                    if (n <= 0 || threadShouldExit()) break;
                    mb.append (buf, (size_t) n);
                    if (mb.getSize() > (size_t) kMaxBytes) { mb.reset(); break; }
                }
                body = mb.toString(); got = body.isNotEmpty() && ! threadShouldExit();
            }
            { const juce::ScopedLock sl (lock); stream = nullptr; }
        }
        Info info;
        r.ok = got && parseInfo (body, info, trusted);
        if (r.ok) r.info = info;
        if (r.ok && ! threadShouldExit())
        {
            auto* o = new juce::DynamicObject(); auto* io = new juce::DynamicObject();
            o->setProperty ("checked", nowS); o->setProperty ("ok", true);
            io->setProperty ("version", info.version); io->setProperty ("url", info.url); io->setProperty ("installer", info.installer); io->setProperty ("notes", info.notes);
            o->setProperty ("info", juce::var (io));
            writeJson (cacheFile, juce::var (o));
        }
        return r;
    }
    void run() override
    {
        const auto r = work();
        if (threadShouldExit()) return;
        auto alive = std::weak_ptr<int> (token);
        juce::MessageManager::callAsync ([alive, r, f = cb] { if (alive.lock() && f) f (r); });
    }

    std::shared_ptr<int> token = std::make_shared<int> (0);
    std::function<void (const Result&)> cb; bool forced = false, trusted = true; juce::String url;
    juce::CriticalSection lock; juce::WebInputStream* stream = nullptr;
};

inline bool shouldNotify (const Result& r, const Settings& s, bool ignoreSkip = false)
{
    return r.ok && isNewer (r.info.version, RV_VERSION_STRING) && (ignoreSkip || r.info.version != s.skipVersion);
}
} // namespace rv::upd
