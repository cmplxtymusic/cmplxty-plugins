'use strict';
const $ = (id) => document.getElementById(id);
const results = $('results');
let searching = false, lastPhrase = '', settings = null, meta = null;
let audio = null, playing = null;       // playing = {btn, stop()}
let favIds = new Set();
const pending = new Map();              // cid -> card element (while the clip is still downloading)
const cleanLive = new Map();            // cid -> card api while a clean up runs
const cardApi = new Map();              // cid -> {set(c)} for cards shown in the results list

function fileUrl(p) {
  let s = p.replace(/\\/g, '/');
  if (!s.startsWith('/')) s = '/' + s;
  return 'file://' + encodeURI(s).replace(/#/g, '%23').replace(/\?/g, '%3F');
}
function fmtClock(sec) { sec = Math.max(0, Math.floor(sec)); return String(Math.floor(sec / 60)).padStart(2, '0') + ':' + String(sec % 60).padStart(2, '0'); }
function fmtDur(sec) { const t = Math.max(0, sec); return fmtClock(t) + '.' + Math.floor((t % 1) * 10); }
function fmtViews(n) { if (n == null) return ''; if (n >= 1e9) return (n / 1e9).toFixed(1) + 'B views'; if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M views'; if (n >= 1e3) return Math.round(n / 1e3) + 'K views'; return n + ' views'; }
function fmtUp(d) { return /^\d{8}$/.test(d || '') ? d.slice(0, 4) + '/' + d.slice(4, 6) : ''; }
function setStatus(t, err) { $('status').textContent = t; $('statusWrap').classList.toggle('err', !!err); }
function setSearching(on) { searching = on; $('btnGo').disabled = on; $('btnMore').disabled = on; $('btnStop').classList.toggle('hidden', !on); $('count').classList.toggle('busy', on); }
function el(tag, cls, text) { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }

function stopAudio() { if (playing) { const p = playing; playing = null; p.stop(); } if (audio) { audio.pause(); audio = null; } }
function volume() { return $('btnMute').dataset.muted === '1' ? 0 : (settings ? settings.volume : 0.9); }

function selectCard(li) { document.querySelectorAll('li.card.sel').forEach((x) => x.classList.remove('sel')); if (li) li.classList.add('sel'); }

// ---------------- cards ----------------
function highlightLine(c) {
  const box = el('div', 'cline');
  const w = c.words || [];
  box.textContent = '\u201C';
  const pre = w.slice(0, c.hiFrom), mid = w.slice(c.hiFrom, c.hiTo + 1), post = w.slice(c.hiTo + 1);
  if (pre.length) box.append(pre.join(' ') + ' ');
  const em = el('em', null, mid.join(' ')); box.appendChild(em);
  if (post.length) box.append(' ' + post.join(' '));
  box.append('\u201D');
  return box;
}

function makeCard(c, opts = {}) {
  const li = el('li', 'card');
  li.dataset.cid = c.cid;
  const head = el('div', 'chead');
  if (c.category) head.appendChild(el('span', 'chip', c.category));
  head.appendChild(el('span', 'ctitle', c.title)).title = c.title + ' \u00B7 ' + (c.channel || '');
  const heart = el('button', 'cbtn heart', '\u2661'); heart.title = 'Favorite';
  const open = el('button', 'cbtn open', '\u2197'); open.title = 'Open this moment on YouTube';
  const menuBtn = el('button', 'cbtn menubtn', '\u22EF'); menuBtn.title = 'More';
  const menu = el('div', 'cmenu hidden');
  const mClean = el('button', 'ghost', 'Clean up vocals'), mKey = el('button', 'ghost', 'Detect key'), mSave = el('button', 'ghost', 'Save to folder'), mOpen = el('button', 'ghost', 'Open on YouTube');
  menu.append(mClean, mKey, mSave, mOpen);
  head.append(heart, open, menuBtn, menu);
  li.appendChild(head);
  li.appendChild(highlightLine(c));
  const metaLine = el('div', 'cmeta');
  const bits = [c.channel || 'unknown channel', 'at ' + fmtClock(c.start), fmtViews(c.views), fmtUp(c.uploaded)].filter(Boolean);
  metaLine.textContent = bits.join(' \u00B7 ');
  if (/creative commons/i.test(c.license || '')) metaLine.appendChild(el('span', 'badge', 'CC'));
  if (c.approx) { const b = el('span', 'badge', '~timing'); b.title = 'This clip comes from hand-typed captions, so the cut may be a little loose. Use the trim flags.'; metaLine.appendChild(b); }
  li.appendChild(metaLine);

  const box = el('div', 'wavebox');
  const canvas = el('canvas'); canvas.draggable = true; canvas.title = 'Click to play. Drag to your DAW or a folder.';
  const fs = el('div', 'flag s'); fs.appendChild(el('span', 'tab', 'S'));
  const fe = el('div', 'flag e'); fe.appendChild(el('span', 'tab', 'E'));
  const ph = el('div', 'playhead');
  const loadwrap = el('div', 'loadwrap');
  const loadText = el('span', null, 'Waiting to download...');
  const lb = el('div', 'loadbar'); const lbf = el('div'); lb.appendChild(lbf);
  loadwrap.append(loadText, lb);
  box.append(canvas, fs, fe, ph, loadwrap);
  li.appendChild(box);

  const tagBox = opts.tags ? el('div', 'tags') : null;
  if (tagBox) li.appendChild(tagBox);
  const foot = el('div', 'cfoot');
  const dur = el('span', 'dur', '--:--');
  const modeBox = el('span', 'mode hidden');
  const mOrig = el('button', 'small ghost on', 'Original'), mVocal = el('button', 'small ghost', 'Clean');
  modeBox.append(mOrig, mVocal);
  const cleanBtn = el('button', 'small ghost', 'Clean up'); cleanBtn.title = 'Isolate the vocal (free, works offline after a one-time download)';
  const playBtn = el('button', 'small primary play', 'Play');
  const saveBtn = el('button', 'small ghost', 'Save to folder');
  const handle = el('div', 'handle'); handle.draggable = true; handle.title = 'Drag this into your DAW or a folder'; handle.textContent = '\u2807 Drag';
  foot.append(dur, modeBox, el('span', 'sp'), cleanBtn, playBtn, saveBtn, handle);
  li.appendChild(foot);

  let busy = true, mode = 'orig', cleaning = false;
  const cur = () => (mode === 'clean' && c.cleanFile ? c.cleanFile : c.file);
  const api = {
    c, li,
    progress(pct, text) { lbf.style.width = pct + '%'; if (text) loadText.textContent = text; },
    set(full) { Object.assign(c, full); loadwrap.classList.add('hidden'); busy = false; paintMode(); draw(); updateDur(); },
    cleanEvent(evt) { if (cleaning) { loadwrap.classList.remove('hidden'); api.progress(evt.pct || 0, evt.text || ''); } },
  };
  api.progress(c.file ? 100 : 10);
  if (c.needsRestore) loadText.textContent = 'Getting the audio back on this PC...';
  if (c.file) { loadwrap.classList.add('hidden'); busy = false; }
  function paintMode() {
    modeBox.classList.toggle('hidden', !c.cleanFile);
    mOrig.classList.toggle('on', mode === 'orig'); mVocal.classList.toggle('on', mode === 'clean');
    cleanBtn.textContent = cleaning ? 'Cancel' : (c.cleanFile ? 'Redo clean up' : 'Clean up');
    mClean.textContent = cleaning ? 'Cancel clean up' : 'Clean up vocals';
  }
  function setMode(m) { if (m === 'clean' && !c.cleanFile) return; if (playing && playing.btn === playBtn) stopAudio(); mode = m; paintMode(); draw(); }
  mOrig.addEventListener('click', () => setMode('orig')); mVocal.addEventListener('click', () => setMode('clean'));

  function pps() { return canvas.clientWidth / (c.wideDur || 1); }
  function updateDur() { dur.textContent = fmtDur(c.trimEnd - c.trimStart); fs.style.left = (c.trimStart * pps()) + 'px'; fe.style.left = (c.trimEnd * pps()) + 'px'; }
  function draw() {
    const pk = mode === 'clean' && c.cleanPeaks ? c.cleanPeaks : c.peaks;
    if (!pk) return;
    const W = canvas.clientWidth, H = canvas.clientHeight; if (!W) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(W * dpr); canvas.height = Math.round(H * dpr);
    const g = canvas.getContext('2d'); g.scale(dpr, dpr); g.clearRect(0, 0, W, H);
    const rulerH = 16, mid = (H - rulerH) / 2, amp = mid - 4, p = pps();
    const n = pk.length;
    for (let x = 0; x < W; x++) {
      const t = x / p;
      const v = pk[Math.min(n - 1, Math.floor((x / W) * n))];
      const inside = t >= c.trimStart && t <= c.trimEnd;
      const inHit = t >= c.hitStart && t <= c.hitEnd;
      g.fillStyle = !inside ? '#3a3a3a' : inHit ? '#ffffff' : '#9a9a9a';
      const h = Math.max(1, v * amp);
      g.fillRect(x, mid - h, 1, h * 2);
    }
    // ruler
    g.fillStyle = '#141414'; g.fillRect(0, H - rulerH, W, rulerH);
    g.font = '10px "Segoe UI", sans-serif'; g.fillStyle = '#9a9a9a'; g.strokeStyle = '#555';
    const step = [1, 2, 5, 10, 30].find((s) => s * p >= 46) || 60;
    for (let t = 0; t <= c.wideDur; t += step) {
      const x = Math.round(t * p) + 0.5;
      g.beginPath(); g.moveTo(x, H - rulerH); g.lineTo(x, H - rulerH + 4); g.stroke();
      g.fillText(fmtClock(t), x + 3, H - 4);
    }
  }
  new ResizeObserver(() => { draw(); updateDur(); }).observe(canvas);

  // drag the finished excerpt out (never the whole video)
  const dragOut = (e) => { e.preventDefault(); if (!busy && cur()) window.yoink.startDrag(cur()); };
  canvas.addEventListener('dragstart', dragOut);
  handle.addEventListener('dragstart', dragOut);

  // trim flags: move freely, re-cut with ffmpeg on release so the dragged file matches
  let recutTimer = null;
  function bindFlag(flag, which) {
    flag.addEventListener('mousedown', (ev) => {
      if (busy) return;
      ev.preventDefault(); ev.stopPropagation();
      const rect = canvas.getBoundingClientRect();
      const move = (e) => {
        const t = Math.min(c.wideDur, Math.max(0, (e.clientX - rect.left) / pps()));
        if (which === 's') c.trimStart = Math.min(t, c.trimEnd - 0.05); else c.trimEnd = Math.max(t, c.trimStart + 0.05);
        draw(); updateDur();
      };
      const up = async () => {
        window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up);
        busy = true; handle.classList.add('busy'); dur.textContent = 'Cutting...';
        const r = await window.yoink.retrim({ wideFile: c.wideFile, cleanWide: c.cleanWide, wideDur: c.wideDur, trimStart: c.trimStart, trimEnd: c.trimEnd, format: c.format, videoId: c.videoId, winStart: c.winStart, title: c.title, phrase: opts.phrase || lastPhrase });
        busy = false; handle.classList.remove('busy');
        if (r.error) setStatus(r.error, true); else { c.file = r.file; if (r.cleanFile) c.cleanFile = r.cleanFile; c.trimStart = r.trimStart; c.trimEnd = r.trimEnd; if (favIds.has(c.cid)) window.yoink.favUpdate(c); }
        draw(); updateDur();
      };
      window.addEventListener('mousemove', move); window.addEventListener('mouseup', up);
    });
  }
  bindFlag(fs, 's'); bindFlag(fe, 'e');

  // preview
  function play() {
    if (busy || !cur()) return;
    if (playing && playing.btn === playBtn) { stopAudio(); return; }
    stopAudio();
    const a = new Audio(fileUrl(cur())); audio = a; a.volume = volume();
    playBtn.textContent = 'Stop';
    const stop = () => { playBtn.textContent = 'Play'; ph.style.display = 'none'; };
    playing = { btn: playBtn, stop };
    a.addEventListener('timeupdate', () => { ph.style.display = 'block'; ph.style.left = ((c.trimStart + a.currentTime) * pps()) + 'px'; });
    a.addEventListener('ended', () => stopAudio());
    a.addEventListener('error', () => { setStatus('Could not play that clip. Try dragging it into your DAW instead.', true); stopAudio(); });
    a.play().catch(() => {});
  }
  playBtn.addEventListener('click', play);
  canvas.addEventListener('click', play);
  const save = async () => { if (busy || !cur()) return; const r = await window.yoink.saveClip(cur()); if (r.error) setStatus(r.error, true); else if (r.saved) setStatus('Saved: ' + r.saved); };
  saveBtn.addEventListener('click', save);
  const openYT = () => window.yoink.openUrl(c.url);
  open.addEventListener('click', openYT);

  // card menu
  const closeMenu = () => menu.classList.add('hidden');
  menuBtn.addEventListener('click', (e) => { e.stopPropagation(); const was = menu.classList.contains('hidden'); document.querySelectorAll('.cmenu').forEach((m) => m.classList.add('hidden')); menu.classList.toggle('hidden', !was); });
  document.addEventListener('click', closeMenu);
  mSave.addEventListener('click', () => { closeMenu(); save(); }); mOpen.addEventListener('click', () => { closeMenu(); openYT(); });

  // Clean up: isolate the vocal. First use downloads the model (with progress), later uses are offline and cached.
  async function clean() {
    if (cleaning) { window.yoink.cleanCancel(c.cid); return; }
    if (busy || !c.wideFile) return;
    cleaning = true; busy = true; paintMode(); loadwrap.classList.remove('hidden'); api.progress(2, 'Starting...');
    cleanLive.set(c.cid, api);
    const r = await window.yoink.cleanStart({ cid: c.cid, wideFile: c.wideFile, wideDur: c.wideDur, trimStart: c.trimStart, trimEnd: c.trimEnd, format: c.format, videoId: c.videoId, winStart: c.winStart, title: c.title, phrase: opts.phrase || lastPhrase });
    cleanLive.delete(c.cid); cleaning = false; busy = false; loadwrap.classList.add('hidden');
    if (r.error) setStatus(r.error, true);
    else if (r.cancelled) setStatus('Clean up cancelled.');
    else { c.cleanWide = r.cleanWide; c.cleanFile = r.cleanFile; c.cleanPeaks = r.cleanPeaks; mode = 'clean'; setStatus(r.cached ? 'Clean version ready (from cache). Not listened to by Yoink, so check it by ear.' : 'Clean version ready. Not listened to by Yoink, so check it by ear.'); if (favIds.has(c.cid)) window.yoink.favUpdate(c); }
    paintMode(); draw(); updateDur();
  }
  cleanBtn.addEventListener('click', clean); mClean.addEventListener('click', () => { closeMenu(); clean(); });
  const keyBadge = el('span', 'badge key hidden');
  metaLine.appendChild(keyBadge);
  async function detectKey() {
    closeMenu(); if (busy || !c.wideFile) return;
    keyBadge.classList.remove('hidden'); keyBadge.textContent = 'Key: ...';
    const r = await window.yoink.detectKey({ wideFile: c.wideFile, trimStart: c.trimStart, trimEnd: c.trimEnd });
    if (r.error) { keyBadge.textContent = 'Key: unknown'; keyBadge.title = r.error; return; }
    keyBadge.textContent = 'Key ' + r.key + ' \u00B7 ' + r.camelot; keyBadge.title = 'Estimated from the clip audio (Camelot ' + r.camelot + '), ' + Math.round(r.confidence * 100) + '% sure. Short or spoken clips can be wrong.';
  }
  mKey.addEventListener('click', detectKey);
  api.play = play; api.clean = clean; api.save = save; api.key = detectKey;
  const paintHeart = () => { const on = favIds.has(c.cid); heart.textContent = on ? '\u2665' : '\u2661'; heart.classList.toggle('on', on); heart.title = on ? 'Remove from favorites' : 'Add to favorites'; };
  paintHeart();
  heart.addEventListener('click', async () => {
    if (busy) return;
    const r = await window.yoink.favToggle(Object.assign({}, c, { phrase: opts.phrase || lastPhrase }));
    if (r.error) { setStatus(r.error, true); return; }
    favIds = new Set(r.ids); document.querySelectorAll('li.card').forEach((x) => { x.dispatchEvent(new CustomEvent('favchange')); });
    if (opts.onFavChange) opts.onFavChange(r.favorite, li);
  });
  li.addEventListener('favchange', paintHeart);
  api.heart = () => heart.click();
  li._api = api;

  // tags (favorites only)
  if (tagBox) {
    const paintTags = () => {
      tagBox.replaceChildren();
      for (const t of c.tags || []) { const chip = el('span', 'tagchip', t); const x = el('button', 'tagx', '\u00D7'); x.title = 'Remove tag'; x.addEventListener('click', () => setTags((c.tags || []).filter((y) => y !== t))); chip.appendChild(x); tagBox.appendChild(chip); }
      const inp = el('input'); inp.type = 'text'; inp.className = 'taginp'; inp.placeholder = '+ tag'; inp.maxLength = 24;
      inp.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Enter' && inp.value.trim()) setTags([...(c.tags || []), inp.value.trim()]); });
      tagBox.appendChild(inp);
    };
    async function setTags(t) { const r = await window.yoink.favSetTags(c.cid, t); if (!r.error) { c.tags = r.tags; paintTags(); if (opts.onTags) opts.onTags(); } }
    paintTags();
    api.tagList = () => c.tags || [];
  }
  li.addEventListener('mousedown', () => selectCard(li));
  paintMode();
  api.draw = draw;
  requestAnimationFrame(() => { draw(); updateDur(); });
  return api;
}

// ---------------- filters ----------------
function readFilters() {
  return {
    cc: $('fCC').getAttribute('aria-pressed') === 'true', manual: $('fManual').getAttribute('aria-pressed') === 'true',
    language: $('fLanguage').value, category: $('fCategory').value, region: $('fRegion').value,
    channel: $('fChannel').value.trim(), title: $('fTitle').value.trim(), viewsMin: $('fViewsMin').value.trim(), viewsMax: $('fViewsMax').value.trim(),
    uploadFrom: $('fFrom').value.trim(), uploadTo: $('fTo').value.trim(),
  };
}
function writeFilters(f) {
  f = f || {};
  $('fCC').setAttribute('aria-pressed', String(!!f.cc)); $('fManual').setAttribute('aria-pressed', String(!!f.manual));
  $('fLanguage').value = f.language || 'any'; $('fCategory').value = f.category || 'all'; $('fRegion').value = f.region || 'any';
  $('fChannel').value = f.channel || ''; $('fTitle').value = f.title || '';
  $('fViewsMin').value = f.viewsMin != null ? f.viewsMin : ''; $('fViewsMax').value = f.viewsMax != null ? f.viewsMax : '';
  const ym = (x) => (x && x.y ? x.y + '/' + (x.m ? String(x.m).padStart(2, '0') : '') : '');
  $('fFrom').value = f.uploadFrom || ym(f.from).replace(/\/$/, ''); $('fTo').value = f.uploadTo || ym(f.to).replace(/\/$/, '');
  updateAdvBadge();
}
// Advanced filters: Channel, Title, Views, Uploaded. The badge counts the ones in use so a closed drop down never hides an active filter.
function advCount() {
  return (['fChannel', 'fTitle'].filter((id) => $(id).value.trim()).length) + (($('fViewsMin').value.trim() || $('fViewsMax').value.trim()) ? 1 : 0) + (($('fFrom').value.trim() || $('fTo').value.trim()) ? 1 : 0);
}
function updateAdvBadge() { const n = advCount(), b = $('advBadge'); b.textContent = String(n); b.classList.toggle('hidden', n === 0); b.title = n + ' advanced filter' + (n === 1 ? '' : 's') + ' in use'; }
for (const id of ['fChannel', 'fTitle', 'fViewsMin', 'fViewsMax', 'fFrom', 'fTo']) $(id).addEventListener('input', updateAdvBadge);
$('advFilters').addEventListener('toggle', () => { if (settings && settings.advOpen !== $('advFilters').open) window.yoink.setSettings({ advOpen: $('advFilters').open }).then((x) => { settings = x; }); });
function filterSummary(f) {
  const p = [];
  if (f.cc) p.push('Creative Commons'); if (f.manual) p.push('Manual subtitles');
  if (f.language && f.language !== 'any') p.push('Language: ' + f.language); if (f.category && f.category !== 'all') p.push(f.category);
  if (f.region && f.region !== 'any') p.push('Region: ' + f.region); if (f.channel) p.push('Channel: ' + f.channel); if (f.title) p.push('Title: ' + f.title);
  if (f.viewsMin != null || f.viewsMax != null) p.push('Views ' + (f.viewsMin != null ? f.viewsMin : '0') + '-' + (f.viewsMax != null ? f.viewsMax : 'max'));
  if (f.from || f.to || f.uploadFrom || f.uploadTo) p.push('Uploaded ' + (f.uploadFrom || (f.from ? f.from.y + '/' + (f.from.m || '') : '')) + ' to ' + (f.uploadTo || (f.to ? f.to.y + '/' + (f.to.m || '') : '')));
  return p.length ? p.join(' \u00B7 ') : 'No filters';
}
function validDate(s) { return !s || /^\d{4}([\/\-.]\d{1,2})?$/.test(s); }
for (const [id] of [['fCC'], ['fManual']]) $(id).addEventListener('click', () => $(id).setAttribute('aria-pressed', String($(id).getAttribute('aria-pressed') !== 'true')));
$('btnClearFilters').addEventListener('click', () => writeFilters({}));

// ---------------- search ----------------
// Load more has three states: hidden (searching), 'more' (shown), 'end' (the scan really ran out). It is shown after every run that is not exhausted,
// including Stop and errors, and as a last resort when a run ends without a done event.
let doneSeen = false, runToken = 0;
function setMore(m) { $('btnMore').classList.toggle('hidden', m !== 'more'); $('noMore').classList.toggle('hidden', m !== 'end'); }
function resetResults() { stopAudio(); results.innerHTML = ''; pending.clear(); cardApi.clear(); setMore('hidden'); $('count').classList.add('hidden'); }
async function startSearch(phrase, filters) {
  if (searching) return;
  phrase = (phrase || '').trim();
  if (!phrase) { setStatus('Type a phrase first.', true); return; }
  if (!validDate(filters.uploadFrom) || !validDate(filters.uploadTo)) { setStatus('Uploaded needs a date like 2019/04 (YYYY/MM).', true); return; }
  resetResults(); lastPhrase = phrase;
  $('empty').classList.add('hidden');
  setSearching(true); setStatus('Starting search...');
  const my = ++runToken; doneSeen = false;
  try { await window.yoink.search(phrase, filters); }
  finally { if (my === runToken) { setSearching(false); if (!doneSeen) setMore('more'); } }
}
$('searchForm').addEventListener('submit', (e) => { e.preventDefault(); startSearch($('phrase').value, readFilters()); });
$('btnStop').addEventListener('click', () => window.yoink.cancel());
$('btnFolder').addEventListener('click', () => window.yoink.openClipsFolder());
$('btnMore').addEventListener('click', async () => {
  if (searching) return;
  setMore('hidden'); setSearching(true); setStatus('Looking for more...');
  const my = ++runToken; doneSeen = false;
  try { await window.yoink.loadMore(); }
  finally { if (my === runToken) { setSearching(false); if (!doneSeen) setMore('more'); } }
});

function onClipEvent(evt) {
  if (evt.type === 'status') setStatus(evt.text);
  else if (evt.type === 'count') {
    const c = $('count'); c.classList.remove('hidden');
    c.textContent = 'Found ' + evt.matches.toLocaleString() + ' match' + (evt.matches === 1 ? '' : 'es');
    c.appendChild(el('small', null, 'in ' + evt.scanned.toLocaleString() + ' videos scanned'));
  } else if (evt.type === 'pending') {
    const api = makeCard(evt.clip, { phrase: lastPhrase });
    pending.set(evt.clip.cid, api); cardApi.set(evt.clip.cid, api); results.appendChild(api.li);
  } else if (evt.type === 'progress') { const a = pending.get(evt.cid); if (a) a.progress(evt.pct, evt.text); }
  else if (evt.type === 'clip') {
    const a = pending.get(evt.clip.cid);
    if (a) { a.set(evt.clip); pending.delete(evt.clip.cid); }
    else { const n = makeCard(evt.clip, { phrase: lastPhrase }); results.appendChild(n.li); cardApi.set(evt.clip.cid, n); }
  } else if (evt.type === 'dropped') { const a = pending.get(evt.cid); if (a) { a.li.remove(); pending.delete(evt.cid); cardApi.delete(evt.cid); } }
  else if (evt.type === 'error') setStatus(evt.text, true);
  else if (evt.type === 'done') {
    setStatus(evt.found && /^Found \d+ match/.test(evt.text) ? 'Done. Press Load more for more clips.' : evt.text, !evt.found && /YouTube|Could not|bot|rate|unavailable|tool/i.test(evt.text) && !/No exact/.test(evt.text));
    setSearching(false);
    doneSeen = true;
    setMore(evt.exhausted ? (results.children.length ? 'end' : 'hidden') : 'more');
    if (evt.stopped && !evt.exhausted) setStatus('Stopped. Press Load more to keep going.');
    if (!results.children.length) $('empty').classList.add('hidden');
  }
}
window.yoink.onEvent(onClipEvent);

// ---------------- panels: history, favorites ----------------
function openPanel(title, actions) { $('panelTitle').textContent = title; $('panelActions').replaceChildren(...(actions || [])); $('panelBody').replaceChildren(); $('panel').classList.remove('hidden'); }
function closePanel() { $('panel').classList.add('hidden'); $('panelBody').replaceChildren(); }
$('panelClose').addEventListener('click', closePanel);
document.addEventListener('keydown', (e) => { if (e.key === 'Escape') { closePanel(); $('settingsPanel').classList.add('hidden'); $('signinPanel').classList.add('hidden'); } });

// ---------------- keyboard shortcuts ----------------
// Space play/stop, J/K or Down/Up move between clips, F favorite, C clean up, S save, / or Ctrl+K (Cmd+K on a Mac) search, Ctrl+, (Cmd+,) settings, Esc close.
function cardList() { const open = !$('panel').classList.contains('hidden'); return [...(open ? $('panelBody') : results).querySelectorAll('li.card:not(.hidden)')]; }
const IS_MAC_UI = /Mac/i.test(navigator.platform);
const modKey = (e) => (IS_MAC_UI ? e.metaKey : (e.ctrlKey || e.metaKey));   // Cmd on a Mac (Ctrl+K is "delete to end of line" in a Mac text box), Ctrl on Windows
document.addEventListener('keydown', (e) => {
  const t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
  if (modKey(e) && e.key.toLowerCase() === 'k') { e.preventDefault(); closePanel(); $('settingsPanel').classList.add('hidden'); $('phrase').focus(); $('phrase').select(); return; }
  if (modKey(e) && e.key === ',') { e.preventDefault(); openSettings(); return; }
  if (typing || e.ctrlKey || e.metaKey || e.altKey || (e.key === ' ' && t && t.tagName === 'BUTTON')) return;
  if (!$('settingsPanel').classList.contains('hidden') || !$('signinPanel').classList.contains('hidden')) return;
  if (e.key === '/') { e.preventDefault(); $('phrase').focus(); return; }
  const list = cardList(); if (!list.length) return;
  let i = list.findIndex((x) => x.classList.contains('sel'));
  const move = (d) => { i = i < 0 ? 0 : Math.min(list.length - 1, Math.max(0, i + d)); selectCard(list[i]); list[i].scrollIntoView({ block: 'nearest' }); };
  const k = e.key.toLowerCase(), api = i >= 0 ? list[i]._api : null;
  if (k === 'j' || e.key === 'ArrowDown') { e.preventDefault(); move(1); }
  else if (k === 'k' || e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
  else if (e.key === ' ') { e.preventDefault(); if (!api) { move(0); return; } api.play(); }
  else if (api && k === 'f') api.heart();
  else if (api && k === 'c') api.clean();
  else if (api && k === 's') api.save();
});

async function showHistory() {
  const clear = el('button', 'ghost small', 'Clear all');
  openPanel('History', [clear]);
  const body = $('panelBody');
  const render = (items) => {
    body.replaceChildren();
    if (!items.length) { body.appendChild(el('div', 'emptyp', 'Your past searches show up here.')); return; }
    for (const h of items) {
      const row = el('div', 'hrow');
      const ph = el('div', 'ph'); ph.appendChild(el('b', null, '\u201C' + h.phrase + '\u201D')); ph.appendChild(el('div', null, new Date(h.time).toLocaleString() + ' \u00B7 ' + filterSummary(h.filters || {})));
      const run = el('button', 'small primary', 'Search again'); const del = el('button', 'small ghost', 'Remove');
      run.addEventListener('click', () => { closePanel(); $('phrase').value = h.phrase; writeFilters(h.filters); startSearch(h.phrase, readFilters()); });
      del.addEventListener('click', async () => render(await window.yoink.historyRemove(h.time)));
      row.append(ph, run, del); body.appendChild(row);
    }
  };
  clear.addEventListener('click', async () => render(await window.yoink.historyClear()));
  render(await window.yoink.historyList());
}
async function showFavorites() {
  openPanel('Favorites');
  const body = $('panelBody');
  const ul = el('ul', 'cards');
  const favs = await window.yoink.favList();
  favIds = new Set(favs.map((f) => f.cid));
  if (!favs.length) { body.appendChild(el('div', 'emptyp', 'Tap the heart on a clip to keep it here.')); return; }
  body.appendChild(ul);
  favApis.clear();
  const bar = el('div', 'tagbar'); body.insertBefore(bar, ul);
  let tagFilter = '';
  const applyTags = () => {
    const all = [...new Set([...favApis.values()].flatMap((a) => a.tagList ? a.tagList() : []))].sort();
    if (tagFilter && !all.includes(tagFilter)) tagFilter = '';
    bar.replaceChildren();
    if (all.length) {
      const mk = (t, label) => { const b = el('button', 'small ghost' + (tagFilter === t ? ' on' : ''), label); b.addEventListener('click', () => { tagFilter = t; applyTags(); }); bar.appendChild(b); };
      mk('', 'All'); for (const t of all) mk(t, t);
    }
    for (const a of favApis.values()) a.li.classList.toggle('hidden', !!tagFilter && !(a.tagList && a.tagList().includes(tagFilter)));
  };
  for (const c of favs) {
    const api = makeCard(c, { tags: true, onTags: applyTags, phrase: c.phrase || c.title, onFavChange: (on, li) => { if (!on) { favApis.delete(c.cid); li.remove(); applyTags(); if (!ul.children.length) { ul.remove(); bar.remove(); body.appendChild(el('div', 'emptyp', 'Tap the heart on a clip to keep it here.')); } } } });
    favApis.set(c.cid, api);
    ul.appendChild(api.li);
  }
  applyTags();
}
const favApis = new Map();
window.yoink.onFav((evt) => {
  const a = favApis.get(evt.cid); if (!a) return;
  if (evt.state === 'restoring') a.progress(40, 'Getting the audio back on this PC...');
  else if (evt.state === 'ready') a.set(Object.assign({}, evt.fav, { tags: evt.fav.tags || a.c.tags }));
  else if (evt.state === 'failed') a.progress(0, 'Could not restore this one: ' + (evt.text || 'unknown error') + ' Close and reopen Favorites to try again.');
});
window.yoink.onClean((evt) => { const a = cleanLive.get(evt.cid); if (a) a.cleanEvent(evt); });
$('btnHistory').addEventListener('click', showHistory);
$('btnFavs').addEventListener('click', showFavorites);

// ---------------- settings ----------------
async function openSettings() {
  settings = await window.yoink.getSettings();
  $('padBefore').value = settings.padBefore; $('padAfter').value = settings.padAfter; $('format').value = settings.format; $('maxClips').value = settings.maxClips; $('volume').value = settings.volume;
  const info = await window.yoink.info();
  $('ytVer').textContent = info.ytdlp || 'not found'; $('appVer').textContent = info.app; $('outDir').value = info.clipsDir;
  $('paths').textContent = 'Settings, favorites and history live in: ' + info.userData;
  $('defMsg').textContent = filterSummary(settings.defaultFilters || {}) === 'No filters' ? 'No default filters set.' : 'Defaults: ' + filterSummary(settings.defaultFilters);
  const ci = await window.yoink.cacheInfo(); $('cacheSize').textContent = (ci.bytes / 1048576).toFixed(1) + ' MB';
  $('normalize').checked = !!settings.normalize; $('fadeOut').checked = !!settings.fadeOut; $('autoUpdate').checked = settings.autoUpdateCheck !== false;
  paintModel(); paintAccount();
  $('settingsPanel').classList.remove('hidden');
}
async function saveSettings() {
  settings = await window.yoink.setSettings({ padBefore: $('padBefore').value, padAfter: $('padAfter').value, format: $('format').value, maxClips: $('maxClips').value, volume: $('volume').value, normalize: $('normalize').checked, fadeOut: $('fadeOut').checked, autoUpdateCheck: $('autoUpdate').checked });
  if (audio) audio.volume = volume();
}
$('btnSettings').addEventListener('click', openSettings);
$('btnClose').addEventListener('click', async () => { await saveSettings(); $('settingsPanel').classList.add('hidden'); });
for (const id of ['padBefore', 'padAfter', 'format', 'maxClips', 'volume', 'normalize', 'fadeOut', 'autoUpdate']) $(id).addEventListener('change', saveSettings);
$('btnChoose').addEventListener('click', async () => { const r = await window.yoink.chooseFolder(); if (r.outputDir) { const i = await window.yoink.info(); $('outDir').value = i.clipsDir; } });
$('btnSaveDefaults').addEventListener('click', async () => { const f = readFilters(); settings = await window.yoink.setSettings({ defaultFilters: f }); $('defMsg').textContent = 'Defaults: ' + filterSummary(settings.defaultFilters); });
$('btnResetDefaults').addEventListener('click', async () => { settings = await window.yoink.setSettings({ defaultFilters: {} }); $('defMsg').textContent = 'No default filters set.'; });
$('btnUpdate').addEventListener('click', async () => {
  $('btnUpdate').disabled = true; $('updMsg').textContent = 'Updating yt-dlp...';
  const r = await window.yoink.updateYtdlp();
  $('updMsg').textContent = r.message; $('ytVer').textContent = r.version || 'unknown';
  $('btnUpdate').disabled = false;
});
$('btnCheckUpdates').addEventListener('click', async () => {
  $('btnCheckUpdates').disabled = true; $('updMsg').textContent = 'Checking...'; $('btnDlPage').classList.add('hidden');
  const r = await window.yoink.checkForUpdates();
  $('btnCheckUpdates').disabled = false;
  if (r.status === 'available') { $('updMsg').textContent = 'Yoink ' + r.version + ' is available.'; showBanner(r); }
  else if (r.status === 'latest') $('updMsg').textContent = 'You are on the latest version.';
  else { $('updMsg').textContent = 'Could not reach the update server. Check your connection and try again.'; $('btnDlPage').classList.remove('hidden'); }
});
$('btnDlPage').addEventListener('click', () => window.yoink.openDownloadPage());

// ---------------- update banner ----------------
let bannerVer = '';
function showBanner(r) {
  bannerVer = r.version; $('updText').textContent = 'Yoink ' + r.version + ' is available' + (r.notes ? ': ' + r.notes : '');
  $('updText').title = r.notes || ''; $('updProg').classList.add('hidden'); $('btnUpdCancel').classList.add('hidden');
  for (const id of ['btnUpdNow', 'btnUpdLater', 'btnUpdSkip']) { $(id).classList.remove('hidden'); $(id).disabled = false; }
  $('updBanner').classList.remove('hidden');
}
$('btnUpdLater').addEventListener('click', () => $('updBanner').classList.add('hidden'));
$('btnUpdSkip').addEventListener('click', async () => { await window.yoink.skipUpdate(bannerVer); $('updBanner').classList.add('hidden'); });
$('btnUpdCancel').addEventListener('click', () => window.yoink.cancelUpdate());
$('btnUpdNow').addEventListener('click', async () => {
  for (const id of ['btnUpdNow', 'btnUpdLater', 'btnUpdSkip']) $(id).classList.add('hidden');
  $('btnUpdCancel').classList.remove('hidden'); $('updProg').classList.remove('hidden'); $('updBar').style.width = '0%'; $('updPct').textContent = '0%';
  $('updText').textContent = 'Downloading Yoink ' + bannerVer + '...';
  const r = await window.yoink.installUpdate();
  if (r.launched) { $('updText').textContent = 'Starting the installer. Yoink will restart on its own.'; $('btnUpdCancel').classList.add('hidden'); }
  else if (r.cancelled) showBanner({ version: bannerVer });
  else { $('updProg').classList.add('hidden'); $('btnUpdCancel').classList.add('hidden'); $('updText').textContent = r.error || 'The update did not work.'; $('btnUpdLater').classList.remove('hidden'); }
});
window.yoink.onUpdate((evt) => {
  if (evt.type === 'available') showBanner(evt);
  else if (evt.type === 'progress') { $('updBar').style.width = evt.pct + '%'; $('updPct').textContent = evt.pct + '%'; }
});

// ---------------- vocal model row ----------------
async function paintModel() {
  const st = await window.yoink.cleanStatus();
  $('modelInfo').textContent = st.ready ? 'installed (' + Math.round(st.modelBytes / 1048576) + ' MB)' : 'downloads on first Clean up (' + Math.round(st.modelBytes / 1048576) + ' MB)';
  $('btnModelRemove').classList.toggle('hidden', !st.ready);
}
$('btnModelRemove').addEventListener('click', async () => { await window.yoink.cleanRemove(); paintModel(); });

// ---------------- account and first-launch prompt ----------------
let acct = null;
function acctLabel(st) { return st.provider === 'demo' ? st.label + ' (demo)' : st.label; }
async function paintAccount() {
  acct = await window.yoink.syncStatus();
  const any = acct.available.google || acct.available.demo;
  $('acctBox').classList.remove('hidden');   // with no provider the section stays and says sign-in is coming soon
  $('acctName').textContent = acct.signedIn ? acctLabel(acct) : 'Guest';
  const b = $('acctBtns'); b.replaceChildren();
  if (acct.signedIn) {
    const now = el('button', 'ghost small', acct.syncing ? 'Syncing...' : 'Sync now'); const out = el('button', 'ghost small', 'Sign out');
    now.addEventListener('click', async () => { now.disabled = true; acct = await window.yoink.syncNow(); paintAccount(); });
    out.addEventListener('click', async () => { await window.yoink.signOut(); paintAccount(); });
    b.append(now, ' ', out);
  } else if (any) { const inb = el('button', 'ghost small', 'Sign in'); inb.addEventListener('click', () => showSignIn(true)); b.appendChild(inb); }
  const notes = [];
  if (acct.signedIn && acct.demo) notes.push('Demo account: favorites sync between profiles on this PC only. They do not leave this PC.');
  if (acct.signedIn && !acct.persistent) notes.push('Secure storage is not available here, so you stay signed in only until Yoink closes.');
  if (acct.signedIn && acct.lastSync) notes.push('Last synced ' + new Date(acct.lastSync).toLocaleTimeString() + '.');
  if (acct.error) notes.push(acct.error);
  if (!acct.signedIn) notes.push(any ? 'Signing in is optional. Favorites stay on this PC as a guest.' : 'Sign-in coming soon. For now favorites stay on this PC.');
  $('acctNote').textContent = notes.join(' ');
}
async function showSignIn(fromSettings) {
  const st = await window.yoink.syncStatus();
  const box = $('signinOpts'); box.replaceChildren(); $('signinMsg').textContent = '';
  const run = async (provider, arg) => {
    $('signinMsg').textContent = provider === 'google' ? 'Finish signing in in your browser...' : 'Signing in...';
    const r = await window.yoink.signIn(provider, arg);
    if (r.error || !r.signedIn) { $('signinMsg').textContent = r.error || 'Sign in did not finish.'; return; }
    $('signinPanel').classList.add('hidden'); paintAccount();
    if (!$('panel').classList.contains('hidden') && $('panelTitle').textContent === 'Favorites') showFavorites();
  };
  if (st.available.google) { const g = el('button', 'primary', 'Sign in with Google'); g.addEventListener('click', () => run('google')); box.appendChild(g); box.appendChild(el('div', 'small', 'Your favorites are kept in a private app folder in your own Google Drive. Nobody else can see them.')); }
  if (st.available.demo) {
    const row = el('div', 'row tight demo'); const inp = el('input'); inp.type = 'text'; inp.placeholder = 'Demo name or email'; inp.maxLength = 60;
    const go = el('button', 'ghost', 'Try demo sign-in'); go.addEventListener('click', () => run('demo', inp.value)); inp.addEventListener('keydown', (e) => { e.stopPropagation(); if (e.key === 'Enter') go.click(); });
    row.append(inp, go); box.appendChild(row); box.appendChild(el('div', 'small', 'Demo only: this does not sync between PCs yet. It keeps favorites per name on this PC so you can try the feature.'));
  }
  $('signinPanel').classList.remove('hidden'); $('btnGuest').focus();
}
$('btnGuest').addEventListener('click', () => $('signinPanel').classList.add('hidden'));
window.yoink.onSync((evt) => { if (evt.type === 'status' && !$('settingsPanel').classList.contains('hidden')) paintAccount(); });
$('btnClearCache').addEventListener('click', async () => { await window.yoink.cacheClear(); $('cacheSize').textContent = '0.0 MB'; });
$('btnMute').addEventListener('click', () => { const m = $('btnMute'); const on = m.dataset.muted !== '1'; m.dataset.muted = on ? '1' : '0'; m.textContent = on ? '\u{1F507}' : '\u{1F50A}'; if (audio) audio.volume = volume(); });

// ---------------- boot ----------------
(async () => {
  meta = await window.yoink.meta();
  settings = await window.yoink.getSettings();
  const fill = (id, first, items) => { const s = $(id); s.append(new Option(first[1], first[0])); for (const [v, n] of items) s.append(new Option(n, v)); };
  fill('fLanguage', ['any', 'Any'], meta.languages); fill('fRegion', ['any', 'Any'], meta.regions);
  fill('fCategory', ['all', 'All'], meta.categories.map((c) => [c, c]));
  writeFilters(settings.defaultFilters);
  $('advFilters').open = !!settings.advOpen;
  favIds = new Set((await window.yoink.favList()).map((f) => f.cid));
  // First launch of this version (also after an update): offer sign in once, then never again. Guest is the default.
  try {
    const st = await window.yoink.syncStatus();
    if (!settings.signInPrompted && (st.signedIn || st.available.google || st.available.demo)) {   // with no provider configured nothing is shown and the flag stays unset
      if (!st.signedIn) await showSignIn(false);
      settings = await window.yoink.setSettings({ signInPrompted: true });
    }
  } catch (_) {}
})();
