# CMPLXTY Cyber Wavetable

Free polyphonic wavetable synth by CMPLXTY, made for cyberpunk, synthwave and dark electronic sounds: two morphing wavetable
oscillators with unison, a sub and noise, a multimode filter, two envelopes, two LFOs and a 4-slot mod matrix. It is inspired by the
classic wavetable synths but is its own design and not a clone of any product. Instrument plug-in: MIDI in, stereo out, zero latency.

### Controls
1. **OSC A / OSC B** (power switches): **Table** (Analog sine > triangle > saw > square > pulse, Digital, Vocal formants A-E-I-O-U,
   Sync sweep, Metal FM, Chip 4-bit), **Position** morphs smoothly through the table's 8 frames, **Semi** ±24, **Fine** ±100 ct,
   **Unison** 1-7 voices, **Detune**, **Level**.
2. **MIX**: **Sub** (sine one octave down), **Noise**, unison **Width** (stereo spread of the unison voices).
3. **FILTER**: **Type** LP 12 / LP 24 / Band Pass / High Pass, **Cutoff** 20 Hz-20 kHz, **Reso**, **Drive** (pre-filter saturation),
   **Env** ±100 % (mod envelope to cutoff, up to ±6 octaves), **Key** tracking.
4. **AMP ENV** and **MOD ENV**: ADSR, 1 ms-10 s.
5. **LFO 1 / LFO 2**: **Rate** 0.01-30 Hz, **Shape** Sine / Triangle / Saw / Square / S&H (free-running, shared by all voices).
6. **VOICE**: **Voices** 1-16 (1 = mono legato with last-note priority), **Glide**, **Volume**.
7. **MOD 1-4**: **Source** (LFO 1, LFO 2, Mod Env, Velocity, Mod Wheel, Note, Pressure) > **Target** (A / B / both positions,
   A / B / global pitch, cutoff, resonance, A / B level, detune, pan, amp, noise) with a bipolar **Amount**.

MIDI: notes with velocity, pitch bend (±2 st), mod wheel (CC1), sustain (CC64), channel / poly pressure, all notes off / all sound off.
Notes are sample-accurate. The display shows both oscillators' live wavetable frame in front of the table's other frames,
the filter curve at its live (modulated) cutoff, both LFOs, the envelope, the active voice count and the output level.

### Presets
26 factory presets: Init, Neon Supersaw, Night City Pad, Chrome Pluck, Grid Runner Bass, Replicant Lead, Data Stream Arp, Synthwave Brass,
Vocoder Ghost, Glitch Sync, Megacorp Reese, Hologram Bells, Acid Implant, Chiptune Hacker, Rain On Chrome, Overdrive Protocol,
Deep Net Drone, Cyber Organ, Laser Grid Stab, Android Choir, Sub Zero Kick Bass, Static Interference, Pressure Wave, Neural Keys,
Blackout Siren, Metro Rain Lead.
Use the **< >** buttons to step through them, or click the name for the menu: **Save preset as...**, **Load preset file...**,
**Open user preset folder** and every factory and user preset.
Your presets are `.cmplxtypreset` files in `%APPDATA%\CMPLXTY\Cyber Wavetable\Presets` (Windows) or `~/Library/Application Support/CMPLXTY/Cyber Wavetable/Presets` (Mac).
Installers and updates never touch that folder.

- Monochrome UI. Undo/redo (preset loads too). Double-click a knob to reset it. Right-click any control for its menu (**Reset**,
  **Type in value**, **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so
  **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/cyber-wavetable/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
Each table frame is drawn once, then band-limited by FFT into 11 mip levels (one per octave, harmonics kept below Nyquist), so nothing
aliases at any pitch or sample rate. Playback reads two mip levels' neighbouring frames with linear interpolation (position morph).
Unison voices are spread evenly in pitch and pan. The filter is a TPT state-variable filter (two in series for LP 24) with a tanh
pre-drive; modulation, glide and filter coefficients update every 16 samples with per-sample ramps. Envelopes: linear attack,
exponential decay / release. Voice stealing takes the quietest released voice first, then the oldest.

## Install (users)
Windows: run `CMPLXTY-Cyber-Wavetable-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings and presets.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target CyberWavetable_VST3 CyberWavetable_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: tables (normalised, morphing, top mip a pure sine), mip choice band-limited at 44.1 / 48 / 96 kHz, pitch
  (440 Hz within 0.2 Hz, semi / fine, bend), release to silence, polyphony cap and stealing, mono legato with note stack, glide, sustain,
  LP 12 / LP 24 / HP / filter envelope / full resonance, unison stereo, width 0 = mono, sub, noise, mod matrix (LFO > cutoff, velocity > amp,
  wheel > pitch, LFO > position), a 120-patch fuzz at 3 rates and random block sizes, and worst-case speed (16 voices x 14 oscillators).
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus, menu and preset audits), `params`, `proc` and `presets`. `proc` covers MIDI notes through
  processBlock, sample-accurate note start, pitch bend, CC1 / CC64 / CC120 / CC123, the voice count, notes queued from the editor, mono
  output, **every factory preset playing a chord with bend, wheel and pressure at 44.1 / 48 / 96 kHz (finite, bounded, audible)**, a user
  preset file round trip, and state round trip (including the preset name).
- Validated with pluginval (strictness 5) and clap-validator on Linux.
