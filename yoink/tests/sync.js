'use strict';
// Merge rules, the demo provider shared by two "PCs", token persistence, and the Google Drive provider against a fake local server.
const assert = require('assert');
const fs = require('fs'), os = require('os'), path = require('path'), http = require('http');
const { mergeItems, pick, TokenStore, SyncService } = require('../src/sync');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-sync-'));
const T = Date.now();

// ---- merge
{
  const m = mergeItems([{ cid: 'a', updatedAt: 5 }, { cid: 'b', updatedAt: 9, title: 'new' }], [{ cid: 'b', updatedAt: 3, title: 'old' }, { cid: 'c', updatedAt: 1 }]);
  assert.deepStrictEqual(m.map((x) => x.cid).sort(), ['a', 'b', 'c'], 'union keeps everything');
  assert.strictEqual(m.find((x) => x.cid === 'b').title, 'new', 'newest wins');
  const d = mergeItems([{ cid: 'a', deleted: true, updatedAt: T }], [{ cid: 'a', updatedAt: T - 1000 }]);
  assert.ok(d[0].deleted, 'newer delete beats older copy');
  const r = mergeItems([{ cid: 'a', updatedAt: T }], [{ cid: 'a', deleted: true, updatedAt: T - 1000 }]);
  assert.ok(!r[0].deleted, 'newer favorite beats older delete');
  assert.strictEqual(mergeItems([{ cid: 'z', deleted: true, updatedAt: T - 200 * 86400000 }], []).length, 0, 'old tombstones are dropped');
  const p = pick({ cid: 'x', file: 'C:\\secret.wav', peaks: [1, 2], title: 't', favoritedAt: 7 });
  assert.ok(!p.file && !p.peaks && p.title === 't' && p.updatedAt === 7, 'pick drops paths and peaks');
}

// ---- a fake "PC": its own favorites store
const xor = { available: () => true, encrypt: (s) => Buffer.from(Buffer.from(s).map((b) => b ^ 42)), decrypt: (b) => Buffer.from(Buffer.from(b).map((x) => x ^ 42)).toString() };
function pc(name, opts = {}) {
  const dir = path.join(tmp, name); fs.mkdirSync(dir, { recursive: true });
  const st = { favs: [], tombs: [], removed: [] };
  const mk = () => new SyncService(Object.assign({
    dir, tokens: new TokenStore(path.join(dir, 'tok.bin'), opts.cipher === undefined ? xor : opts.cipher), config: { showDemo: true }, demoDir: path.join(tmp, 'shared'),
    loadFavs: () => st.favs, saveFavs: (a) => { st.favs = a; }, loadTombs: () => st.tombs, saveTombs: (a) => { st.tombs = a; },
    onRemoved: (r) => st.removed.push(r.cid), openUrl: () => {},
  }, opts.extra || {}));
  return { st, svc: mk(), mk, dir };
}
const fav = (cid, t, extra) => Object.assign({ cid, videoId: 'v' + cid, title: 'T' + cid, favoritedAt: t, updatedAt: t, file: 'C:\\x\\' + cid + '.wav' }, extra || {});

(async () => {
  // ---- demo provider: two PCs, union on first sign-in, deletes propagate, tags propagate
  const A = pc('A'), B = pc('B');
  A.st.favs = [fav('1', T - 5000), fav('2', T - 4000)];
  B.st.favs = [fav('2', T - 4000), fav('3', T - 3000)];
  await A.svc.signIn('demo', 'Sam@example.com');
  assert.strictEqual(A.st.favs.length, 2, 'A keeps its own');
  const rb = await B.svc.signIn('demo', 'sam@example.com');
  assert.ok(rb.signedIn && rb.demo, 'B signed in');
  assert.deepStrictEqual(B.st.favs.map((x) => x.cid).sort(), ['1', '2', '3'], 'B merged (union, nothing lost)');
  assert.ok(B.st.favs.find((x) => x.cid === '1').needsRestore, 'B must restore audio for the new one');
  assert.ok(B.st.favs.find((x) => x.cid === '3').file.includes('3.wav'), 'B keeps its own local file path');
  await A.svc.sync();
  assert.deepStrictEqual(A.st.favs.map((x) => x.cid).sort(), ['1', '2', '3'], 'A got B favorite');
  assert.ok(!A.st.favs.find((x) => x.cid === '1').needsRestore, 'A local copy is not flagged');
  // tag on A, delete on B
  A.st.favs.find((x) => x.cid === '2').tags = ['vox']; A.st.favs.find((x) => x.cid === '2').updatedAt = Date.now();
  await A.svc.sync(); await B.svc.sync();
  assert.deepStrictEqual(B.st.favs.find((x) => x.cid === '2').tags, ['vox'], 'tags travel');
  assert.ok(!B.st.favs.find((x) => x.cid === '2').file === false, 'B local file untouched by tag update');
  B.st.favs = B.st.favs.filter((x) => x.cid !== '3'); B.svc.recordRemoved('3');
  await B.svc.sync(); await A.svc.sync();
  assert.deepStrictEqual(A.st.favs.map((x) => x.cid).sort(), ['1', '2'], 'delete propagates');
  assert.deepStrictEqual(A.st.removed, ['3']);
  // other account does not see it
  const C = pc('C'); await C.svc.signIn('demo', 'other@example.com'); assert.strictEqual(C.st.favs.length, 0, 'accounts are separate');
  // guest: no tombstones
  const G = pc('G'); G.svc.recordRemoved('9'); assert.strictEqual(G.st.tombs.length, 0, 'guest records nothing');
  // stays signed in across launches, then sign out
  const A2 = A.mk(); assert.ok(A2.status().signedIn && A2.status().label === 'Sam@example.com', 'signed in after restart');
  assert.ok(!fs.readFileSync(path.join(A.dir, 'tok.bin')).toString().includes('demo-'), 'token file is not plain text');
  A2.signOut(); assert.ok(!A.mk().status().signedIn, 'signed out stays out');
  // no encryption available: works for the session only
  const N = pc('N', { cipher: { available: () => false } }); await N.svc.signIn('demo', 'n@x.com');
  assert.ok(N.svc.status().signedIn && !N.svc.status().persistent && !N.mk().status().signedIn, 'no safeStorage: session only');
  // nothing offered
  assert.ok(!pc('Z', { extra: { config: { showDemo: false } } }).svc.anyProvider(), 'no provider, no prompt');

  // ---- Google provider against a fake server
  const files = {}; let tokenCalls = 0, authSeen = null, nextId = 1; let revoked = false;
  const srv = http.createServer((rq, rs) => {
    const u = new URL(rq.url, 'http://x'); const chunks = []; rq.on('data', (b) => chunks.push(b)); rq.on('end', () => {
      const body = Buffer.concat(chunks).toString(); const send = (c, o, h) => { rs.writeHead(c, Object.assign({ 'Content-Type': 'application/json' }, h || {})); rs.end(typeof o === 'string' ? o : JSON.stringify(o)); };
      if (u.pathname === '/auth') { authSeen = u.searchParams; return send(302, '', { Location: u.searchParams.get('redirect_uri') + '/?code=C0DE&state=' + u.searchParams.get('state') }); }
      if (u.pathname === '/token') {
        tokenCalls++; const p = new URLSearchParams(body);
        if (p.get('grant_type') === 'authorization_code') { assert.ok(p.get('code_verifier') && p.get('code') === 'C0DE'); return send(200, { access_token: 'AT1', refresh_token: 'RT1', expires_in: 3600 }); }
        if (revoked || p.get('refresh_token') !== 'RT1') return send(400, { error: 'invalid_grant' });
        return send(200, { access_token: 'AT2', expires_in: 3600 });
      }
      if (u.pathname === '/userinfo') return send(200, { email: 'sam@gmail.test' });
      if (!/^Bearer AT/.test(rq.headers.authorization || '')) return send(401, {});
      if (u.pathname === '/files' && rq.method === 'GET') { assert.ok((u.searchParams.get('spaces') || '') === 'appDataFolder'); return send(200, { files: Object.keys(files).map((id) => ({ id })) }); }
      if (u.pathname.startsWith('/files/') && u.searchParams.get('alt') === 'media') return send(200, files[u.pathname.split('/')[2]] || '{}');
      if (u.pathname === '/upload' && rq.method === 'POST') { assert.ok(body.includes('appDataFolder')); const id = 'F' + nextId++; files[id] = body.split('\r\n\r\n')[2].split('\r\n--')[0]; return send(200, { id }); }
      if (u.pathname.startsWith('/upload/') && rq.method === 'PATCH') { files[u.pathname.split('/')[2]] = body; return send(200, {}); }
      send(404, {});
    });
  });
  await new Promise((r) => srv.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + srv.address().port;
  const endpoints = { auth: base + '/auth', token: base + '/token', userinfo: base + '/userinfo', files: base + '/files', upload: base + '/upload' };
  const openUrl = (url) => { http.get(url, (r) => { const loc = r.headers.location; r.resume(); if (loc) http.get(loc, (x) => x.resume()); }); };
  const cfg = { googleClientId: 'cid.apps.test', showDemo: false };
  const P1 = pc('P1', { extra: { config: cfg, endpoints, openUrl } }), P2 = pc('P2', { extra: { config: cfg, endpoints, openUrl } });
  P1.st.favs = [fav('g1', T - 2000)]; P2.st.favs = [fav('g2', T - 1000)];
  const s1 = await P1.svc.signIn('google'); assert.ok(s1.signedIn && s1.label === 'sam@gmail.test', 'google sign-in');
  assert.strictEqual(authSeen.get('code_challenge_method'), 'S256'); assert.ok(/drive\.appdata/.test(authSeen.get('scope')) && authSeen.get('state'), 'PKCE and the narrow scope');
  assert.strictEqual(Object.keys(files).length, 1, 'one hidden file created');
  await P2.svc.signIn('google');
  assert.deepStrictEqual(P2.st.favs.map((x) => x.cid).sort(), ['g1', 'g2'], 'cross-PC merge through Drive');
  assert.strictEqual(Object.keys(files).length, 1, 'still one file');
  assert.ok(!/C:\\\\/.test(Object.values(files)[0]), 'no local paths uploaded');
  await P1.svc.sync(); assert.strictEqual(P1.st.favs.length, 2);
  const P1b = P1.mk(); await P1b.sync(); assert.ok(P1b.status().signedIn && !P1b.status().error, 'refresh token restores the session');
  assert.ok(tokenCalls >= 3);
  revoked = true; const P1c = P1.mk(); await P1c.sync().catch(() => {}); assert.ok(!P1c.status().signedIn && /expired/.test(P1c.status().error), 'revoked token signs out with a clear message');
  srv.close();
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log('sync tests passed');
})().catch((e) => { console.error(e); process.exit(1); });
