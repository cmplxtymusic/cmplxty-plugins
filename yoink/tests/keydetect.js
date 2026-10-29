'use strict';
// Key detection on generated chords/scales. Run with: node tests/keydetect.js
const assert = require('assert');
const { detectKey } = require('../src/keydetect');
const SR = 22050;
function tone(freqs, sec) { const n = Math.round(sec * SR), x = new Float32Array(n); for (const f of freqs) for (let h = 1; h <= 4; h++) for (let i = 0; i < n; i++) x[i] += Math.sin(2 * Math.PI * f * h * i / SR) / (h * freqs.length); return x; }
const hz = (m) => 440 * Math.pow(2, (m - 69) / 12);
function seq(parts) { const len = parts.reduce((a, p) => a + p.length, 0), o = new Float32Array(len); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o; }
// I - IV - V - I progressions (root, third, fifth), 0.8 s each
function prog(root, minor) { const t = minor ? 3 : 4; const ch = (r) => tone([hz(r), hz(r + t), hz(r + 7)], 0.8); return seq([ch(root), ch(root + 5), ch(root + 7), ch(root)]); }
let r = detectKey(prog(60, false), SR);       assert.strictEqual(r.key, 'C major'); assert.strictEqual(r.camelot, '8B');
r = detectKey(prog(57, true), SR);            assert.strictEqual(r.key, 'A minor'); assert.strictEqual(r.camelot, '8A');
r = detectKey(prog(67, false), SR);           assert.strictEqual(r.key, 'G major'); assert.strictEqual(r.camelot, '9B');
r = detectKey(prog(62, true), SR);            assert.strictEqual(r.key, 'D minor'); assert.strictEqual(r.camelot, '7A');
assert.ok(r.confidence >= 0 && r.confidence <= 1);
assert.ok(detectKey(new Float32Array(100), SR).error);
assert.ok(detectKey(new Float32Array(30000), SR).error);   // silence
console.log('keydetect tests passed');
