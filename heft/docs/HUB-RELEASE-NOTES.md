# Prepared only . not applied

The Plugin Hub reads GitHub Releases; no plugins.json/catalog code edit is required. Verified against plugin-hub/src/HubCore.cpp: parseReleases accepts `heft-v1.0.0`, derives bundle `CMPLXTY HEFT` from `CMPLXTY-HEFT-Setup-...`, and typeFor falls back to `FX` for new slugs. SHA256SUMS is needed to enable installation. Drafts/prereleases are ignored. After Sam explicitly approves pushing/tagging:

1. Push the HEFT branch/approved integration and tag `heft-v1.0.0` only with approval.
2. Run heft workflow; verify macOS universal package and native host/installation smoke tests.
3. Prepare a complete release with assets `CMPLXTY-HEFT-Setup-1.0.0-win.exe`, `CMPLXTY-HEFT-Setup-1.0.0-mac.pkg`, combined `SHA256SUMS` and corresponding source archive/licenses.
4. Publish only after the approved release gate. Hub Refresh should discover slug heft/version1.0.0 from tag and installers. Verify Hub download/install/update paths manually before declaring support.
5. Website/Gumroad product and update metadata can be coordinated later; no Bob/Wolf notification or site change performed here.

Suggested release summary: Original density/motion effect with drive/compression SLAM, gated noise colour, mono-preserving width, eight-stage disperser, quadrature frequency shift, transient/tail shaping, four modulation slots, ten presets, user presets and linked sample-peak protection. Windows installer unsigned; Mac artifact pending CI. Not a PHAT clone.
