'use strict';
// "Clean up": isolate the vocal from a clip's source audio, fully offline.
// Model: HT-Demucs FT vocals specialist (Meta Demucs, MIT) exported to ONNX by StemSplit (MIT), run with onnxruntime-node (MIT).
// The model file is downloaded once on first use (166 MB) and checked against a pinned sha256.
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const https = require('https');
const crypto = require('crypto');

const MODEL = {
  file: 'htdemucs_ft_vocals_fp16weights.onnx',
  tag: 'htdemucs-ft-vocals-fp16-v1',
  size: 165612636,
  sha256: '0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce',
  urls: [
    'https://huggingface.co/StemSplitio/htdemucs-ft-vocals-onnx/resolve/2ef0d757d3e226d0da85fb8c71514f464fcabdd0/htdemucs_ft_vocals_fp16weights.onnx',
    'https://cmplxty.music/models/htdemucs_ft_vocals_fp16weights.onnx',   // optional mirror, only used if the first link fails
  ],
};
const SR = 44100, SEG = 343980, OVERLAP = SEG >> 2, STRIDE = SEG - OVERLAP, VOCALS = 3;   // stems: drums, bass, other, vocals

// ---------- tiny WAV reader / writer (16-bit PCM or float32; the app's wide files are 44.1 kHz 16-bit stereo) ----------
function readWav(file) {
  const b = fs.readFileSync(file);
  if (b.length < 44 || b.toString('ascii', 0, 4) !== 'RIFF' || b.toString('ascii', 8, 12) !== 'WAVE') throw new Error('Not a WAV file.');
  let p = 12, fmt = null, data = null;
  while (p + 8 <= b.length) {
    const id = b.toString('ascii', p, p + 4); let n = b.readUInt32LE(p + 4);
    const body = p + 8;
    if (id === 'fmt ') fmt = { tag: b.readUInt16LE(body), ch: b.readUInt16LE(body + 2), rate: b.readUInt32LE(body + 4), bits: b.readUInt16LE(body + 14) };
    else if (id === 'data') { if (body + n > b.length) n = b.length - body; data = b.subarray(body, body + n); break; }
    p = body + n + (n & 1);
  }
  if (!fmt || !data) throw new Error('Unreadable WAV file.');
  if (fmt.tag === 0xFFFE) fmt.tag = fmt.bits === 32 ? 3 : 1;
  if (!((fmt.tag === 1 && fmt.bits === 16) || (fmt.tag === 3 && fmt.bits === 32))) throw new Error('Unsupported WAV format.');
  const bps = fmt.bits / 8, frames = Math.floor(data.length / (bps * fmt.ch));
  const chans = [];
  for (let c = 0; c < fmt.ch; c++) chans.push(new Float32Array(frames));
  for (let i = 0; i < frames; i++) for (let c = 0; c < fmt.ch; c++) {
    const o = (i * fmt.ch + c) * bps;
    chans[c][i] = fmt.bits === 16 ? data.readInt16LE(o) / 32768 : data.readFloatLE(o);
  }
  return { rate: fmt.rate, channels: chans };
}
function writeWav(file, L, R, rate = SR) {
  const n = L.length, buf = Buffer.alloc(44 + n * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 4, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22); buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34);
  buf.write('data', 36); buf.writeUInt32LE(n * 4, 40);
  const q = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32768)));
  for (let i = 0; i < n; i++) { buf.writeInt16LE(q(L[i]), 44 + i * 4); buf.writeInt16LE(q(R[i]), 46 + i * 4); }
  const tmp = file + '.tmp'; fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(tmp, buf); fs.renameSync(tmp, file);
}

// ---------- ONNX runtime loading ----------
function loadOrt() {
  const cands = [];
  if (process.env.YOINK_ORT_DIR) cands.push(process.env.YOINK_ORT_DIR);
  if (process.resourcesPath) {
    // macOS: one folder per CPU (Apple chips use onnxruntime-node 1.31, Intel Macs 1.23 because newer releases dropped Intel). Windows: ort/node_modules.
    cands.push(path.join(process.resourcesPath, 'ort', process.arch, 'node_modules', 'onnxruntime-node'));
    cands.push(path.join(process.resourcesPath, 'ort', 'node_modules', 'onnxruntime-node'));
  }
  cands.push('onnxruntime-node');
  let last;
  for (const c of cands) { try { return require(c); } catch (e) { last = e; } }
  throw new Error('The audio engine could not be loaded. ' + (last && last.message ? last.message.split('\n')[0] : ''));
}

// ---------- model download ----------
function fetchTo(url, dest, onProgress, signal, redirects = 6) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256'); let settled = false, req = null;
    const fail = (e) => { if (settled) return; settled = true; try { req && req.destroy(); } catch (_) {} try { fs.unlinkSync(dest); } catch (_) {} reject(e instanceof Error ? e : new Error(String(e))); };
    if (signal && signal.aborted) return fail(new Error('Cancelled.'));
    const onAbort = () => fail(new Error('Cancelled.'));
    if (signal) signal.addEventListener('abort', onAbort, { once: true });
    const go = (u, left) => {
      const lib = /^http:/i.test(u) ? http : https;
      req = lib.get(u, { headers: { 'User-Agent': 'Yoink' }, timeout: 30000 }, (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && left > 0) { res.resume(); return go(new URL(res.headers.location, u).toString(), left - 1); }
        if (res.statusCode !== 200) { res.resume(); return fail(new Error('Download failed (HTTP ' + res.statusCode + ').')); }
        const total = Number(res.headers['content-length']) || 0; let got = 0;
        const out = fs.createWriteStream(dest);
        res.on('data', (b) => { got += b.length; hash.update(b); if (onProgress) onProgress(got, total); });
        res.pipe(out); res.on('error', fail); out.on('error', fail);
        res.on('aborted', () => fail(new Error('Download was cut off.')));
        out.on('finish', () => out.close(() => { if (settled) return; if (total && got < total) return fail(new Error('Download was cut off.')); settled = true; if (signal) signal.removeEventListener('abort', onAbort); resolve({ sha256: hash.digest('hex'), bytes: got }); }));
      });
      req.on('timeout', () => fail(new Error('Download timed out.')));
      req.on('error', fail);
    };
    go(url, redirects);
  });
}

class VocalCleaner {
  // opts: modelsDir, cacheDir, model (override for tests), ort (override), idleMs
  constructor(opts) {
    this.modelsDir = opts.modelsDir; this.cacheDir = opts.cacheDir;
    this.model = Object.assign({}, MODEL, opts.model || {});
    this.ortOverride = opts.ort || null; this.idleMs = opts.idleMs != null ? opts.idleMs : 90000;
    this.session = null; this.ort = null; this.sessionP = null; this.idleTimer = null; this.queue = Promise.resolve();
  }
  modelPath() { return path.join(this.modelsDir, this.model.file); }
  modelReady() { try { return fs.statSync(this.modelPath()).size === this.model.size; } catch (_) { return false; } }
  status() { return { ready: this.modelReady(), modelBytes: this.model.size, modelName: 'HT-Demucs FT vocals (MIT)' }; }
  removeModel() { this.release(); try { fs.unlinkSync(this.modelPath()); } catch (_) {} }

  async ensureModel(onProgress, signal) {
    if (this.modelReady()) return;
    fs.mkdirSync(this.modelsDir, { recursive: true });
    const part = this.modelPath() + '.part'; let lastErr = null;
    for (const url of this.model.urls) {
      try {
        const r = await fetchTo(url, part, (got, total) => onProgress && onProgress(got, total || this.model.size), signal);
        if (r.bytes !== this.model.size || r.sha256 !== this.model.sha256) { try { fs.unlinkSync(part); } catch (_) {} throw new Error('The downloaded model did not match its checksum.'); }
        fs.renameSync(part, this.modelPath());
        return;
      } catch (e) { lastErr = e; if (/Cancelled/.test(e.message)) throw e; }
    }
    throw new Error('Could not download the vocal model. ' + (lastErr ? lastErr.message : '') + ' Check your internet connection and try again.');
  }

  release() { clearTimeout(this.idleTimer); const s = this.session; this.session = null; this.sessionP = null; if (s && s.release) { try { s.release(); } catch (_) {} } }
  touch() { clearTimeout(this.idleTimer); if (this.idleMs > 0) { this.idleTimer = setTimeout(() => this.release(), this.idleMs); if (this.idleTimer.unref) this.idleTimer.unref(); } }

  async getSession() {
    if (this.session) return this.session;
    if (!this.sessionP) {
      this.sessionP = (async () => {
        this.ort = this.ortOverride || loadOrt();
        const threads = Math.max(2, Math.min(8, os.cpus().length - 1));
        // 'disabled' graph optimisation is both faster to load and keeps memory near 1 GB (the default optimiser peaks near 6 GB on this model).
        const s = await this.ort.InferenceSession.create(this.modelPath(), { executionProviders: ['cpu'], graphOptimizationLevel: 'disabled', enableCpuMemArena: false, enableMemPattern: false, intraOpNumThreads: threads });
        this.session = s; return s;
      })().catch((e) => { this.sessionP = null; throw e; });
    }
    return this.sessionP;
  }

  // Core separation on raw float channels. Returns {L, R} of the vocal stem.
  async separate(L, R, onProgress, signal) {
    const sess = await this.getSession();
    const total = L.length, nChunks = Math.max(1, Math.ceil(total / STRIDE));
    const outL = new Float32Array(total), outR = new Float32Array(total), wsum = new Float32Array(total);
    const win = new Float32Array(SEG).fill(1);
    for (let i = 0; i < OVERLAP; i++) { const f = i / (OVERLAP - 1); win[i] = f; win[SEG - 1 - i] = f; }
    for (let i = 0; i < nChunks; i++) {
      if (signal && signal.aborted) throw new Error('Cancelled.');
      const start = i * STRIDE, end = Math.min(start + SEG, total), n = end - start;
      const x = new Float32Array(2 * SEG);
      x.set(L.subarray(start, end), 0); x.set(R.subarray(start, end), SEG);
      const feeds = { mix: new this.ort.Tensor('float32', x, [1, 2, SEG]) };
      const res = await sess.run(feeds);
      const st = res.stems.data;                                     // [1, 4, 2, SEG]
      const offL = (VOCALS * 2) * SEG, offR = (VOCALS * 2 + 1) * SEG;
      for (let k = 0; k < n; k++) {
        // the first chunk has no neighbour before it and the last none after it, so they keep full weight at the clip edges
        let w = win[k]; if (i === 0 && k < OVERLAP) w = 1; if (i === nChunks - 1 && k >= SEG - OVERLAP) w = 1;
        outL[start + k] += st[offL + k] * w; outR[start + k] += st[offR + k] * w; wsum[start + k] += w;
      }
      if (onProgress) onProgress((i + 1) / nChunks, i + 1, nChunks);
    }
    for (let k = 0; k < total; k++) { const w = wsum[k] > 1e-8 ? wsum[k] : 1; outL[k] /= w; outR[k] /= w; }
    return { L: outL, R: outR };
  }

  cacheKey(file) {
    const h = crypto.createHash('sha1'); h.update(this.model.tag); h.update(fs.readFileSync(file)); return h.digest('hex').slice(0, 20);
  }
  cachedFile(file) { const f = path.join(this.cacheDir, this.cacheKey(file) + '.wav'); return fs.existsSync(f) && fs.statSync(f).size > 1000 ? f : null; }

  // wideFile: 44.1 kHz WAV. job: {onProgress(phase, pct, text), signal}. One job at a time; the rest wait their turn.
  clean(wideFile, job = {}) {
    const run = this.queue.then(() => this._clean(wideFile, job), () => this._clean(wideFile, job));
    this.queue = run.catch(() => {});
    return run;
  }
  async _clean(wideFile, { onProgress, signal } = {}) {
    const say = (phase, pct, text) => { if (onProgress) onProgress(phase, pct, text); };
    if (signal && signal.aborted) throw new Error('Cancelled.');
    const hit = this.cachedFile(wideFile);
    if (hit) { say('done', 100, 'Done.'); return { file: hit, cached: true }; }
    if (!this.modelReady()) {
      say('download', 0, 'Downloading the vocal model (one time, ' + Math.round(this.model.size / 1048576) + ' MB)...');
      await this.ensureModel((got, total) => say('download', Math.min(99, Math.round(got / total * 100)), `Downloading the vocal model: ${(got / 1048576).toFixed(0)} of ${(total / 1048576).toFixed(0)} MB`), signal);
    }
    const wav = readWav(wideFile);
    if (wav.rate !== SR) throw new Error('This audio is not 44.1 kHz.');
    const L = wav.channels[0], R = wav.channels[1] || wav.channels[0];
    say('load', 0, this.session ? 'Starting...' : 'Loading the model...');
    await this.getSession();
    const t0 = Date.now();
    try {
      const v = await this.separate(L, R, (f, i, n) => say('run', Math.round(f * 100), `Isolating the vocal... ${i} of ${n}`), signal);
      const out = path.join(this.cacheDir, this.cacheKey(wideFile) + '.wav');
      writeWav(out, v.L, v.R);
      say('done', 100, 'Done.');
      return { file: out, cached: false, ms: Date.now() - t0 };
    } finally { this.touch(); }
  }
}

module.exports = { VocalCleaner, MODEL, SR, SEG, readWav, writeWav, loadOrt };
