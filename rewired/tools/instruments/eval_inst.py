#!/usr/bin/env python3
"""Rewired 1.0.1 instrument-label accuracy. Solo clips: clip-level top-1 among the groups allowed for the part.
Songs: share of transcribed melody/chords/bass notes whose sub label matches the instrument playing at that time."""
import json, subprocess, sys, os, numpy as np
CLI=sys.argv[1]; M='/workspace/rw-models'
G=['Horns','Winds','Strings','Piano / Keys','Organ','Guitar','Mallets / Bells','Synth','Vocals','Bass Guitar','Upright Bass']
ALLOW={'bass':[9,10,7],'lead':list(range(9)),'comp':list(range(9))}
meta=json.load(open('solo.json')); ok=0; conf={}
for m in meta:
    subprocess.run([CLI,'--models',M,'--tag',m['file'],'--out','/tmp/t.json'],check=True,capture_output=True)
    s=np.array(json.load(open('/tmp/t.json'))); al=ALLOW[m['kind']]; mean=s.mean(0)
    pred=G[al[int(np.argmax(mean[al]))]]; ok+=pred==m['group']; conf.setdefault(m['name'],[]).append(pred)
print(f"solo clips: {ok}/{len(meta)} = {ok/len(meta):.1%}")
for k,v in conf.items(): print(f"  {k:11s} -> {', '.join(v)}")
PN={'Bass':'Bass','Melody':'Melody','Chords':'Chords'}
def song_eval(mode):
    tot={}; 
    for d in sorted(os.listdir('songs')):
        p=f'songs/{d}'; tr=json.load(open(p+'/truth.json'))
        a=['--song',p+'/mix.wav'] if mode=='song' else sum([[f'--{k}',f'{p}/stems/{k}.wav'] for k in ('bass','melody','chords','drums')],[])
        subprocess.run([CLI,'--models',M,*a,'--out','/tmp/o.mid','--json','/tmp/o.json'],check=True,capture_output=True)
        subs=json.load(open('/tmp/o.json'))['subs']; line=[]
        for part,segs in tr.items():
            n=c=0
            for sp in subs.get(part,[]):
                for s,e,pi in sp['notes']:
                    t=[g for a0,b0,g in segs if a0<=s<b0]
                    if not t: continue
                    want=t[0]; got=G[sp['inst']]; n+=1; c+= got==want
            tot.setdefault(part,[0,0]); tot[part][0]+=c; tot[part][1]+=n; line.append(f"{part} {c}/{n} [{'; '.join(x['name'] for x in subs.get(part,[]))}]")
        print(f"  {mode} {d}: "+' | '.join(line))
    allc=sum(v[0] for v in tot.values()); alln=sum(v[1] for v in tot.values())
    print(f"{mode} mode note-label accuracy: "+', '.join(f"{k} {v[0]/max(1,v[1]):.1%}" for k,v in tot.items())+f"; overall {allc/max(1,alln):.1%}")
song_eval('stems'); song_eval('song')
