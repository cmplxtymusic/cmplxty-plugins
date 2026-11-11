# Prepared only, not applied

The Plugin Hub reads GitHub Releases; no plugins.json/catalog code edit is required. Verified against plugin-hub/src/HubCore.cpp: parseReleases accepts `fallout-v1.0.0`, derives bundle `CMPLXTY FALLOUT` from `CMPLXTY-FALLOUT-Setup-...`, and typeFor falls back to a generic type for new slugs. Because FALLOUT is an instrument, Bob should be told the type label is **Synth** / **Instrument** (not FX) when the listing is added. SHA256SUMS is needed to enable installation. Drafts/prereleases are ignored. After Sam explicitly approves pushing/tagging:

1. Push the FALLOUT branch/approved integration and tag `fallout-v1.0.0` only with approval.
2. Run the fallout workflow; verify the macOS universal package and native host/installation smoke tests.
3. Prepare a complete release with assets `CMPLXTY-FALLOUT-Setup-1.0.0-win.exe`, `CMPLXTY-FALLOUT-Setup-1.0.0-mac.pkg`, combined `SHA256SUMS` and corresponding source archive/licenses.
4. Publish only after the approved release gate. Hub Refresh should discover slug fallout/version 1.0.0 from the tag and installers. Verify Hub download/install/update paths manually before declaring support.
5. Website/Gumroad product and update metadata can be coordinated later; no Bob/Wolf notification or site change performed here.

Suggested release summary: Original 303-style monophonic acid bass with six anti-aliased oscillators, a 4-pole resonant ladder, accents, slides, a 1–32 step pitch grid with micro/rubato timing, host-sync delay, MIDI live/transpose, pattern drag-out as MIDI and WAV, 26 factory presets, user presets and a linked sample-peak limiter. Windows installer unsigned; Mac artifact pending CI. Not a NUKE clone.
