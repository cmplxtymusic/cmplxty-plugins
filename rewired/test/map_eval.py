#!/usr/bin/env python3
"""Score a Rewired song map against make_map_songs.py truth.
map_eval.py truth.json map.json [map.mid]  -> prints a report, writes nothing; --json prints one JSON line instead.
 - tempo / downbeat: map bpm vs truth, bar-1 position error in beats
 - sections: every true boundary (not the song start / end) -> distance in bars to the nearest map boundary; hit = within 1 bar;
   precision = map boundaries within 1 bar of a true one; label accuracy = share of bars whose label type matches
 - lanes: per true bar, a lane is "on" when its blocks cover >= 40 % of the bar (drum pieces: >= 20 %); accuracy / precision / recall
   against the truth table (Drums, Kick, Snare, Hats, Cymbals, Bass, Lead, Chords, Vocals; Music is scored against Lead or Chords)
 - MIDI: parsed back with mido: markers, track names and block notes must match the map"""
import sys, json, numpy as np
args = [a for a in sys.argv[1:] if not a.startswith('--')]
T = json.load(open(args[0])); M = json.load(open(args[1])); mid = args[2] if len(args) > 2 else None
tb = 240.0 / T['bpm']; nb = T['bars']; off = T['offset']
def tbar(sec): return (sec - off) / tb
mb = 240.0 / M['bpm']
def mtime(bar): return M['firstDownbeat'] + bar * mb
rep = {'song': T['name']}
rep['bpm_true'] = T['bpm']; rep['bpm_map'] = round(M['bpm'], 3); rep['bpm_detected'] = M.get('transcribeBpm'); rep['bpm_notes_midi'] = M.get('bpmOut', M.get('transcribeBpm'))
d = (M['firstDownbeat'] - off) / (tb / 4); d = (d + 2) % 4 - 2; rep['downbeat_err_beats'] = round(d, 2)
# sections
true_b = [s[1] for s in T['sections'][1:]]
map_b = [tbar(mtime(s[2])) for s in M['sections'][1:]]
errs = [min([abs(t - m) for m in map_b] or [99]) for t in true_b]
rep['boundaries_true'] = len(true_b); rep['boundaries_map'] = len(map_b)
rep['boundary_mean_abs_err_bars'] = round(float(np.mean(errs)), 2)
rep['boundary_hit_1bar'] = f"{sum(e <= 1.0 for e in errs)}/{len(errs)}"
rep['boundary_precision_1bar'] = f"{sum(min([abs(m - t) for t in true_b] or [99]) <= 1.0 for m in map_b)}/{len(map_b)}"
names = ['Intro', 'Build', 'Drop', 'Break', 'Outro']
def lab_true(b):
    for s in T['sections']:
        if s[1] <= b < s[2]: return s[0]
def lab_map(b):
    t = off + (b + 0.5) * tb
    for s in M['sections']:
        if mtime(s[2]) <= t < mtime(s[3]): return names[s[1]]
    return None
rep['label_acc'] = round(np.mean([lab_true(b) == lab_map(b) for b in range(nb)]), 3)
rep['map_sections'] = ' '.join(f"{s[0]}[{tbar(mtime(s[2])):.1f}-{tbar(mtime(s[3])):.1f}]" for s in M['sections'])
rep['true_sections'] = ' '.join(f"{s[0]}[{s[1]}-{s[2]}]" for s in T['sections'])
# lanes
def lane(name):
    for L in M['lanes']:
        n = L['name']
        if name in ('Kick', 'Snare', 'Hats', 'Cymbals') and n == name: return L
        if name in ('Drums', 'Vocals', 'Music') and n == name: return L
        if name in ('Bass', 'Lead', 'Chords') and not L['child'] and (n == name or n.startswith(name + ' (')): return L
def cover(L, b):
    a, z = off + b * tb, off + (b + 1) * tb; c = 0.0
    for s, e, v in L['blocks']: c += max(0.0, min(e, z) - max(s, a))
    return c / tb
lanes = {}; tot = [0, 0, 0, 0]   # tp fp fn tn
for name in ['Drums', 'Kick', 'Snare', 'Hats', 'Cymbals', 'Bass', 'Lead', 'Chords', 'Music', 'Vocals']:
    L = lane(name)
    if L is None: lanes[name] = 'n/a'; continue
    truth = T['lanes'][name] if name != 'Music' else [max(a, b) for a, b in zip(T['lanes']['Lead'], T['lanes']['Chords'])]
    th = 0.2 if name in ('Kick', 'Snare', 'Hats', 'Cymbals') else 0.4
    pred = [int(cover(L, b) >= th) for b in range(nb)]
    tp = sum(p and t for p, t in zip(pred, truth)); fp = sum(p and not t for p, t in zip(pred, truth))
    fn = sum(t and not p for p, t in zip(pred, truth)); tn = nb - tp - fp - fn
    for i, v in enumerate((tp, fp, fn, tn)): tot[i] += v
    lanes[name] = dict(acc=round((tp + tn) / nb, 3), prec=round(tp / max(1, tp + fp), 3), rec=round(tp / max(1, tp + fn), 3), src='audio' if L['audio'] else 'notes')
rep['lanes'] = lanes
rep['lanes_overall_acc'] = round((tot[0] + tot[3]) / max(1, sum(tot)), 3)
rep['map_build_ms'] = round(M['buildMs'], 1); rep['run_s'] = round(M['time'], 1)
if mid:
    import mido
    f = mido.MidiFile(mid); names_ = []; markers = []; notes = []
    for tr in f.tracks:
        nm = None; cnt = 0; t = 0
        for msg in tr:
            t += msg.time
            if msg.type == 'track_name' and nm is None: nm = msg.name
            if msg.type == 'marker': markers.append((t / f.ticks_per_beat / 4, msg.text))
            if msg.type == 'note_on' and msg.velocity > 0: cnt += 1
        names_.append(nm); notes.append(cnt)
    tempo = [m.tempo for m in f.tracks[0] if m.type == 'set_tempo'][0]
    want = ['Sections'] + [L['name'] for L in M['lanes']]
    sec_markers = [m for m in markers if m[1] not in ('Song start', 'End')]
    ok_names = names_[1:] == want
    ok_markers = [m[1] for m in sec_markers] == [s[0] for s in M['sections']]
    lead_in = max(0, int(np.ceil((M['firstDownbeat'] - 0.03) / mb)))
    ok_pos = all(abs(m[0] - (s[2] + lead_in)) < 1e-3 for m, s in zip(sec_markers, M['sections']))
    ok_blocks = all(n == len(L['blocks']) for n, L in zip(notes[2:], M['lanes']))
    rep['midi'] = dict(type=f.type, tracks=len(f.tracks), bpm=round(mido.tempo2bpm(tempo), 3), markers=len(markers), names_ok=ok_names,
                       markers_ok=ok_markers, marker_bars_ok=ok_pos, blocks_ok=ok_blocks, song_start=[m[0] for m in markers if m[1] == 'Song start'])
if '--json' in sys.argv: print(json.dumps(rep)); sys.exit(0)
for k, v in rep.items():
    if k == 'lanes':
        for n, x in v.items(): print(f"  lane {n:8s} {x}")
    else: print(f"{k:28s} {v}")
