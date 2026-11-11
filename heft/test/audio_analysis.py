#!/usr/bin/env python3
"""Generate repeatable audio, render the actual JUCE processor, analyze without loudness matching."""
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
sr=48000;duration=8;n=sr*duration;t=np.arange(n)/sr;rng=np.random.default_rng(37191)
def save(name,x):
 x=np.asarray(x,dtype=np.float32)
 if x.ndim==1:x=np.column_stack([x,x])
 wavfile.write(audio/(name+'.wav'),sr,x)
 return audio/(name+'.wav')
def render(src,dst,preset=0,param=None):
 cmd=[str(Path(a.tool).resolve()),'render',str(src),str(dst),str(preset)]
 if param:cmd+=list(map(str,param))
 subprocess.run(cmd,check=True)
def read(f):
 rate,x=wavfile.read(f)
 if np.issubdtype(x.dtype,np.integer):x=x/float(2**(np.iinfo(x.dtype).bits-1))
 return x.astype(float)
def norm(x,peak=.72):return x/max(1.e-9,np.max(np.abs(x)))*peak
# Drums: kick pitch drops, snare noise bodies and alternating closed/open hats.
dr=np.zeros((n,2))
for beat in np.arange(.1,duration,.5):
 k=np.arange(int(.4*sr))/sr;y=np.sin(2*np.pi*(48*k+48*.022*(1-np.exp(-k/.022))))*np.exp(-k/.09)
 at=int(beat*sr);length=min(len(y),n-at);dr[at:at+length]+=y[:length,None]
for beat in np.arange(.6,duration,1.):
 k=np.arange(int(.28*sr))/sr;noise=rng.normal(size=len(k));noise=signal.sosfilt(signal.butter(2,[400,7000],btype='bandpass',fs=sr,output='sos'),noise);y=(noise*.5+.18*np.sin(2*np.pi*180*k))*np.exp(-k/.055);at=int(beat*sr);length=min(len(y),n-at);dr[at:at+length]+=y[:length,None]
for i,beat in enumerate(np.arange(.1,duration,.25)):
 k=np.arange(int(.2*sr))/sr;noise=signal.sosfilt(signal.butter(2,6500,btype='highpass',fs=sr,output='sos'),rng.normal(size=len(k)));y=.15*noise*np.exp(-k/(.07 if i%4==3 else .018));at=int(beat*sr);length=min(len(y),n-at);dr[at:at+length,0]+=y[:length];dr[at:at+length,1]+=y[:length]*(.65 if i%2 else 1.1)
bass=np.zeros(n)
for i,at in enumerate(np.arange(.1,duration,.5)):
 k=np.arange(int(.46*sr))/sr;f=[55,55,65.406,49][i%4];env=(1-np.exp(-k/.003))*np.exp(-k/.25);y=(.65*np.sin(2*np.pi*f*k)+.25*signal.sawtooth(2*np.pi*f*k))*env;idx=int(at*sr);length=min(len(k),n-idx);bass[idx:idx+length]+=y[:length]
synth=np.zeros((n,2))
for at in np.arange(.1,duration,2):
 k=np.arange(int(1.9*sr))/sr;env=(1-np.exp(-k/.03))*np.exp(-k/1.3)*np.minimum(1,(1.9-k)/.1)
 for c in range(2):
  y=sum(signal.sawtooth(2*np.pi*f*(1+(.0015 if c else -.0015))*k) for f in [130.813,155.563,195.998]);y=signal.sosfilt(signal.butter(2,2800,fs=sr,output='sos'),y)*env;idx=int(at*sr);length=min(len(k),n-idx);synth[idx:idx+length,c]+=y[:length]
for name,x,preset in [('drums',dr,1),('bass',bass,2),('synth',synth,4)]:
 src=save(name+'-dry',norm(x));render(src,audio/(name+'-heft.wav'),preset)
# Isolated sections and sideband rejection, no drive/no output attenuation.
tone=.2*np.sin(2*np.pi*1000*t);src=save('tone-dry',tone)
for name,param in [('shift-up',('shift',100)),('shift-down',('shift',-100)),('wide',('wide',1)),('disperser',('smear',1)),('fuzz',('fuzz',1)),('transient',('transient',1)),('tight',('tight',1))]:render(src,audio/(name+'.wav'),0,param)
render(audio/'synth-dry.wav',audio/'motion.wav',7)
render(audio/'synth-dry.wav',audio/'motion-off.wav',7,('motion',0))
for name,param in [('transient-drums',('transient',1)),('tight-drums',('tight',1))]:render(audio/'drums-dry.wav',audio/(name+'.wav'),0,param)
def metrics(f):
 x=read(f);rms=np.sqrt(np.mean(x*x));peak=np.max(np.abs(x));mono=x.mean(axis=1);freq,P=signal.welch(mono,sr,nperseg=8192);sel=(freq>=20)&(freq<=20000);energy=P[sel].sum()
 corr=np.corrcoef(x.T)[0,1]
 cmd=['ffmpeg','-hide_banner','-i',str(f),'-af','ebur128=peak=true','-f','null','-']
 text=subprocess.run(cmd,capture_output=True,text=True,check=True).stderr
 loud=re.findall(r'I:\s*(-?[\d.]+) LUFS',text)
 tp=20*np.log10(max(1.e-15,np.max(np.abs(signal.resample_poly(x,4,1,axis=0)))))
 return dict(rms_dbfs=20*np.log10(max(rms,1.e-15)),peak_dbfs=20*np.log10(max(peak,1.e-15)),crest_db=20*np.log10(max(peak,1.e-15)/max(rms,1.e-15)),lufs=float(loud[-1]),correlation=float(corr),centroid_hz=float((freq[sel]*P[sel]).sum()/energy),low_percent=float(P[(freq>=20)&(freq<200)].sum()/energy*100),mid_percent=float(P[(freq>=200)&(freq<2000)].sum()/energy*100),high_percent=float(P[(freq>=2000)&(freq<=20000)].sum()/energy*100),true_peak_4x_dbfs=float(tp),nonfinite=int((~np.isfinite(x)).sum()))
results={}
fig,axes=plt.subplots(3,2,figsize=(12,10))
for row,name in enumerate(['drums','bass','synth']):
 results[name]={kind:metrics(audio/(name+'-'+kind+'.wav')) for kind in ['dry','heft']}
 for kind in ['dry','heft']:
  x=read(audio/(name+'-'+kind+'.wav'));freq,P=signal.welch(x.mean(axis=1),sr,nperseg=8192);axes[row,0].semilogx(freq[1:],10*np.log10(P[1:]+1.e-18),label=kind)
  axes[row,1].plot(t[:sr],x[:sr,0],label=kind,alpha=.7)
 axes[row,0].set(xlim=(20,20000),ylim=(-110,-15),title=name+' spectrum',xlabel='Hz',ylabel='dB/Hz');axes[row,1].set(title=name+' first second',xlabel='seconds',ylabel='amplitude');axes[row,0].legend();axes[row,1].legend()
fig.tight_layout();fig.savefig(out/'audio-analysis.png',dpi=130);plt.close(fig)
results['module_checks']={}
reference=read(audio/'tone-dry.wav')[:-64]
for name in ['fuzz','wide','disperser']:
 x=read(audio/(name+'.wav'))[64:]
 delta=float(np.sqrt(np.mean((x-reference)**2)))
 results['module_checks'][name+'_difference_rms']=delta
 assert delta>1.e-4,(name,delta)
motion_delta=float(np.sqrt(np.mean((read(audio/'motion.wav')-read(audio/'motion-off.wav'))**2)))
results['module_checks']['lfo_noise_motion_difference_rms']=motion_delta
assert motion_delta>1.e-4,motion_delta
results['shift_checks']={}
for name,expected,image in [('shift-up',1100,900),('shift-down',900,1100)]:
 x=read(audio/(name+'.wav'))[sr:,0];freq=np.fft.rfftfreq(len(x),1/sr);power=np.abs(np.fft.rfft(x*np.hanning(len(x))));peak=freq[np.argmax(power)];wanted=power[np.argmin(np.abs(freq-expected))];unwanted=power[np.argmin(np.abs(freq-image))];rejection=20*np.log10(wanted/max(unwanted,1.e-15));results['shift_checks'][name]=dict(peak_hz=float(peak),expected_hz=expected,image_rejection_db=float(rejection));assert abs(peak-expected)<1 and rejection>35,(name,peak,rejection)
for name in ['transient-drums','tight-drums']:results[name]=metrics(audio/(name+'.wav'))
assert results['transient-drums']['rms_dbfs']>results['drums']['dry']['rms_dbfs']+.2,'Transient section must observably boost attacks'
assert results['tight-drums']['rms_dbfs']<results['drums']['dry']['rms_dbfs']-1,'Tight must observably attenuate tails'
(out/'audio-metrics.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
assert all(results[name][kind]['nonfinite']==0 for name in ['drums','bass','synth'] for kind in ['dry','heft'])
