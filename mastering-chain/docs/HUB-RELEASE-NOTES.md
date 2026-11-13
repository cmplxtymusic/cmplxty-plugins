# GLAZE 1.0.0 — hub release notes

Type: **FX**. Slug: `mastering-chain`. Product name: CMPLXTY GLAZE. Version: 1.0.0. Tag: `mastering-chain-v1.0.0`.

Mastering chain for the master track, tuned for bass music. EQ, glue compressor, three-band multiband leveller, 8x oversampled saturation (TAPE / TUBE / HARD), stereo width with bass mono, and a lookahead true-peak limiter with BS.1770 loudness metering (momentary, short-term, integrated, true-peak max). Sixteen factory presets for bass house, midtempo and cyberpunk, plus user `.glaze` presets. Only the limiter is active by default; everything else is transparent until you use it. Reports 262 samples (5.5 ms) of latency.

Windows installer unsigned; Mac artifact pending CI. Inspired by the usual mastering-session order and by how respected mastering tools behave, not a recreation of any existing product.

## Release gate (not yet run)

1. Push the `mastering-chain` branch (or the approved integration) and tag `mastering-chain-v1.0.0` only with approval.
2. Run the mastering-chain workflow; verify the macOS universal package and native host / installer smoke tests.
3. Prepare a complete release with assets `CMPLXTY-GLAZE-Setup-1.0.0-win.exe`, `CMPLXTY-GLAZE-Setup-1.0.0-mac.pkg`, combined `SHA256SUMS`, source archive and licenses.
4. Hub Refresh should discover slug `mastering-chain` / version 1.0.0 from the tag and installers.
5. Website / Gumroad listing and `version.json` (`https://cmplxty.music/plugins/mastering-chain/version.json`, with a `mac.url`) are coordinated with Bob and Wolf after approval. `mac/install-mac.sh` already lists the `mastering-chain` slug.

Suggested summary: Mastering chain for bass music. EQ, glue, multiband, saturation, width with bass mono and a true-peak limiter with LUFS metering, 16 presets, only the limiter active by default.
