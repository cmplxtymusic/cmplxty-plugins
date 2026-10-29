#!/usr/bin/env python3
"""Render synthetic test songs with known MIDI for CMPLXTY Rewired accuracy tests.
make_songs.py OUTDIR [n_songs]
Each song: mix.wav (stereo 44.1k), stems/{bass,melody,chords,drums}.wav, truth.mid (tracks Bass Melody Chords Kick Snare Hats Cymbals).
Everything is synthesised here (numpy), so the ground truth is exact."""
import sys, os, numpy as np, soundfile as sf, pretty_midi as pm
SR = 44100
rng = np.random.default_rng(7)
def hz(p): return 440.0 * 2 ** ((p - 69) / 12)
def adsr(n, a, d, s, r, sr=SR):
    a, d, r = int(a*sr), int(d*sr), int(r*sr); e = np.ones(n) * s
    a = min(a, n); e[:a] = np.linspace(0, 1, a, endpoint=False)
    dd = min(d, max(0, n-a)); e[a:a+dd] = np.linspace(1, s, dd, endpoint=False)
    rel = s * np.exp(-np.arange(r) / (r/5 + 1)); return np.concatenate([e, rel])
def onepole(x, fc):
    a = np.exp(-2*np.pi*fc/SR); y = np.zeros_like(x); z = 0.0
    from scipy.signal import lfilter
    return lfilter([1-a], [1, -a], x)
def saw(f, n, ph=0.0):
    t = (np.arange(n) * f / SR + ph) % 1.0; return 2*t - 1
def square(f, n, pw=0.5):
    t = (np.arange(n) * f / SR) % 1.0; return np.where(t < pw, 1.0, -1.0)
def place(buf, x, start):
    i = int(start*SR); e = min(len(buf), i+len(x)); buf[i:e] += x[:e-i]

def bass_voice(kind, p, dur, vel):
    n = int(dur*SR); f = hz(p)
    if kind == 0: x = saw(f, n) ; x = onepole(onepole(x, 500), 700)
    elif kind == 1: x = square(f, n, 0.4); x = onepole(onepole(x, 400), 600)
    else: x = np.sin(2*np.pi*f*np.arange(n)/SR) + 0.3*np.sin(4*np.pi*f*np.arange(n)/SR)
    return x * adsr(n, 0.005, 0.15, 0.7, 0.04)[:n+int(0.04*SR)][:n] * vel
def lead_voice(kind, p, dur, vel):
    n = int(dur*SR); t = np.arange(n)/SR; f = hz(p) * (1 + 0.004*np.sin(2*np.pi*5.5*t)*np.clip(t/0.3, 0, 1))
    ph = np.cumsum(f)/SR
    if kind == 0: x = 2*(ph % 1) - 1 + 2*((ph*1.006) % 1) - 1; x = onepole(x, 3500)*0.5
    elif kind == 1: x = np.where((ph % 1) < 0.5, 1.0, -1.0); x = onepole(x, 2500)
    else:  # breathy "vocal-ish" lead: sine stack with formant-like weights
        x = sum(w*np.sin(2*np.pi*k*ph) for k, w in [(1,1),(2,.5),(3,.35),(4,.2),(5,.12),(6,.08)])
    env = adsr(n, 0.01, 0.1, 0.8, 0.06); x = np.concatenate([x, x[-1]*0 + np.zeros(len(env)-n)]) if len(env) > n else x
    return x * env[:len(x)] * vel
def chord_voice(kind, p, dur, vel):
    n = int(dur*SR); f = hz(p); t = np.arange(n)/SR
    if kind == 0: x = (saw(f, n) + saw(f*1.004, n, .3)) * .5; x = onepole(onepole(x, 1800), 2500); env = adsr(n, 0.04, 0.3, 0.6, 0.15)
    elif kind == 1: x = np.sin(2*np.pi*f*t + 1.5*np.exp(-t*4)*np.sin(2*np.pi*f*t)); env = adsr(n, 0.004, 0.6, 0.35, 0.1)   # EP
    else: x = square(f, n, 0.3); x = onepole(x, 1500); env = adsr(n, 0.01, 0.2, 0.5, 0.12)
    x = np.concatenate([x, np.zeros(max(0, len(env)-n))]); return x * env * vel
def kick(vel):
    n = int(0.35*SR); t = np.arange(n)/SR; f = 45 + 110*np.exp(-t*30); ph = np.cumsum(f)/SR
    return np.sin(2*np.pi*ph) * np.exp(-t*9) * vel
def snare(vel):
    n = int(0.25*SR); t = np.arange(n)/SR
    nz = onepole(rng.standard_normal(n), 6000) - onepole(rng.standard_normal(n), 400)*0
    return (0.6*np.sin(2*np.pi*190*t)*np.exp(-t*25) + 0.9*nz*np.exp(-t*16)) * vel
def hat(vel, open_=False):
    n = int((0.35 if open_ else 0.07)*SR); t = np.arange(n)/SR; nz = rng.standard_normal(n)
    nz = nz - onepole(nz, 7000); return nz * np.exp(-t*(9 if open_ else 60)) * vel * 0.5
def cymbal(vel):
    n = int(2.0*SR); t = np.arange(n)/SR; nz = rng.standard_normal(n); nz = nz - onepole(nz, 4000)
    return nz * np.exp(-t*2.0) * vel * 0.45

SCALES = {'minor': [0,2,3,5,7,8,10], 'major': [0,2,4,5,7,9,11]}
PROGS = {'minor': [[0,2,4],[5,0,2],[3,5,0],[4,6,1]], 'major': [[0,2,4],[4,6,1],[5,0,2],[3,5,0]]}
def song(idx, out):
    bpm = [96, 120, 128, 140, 110, 85][idx % 6]; key = [57, 60, 55, 62, 53, 64][idx % 6]; mode = ['minor','major'][idx % 2]
    kinds = (idx % 3, (idx+1) % 3, (idx+2) % 3); bars = 12; beat = 60.0/bpm; L = bars*4*beat + 2.5
    sc = SCALES[mode]
    def deg(d, octv): q, r = divmod(d, 7); return key + 12*(octv+q) + sc[r]
    stems = {k: np.zeros(int(L*SR)+SR) for k in ('bass','melody','chords','drums')}
    mid = pm.PrettyMIDI(initial_tempo=bpm); trk = {}
    for name, prog, drum in [('Bass',33,False),('Melody',81,False),('Chords',89,False),('Kick',0,True),('Snare',0,True),('Hats',0,True),('Cymbals',0,True)]:
        trk[name] = pm.Instrument(program=prog, is_drum=drum, name=name); mid.instruments.append(trk[name])
    def note(tr, p, s, d, v): trk[tr].notes.append(pm.Note(velocity=int(v*127), pitch=int(p), start=s, end=s+d))
    for b in range(bars):
        ch = PROGS[mode][b % 4]; t0 = b*4*beat
        # chords: whole-bar or two half-bar hits
        hits = [(0, 4)] if idx % 2 == 0 else [(0, 2), (2, 2)]
        for (o, dl) in hits:
            for d in ch:
                p = deg(d, -1) if deg(d, -1) >= key - 2 else deg(d, 0)
                v = 0.55
                place(stems['chords'], chord_voice(kinds[2], p, dl*beat*0.95, v)*0.22, t0+o*beat); note('Chords', p, t0+o*beat, dl*beat*0.95, v)
        # bass: root pattern in 8ths/quarters
        root = deg(ch[0], -2)
        pat = [(0,1),(1.5,.5),(2,1),(3,.5),(3.5,.5)] if idx % 2 else [(0,.5),(.5,.5),(1,.5),(2,1),(3,.5),(3.5,.5)]
        for (o, dl) in pat:
            p = root + (12 if (o == 3.5 and idx % 3 == 0) else 0) + (7 if o == 3 else 0)
            v = 0.8; place(stems['bass'], bass_voice(kinds[0], p, dl*beat*0.9, v)*0.5, t0+o*beat); note('Bass', p, t0+o*beat, dl*beat*0.9, v)
        # melody: random walk on the scale above the chords, rests sometimes
        if b >= 2:
            o = 0.0; d = 7 + ch[0]
            while o < 4:
                dl = rng.choice([0.5, 0.5, 1.0, 1.5]) if o + 0.5 < 4 else 0.5
                dl = min(dl, 4 - o)
                if rng.random() > 0.15:
                    d = int(np.clip(d + rng.choice([-2,-1,1,2,0,3]), 7, 15)); p = deg(d, 0)
                    v = 0.7; place(stems['melody'], lead_voice(kinds[1], p, dl*beat*0.85, v)*0.3, t0+o*beat); note('Melody', p, t0+o*beat, dl*beat*0.85, v)
                o += dl
        # drums
        for st in range(16):
            tt = t0 + st*beat/4
            if st in ((0, 8) if idx % 2 else (0, 6, 8, 11)) or (bpm >= 120 and st in (0, 4, 8, 12)):
                place(stems['drums'], kick(0.9), tt); note('Kick', 36, tt, 0.1, 0.9)
            if st in (4, 12): place(stems['drums'], snare(0.8), tt); note('Snare', 38, tt, 0.1, 0.8)
            if st % 2 == 0 and not (st == 0 and b % 4 == 0):
                v = 0.6 if st % 4 == 0 else 0.45; place(stems['drums'], hat(v), tt); note('Hats', 42, tt, 0.05, v)
        if b % 4 == 0: place(stems['drums'], cymbal(0.8), t0); note('Cymbals', 49, t0, 0.5, 0.8)
    os.makedirs(os.path.join(out, 'stems'), exist_ok=True)
    pan = {'bass': 0.0, 'melody': 0.15, 'chords': -0.3, 'drums': 0.0}
    mix = np.zeros((len(stems['bass']), 2))
    for k, x in stems.items():
        st = np.stack([x*(1-pan[k])**.5*1.0, x*(1+pan[k])**.5*1.0], 1) / 1.0
        sf.write(os.path.join(out, 'stems', k + '.wav'), st.astype(np.float32), SR); mix += st
    pk = np.abs(mix).max(); mix *= 0.9/pk
    sf.write(os.path.join(out, 'mix.wav'), mix.astype(np.float32), SR)
    for k in stems: x, _ = sf.read(os.path.join(out, 'stems', k + '.wav')); sf.write(os.path.join(out, 'stems', k + '.wav'), (x*0.9/pk).astype(np.float32), SR)
    mid.write(os.path.join(out, 'truth.mid')); print(out, bpm, key, mode, kinds, round(L,1), 's')
n = int(sys.argv[2]) if len(sys.argv) > 2 else 4
for i in range(n): song(i, os.path.join(sys.argv[1], f'song{i+1}'))
