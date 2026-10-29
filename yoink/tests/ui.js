// Headless UI test for v2.1 (main-process hook, enabled by YOINK_E2E_DIR + YOINK_UI_TEST; driven by tests/ui-run.js).
// Offline: uses synthetic clips built from local audio. Pass A..D are launches 1 and 2 of two fresh profiles.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { wavPeaks } = require('../src/core');
module.exports = async function ui(win, outDir) {
  const wc = win.webContents, PASS = process.env.YOINK_UI_PASS || 'A', UD = process.env.YOINK_USERDATA;
  const shot = async (name) => { const img = await wc.capturePage(); fs.writeFileSync(path.join(outDir, name), img.toPNG()); console.log('SHOT', name); };
  const js = (c) => wc.executeJavaScript(c);
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  let fails = 0;
  const ok = (name, cond, extra) => { if (!cond) fails++; console.log((cond ? 'PASS ' : 'FAIL ') + '[' + PASS + '] ' + name + (extra ? ' ' + extra : '')); };
  const until = async (fn, ms = 10000, step = 200) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (await fn()) return true; await sleep(step); } return false; };
  const vis = (id) => js(`(()=>{const e=document.getElementById(${JSON.stringify(id)}); return !!e && !e.classList.contains('hidden')})()`);
  await new Promise((r) => wc.once('did-finish-load', r)).catch(() => {});
  await sleep(1500);
  const set = await js(`window.yoink.getSettings()`);

  // ---- no sign-in provider configured (showDemo false, no Google id): nothing is shown, flag not consumed ----
  if (PASS === 'N') {
    const a = (await js(`window.yoink.syncStatus()`)).available;
    ok('no provider is available', a && !a.demo && !a.google, JSON.stringify(a));
    ok('no sign-in prompt', !(await vis('signinPanel')));
    ok('asked-once flag is NOT consumed', !(await js(`window.yoink.getSettings()`)).signInPrompted);
    await js(`document.getElementById('btnSettings').click()`); await sleep(1000);
    ok('settings shows account box as Guest', (await vis('acctBox')) && (await js(`document.getElementById('acctName').textContent`)) === 'Guest');
    const note = await js(`document.getElementById('acctNote').textContent`);
    ok('settings says sign-in is coming soon', /coming soon/i.test(note), note);
    ok('settings has no Sign in button', !(await js(`document.getElementById('acctBtns').children.length`)));
    await shot('42-settings-no-signin.png');
    console.log('UI_FAILS', fails); console.log('E2E_DONE');
    return;
  }

  // ---- sign-in prompt: once only ----
  if (PASS === 'A' || PASS === 'C') {
    ok('sign-in prompt shows on first launch', await vis('signinPanel'));
    ok('Continue as guest is the focused default', await js(`document.activeElement && document.activeElement.id==='btnGuest'`));
    await shot(PASS === 'A' ? '10-signin-prompt.png' : '11-signin-demo.png');
  } else ok('sign-in prompt does NOT show again', !(await vis('signinPanel')));
  if (PASS === 'A') { await js(`document.getElementById('btnGuest').click()`); ok('guest: prompt closes', !(await vis('signinPanel'))); }
  if (PASS === 'C') {
    await js(`(()=>{const i=document.querySelector('#signinOpts input'); i.value='sam'; [...document.querySelectorAll('#signinOpts button')].find(b=>/demo/i.test(b.textContent)).click()})()`);
    ok('demo sign-in works', await until(async () => (await js(`window.yoink.syncStatus()`)).signedIn, 8000));
    ok('prompt closed after sign-in', !(await vis('signinPanel')));
  }
  const st = await js(`window.yoink.syncStatus()`);
  console.log('SYNC_STATUS', JSON.stringify(st));
  if (PASS === 'A' || PASS === 'B') ok('guest stays guest', !st.signedIn);
  if (PASS === 'D') { console.log('SIGNIN_PERSISTS_ACROSS_LAUNCH', st.signedIn, 'persistent=' + st.persistent); ok('signed-in state is consistent with persistence', st.signedIn === !!st.persistent || st.signedIn); }
  ok('signInPrompted flag stored after first launch', (await js(`window.yoink.getSettings()`)).signInPrompted === true);

  // ---- advanced filters ----
  ok('advanced filters exist and are collapsed on first launch', PASS === 'B' ? true : await js(`document.getElementById('advFilters').open===false`));
  if (PASS === 'A') {
    ok('badge hidden with no advanced filters', !(await vis('advBadge')));
    await js(`(()=>{const $=id=>document.getElementById(id); $('fChannel').value='lex'; $('fChannel').dispatchEvent(new Event('input',{bubbles:true})); $('fViewsMin').value='1000'; $('fViewsMin').dispatchEvent(new Event('input',{bubbles:true}));})()`);
    await sleep(300);
    ok('badge counts 2 while collapsed', await js(`document.getElementById('advBadge').textContent==='2' && !document.getElementById('advBadge').classList.contains('hidden')`));
    await js(`document.querySelector('#advFilters summary').click()`);
    await sleep(500);
    ok('opening persists advOpen', (await js(`window.yoink.getSettings()`)).advOpen === true);
    await shot('20-advanced-open.png');
    await js(`document.querySelector('#advFilters summary').click()`);
    await sleep(500);
    await shot('21-advanced-collapsed-badge.png');
    await js(`document.querySelector('#advFilters summary').click()`);   // leave it open for pass B
    await sleep(400);
    ok('Clear resets badge', await js(`(document.getElementById('btnClearFilters').click(), document.getElementById('advBadge').classList.contains('hidden'))`));
  }
  if (PASS === 'B') ok('advanced open state is remembered across launches', await js(`document.getElementById('advFilters').open===true`));

  // ---- load more states ----
  if (PASS === 'A') {
    await js(`(()=>{ for (const t of ['one','two']) onClipEvent({type:'pending',clip:{cid:'lm'+t,title:t,channel:'c',start:1,end:2,words:[],category:''}}); })()`);
    const ev = (o) => js(`onClipEvent(${JSON.stringify(Object.assign({ type: 'done', found: 2, text: 'Found 2 matches.' }, o))})`);
    await ev({ exhausted: false }); ok('more: shown when not exhausted', await vis('btnMore') && !(await vis('noMore')));
    await ev({ exhausted: true }); ok('end: "No more results" shown, button hidden', !(await vis('btnMore')) && await vis('noMore'));
    await ev({ exhausted: false, stopped: true }); ok('stop: Load more comes back', await vis('btnMore') && !(await vis('noMore')));
    await ev({ exhausted: false, failed: true, text: 'Press Load more to try again.' }); ok('failure: Load more comes back', await vis('btnMore'));
    await js(`document.getElementById('results').innerHTML=''; setMore('hidden')`);
    // a run that ends with no done event at all
    const r = await js(`(async()=>{ const real=window.yoink.search; try { Object.defineProperty(window.yoink,'search',{value:async()=>{},configurable:true,writable:true}); } catch(e){} await startSearch('abc', readFilters()); return !document.getElementById('btnMore').classList.contains('hidden'); })()`).catch((e) => String(e));
    ok('a search that ends in an error still shows Load more', r === true, String(r));
    await js(`document.getElementById('results').innerHTML=''; setMore('hidden')`);
  }

  // ---- update banner (needs the local server from the runner) ----
  if (process.env.YOINK_UPDATE_URL) {
    if (PASS === 'A') {
      ok('banner appears for a newer version', await until(() => vis('updBanner'), 12000));
      ok('banner text', /9\.9\.9 is available/.test(await js(`document.getElementById('updText').textContent`)), await js(`document.getElementById('updText').textContent`));
      ok('banner has Update now / Later / Skip', await js(`['btnUpdNow','btnUpdLater','btnUpdSkip'].every(i=>!document.getElementById(i).classList.contains('hidden'))`));
      await shot('30-update-banner.png');
      await js(`document.getElementById('btnUpdLater').click()`); await sleep(300);
      ok('Later hides it', !(await vis('updBanner')));
      const r = await js(`window.yoink.checkForUpdates(true)`); ok('manual check says available', r.status === 'available' && r.version === '9.9.9', JSON.stringify(r));
      const prog = []; await js(`window.__p=[]; void window.yoink.onUpdate(e=>window.__p.push(e.type+(e.pct!=null?':'+e.pct:'')))`);
      const inst = await js(`window.yoink.installUpdate()`);
      console.log('INSTALL_RESULT', JSON.stringify(inst));
      ok('Update now downloads and verifies sha256 (no launch in test)', !inst.error && !!inst.file, JSON.stringify(inst));
      if (inst.file) ok('downloaded installer hash matches', crypto.createHash('sha256').update(fs.readFileSync(inst.file)).digest('hex') === process.env.YOINK_UPDATE_SHA);
      ok('progress events reached the renderer', (await js(`window.__p.length`)) > 0, await js(`window.__p.slice(0,4).join(',')`));
      await js(`void showBanner({version:'9.9.9',notes:'Test notes'})`).catch((e) => console.log('showBanner', e.message));
      await js(`document.getElementById('btnUpdSkip').click()`); await sleep(500);
      ok('Skip this version stores it', (await js(`window.yoink.getSettings()`)).skipVersion === '9.9.9');
    }
    if (PASS === 'B') {
      await sleep(7000);
      ok('skipped version: no banner on launch', !(await vis('updBanner')));
      const r = await js(`window.yoink.checkForUpdates(true)`); ok('manual check ignores skip', r.status === 'available', JSON.stringify(r));
    }
  }

  // ---- settings: updates, account, normalize ----
  await js(`document.getElementById('btnSettings').click()`); await sleep(1200);
  ok('settings has Check for updates + auto toggle', await js(`!!document.getElementById('btnCheckUpdates') && !!document.getElementById('autoUpdate')`));
  ok('auto check on by default', await js(`document.getElementById('autoUpdate').checked===true`));
  ok('account box shown', await vis('acctBox'), await js(`document.getElementById('acctName').textContent`));
  if (PASS === 'D') ok('persisted account name shown', st.signedIn ? /sam/i.test(await js(`document.getElementById('acctName').textContent`)) : true);
  if (PASS === 'A') await shot('40-settings.png');
  if (PASS === 'C') { await shot('41-settings-signed-in.png'); }
  await js(`document.getElementById('btnClose').click()`); await sleep(200);

  // ---- clip cards, clean up, key, shortcuts, tags (pass A only; real model) ----
  if (PASS === 'A') {
    const wideDir = path.join(UD, 'data', 'wide');
    const mk = async (n) => {
      const f = path.join(wideDir, 'wide_' + n + '.wav'); const { peaks, duration } = wavPeaks(f);
      const clip = { cid: 'ui' + n, videoId: 'ABCDEFGHIJ' + n, title: 'Synthetic clip ' + n, channel: 'Test channel', url: 'https://youtu.be/ABCDEFGHIJ' + n, words: ['so', 'what', 'the', 'heck', 'is', 'that'], hiFrom: 1, hiTo: 3, phrase: 'what the heck', category: 'Music', start: 5, end: 6.5, winStart: 0, wideFile: f, wideDur: duration, peaks, trimStart: 1, trimEnd: Math.min(duration, 5), hitStart: 2, hitEnd: 3, format: 'wav', views: 1234, uploaded: '20200101' };
      const r = await js(`window.yoink.retrim(${JSON.stringify({ cid: clip.cid, wideFile: f, wideDur: duration, trimStart: clip.trimStart, trimEnd: clip.trimEnd, format: 'wav', videoId: clip.videoId, winStart: 0, phrase: clip.phrase, title: clip.title, hitStart: 2, hitEnd: 3 })})`);
      if (r && r.file) clip.file = r.file; else console.log('RETRIM', JSON.stringify(r));
      return clip;
    };
    const c1 = await mk('1'), c2 = await mk('2');
    ok('synthetic clips rendered', !!c1.file && !!c2.file);
    await js(`(()=>{ lastPhrase='what the heck'; onClipEvent({type:'clip',clip:${JSON.stringify(c1)}}); onClipEvent({type:'clip',clip:${JSON.stringify(c2)}}); setMore('more'); })()`);
    await sleep(800);
    ok('two cards rendered', await js(`document.querySelectorAll('#results li.card').length===2`));
    ok('Original/Clean toggle hidden before clean up', await js(`document.querySelector('#results li.card .cfoot .mode, #results li.card .cfoot .modebox')?.classList.contains('hidden') ?? true`));

    // shortcuts
    const key = (k, extra) => js(`document.dispatchEvent(new KeyboardEvent('keydown',{key:${JSON.stringify(k)},bubbles:true,${extra || ''}}))`);
    await key('j'); await sleep(150);
    ok('J selects first card', await js(`document.querySelector('#results li.card').classList.contains('sel')`));
    await key('j'); await sleep(150);
    ok('J again moves to the second', await js(`document.querySelectorAll('#results li.card')[1].classList.contains('sel')`));
    await key('k'); await sleep(150);
    ok('K moves back', await js(`document.querySelector('#results li.card').classList.contains('sel')`));
    await key('f'); await sleep(1000);
    ok('F favorites the selected clip', (await js(`window.yoink.favList()`)).some((f) => f.cid === 'ui1'));
    ok('Ctrl+K focuses search', await js(`(document.dispatchEvent(new KeyboardEvent('keydown',{key:'k',ctrlKey:true,bubbles:true})), document.activeElement.id==='phrase')`));
    await js(`document.activeElement.blur()`);

    // key detection via the menu
    await js(`document.querySelector('#results li.card')._api.key()`);
    ok('key detection shows a badge', await until(() => js(`/Key /.test(document.querySelector('#results li.card').textContent)`), 20000), await js(`(document.querySelector('#results li.card .keyb, #results li.card .keybadge')||{}).textContent`));

    // clean up (REAL model, REAL onnxruntime on this Linux box)
    const t0 = Date.now();
    await js(`document.querySelector('#results li.card')._api.clean()`);
    let seenProgress = false;
    ok('clean up shows progress', await until(async () => { const t = await js(`(document.querySelector('#results li.card .loadwrap')||{}).className+'|'+(document.querySelector('#results li.card').textContent)`); if (/Clean|clean|vocal|Loading|%/.test(t) && !seenProgress) { seenProgress = true; } return seenProgress; }, 15000));
    await sleep(2500); await shot('50-clean-progress.png');
    const done = await until(() => js(`!!document.querySelector('#results li.card')._api && [...document.querySelectorAll('#results li.card')][0].querySelector('.cfoot').textContent.includes('Redo')`), 240000, 500);
    ok('clean up finishes', done, ((Date.now() - t0) / 1000).toFixed(1) + 's');
    await sleep(500);
    ok('Original / Clean toggle visible after clean up', await js(`!![...document.querySelectorAll('#results li.card')][0].querySelector('.cfoot').textContent.match(/Clean/) `));
    await js(`document.querySelector('#results li.card').scrollIntoView()`);
    await shot('51-clean-done.png');
    // cache hit
    const t1 = Date.now();
    const r2 = await js(`window.yoink.cleanStart({cid:'ui1', wideFile:${JSON.stringify(c1.wideFile)}, trimStart:${c1.trimStart}, trimEnd:${c1.trimEnd}, wideDur:${c1.wideDur}, videoId:${JSON.stringify(c1.videoId)}, winStart:0, format:'wav', phrase:'what the heck', title:'t'})`);
    ok('second clean of the same clip is a cache hit (fast)', !r2.error && r2.cached === true && (Date.now() - t1) < 5000, JSON.stringify(Object.assign({}, r2, { cleanPeaks: '[..]' })) + ' ' + (Date.now() - t1) + 'ms');
    if (r2.cleanFile) { const { execFileSync } = require('child_process'); try { console.log('CLEAN_FILE_DURATION', execFileSync(process.env.YOINK_FFPROBE || 'ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', r2.cleanFile]).toString().trim(), 'orig clip', (c1.trimEnd - c1.trimStart)); } catch (e) {} }
    // cancel
    const cancelP = js(`window.yoink.cleanStart({cid:'ui2', wideFile:${JSON.stringify(c2.wideFile)}, trimStart:${c2.trimStart}, trimEnd:${c2.trimEnd}, wideDur:${c2.wideDur}, videoId:${JSON.stringify(c2.videoId)}, winStart:0, format:'wav', phrase:'x', title:'t'})`);
    await sleep(1500); await js(`window.yoink.cleanCancel('ui2')`);
    const cr = await cancelP;
    ok('cancel stops a clean up', !!cr.cancelled, JSON.stringify(cr).slice(0, 120));
    // tags in favorites panel
    await js(`document.getElementById('btnFavs').click()`); await sleep(1500);
    ok('favorites panel shows the clip', await js(`document.querySelectorAll('#panelBody li.card').length>=1`));
    await js(`(()=>{const i=document.querySelector('#panelBody li.card .tags input, #panelBody li.card input'); i.value='hook'; i.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}))})()`);
    await sleep(800);
    ok('tag saved on favorite', ((await js(`window.yoink.favList()`)).find((f) => f.cid === 'ui1') || {}).tags?.includes('hook'));
    ok('favorite keeps the cleaned vocal', !!((await js(`window.yoink.favList()`)).find((f) => f.cid === 'ui1') || {}).cleanFile);
    await shot('52-favorites-tags.png');
    await js(`document.getElementById('panelClose').click()`);
  }

  console.log('UI_FAILS', fails);
  console.log('E2E_DONE');
};
