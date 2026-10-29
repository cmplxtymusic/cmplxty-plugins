// Runs tests/ui.js in real Electron under xvfb: 4 launches (2 fresh profiles x 2 launches). Usage: xvfb-run -a node tests/ui-run.js <outDir> [modelFile]
const { spawnSync, execFileSync } = require('child_process');
const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const root = path.join(__dirname, '..');
const out = path.resolve(process.argv[2] || '/tmp/yoink-ui-out'); fs.mkdirSync(out, { recursive: true });
const model = process.argv[3] || '/workspace/yoink-win/models/htdemucs_ft_vocals_fp16weights.onnx';
const electron = process.env.YOINK_ELECTRON || path.join(root, 'node_modules', 'electron', 'dist', 'electron');
const base = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-ui-'));
const samples = '/workspace/yoink-win/samples';
function prep(ud) {
  const wide = path.join(ud, 'data', 'wide'); fs.mkdirSync(wide, { recursive: true }); fs.mkdirSync(path.join(ud, 'models'), { recursive: true });
  const names = fs.readdirSync(samples).filter((f) => f.endsWith('.wav')).sort();
  for (let k = 1; k <= 2; k++) {   // ~10 s of real speech/music per synthetic "wide" file
    const ins = (k === 1 ? names.slice(0, 6) : names).flatMap((n) => ['-i', path.join(samples, n)]);
    execFileSync('ffmpeg', ['-y', '-v', 'error', ...ins, '-filter_complex', `concat=n=${ins.length / 2}:v=0:a=1`, '-ar', '44100', '-ac', '2', '-c:a', 'pcm_s16le', path.join(wide, `wide_${k}.wav`)]);
  }
  fs.copyFileSync(model, path.join(ud, 'models', path.basename(model)));
}
const payload = crypto.randomBytes(300000), sha = crypto.createHash('sha256').update(payload).digest('hex');
const srv = http.createServer((req, res) => {
  if (req.url === '/yoink-version.json') { res.setHeader('content-type', 'application/json'); res.end(JSON.stringify({ version: '9.9.9', url: 'https://cmplxty.music/easter-egg/', installer: `http://127.0.0.1:${srv.address().port}/Yoink-Setup.exe`, sha256: sha, size: payload.length, notes: 'Clean up vocals, advanced filters and a few fixes.' })); }
  else if (req.url === '/Yoink-Setup.exe') { res.setHeader('content-length', payload.length); let i = 0; const t = setInterval(() => { if (i >= payload.length) { clearInterval(t); res.end(); } else { res.write(payload.subarray(i, i + 50000)); i += 50000; } }, 60); }
  else { res.statusCode = 404; res.end(); }
});
srv.listen(0, '127.0.0.1', () => {
  const port = srv.address().port; let fails = 0;
  const ud1 = path.join(base, 'ud1'), ud2 = path.join(base, 'ud2'); prep(ud1);
  fs.mkdirSync(ud2, { recursive: true });
  // an async runner so the server stays alive: spawnSync would block it, so use the event loop
  const { spawn } = require('child_process');
  const launch = (pass, ud, extra) => new Promise((resolve) => {
    const env = Object.assign({}, process.env, { YOINK_E2E_DIR: out, YOINK_UI_TEST: '1', YOINK_UI_PASS: pass, YOINK_USERDATA: ud, YOINK_CLIPS_DIR: path.join(ud, 'clips'), YOINK_SYNC_DEMO: '1', YOINK_SYNC_DIR: path.join(base, 'cloud'), YOINK_UPDATE_NOLAUNCH: '1', YOINK_ORT_DIR: '', YOINK_UPDATE_SHA: sha }, extra);
    if (pass === 'A' || pass === 'B') env.YOINK_UPDATE_URL = `http://127.0.0.1:${port}/yoink-version.json`;
    const p = spawn(electron, ['--no-sandbox', '--disable-gpu', root], { env }); let log = '';
    p.stdout.on('data', (d) => { log += d; process.stdout.write(d); }); p.stderr.on('data', (d) => { if (/Error|error|FAIL|Uncaught/.test(String(d)) && !/dbus|GPU|gpu|vaapi|Fontconfig/i.test(String(d))) { log += d; process.stderr.write(d); } });
    const kill = setTimeout(() => { console.log('TIMEOUT pass ' + pass); p.kill('SIGKILL'); }, 600000);
    p.on('exit', () => { clearTimeout(kill); const m = /UI_FAILS (\d+)/.exec(log); if (!m || +m[1]) fails++; if (!/E2E_DONE/.test(log)) { fails++; console.log('NO E2E_DONE in pass ' + pass); } resolve(); });
  });
  (async () => { await launch('A', ud1); await launch('B', ud1); await launch('C', ud2); await launch('D', ud2); const ud3 = path.join(base, 'ud3'); fs.mkdirSync(ud3, { recursive: true }); await launch('N', ud3, { YOINK_SYNC_DEMO: '0' }); console.log(fails ? 'UI_RUN_FAILED ' + fails : 'UI_RUN_OK'); srv.close(); process.exit(fails ? 1 : 0); })();
});
