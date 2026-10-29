'use strict';
// Offline key estimate for a clip: chroma (12 pitch classes) matched against Krumhansl-Schmuckler key profiles.
// It is an estimate. Speech and very short clips give weak answers, so the result carries a confidence the UI shows.
const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
// Camelot wheel: number for each major key root (C=8B, G=9B ...); minor is the relative minor (A minor = 8A).
const CAM_MAJOR = [8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1];
const CAM_MINOR = [5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10];

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}

function corr(a, b) {
  const n = a.length; let ma = 0, mb = 0; for (let i = 0; i < n; i++) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
  let num = 0, da = 0, db = 0; for (let i = 0; i < n; i++) { const x = a[i] - ma, y = b[i] - mb; num += x * y; da += x * x; db += y * y; }
  return da > 0 && db > 0 ? num / Math.sqrt(da * db) : 0;
}

// samples: mono Float32Array at `rate` Hz. Returns { key:'C major', camelot:'8B', confidence:0..1 } or { error }.
function detectKey(samples, rate) {
  const N = 8192, hop = 4096;
  if (!samples || samples.length < N) return { error: 'Too short to tell.' };
  const chroma = new Float64Array(12), re = new Float64Array(N), im = new Float64Array(N);
  const win = new Float64Array(N); for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1));
  const lo = Math.ceil(70 * N / rate), hi = Math.floor(2000 * N / rate);
  const bin = new Int8Array(hi + 1).fill(-1);
  for (let k = lo; k <= hi; k++) { const f = k * rate / N; const m = 69 + 12 * Math.log2(f / 440); bin[k] = ((Math.round(m) % 12) + 12) % 12; }
  let energy = 0;
  for (let s = 0; s + N <= samples.length; s += hop) {
    for (let i = 0; i < N; i++) { re[i] = samples[s + i] * win[i]; im[i] = 0; }
    fft(re, im);
    for (let k = lo; k <= hi; k++) { const mag = Math.hypot(re[k], im[k]); chroma[bin[k]] += mag * mag; energy += mag * mag; }
  }
  if (energy < 1e-6) return { error: 'This clip is too quiet to tell.' };
  const c = Array.from(chroma, (v) => Math.sqrt(v));          // compress so one loud partial does not dominate
  let best = null, second = -2;
  for (let root = 0; root < 12; root++) {
    for (const [mode, prof] of [['major', MAJOR], ['minor', MINOR]]) {
      const rot = prof.map((_, i) => prof[(i - root + 12) % 12]);
      const r = corr(c, rot);
      if (!best || r > best.r) { if (best) second = Math.max(second, best.r); best = { r, root, mode }; } else second = Math.max(second, r);
    }
  }
  const camNum = best.mode === 'major' ? CAM_MAJOR[best.root] : CAM_MINOR[best.root];
  const conf = Math.max(0, Math.min(1, (best.r - second) * 6 + (best.r - 0.5) * 0.6));
  return { key: NOTES[best.root] + ' ' + best.mode, camelot: camNum + (best.mode === 'major' ? 'B' : 'A'), confidence: Math.round(conf * 100) / 100 };
}

module.exports = { detectKey, NOTES };
