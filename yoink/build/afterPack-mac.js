// macOS: electron-builder never copies a folder named node_modules through extraResources, so the staged onnxruntime goes in here.
// Both CPU folders (arm64 and x64) go into BOTH halves of the universal build, so @electron/universal sees identical files.
// Also trims Chromium locale folders and checks the bundled tools are really there.
const fs = require('fs'); const path = require('path');
exports.default = async function (ctx) {
  const src = path.join(__dirname, 'ort-mac');
  if (!fs.existsSync(path.join(src, 'arm64')) || !fs.existsSync(path.join(src, 'x64'))) throw new Error('build/ort-mac is missing: run node build/stage-ort-mac.js first');
  const app = fs.readdirSync(ctx.appOutDir).find((n) => n.endsWith('.app'));
  if (!app) throw new Error('no .app found in ' + ctx.appOutDir);
  const res = path.join(ctx.appOutDir, app, 'Contents', 'Resources');
  fs.cpSync(src, path.join(res, 'ort'), { recursive: true });
  for (const t of ['yt-dlp', 'ffmpeg']) if (!fs.existsSync(path.join(res, 'bin', t))) throw new Error('missing bundled tool: bin/' + t + ' (run build-mac.sh tools)');
  for (const t of ['yt-dlp', 'ffmpeg']) fs.chmodSync(path.join(res, 'bin', t), 0o755);
};
