# REWIRED 1.1.0: Map mode, measurements

Test songs: `python3 test/make_map_songs.py OUTDIR [--vocal speech.wav]` renders 4 arranged songs with a truth table
(sections, bar 1, which part plays in which bar). Run each through `RewiredCli --song mix.wav --map m.mid --map-json m.json`
(or the stems), then `python3 test/map_eval.py truth.json m.json m.mid`.
The measurements below used the speech vocal option (a local spoken recording, not in the repo).

## Song mode (full mix -> Spleeter -> map)

| song | length | bpm (map) | bar 1 error | boundaries within 1 bar | mean boundary error | labels right | lane-bars right | map time |
|---|---|---|---|---|---|---|---|---|
| dubstep140 | 126 s | 140 (true 140) | -0.02 beat | 6/6 | 0.01 bars | 100 % | 96.3 % | 248 ms |
| house128 (0.6 s lead-in) | 184 s | 128 (128) | -0.01 beat | 7/7 | 0.00 | 100 % | 96.9 % | 301 ms |
| dnb174 | 180 s | 174 (174) | -0.03 beat | 4/6 | 1.34 | 93.8 % | 92.3 % | 353 ms |
| midtempo100 (1.2 s lead-in) | 148 s | 100 (100) | -0.01 beat | 5/5 | 0.00 | 100 % | 96.3 % | 295 ms |

Total: 22/24 boundaries within 1 bar, and 22 of the 24 boundaries the map drew are within 1 bar of a real one (the 2 misses are on dnb).
Audio lanes (Drums, Bass, Music, Vocals) 93-100 %; note lanes are lower: Hats 84-91 % (they run into the gaps),
Chords 80-88 %, Lead (dnb only) 71 % (transcription puts lead notes where there is no lead).

## Stems mode (bass / melody=vocal / chords / drums stems)

dubstep 6/6 boundaries, lanes 98.1 %; house 6/7, 99.0 %; dnb 5/6, 99.0 %; midtempo 4/5, 99.6 %. Labels 93-100 %.

## Map MIDI parse-back (mido)

Every song: type 1, 960 ppq, tempo = map bpm, one marker per section + End (+ "Song start" when the song does not start on bar 1),
markers on the right bars, one named track per lane, blocks only on bars where the lane is on.

## Run time (sequential, same box, song mode)

| song | 1.0.1 | 1.1.0 (with map) | transcription MIDI |
|---|---|---|---|
| dubstep140 | 37.7 s | 38.4 s | identical |
| house128 | 55.3 s | 54.8 s | identical |
| dnb174 | 39.0 s | 40.2 s | identical |
| midtempo100 | 46.3 s | 44.9 s | identical |

The map adds 0.25-0.35 s (0.3-0.8 s from stems); the rest is run-to-run noise. Note MIDI from 1.1.0 is byte-identical to 1.0.1.

## Rework (after the first review): tempo edit, play / solo, controls

Map build time (envelopes in, map out; the envelopes come from the separation pass and are saved with the project):

| song | before (bd8e1fd) | rework | re-grid to a new tempo |
|---|---|---|---|
| dubstep140 | 248 ms | 4-8 ms | same |
| house128 | 301 ms | 6 ms | same |
| dnb174 | 353 ms | 9-25 ms | see below |
| midtempo100 | 295 ms | 3 ms | same |

Accuracy is unchanged by the rework (song mode 22/24 boundaries within 1 bar, labels 98 %, lanes 95.4 %; stems 21/24, lanes 99 %),
see the tables above. What changed:

- The tempo is a whole number. NOTES MIDI now uses the map tempo: drum & bass writes 174 (it was 87), dubstep 140 (was 139.68).
  The note times are the same; only the file tempo (and so the ticks) changed.
- A tempo you set (BPM box, - / +, 1/2, x2, typing, the TEMPO knob or automation) re-grids sections, lanes, bar 1, quantize and
  both MIDI files without processing again. 1/2, 1x, 2x (and 1/4, 4x) keep the detected bar 1.
- Song mode moves a sub bass that Spleeter left in "other" from Music to Bass, only where the sub band is most of that stem and loud
  next to the bass stem. Neutral on the 4 test songs (they have no such sub); on two real songs it changed 1 bar.

Run time, sequential, song mode, same box (the rework with `--preview 1`, which also keeps the playback audio as the plugin does):

| song | bd8e1fd | rework |
|---|---|---|
| dubstep140 | 39.5 s | 39.3 s |
| house128 | 57.3 s | 57.8 s |
| dnb174 | 41.3 s | 41.2 s |
| midtempo100 | 47.5 s | 46.5 s |

Re-grid of dnb174 (180 s) to 87 / 174 / 120 bpm: 9.3 ms each. The playback copy costs 4 bytes per frame of the mix
(16-bit stereo, 44.1 kHz) plus 2 bytes per frame for each of the 4 stems at 22 kHz: about 64 MB for a 3-minute song (not saved in the project).

## Controls audit (all tested in `RewiredScreenshot controls`, unless noted)

| control | what it does now | saved in project | test |
|---|---|---|---|
| PLAY / STOP | plays the song (or stems sum) from the playhead, 8 ms fades | position no, solo yes | audio level out of processBlock, stops, silent after |
| playhead / lane click | moves the playhead (map and NOTES) | - | position within 0.15 s |
| section click (ruler) | plays from the section start | - | position = section start |
| DAW transport | DAW starting playback stops the preview | - | test play head |
| Space | play / stop, Standalone app only (a plugin never takes the keyboard) | - | key audit: 0 components take focus |
| M (per lane) | REAL MIDI: notes instead of blocks in the map file, drawn as a mini piano roll; a soloed lane then plays its notes | yes | map file note count, solo source switch |
| S (per lane) | solo: audio lanes play their stem, note lanes play their notes on a small synth; others dim | yes | stem level, synth audible, round trip |
| BPM box | drag (4 px / bpm), wheel, double-click to type a number or AUTO | yes (Tempo param) | 174, 86.6 -> 87, junk ignored, auto |
| - / + / 1/2 / x2 | whole-bpm steps, half, double | yes | bars 8 -> 16 -> 8 -> 4, bar 1 kept |
| AUTO | on: detected tempo; click off: keep the shown tempo as yours | yes | both directions |
| TEMPO knob | same parameter as the box (AUTO / whole bpm), automatable | yes | 140 bpm -> 10 bars |
| QUANTIZE | live, on the bar-1 grid of the current tempo; Off restores the played notes | yes | grid check, Off check |
| detection knobs + MELODY FROM | mark the result stale: PROCESS becomes RE-PROCESS with a note | yes | all 7, and back |
| UNDO / REDO | tempo edits are undoable (APVTS) | - | undo / redo re-grid |
| NOTES / MAP tabs | switch view | yes | both |
| part switches (7) | NOTES view only; leave the part out of EXPORT SELECTED | yes | toggle + export |
| CLEAR | stops playback, clears inputs and result | - | playing off, PLAY disabled |
| LOAD SONG / LOAD STEMS / slot boxes / drops | load inputs (drops: `drop` audit) | yes | drop audit |
| EXPORT ALL / SELECTED / MAP, DRAG chips | write the files (tests call the same functions; the buttons also reveal the file) | - | proc tests |
| + N INSTR. | opens instrument sub-tracks | - | `subs` screenshot |
| presets bar, menu | factory / user presets | yes | preset audit |
| UPDATE CHECK | on / off (1.0.x, unchanged) | settings file | not automated |

Dead or wrong controls found and fixed: AUTO was greyed out (looked dead) while on; the TEMPO knob read "AUTO bpm" and 0.1-bpm steps;
"MIDI SOLO" headers overlapped; typing "86.6" gave 86 and junk/"auto" gave 20 bpm; STOP reported "playing" until the fade ended;
a 1/2 tempo near 0 s lost bar 1 (anchor sentinel bug); a preview still loading when the song changed could play the old song;
the plugin build used a test-only message loop call.

## Known limits

- The beat tracker alone still reads drum & bass as 87; the map's octave check makes it 174 and everything uses that. If a song is
  read at the wrong octave, press 1/2 or x2.
- Space plays / stops only in the Standalone app; in a DAW use PLAY (Space stays the DAW's transport).
- After reopening a song-mode project the separated stems are not saved, so solo on an audio lane plays its notes until you PROCESS again (stems mode reloads your stem files).
- When the melody is sung there is no separate Lead lane: the Vocals lane carries it (M on Vocals = the sung melody notes).
- drum & bass test song: 4/6 boundaries within 1 bar (two 8-bar builds found as 4 bars).
- Section names are a guess from energy: a song with no clear drop gets Intro / Break / Outro only.
