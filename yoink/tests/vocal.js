'use strict';
// Vocal cleaner plumbing with a fake model and a fake onnxruntime: WAV IO, download + checksum, progress, cancel, cache, queue.
const assert = require('assert'), fs = require('fs'), os = require('os'), path = require('path'), http = require('http'), crypto = require('crypto');
const { VocalCleaner, SR, SEG, readWav, writeWav } = require('../src/vocal');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-vocal-'));
const fake = crypto.randomBytes(5000);
const sha = crypto.createHash('sha256').update(fake).digest('hex');
let runs = 0, delay = 0;
const ort = {
  Tensor: class { constructor(t, d, s) { this.type = t; this.data = d; this.dims = s; } },
  InferenceSession: { create: async () => ({ release() {}, run: async (f) => {
    runs++; if (delay) await new Promise((r) => setTimeout(r, delay));
    const x = f.mix.data, st = new Float32Array(4 * 2 * SEG); for (let i = 0; i < 2 * SEG; i++) st[3 * 2 * SEG + i] = x[i] * 0.5; return { stems: { data: st } };
  } }) },
};
let hits = 0;
const srv = http.createServer((rq, rs) => { hits++; if (rq.url === '/bad') { rs.writeHead(200, { 'Content-Length': 5000 }); return rs.end(crypto.randomBytes(5000)); } if (rq.url === '/slow') { rs.writeHead(200, { 'Content-Length': 5000 }); rs.write(fake.subarray(0, 100)); return; } rs.writeHead(200, { 'Content-Length': fake.length }); rs.end(fake); });

(async () => {
  await new Promise((r) => srv.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + srv.address().port;
  // wav io
  const n = SR * 3, L = new Float32Array(n), R = new Float32Array(n); for (let i = 0; i < n; i++) { L[i] = 0.5 * Math.sin(i * 0.05); R[i] = 0.25 * Math.sin(i * 0.03); }
  const wide = path.join(tmp, 'wide.wav'); writeWav(wide, L, R);
  const w = readWav(wide); assert.strictEqual(w.rate, SR); assert.strictEqual(w.channels.length, 2); assert.ok(Math.abs(w.channels[0][1000] - L[1000]) < 1e-3, 'wav round trip');

  const mk = (urls, extra) => new VocalCleaner(Object.assign({ modelsDir: path.join(tmp, 'models'), cacheDir: path.join(tmp, 'cache'), ort, idleMs: 0, model: { file: 'm.onnx', size: fake.length, sha256: sha, urls, tag: 'test' } }, extra || {}));
  // bad checksum, then the good mirror
  let v = mk([base + '/bad', base + '/good']); assert.ok(!v.modelReady());
  const prog = []; const r1 = await v.clean(wide, { onProgress: (ph, p) => prog.push(ph + ':' + p) });
  assert.ok(v.modelReady(), 'model downloaded after skipping the bad mirror'); assert.ok(!fs.existsSync(v.modelPath() + '.part'), 'no .part left');
  assert.ok(prog.some((p) => p.startsWith('download:')) && prog.some((p) => p.startsWith('run:')) && prog[prog.length - 1] === 'done:100', 'progress phases: ' + prog.join(' '));
  const o = readWav(r1.file); assert.strictEqual(o.channels[0].length, n, 'same length'); assert.ok(Math.abs(o.channels[0][5000] - L[5000] * 0.5) < 2e-3, 'vocal stem content is used (fake model halves it)');
  // cache
  const before = runs; const r2 = await v.clean(wide); assert.ok(r2.cached && r2.file === r1.file && runs === before, 'second request is served from the cache');
  // only bad mirror: clear error, no model
  const v2 = mk([base + '/bad'], { modelsDir: path.join(tmp, 'm2'), cacheDir: path.join(tmp, 'c2') }); await assert.rejects(v2.clean(wide), /checksum|download/i); assert.ok(!v2.modelReady());
  // cancel during download
  const v3 = mk([base + '/slow'], { modelsDir: path.join(tmp, 'm3'), cacheDir: path.join(tmp, 'c3') }); const ac = new AbortController(); const p3 = v3.clean(wide, { signal: ac.signal }); setTimeout(() => ac.abort(), 300);
  await assert.rejects(p3, /Cancel/i); assert.ok(!v3.modelReady());
  // cancel during run: long clip, many chunks
  const m = SR * 40, L2 = new Float32Array(m).fill(0.1); const long = path.join(tmp, 'long.wav'); writeWav(long, L2, L2);
  delay = 40; const ac2 = new AbortController(); const seen = []; const p4 = v.clean(long, { signal: ac2.signal, onProgress: (ph, p) => { if (ph === 'run') { seen.push(p); if (p >= 10) ac2.abort(); } } });
  await assert.rejects(p4, /Cancel/i); assert.ok(seen.length < 10, 'stopped early'); assert.ok(!v.cachedFile(long), 'a cancelled job leaves no cache entry'); delay = 0;
  // queue: two jobs in order, a failed one does not block the next
  const a = v.clean(path.join(tmp, 'missing.wav')).catch((e) => e); const b = v.clean(long); assert.ok((await a) instanceof Error); assert.ok((await b).file);
  // remove model
  v.removeModel(); assert.ok(!v.modelReady());
  srv.close(); srv.closeAllConnections && srv.closeAllConnections(); fs.rmSync(tmp, { recursive: true, force: true });
  console.log('vocal tests passed');
})().catch((e) => { console.error(e); process.exit(1); });
