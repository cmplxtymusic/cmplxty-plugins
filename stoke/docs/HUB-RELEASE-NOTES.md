# STOKE 1.0.0 — hub release notes

Type: **FX**. Slug: `stoke`. Version: 1.0.0.

Clean mid-focus. A single FOCUS knob brings the midrange forward; CENTRE and WIDTH aim the band; GLOW adds warm mid harmonics (oversampled, on that band only); SMOOTH tames harsh peaks and low-mid mud; MID ONLY lifts just the centre of a stereo image. Gain match and a −1 dBFS safety limiter are on by default. At FOCUS zero the plugin is a true wire. Twelve factory presets, user `.stoke` files, live spectrum with the lifted band marked.

Windows installer unsigned; Mac artifact pending CI. Inspired by the idea of a clean, musical mid lift, not a recreation of any existing product.

## Release gate (not yet run)

1. Push the `stoke` branch (or the approved integration) and tag `stoke-v1.0.0` only with approval.
2. Run the stoke workflow; verify the macOS universal package and native host/installation smoke tests.
3. Prepare a complete release with assets `CMPLXTY-STOKE-Setup-1.0.0-win.exe`, `CMPLXTY-STOKE-Setup-1.0.0-mac.pkg`, combined `SHA256SUMS`, and the corresponding source archive and licenses.
4. Publish only after the approved release gate. Hub Refresh should discover slug `stoke` / version 1.0.0 from the tag and installers.
5. Website/Gumroad product and update metadata can be coordinated later; no Bob/Wolf notification or site change was performed here. `mac/install-mac.sh` already lists the `stoke` slug so `all` and `--list` will pick it up once `https://cmplxty.music/plugins/stoke/version.json` exists and has a `mac.url`.

Suggested release summary: Original mid-focus effect. FOCUS brings the mids forward cleanly, GLOW adds warm harmonics on that band, SMOOTH keeps harshness and mud in check, and FOCUS at zero is a perfect wire. Gain match on by default, mid-only mode, live spectrum, 12 factory presets.
