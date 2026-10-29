// Update-check tests. Offline: version parse / compare, version.json parsing, settings + cache files, notify policy.
// With  --net <port>  (run under Wine, against tests/update_server.py): the real JUCE WebInputStream (wininet) path:
// success, same version, 404, garbage, oversize, refused connection, hung server (timeout), cancel, 24 h cache.
#include "../src/UpdateCheck.h"
#include <cstdio>
#include <chrono>

using namespace vv::upd;
static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++fails; std::printf ("FAIL line %d  %s\n", __LINE__, #c); } } while (0)

static double secondsSince (std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(); }

static void offline()
{
    // ---- compare
    CHECK (isNewer ("1.0.2", "1.0.1")); CHECK (! isNewer ("1.0.1", "1.0.2")); CHECK (! isNewer ("1.0.2", "1.0.2"));
    CHECK (isNewer ("1.1.0", "1.0.9")); CHECK (isNewer ("2.0", "1.9.9")); CHECK (isNewer ("1.0.10", "1.0.9")); CHECK (! isNewer ("1.0.9", "1.0.10"));
    CHECK (isNewer ("v1.0.3", "1.0.2")); CHECK (isNewer ("1.0.2.1", "1.0.2")); CHECK (! isNewer ("1.0", "1.0.0")); CHECK (compareVersions ("1.0", "1.0.0.0") == 0);
    CHECK (isNewer ("10.0.0", "9.99.99"));
    // garbage never looks newer
    for (const char* bad : { "", "x", "1.x", "1..2", ".1", "1.", "1.0.2-beta", "1.0.2 ", "99999.0", "1.2.3.4.5", "-1.0", "1,0,2", "1.0.2; rm -rf /", "١.٠" })
    {
        int o[4]; const bool ok = parseVersion (bad, o);
        if (juce::String (bad) == "1.0.2 ") CHECK (ok);     // surrounding whitespace is trimmed
        else CHECK (! ok);
        CHECK (! isNewer (bad, "1.0.0") || juce::String (bad) == "1.0.2 ");
    }
    // ---- version.json
    Info i;
    const juce::String good = R"j({"version":"1.0.2","url":"https://cmplxty.music/plugins/vvitch/","installer":"https://cmplxty.music/plugins/vvitch/CMPLXTY-VVITCH-Setup-1.0.0-win.exe","notes":"Gender fix"})j";
    { // "mac" block: used only when useMacBlock() (true on macOS), falls back to top level
        const char* mj = R"j({"version":"1.0.5","url":"https://cmplxty.music/a.exe","mac":{"version":"1.0.6","url":"https://cmplxty.music/a.pkg","installer":"a.pkg","sha256":"00","size":1,"notes":"m"}})j";
        Info mi; useMacBlock() = false;
        CHECK (parseInfo (mj, mi) && mi.version == "1.0.5" && mi.url.endsWith (".exe"));
        useMacBlock() = true;
        CHECK (parseInfo (mj, mi) && mi.version == "1.0.6" && mi.url.endsWith (".pkg") && mi.installer == "a.pkg" && mi.notes == "m");
        CHECK (parseInfo (R"j({"version":"1.0.5","url":"https://cmplxty.music/a.exe"})j", mi) && mi.version == "1.0.5");
        CHECK (parseInfo (R"j({"version":"1.0.5","url":"https://cmplxty.music/a.exe","mac":{"version":""}})j", mi) && mi.version == "1.0.5");
        CHECK (! parseInfo (R"j({"version":"1.0.5","url":"https://cmplxty.music/a.exe","mac":{"version":"1.0.6","url":"https://evil.example/a.pkg"}})j", mi));
        useMacBlock() = false;
    }
    CHECK (parseInfo (good, i) && i.version == "1.0.2" && i.url == "https://cmplxty.music/plugins/vvitch/" && i.installer.endsWith ("CMPLXTY-VVITCH-Setup-1.0.0-win.exe") && i.notes == "Gender fix");
    CHECK (parseInfo (R"j({"version":"1.0.2","url":"https://cmplxty.music/plugins/vvitch/"})j", i) && i.installer.isEmpty() && i.notes.isEmpty());
    // the shape the listing publishes: url = direct installer URL, "Update" opens its folder page
    CHECK (parseInfo (R"j({"version":"1.0.1","url":"https://cmplxty.music/plugins/vvitch/CMPLXTY-VVITCH-Setup-1.0.1-win.exe","installer":"CMPLXTY-VVITCH-Setup-1.0.1-win.exe","notes":"n"})j", i) && i.installer == "CMPLXTY-VVITCH-Setup-1.0.1-win.exe");
    CHECK (pageForUrl (i.url) == "https://cmplxty.music/plugins/vvitch/" && isTrustedUrl (pageForUrl (i.url)));
    CHECK (pageForUrl ("https://cmplxty.music/plugins/vvitch/") == "https://cmplxty.music/plugins/vvitch/");
    CHECK (parseInfo (R"j({"version":"1.0.2","url":"https://www.cmplxty.music/q","extra":[1,2,3]})j", i));
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"http://cmplxty.music/plugins/vvitch/"})j", i));           // not https
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"https://evil.example/cmplxty.music"})j", i));           // other host
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"https://cmplxty.music.evil.example/"})j", i));
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"https://evilcmplxty.music/"})j", i));
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"https://cmplxty.music/a b"})j", i));
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"javascript:alert(1)"})j", i));
    CHECK (! parseInfo (R"j({"version":"1.0.2","url":"file:///C:/Windows/System32/calc.exe"})j", i));
    CHECK (! parseInfo (R"j({"version":"latest","url":"https://cmplxty.music/"})j", i));
    CHECK (! parseInfo (R"j({"url":"https://cmplxty.music/"})j", i));
    CHECK (! parseInfo (R"j([1,2,3])j", i)); CHECK (! parseInfo ("", i)); CHECK (! parseInfo ("<html>404</html>", i)); CHECK (! parseInfo ("{\"version\":", i));
    CHECK (! parseInfo (juce::String::repeatedString ("x", 20000), i));
    CHECK (parseInfo (R"j({"version":"1.0.2","url":"http://127.0.0.1:1/"})j", i, false));                      // test-only relaxed mode
    // ---- files
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dr_upd_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dirOverride() = tmp;
    { Settings s = Settings::load(); CHECK (s.enabled && s.skipVersion.isEmpty()); }                       // no file: checks ON by default
    { Settings s; s.enabled = false; s.skipVersion = "1.0.9"; s.save(); }
    { Settings s = Settings::load(); CHECK (! s.enabled && s.skipVersion == "1.0.9"); }
    tmp.getChildFile ("settings.json").replaceWithText ("{ not json");
    { Settings s = Settings::load(); CHECK (s.enabled && s.skipVersion.isEmpty()); }                       // corrupt file: defaults, no crash
    // ---- notify policy (current build = VV_VERSION_STRING)
    {
        Result r; r.ok = true; r.info.version = "99.0.0"; Settings s;
        CHECK (shouldNotify (r, s));
        s.skipVersion = "99.0.0"; CHECK (! shouldNotify (r, s)); CHECK (shouldNotify (r, s, true));          // "check now" ignores the skip
        r.info.version = "99.0.1"; CHECK (shouldNotify (r, s));                                              // a newer one is announced again
        r.info.version = VV_VERSION_STRING; CHECK (! shouldNotify (r, Settings()));
        r.info.version = "0.9.0"; CHECK (! shouldNotify (r, Settings()));
        r.ok = false; r.info.version = "99.0.0"; CHECK (! shouldNotify (r, Settings()));
    }
    // opt-out: start() does nothing (no thread, no callback, no file written)
    {
        Settings s; s.enabled = false; s.save();
        bool called = false; Checker c; c.start ([&] (const Result&) { called = true; }, false, "http://127.0.0.1:1/x");
        juce::Thread::sleep (300); juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        CHECK (! called); CHECK (! tmp.getChildFile ("update-cache.json").existsAsFile());
    }
    tmp.deleteRecursively(); dirOverride() = juce::File();
    std::printf ("  offline: %d checks\n", checks);
}

static void net (int port)
{
    const juce::String base = "http://127.0.0.1:" + juce::String (port);
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gc_upd_net_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    dirOverride() = tmp;
    auto cache = [&] { return tmp.getChildFile ("update-cache.json"); };
    auto wipe = [&] { cache().deleteFile(); };

    auto r = Checker::checkNow (base + "/new.json", true);
    CHECK (r.ok && ! r.fromCache && r.info.version == "99.0.0" && r.info.url == "https://cmplxty.music/plugins/vvitch/" && r.info.notes == "net test");
    CHECK (shouldNotify (r, Settings()));
    CHECK (cache().existsAsFile());
    // second call within 24 h: from cache, server not contacted (the server counts requests; the driver checks the count)
    r = Checker::checkNow (base + "/new.json", false);
    CHECK (r.ok && r.fromCache && r.info.version == "99.0.0");
    // dead server but fresh cache: still fine
    r = Checker::checkNow ("http://127.0.0.1:9/never.json", false);
    CHECK (r.ok && r.fromCache);
    // stale cache (25 h) -> network again
    { auto v = readJson (cache()); auto* o = v.getDynamicObject(); o->setProperty ("checked", (juce::int64) (juce::Time::currentTimeMillis() / 1000) - 25 * 3600); writeJson (cache(), v); }
    r = Checker::checkNow (base + "/same.json", false);
    CHECK (r.ok && ! r.fromCache && ! shouldNotify (r, Settings()));
    wipe();
    r = Checker::checkNow (base + "/404", true);           CHECK (! r.ok);
    r = Checker::checkNow (base + "/garbage", true);       CHECK (! r.ok);
    r = Checker::checkNow (base + "/big", true);           CHECK (! r.ok);
    r = Checker::checkNow (base + "/evilurl.json", true);  CHECK (! r.ok);
    r = Checker::checkNow (base + "/redirect", true);      CHECK (r.ok && r.info.version == "99.0.0");   // 302 -> /new.json
    // a failed attempt also counts: at most one request per 24 h, even when offline
    wipe();
    r = Checker::checkNow ("http://127.0.0.1:9/refused.json", true);   CHECK (! r.ok);
    { const auto v = readJson (cache()); CHECK (v.isObject() && ! (bool) v.getProperty ("ok", true)); }
    r = Checker::checkNow (base + "/new.json", false);     CHECK (! r.ok && r.fromCache);
    wipe();
    // hung server: must give up by itself within a few seconds
    {
        const auto t0 = std::chrono::steady_clock::now();
        r = Checker::checkNow (base + "/slow", true);
        const double dt = secondsSince (t0);
        std::printf ("  hung server returned after %.1f s\n", dt);
        CHECK (! r.ok); CHECK (dt < 12.0);
    }
    // threaded path as the editor uses it (callback on the message thread; destroying the Checker while running is safe)
    {
        wipe(); juce::ScopedJuceInitialiser_GUI gui; bool got = false; Result gr;
        {
            Checker c; c.start ([&] (const Result& x) { got = true; gr = x; }, true, base + "/new.json");
            const auto t0 = std::chrono::steady_clock::now();
            while (! got && secondsSince (t0) < 10.0) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
        CHECK (got && gr.ok && gr.info.version == "99.0.0");
        wipe(); got = false;
        {   Checker c; c.start ([&] (const Result&) { got = true; }, true, base + "/slow"); juce::Thread::sleep (300); }   // destroyed mid-request
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (! got);
        std::printf ("  destroy-while-running returned promptly\n");
    }
    tmp.deleteRecursively(); dirOverride() = juce::File();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int port = 0;
    for (int i = 1; i + 1 < argc; ++i) if (juce::String (argv[i]) == "--net") port = juce::String (argv[i + 1]).getIntValue();
    if (port == 0) offline(); else net (port);
    std::printf ("update_test%s: %d checks, %d failures\n", port ? " --net" : "", checks, fails);
    return fails == 0 ? 0 : 1;
}
