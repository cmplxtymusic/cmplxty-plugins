// Lite variant: same app, but yt-dlp.exe is not bundled (it is downloaded on first search). ~25 MB smaller.
const base = require('../package.json').build;
module.exports = {
  ...base,
  directories: { ...base.directories, output: 'dist-lite' },
  artifactName: 'Yoink-Setup-Lite.exe',
  extraResources: [{ from: 'build/tools-lite', to: 'bin' }, ...base.extraResources.filter((r) => r.to !== 'bin')],
};
