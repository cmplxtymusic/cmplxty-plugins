'use strict';
// Quality + speed benchmark for Clean up. Needs the model file (set YOINK_MODEL_DIR) and a speech WAV (argv[2], 44.1 kHz).
// Builds a synthetic "song" (chords, bass, drums), mixes a clean speech reference into it, separates, and reports SDR.
const fs = require('fs'), path = require('path');
const { VocalCleaner, readWav, writeWav, SR } = require('../src/vocal');
const out = process.argv[3] || '/tmp/vocal-bench';
fs.mkdirSync(out, { recursive: true });
const rss = () => Math.round(process.memoryUsage().rss / 1048576);
let peakRss = 0; const poll = setInterval(() => { peakRss = Math.max(peakRss, rss()); }, 100);

function synthMusic(n, seed = 1) {
  let s = seed; const rnd = () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296) - 0.5;
  const L = new Float32Array(n), R = new Float32Array(n);
  const chords = [[220, 261.6, 329.6], [174.6, 220, 261.6], [130.8, 164.8, 196], [196, 246.9, 293.7]];
  const bpm = 100, beat = 60 / bpm * SR;
  for (let i = 0; i < n; i++) {
    const t = i / SR, bi = Math.floor(i / beat), ch = chords[Math.floor(bi / 4) % 4], ph = (i % beat) / beat;
    let pad = 0; for (const f of ch) { pad += 2 * ((t * f) % 1) - 1; pad += 2 * ((t * f * 1.004) % 1) - 1; }
    pad *= 0.06;
    const bass = Math.sin(2 * Math.PI * ch[0] / 2 * t) * 0.3 * Math.exp(-ph * 1.5);
    const kick = (bi % 2 === 0) ? Math.sin(2 * Math.PI * (50 + 100 * Math.exp(-ph * 30)) * ph * beat / SR) * Math.exp(-ph * 8) * 0.6 : 0;
    const snare = (bi % 2 === 1) ? rnd() * Math.exp(-ph * 10) * 0.5 : 0;
    const hat = (ph % 0.5) < 0.04 ? rnd() * 0.25 : 0;
    const v = pad + bass + kick + snare + hat;
    L[i] = v + pad * 0.1; R[i] = v - pad * 0.1;
  }
  return { L, R };
}
const e = (a) => { let s = 0; for (let i = 0; i < a.length; i++) s += a[i] * a[i]; return s; };
const db = (x) => 10 * Math.log10(Math.max(x, 1e-12));
function sdr(ref, est) { let n = 0; for (let i = 0; i < ref.length; i++) { const d = est[i] - ref[i]; n += d * d; } return db(e(ref) / n); }

(async () => {
  const sp = readWav(process.argv[2]); let vL = sp.channels[0], vR = sp.channels[1] || sp.channels[0];
  // put ~1.5 s of speech inside a 9 s window like a real wide file
  const N = Math.round((+process.env.DUR || 9) * SR), off = Math.round((+process.env.OFF || 3.5) * SR);
  const refL = new Float32Array(N), refR = new Float32Array(N);
  for (let i = 0; i < vL.length && off + i < N; i++) { refL[off + i] = vL[i]; refR[off + i] = vR[i]; }
  let pk = 0; for (let i = 0; i < N; i++) pk = Math.max(pk, Math.abs(refL[i]));
  const g = 0.5 / pk; for (let i = 0; i < N; i++) { refL[i] *= g; refR[i] *= g; }
  let mus = synthMusic(N);
  if (process.env.MUSIC) { const m = readWav(process.env.MUSIC); mus = { L: m.channels[0].subarray(0, N), R: (m.channels[1] || m.channels[0]).subarray(0, N) }; }
  // music level set so the speech is ~ +0 dB over the music inside the speech region
  const a = off, b = Math.min(N, off + vL.length);
  let ev = 0, em = 0; for (let i = a; i < b; i++) { ev += refL[i] ** 2; em += mus.L[i] ** 2; }
  const mg = Math.sqrt(ev / em) * Math.pow(10, -(+process.env.SNR || 0) / 20);
  const mixL = new Float32Array(N), mixR = new Float32Array(N), muL = new Float32Array(N);
  for (let i = 0; i < N; i++) { muL[i] = mus.L[i] * mg; mixL[i] = refL[i] + muL[i]; mixR[i] = refR[i] + mus.R[i] * mg; }
  writeWav(path.join(out, 'mix.wav'), mixL, mixR); writeWav(path.join(out, 'ref_vocal.wav'), refL, refR);

  const vc = new VocalCleaner({ modelsDir: process.env.YOINK_MODEL_DIR, cacheDir: path.join(out, 'cache'), idleMs: 0 });
  const t0 = Date.now();
  const res = await vc.clean(path.join(out, 'mix.wav'), { onProgress: (p, pct, t) => { if (p !== 'run') console.log('  ', p, pct, t); } });
  const wall = Date.now() - t0;
  const est = readWav(res.file).channels;
  const r = (x, y) => ({ ref: x, est: y });
  const seg = (x) => x.subarray(a, b);
  const result = {
    wallMs: wall, audioSec: N / SR, peakRssMB: peakRss, cached: res.cached,
    sdrMixVsRef_dB: +sdr(seg(refL), seg(mixL)).toFixed(1),
    sdrCleanVsRef_dB: +sdr(seg(refL), seg(est[0])).toFixed(1),
    // how much of the music is left: energy of (clean - ref) relative to the music energy there
    musicLeftBelowMusic_dB: +(db(e(seg(est[0]).map((v, i) => v - seg(refL)[i])) / e(seg(muL)))).toFixed(1),
    // outside the speech the vocal stem should be near silent: level of the clean stem vs the music there
    silenceSuppression_dB: +(db(e(muL.subarray(0, a)) / e(est[0].subarray(0, a)))).toFixed(1),
  };
  console.log(JSON.stringify(result, null, 2));
  fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(result, null, 2));
  clearInterval(poll); process.exit(0);
})().catch((e) => { console.error(e); process.exit(1); });
