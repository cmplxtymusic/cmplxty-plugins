# Quartz 1.0.4 (Windows x64 VST3: Quartz engine + ALTER voice manipulator)

Install: run `Quartz-Setup.exe` (per-user or all-users; updates in place, your presets in `%APPDATA%\Quartz` are kept), then in
FL Studio: Options > Manage plugins > Find plugins. Manual install: see README-manual-install.txt in the zip.

## Mouse and keyboard (every knob)
* Double-click the knob = reset to its default (one undoable change). Double-click the value text under it = type a number.
* Right-click a knob: menu with underlined shortcut letters, pressed while the menu is open:
  **C** Copy value, **V** Paste value, **D** Reset to default, **E** Enter value, **A** Create automation clip (host menu),
  plus *Bypass this knob* and the carrier items where they exist. The copied value is shared by all knobs and by all Quartz
  instances loaded in the same host process (it is the knob's 0..100 % position, so it maps onto a knob with another range proportionally);
  with nothing copied, Paste also accepts a plain number from the Windows clipboard (e.g. "12" or "-3.5 st").
* Right-click the QUARTZ ENGINE / ALTER diamond: Switch off (0) / Switch on (100) and Create automation clip. The toggles are
  ordinary host parameters (0 = off, 1 = on).
* "Create automation clip" is the HOST's own function; Quartz asks the host for its parameter menu (VST3 IComponentHandler3). If
  the host offers no such entry, the host's own parameter menu is shown instead.

* **Space and every other DAW key keep working** (since 1.0.3): Quartz does not keep the keyboard after you touch a knob. It only
  takes the keyboard while a right-click menu or the "type a value" box is open, and gives it straight back to the host afterwards.
  (Undo: use the UNDO button in the header; the Ctrl+Z shortcut only works if the host itself puts keyboard focus on the Quartz window.)

## ALTER Key / Scale: "--"
Both default to "--" = no auto-tune / no note quantizing. If EITHER Key or Scale is "--":
Transpose mode = plain transpose; Quantize mode = plain transpose (nothing is snapped); Robot mode = one fixed robot note
(not snapped to a scale). Only the manual Pitch / Formant / Gender controls act. Pick a real key AND scale to auto-tune.
Presets that are meant to auto-tune (Hard Tune, Pop Quantize, Robot ...) carry a key and scale; all others use "--".
Sessions saved with 1.0.0 / 1.0.1 load unchanged (their key/scale are kept, e.g. C + Chromatic keeps quantizing).

## Automation keeps resetting a knob in FL Studio (clip deleted, knob still jumps back)
This is FL Studio's "Initialized controls": FL remembers the first value of any control it has ever automated and re-applies it
every time playback starts, even after the clip is deleted. Fix: right-click the control > **Delete init**, or remove the entry
under Browser > Current project > Initialized controls. Quartz itself never writes parameter values back to the host, and every
edit made in the Quartz window is sent as a proper begin/perform/end gesture.

## Update notice and privacy
When the editor opens (at most once per 24 h; after a failed check at most once per 6 h) Quartz makes ONE plain HTTPS GET of
`https://cmplxty.music/quartz-engine/version.json` on a background thread (never the audio thread, 4 s timeout, silent if offline
or blocked). Nothing is sent but the request itself: no analytics, no identifiers, no cookies, no body; the request carries the
default User-Agent of the JUCE network library ("juce") only. If the file names a newer version a small banner shows
"Quartz X.Y.Z is available" with UPDATE (opens https://cmplxty.music/quartz-engine/ in your browser; Quartz does not download or
run anything), LATER and SKIP THIS VERSION.
Turn it off: click the version text in the plug-in footer > untick "Check for updates automatically". The setting and the cache
live in `%APPDATA%\Quartz\settings.json` and `update-cache.json` (not host parameters, not in your project). Delete the files to reset.

## Build
See BUILD.txt. JUCE 8.0.9 (patched for MinGW: patches/juce-8.0.9-mingw.patch), AGPLv3.
