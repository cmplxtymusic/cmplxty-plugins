#!/usr/bin/env python3
"""Instrument-label test set for Rewired 1.0.1, rendered with GeneralUser GS (sf2) through TinySoundFont (MIT).
Writes solo/<name>_<k>.wav (12 s phrases) and songs/songN/{mix.wav, stems/*.wav, truth.json}."""
import os, sys, json, subprocess, numpy as np, soundfile as sf
SF='/workspace/sf2/GeneralUser.sf2'; R='/workspace/tsf/render'; SR=44100
rng=np.random.default_rng(11)
# name, GM program, group, part ('lead','comp','bass')
INST=[('trumpet',56,'Horns','lead'),('trombone',57,'Horns','lead'),('frenchhorn',60,'Horns','comp'),('brass',61,'Horns','comp'),('altosax',65,'Horns','lead'),('tenorsax',66,'Horns','lead'),
 ('flute',73,'Winds','lead'),('clarinet',71,'Winds','lead'),('violin',40,'Strings','lead'),('strings',48,'Strings','comp'),('cello',42,'Strings','comp'),
 ('piano',0,'Piano / Keys','comp'),('epiano',4,'Piano / Keys','comp'),('organ',16,'Organ','comp'),('rockorgan',18,'Organ','comp'),
 ('nylongtr',24,'Guitar','comp'),('steelgtr',25,'Guitar','comp'),('cleangtr',27,'Guitar','lead'),('distgtr',30,'Guitar','lead'),
 ('vibes',11,'Mallets / Bells','lead'),('marimba',12,'Mallets / Bells','lead'),('sawlead',81,'Synth','lead'),('squarelead',80,'Synth','lead'),('warmpad',89,'Synth','comp'),('polysynth',90,'Synth','comp'),
 ('choir',52,'Vocals','comp'),('voiceoohs',53,'Vocals','lead'),
 ('fingerbass',33,'Bass Guitar','bass'),('pickbass',34,'Bass Guitar','bass'),('upright',32,'Upright Bass','bass'),('synthbass',38,'Synth','bass'),('synthbass2',39,'Synth','bass')]
SCALE=[0,2,3,5,7,8,10]
def phrase(kind, t0, dur, bpm=100):
    b=60/bpm; out=[]; t=t0; root=rng.integers(0,12)
    while t < t0+dur-0.3:
        if kind=='lead':
            L=float(rng.choice([0.5,0.5,1,1,1.5,2]))*b; p=60+root+SCALE[rng.integers(0,7)]+12*int(rng.integers(0,2)); out.append((t,min(L*0.95,t0+dur-t),int(p),int(rng.integers(80,115)))); t+=L
        elif kind=='comp':
            L=float(rng.choice([2,2,4]))*b; d=SCALE[rng.integers(0,7)]; base=48+root+d
            for iv in (0,3,7,12 if rng.random()<.5 else 10): out.append((t,min(L*0.95,t0+dur-t),int(base+iv),int(rng.integers(70,100))))
            t+=L
        else:
            L=float(rng.choice([0.5,1,1]))*b; p=33+root+SCALE[rng.integers(0,7)]; out.append((t,min(L*0.9,t0+dur-t),int(p),int(rng.integers(90,120)))); t+=L
    return out
def render(prog, notes, secs):
    with open('/tmp/n.txt','w') as f:
        for s,d,p,v in notes: f.write(f"{s} {d} {p} {v}\n")
    subprocess.run([R,SF,'0',str(prog),'/tmp/n.txt','/tmp/o.raw',str(secs)],check=True)
    x=np.fromfile('/tmp/o.raw',dtype=np.float32); return x
os.makedirs('solo',exist_ok=True); meta=[]
for name,prog,grp,kind in INST:
    for k in range(2):
        n=phrase(kind,0.2,11.5); x=render(prog,n,12.5); x/=max(1e-6,np.abs(x).max())/0.7
        fn=f'solo/{name}_{k}.wav'; sf.write(fn,x,SR); meta.append(dict(file=fn,name=name,group=grp,kind=kind))
json.dump(meta,open('solo.json','w'),indent=1)
# songs: bass + melody (2 instruments in halves) + chords (2 instruments in halves) + drums from rw-songs
SONGS=[(('fingerbass',33,'Bass Guitar'),[('trumpet',56,'Horns'),('flute',73,'Winds')],[('piano',0,'Piano / Keys'),('strings',48,'Strings')]),
       (('upright',32,'Upright Bass'),[('altosax',65,'Horns'),('vibes',11,'Mallets / Bells')],[('epiano',4,'Piano / Keys'),('brass',61,'Horns')]),
       (('synthbass',38,'Synth'),[('sawlead',81,'Synth'),('violin',40,'Strings')],[('warmpad',89,'Synth'),('organ',16,'Organ')]),
       (('pickbass',34,'Bass Guitar'),[('cleangtr',27,'Guitar'),('trombone',57,'Horns')],[('steelgtr',25,'Guitar'),('choir',52,'Vocals')]),
       (('fingerbass',33,'Bass Guitar'),[('clarinet',71,'Winds'),('distgtr',30,'Guitar')],[('piano',0,'Piano / Keys'),('polysynth',90,'Synth')]),
       (('upright',32,'Upright Bass'),[('tenorsax',66,'Horns'),('marimba',12,'Mallets / Bells')],[('nylongtr',24,'Guitar'),('cello',42,'Strings')])]
D=24.0
for i,(bass,mel,cho) in enumerate(SONGS):
    d=f'songs/song{i+1}'; os.makedirs(d+'/stems',exist_ok=True); truth={'Bass':[[0,D,bass[2]]],'Melody':[],'Chords':[]}
    bn=phrase('bass',0.2,D-0.4); b=render(bass[1],bn,D+1)
    m=np.zeros(int((D+1)*SR),np.float32); c=np.zeros_like(m)
    for h,(nm,pg,gr) in enumerate(mel):
        x=render(pg,phrase('lead',h*D/2+0.2,D/2-0.4),D+1); m+=x/ max(1e-6,np.abs(x).max()); truth['Melody'].append([h*D/2,(h+1)*D/2,gr])
    for h,(nm,pg,gr) in enumerate(cho):
        x=render(pg,phrase('comp',h*D/2+0.2,D/2-0.4),D+1); c+=x/max(1e-6,np.abs(x).max()); truth['Chords'].append([h*D/2,(h+1)*D/2,gr])
    dr,_=sf.read(f'/workspace/rw-songs/song{(i%6)+1}/stems/drums.wav',dtype='float32')
    if dr.ndim>1: dr=dr.mean(1)
    dr=np.resize(dr,len(m)) if len(dr)<len(m) else dr[:len(m)]
    b=b/max(1e-6,np.abs(b).max())
    st={'bass':0.8*b,'melody':0.6*m,'chords':0.5*c,'drums':0.6*dr/max(1e-6,np.abs(dr).max())}
    for k,v in st.items(): sf.write(f'{d}/stems/{k}.wav',v.astype(np.float32),SR)
    mix=sum(st.values()); mix=mix/np.abs(mix).max()*0.9; sf.write(f'{d}/mix.wav',np.stack([mix,mix],1).astype(np.float32),SR)
    json.dump(truth,open(d+'/truth.json','w'))
print('ok',len(meta))
