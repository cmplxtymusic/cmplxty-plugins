// Copies the Windows x64 build of onnxruntime-node (and its tiny JS dependency) to build/ort/node_modules, without the
// macOS/Linux binaries and without the DirectML files (CPU only). About 32 MB. Run by build-win.sh.
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const out = path.join(__dirname, 'ort', 'node_modules');
fs.rmSync(path.join(__dirname, 'ort'), { recursive: true, force: true });
const copy = (from, to) => { fs.mkdirSync(path.dirname(to), { recursive: true }); fs.cpSync(from, to, { recursive: true }); };
const node = path.join(root, 'node_modules', 'onnxruntime-node');
const common = path.join(root, 'node_modules', 'onnxruntime-common');
for (const f of ['package.json', 'dist']) copy(path.join(node, f), path.join(out, 'onnxruntime-node', f));
for (const f of ['onnxruntime.dll', 'onnxruntime_binding.node']) copy(path.join(node, 'bin', 'napi-v6', 'win32', 'x64', f), path.join(out, 'onnxruntime-node', 'bin', 'napi-v6', 'win32', 'x64', f));
for (const f of ['package.json', 'dist']) copy(path.join(common, f), path.join(out, 'onnxruntime-common', f));
for (const d of ['onnxruntime-node', 'onnxruntime-common']) for (const lic of ['LICENSE', 'ThirdPartyNotices.txt']) { const p = path.join(root, 'node_modules', d, lic); if (fs.existsSync(p)) copy(p, path.join(out, d, lic)); }
// drop source maps
(function strip(d) { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) strip(p); else if (p.endsWith('.map') || p.endsWith('.d.ts')) fs.unlinkSync(p); } })(path.join(__dirname, 'ort'));
console.log('staged onnxruntime for win32-x64 in build/ort');
