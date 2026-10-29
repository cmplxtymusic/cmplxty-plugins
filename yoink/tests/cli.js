// CLI test harness: node tests/cli.js "phrase" [maxClips] [outDir]   (FILTERS='{"cc":true}' PAD=0.3 FORMAT=wav)
const path = require('path');
const { Yoink } = require('../src/core');
const phrase = process.argv[2] || 'get low';
const maxClips = Number(process.argv[3] || 3);
const out = process.argv[4] || '/tmp/yoink-test';
const y = new Yoink({ ytdlp: process.env.YTDLP || 'yt-dlp', ffmpeg: process.env.FFMPEG || '/usr/bin/ffmpeg', dataDir: path.join(out, 'data'), clipsDir: path.join(out, 'clips') });
const t0 = Date.now();
const pad = Number(process.env.PAD || 0.3);
const s = y.newSession(phrase, JSON.parse(process.env.FILTERS || '{}'));
y.search(s, { maxClips, padBefore: pad, padAfter: pad, format: process.env.FORMAT || 'wav' }, (e) => {
  const t = ((Date.now() - t0) / 1000).toFixed(1);
  if (e.type === 'clip') { const c = Object.assign({}, e.clip); delete c.peaks; delete c.words; console.log(`[${t}s] CLIP`, JSON.stringify(c)); }
  else if (e.type === 'pending' || e.type === 'progress' || e.type === 'status') { if (process.env.VERBOSE) console.log(`[${t}s] ${e.type}`, e.text || e.cid || ''); }
  else console.log(`[${t}s] ${e.type}:`, e.text || JSON.stringify(e));
}).then(async () => {
  if (process.env.LOADMORE) { await y.search(s, { maxClips: Number(process.env.LOADMORE), padBefore: pad, padAfter: pad, format: 'wav' }, (e) => { if (e.type === 'clip') console.log('MORE CLIP', e.clip.title, e.clip.start); if (e.type==='done') console.log('MORE DONE', e.text); }); }
  process.exit(0);
});
