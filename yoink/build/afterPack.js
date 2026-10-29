// Trim files the app never uses (size). Runs after electron-builder lays out win-unpacked.
// Kept deliberately conservative: Vulkan/SwiftShader DLLs stay because Chromium may need them as a software-GL fallback.
const fs = require('fs'); const path = require('path');
exports.default = async function (ctx) {
  // electron-builder never copies a folder named node_modules through extraResources, so the staged onnxruntime goes in here.
  const src = path.join(__dirname, 'ort'), dst = path.join(ctx.appOutDir, 'resources', 'ort');
  if (!fs.existsSync(src)) throw new Error('build/ort is missing: run node build/stage-ort.js first');
  fs.cpSync(src, dst, { recursive: true });
  for (const f of ['LICENSES.chromium.html', 'dxcompiler.dll', 'dxil.dll']) { try { fs.rmSync(path.join(ctx.appOutDir, f)); } catch (_) {} }
};
