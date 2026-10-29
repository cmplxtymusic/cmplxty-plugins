# CMPLXTY VVITCH

Free witch house polysynth by CMPLXTY: glassy detuned saws, chip-crushed leads, ghostly choral pads, pitched-down DRAG and huge dark
reverb. It is inspired by the sound of witch house and early-2010s dark electronic acts (Crystal Castles and friends) but is its own
design and not a clone of any product. Instrument plug-in: MIDI in, stereo out, zero latency.

### Controls
1. **OSCILLATOR 1 / 2**: **Wave** Saw / Square / PWM / Triangle (polyBLEP, alias-suppressed), Osc 1 **Octave** ±2, Osc 2 **Semi** ±24 and
   **Fine** ±50 ct, **Level**.
2. **PULSE**: pulse **Width**, **PWM** depth and **Rate** (for the PWM wave).
3. **UNISON**: **Voices** 1-7 per oscillator, **Detune**, stereo **Width**.
4. **NOISE**: **Level** and **Tone** (dark to hissy).
5. **CHORAL VOX**: a 5-vowel formant stage (A-E-I-O-U): **Amount**, **Vowel**, **Motion** (slow vowel drift, a ghost choir).
6. **FILTER**: **Type** LP 24 / LP 12 / Band Pass / High Pass, **Cutoff**, **Reso**, **Drive** (pre-filter saturation), **Env** ±, **Key** tracking.
   **FILTER ENV** A / D / S.
7. **AMP ENV**: ADSR, 1 ms-10 s.
8. **PITCH DROP**: every note starts above (or below) pitch and falls into place: **Drop** ±24 st, **Time**.
9. **DRAG** (power switch): the whole synth pitched down and slowed, chopped-and-screwed style. **Down** 0-24 st.
10. **TAPE**: **Wow**, **Flutter**, **Age** (darker, noisier, wobblier).
11. **CHIP**: **Bits** 2-16, **Rate** (sample-rate crush down to 200 Hz), **Mix**.
12. **MOTION**: LFO **Rate**, **Shape** (Sine / Triangle / Saw / Square / S&H), to **Cutoff** and to **Pitch**.
13. **ARP** (power switch): **Mode** Up / Down / Up-Down / Random / Chop, **Rate** 1/4-1/32 (triplets too, synced to the host tempo),
    **Octaves** 1-4, **Gate**.
14. **VOICE**: **Voices** 1-12 (1 = mono legato), **Glide**, **Volume**.
15. **DELAY** (dark ping-pong): **Time**, **Feedback**, **Mix**. **REVERB** (dark 8-line FDN): **Size**, **Dark**, **Mix**.

MIDI: notes with velocity, pitch bend (±2 st), mod wheel (CC1, vibrato), sustain (CC64), all notes off / all sound off. Notes are
sample-accurate. The display shows the output waveform, the live filter curve, the vowel and pitch (drop / drag), and the voices, LFO and level.
A peak limiter keeps the output under -0.5 dBFS.

### Presets
48 factory presets in 6 categories, plus Init:
- **Leads:** Glass Coffin, Shattered Halo, Chip Scream, Untrust Siren, Broken Mirror, Pale Static, Cathedral Wire, Hex Sync.
- **Pads:** Haunted Nave, Funeral Silk, Grave Light, Black Veil, Lantern Fog, Crypt Bloom, Snowblind, Midnight Mass.
- **Bass:** Drag Abyss, Tombstone Sub, Rust Bell Bass, Slow Grave, Ninja Drop, Hollow Body, Ash Reese, Wraith 808.
- **Vox:** Choir Of Ghosts, Chopped Hymn, Oooh Below, Saint Static, Ave Machina, Stutter Prayer, Lament Pitchdown, Hollow Vowels.
- **FX:** Tape Funeral, Static Ritual, Ascension, Dead Signal, Bell Tower Gone, Wind In The Nave, Glitch Exorcism, Power Down.
- **Keys:** Cursed Music Box, Bone Pluck, Candle Arp, Ice Chapel Keys, Necro Organ, 8-Bit Requiem, Velvet Coffin EP, Ritual Sequence.

Use the **< >** buttons to step through them, or click the name for the menu (grouped by category): **Save preset as...**,
**Load preset file...**, **Open user preset folder** and every factory and user preset.
Your presets are `.cmplxtypreset` files in `%APPDATA%\CMPLXTY\VVITCH\Presets` (Windows) or `~/Library/Application Support/CMPLXTY/VVITCH/Presets` (Mac).
Installers and updates never touch that folder.

- Monochrome UI. Undo/redo (preset loads too). Double-click a knob to reset it. Right-click any control for its menu (**Reset**,
  **Type in value**, **Copy / Paste**, **Automate**, **Init all**; shortcut letters underlined). The editor never takes keyboard focus, so
  **Space reaches the host**.
- Optional **update notice**: at most one `GET https://cmplxty.music/plugins/vvitch/version.json` per 24 h, on a background thread.
  It is silent on failure and sends no identifiers. Choices: Update / Later / Skip this version, and you can switch it off in the UI.

## Algorithm (short)
Two polyBLEP oscillators per voice with up to 7 unison copies spread in pitch and pan, plus filtered noise, go through a parallel
formant bank (5 vowels, morphing), a tanh drive and a TPT state-variable filter (two in series for LP 24), then the amp envelope.
Pitch = note + glide + per-note drop + DRAG + LFO / wheel vibrato. The summed voices pass the CHIP stage (bit depth + sample-and-hold),
TAPE (modulated delay line for wow / flutter, low-pass and hiss for age), a dark ping-pong delay, an 8-line feedback-delay-network
reverb with damping, volume and a peak limiter. Voice stealing takes the quietest released voice first, then the oldest.

## Install (users)
Windows: run `CMPLXTY-VVITCH-Setup-<version>-win.exe`. Installing over an old version updates it in place and keeps your settings and presets.
macOS: see `HOW-TO-OPEN-ON-MAC.md`. The installers are **unsigned**.

## Build (developers)
```
scripts/fetch-deps.sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=OFF -DCLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES=OFF
cmake --build build/linux --target Vvitch_VST3 Vvitch_CLAP
scripts/build-windows-xwin.sh && scripts/build-installer.sh   # Windows installer (cross-built)
scripts/build-macos-pkg.sh                                    # macOS
```

### Tests
- `test/dsp_test.cpp`: A4 pitch, octave / semi, DRAG, pitch drop, release to silence, polyphony, arp, glide, a stress fuzz and
  worst-case speed (12 voices x 7 unison x 2 oscillators).
- `test/update_test.cpp`: the update notice (78 checks).
- `test/screenshot.cpp`: modes `keys` (focus, menu and preset audits), `params`, `proc` and `presets`. `proc` covers MIDI through
  processBlock, sample-accurate note start, bend, CC1 / CC64 / CC120 / CC123, the voice count, mono output, every preset value naming a
  real parameter in range, **every factory preset playing a chord at 44.1 / 48 / 96 kHz (finite, peak <= 0.95, audible)**, user presets and
  state round trips.
- Validated with pluginval (strictness 5) and clap-validator on Linux.

Font: UnifrakturMaguntia (SIL Open Font License 1.1). Code: GPLv3.
