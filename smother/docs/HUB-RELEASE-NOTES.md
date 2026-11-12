# Prepared only, not applied

The Plugin Hub reads GitHub Releases; no plugins.json or HubCore catalog edit is required. Verified against plugin-hub/src/HubCore.cpp: `parseReleases` accepts a `smother-v1.0.0` tag, derives bundle `CMPLXTY SMOTHER` from `CMPLXTY-SMOTHER-Setup-...`, and `typeFor` falls back to a generic **FX** label for new slugs. Because SMOTHER is a drum instrument, Bob should be told the type label is **Drum Machine / Instrument** (not FX) when the listing is added. SHA256SUMS is needed to enable installation. Drafts and prereleases are ignored. After Sam explicitly approves pushing and tagging:

1. Push the `smother` branch (or the approved integration) and tag `smother-v1.0.0` only with approval.
2. Run the smother workflow; verify the macOS universal package and native host/installation smoke tests.
3. Prepare a complete release with assets `CMPLXTY-SMOTHER-Setup-1.0.0-win.exe`, `CMPLXTY-SMOTHER-Setup-1.0.0-mac.pkg`, combined `SHA256SUMS`, and the corresponding source archive and licenses.
4. Publish only after the approved release gate. Hub Refresh should discover slug `smother` / version 1.0.0 from the tag and installers. Verify Hub download/install/update paths manually before declaring support.
5. Website/Gumroad product and update metadata can be coordinated later; no Bob/Wolf notification or site change was performed here.

Suggested release summary: Original sleeve drum machine with a 16-lane polymeter step grid, drawable MUTE / LOWPASS / HIGHPASS / CRUSH / DISTORT / FLANGE / DELAY / REVERB sleeves (attack, release, depth, curve, ALL-row and sleeve humanize), 12 synthesized kits across eight drum types, user-sample drop with start/end chop, per-lane EQ and FX, a master HP/LP/OTT chain, a −1 dBFS limiter, host-transport sync, GM-style MIDI triggering, MIDI and WAV drag-out, 22 factory presets and user `.smother` files. Windows installer unsigned; Mac artifact pending CI. Inspired by the idea of painting processing over a beat, not a clone of any existing product.
