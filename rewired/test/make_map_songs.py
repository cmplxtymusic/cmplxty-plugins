#!/usr/bin/env python3
"""Render arranged test songs with a known structure for the Rewired MAP mode tests.
make_map_songs.py OUTDIR [--vocal FILE]
Each song: mix.wav (stereo 44.1k), stems/{bass,melody,chords,drums,vocals}.wav and truth.json:
  bpm, offset (seconds before bar 1), bars, sections [[label, startBar, endBar]], lanes {Drums, Kick, Snare, Hats, Cymbals, Bass, Lead, Chords, Vocals: [0/1 per bar]}.
Vocals are phrases cut from FILE (any speech / singing recording) when given, otherwise a formant-synth "ah" voice.
Everything else is synthesised here (numpy), so the truth is exact."""
import sys, os, json, numpy as np, soundfile as sf
from scipy.signal import lfilter, resample_poly
SR = 44100
def hz(p): return 440.0 * 2 ** ((p - 69) / 12)
def onepole(x, fc): a = np.exp(-2*np.pi*fc/SR); return lfilter([1-a], [1, -a], x)
def adsr(n, a, d, s, r):
    a, d, r = int(a*SR), int(d*SR), int(r*SR); e = np.ones(n) * s
    a = min(a, n); e[:a] = np.linspace(0, 1, a, endpoint=False)
    dd = min(d, max(0, n-a)); e[a:a+dd] = np.linspace(1, s, dd, endpoint=False)
    return np.concatenate([e, s * np.exp(-np.arange(r) / (r/5 + 1))])
def saw(f, n, ph=0.0): t = (np.arange(n) * f / SR + ph) % 1.0; return 2*t - 1
def place(buf, x, start):
    i = int(round(start*SR));
    if i < 0: x = x[-i:]; i = 0
    e = min(len(buf), i+len(x)); buf[i:e] += x[:e-i]

class Kit:
    def __init__(s, seed): s.rng = np.random.default_rng(seed)
    def kick(s, v):
        n = int(0.35*SR); t = np.arange(n)/SR; ph = np.cumsum(45 + 110*np.exp(-t*30))/SR
        return np.sin(2*np.pi*ph) * np.exp(-t*9) * v
    def snare(s, v):
        n = int(0.25*SR); t = np.arange(n)/SR; nz = onepole(s.rng.standard_normal(n), 6000)
        return (0.6*np.sin(2*np.pi*190*t)*np.exp(-t*25) + 0.9*nz*np.exp(-t*16)) * v
    def hat(s, v):
        n = int(0.07*SR); t = np.arange(n)/SR; nz = s.rng.standard_normal(n); nz = nz - onepole(nz, 7000)
        return nz * np.exp(-t*60) * v * 0.5
    def crash(s, v):
        n = int(2.0*SR); t = np.arange(n)/SR; nz = s.rng.standard_normal(n); nz = nz - onepole(nz, 4000)
        return nz * np.exp(-t*2.0) * v * 0.45

def bass_note(p, dur, kind):
    n = int(dur*SR); f = hz(p)
    x = saw(f, n) if kind == 0 else np.sin(2*np.pi*f*np.arange(n)/SR) + 0.4*np.sin(4*np.pi*f*np.arange(n)/SR)
    x = onepole(onepole(x, 600), 800) if kind == 0 else x
    return x * adsr(n, 0.005, 0.15, 0.75, 0.03)[:n]
def lead_note(p, dur):
    n = int(dur*SR); t = np.arange(n)/SR; ph = np.cumsum(hz(p)*(1 + 0.004*np.sin(2*np.pi*5.5*t)))/SR
    x = (2*(ph % 1) - 1 + 2*((ph*1.006) % 1) - 1) * 0.5; return onepole(x, 3500) * adsr(n, 0.01, 0.1, 0.8, 0.05)[:n]
def pad_chord(ps, dur):
    n = int(dur*SR); x = np.zeros(n)
    for p in ps: x += (saw(hz(p), n) + saw(hz(p)*1.004, n, .3)) * .5
    return onepole(onepole(x, 1500), 2200) * adsr(n, 0.25, 0.4, 0.7, 0.3)[:n] / len(ps)
def synth_voice(p, dur, rng):
    """formant 'ah' with vibrato and breath: close enough to a voice for the separator"""
    n = int(dur*SR); t = np.arange(n)/SR; f0 = hz(p)*(1 + 0.012*np.sin(2*np.pi*5.2*t)*np.clip(t/0.25, 0, 1))
    ph = np.cumsum(f0)/SR; src = 2*(ph % 1) - 1 + 0.05*rng.standard_normal(n)
    out = np.zeros(n)
    for fc, bw, g in [(730, 90, 1.0), (1090, 110, 0.6), (2440, 160, 0.3), (3400, 250, 0.15)]:
        r = np.exp(-np.pi*bw/SR); th = 2*np.pi*fc/SR
        out += g * lfilter([1 - r], [1, -2*r*np.cos(th), r*r], src)
    return out * adsr(n, 0.04, 0.1, 0.85, 0.08)[:n]

SONGS = [
    # name, bpm, offset seconds before bar 1 (silence or a pickup), sections, key
    dict(name='dubstep140', bpm=140, offset=0.0, key=41,
         sections=[('Intro', 8), ('Build', 8), ('Drop', 16), ('Break', 8), ('Build', 8), ('Drop', 16), ('Outro', 8)]),
    dict(name='house128', bpm=128, offset=0.6, key=45,
         sections=[('Intro', 16), ('Break', 8), ('Build', 8), ('Drop', 16), ('Break', 16), ('Build', 8), ('Drop', 16), ('Outro', 8)]),
    dict(name='dnb174', bpm=174, offset=0.0, key=38,
         sections=[('Intro', 16), ('Build', 8), ('Drop', 32), ('Break', 16), ('Build', 8), ('Drop', 32), ('Outro', 16)]),
    dict(name='midtempo100', bpm=100, offset=1.2, key=43,
         sections=[('Intro', 8), ('Drop', 16), ('Break', 8), ('Build', 4), ('Drop', 16), ('Outro', 8)]),
]
PROG = [[0, 3, 7], [-4, 0, 3], [-2, 2, 5], [-5, -2, 2]]   # i - VI - VII - v (minor)

def render(cfg, out, vocal):
    bpm, beat = cfg['bpm'], 60.0 / cfg['bpm']; bar = 4 * beat; off = cfg['offset']; key = cfg['key']
    nb = sum(n for _, n in cfg['sections']); L = off + nb * bar + 3.0
    rng = np.random.default_rng(nb + bpm); kit = Kit(bpm)
    stems = {k: np.zeros(int(L*SR)) for k in ('bass', 'melody', 'chords', 'drums', 'vocals')}
    lanes = {k: [0]*nb for k in ('Drums', 'Kick', 'Snare', 'Hats', 'Cymbals', 'Bass', 'Lead', 'Chords', 'Vocals')}
    secs = []; b0 = 0; nDrop = 0; vpos = 0
    if off > 0.5:   # pickup: the vocal starts half a bar early in songs with a long lead-in
        pass
    for label, n in cfg['sections']:
        secs.append([label, b0, b0 + n])
        if label == 'Drop': nDrop += 1
        for k in range(n):
            b = b0 + k; t0 = off + b * bar; ch = PROG[b % 4]; root = key + ch[0]
            last = k == n - 1; half2 = k >= n // 2
            # ---- what plays where (the truth table)
            kick = label in ('Drop', 'Outro') or (label == 'Build') or (label == 'Intro' and cfg['name'] in ('house128',))
            snare = label == 'Drop' or label == 'Build' or (label == 'Outro' and k < n // 2)
            hats = label in ('Drop', 'Outro') or (label == 'Intro' and k >= n // 2) or (label == 'Build' and half2)
            bass = label == 'Drop' or (label == 'Outro' and k < n // 4)
            lead = label == 'Drop' and (nDrop == 1 and half2 or nDrop >= 2) or (label == 'Break' and half2 and n >= 16)
            chords = label in ('Intro', 'Break', 'Build') or (label == 'Drop' and k % 8 < 4) or (label == 'Outro' and half2)
            vocals = (label in ('Intro', 'Break') and k % 4 in (1, 2)) or (label == 'Drop' and nDrop >= 2 and k % 8 == 6) or (label == 'Build' and k < 2)
            if label == 'Break' and cfg['name'] == 'house128' and n == 8: chords = True; vocals = k % 2 == 1
            # ---- render
            if kick:
                steps = [0, 4, 8, 12] if bpm < 170 or label != 'Drop' else [0, 10]
                if label == 'Drop' and bpm == 140: steps = [0, 10]
                for st in steps: place(stems['drums'], kit.kick(0.9), t0 + st*beat/4)
            if snare:
                if label == 'Build':   # snare roll that doubles every quarter of the build
                    q = int(4 * k / max(1, n)); hits = [4, 4, 8, 16][min(q, 3)]
                    for h in range(hits): place(stems['drums'], kit.snare(0.4 + 0.5 * k / n), t0 + h * bar / hits)
                else:
                    for st in ([8] if bpm == 140 else [4, 12]): place(stems['drums'], kit.snare(0.8), t0 + st*beat/4)
            if hats:
                for st in range(0, 16, 2): place(stems['drums'], kit.hat(0.6 if st % 4 == 2 else 0.4), t0 + st*beat/4)
            cym = k == 0 and label in ('Drop', 'Outro')
            if cym: place(stems['drums'], kit.crash(0.8), t0)
            if bass:
                for o, d in ([(0, 1.5), (1.5, 0.5), (2, 1.0), (3, 0.75)] if bpm < 170 else [(0, 2.0), (2.5, 1.0)]):
                    place(stems['bass'], bass_note(root - 12, d * beat * 0.92, nDrop % 2) * 0.55, t0 + o * beat)
            if lead:
                mel = [12, 15, 14, 10, 12, 19, 17, 15]
                for i in range(8):
                    if (b * 3 + i) % 7 == 3: continue
                    place(stems['melody'], lead_note(root + mel[(i + b) % 8], beat * 0.45) * 0.28, t0 + i * beat / 2)
            if chords:
                place(stems['chords'], pad_chord([root + iv for iv in ch], bar * 0.97) * 0.5, t0)
            if vocals:
                if vocal is not None:
                    seg = vocal[vpos:vpos + int(bar * SR * 0.95)]; vpos = (vpos + len(seg)) % max(1, len(vocal) - int(bar * SR))
                    seg = seg * np.minimum(1, np.minimum(np.arange(len(seg)), np.arange(len(seg))[::-1]) / (0.01 * SR))
                    place(stems['vocals'], seg * 1.0, t0)
                else:
                    for i, (o, d, iv) in enumerate([(0, 1.0, 12), (1, 1.0, 15), (2, 1.5, 14)]):
                        place(stems['vocals'], synth_voice(root + iv, d * beat * 0.95, rng) * 0.6, t0 + o * beat)
            for name, on in (('Kick', kick), ('Snare', snare), ('Hats', hats), ('Cymbals', cym), ('Bass', bass), ('Lead', lead), ('Chords', chords), ('Vocals', vocals)):
                if on: lanes[name][b] = 1
            if kick or snare or hats or cym: lanes['Drums'][b] = 1
        b0 += n
    os.makedirs(os.path.join(out, 'stems'), exist_ok=True)
    pan = {'bass': 0.0, 'melody': 0.2, 'chords': -0.3, 'drums': 0.0, 'vocals': 0.0}
    mix = np.zeros((len(stems['bass']), 2))
    for k, x in stems.items(): mix += np.stack([x*(1-pan[k])**.5, x*(1+pan[k])**.5], 1)
    pk = np.abs(mix).max(); g = 0.9 / pk; mix *= g
    sf.write(os.path.join(out, 'mix.wav'), mix.astype(np.float32), SR)
    for k, x in stems.items(): sf.write(os.path.join(out, 'stems', k + '.wav'), (x * g).astype(np.float32), SR)
    truth = dict(name=cfg['name'], bpm=bpm, offset=off, bars=nb, sections=secs, lanes=lanes, seconds=L)
    json.dump(truth, open(os.path.join(out, 'truth.json'), 'w'))
    print(out, bpm, 'bpm', nb, 'bars', round(L, 1), 's')

if __name__ == '__main__':
    outdir = sys.argv[1]; vocal = None
    if '--vocal' in sys.argv:
        v, sr = sf.read(sys.argv[sys.argv.index('--vocal') + 1]); v = v.mean(1) if v.ndim > 1 else v
        if sr != SR: v = resample_poly(v, SR, sr)
        # keep only the voiced parts so every phrase really has a voice in it
        fr = int(0.05 * SR); e = np.array([np.sqrt(np.mean(v[i:i+fr]**2)) for i in range(0, len(v) - fr, fr)])
        keep = np.repeat(e > 0.05 * e.max(), fr); v = v[:len(keep)][keep]; vocal = v / (np.abs(v).max() + 1e-9)
    for i, cfg in enumerate(SONGS):
        render(cfg, os.path.join(outdir, cfg['name']), vocal if (vocal is not None and i % 2 == 0) else None)
