// Headless UI test hook. Enabled only when YOINK_E2E_DIR is set (never in normal use).
// Drives the real renderer: filters, search, cards, trim flags (re-cut), favorites, history, settings, load more.
const fs = require('fs');
const path = require('path');
module.exports = async function e2e(win, outDir, phrase) {
  const wc = win.webContents;
  const shot = async (name) => { const img = await wc.capturePage(); fs.writeFileSync(path.join(outDir, name), img.toPNG()); console.log('SHOT', name); };
  const js = (c) => wc.executeJavaScript(c);
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const ok = (name, cond, extra) => console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra ? ' ' + extra : ''));
  const waitIdle = async (max = 150) => {
    for (let i = 0; i < max; i++) { await sleep(1000); if (i > 3 && await js(`document.getElementById('btnStop').classList.contains('hidden')`)) return true; }
    return false;
  };
  await new Promise((r) => wc.once('did-finish-load', r)).catch(() => {});
  await sleep(1000);
  await shot('01-signin-prompt.png');
  await js(`(document.getElementById('btnGuest')||{click(){}}).click()`);
  await sleep(300);
  await shot('01-start.png');

  // filters: fill them in like a user would
  await js(`(()=>{ const $=id=>document.getElementById(id);
    $('fManual').click(); $('fLanguage').value='en'; $('fCategory').value='Music'; $('fViewsMin').value='1000'; $('fFrom').value='2010/01'; $('fTo').value='2026/12'; })()`);
  await shot('02-filters.png');
  const filt = await js(`JSON.stringify(readFilters())`);
  console.log('FILTERS', filt);
  await js(`document.getElementById('btnClearFilters').click()`);
  ok('clear filters', (await js(`readFilters().manual===false && readFilters().category==='all'`)));

  await js(`window.yoink.setSettings({maxClips:3})`);
  await js(`document.getElementById('phrase').value=${JSON.stringify(phrase)}; document.getElementById('searchForm').requestSubmit();`);
  let shotMid = false;
  for (let i = 0; i < 150; i++) {
    await sleep(700);
    const n = await js(`document.querySelectorAll('#results li.card').length`);
    if (n >= 1 && !shotMid) { shotMid = true; await shot('03-first-card-loading.png'); }
    if (i > 5 && await js(`document.getElementById('btnStop').classList.contains('hidden')`)) break;
  }
  await sleep(500);
  console.log('COUNT', await js(`document.getElementById('count').textContent`));
  console.log('STATUS', await js(`document.getElementById('status').textContent`));
  const cards = await js(`[...document.querySelectorAll('#results li.card')].map(li=>({chip:(li.querySelector('.chip')||{}).textContent, title:li.querySelector('.ctitle').textContent, line:li.querySelector('.cline').textContent, dur:li.querySelector('.dur').textContent, loading:!li.querySelector('.loadwrap').classList.contains('hidden'), draggable:li.querySelector('canvas').draggable, handle:li.querySelector('.handle').draggable}))`);
  console.log('CARDS', JSON.stringify(cards, null, 1));
  ok('cards shown', cards.length >= 1);
  ok('cards finished loading', cards.every((c) => !c.loading));
  ok('drag handle + waveform draggable', cards.every((c) => c.draggable && c.handle));
  ok('Load more visible', await js(`!document.getElementById('btnMore').classList.contains('hidden')`));
  await shot('04-results.png');

  // play preview
  const played = await js(`new Promise(res=>{const b=document.querySelector('#results li.card .cfoot .play'); b.click(); setTimeout(()=>res(b.textContent),900)})`);
  ok('Play button toggles to Stop', played === 'Stop', played);
  await shot('05-playing.png');
  await js(`document.querySelector('#results li.card .cfoot .play').click()`);

  // trim flags: drag the start flag right and the end flag left, then wait for the re-cut
  const before = await js(`(()=>{const li=document.querySelector('#results li.card'); return {dur:li.querySelector('.dur').textContent, file:null}})()`);
  const drag = (which, dx) => js(`new Promise(res=>{
    const li=document.querySelector('#results li.card'); const f=li.querySelector('.flag.${which}'); const r=f.getBoundingClientRect();
    f.dispatchEvent(new MouseEvent('mousedown',{bubbles:true,clientX:r.left,clientY:r.top+20}));
    window.dispatchEvent(new MouseEvent('mousemove',{clientX:r.left+(${dx}),clientY:r.top+20}));
    window.dispatchEvent(new MouseEvent('mouseup',{clientX:r.left+(${dx}),clientY:r.top+20}));
    setTimeout(res,3500)})`);
  await drag('s', 40); await drag('e', -40);
  const after = await js(`document.querySelector('#results li.card .dur').textContent`);
  ok('trim changes duration', before.dur !== after, before.dur + ' -> ' + after);
  await shot('06-trimmed.png');
  const trimmed = await js(`(()=>{ const a=[...document.querySelectorAll('#results li.card')][0]; return a.querySelector('.dur').textContent })()`);
  // verify the dragged file is the re-cut one: fav a card and check file duration through IPC
  await js(`document.querySelector('#results li.card .heart').click()`);
  await sleep(800);
  ok('heart turns on', await js(`document.querySelector('#results li.card .heart').classList.contains('on')`));
  const favs = await js(`window.yoink.favList()`);
  ok('favorite saved', favs.length === 1, favs[0] && favs[0].file);
  if (favs[0]) {
    const { execFileSync } = require('child_process');
    let d = '?';
    try { d = execFileSync(process.env.YOINK_FFPROBE || 'ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', favs[0].file]).toString().trim(); } catch (e) { d = 'ffprobe n/a'; }
    console.log('FAVORITE_FILE_DURATION', d, 'UI', trimmed);
  }
  await js(`document.getElementById('btnFavs').click()`);
  await sleep(1500);
  await shot('07-favorites.png');
  ok('favorite card in panel', await js(`document.querySelectorAll('#panelBody li.card').length===1`));
  await js(`document.getElementById('panelClose').click()`);

  await js(`document.getElementById('btnHistory').click()`);
  await sleep(500);
  await shot('08-history.png');
  ok('history has this search', await js(`document.querySelectorAll('#panelBody .hrow').length>=1`));
  await js(`document.getElementById('panelClose').click()`);

  await js(`document.getElementById('btnSettings').click()`);
  await sleep(1500);
  await shot('09-settings.png');
  console.log('YTVER', await js(`document.getElementById('ytVer').textContent`), 'APPVER', await js(`document.getElementById('appVer').textContent`));
  await js(`document.getElementById('btnClose').click()`);

  // load more
  const n0 = await js(`document.querySelectorAll('#results li.card').length`);
  await js(`document.getElementById('btnMore').click()`);
  await waitIdle();
  const n1 = await js(`document.querySelectorAll('#results li.card').length`);
  ok('Load more adds cards', n1 > n0, n0 + ' -> ' + n1);
  console.log('COUNT2', await js(`document.getElementById('count').textContent`));
  await shot('10-load-more.png');
  console.log('E2E_DONE');
};
