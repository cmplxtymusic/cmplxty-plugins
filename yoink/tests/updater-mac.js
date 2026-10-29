'use strict';
// macOS update flow tests. They run on Linux too: the Mac pieces are exercised with stand-in hdiutil/ditto/open commands.
// Run with: node tests/updater-mac.js
const assert = require('assert');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { spawnSync } = require('child_process');
const U = require('../src/updater');

(async () => {
  // ---- parseInfo reads the "mac" block on a Mac and never the Windows fields
  const win = { version: '2.1.0', url: 'https://cmplxty.music/easter-egg/', installer: 'https://cmplxty.music/yoink.exe', sha256: 'a'.repeat(64), size: 5, notes: 'win' };
  assert.strictEqual(U.parseInfo(win, { platform: 'darwin' }), null, 'no mac block: nothing for a Mac');
  assert.strictEqual(U.parseInfo(win).installer, 'https://cmplxty.music/yoink.exe', 'default (Windows) parsing is unchanged');
  const withMac = Object.assign({}, win, { mac: { version: '2.1.0', installer: 'https://cmplxty.music/yoink.dmg', sha256: 'B'.repeat(64), size: 99, notes: 'mac' } });
  const m = U.parseInfo(withMac, { platform: 'darwin' });
  assert.strictEqual(m.installer, 'https://cmplxty.music/yoink.dmg'); assert.strictEqual(m.sha256, 'b'.repeat(64)); assert.strictEqual(m.size, 99); assert.strictEqual(m.notes, 'mac');
  assert.strictEqual(U.parseInfo(withMac).installer, 'https://cmplxty.music/yoink.exe', 'mac block ignored on Windows');
  assert.strictEqual(U.parseInfo({ version: '2.1.0', mac: { version: '2.1.0', installer: 'https://evil.example/y.dmg' } }, { platform: 'darwin' }).installer, '', 'foreign host dropped');
  assert.strictEqual(U.parseInfo({ version: '2.1.0', mac: { installer: 'https://cmplxty.music/yoink.dmg' } }, { platform: 'darwin' }).version, '2.1.0', 'mac block can inherit the top-level version');
  assert.strictEqual(U.parseInfo({ mac: { version: 'nope' } }, { platform: 'darwin' }), null);

  // ---- macAppPath
  assert.strictEqual(U.macAppPath('/Applications/Yoink.app/Contents/MacOS/Yoink'), '/Applications/Yoink.app');
  assert.strictEqual(U.macAppPath('/Users/a b/Apps/Yoink.app/Contents/MacOS/Yoink'), '/Users/a b/Apps/Yoink.app');
  assert.strictEqual(U.macAppPath('/usr/lib/electron/electron'), '');

  // ---- check + download against a local server
  const payload = crypto.randomBytes(200000); const sha = crypto.createHash('sha256').update(payload).digest('hex');
  let withBlock = true;
  const srv = http.createServer((req, res) => {
    if (req.url === '/v.json') {
      const base = 'http://127.0.0.1:' + srv.address().port;
      const j = { version: '2.1.0', url: 'https://cmplxty.music/easter-egg/', installer: base + '/i.exe', sha256: 'c'.repeat(64), size: 1, notes: 'win' };
      if (withBlock) j.mac = { version: '2.2.0', installer: base + '/y.dmg', sha256: sha, size: payload.length, notes: 'mac' };
      return res.end(JSON.stringify(j));
    }
    if (req.url === '/y.dmg') { res.setHeader('content-length', payload.length); return res.end(payload); }
    res.statusCode = 404; res.end();
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const opts = { url: 'http://127.0.0.1:' + srv.address().port + '/v.json', allowLocal: true, timeoutMs: 800, platform: 'darwin' };
  let r = await U.checkForUpdate('2.1.0', opts); assert.strictEqual(r.status, 'available'); assert.strictEqual(r.info.version, '2.2.0'); assert.ok(r.info.installer.endsWith('/y.dmg'));
  r = await U.checkForUpdate('2.2.0', opts); assert.strictEqual(r.status, 'latest');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-upmac-'));
  r = await U.checkForUpdate('2.1.0', opts);
  const f = await U.downloadInstaller(r.info, dir, () => {}, undefined, 5, { ext: '.dmg' });
  assert.strictEqual(path.basename(f), 'Yoink-2.2.0-mac.dmg'); assert.ok(fs.readFileSync(f).equals(payload));
  withBlock = false; r = await U.checkForUpdate('2.0.0', opts); assert.strictEqual(r.status, 'latest', 'Windows-only release: Mac users are not told');
  srv.close();

  // ---- the swap script, run with stand-in commands
  const dead = spawnSync('true').pid;
  function scenario(name, { dittoFails }) {
    const t = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-swap-'));
    const apps = path.join(t, 'My Apps'), stubs = path.join(t, 'stubs'), newSrc = path.join(t, 'newsrc', 'Yoink.app'), mnt = path.join(t, 'mnt');
    for (const [d, v] of [[path.join(apps, 'Yoink.app'), 'old'], [newSrc, 'new']]) { fs.mkdirSync(path.join(d, 'Contents'), { recursive: true }); fs.writeFileSync(path.join(d, 'Contents', 'ver'), v); }
    fs.mkdirSync(stubs); const log = path.join(t, 'log.txt'); const dmg = path.join(t, 'Yoink-2.2.0-mac.dmg'); fs.writeFileSync(dmg, 'x');
    const stub = (n, body) => fs.writeFileSync(path.join(stubs, n), '#!/bin/bash\necho "' + n + ' $*" >> ' + JSON.stringify(log) + '\n' + body + '\n', { mode: 0o755 });
    stub('hdiutil', 'if [ "$1" = attach ]; then while [ $# -gt 0 ]; do [ "$1" = -mountpoint ] && M="$2"; shift; done; cp -R ' + JSON.stringify(newSrc) + ' "$M/"; fi');
    stub('ditto', dittoFails ? 'exit 1' : 'cp -R "$1" "$2"');
    stub('open', 'true'); stub('xattr', 'true');
    const script = path.join(t, 'u.sh');
    fs.writeFileSync(script, U.macInstallScript({ dmg, appPath: path.join(apps, 'Yoink.app'), pid: dead, mountPoint: mnt }), { mode: 0o755 });
    const out = spawnSync('/bin/bash', [script], { env: Object.assign({}, process.env, { PATH: stubs + ':' + process.env.PATH }), encoding: 'utf8' });
    assert.strictEqual(out.status, 0, name + ': ' + out.stderr);
    return { apps, dmg, log: fs.readFileSync(log, 'utf8'), ver: () => fs.readFileSync(path.join(apps, 'Yoink.app', 'Contents', 'ver'), 'utf8'), left: fs.readdirSync(apps), script };
  }
  let s = scenario('swap', { dittoFails: false });
  assert.strictEqual(s.ver(), 'new', 'new app is in place'); assert.deepStrictEqual(s.left, ['Yoink.app'], 'no leftovers next to the app');
  assert.ok(!fs.existsSync(s.dmg), 'dmg deleted after success'); assert.ok(!fs.existsSync(s.script), 'script removes itself');
  assert.ok(/hdiutil attach .*-nobrowse -readonly/.test(s.log) && /hdiutil detach/.test(s.log), 'mounted hidden, then detached');
  assert.ok(/open .*My Apps\/Yoink\.app/.test(s.log), 'relaunches the app (path with a space)');
  s = scenario('failed copy', { dittoFails: true });
  assert.strictEqual(s.ver(), 'old', 'a failed copy leaves the old app alone'); assert.deepStrictEqual(s.left, ['Yoink.app']);
  assert.ok(fs.existsSync(s.dmg), 'dmg kept'); assert.ok(/open .*\.dmg/.test(s.log), 'falls back to showing the disk image');
  console.log('updater-mac ok');
})().catch((e) => { console.error(e); process.exit(1); });
