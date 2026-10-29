'use strict';
const { app, BrowserWindow, ipcMain, dialog, shell, nativeImage, safeStorage, Menu } = require('electron');
const os = require('os');
const path = require('path');
const fs = require('fs');
const { Yoink, run, downloadTo, normFilters, wavPeaks, CATEGORIES, LANGUAGES, REGIONS } = require('./core');
const updater = require('./updater');
const { VocalCleaner, readWav } = require('./vocal');
const { detectKey } = require('./keydetect');
const sync = require('./sync');

const IS_WIN = process.platform === 'win32';
const IS_MAC = process.platform === 'darwin';
const DOWNLOAD_PAGE = IS_MAC ? 'https://cmplxty.music/yoink.dmg' : 'https://cmplxty.music/yoink.exe';
let win = null;
let yoink = null;
let currentAbort = null;
let session = null;      // the running or last search, so "Load more" can continue it
let settings = null;

if (process.env.YOINK_USERDATA) app.setPath('userData', process.env.YOINK_USERDATA);
if (IS_WIN) app.setAppUserModelId('com.cmplxty.yoink');

const DEFAULTS = { padBefore: 0.3, padAfter: 0.3, format: 'wav', maxClips: 12, saveDir: '', outputDir: '', volume: 0.9, defaultFilters: {},
  autoUpdateCheck: true, skipVersion: '', lastUpdateCheck: 0, advOpen: false, signInPrompted: false, normalize: false, fadeOut: false };

function exe(name) { return IS_WIN ? name + '.exe' : name; }

// Find a bundled tool. Windows build: <install folder>\resources\bin\yt-dlp.exe
function bundled(name) {
  const cands = [
    process.env['YOINK_' + name.toUpperCase().replace('-', '')],
    path.join(process.resourcesPath || '', 'bin', exe(name)),
    path.join(__dirname, '..', 'bin', exe(name)),
  ].filter(Boolean);
  for (const c of cands) if (fs.existsSync(c)) return c;
  return null;
}

const userFile = (n) => path.join(app.getPath('userData'), n);
function readJson(file, dflt) { try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch (_) { return dflt; } }
function writeJson(file, obj) { fs.mkdirSync(path.dirname(file), { recursive: true }); const tmp = file + '.tmp'; fs.writeFileSync(tmp, JSON.stringify(obj, null, 2)); fs.renameSync(tmp, file); }
function loadSettings() { return Object.assign({}, DEFAULTS, readJson(userFile('settings.json'), {})); }
function saveSettings() { writeJson(userFile('settings.json'), settings); }

function clipsDir() {
  if (process.env.YOINK_CLIPS_DIR) return process.env.YOINK_CLIPS_DIR;
  if (settings && settings.outputDir) return settings.outputDir;
  return path.join(app.getPath('documents'), 'Yoink Clips');
}

// yt-dlp lives in a writable copy under the user's data folder so it can update itself.
function ytdlpPath() {
  const writable = path.join(app.getPath('userData'), 'bin', exe('yt-dlp'));
  const src = bundled('yt-dlp');
  // First run, or first run of a newer Yoink: put the bundled yt-dlp in the writable folder (so it can self-update).
  // A newer Yoink replaces an older copy, but never replaces a copy that was updated later than the bundled one.
  if (src) {
    let copy = !fs.existsSync(writable);
    if (!copy && settings.lastVersion !== app.getVersion()) { try { copy = fs.statSync(src).mtimeMs > fs.statSync(writable).mtimeMs; } catch (_) { copy = true; } }
    if (copy) {
      try { fs.mkdirSync(path.dirname(writable), { recursive: true }); fs.copyFileSync(src, writable); try { fs.chmodSync(writable, 0o755); } catch (_) {} } catch (_) { return src; }
    }
  }
  if (settings.lastVersion !== app.getVersion()) { settings.lastVersion = app.getVersion(); saveSettings(); }
  if (fs.existsSync(writable)) return writable;
  return src || writable; // lite build: downloaded on first search into the writable folder
}

// Lite installer: yt-dlp is not bundled, so fetch it once (23 MB) before the first search.
let ensuring = null;
async function ensureYtdlp(emit) {
  if (fs.existsSync(yoink.ytdlp) || !(IS_WIN || IS_MAC)) return true;
  if (!ensuring) ensuring = (async () => {
    const tmp = yoink.ytdlp + '.part';
    fs.mkdirSync(path.dirname(yoink.ytdlp), { recursive: true });
    await downloadTo('https://github.com/yt-dlp/yt-dlp/releases/latest/download/' + (IS_MAC ? 'yt-dlp_macos' : 'yt-dlp.exe'), tmp);
    if (fs.statSync(tmp).size < 1000000) throw new Error('Downloaded file looks wrong.');
    try { fs.chmodSync(tmp, 0o755); } catch (_) {}
    fs.renameSync(tmp, yoink.ytdlp);
  })().finally(() => { ensuring = null; });
  emit({ type: 'status', text: 'One-time setup: downloading yt-dlp (about 23 MB)...' });
  try { await ensuring; return true; } catch (e) { emit({ type: 'done', text: 'Could not download yt-dlp: ' + e.message + ' Check your internet connection and try again.', found: 0, exhausted: false, failed: true }); return false; }
}

function makeEngine() {
  const ffmpegBin = bundled('ffmpeg') || exe('ffmpeg');
  let dir = clipsDir();
  try { fs.mkdirSync(dir, { recursive: true }); } catch (_) { dir = path.join(app.getPath('documents'), 'Yoink Clips'); settings.outputDir = ''; }
  yoink = new Yoink({ ytdlp: ytdlpPath(), ffmpeg: ffmpegBin, dataDir: path.join(app.getPath('userData'), 'data'), clipsDir: dir });
}

function send(channel, payload) { if (win && !win.isDestroyed()) win.webContents.send(channel, payload); }
function clampNum(v, lo, hi, d) { v = Number(v); return Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d; }

async function updateYtdlp() {
  const bin = yoink.ytdlp;
  if (!fs.existsSync(bin)) {
    const ok = await ensureYtdlp(() => {});
    return { version: await yoink.ytdlpVersion(), message: ok ? 'Downloaded yt-dlp.' : 'Could not download yt-dlp. Check your internet connection and try again.', error: !ok };
  }
  const before = await yoink.ytdlpVersion();
  // 1) built-in updater
  let r = await run(bin, ['-U'], { timeoutMs: 120000 });
  let after = await yoink.ytdlpVersion();
  if (r.code === 0 && after) return { version: after, message: after === before ? `yt-dlp is already up to date (${after}).` : `Updated yt-dlp ${before} -> ${after}.` };
  // 2) download the latest standalone build ourselves
  const asset = IS_WIN ? 'yt-dlp.exe' : process.platform === 'darwin' ? 'yt-dlp_macos' : 'yt-dlp_linux';
  const tmp = bin + '.new';
  try {
    await downloadTo('https://github.com/yt-dlp/yt-dlp/releases/latest/download/' + asset, tmp);
    if (fs.statSync(tmp).size < 1000000) throw new Error('Downloaded file looks wrong.');
    try { fs.chmodSync(tmp, 0o755); } catch (_) {}
    const old = bin + '.old';
    try { fs.unlinkSync(old); } catch (_) {}
    fs.renameSync(bin, old);
    fs.renameSync(tmp, bin);
    try { fs.unlinkSync(old); } catch (_) {}
    after = await yoink.ytdlpVersion();
    return { version: after, message: `Updated yt-dlp ${before} -> ${after}.` };
  } catch (e) {
    try { fs.unlinkSync(tmp); } catch (_) {}
    return { version: before, message: 'Could not update yt-dlp: ' + e.message + ' Check your internet connection and try again.', error: true };
  }
}

// ---- favorites and history (plain JSON in the user data folder) ----
const favDir = () => path.join(app.getPath('userData'), 'favorites');
const loadFavs = () => readJson(userFile('favorites.json'), []);
const saveFavs = (a) => writeJson(userFile('favorites.json'), a);
const loadHistory = () => readJson(userFile('history.json'), []);
const saveHistory = (a) => writeJson(userFile('history.json'), a.slice(0, 100));
function addHistory(phrase, filters) {
  const h = loadHistory().filter((x) => !(x.phrase === phrase && JSON.stringify(x.filters) === JSON.stringify(filters)));
  h.unshift({ phrase, filters, time: Date.now() });
  saveHistory(h);
}
const insideDir = (file, dir) => { const rel = path.relative(dir, path.resolve(String(file || ''))); return !!rel && !rel.startsWith('..') && !path.isAbsolute(rel); };
const isOurWide = (f) => insideDir(f, path.join(yoink.dataDir, 'wide')) || insideDir(f, path.join(yoink.dataDir, 'vocals')) || insideDir(f, favDir());

function createWindow() {
  win = new BrowserWindow({
    width: 1040, height: 820, minWidth: 760, minHeight: 560, backgroundColor: '#050505', title: 'Yoink',
    icon: path.join(__dirname, '..', 'assets', 'icon.png'), autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: false },
  });
  win.setMenuBarVisibility(false);
  win.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));
}

function engineOpts() {
  return { padBefore: settings.padBefore, padAfter: settings.padAfter, format: settings.format, maxClips: settings.maxClips, normalize: !!settings.normalize, fadeOut: !!settings.fadeOut };
}
function exportOpts() { return { normalize: !!settings.normalize, fadeOut: !!settings.fadeOut };
}

// Only the newest run may talk to the window. A run that was stopped or replaced must finish before the next one starts,
// and its late events are dropped, so a stale "done" can never hide the Load more button of the current run.
let runId = 0, runP = null;
async function runSearch(sess, isMore) {
  const my = ++runId;
  const prevAbort = currentAbort, prevP = runP;
  if (prevAbort) prevAbort.abort();
  const ctl = new AbortController(); currentAbort = ctl;
  const p = (async () => {
    if (prevP) { try { await prevP; } catch (_) {} }
    if (my !== runId) return;
    try {
      await yoink.search(sess, engineOpts(), (evt) => { if (my === runId) send('yoink:event', Object.assign({ more: isMore }, evt)); }, ctl.signal);
    } catch (e) {
      if (my === runId) send('yoink:event', { type: 'done', text: 'Something went wrong: ' + (e.message || e) + ' Press Load more to try again.', found: 0, exhausted: false, failed: true });
    } finally { if (currentAbort === ctl) currentAbort = null; }
  })();
  runP = p;
  await p;
}

const gotLock = process.env.YOINK_E2E_DIR ? true : app.requestSingleInstanceLock();
if (!gotLock) app.quit();
else app.on('second-instance', () => { if (win && !win.isDestroyed()) { if (win.isMinimized()) win.restore(); win.focus(); } else if (app.isReady()) createWindow(); });

// macOS: a normal app menu (Cmd+Q, Cmd+W, Cmd+H, Cmd+M and the Edit shortcuts for the text boxes).
// Settings (Cmd+,) and Search (Cmd+K) are handled inside the page, so they have no menu accelerator here.
function setupMacMenu() {
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { label: app.name, submenu: [{ role: 'about' }, { type: 'separator' }, { role: 'hide' }, { role: 'hideOthers' }, { role: 'unhide' }, { type: 'separator' }, { role: 'quit' }] },
    { label: 'Edit', submenu: [{ role: 'undo' }, { role: 'redo' }, { type: 'separator' }, { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }] },
    { label: 'Window', submenu: [{ role: 'minimize' }, { role: 'zoom' }, { role: 'close' }, { type: 'separator' }, { role: 'front' }] },
  ]));
}

app.whenReady().then(() => {
  if (!gotLock) return;
  settings = loadSettings();
  makeEngine();
  if (IS_MAC) {
    setupMacMenu();
    if (!app.isPackaged && app.dock) { try { app.dock.setIcon(path.join(__dirname, '..', 'assets', 'icon.png')); } catch (_) {} }
    app.setAboutPanelOptions({ applicationName: 'Yoink', applicationVersion: app.getVersion(), copyright: 'CMPLXTY (MIT)', credits: 'Free and open source. https://cmplxty.music' });
  }

  ipcMain.handle('meta', () => ({ categories: CATEGORIES, languages: LANGUAGES, regions: REGIONS, version: app.getVersion() }));
  ipcMain.handle('settings:get', () => settings);
  ipcMain.handle('settings:set', (_e, s) => {
    s = s || {};
    settings = Object.assign({}, settings, {
      padBefore: clampNum(s.padBefore, 0, 3, settings.padBefore), padAfter: clampNum(s.padAfter, 0, 3, settings.padAfter),
      format: ['mp3', 'm4a'].includes(s.format) ? s.format : s.format === 'wav' ? 'wav' : settings.format,
      maxClips: Math.round(clampNum(s.maxClips, 1, 50, settings.maxClips)), volume: clampNum(s.volume, 0, 1, settings.volume),
      defaultFilters: s.defaultFilters !== undefined ? normFilters(s.defaultFilters) : settings.defaultFilters,
      autoUpdateCheck: s.autoUpdateCheck !== undefined ? !!s.autoUpdateCheck : settings.autoUpdateCheck,
      skipVersion: typeof s.skipVersion === 'string' && /^(\d+\.\d+\.\d+)?$/.test(s.skipVersion) ? s.skipVersion : settings.skipVersion,
      advOpen: s.advOpen !== undefined ? !!s.advOpen : settings.advOpen,
      signInPrompted: settings.signInPrompted || s.signInPrompted === true,
      normalize: s.normalize !== undefined ? !!s.normalize : settings.normalize, fadeOut: s.fadeOut !== undefined ? !!s.fadeOut : settings.fadeOut,
    });
    saveSettings();
    return settings;
  });
  ipcMain.handle('settings:chooseFolder', async () => {
    const r = await dialog.showOpenDialog(win, { title: 'Choose where clips are saved', properties: ['openDirectory', 'createDirectory'], defaultPath: yoink.clipsDir });
    if (r.canceled || !r.filePaths[0]) return { canceled: true, outputDir: settings.outputDir };
    settings.outputDir = r.filePaths[0]; saveSettings(); yoink.setClipsDir(r.filePaths[0]);
    return { outputDir: settings.outputDir };
  });
  ipcMain.handle('info', async () => ({ ytdlp: await yoink.ytdlpVersion(), ytdlpPath: yoink.ytdlp, ffmpegPath: yoink.ffmpeg, clipsDir: yoink.clipsDir, app: app.getVersion(), userData: app.getPath('userData') }));
  ipcMain.handle('ytdlp:update', () => updateYtdlp());
  ipcMain.handle('clips:open', async () => { fs.mkdirSync(yoink.clipsDir, { recursive: true }); await shell.openPath(yoink.clipsDir); return yoink.clipsDir; });
  ipcMain.handle('open:url', (_e, url) => { if (/^https:\/\/(www\.youtube\.com|youtu\.be|cmplxty\.music)\//.test(url)) shell.openExternal(url); });
  ipcMain.handle('update:open', () => { shell.openExternal(DOWNLOAD_PAGE); return DOWNLOAD_PAGE; });

  // ---- in-app update: one small JSON file, nothing about the user is sent, failures are silent ----
  let updInfo = null, updAbort = null;
  const noOpen = !!process.env.YOINK_UPDATE_NOLAUNCH;
  async function checkUpdates(manual) {
    const override = process.env.YOINK_UPDATE_URL;
    const r = await updater.checkForUpdate(app.getVersion(), { url: override || undefined, allowLocal: !!override });
    if (r.status !== 'error') { settings.lastUpdateCheck = Date.now(); saveSettings(); }
    updInfo = r.status === 'available' ? r.info : null;
    if (r.status === 'available') return { status: !manual && settings.skipVersion === r.info.version ? 'skipped' : 'available', version: r.info.version, notes: r.info.notes, size: r.info.size };
    return { status: r.status, version: r.info ? r.info.version : '' };
  }
  ipcMain.handle('update:check', () => checkUpdates(true));
  ipcMain.handle('update:skip', (_e, v) => { if (typeof v === 'string' && /^\d+\.\d+\.\d+$/.test(v)) { settings.skipVersion = v; saveSettings(); } return true; });
  ipcMain.handle('update:cancel', () => { if (updAbort) updAbort.abort(); });
  ipcMain.handle('update:install', async () => {
    const info = updInfo; if (!info) return { error: 'There is no update to install.' };
    const ctl = updAbort = new AbortController();
    try {
      const dir = path.join(os.tmpdir(), 'yoink-update'); fs.mkdirSync(dir, { recursive: true });
      const file = await updater.downloadInstaller(info, dir, (got, total) => send('update:event', { type: 'progress', pct: total ? Math.round(got / total * 100) : 0 }), ctl.signal, 5, { ext: IS_MAC ? '.dmg' : '.exe' });
      send('update:event', { type: 'ready' });
      if (noOpen) return { file, launched: false };
      updater.launchInstaller(file, { appPath: IS_MAC ? updater.macAppPath(app.getPath('exe')) : '' });
      setTimeout(() => app.quit(), 700);
      return { launched: true };
    } catch (e) {
      if (ctl.signal.aborted) return { cancelled: true };
      if (!noOpen) shell.openExternal(info.url);
      return { error: 'The automatic update did not work, so the download page was opened in your browser.', openedPage: true };
    } finally { if (updAbort === ctl) updAbort = null; }
  });
  ipcMain.handle('clip:save', async (_e, file) => {
    if (!file || !fs.existsSync(file)) return { error: 'That clip file is missing.' };
    const startDir = settings.saveDir && fs.existsSync(settings.saveDir) ? settings.saveDir : app.getPath('desktop');
    const r = await dialog.showSaveDialog(win, { title: 'Save clip to folder', defaultPath: path.join(startDir, path.basename(file)) });
    if (r.canceled || !r.filePath) return { canceled: true };
    fs.copyFileSync(file, r.filePath);
    settings.saveDir = path.dirname(r.filePath); saveSettings();
    return { saved: r.filePath };
  });

  ipcMain.handle('search', async (_e, phrase, filters) => {
    phrase = String(phrase || '').trim();
    const f = normFilters(filters);
    session = yoink.newSession(phrase, f);
    if (phrase) addHistory(phrase, f);
    if (!(await ensureYtdlp((m) => send('yoink:event', m)))) return;
    await runSearch(session, false);
  });
  ipcMain.handle('search:more', async () => { if (session) await runSearch(session, true); });
  ipcMain.handle('search:cancel', () => { if (currentAbort) currentAbort.abort(); });

  // Re-cut a clip from its wide source after the trim flags moved. Instant (no network).
  ipcMain.handle('clip:retrim', async (_e, c) => {
    try {
      if (!c || !isOurWide(c.wideFile) || !fs.existsSync(c.wideFile)) return { error: 'The source audio for this clip is gone. Search again.' };
      const dur = Number(c.wideDur) || 0;
      const s = clampNum(c.trimStart, 0, dur, 0), e = clampNum(c.trimEnd, 0, dur, dur);
      if (e - s < 0.05) return { error: 'The clip is too short.' };
      const fmt = ['mp3', 'm4a'].includes(c.format) ? c.format : 'wav';
      const vid = String(c.videoId || 'clip').replace(/[^\w-]/g, ''), ph = String(c.phrase || c.title || 'clip'), xo = exportOpts();
      const out = yoink.clipFile(vid, ph, Number(c.winStart || 0) + s, s, e, fmt, xo);
      await yoink.renderClip(c.wideFile, s, e, fmt, out, null, xo);
      const res = { file: out, trimStart: s, trimEnd: e };
      if (c.cleanWide && isOurWide(c.cleanWide) && fs.existsSync(c.cleanWide)) {
        const xv = Object.assign({ variant: 'vocal' }, xo);
        res.cleanFile = yoink.clipFile(vid, ph, Number(c.winStart || 0) + s, s, e, fmt, xv);
        await yoink.renderClip(c.cleanWide, s, e, fmt, res.cleanFile, null, xv);
      }
      return res;
    } catch (err) { return { error: String(err.message || err) }; }
  });

  // ---- sign-in and sync (optional; guests never touch any of this) ----
  const syncConfig = () => {
    const cfg = Object.assign({ googleClientId: '', googleClientSecret: '', showDemo: false }, readJson(path.join(process.resourcesPath || '', 'sync-config.json'), null) || readJson(path.join(__dirname, '..', 'sync-config.json'), {}));
    if (process.env.YOINK_SYNC_DEMO === '0') cfg.showDemo = false; if (process.env.YOINK_SYNC_DEMO === '1') cfg.showDemo = true;
    return cfg;
  };
  const cipher = { available: () => { try { return safeStorage.isEncryptionAvailable(); } catch (_) { return false; } }, encrypt: (t) => safeStorage.encryptString(t), decrypt: (b) => safeStorage.decryptString(b) };
  const syncSvc = new sync.SyncService({
    tokens: new sync.TokenStore(userFile('account.bin'), cipher), config: syncConfig(), demoDir: process.env.YOINK_SYNC_DIR || userFile('demo-cloud'),
    loadFavs, saveFavs, loadTombs: () => readJson(userFile('fav-tombs.json'), []), saveTombs: (a) => writeJson(userFile('fav-tombs.json'), a),
    onRemoved: (rec) => { for (const f of [rec.wideFile, rec.cleanWide]) if (f && insideDir(f, favDir())) { try { fs.unlinkSync(f); } catch (_) {} } },
    openUrl: (u) => { if (/^https:\/\/accounts\.google\.com\//.test(u) || (process.env.YOINK_SYNC_OPEN_ANY && /^http:\/\/127\.0\.0\.1:/.test(u))) shell.openExternal(u); },
    endpoints: process.env.YOINK_SYNC_ENDPOINTS ? JSON.parse(process.env.YOINK_SYNC_ENDPOINTS) : undefined,
    emit: (evt) => send('sync:event', evt),
  });
  ipcMain.handle('sync:status', () => syncSvc.status());
  ipcMain.handle('sync:signin', async (_e, provider, arg) => { try { return await syncSvc.signIn(String(provider), arg); } catch (e) { return Object.assign(syncSvc.status(), { error: String(e.message || e) }); } });
  ipcMain.handle('sync:signout', () => syncSvc.signOut());
  ipcMain.handle('sync:now', async () => { try { await syncSvc.sync(); } catch (_) {} return syncSvc.status(); });
  if (syncSvc.status().signedIn) setTimeout(() => syncSvc.sync().catch(() => {}), 3000);

  // Favorites: the heart keeps the wide audio in the app's data folder so a favorite survives a cache clean.
  // A favorite that arrived by sync has no audio on this PC yet: it is downloaded again (same source, same cut) when the Favorites list is opened.
  const restoring = new Set(); let restoreQ = Promise.resolve();
  function restoreFav(cid) {
    if (restoring.has(cid)) return; restoring.add(cid);
    restoreQ = restoreQ.then(async () => {
      try {
        const c = loadFavs().find((x) => x.cid === cid); if (!c || !c.needsRestore) return;
        if (!/^[\w-]{11}$/.test(String(c.videoId || '')) || !(c.end > c.start)) throw new Error('This favorite has no source to restore.');
        send('fav:event', { cid, state: 'restoring' });
        if (!(await ensureYtdlp(() => {}))) throw new Error('yt-dlp is missing.');
        const wide = await yoink.makeWide({ id: c.videoId }, { start: Number(c.start), end: Number(c.end) });
        fs.mkdirSync(favDir(), { recursive: true });
        const keep = path.join(favDir(), path.basename(wide.file)); if (!fs.existsSync(keep)) fs.copyFileSync(wide.file, keep);
        const fmt = ['mp3', 'm4a'].includes(c.format) ? c.format : 'wav', xo = exportOpts();
        const s0 = clampNum(c.trimStart, 0, wide.duration, 0), e0 = clampNum(c.trimEnd, 0, wide.duration, wide.duration);
        const file = await yoink.renderClip(keep, s0, e0, fmt, yoink.clipFile(c.videoId, String(c.phrase || c.title || 'clip'), wide.winStart + s0, s0, e0, fmt, xo), null, xo);
        const favs = loadFavs(), i = favs.findIndex((x) => x.cid === cid); if (i < 0) return;
        favs[i] = Object.assign({}, favs[i], { wideFile: keep, file, winStart: wide.winStart, wideDur: wide.duration, peaks: wide.peaks, trimStart: s0, trimEnd: e0, hitStart: Number(c.start) - wide.winStart, hitEnd: Number(c.end) - wide.winStart, format: fmt, needsRestore: false });
        saveFavs(favs); send('fav:event', { cid, state: 'ready', fav: favs[i] });
      } catch (e) { send('fav:event', { cid, state: 'failed', text: String(e.message || e) }); }
      finally { restoring.delete(cid); }
    });
  }
  const touch = (rec) => Object.assign(rec, { updatedAt: Date.now() });
  const cleanTags = (t) => [...new Set((Array.isArray(t) ? t : []).map((x) => String(x).replace(/[\u0000-\u001f,]/g, ' ').trim().slice(0, 24)).filter(Boolean))].slice(0, 8);
  ipcMain.handle('fav:list', async () => {
    const favs = loadFavs();
    for (const c of favs) {
      if (c.needsRestore) { restoreFav(c.cid); continue; }
      if (!fs.existsSync(c.file) && fs.existsSync(c.wideFile)) {
        try { await yoink.renderClip(c.wideFile, c.trimStart, c.trimEnd, c.format, c.file); } catch (_) {}
      }
    }
    return favs;
  });
  ipcMain.handle('fav:toggle', (_e, c) => {
    let favs = loadFavs();
    const i = favs.findIndex((x) => x.cid === (c && c.cid));
    if (i >= 0) {
      const [old] = favs.splice(i, 1);
      for (const f of [old.wideFile, old.cleanWide]) if (f && insideDir(f, favDir())) { try { fs.unlinkSync(f); } catch (_) {} }
      saveFavs(favs); syncSvc.recordRemoved(old.cid); syncSvc.schedule();
      return { favorite: false, ids: favs.map((x) => x.cid) };
    }
    if (!c || !c.cid || !c.wideFile || !fs.existsSync(c.wideFile)) return { error: 'This clip is not ready yet.' };
    fs.mkdirSync(favDir(), { recursive: true });
    const keep = path.join(favDir(), path.basename(c.wideFile));
    if (!fs.existsSync(keep)) fs.copyFileSync(c.wideFile, keep);
    const rec = touch(Object.assign({}, c, { wideFile: keep, favoritedAt: Date.now(), tags: [], needsRestore: false }));
    if (c.cleanWide && isOurWide(c.cleanWide) && fs.existsSync(c.cleanWide)) {
      const kc = path.join(favDir(), 'vocal_' + path.basename(c.cleanWide)); if (!fs.existsSync(kc)) fs.copyFileSync(c.cleanWide, kc); rec.cleanWide = kc;
    }
    favs.unshift(rec);
    saveFavs(favs); syncSvc.schedule();
    return { favorite: true, ids: favs.map((x) => x.cid) };
  });
  ipcMain.handle('fav:update', (_e, c) => {
    const favs = loadFavs(); const i = favs.findIndex((x) => x.cid === (c && c.cid));
    if (i >= 0) {
      const next = { file: c.file, trimStart: c.trimStart, trimEnd: c.trimEnd };
      if (c.cleanFile && c.cleanWide && isOurWide(c.cleanWide) && fs.existsSync(c.cleanWide)) {
        let kc = c.cleanWide;
        if (!insideDir(kc, favDir())) { fs.mkdirSync(favDir(), { recursive: true }); kc = path.join(favDir(), 'vocal_' + path.basename(c.cleanWide)); if (!fs.existsSync(kc)) fs.copyFileSync(c.cleanWide, kc); }
        next.cleanWide = kc; next.cleanFile = c.cleanFile;
      }
      favs[i] = touch(Object.assign({}, favs[i], next)); saveFavs(favs); syncSvc.schedule();
    }
    return true;
  });
  ipcMain.handle('fav:setTags', (_e, cid, tags) => {
    const favs = loadFavs(); const i = favs.findIndex((x) => x.cid === cid);
    if (i < 0) return { error: 'Not a favorite.' };
    favs[i] = touch(Object.assign({}, favs[i], { tags: cleanTags(tags) })); saveFavs(favs); syncSvc.schedule();
    return { tags: favs[i].tags };
  });

  // ---- Clean up (vocal isolation, offline after a one-time model download) ----
  let vocal = null; const cleans = new Map();
  const cleaner = () => vocal || (vocal = new VocalCleaner({ modelsDir: userFile('models'), cacheDir: path.join(yoink.dataDir, 'vocals'), idleMs: 90000 }));
  ipcMain.handle('clean:status', () => { const st = cleaner().status(); return Object.assign(st, { bytes: st.ready ? st.modelBytes : 0 }); });
  ipcMain.handle('clean:remove', () => { cleaner().removeModel(); return cleaner().status(); });
  ipcMain.handle('clean:cancel', (_e, cid) => { const a = cleans.get(cid); if (a) a.abort(); });
  ipcMain.handle('clean:start', async (_e, c) => {
    const cid = c && String(c.cid || '');
    try {
      if (!cid || !isOurWide(c.wideFile) || !fs.existsSync(c.wideFile)) return { error: 'The source audio for this clip is gone. Search again.' };
      if (cleans.has(cid)) return { error: 'Already cleaning this clip.' };
      const ctl = new AbortController(); cleans.set(cid, ctl);
      const say = (phase, pct, text) => send('clean:event', { cid, phase, pct, text });
      try {
        const r = await cleaner().clean(c.wideFile, { onProgress: say, signal: ctl.signal });
        const dur = Number(c.wideDur) || 0, s0 = clampNum(c.trimStart, 0, dur, 0), e0 = clampNum(c.trimEnd, 0, dur, dur);
        const fmt = ['mp3', 'm4a'].includes(c.format) ? c.format : 'wav', xv = Object.assign({ variant: 'vocal' }, exportOpts());
        const out = yoink.clipFile(String(c.videoId || 'clip').replace(/[^\w-]/g, ''), String(c.phrase || c.title || 'clip'), Number(c.winStart || 0) + s0, s0, e0, fmt, xv);
        await yoink.renderClip(r.file, s0, e0, fmt, out, null, xv);
        return { cleanWide: r.file, cleanFile: out, cleanPeaks: wavPeaks(r.file).peaks, cached: !!r.cached };
      } finally { cleans.delete(cid); }
    } catch (e) {
      if (/Cancel/i.test(e.message || '')) return { cancelled: true };
      const m = String(e.message || e);
      if (/ERR_DLOPEN_FAILED|module could not be found|Cannot find module|onnxruntime/i.test(m)) return { error: IS_MAC ? 'The vocal engine could not start on this Mac. Try reinstalling Yoink. If it keeps happening, tell us your macOS version and chip (Apple or Intel).' : 'The vocal engine could not start on this PC. It needs the free Microsoft Visual C++ Redistributable (x64): https://aka.ms/vs/17/release/vc_redist.x64.exe' };
      return { error: m };
    }
  });
  // Estimated key of the trimmed part (from the original mix, which has the most to go on).
  ipcMain.handle('clip:key', (_e, c) => {
    try {
      if (!c || !isOurWide(c.wideFile) || !fs.existsSync(c.wideFile)) return { error: 'The source audio for this clip is gone.' };
      const w = readWav(c.wideFile); const a = Math.max(0, Math.floor(clampNum(c.trimStart, 0, 1e5, 0) * w.rate)), b = Math.min(w.channels[0].length, Math.ceil(clampNum(c.trimEnd, 0, 1e5, 0) * w.rate));
      if (b - a < w.rate * 0.5) return { error: 'This clip is too short to tell the key.' };
      const mono = new Float32Array(b - a); for (let i = 0; i < mono.length; i++) { let v = 0; for (const ch of w.channels) v += ch[a + i]; mono[i] = v / w.channels.length; }
      return detectKey(mono, w.rate);
    } catch (e) { return { error: String(e.message || e) }; }
  });

  ipcMain.handle('history:list', () => loadHistory());
  ipcMain.handle('history:remove', (_e, time) => { saveHistory(loadHistory().filter((x) => x.time !== time)); return loadHistory(); });
  ipcMain.handle('history:clear', () => { saveHistory([]); return []; });

  const cacheDirs = () => [path.join(yoink.dataDir, 'wide'), path.join(yoink.dataDir, 'vocals'), path.join(yoink.dataDir, 'captions')];
  const dirSize = (d) => { let n = 0; try { for (const f of fs.readdirSync(d)) { try { n += fs.statSync(path.join(d, f)).size; } catch (_) {} } } catch (_) {} return n; };
  ipcMain.handle('cache:info', () => ({ bytes: cacheDirs().reduce((a, d) => a + dirSize(d), 0) }));
  ipcMain.handle('cache:clear', () => {
    for (const d of cacheDirs()) { try { for (const f of fs.readdirSync(d)) { try { fs.unlinkSync(path.join(d, f)); } catch (_) {} } } catch (_) {} }
    return { bytes: 0 };
  });

  // Drag a real audio file out of the window into a DAW / Explorer. Only ever the finished excerpt.
  ipcMain.on('drag:start', (e, file) => {
    if (!file || !fs.existsSync(file)) return;
    let icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'drag.png'));
    if (icon.isEmpty()) icon = nativeImage.createFromPath(path.join(__dirname, '..', 'assets', 'icon.png'));   // macOS needs a drag icon
    if (!icon.isEmpty()) icon = icon.resize({ width: 48, height: 48 });
    e.sender.startDrag({ file, icon });
  });

  createWindow();
  win.webContents.once('did-finish-load', () => {
    if (process.env.YOINK_E2E_DIR && !process.env.YOINK_UPDATE_URL) return;
    setTimeout(async () => {
      try { if (!updater.shouldAutoCheck(settings)) return; const r = await checkUpdates(false); if (r.status === 'available') send('update:event', Object.assign({ type: 'available' }, r)); } catch (_) {}
    }, 3000);
  });
  if (process.env.YOINK_E2E_DIR) require(process.env.YOINK_UI_TEST ? '../tests/ui' : '../tests/e2e')(win, process.env.YOINK_E2E_DIR, process.env.YOINK_E2E_PHRASE || 'what the heck').then(() => setTimeout(() => app.quit(), 500)).catch((e) => { console.log('E2E_FAIL', e); app.quit(); });
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});

// Windows: closing the window quits. macOS: the app stays in the Dock until Cmd+Q, and clicking the Dock icon opens a new window (see 'activate').
app.on('window-all-closed', () => { if (currentAbort) currentAbort.abort(); if (!IS_MAC || process.env.YOINK_E2E_DIR) app.quit(); });
