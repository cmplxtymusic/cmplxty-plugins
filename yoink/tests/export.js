'use strict';
// Export options (normalize, fade out) with the real ffmpeg. Skipped if ffmpeg is not on PATH. Run with: node tests/export.js
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');
const { Yoink } = require('../src/core');
const FF = process.env.YOINK_FFMPEG || 'ffmpeg';
try { execFileSync(FF, ['-version'], { stdio: 'ignore' }); } catch (_) { console.log('export tests skipped (no ffmpeg)'); process.exit(0); }
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-ex-'));
const wide = path.join(tmp, 'wide.wav');
execFileSync(FF, ['-y', '-v', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=3', '-af', 'volume=6dB', '-ar', '44100', '-ac', '2', wide]);
const y = new Yoink({ ytdlp: 'x', ffmpeg: FF, dataDir: path.join(tmp, 'd'), clipsDir: tmp });
const maxVol = (f, ss = 0, t = 99) => { const r = require('child_process').spawnSync(FF, ['-v', 'info', '-ss', String(ss), '-i', f, '-t', String(t), '-af', 'volumedetect', '-f', 'null', '-'], { encoding: 'utf8' }); return parseFloat(/max_volume: (-?[\d.]+) dB/.exec(r.stderr)[1]); };
(async () => {
  const plain = await y.renderClip(wide, 0.5, 2.5, 'wav', path.join(tmp, 'plain.wav'));
  const norm = await y.renderClip(wide, 0.5, 2.5, 'wav', path.join(tmp, 'norm.wav'), null, { normalize: true });
  const fade = await y.renderClip(wide, 0.5, 2.5, 'wav', path.join(tmp, 'fade.wav'), null, { fadeOut: true });
  const mp = maxVol(plain), mn = maxVol(norm);
  assert.ok(mp < -6, 'source clip is quiet: ' + mp);
  assert.ok(mn > -1.6 && mn <= -0.4, 'normalized peak near -1 dBFS: ' + mn);
  const tailPlain = maxVol(plain, 1.9, 0.1), tailFade = maxVol(fade, 1.9, 0.1);
  assert.ok(tailFade < tailPlain - 6, `fade out shortens the tail: ${tailPlain} vs ${tailFade}`);
  // option changes the file name, so a changed setting never reuses an old cut
  const a = y.clipFile('abc', 'hi', 5, 0, 1, 'wav', {}), b = y.clipFile('abc', 'hi', 5, 0, 1, 'wav', { normalize: true }), c = y.clipFile('abc', 'hi', 5, 0, 1, 'wav', { variant: 'vocal' });
  assert.ok(a !== b && a !== c && /_vocal_/.test(c));
  console.log('export tests passed (plain peak ' + mp + ' dB, normalized ' + mn + ' dB, tail ' + tailPlain + ' -> ' + tailFade + ' dB)');
})().catch((e) => { console.error(e); process.exit(1); });
