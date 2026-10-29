# Yoink for Mac

Yoink for Mac is the same app as the Windows one, built as a universal `.dmg` (Apple Silicon and Intel).
Needs macOS 13 (Ventura) or newer. "Clean up" (vocal removal) needs macOS 14 or newer on Apple Silicon Macs and 13.4 or newer on Intel Macs.

## For people installing it
1. Open `Yoink.dmg` and drag Yoink onto the Applications folder.
2. First open: the build is **not signed with an Apple Developer ID** (that costs $99 a year), so macOS shows a warning like "Yoink cannot be opened because Apple cannot check it for malicious software".
   - Right-click (or Control-click) Yoink in Applications and choose **Open**, then **Open** again. Or: System Settings > Privacy & Security > scroll down > **Open Anyway**.
   - You only do this once. After that Yoink opens normally.
3. macOS may ask to let Yoink use your keychain (that is where your sign-in is stored, encrypted). Choose Always Allow.

Where things are: clips go to `~/Documents/Yoink Clips` (change it in Settings). App data, settings and the Clean up model are in `~/Library/Application Support/Yoink`. Delete Yoink from Applications to remove it; delete that folder too for a full clean-out.
Updates: Yoink checks cmplxty.music once a day. When a Mac build is newer it shows a notice; "Install" downloads the new `.dmg`, checks its SHA256, quits, swaps the app in the Applications folder and reopens it. If it cannot replace the app (no write access) it opens the `.dmg` for you to drag over by hand.

## What was changed from the Windows app
| Area | Windows | Mac |
| --- | --- | --- |
| Packaging | NSIS installer | universal `.dmg` (`build/mac-config.js`), the Windows config in `package.json` is untouched |
| yt-dlp | `yt-dlp.exe` | official `yt-dlp_macos` (universal binary, Unlicense), fetched at build time and checked against yt-dlp's published SHA2-256SUMS |
| ffmpeg | mingw build | same audio-only LGPL build (FFmpeg 7.1.1 + LAME 3.100) compiled from source on the Mac runner for both CPUs, joined with `lipo` (`build-ffmpeg-min-mac.sh`); Apple Secure Transport for https instead of schannel |
| Clean up engine | onnxruntime-node 1.31.1 win32 | Apple Silicon: onnxruntime-node 1.31.1 (darwin/arm64). Intel: 1.23.2, the last release with an Intel Mac binary (hash-pinned, fetched from npm at build time). Both are inside the app; Yoink picks by CPU (`src/vocal.js`) |
| Updates | downloads the `.exe`, runs it with `--updated` | downloads the `.dmg`, swaps the app (`src/updater.js`, `macInstallScript`). Reads a new `mac` block in `yoink-version.json` |
| Sign-in token | Electron safeStorage | same code; on Mac safeStorage uses the Keychain |
| Menus and keys | none needed | standard Mac app menu (Cmd+Q, W, M, H, copy/paste), Cmd+K search, Cmd+, settings (`renderer/app.js`, `setupMacMenu` in `src/main.js`) |
| Windows | quits when the window closes | stays in the Dock after the window closes, Dock click reopens, Cmd+Q quits |
| Drag out to Finder or a DAW | works | same code; drag icon falls back to `assets/icon.png` (macOS needs one) |

## Building the .dmg
A `.dmg` can only be made on macOS (it needs Apple's `hdiutil`, `lipo` and `codesign`). Two ways:
- **GitHub Actions (no Mac needed):** push this folder to the repo, open Actions > "Build Yoink for Mac" > Run workflow. It builds, runs the tests, prints the SHA256 and uploads `Yoink.dmg` and `Yoink.dmg.sha256` as a downloadable artifact (about 15-25 minutes). The workflow file is `.github/workflows/build-mac.yml`.
- **On a Mac:** install Node 22 and Xcode command line tools (`xcode-select --install`), then `bash build-mac.sh`. Output: `dist-mac/Yoink.dmg`.

### Signing (optional, not done)
- Default: ad-hoc signed (needed, Apple Silicon will not run completely unsigned apps). Gatekeeper warns on first open.
- With an Apple Developer ID certificate: set `CSC_LINK` (base64 .p12) and `CSC_KEY_PASSWORD`; add `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID` (or `APPLE_API_KEY`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER`) to also notarize. The hardened runtime is already configured (`build/entitlements.mac.plist`). Before the first real-signed run, note that yt-dlp is a PyInstaller program; Apple notarization needs it signed with our certificate and PyInstaller binaries need the signature applied to the bundle correctly. If notarization rejects it, switch to downloading yt-dlp on first search (the app already supports that; remove `bin/yt-dlp` from `build/tools-mac`).

## Putting the .dmg on the site
1. Upload `Yoink.dmg` to the site (proposed link: `https://cmplxty.music/yoink.dmg`; Yoink only accepts download links on cmplxty.music).
2. Add a `mac` block to `yoink-version.json`. Windows users never read it, and Mac users never read the Windows fields:
```json
"mac": {
  "version": "2.1.0",
  "installer": "https://cmplxty.music/yoink.dmg",
  "sha256": "<contents of Yoink.dmg.sha256, first word>",
  "size": <bytes>,
  "notes": "First Mac release."
}
```
Until the `mac` block exists, Mac users are told they are up to date.

## Tests
`npm test` (unchanged Windows suite) and `npm run test:mac` (`tests/updater-mac.js`: Mac version-file parsing, `.dmg` download and checksum, and the app-swap script run against stand-in `hdiutil`/`ditto`/`open` commands). Everything in the table above that touches Apple tools only runs for real on a Mac.
