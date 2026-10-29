'use strict';
// Yoink core v2: search YouTube -> filter -> fetch captions -> find exact phrase -> cut audio (with a wider
// source so the clip can be trimmed afterwards). No Electron dependency, so it can be tested from the command line.
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const https = require('https');
const http = require('http');

// ---------- constants shared with the UI ----------
const CATEGORIES = ['Autos & Vehicles', 'Comedy', 'Education', 'Entertainment', 'Film & Animation', 'Gaming', 'Howto & Style', 'Music',
  'News & Politics', 'Nonprofits & Activism', 'People & Blogs', 'Pets & Animals', 'Science & Technology', 'Sports', 'Travel & Events'];
const LANGUAGES = [['en', 'English'], ['es', 'Spanish'], ['fr', 'French'], ['de', 'German'], ['it', 'Italian'], ['pt', 'Portuguese'], ['nl', 'Dutch'],
  ['sv', 'Swedish'], ['no', 'Norwegian'], ['da', 'Danish'], ['fi', 'Finnish'], ['pl', 'Polish'], ['cs', 'Czech'], ['ro', 'Romanian'], ['hu', 'Hungarian'],
  ['el', 'Greek'], ['tr', 'Turkish'], ['ru', 'Russian'], ['uk', 'Ukrainian'], ['ar', 'Arabic'], ['he', 'Hebrew'], ['hi', 'Hindi'], ['bn', 'Bengali'],
  ['id', 'Indonesian'], ['vi', 'Vietnamese'], ['th', 'Thai'], ['zh', 'Chinese'], ['ja', 'Japanese'], ['ko', 'Korean'], ['fa', 'Persian']];
const REGIONS = [['US', 'United States'], ['GB', 'United Kingdom'], ['CA', 'Canada'], ['AU', 'Australia'], ['IE', 'Ireland'], ['NZ', 'New Zealand'],
  ['DE', 'Germany'], ['FR', 'France'], ['ES', 'Spain'], ['IT', 'Italy'], ['NL', 'Netherlands'], ['SE', 'Sweden'], ['NO', 'Norway'], ['PL', 'Poland'],
  ['BR', 'Brazil'], ['MX', 'Mexico'], ['AR', 'Argentina'], ['CO', 'Colombia'], ['JP', 'Japan'], ['KR', 'South Korea'], ['IN', 'India'], ['ID', 'Indonesia'],
  ['PH', 'Philippines'], ['ZA', 'South Africa'], ['NG', 'Nigeria'], ['EG', 'Egypt'], ['TR', 'Turkey'], ['RU', 'Russia'], ['UA', 'Ukraine'], ['JM', 'Jamaica']];
const MAX_STEP_TRIES = 6; // a search query that fails this many times in total is skipped
const WIDE_MARGIN = 3.5; // seconds of extra audio kept on each side of the phrase so the clip can be trimmed wider

// ---------- small helpers ----------
function sleep(ms, signal) {
  return new Promise((r) => { const t = setTimeout(r, ms); if (signal) signal.addEventListener('abort', () => { clearTimeout(t); r(); }, { once: true }); });
}
function run(bin, args, opts = {}) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(bin, args, { windowsHide: true, shell: false, stdio: ['ignore', 'pipe', 'pipe'], env: opts.env || process.env });
    } catch (e) {
      return resolve({ code: -1, out: '', err: String(e.message || e) });
    }
    let out = '', err = '';
    let done = false;
    const finish = (code) => { if (done) return; done = true; clearTimeout(timer); resolve({ code, out, err }); };
    const timer = setTimeout(() => { try { child.kill(); } catch (_) {} finish(-2); }, opts.timeoutMs || 120000);
    if (opts.signal) opts.signal.addEventListener('abort', () => { try { child.kill(); } catch (_) {} });
    child.stdout.on('data', (d) => { out += d; });
    child.stderr.on('data', (d) => { err += d; });
    child.on('error', (e) => { err += String(e.message || e); finish(-1); });
    child.on('close', (code) => finish(code));
  });
}

// Unicode-aware tokenizer. Letters and numbers of any script are kept. Scripts written without spaces
// (Chinese, Japanese kana, Thai) become one token per character so a phrase can match in the middle of a caption line.
const NOSPACE = /[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Thai}]/u;
const NOSPACE_G = /[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Thai}]/gu;
function normTokens(s) {
  return String(s || '')
    .normalize('NFKC')
    .toLowerCase()
    .replace(/[\u2018\u2019`\u00b4]/g, "'")
    .replace(NOSPACE_G, ' $& ')
    .replace(/[^\p{L}\p{N}\p{M}'\s]/gu, ' ')
    .split(/\s+/)
    .map((t) => t.replace(/^'+|'+$/g, ''))
    .filter(Boolean);
}
function joinWords(words) {
  let out = '';
  for (let i = 0; i < words.length; i++) {
    const w = words[i];
    const glue = i > 0 && NOSPACE.test(w) && NOSPACE.test(words[i - 1]) && [...w].length === 1 && [...words[i - 1]].length === 1;
    out += (i > 0 && !glue ? ' ' : '') + w;
  }
  return out;
}

function safeName(s, max = 40) {
  return String(s || '').replace(/[<>:"/\\|?*\x00-\x1f]/g, '').replace(/\s+/g, '_').replace(/_+/g, '_').slice(0, max).replace(/^_|_$/g, '') || 'clip';
}

// ---------- filters ----------
// "YYYY/MM" (also "YYYY-MM" or "YYYY") -> {y, m} or null
function parseYM(s) {
  const m = String(s || '').trim().match(/^(\d{4})(?:[\/\-.](\d{1,2}))?$/);
  if (!m) return null;
  const y = Number(m[1]), mo = m[2] ? Number(m[2]) : null;
  if (y < 2005 || y > 2100 || (mo != null && (mo < 1 || mo > 12))) return null;
  return { y, m: mo };
}
function numOrNull(v) { if (v === '' || v == null) return null; const n = Number(String(v).replace(/[, _]/g, '')); return Number.isFinite(n) && n >= 0 ? n : null; }
function normFilters(f) {
  f = f || {};
  const lang = String(f.language || 'any').toLowerCase();
  const reg = String(f.region || 'any').toUpperCase();
  return {
    cc: !!f.cc, manual: !!f.manual,
    language: lang === 'any' || !/^[a-z]{2,3}$/.test(lang) ? 'any' : lang,
    category: CATEGORIES.includes(f.category) ? f.category : 'all',
    region: /^[A-Z]{2}$/.test(reg) ? reg : 'any',
    channel: String(f.channel || '').trim(), title: String(f.title || '').trim(),
    viewsMin: numOrNull(f.viewsMin), viewsMax: numOrNull(f.viewsMax),
    from: parseYM(f.uploadFrom), to: parseYM(f.uploadTo),
  };
}
function containsCI(hay, needle) { return String(hay || '').toLowerCase().includes(String(needle).toLowerCase()); }
// Cheap filters: need only what the YouTube search list already gives us (title, channel, views).
function passesCheap(v, f) {
  if (f.channel && !containsCI(v.channel, f.channel)) return false;
  if (f.title && !f.title.toLowerCase().split(/\s+/).filter(Boolean).every((w) => containsCI(v.title, w))) return false;
  if (v.views != null) {
    if (f.viewsMin != null && v.views < f.viewsMin) return false;
    if (f.viewsMax != null && v.views > f.viewsMax) return false;
  }
  return true;
}
// Filters that need the video's own metadata (one yt-dlp call, which also fetches the captions).
function passesMeta(meta, f) {
  if (f.cc && !/creative commons/i.test(meta.license || '')) return false;
  if (f.category !== 'all' && !(meta.categories || []).includes(f.category)) return false;
  if (f.channel && !(containsCI(meta.channel, f.channel) || containsCI(meta.uploader, f.channel))) return false;
  if (f.title && !f.title.toLowerCase().split(/\s+/).filter(Boolean).every((w) => containsCI(meta.title, w))) return false;
  if (meta.view_count != null) {
    if (f.viewsMin != null && meta.view_count < f.viewsMin) return false;
    if (f.viewsMax != null && meta.view_count > f.viewsMax) return false;
  }
  if (f.from || f.to) {
    const d = String(meta.upload_date || '');
    if (!/^\d{8}$/.test(d)) return false;
    const ym = Number(d.slice(0, 6));
    if (f.from && ym < f.from.y * 100 + (f.from.m || 1)) return false;
    if (f.to && ym > f.to.y * 100 + (f.to.m || 12)) return false;
  }
  if (f.manual && !(meta.subKeys || []).length) return false;
  return true;
}
function subLangSpec(f) {
  const l = f.language;
  return l === 'any' ? ['en.*', '.*-orig'] : [l + '.*', l + '-orig'];
}
function specMatcher(f) {
  const l = f.language === 'any' ? 'en' : f.language;
  return (lang) => lang.startsWith(l) || (f.language === 'any' && /-orig$/.test(lang));
}

// ---------- caption parsing ----------
// json3 captions -> [{w, t(start sec), e(end sec), approx}]
function parseJson3(text) {
  let j;
  try { j = JSON.parse(text); } catch (_) { return []; }
  const words = [];
  for (const ev of j.events || []) {
    if (!ev.segs) continue;
    const evStart = (ev.tStartMs || 0) / 1000;
    const evEnd = evStart + (ev.dDurationMs || 0) / 1000;
    const hasOffsets = ev.segs.some((s) => typeof s.tOffsetMs === 'number' && s.tOffsetMs > 0);
    if (hasOffsets) {
      // auto-generated: word-level timing
      for (const s of ev.segs) {
        const toks = normTokens(s.utf8);
        if (!toks.length) continue;
        const t = evStart + (s.tOffsetMs || 0) / 1000;
        toks.forEach((w) => words.push({ w, t, e: null, approx: false }));
      }
    } else {
      // manual caption line: spread words across the line by character count
      const lineText = ev.segs.map((s) => s.utf8 || '').join('');
      const toks = normTokens(lineText);
      if (!toks.length) continue;
      const total = toks.reduce((a, w) => a + w.length + 1, 0);
      let acc = 0;
      const dur = Math.max(0.2, evEnd - evStart);
      for (const w of toks) {
        const t = evStart + (acc / total) * dur;
        acc += w.length + 1;
        words.push({ w, t, e: evStart + (acc / total) * dur, approx: true });
      }
    }
  }
  words.sort((a, b) => a.t - b.t);
  // drop exact duplicates (rolling captions)
  const out = [];
  for (const x of words) {
    const p = out[out.length - 1];
    if (p && p.w === x.w && Math.abs(p.t - x.t) < 0.02) continue;
    out.push(x);
  }
  for (let i = 0; i < out.length; i++) {
    if (out[i].e == null) {
      const next = out[i + 1] ? out[i + 1].t : out[i].t + 0.6;
      out[i].e = Math.min(next, out[i].t + 0.35 + out[i].w.length * 0.06);
    }
    if (out[i].e <= out[i].t) out[i].e = out[i].t + 0.25;
  }
  return out;
}

function findPhrase(words, phrase, maxHits = 3) {
  const target = normTokens(phrase);
  if (!target.length) return [];
  // Compare with spaces removed so "doggone" also matches "dog gone", but only on whole-word boundaries.
  const want = target.join('').replace(/'/g, '');
  let text = '';
  const startAt = []; // char index where each word starts
  const wordAt = [];  // word index for every char
  for (let i = 0; i < words.length; i++) {
    startAt.push(text.length);
    const w = words[i].w.replace(/'/g, '');
    for (let c = 0; c < w.length; c++) wordAt.push(i);
    text += w;
  }
  const hits = [];
  let lastEnd = -1e9;
  let from = 0;
  while (from <= text.length - want.length) {
    const idx = text.indexOf(want, from);
    if (idx < 0) break;
    from = idx + 1;
    const i = wordAt[idx];
    const j = wordAt[idx + want.length - 1];
    if (startAt[i] !== idx) continue;                                   // must start at a word start
    if (j + 1 < words.length ? startAt[j + 1] !== idx + want.length : false) continue; // must end at a word end
    const first = words[i], last = words[j];
    if (last.e - first.t > 1.5 + target.length * 1.2) continue; // split across a long gap
    if (first.t < lastEnd + 8) continue;                         // skip near-duplicates
    const ctxFrom = Math.max(0, i - 6), ctxTo = Math.min(words.length, j + 7);
    const ctx = words.slice(ctxFrom, ctxTo).map((x) => x.w);
    hits.push({ start: first.t, end: last.e, line: joinWords(ctx), words: ctx, hiFrom: i - ctxFrom, hiTo: j - ctxFrom, approx: words.slice(i, j + 1).some((x) => x.approx) });
    lastEnd = last.e;
    if (hits.length >= maxHits) break;
  }
  return hits;
}

// ---------- WAV helpers (for the waveform) ----------
// Returns {peaks:[0..1 x bins], duration}. Reads 16-bit PCM wav written by ffmpeg.
function wavPeaks(file, bins = 480) {
  const buf = fs.readFileSync(file);
  if (buf.length < 44 || buf.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not a wav file');
  let pos = 12, fmt = null, dataOff = -1, dataLen = 0;
  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    let len = buf.readUInt32LE(pos + 4);
    if (id === 'fmt ') fmt = { ch: buf.readUInt16LE(pos + 10), rate: buf.readUInt32LE(pos + 12), bits: buf.readUInt16LE(pos + 22) };
    if (id === 'data') { dataOff = pos + 8; dataLen = Math.min(len, buf.length - dataOff); break; }
    pos += 8 + len + (len % 2);
  }
  if (!fmt || dataOff < 0 || fmt.bits !== 16) throw new Error('unsupported wav');
  const frames = Math.floor(dataLen / (2 * fmt.ch));
  const peaks = new Array(bins).fill(0);
  for (let f = 0; f < frames; f++) {
    let a = 0;
    for (let c = 0; c < fmt.ch; c++) a = Math.max(a, Math.abs(buf.readInt16LE(dataOff + (f * fmt.ch + c) * 2)));
    const b = Math.min(bins - 1, Math.floor((f / frames) * bins));
    if (a > peaks[b]) peaks[b] = a;
  }
  const mx = Math.max(1000, ...peaks);
  return { peaks: peaks.map((p) => Math.round((p / mx) * 100) / 100), duration: frames / fmt.rate };
}

// ---------- the engine ----------
class Yoink {
  constructor(cfg) {
    this.ytdlp = cfg.ytdlp;
    this.ffmpeg = cfg.ffmpeg;
    this.dataDir = cfg.dataDir;
    this.clipsDir = cfg.clipsDir;
    this.extraArgs = cfg.extraArgs || []; // e.g. ['--proxy', 'http://...']
    this.env = cfg.env || process.env;
    fs.mkdirSync(path.join(this.dataDir, 'captions'), { recursive: true });
    fs.mkdirSync(path.join(this.dataDir, 'wide'), { recursive: true });
    fs.mkdirSync(this.clipsDir, { recursive: true });
  }

  setClipsDir(dir) { this.clipsDir = dir; fs.mkdirSync(dir, { recursive: true }); }

  ytdlpVersion() {
    return run(this.ytdlp, ['--version'], { timeoutMs: 15000, env: this.env }).then((r) => (r.code === 0 ? r.out.trim() : null));
  }

  // One YouTube search. f = normalized filters (cc and region change the search itself).
  async searchVideos(query, count, signal, f = {}) {
    const args = [...this.extraArgs];
    if (f.region && f.region !== 'any') args.push('--xff', f.region);
    args.push('--flat-playlist', '--no-warnings', '--print', '%(id)s\t%(duration)s\t%(channel)s\t%(view_count)s\t%(title)s');
    if (f.cc) args.push('--playlist-end', String(count), 'https://www.youtube.com/results?search_query=' + encodeURIComponent(query) + '&sp=EgIwAQ%3D%3D');
    else args.push(`ytsearch${count}:${query}`);
    const r = await run(this.ytdlp, args, { timeoutMs: 60000, signal, env: this.env });
    if (r.code !== 0 && !r.out.trim()) throw new Error(explainYtdlpError(r.err));
    return r.out.split(/\r?\n/).filter(Boolean).map((l) => {
      const [id, dur, channel, views, ...t] = l.split('\t');
      return { id, duration: Number(dur) || 0, channel: channel && channel !== 'NA' ? channel : '', views: views && views !== 'NA' ? Number(views) : null, title: t.join('\t') };
    }).filter((v) => /^[\w-]{11}$/.test(v.id));
  }

  // One yt-dlp call per video: metadata (license, category, date...) + the caption tracks we want.
  // Returns { meta, tracks:[{words, manual, lang}] }. Cached on disk.
  async getVideoData(id, f, signal) {
    const dir = path.join(this.dataDir, 'captions');
    const spec = subLangSpec(f);
    const accept = specMatcher(f);
    const key = crypto.createHash('md5').update(spec.join(',') + (f.manual ? 'm' : 'a')).digest('hex').slice(0, 6);
    const marker = path.join(dir, `${id}.${key}.done`);
    const metaFile = path.join(dir, id + '.meta.json');
    const listTracks = () => fs.readdirSync(dir).filter((x) => x.startsWith(id + '.') && x.endsWith('.json3') && accept(x.slice(id.length + 1, -6)));
    if (!(fs.existsSync(marker) && fs.existsSync(metaFile))) {
      const args = [...this.extraArgs];
      if (f.region && f.region !== 'any') args.push('--xff', f.region);
      args.push('--skip-download', '--no-warnings', '--write-info-json', '--no-write-comments', '--write-subs');
      if (!f.manual) args.push('--write-auto-subs');
      args.push('--sub-langs', spec.join(','), '--sub-format', 'json3', '--no-playlist', '-o', path.join(dir, '%(id)s'), 'https://www.youtube.com/watch?v=' + id);
      const r = await run(this.ytdlp, args, { timeoutMs: 60000, signal, env: this.env });
      const infoFile = path.join(dir, id + '.info.json');
      if (fs.existsSync(infoFile)) {
        try {
          const j = JSON.parse(fs.readFileSync(infoFile, 'utf8'));
          const autoKeys = Object.keys(j.automatic_captions || {});
          const orig = autoKeys.find((k) => /-orig$/.test(k));
          const meta = {
            id, title: j.title, channel: j.channel || j.uploader || '', uploader: j.uploader || '', view_count: j.view_count == null ? null : j.view_count,
            upload_date: j.upload_date || '', categories: j.categories || [], license: j.license || '', language: j.language || '', duration: j.duration || 0,
            subKeys: Object.keys(j.subtitles || {}).filter((k) => k !== 'live_chat'), autoKeys: autoKeys.filter((k) => /-orig$|^en/.test(k)),
            origLang: orig ? orig.replace(/-orig$/, '').split('-')[0] : '',
          };
          fs.writeFileSync(metaFile, JSON.stringify(meta));
        } catch (_) {}
        try { fs.unlinkSync(infoFile); } catch (_) {}
      }
      if (fs.existsSync(metaFile) || listTracks().length) fs.writeFileSync(marker, '1');
      else throw new Error(explainYtdlpError(r.err) || 'Could not read that video.');
    }
    let meta = {};
    try { meta = JSON.parse(fs.readFileSync(metaFile, 'utf8')); } catch (_) {}
    const tracks = [];
    for (const file of listTracks()) {
      const lang = file.slice(id.length + 1, -6);
      const manual = (meta.subKeys || []).includes(lang);
      if (f.manual && !manual) continue;
      if (!manual) {
        // Auto tracks without "-orig" can be machine translations of another language: only trust them for the video's own language.
        const base = lang.split('-')[0];
        const own = meta.origLang || (meta.language || '').split('-')[0];
        if (!/-orig$/.test(lang) && own && own !== base) continue;
      }
      const words = parseJson3(fs.readFileSync(path.join(dir, file), 'utf8'));
      if (words.length) tracks.push({ words, manual, lang });
    }
    // word-timed (auto) tracks first: their timing is much tighter than manual captions
    tracks.sort((x, y) => (x.manual ? 1 : 0) - (y.manual ? 1 : 0));
    return { meta, tracks };
  }

  // Download a wide window around the hit (lossless WAV) so the clip can be trimmed later. Returns {file, winStart, duration, peaks}.
  async makeWide(video, hit, signal) {
    const winStart = Math.max(0, hit.start - WIDE_MARGIN);
    const winEnd = hit.end + WIDE_MARGIN;
    const base = `${video.id}_${Math.round(winStart * 1000)}_${Math.round(winEnd * 1000)}`;
    const wide = path.join(this.dataDir, 'wide', base + '.wav');
    if (!(fs.existsSync(wide) && fs.statSync(wide).size > 1000)) {
      const tmpBase = path.join(this.dataDir, 'tmp_' + base + '_' + process.pid);
      const r = await run(this.ytdlp, [...this.extraArgs, '--no-warnings', '--no-playlist', '-f', 'bestaudio[ext=m4a]/bestaudio/best',
        '--download-sections', `*${winStart.toFixed(2)}-${winEnd.toFixed(2)}`, '--ffmpeg-location', this.ffmpeg, '--no-part', '--no-mtime',
        '-o', tmpBase + '.%(ext)s', 'https://www.youtube.com/watch?v=' + video.id], { timeoutMs: 120000, signal, env: this.env });
      const tmpFile = fs.readdirSync(this.dataDir).filter((x) => x.startsWith(path.basename(tmpBase) + '.')).map((x) => path.join(this.dataDir, x))[0];
      if (!tmpFile) throw new Error(explainYtdlpError(r.err) || 'Could not download the audio for this clip.');
      try {
        const f = await run(this.ffmpeg, ['-y', '-v', 'error', '-i', tmpFile, '-vn', '-ar', '44100', '-ac', '2', '-c:a', 'pcm_s16le', wide], { timeoutMs: 60000, signal, env: this.env });
        if (f.code !== 0 || !fs.existsSync(wide)) throw new Error('ffmpeg could not read the audio. ' + f.err.slice(0, 200));
      } finally { try { fs.unlinkSync(tmpFile); } catch (_) {} }
    }
    const { peaks, duration } = wavPeaks(wide);
    return { file: wide, winStart, duration, peaks };
  }

  // x = { variant: 'vocal' | '', normalize: bool, fadeOut: bool }. The options are part of the file name hash so a changed setting never reuses an old cut.
  clipFile(videoId, phrase, absStart, trimS, trimE, fmt, x = {}) {
    const opt = (x.normalize ? 'n' : '') + (x.fadeOut ? 'f' : '');
    const tag = crypto.createHash('md5').update(videoId + '|' + absStart.toFixed(2) + '|' + trimS.toFixed(2) + '|' + trimE.toFixed(2) + (opt ? '|' + opt : '')).digest('hex').slice(0, 4);
    return path.join(this.clipsDir, `${safeName(phrase, 30)}${x.variant === 'vocal' ? '_vocal' : ''}_${videoId}_${Math.round(absStart)}s_${tag}.${fmt}`);
  }

  // Gain (dB) that brings the loudest peak of [trimS, trimE] to -1 dBFS. Capped at +24 dB so near-silence is not blown up.
  async peakGainDb(wideFile, trimS, trimE, signal) {
    const f = await run(this.ffmpeg, ['-v', 'info', '-ss', trimS.toFixed(3), '-i', wideFile, '-t', Math.max(0.05, trimE - trimS).toFixed(3), '-af', 'volumedetect', '-f', 'null', '-'], { timeoutMs: 60000, signal, env: this.env });
    const m = /max_volume:\s*(-?[\d.]+) dB/.exec(f.err || '');
    if (!m) return 0;
    return Math.max(-24, Math.min(24, -1 - parseFloat(m[1])));
  }

  // Cut [trimS, trimE] (seconds inside the wide file) into the final audio file. Instant, no network.
  // x.normalize: peak-normalize to -1 dBFS. x.fadeOut: 250 ms fade at the end (otherwise only a 10 ms de-click).
  async renderClip(wideFile, trimS, trimE, fmt, out, signal, x = {}) {
    fmt = ['mp3', 'm4a'].includes(fmt) ? fmt : 'wav';
    if (fs.existsSync(out) && fs.statSync(out).size > 1000) return out;
    const dur = Math.max(0.05, trimE - trimS);
    const args = ['-y', '-v', 'error', '-ss', trimS.toFixed(3), '-i', wideFile, '-t', dur.toFixed(3), '-vn', '-ar', '44100', '-ac', '2'];
    if (fmt === 'wav') args.push('-c:a', 'pcm_s16le');
    else if (fmt === 'mp3') args.push('-c:a', 'libmp3lame', '-b:a', '192k');
    else args.push('-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart');
    const chain = [];
    if (x.normalize) { const g = await this.peakGainDb(wideFile, trimS, trimE, signal); if (Math.abs(g) > 0.05) chain.push('volume=' + g.toFixed(2) + 'dB'); }
    chain.push('afade=t=in:d=0.005');
    const fo = x.fadeOut && dur >= 0.6 ? 0.25 : 0.01;
    chain.push('afade=t=out:st=' + Math.max(0, dur - fo).toFixed(3) + ':d=' + fo);
    if (x.normalize) chain.push('alimiter=limit=0.95');
    args.push('-af', chain.join(','), out);
    const f = await run(this.ffmpeg, args, { timeoutMs: 60000, signal, env: this.env });
    if (f.code !== 0 || !fs.existsSync(out)) throw new Error('ffmpeg could not cut the clip. ' + f.err.slice(0, 200));
    return out;
  }

  // Search plan: several query variants, deeper each round. "Load more" simply continues the plan.
  newSession(phrase, filters) {
    const f = normFilters(filters);
    const extra = [];
    if (f.title) extra.push(f.title);
    if (f.channel) extra.push(f.channel);
    const sfx = extra.length ? ' ' + extra.join(' ') : '';
    const base = [`"${phrase}"${sfx}`, `${phrase}${sfx}`, `"${phrase}" interview${sfx}`, `"${phrase}" podcast${sfx}`, `"${phrase}" scene${sfx}`];
    const more = [`"${phrase}" lyrics${sfx}`, `"${phrase}" song${sfx}`, `"${phrase}" movie${sfx}`, `"${phrase}" funny${sfx}`, `"${phrase}" compilation${sfx}`];
    const plan = [];
    for (const [qs, depth] of [[base, 20], [base, 50], [more, 30], [base, 100], [more, 80]]) for (const q of qs) plan.push({ q, depth });
    return { phrase, f, plan, planIdx: 0, seen: new Set(), cids: new Set(), queue: [], scanned: 0, matches: 0, shown: 0, noCaps: 0, filtered: 0, errors: 0, lastError: '', exhausted: false, blocked: false, tries: {} };
  }

  // Run (or continue) a search until opts.maxClips more clips are ready, or the plan runs out.
  // emit(evt): status | count | pending | progress | clip | done
  async search(session, opts, emit, signal) {
    opts = Object.assign({ padBefore: 0.3, padAfter: 0.3, format: 'wav', maxClips: 12, concurrency: 3 }, opts);
    const S = session, f = S.f, phrase = S.phrase;
    if (!normTokens(phrase).length) { emit({ type: 'error', text: 'Type a phrase first.' }); return; }
    let batch = 0, searchDone = false;
    const aborted = () => signal && signal.aborted;
    const enough = () => { const e = batch >= opts.maxClips; if (e && !inner.signal.aborted) inner.abort(); return e; };
    const waiters = [];
    const notify = () => { while (waiters.length) waiters.shift()(); };
    const waitFor = () => new Promise((r) => { const t = setTimeout(r, 400); waiters.push(() => { clearTimeout(t); r(); }); }); // timeout = safety net against a missed notify
    const inner = new AbortController();                   // cancels an in-flight YouTube search once we have enough clips
    if (signal) signal.addEventListener('abort', () => inner.abort());
    const count = () => emit({ type: 'count', matches: S.matches, scanned: S.scanned, shown: S.shown });

    emit({ type: 'status', text: `Searching YouTube for "${phrase}"...` });
    S.blocked = false;
    const searcher = (async () => {
      let fails = 0;
      while (!aborted() && !enough() && S.planIdx < S.plan.length) {
        while (!aborted() && !enough() && S.queue.length >= opts.concurrency * 2) await waitFor();
        if (aborted() || enough()) break;
        const idx = S.planIdx;
        const step = S.plan[S.planIdx++];
        try {
          const vids = await this.searchVideos(step.q, step.depth, inner.signal, f);
          if (inner.signal.aborted) { S.planIdx = idx; break; }   // interrupted (Stop, or we had enough): this step is redone on Load more
          fails = 0;
          for (const v of vids) {
            if (S.seen.has(v.id)) continue;
            S.seen.add(v.id);
            if (!passesCheap(v, f)) { S.filtered++; continue; }   // cheap filters first: no caption fetch for these
            S.queue.push(v);
          }
          notify();
        } catch (e) {
          S.planIdx = idx;                                        // a failed step is NOT used up: it is retried, never counted as "no more results"
          if (inner.signal.aborted) break;
          S.errors++; S.lastError = e.message;
          S.tries[idx] = (S.tries[idx] || 0) + 1;
          if (!S.seen.size) emit({ type: 'status', text: e.message });
          if (S.tries[idx] >= MAX_STEP_TRIES) { S.planIdx = idx + 1; fails = 0; continue; }   // a query that keeps failing is skipped for good
          if (++fails >= 2) { S.blocked = true; break; }          // YouTube keeps refusing: stop and let the user press Load more to try again
          await sleep(opts.retryMs != null ? opts.retryMs : 1500, inner.signal);
        }
      }
      searchDone = true; notify();
    })();

    const cutHit = async (v, meta, hit) => {
      const cid = `${v.id}_${Math.round(hit.start * 100)}`;
      const card = { cid, videoId: v.id, title: meta.title || v.title, channel: meta.channel || v.channel, category: (meta.categories || [])[0] || '',
        views: meta.view_count != null ? meta.view_count : v.views, uploaded: meta.upload_date || '', license: meta.license || '',
        words: hit.words, hiFrom: hit.hiFrom, hiTo: hit.hiTo, line: hit.line, start: hit.start, end: hit.end, approx: hit.approx,
        url: `https://www.youtube.com/watch?v=${v.id}&t=${Math.max(0, Math.floor(hit.start - 2))}s` };
      emit({ type: 'pending', clip: card });
      emit({ type: 'progress', cid, pct: 15, text: 'Waiting to download...' });
      try {
        emit({ type: 'progress', cid, pct: 35, text: 'Downloading audio...' });
        const wide = await this.makeWide(v, hit, signal);
        emit({ type: 'progress', cid, pct: 80, text: 'Cutting...' });
        const trimStart = Math.max(0, hit.start - opts.padBefore - wide.winStart);
        const trimEnd = Math.min(wide.duration, hit.end + opts.padAfter - wide.winStart);
        const xo = { normalize: !!opts.normalize, fadeOut: !!opts.fadeOut };
        const out = await this.renderClip(wide.file, trimStart, trimEnd, opts.format, this.clipFile(v.id, phrase, hit.start, trimStart, trimEnd, opts.format === 'mp3' || opts.format === 'm4a' ? opts.format : 'wav', xo), signal, xo);
        emit({ type: 'clip', clip: Object.assign(card, { file: out, wideFile: wide.file, winStart: wide.winStart, wideDur: wide.duration, peaks: wide.peaks,
          trimStart, trimEnd, hitStart: hit.start - wide.winStart, hitEnd: hit.end - wide.winStart, format: opts.format }) });
        return true;
      } catch (e) {
        S.errors++; S.lastError = e.message;
        emit({ type: 'dropped', cid });
        return false;
      }
    };

    const worker = async () => {
      while (!aborted() && !enough()) {
        const v = S.queue.shift();
        if (!v) { if (searchDone) return; await waitFor(); continue; }
        notify();
        emit({ type: 'status', text: `Checking captions: ${v.title.slice(0, 70)}` });
        let data;
        try { data = await this.getVideoData(v.id, f, signal); } catch (e) { if (aborted()) { S.queue.unshift(v); break; } S.errors++; S.lastError = e.message; S.scanned++; count(); continue; }
        S.scanned++;
        if (!passesMeta(data.meta, f)) { S.filtered++; count(); continue; }
        if (!data.tracks.length) { S.noCaps++; count(); continue; }
        let hits = [];
        for (const t of data.tracks) { hits = findPhrase(t.words, phrase, 2); if (hits.length) break; }
        for (const hit of hits) {
          if (aborted() || enough()) break;
          const cid = `${v.id}_${Math.round(hit.start * 100)}`;
          if (S.cids.has(cid)) continue;
          S.cids.add(cid);
          S.matches++; count();
          batch++; if (enough()) notify();                    // reserve a slot so parallel workers never overshoot
          if (await cutHit(v, data.meta, hit)) { S.shown++; count(); } else batch--;
        }
        count();
      }
    };
    await Promise.all([searcher, ...Array.from({ length: opts.concurrency }, worker)]);
    notify();
    S.exhausted = S.exhausted || (S.planIdx >= S.plan.length && !S.queue.length && !S.blocked);   // only when every query ran and every video was looked at
    const active = [f.cc && 'Creative Commons', f.manual && 'manual subtitles', f.language !== 'any' && 'language ' + f.language, f.category !== 'all' && f.category, f.channel && 'channel', f.title && 'title', (f.viewsMin != null || f.viewsMax != null) && 'views', (f.from || f.to) && 'upload date'].filter(Boolean);
    let text;
    if (aborted()) text = `Stopped. Found ${S.matches} matches in ${S.scanned} videos scanned.`;
    else if (S.shown) text = `Found ${S.matches} matches in ${S.scanned} videos scanned.${S.exhausted ? ' That is everything the search turned up.' : ''}`;
    else if (S.blocked) text = (S.lastError || 'YouTube did not answer.') + ' Press Load more to try again.';
    else if (!S.scanned && S.lastError) text = S.lastError;
    else text = `No exact matches for "${phrase}" in ${S.scanned} videos scanned${active.length ? ' with your filters (' + active.join(', ') + ')' : ''}. Try fewer filters, or a shorter or more common phrase.`;
    emit({ type: 'done', text, found: batch, matches: S.matches, scanned: S.scanned, exhausted: S.exhausted, stopped: !!aborted(), failed: !!S.blocked });
    return { found: batch, scanned: S.scanned };
  }
}

function explainYtdlpError(err) {
  const e = String(err || '');
  if (/Sign in to confirm you.re not a bot|confirm you.re not a bot/i.test(e)) return 'YouTube is asking this network to prove it is not a bot. Try again later, switch network/VPN, or press "Update yt-dlp" in Settings.';
  if (/HTTP Error 429|Too Many Requests/i.test(e)) return 'YouTube is rate limiting this connection (too many requests). Wait a few minutes and try again.';
  if (/Unable to download|getaddrinfo|Temporary failure|timed out|Network is unreachable|Connection/i.test(e)) return 'Could not reach YouTube. Check your internet connection.';
  if (/Video unavailable|Private video|age/i.test(e)) return 'That video is unavailable.';
  if (/ENOENT|not recognized|not found/i.test(e)) return 'A bundled tool (yt-dlp or ffmpeg) could not be started.';
  const line = e.split(/\r?\n/).filter((l) => /ERROR/.test(l))[0];
  return line ? line.replace(/^ERROR:\s*/, '').slice(0, 200) : '';
}


function downloadTo(url, dest, onProgress, redirects = 8) {
  return new Promise((resolve, reject) => {
    const get = /^http:/i.test(url) ? http.get : https.get;
    get(url, { headers: { 'User-Agent': 'Yoink' } }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirects > 0) {
        res.resume();
        return resolve(downloadTo(new URL(res.headers.location, url).toString(), dest, onProgress, redirects - 1));
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error('Download failed (HTTP ' + res.statusCode + ').')); }
      const total = Number(res.headers['content-length']) || 0;
      let got = 0;
      if (onProgress) res.on('data', (b) => { got += b.length; onProgress(got, total); });
      const f = fs.createWriteStream(dest);
      res.pipe(f);
      res.on('error', reject);
      f.on('finish', () => f.close(() => (total && got < total ? reject(new Error('Download was cut off.')) : resolve(dest))));
      f.on('error', reject);
    }).on('error', reject);
  });
}


module.exports = { downloadTo, Yoink, parseJson3, findPhrase, normTokens, joinWords, run, explainYtdlpError, safeName, wavPeaks, normFilters, passesCheap, passesMeta, parseYM, CATEGORIES, LANGUAGES, REGIONS, WIDE_MARGIN };
