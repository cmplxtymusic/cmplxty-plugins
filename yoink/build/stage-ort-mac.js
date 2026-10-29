// macOS: stages onnxruntime-node for the Clean up (vocal) feature into build/ort-mac/<arch>/node_modules, one folder per CPU:
//   arm64  onnxruntime-node 1.31.1 (the version in package.json; needs macOS 14 or newer)
//   x64    onnxruntime-node 1.23.2 (the last release that ships an Intel Mac binary; needs macOS 13.4 or newer). Fetched from npm and
//          checked against the npm integrity hash below, so this needs internet access. Set YOINK_ORT_X64_TGZ=<file> to use a local copy.
// Both folders go into every build, so the universal app's two halves are identical there. About 90 MB before compression.
// Run by the macOS CI job and by build-mac.sh. Works on Linux too (it only copies files), which is how it is tested.
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const root = path.join(__dirname, '..');
const out = path.join(__dirname, 'ort-mac');
const X64 = {
  node: { version: '1.23.2', integrity: 'sha512-OBTsG0W8ddBVOeVVVychpVBS87A9YV5sa2hJ6lc025T97Le+J4v++PwSC4XFs1C62SWyNdof0Mh4KvnZgtt4aw==' },
  common: { version: '1.23.2', integrity: 'sha512-5LFsC9Dukzp2WV6kNHYLNzp8sT6V02IubLCbzw2Xd6X5GOlr65gAX6xiJwyi2URJol/s71gaQLC5F2C25AAR2w==' },
};
const copy = (from, to) => { fs.mkdirSync(path.dirname(to), { recursive: true }); fs.cpSync(from, to, { recursive: true }); };
function strip(d) { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) strip(p); else if (p.endsWith('.map') || p.endsWith('.d.ts')) fs.unlinkSync(p); } }
function copyLicenses(srcPkg, dstPkg) { for (const lic of ['LICENSE', 'ThirdPartyNotices.txt']) { const p = path.join(srcPkg, lic); if (fs.existsSync(p)) copy(p, path.join(dstPkg, lic)); } }

function verifyIntegrity(file, integrity) {
  const [algo, b64] = integrity.split('-');
  const got = crypto.createHash(algo).update(fs.readFileSync(file)).digest('base64');
  if (got !== b64) throw new Error(`${path.basename(file)} does not match its npm integrity hash (got ${algo}-${got})`);
}
function fetchNpm(name, spec, integrity, tmp) {
  const local = name === 'onnxruntime-node' ? process.env.YOINK_ORT_X64_TGZ : '';
  let tgz = local;
  if (!tgz) {
    const out = execFileSync('npm', ['pack', `${name}@${spec}`, '--silent', '--pack-destination', tmp], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'], shell: process.platform === 'win32' });
    tgz = path.join(tmp, out.trim().split('\n').pop());
  }
  verifyIntegrity(tgz, integrity);
  const dir = path.join(tmp, name + '-' + spec); fs.mkdirSync(dir, { recursive: true });
  execFileSync('tar', ['-xzf', tgz, '-C', dir]);
  return path.join(dir, 'package');
}

fs.rmSync(out, { recursive: true, force: true });
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'yoink-ort-'));
try {
  // arm64: from node_modules (installed by npm ci)
  const node = path.join(root, 'node_modules', 'onnxruntime-node'), common = path.join(root, 'node_modules', 'onnxruntime-common');
  const a = path.join(out, 'arm64', 'node_modules');
  const armBin = path.join(node, 'bin', 'napi-v6', 'darwin', 'arm64');
  if (!fs.existsSync(armBin)) throw new Error('onnxruntime-node has no darwin/arm64 binary: run npm ci first');
  for (const f of ['package.json', 'dist']) copy(path.join(node, f), path.join(a, 'onnxruntime-node', f));
  // the binding loads @rpath/libonnxruntime.1.dylib, so the versioned duplicate (45 MB) is not needed
  for (const f of ['onnxruntime_binding.node', 'libonnxruntime.1.dylib']) copy(path.join(armBin, f), path.join(a, 'onnxruntime-node', 'bin', 'napi-v6', 'darwin', 'arm64', f));
  for (const f of ['package.json', 'dist']) copy(path.join(common, f), path.join(a, 'onnxruntime-common', f));
  copyLicenses(node, path.join(a, 'onnxruntime-node')); copyLicenses(common, path.join(a, 'onnxruntime-common'));

  // x64: onnxruntime-node 1.23.2 from npm
  const xn = fetchNpm('onnxruntime-node', X64.node.version, X64.node.integrity, tmp);
  const xc = fetchNpm('onnxruntime-common', X64.common.version, X64.common.integrity, tmp);
  const x = path.join(out, 'x64', 'node_modules');
  const xBin = path.join(xn, 'bin', 'napi-v6', 'darwin', 'x64');
  if (!fs.existsSync(xBin)) throw new Error('onnxruntime-node ' + X64.node.version + ' has no darwin/x64 binary');
  for (const f of ['package.json', 'dist']) copy(path.join(xn, f), path.join(x, 'onnxruntime-node', f));
  copy(xBin, path.join(x, 'onnxruntime-node', 'bin', 'napi-v6', 'darwin', 'x64'));
  for (const f of ['package.json', 'dist']) copy(path.join(xc, f), path.join(x, 'onnxruntime-common', f));
  copyLicenses(xn, path.join(x, 'onnxruntime-node')); copyLicenses(xc, path.join(x, 'onnxruntime-common'));
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
strip(out);
console.log('staged onnxruntime for macOS (arm64 1.31.1, x64 ' + X64.node.version + ') in build/ort-mac');
