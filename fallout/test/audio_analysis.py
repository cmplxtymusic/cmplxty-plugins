#!/usr/bin/env python3
"""Render factory patterns through the actual JUCE processor and measure them."""
import argparse, json, os, re, subprocess
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('tool');p.add_argument('output');a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);audio=out/'audio';audio.mkdir(exist_ok=True)
sr=48000
def save_from_render(name, preset, seconds=8):
    dst=audio/(name+'.wav')
    subprocess.run([str(Path(a.tool).resolve()),'renderpat',str(dst),str(preset),str(seconds)],check=True)
    return dst
def read(f):
    rate,x=wavfile.read(f)
    if np.issubdtype(x.dtype,np.integer):x=x/float(2**(np.iinfo(x.dtype).bits-1))
    return x.astype(float)
def metrics(f):
    x=read(f);rms=np.sqrt(np.mean(x*x));peak=np.max(np.abs(x));mono=x.mean(axis=1);freq,P=signal.welch(mono,sr,nperseg=8192);sel=(freq>=20)&(freq<=20000);energy=P[sel].sum()
    corr=np.corrcoef(x.T)[0,1] if x.ndim==2 and x.shape[1]>1 else 1.0
    cmd=['ffmpeg','-hide_banner','-i',str(f),'-af','ebur128=peak=true','-f','null','-']
    text=subprocess.run(cmd,capture_output=True,text=True,check=True).stderr
    loud=re.findall(r'I:\s*(-?[\d.]+) LUFS',text)
    tp=20*np.log10(max(1.e-15,np.max(np.abs(signal.resample_poly(x,4,1,axis=0)))))
    return dict(rms_dbfs=20*np.log10(max(rms,1.e-15)),peak_dbfs=20*np.log10(max(peak,1.e-15)),crest_db=20*np.log10(max(peak,1.e-15)/max(rms,1.e-15)),lufs=float(loud[-1]) if loud else None,correlation=float(corr),centroid_hz=float((freq[sel]*P[sel]).sum()/energy),low_percent=float(P[(freq>=20)&(freq<200)].sum()/energy*100),mid_percent=float(P[(freq>=200)&(freq<2000)].sum()/energy*100),high_percent=float(P[(freq>=2000)&(freq<=20000)].sum()/energy*100),true_peak_4x_dbfs=float(tp),nonfinite=int((~np.isfinite(x)).sum()),duration_s=len(mono)/sr)
results={}
presets=[('ash-init',0),('residue-line',1),('sump-crawler',2),('ember-horde',8),('folded-waste',9),('snarl-pit',10),('half-life',11),('fallout-shelter',25)]
fig,axes=plt.subplots(4,2,figsize=(12,14))
for i,(name,preset) in enumerate(presets):
    f=save_from_render(name,preset,8)
    results[name]=metrics(f)
    x=read(f);freq,P=signal.welch(x.mean(axis=1),sr,nperseg=8192)
    ax=axes[i//2,i%2]
    ax.semilogx(freq[1:],10*np.log10(P[1:]+1.e-18))
    ax.set(xlim=(20,20000),ylim=(-110,-15),title=f'{name} (preset {preset})',xlabel='Hz',ylabel='dB/Hz')
    assert results[name]['nonfinite']==0,name
    assert results[name]['peak_dbfs']<=-0.99, (name,results[name]['peak_dbfs'])
fig.tight_layout();fig.savefig(out/'audio-analysis.png',dpi=130);plt.close(fig)
(out/'audio-metrics.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
