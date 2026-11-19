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

## Known limits

- The transcription tempo (NOTES view, note MIDI) can still be half-time on drum & bass (87); the map tempo is 174.
- Spleeter can put sub bass in "other": the Bass lane then misses it and Music shows it.
- When the melody is sung there is no Lead lane (it is the Vocals lane).
- Section names are a guess from energy: a song with no clear drop gets Intro / Break / Outro only.
