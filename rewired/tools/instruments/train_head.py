#!/usr/bin/env python3
"""Trains Rewired's instrument head: softmax(W . log(yamnet_scores + 1e-4) + b) over 11 groups.
Train audio: FluidR3_GM (MIT) + TimGM6mb (GPL-2 sample set, used only to render training clips; nothing from it ships).
Held-out test: GeneralUser GS clips from make_inst.py (a soundfont the head never sees)."""
import numpy as np, subprocess, soundfile as sf, json, sys, math
from scipy.signal import resample_poly
import onnxruntime as ort
sys.path.insert(0,'/workspace/rw-inst'); 
G=['Horns','Winds','Strings','Piano / Keys','Organ','Guitar','Mallets / Bells','Synth','Vocals','Bass Guitar','Upright Bass']
P={0:[56,57,58,59,60,61,64,65,66,67],1:[68,69,70,71,72,73,74,75,77,79],2:[40,41,42,43,44,45,48,49,110],3:[0,1,2,3,4,5,6,7],
   4:[16,17,18,19,20],5:[24,25,26,27,28,29,30,31,105],6:[8,9,10,11,12,13,14],7:[62,63,80,81,82,83,84,87,88,89,90,91,94,95],8:[52,53,54],
   9:[33,34,35,36,37],10:[32]}
SYNBASS=[38,39]
R='/workspace/tsf/render'; SR=44100
rng=np.random.default_rng(5); SCALE=[0,2,3,5,7,8,10]
def phrase(kind,dur,lo):
    b=60/rng.uniform(80,140); out=[]; t=0.2; root=rng.integers(0,12)
    while t<dur-0.3:
        if kind=='comp':
            L=float(rng.choice([1,2,4]))*b; base=lo+root+SCALE[rng.integers(0,7)]
            for iv in (0,3,7) if rng.random()<.6 else (0,4,7,11): out.append((t,min(L*.95,dur-t),int(base+iv),int(rng.integers(60,110))))
        else:
            L=float(rng.choice([.25,.5,.5,1,1.5,2]))*b; out.append((t,min(L*rng.uniform(.5,.98),dur-t),int(lo+root+SCALE[rng.integers(0,7)]+12*int(rng.integers(0,2))),int(rng.integers(70,120))))
        t+=L
    return out
def render(sf2,prog,notes,secs):
    with open('/tmp/tn.txt','w') as f:
        for s,d,p,v in notes: f.write(f"{s} {d} {p} {v}\n")
    subprocess.run([R,sf2,'0',str(prog),'/tmp/tn.txt','/tmp/to.raw',str(secs)],check=True,capture_output=True)
    x=np.fromfile('/tmp/to.raw',dtype=np.float32); return x/max(1e-6,np.abs(x).max())
sess=ort.InferenceSession('/workspace/yamnet/instruments.onnx')
def feats(x44):
    w=resample_poly(x44,160,441).astype(np.float32)
    need=int(16000*0.975); n=max(len(w),need); n=need+int(math.ceil((n-need)/7680))*7680; w=np.pad(w,(0,n-len(w)))
    nf=1+(len(w)-400)//160; h=0.5-0.5*np.cos(2*np.pi*np.arange(400)/400)
    fr=np.lib.stride_tricks.sliding_window_view(w,400)[::160][:nf]*h
    m=np.abs(np.fft.rfft(fr,512)).astype(np.float32); X=np.stack([m[i:i+96] for i in range(0,nf-96+1,48)])
    s=sess.run(['scores'],{'mag':X})[0]; return np.log(s+1e-4)
drums=[sf.read(f'/workspace/rw-songs/song{i}/stems/drums.wav',dtype='float32')[0] for i in range(1,7)]
drums=[d.mean(1) if d.ndim>1 else d for d in drums]
def sample(sf2,g,prog):
    lo={9:28,10:28}.get(g,36 if g==7 and prog in SYNBASS else (48 if rng.random()<.5 else 60))
    kind='comp' if (g in (2,3,4,5,7,8) and rng.random()<.5) else 'lead'
    D=10.0; x=render(sf2,prog,phrase(kind,D,lo),D)
    r=rng.random()
    if r<0.35:  # bleed from another instrument (separated stems are never clean)
        og=int(rng.integers(0,9)); y=render(sf2,int(rng.choice(P[og])),phrase('comp',D,48),D); x=x+10**(-rng.uniform(12,24)/20)*y[:len(x)]
    if rng.random()<0.3:
        d=drums[rng.integers(0,6)]; o=rng.integers(0,len(d)-len(x)); x=x+10**(-rng.uniform(14,26)/20)*d[o:o+len(x)]/max(1e-6,np.abs(d).max())
    x=x+rng.normal(0,10**(-rng.uniform(50,70)/20),len(x))
    return feats((x*10**(-rng.uniform(0,18)/20)).astype(np.float32))
if __name__=='__main__':
    Xs=[];Ys=[]
    for sf2 in ['/workspace/sf2/FluidR3_GM.sf2','/home/box/.local/lib/python3.13/site-packages/pretty_midi/TimGM6mb.sf2']:
        for g,progs in P.items():
            for prog in progs+(SYNBASS if g==7 else []):
                for k in range(3 if g not in (9,10) else 6):
                    f=sample(sf2,g,prog if not (g==7 and k==0 and prog in SYNBASS) else prog); Xs.append(f); Ys.append(np.full(len(f),g))
        print('rendered',sf2,len(Xs),flush=True)
    X=np.concatenate(Xs); Y=np.concatenate(Ys); np.savez('train.npz',X=X,Y=Y); print(X.shape)
