// Quartz update notice (1.0.2; mac block 1.0.4).
//  * One plain-HTTPS GET of https://cmplxty.music/quartz-engine/version.json when the editor opens - at most once per 24 h (cached in
//    %APPDATA%\Quartz\update-cache.json), on a background thread, never on the audio thread, 4 s connect timeout, silent when offline.
//  * No identifiers, no analytics, no cookies, no query string, no request body: only what any HTTPS GET carries (IP address, TLS, default user agent).
//  * Can be turned off (%APPDATA%\Quartz\settings.json -> "checkForUpdates": false; this is not a host parameter).
//  * Nothing is downloaded or run: [Update] only opens the Quartz page in the default browser.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <memory>
#include "Version.h"

namespace qz::upd
{
inline const char* const kVersionUrl = "https://cmplxty.music/quartz-engine/version.json";
inline constexpr int kRefreshSeconds = 24 * 3600, kRetryAfterFailureSeconds = 6 * 3600, kTimeoutMs = 4000, kMaxBytes = 16384;

struct Info
{
    juce::String version, url, installer, notes;
};

// "1.0.2" / "v1.0.2" / "1.2" -> up to 4 numeric parts. Anything else (letters, empty parts, huge numbers) is rejected.
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
// -1 / 0 / +1; an unparsable string compares as "equal" (so garbage never produces a banner)
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

// The Mac build reads the "mac" block of version.json when it has one ({"version","url","installer","notes"}; a missing field falls back to
// the top level), so a Windows-only release never nags Mac users and the Mac banner links the Mac installer. No usable mac block = top level.
#if JUCE_MAC
 inline constexpr bool kIsMacBuild = true;
#else
 inline constexpr bool kIsMacBuild = false;
#endif
inline bool parseInfo (const juce::String& text, Info& out, bool requireTrustedUrl = true, bool forMac = kIsMacBuild)
{
    juce::var v;
    if (text.length() > kMaxBytes || ! juce::JSON::parse (text, v).wasOk() || ! v.isObject()) return false;
    auto field = [&v, forMac] (const char* k)
    {
        const auto m = v.getProperty ("mac", {});
        if (forMac && m.isObject() && m.hasProperty (k)) return m.getProperty (k, {}).toString().trim();
        return v.getProperty (k, {}).toString().trim();
    };
    Info i;
    i.version = field ("version");
    i.url = field ("url");
    i.installer = field ("installer");
    i.notes = field ("notes").substring (0, 400);
    int dummy[4];
    if (! parseVersion (i.version, dummy)) return false;
    if (requireTrustedUrl && ! isTrustedUrl (i.url)) return false;
    out = i; return true;
}

// ---------------------------------------------------------------- files (%APPDATA%\Quartz, never a host parameter)
inline juce::File& dirOverride() { static juce::File f; return f; }   // tests only
inline juce::File dataDir()
{
    if (dirOverride() != juce::File()) return dirOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Quartz");
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
    f.replaceWithText (juce::JSON::toString (v, false));          // failures (read-only profile etc.) are ignored on purpose
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
    bool ok = false;            // version.json was obtained (network or cache) and parsed
    bool fromCache = false;
    Info info;
};

// ---------------------------------------------------------------- background checker (owned by the editor)
class Checker : private juce::Thread
{
public:
    Checker() : juce::Thread ("Quartz update check") {}
    ~Checker() override { cancelAndWait(); }

    // onResult runs on the message thread, only while this Checker is alive. Does nothing if the user turned update checks off (unless force).
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
    static Result checkNow (const juce::String& url, bool force, std::atomic<bool>* cancelFlag = nullptr)
    {
        Checker c; c.forced = force; c.url = url; c.cancelFlag = cancelFlag; return c.work();
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
                const auto checked = (juce::int64) c.getProperty ("checked", 0);
                const bool wasOk = (bool) c.getProperty ("ok", false);
                const auto age = nowS - checked;
                if (age >= 0 && age < (wasOk ? kRefreshSeconds : kRetryAfterFailureSeconds))
                {
                    r.fromCache = true;
                    if (wasOk) { Info i; r.ok = parseInfo (juce::JSON::toString (c.getProperty ("info", {}), true), i, true); r.info = i; }
                    return r;
                }
            }
        }
        juce::String body; bool got = false;
        if (! shouldStop())
        {
            juce::WebInputStream wis (juce::URL (url), false);
            wis.withConnectionTimeout (kTimeoutMs);
            { const juce::ScopedLock sl (lock); stream = &wis; }
            if (wis.connect (nullptr) && wis.getStatusCode() == 200 && ! shouldStop())
            {
                juce::MemoryBlock mb; char buf[2048];
                for (;;)
                {
                    const int n = wis.read (buf, (int) sizeof (buf));
                    if (n <= 0 || shouldStop()) break;
                    mb.append (buf, (size_t) n);
                    if (mb.getSize() > (size_t) kMaxBytes) { mb.reset(); break; }
                }
                body = mb.toString(); got = body.isNotEmpty() && ! shouldStop();
            }
            { const juce::ScopedLock sl (lock); stream = nullptr; }
        }
        Info info;
        r.ok = got && parseInfo (body, info);
        if (r.ok) r.info = info;
        if (! shouldStop())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("checked", nowS); o->setProperty ("ok", r.ok);
            if (r.ok)
            {
                auto* io = new juce::DynamicObject();
                io->setProperty ("version", info.version); io->setProperty ("url", info.url); io->setProperty ("installer", info.installer); io->setProperty ("notes", info.notes);
                o->setProperty ("info", juce::var (io));
            }
            writeJson (cacheFile, juce::var (o));
        }
        return r;
    }
    bool shouldStop() const { return threadShouldExit() || (cancelFlag != nullptr && cancelFlag->load()); }
    void run() override
    {
        const auto r = work();
        if (threadShouldExit()) return;
        auto alive = std::weak_ptr<int> (token);
        juce::MessageManager::callAsync ([alive, r, f = cb]
        {
            if (alive.lock() && f) f (r);
        });
    }

    std::shared_ptr<int> token = std::make_shared<int> (0);
    std::function<void (const Result&)> cb; bool forced = false; juce::String url;
    juce::CriticalSection lock; juce::WebInputStream* stream = nullptr; std::atomic<bool>* cancelFlag = nullptr;
};

// Whether a result should put up the banner for this build.
inline bool shouldNotify (const Result& r, const Settings& s, bool ignoreSkip = false)
{
    return r.ok && isNewer (r.info.version, QUARTZ_VERSION_STRING) && (ignoreSkip || r.info.version != s.skipVersion);
}
} // namespace qz::upd
