'use strict';
// Optional sign-in and favorites sync. No Electron dependency, so it is tested from the command line.
// Providers: "demo" (a JSON file on this PC, so the flow can be shown; it does NOT sync between PCs unless the folder is shared)
//            "google" (the user's own Google Drive, hidden appDataFolder, needs a Google OAuth client id from the app's owner).
// Merge rule: per favorite (cid) the newest updatedAt wins; a delete is a tombstone with its own updatedAt. First sign-in is a union.
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const crypto = require('crypto');

// Only these fields ever leave the PC. No file paths, no waveform peaks.
const FIELDS = ['cid', 'videoId', 'title', 'channel', 'url', 'category', 'start', 'end', 'winStart', 'wideDur', 'trimStart', 'trimEnd', 'hitStart', 'hitEnd',
  'hiFrom', 'hiTo', 'words', 'views', 'uploaded', 'license', 'approx', 'phrase', 'format', 'favoritedAt', 'tags', 'updatedAt'];
const TOMB_MS = 90 * 24 * 3600 * 1000;

function pick(c) { const o = {}; for (const f of FIELDS) if (c[f] !== undefined) o[f] = c[f]; if (!o.updatedAt) o.updatedAt = c.favoritedAt || 0; return o; }
const sameItem = (a, b) => JSON.stringify(a, Object.keys(a).sort()) === JSON.stringify(b, Object.keys(b).sort());

// local, remote: arrays of items ({cid, ..., updatedAt} or {cid, deleted:true, updatedAt}). Returns the merged array.
function mergeItems(local, remote, now = Date.now()) {
  const m = new Map();
  for (const it of [...(remote || []), ...(local || [])]) {                       // local second: it wins ties
    if (!it || typeof it.cid !== 'string' || !it.cid) continue;
    const cur = m.get(it.cid);
    if (!cur || (it.updatedAt || 0) >= (cur.updatedAt || 0)) m.set(it.cid, it);
  }
  return [...m.values()].filter((it) => !(it.deleted && now - (it.updatedAt || 0) > TOMB_MS));
}

// ---------- tiny HTTP helper ----------
function request(url, { method = 'GET', headers = {}, body, timeoutMs = 20000 } = {}) {
  return new Promise((resolve, reject) => {
    const u = new URL(url); const lib = u.protocol === 'http:' ? http : https;
    const req = lib.request(u, { method, headers, timeout: timeoutMs }, (res) => {
      const ch = []; res.on('data', (b) => ch.push(b));
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(ch).toString('utf8') }));
      res.on('error', reject);
    });
    req.on('timeout', () => { req.destroy(); reject(new Error('The sync server did not answer.')); });
    req.on('error', reject);
    if (body) req.write(body);
    req.end();
  });
}
const form = (o) => Object.entries(o).map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&');

// ---------- providers ----------
class DemoProvider {
  constructor(dir, account) { this.id = 'demo'; this.file = path.join(dir, 'remote-' + crypto.createHash('sha1').update(account.id).digest('hex').slice(0, 12) + '.json'); }
  static signIn(dir, label) {
    label = String(label || '').trim().slice(0, 60);
    if (!label) throw new Error('Type a name or email for the demo account.');
    return { account: { id: label.toLowerCase(), label }, refreshToken: 'demo-' + crypto.randomBytes(8).toString('hex') };
  }
  async pull() { try { const j = JSON.parse(fs.readFileSync(this.file, 'utf8')); return Array.isArray(j.items) ? j.items : []; } catch (_) { return []; } }
  async push(items) { fs.mkdirSync(path.dirname(this.file), { recursive: true }); const t = this.file + '.tmp'; fs.writeFileSync(t, JSON.stringify({ v: 1, items })); fs.renameSync(t, this.file); }
}

const GOOGLE = {
  auth: 'https://accounts.google.com/o/oauth2/v2/auth', token: 'https://oauth2.googleapis.com/token', userinfo: 'https://openidconnect.googleapis.com/v1/userinfo',
  files: 'https://www.googleapis.com/drive/v3/files', upload: 'https://www.googleapis.com/upload/drive/v3/files', scope: 'https://www.googleapis.com/auth/drive.appdata email',
};
const DRIVE_NAME = 'yoink-favorites.json';

class GoogleProvider {
  constructor(cfg, auth, endpoints) { this.id = 'google'; this.cfg = cfg; this.auth = auth; this.ep = Object.assign({}, GOOGLE, endpoints || {}); this.access = null; this.expires = 0; this.fileId = null; }

  // Browser sign-in (OAuth for installed apps: PKCE + a one-shot listener on 127.0.0.1). openUrl(url) opens the system browser.
  static async signIn(cfg, openUrl, endpoints, timeoutMs = 300000) {
    const ep = Object.assign({}, GOOGLE, endpoints || {});
    if (!cfg.googleClientId) throw new Error('Google sign-in is not set up in this build.');
    const verifier = crypto.randomBytes(32).toString('base64url'), challenge = crypto.createHash('sha256').update(verifier).digest('base64url'), state = crypto.randomBytes(12).toString('hex');
    const server = http.createServer(); await new Promise((r, j) => { server.once('error', j); server.listen(0, '127.0.0.1', r); });
    const redirect = 'http://127.0.0.1:' + server.address().port;
    try {
      const code = await new Promise((resolve, reject) => {
        const t = setTimeout(() => reject(new Error('Sign-in timed out.')), timeoutMs);
        server.on('request', (rq, rs) => {
          const q = new URL(rq.url, redirect).searchParams;
          rs.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
          if (q.get('state') !== state) { rs.end('<p>Wrong sign-in link. Go back to Yoink and try again.</p>'); return; }
          if (q.get('error') || !q.get('code')) { rs.end('<p>Sign-in was cancelled. You can close this tab.</p>'); clearTimeout(t); reject(new Error('Sign-in was cancelled.')); return; }
          rs.end('<p>Signed in. You can close this tab and go back to Yoink.</p>'); clearTimeout(t); resolve(q.get('code'));
        });
        openUrl(ep.auth + '?' + form({ client_id: cfg.googleClientId, redirect_uri: redirect, response_type: 'code', scope: ep.scope, code_challenge: challenge, code_challenge_method: 'S256', state, access_type: 'offline', prompt: 'consent' }));
      });
      const body = { client_id: cfg.googleClientId, code, code_verifier: verifier, grant_type: 'authorization_code', redirect_uri: redirect }; if (cfg.googleClientSecret) body.client_secret = cfg.googleClientSecret;
      const r = await request(ep.token, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: form(body) });
      const j = JSON.parse(r.body || '{}');
      if (r.status !== 200 || !j.refresh_token) throw new Error('Google did not finish the sign-in.');
      let label = 'Google account';
      try { const u = await request(ep.userinfo, { headers: { Authorization: 'Bearer ' + j.access_token } }); label = JSON.parse(u.body).email || label; } catch (_) {}
      return { account: { id: label.toLowerCase(), label }, refreshToken: j.refresh_token };
    } finally { server.close(); }
  }

  async token() {
    if (this.access && Date.now() < this.expires - 30000) return this.access;
    const body = { client_id: this.cfg.googleClientId, refresh_token: this.auth.refreshToken, grant_type: 'refresh_token' }; if (this.cfg.googleClientSecret) body.client_secret = this.cfg.googleClientSecret;
    const r = await request(this.ep.token, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: form(body) });
    const j = JSON.parse(r.body || '{}');
    if (r.status === 400 || r.status === 401) { const e = new Error('Your Google sign-in expired. Sign in again.'); e.reauth = true; throw e; }
    if (r.status !== 200 || !j.access_token) throw new Error('Could not reach Google.');
    this.access = j.access_token; this.expires = Date.now() + (j.expires_in || 3600) * 1000; return this.access;
  }
  async api(url, opts = {}) { const t = await this.token(); const r = await request(url, Object.assign({}, opts, { headers: Object.assign({ Authorization: 'Bearer ' + t }, opts.headers || {}) })); if (r.status >= 400) throw new Error('Google Drive said ' + r.status + '.'); return r; }
  async find() {
    if (this.fileId) return this.fileId;
    const r = await this.api(this.ep.files + '?' + form({ spaces: 'appDataFolder', q: `name='${DRIVE_NAME}' and trashed=false`, fields: 'files(id)' }));
    const f = (JSON.parse(r.body).files || [])[0]; this.fileId = f ? f.id : null; return this.fileId;
  }
  async pull() {
    const id = await this.find(); if (!id) return [];
    const r = await this.api(this.ep.files + '/' + id + '?alt=media');
    const j = JSON.parse(r.body || '{}'); return Array.isArray(j.items) ? j.items : [];
  }
  async push(items) {
    const data = JSON.stringify({ v: 1, items }); const id = await this.find();
    if (id) { await this.api(this.ep.upload + '/' + id + '?uploadType=media', { method: 'PATCH', headers: { 'Content-Type': 'application/json' }, body: data }); return; }
    const bd = 'yoink' + crypto.randomBytes(6).toString('hex');
    const body = `--${bd}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n${JSON.stringify({ name: DRIVE_NAME, parents: ['appDataFolder'] })}\r\n--${bd}\r\nContent-Type: application/json\r\n\r\n${data}\r\n--${bd}--`;
    const r = await this.api(this.ep.upload + '?uploadType=multipart&fields=id', { method: 'POST', headers: { 'Content-Type': 'multipart/related; boundary=' + bd }, body });
    this.fileId = JSON.parse(r.body).id || null;
  }
}

// ---------- the signed-in token file (encrypted with the OS, Electron safeStorage) ----------
class TokenStore {
  // cipher: { available(): bool, encrypt(str): Buffer, decrypt(Buffer): str }
  constructor(file, cipher) { this.file = file; this.cipher = cipher; }
  persistent() { return !!(this.cipher && this.cipher.available()); }
  save(rec) { if (!this.persistent()) return false; fs.mkdirSync(path.dirname(this.file), { recursive: true }); fs.writeFileSync(this.file, this.cipher.encrypt(JSON.stringify(rec))); return true; }
  load() { try { if (!this.persistent() || !fs.existsSync(this.file)) return null; const j = JSON.parse(this.cipher.decrypt(fs.readFileSync(this.file))); return j && j.provider && j.refreshToken && j.account ? j : null; } catch (_) { return null; } }
  clear() { try { fs.unlinkSync(this.file); } catch (_) {} }
}

// ---------- the service ----------
// o: { dir, tokens: TokenStore, config:{googleClientId,googleClientSecret,showDemo}, loadFavs(), saveFavs(arr), loadTombs(), saveTombs(arr),
//      onRemoved(rec), openUrl(url), endpoints, demoDir, emit(evt) }
class SyncService {
  constructor(o) { this.o = o; this.auth = o.tokens.load(); this.syncing = null; this.lastSync = 0; this.error = ''; this.timer = null; }
  available() { const c = this.o.config || {}; return { google: !!c.googleClientId, demo: c.showDemo !== false }; }
  anyProvider() { const a = this.available(); return a.google || a.demo; }
  status() {
    const a = this.available();
    return { signedIn: !!this.auth, provider: this.auth ? this.auth.provider : '', label: this.auth ? this.auth.account.label : '', demo: !!this.auth && this.auth.provider === 'demo',
      available: a, persistent: this.o.tokens.persistent(), syncing: !!this.syncing, lastSync: this.lastSync, error: this.error };
  }
  provider() {
    if (!this.auth) return null;
    if (this.auth.provider === 'google') return new GoogleProvider(this.o.config, this.auth, this.o.endpoints);
    return new DemoProvider(this.o.demoDir, this.auth.account);
  }
  async signIn(providerId, arg) {
    let r;
    if (providerId === 'demo') { if (!this.available().demo) throw new Error('Not available.'); r = DemoProvider.signIn(this.o.demoDir, arg); }
    else if (providerId === 'google') r = await GoogleProvider.signIn(this.o.config, this.o.openUrl, this.o.endpoints);
    else throw new Error('Unknown sign-in method.');
    this.auth = { provider: providerId, account: r.account, refreshToken: r.refreshToken };
    this.o.tokens.save(this.auth);
    this.o.saveTombs([]);
    this.error = '';
    try { await this.sync(); } catch (_) {}
    return this.status();
  }
  signOut() { this.auth = null; this.o.tokens.clear(); this.o.saveTombs([]); this.error = ''; this.lastSync = 0; return this.status(); }

  // call after a favorite is deleted on this PC; only recorded while signed in
  recordRemoved(cid) { if (!this.auth) return; const t = this.o.loadTombs().filter((x) => x.cid !== cid); t.push({ cid, deleted: true, updatedAt: Date.now() }); this.o.saveTombs(t); }
  schedule(ms = 2500) { if (!this.auth) return; clearTimeout(this.timer); this.timer = setTimeout(() => this.sync().catch(() => {}), ms); if (this.timer.unref) this.timer.unref(); }

  sync() {
    if (!this.auth) return Promise.resolve({ skipped: true });
    if (this.syncing) return this.syncing;
    this.syncing = this._sync().finally(() => { this.syncing = null; this.o.emit && this.o.emit({ type: 'status', status: this.status() }); });
    this.o.emit && this.o.emit({ type: 'status', status: this.status() });
    return this.syncing;
  }
  async _sync() {
    const prov = this.provider();
    try {
      const favs = this.o.loadFavs(), tombs = this.o.loadTombs();
      const local = [...favs.map(pick), ...tombs];
      const remote = await prov.pull();
      const merged = mergeItems(local, remote);
      // apply to this PC
      const byCid = new Map(favs.map((c) => [c.cid, c]));
      let added = 0, removed = 0, changed = 0; const next = [];
      for (const it of merged) {
        const cur = byCid.get(it.cid);
        if (it.deleted) { if (cur) { removed++; this.o.onRemoved && this.o.onRemoved(cur); } continue; }
        if (!cur) { added++; next.push(Object.assign({ file: '', wideFile: '', needsRestore: true }, it)); continue; }
        if ((it.updatedAt || 0) > (cur.updatedAt || cur.favoritedAt || 0)) { changed++; next.push(Object.assign({}, cur, it)); } else next.push(Object.assign({}, cur, { updatedAt: cur.updatedAt || cur.favoritedAt || 0 }));
      }
      next.sort((a, b) => (b.favoritedAt || 0) - (a.favoritedAt || 0));
      if (added || removed || changed || next.some((c, i) => c.updatedAt !== (favs.find((f) => f.cid === c.cid) || {}).updatedAt)) this.o.saveFavs(next);
      this.o.saveTombs(merged.filter((x) => x.deleted));
      // push if the other side is missing something
      const rIdx = new Map(remote.map((x) => [x.cid, x]));
      const needPush = merged.length !== remote.length || merged.some((x) => !rIdx.has(x.cid) || !sameItem(x, rIdx.get(x.cid)));
      if (needPush) await prov.push(merged);
      this.lastSync = Date.now(); this.error = '';
      const res = { added, removed, changed, pushed: needPush, total: next.length };
      this.o.emit && this.o.emit({ type: 'done', result: res });
      return res;
    } catch (e) {
      this.error = e && e.reauth ? e.message : 'Could not sync: ' + ((e && e.message) || e);
      if (e && e.reauth) { this.auth = null; this.o.tokens.clear(); }
      throw e;
    }
  }
}

module.exports = { FIELDS, pick, mergeItems, DemoProvider, GoogleProvider, TokenStore, SyncService, GOOGLE };
