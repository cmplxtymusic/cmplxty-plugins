'use strict';
// Update notice tests, against a local HTTP server. Run with: node tests/updater.js
const assert = require('assert');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const U = require('../src/updater');

(async () => {
  assert.strictEqual(U.cmpVersion('2.1.0', '2.0.0'), 1); assert.strictEqual(U.cmpVersion('2.0.0', '2.0.0'), 0);
  assert.strictEqual(U.cmpVersion('2.0.9', '2.1.0'), -1); assert.strictEqual(U.cmpVersion('2.10.0', '2.9.0'), 1); assert.strictEqual(U.cmpVersion('v3', '2.9.9'), 1);
  const now = Date.now();
  assert.strictEqual(U.shouldAutoCheck({}, now), true);
  assert.strictEqual(U.shouldAutoCheck({ lastUpdateCheck: now - 3600e3 }, now), false);
  assert.strictEqual(U.shouldAutoCheck({ lastUpdateCheck: now - 25 * 3600e3 }, now), true);
  assert.strictEqual(U.shouldAutoCheck({ autoUpdateCheck: false, lastUpdateCheck: 0 }, now), false);

  const payload = crypto.randomBytes(300000);
  const sha = crypto.createHash('sha256').update(payload).digest('hex');
  let mode = 'ok'; const seen = [];
  const srv = http.createServer((req, res) => {
    seen.push({ url: req.url, headers: req.headers });
    if (req.url.startsWith('/v.json')) {
      if (mode === 'hang') return;                        // never answers
      if (mode === '500') { res.statusCode = 500; return res.end('no'); }
      if (mode === 'junk') return res.end('<html>nope</html>');
      const base = 'http://127.0.0.1:' + srv.address().port;
      const j = { version: mode === 'same' ? '2.0.0' : '2.1.0', url: 'https://cmplxty.music/easter-egg/', installer: base + '/i.exe', sha256: mode === 'badsha' ? 'a'.repeat(64) : sha, size: payload.length, notes: 'x' };
      if (mode === 'evil') j.installer = 'https://evil.example/i.exe';
      return res.end(JSON.stringify(j));
    }
    if (req.url === '/i.exe') { res.setHeader('content-length', payload.length); return res.end(payload); }
    res.statusCode = 404; res.end();
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + srv.address().port;
  // These fixtures are the Windows update file (no "mac" block). Pin the platform so a Mac CI runner
  // does not take the darwin path, which treats a Windows-only release as "nothing new".
  const opts = { url: base + '/v.json', allowLocal: true, timeoutMs: 800, platform: 'win32' };

  let r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'available'); assert.strictEqual(r.info.version, '2.1.0');
  // sends no identifiers: no cookie, no user agent, no custom headers
  const h = seen[0].headers; assert.ok(!h['user-agent'] && !h.cookie && !h.authorization && !Object.keys(h).some((k) => /^x-/.test(k)), JSON.stringify(h));
  mode = 'same'; r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'latest');
  mode = 'ok'; r = await U.checkForUpdate('2.1.0', opts); assert.strictEqual(r.status, 'latest');
  mode = '500'; r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'error');
  mode = 'junk'; r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'error');
  mode = 'hang'; const t0 = Date.now(); r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'error'); assert.ok(Date.now() - t0 < 3000, 'timeout is short');
  r = await U.checkForUpdate('2.0.0', { url: 'http://127.0.0.1:1/v.json', timeoutMs: 500, platform: 'win32' }); assert.strictEqual(r.status, 'error');   // nothing listening: silent error, no throw
  mode = 'evil'; r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'available'); assert.strictEqual(r.info.installer, '', 'installer from a foreign host is dropped');

  // download + verify
  mode = 'ok'; const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-up-'));
  r = await U.checkForUpdate('2.0.0', opts);
  let last = 0; const f = await U.downloadInstaller(r.info, dir, (g, t) => { last = g / t; });
  assert.ok(fs.readFileSync(f).equals(payload)); assert.ok(Math.abs(last - 1) < 1e-9); assert.ok(!fs.existsSync(f + '.part'));
  mode = 'badsha'; r = await U.checkForUpdate('2.0.0', opts);
  await assert.rejects(U.downloadInstaller(r.info, dir + '/b', () => {}), /checksum/);
  assert.deepStrictEqual(fs.readdirSync(dir + '/b'), [], 'a bad download is deleted');
  await assert.rejects(U.downloadInstaller({ installer: base + '/i.exe', version: '9.9.9', sha256: '' }, dir), /checksum/);
  await assert.rejects(U.downloadInstaller({ installer: base + '/nope', version: '9.9.9', sha256: sha }, dir), /404/);
  srv.close(); fs.rmSync(dir, { recursive: true, force: true });
  console.log('updater tests passed');
  process.exit(0);
})().catch((e) => { console.error(e); process.exit(1); });
