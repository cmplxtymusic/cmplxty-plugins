// macOS build config. Used as: electron-builder --mac --config build/mac-config.js
// It starts from the Windows config in package.json (which is not modified) and replaces the Windows-only parts.
//
// Signing modes (chosen from environment variables, nothing is bought or required):
//   default                      ad-hoc signed ("-"), no Apple account. Apple Silicon Macs refuse to run completely unsigned apps, so
//                                this is the minimum. Gatekeeper still warns on first open (right-click > Open).
//   CSC_LINK + CSC_KEY_PASSWORD  real signing with a Developer ID Application certificate (hardened runtime on).
//   + APPLE_ID, APPLE_APP_SPECIFIC_PASSWORD, APPLE_TEAM_ID   (or APPLE_API_KEY, APPLE_API_KEY_ID, APPLE_API_ISSUER)
//                                also notarizes, so Gatekeeper does not warn at all.
//   YOINK_HARDENED=1             turn the hardened runtime on even when ad-hoc signing (to test it).
// Real signing and notarization have never been run for Yoink: they need Apple's $99/year Developer Program.
const base = require('../package.json').build;
const env = process.env;
const realSigning = !!(env.CSC_LINK || env.CSC_NAME);
const notarize = realSigning && !!((env.APPLE_ID && env.APPLE_APP_SPECIFIC_PASSWORD && env.APPLE_TEAM_ID) || (env.APPLE_API_KEY && env.APPLE_API_KEY_ID && env.APPLE_API_ISSUER));
const { win, nsis, electronDist, ...rest } = base;

module.exports = {
  ...rest,
  electronLanguages: ['en', 'en_US'],
  directories: { ...base.directories, output: 'dist-mac' },
  artifactName: 'Yoink.${ext}',
  afterPack: 'build/afterPack-mac.js',
  extraResources: [{ from: 'build/tools-mac', to: 'bin' }, ...base.extraResources.filter((r) => r.to !== 'bin').map((r) => (r.to === 'THIRD-PARTY.txt' ? { ...r, from: 'build/THIRD-PARTY-mac.txt' } : r))],
  mac: {
    target: [{ target: 'dmg', arch: ['universal'] }],
    category: 'public.app-category.music',
    icon: 'build/icon.icns',
    identity: realSigning ? undefined : '-',
    hardenedRuntime: realSigning || env.YOINK_HARDENED === '1',
    gatekeeperAssess: false,
    entitlements: 'build/entitlements.mac.plist',
    entitlementsInherit: 'build/entitlements.mac.plist',
    notarize,
    // Single-CPU files are expected: the onnxruntime folders hold one arm64 and one x64 build, identical in both halves. Yoink picks the right one at run time.
    x64ArchFiles: 'Contents/Resources/ort/**',
    // yt-dlp is a PyInstaller program: re-signing it breaks it. In ad-hoc mode it is left with its own signature.
    // (For real signing + notarization it has to be signed with our certificate: see README-MAC.md.)
    signIgnore: realSigning ? undefined : ['Contents/Resources/bin/yt-dlp$'],
    extendInfo: { NSHumanReadableCopyright: 'CMPLXTY', LSMinimumSystemVersion: '13.0' },
  },
  dmg: {
    title: 'Yoink',
    icon: 'build/icon.icns',
    contents: [{ x: 130, y: 150, type: 'file' }, { x: 410, y: 150, type: 'link', path: '/Applications' }],
    window: { width: 540, height: 380 },
    sign: false,
  },
};
