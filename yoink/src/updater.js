'use strict';
// In-app update notice. No Electron dependency, so it can be tested from the command line.
// The app fetches one small JSON file, sends nothing about the user or the PC, and fails silently.
const https = require('https');
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawn } = require('child_process');
const os = require('os');

const VERSION_URL = 'https://cmplxty.music/yoink-version.json';
const DAY_MS = 24 * 3600 * 1000;
const TRUSTED_HOST = /^(?:[\w-]+\.)*cmplxty\.music$/i;

// "2.1.0" vs "2.0.9": 1, 0 or -1. Missing parts count as 0. Anything after a dash (2.1.0-beta) is ignored.
function cmpVersion(a, b) {
  const p = (v) => String(v || '').trim().replace(/^v/i, '').split('-')[0].split('.').map((x) => parseInt(x, 10) || 0);
  const x = p(a), y = p(b);
  for (let i = 0; i < Math.max(x.length, y.length, 3); i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d > 0 ? 1 : -1; }
  return 0;
}

function trustedUrl(u, allowAnyHost) {
  try { const x = new URL(u); return x.protocol === 'https:' && (allowAnyHost || TRUSTED_HOST.test(x.hostname)); } catch (_) { return false; }
}
function localUrl(u) { try { const x = new URL(u); return x.protocol === 'http:' && /^(127\.0\.0\.1|localhost)$/.test(x.hostname); } catch (_) { return false; } }

// Accept only well-formed info. The installer link must be on cmplxty.music (or a test server on localhost when testing).
// opts.platform: 'darwin' reads the "mac" block of the file ({ version?, installer, sha256, size, url?, notes? }); anything else reads the
// top-level Windows fields exactly as before. A Mac with no "mac" block gets null, so a Windows-only release never nags Mac users.
function parseInfo(j, opts = {}) {
  if (!j || typeof j !== 'object') return null;
  if (opts.platform === 'darwin') {
    if (!j.mac || typeof j.mac !== 'object') return null;
    j = Object.assign({}, { url: j.url, notes: j.notes, version: j.version }, j.mac);
  }
  const version = String(j.version || '').trim();
  if (!/^\d+\.\d+\.\d+$/.test(version)) return null;
  const ok = (u) => trustedUrl(u, false) || (opts.allowLocal && localUrl(u));
  const url = ok(j.url) ? String(j.url) : 'https://cmplxty.music/easter-egg/';
  const installer = ok(j.installer) ? String(j.installer) : '';
  const sha256 = /^[0-9a-f]{64}$/i.test(String(j.sha256 || '')) ? String(j.sha256).toLowerCase() : '';
  const size = Number.isFinite(Number(j.size)) && Number(j.size) > 0 ? Number(j.size) : 0;
  return { version, url, installer, sha256, size, notes: String(j.notes || '').slice(0, 300) };
}

function getBuffer(url, { timeoutMs = 6000, maxBytes = 65536, signal } = {}, redirects = 3) {
  return new Promise((resolve, reject) => {
    const lib = /^http:/i.test(url) ? http : https;
    let done = false;
    const fail = (e) => { if (!done) { done = true; reject(e); } };
    const req = lib.get(url, { headers: { 'Accept': 'application/json', 'Cache-Control': 'no-cache' }, timeout: timeoutMs }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirects > 0) {
        res.resume(); return resolve(getBuffer(new URL(res.headers.location, url).toString(), { timeoutMs, maxBytes, signal }, redirects - 1));
      }
      if (res.statusCode !== 200) { res.resume(); return fail(new Error('HTTP ' + res.statusCode)); }
      const chunks = []; let n = 0;
      res.on('data', (b) => { n += b.length; if (n > maxBytes) { req.destroy(); fail(new Error('too big')); } else chunks.push(b); });
      res.on('end', () => { if (!done) { done = true; resolve(Buffer.concat(chunks)); } });
      res.on('error', fail);
    });
    req.on('timeout', () => { req.destroy(); fail(new Error('timeout')); });
    req.on('error', fail);
    if (signal) signal.addEventListener('abort', () => { req.destroy(); fail(new Error('aborted')); });
    setTimeout(() => { req.destroy(); fail(new Error('timeout')); }, timeoutMs + 1000).unref();   // hard cap even if the socket stalls
  });
}

// -> { status: 'available' | 'latest' | 'error', info?, error? }
async function checkForUpdate(current, opts = {}) {
  try {
    const buf = await getBuffer(opts.url || VERSION_URL, { timeoutMs: opts.timeoutMs || 6000 });
    const raw = JSON.parse(buf.toString('utf8'));
    const platform = opts.platform || process.platform;
    if (platform === 'darwin' && raw && typeof raw === 'object' && !raw.mac) return { status: 'latest' };   // no Mac build published yet
    const info = parseInfo(raw, { allowLocal: !!opts.allowLocal, platform });
    if (!info) return { status: 'error', error: 'bad update info' };
    return cmpVersion(info.version, current) > 0 ? { status: 'available', info } : { status: 'latest', info };
  } catch (e) { return { status: 'error', error: String((e && e.message) || e) }; }
}

// Auto checks: on, and not more than once per 24 hours.
function shouldAutoCheck(settings, now = Date.now()) {
  if (settings.autoUpdateCheck === false) return false;
  const last = Number(settings.lastUpdateCheck) || 0;
  return !(last && now - last < DAY_MS && now >= last);
}

// After any check, failed or not, note the time: an offline or broken check waits 24 hours like a good one instead of retrying every launch.
function recordCheck(settings, now = Date.now()) { settings.lastUpdateCheck = now; return settings; }

// Download the installer to destDir, check sha256 (and size when the info has one), return the file path.
function downloadInstaller(info, destDir, onProgress, signal, redirects = 5, opts = {}) {
  return new Promise((resolve, reject) => {
    if (!info || !info.installer) return reject(new Error('No installer link.'));
    if (!info.sha256) return reject(new Error('No checksum to verify the installer.'));
    fs.mkdirSync(destDir, { recursive: true });
    const dest = path.join(destDir, opts.ext === '.dmg' ? `Yoink-${info.version}-mac.dmg` : `Yoink-Setup-${info.version}.exe`);
    const part = dest + '.part';
    const hash = crypto.createHash('sha256');
    let settled = false;
    const fail = (e) => { if (settled) return; settled = true; try { fs.unlinkSync(part); } catch (_) {} reject(e instanceof Error ? e : new Error(String(e))); };
    const go = (url, left) => {
      const lib = /^http:/i.test(url) ? http : https;
      const req = lib.get(url, { timeout: 20000 }, (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && left > 0) {
          res.resume(); return go(new URL(res.headers.location, url).toString(), left - 1);
        }
        if (res.statusCode !== 200) { res.resume(); return fail(new Error('Download failed (HTTP ' + res.statusCode + ').')); }
        const total = Number(res.headers['content-length']) || info.size || 0;
        let got = 0;
        const out = fs.createWriteStream(part);
        res.on('data', (b) => { got += b.length; hash.update(b); if (onProgress) onProgress(got, total); });
        res.pipe(out);
        res.on('error', fail); out.on('error', fail);
        out.on('finish', () => out.close(() => {
          if (settled) return;
          if (info.size && got !== info.size) return fail(new Error('Download was cut off or the wrong size.'));
          if (hash.digest('hex') !== info.sha256) return fail(new Error('The download did not match its checksum.'));
          try { fs.renameSync(part, dest); } catch (e) { return fail(e); }
          settled = true; resolve(dest);
        }));
      });
      req.on('timeout', () => { req.destroy(); fail(new Error('Download timed out.')); });
      req.on('error', fail);
      if (signal) signal.addEventListener('abort', () => { req.destroy(); fail(new Error('Cancelled.')); }, { once: true });
    };
    go(info.installer, redirects);
  });
}

// Start the installer so it outlives this process. The caller quits the app right after.
// Windows: runs the NSIS installer with --updated.
// macOS: see macInstallScript. It waits for Yoink to quit, copies the new Yoink.app over the old one, then reopens it.
function launchInstaller(file, opts = {}) {
  const platform = opts.platform || process.platform;
  // re-hash right before running: the file sat on disk since download and could have been swapped
  const want = String(opts.sha256 || '').toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(want)) throw new Error('No checksum to verify the installer.');
  if (crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') !== want) throw new Error('The installer changed after download; not running it.');
  if (platform === 'darwin') return launchMacInstaller(file, opts);
  const child = spawn(file, ['--updated'], { detached: true, stdio: 'ignore', windowsHide: false });
  child.on('error', () => {});
  child.unref();
  return true;
}

const shq = (s) => "'" + String(s).replace(/'/g, "'\\''") + "'";

// The shell script that swaps the app after Yoink quits. Exported so a test can run it with stand-in commands.
// dmg: downloaded .dmg   appPath: the running /Applications/Yoink.app   pid: this process   log: file for the script's output
function macInstallScript({ dmg, appPath, pid, mountPoint }) {
  const name = path.basename(appPath);
  return `#!/bin/bash
# Yoink self-update (macOS). Written by Yoink; safe to delete.
DMG=${shq(dmg)}; APP=${shq(appPath)}; PID=${Number(pid) || 0}; MNT=${shq(mountPoint)}; NAME=${shq(name)}
# 1) wait up to 30 s for the running Yoink to quit
for i in $(seq 1 60); do kill -0 "$PID" 2>/dev/null || break; sleep 0.5; done
mkdir -p "$MNT" || exit 1
# 2) mount the disk image without showing it in Finder
hdiutil attach "$DMG" -nobrowse -readonly -noverify -mountpoint "$MNT" >/dev/null 2>&1 || { open "$DMG"; exit 1; }
NEW="$MNT/$NAME"
if [ -d "$NEW" ]; then
  # 3) swap: copy next to the old app first, then move into place, so a failed copy never leaves a broken app
  PARENT="$(dirname "$APP")"; STAGE="$PARENT/.Yoink-update.app"
  rm -rf "$STAGE" 2>/dev/null
  if ditto "$NEW" "$STAGE" 2>/dev/null; then
    xattr -dr com.apple.quarantine "$STAGE" 2>/dev/null
    rm -rf "$APP.old" 2>/dev/null
    if mv "$APP" "$APP.old" 2>/dev/null && mv "$STAGE" "$APP" 2>/dev/null; then rm -rf "$APP.old"; OK=1
    else [ -d "$APP.old" ] && [ ! -d "$APP" ] && mv "$APP.old" "$APP"; rm -rf "$STAGE"; fi
  else rm -rf "$STAGE"; fi
fi
hdiutil detach "$MNT" -force >/dev/null 2>&1
if [ "$OK" = 1 ]; then rm -f "$DMG"; open "$APP"
else open "$DMG"; fi   # could not replace the app (for example no write access): show the disk image so the user can drag it over
rm -f "$0"
`;
}

function launchMacInstaller(dmg, opts = {}) {
  const appPath = opts.appPath || '';
  if (!appPath) {   // not running from an .app (for example during development): just show the disk image
    const c = spawn('/usr/bin/open', [dmg], { detached: true, stdio: 'ignore' }); c.on('error', () => {}); c.unref(); return true;
  }
  const dir = path.dirname(dmg);
  const script = path.join(dir, 'yoink-update.sh');
  fs.writeFileSync(script, macInstallScript({ dmg, appPath, pid: process.pid, mountPoint: path.join(os.tmpdir(), 'yoink-update-mnt-' + process.pid) }), { mode: 0o755 });
  const child = spawn('/bin/bash', [script], { detached: true, stdio: 'ignore' });
  child.on('error', () => {});
  child.unref();
  return true;
}

// <anything>/Yoink.app/Contents/MacOS/Yoink -> <anything>/Yoink.app, or '' when not running from an .app bundle (for example npm start)
function macAppPath(exePath) {
  const m = /^(.*?\.app)\/Contents\/MacOS\//.exec(String(exePath || ''));
  return m ? m[1] : '';
}

module.exports = { macInstallScript, macAppPath, VERSION_URL, DAY_MS, cmpVersion, parseInfo, checkForUpdate, shouldAutoCheck, recordCheck, downloadInstaller, launchInstaller };
