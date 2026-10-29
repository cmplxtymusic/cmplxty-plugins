#!/usr/bin/env python3
# Generates Source/FactoryPresets.h (48 Quartz + 40 ALTER presets). Run: python3 tools/gen_presets.py
ENUM = {'in':'pIn','gate':'pGate','bits':'pBits','rate':'pRate','drive':'pDrive','grit':'pGrit','dtype':'pDType','tone':'pTone',
 'vocode':'pVocode','cshape':'pCShape','cnote':'pCNote','lowcut':'pLowCut','highcut':'pHighCut','double':'pDouble','widen':'pWiden',
 'delay':'pDelay','fb':'pFb','reverb':'pReverb','decay':'pDecay','mix':'pMix','out':'pOut','intensity':'pIntensity',
 'mode':'pMode','pitch':'pPitch','link':'pLink','formant':'pFormant','gender':'pGender','adrive':'pADrive','speed':'pSpeed',
 'key':'pKey','scale':'pScale','amix':'pAMix','aout':'pAOut'}
CLIP,FOLD,FUZZ=0,1,2; SAW,SQ,NOISE=0,1,2
def Q(name, **k): return (name,k)
QP = [
Q("Init Clean Quartz", intensity=100),
Q("Glass Scream", gate=-42,bits=8,rate=11000,drive=18,grit=62,dtype=FOLD,tone=2.4,vocode=45,cshape=SAW,cnote=48,lowcut=180,highcut=9200,double=28,widen=66,delay=92,fb=35,reverb=48,decay=2.8,mix=85,**{'in':1.5},intensity=70),
Q("Cracked Quartz", gate=-48,bits=7,rate=14000,drive=14,grit=70,dtype=CLIP,tone=2,reverb=20,decay=1.2,mix=95,intensity=85),
Q("Shard Choir", double=38,widen=85,vocode=40,cshape=SAW,cnote=48,reverb=55,decay=4,drive=6,grit=20,highcut=12000,mix=80,intensity=75),
Q("Obsidian Veil", lowcut=160,highcut=6500,reverb=70,decay=6,delay=380,fb=40,drive=8,tone=-3,widen=60,mix=70,intensity=70),
Q("Static Halo", bits=10,rate=9000,grit=40,drive=6,reverb=50,decay=3.5,widen=70,vocode=25,cshape=NOISE,tone=3,mix=85,intensity=75),
Q("Frozen Lung", reverb=90,decay=9,lowcut=200,highcut=5200,drive=3,delay=600,fb=55,mix=65,widen=80,gate=-55,intensity=80),
Q("Broken Prism", dtype=FOLD,drive=20,grit=80,bits=9,rate=7000,double=25,widen=50,delay=140,fb=45,reverb=30,mix=90,intensity=85),
Q("Black Ice Lead", gate=-45,drive=18,dtype=CLIP,grit=30,tone=4,lowcut=250,highcut=11000,double=20,delay=220,fb=30,reverb=25,decay=2,vocode=20,cshape=SAW,cnote=55,mix=90,intensity=75),
Q("Dead Pixel", gate=-40,bits=4,rate=5000,grit=60,drive=10,highcut=7000,reverb=15,mix=100,intensity=80),
Q("Tape Ghost", rate=22000,bits=14,tone=-4,highcut=7000,lowcut=120,delay=190,fb=50,reverb=35,decay=2.2,double=12,widen=30,drive=5,dtype=FUZZ,mix=80,intensity=70),
Q("Arcade Ash", bits=5,rate=16000,vocode=60,cshape=SQ,cnote=60,drive=8,tone=3,highcut=12000,delay=120,fb=25,reverb=15,mix=95,intensity=85),
Q("Cathedral Rot", reverb=85,decay=8,drive=14,dtype=FUZZ,grit=45,lowcut=140,highcut=7500,double=22,widen=60,tone=-2,mix=85,intensity=80),
Q("Mirror Hex", double=45,widen=100,delay=90,fb=35,reverb=40,decay=3,vocode=35,cshape=SAW,cnote=53,drive=10,dtype=FOLD,grit=35,mix=90,intensity=75),
Q("Hollow Crown", lowcut=300,highcut=8000,reverb=60,decay=5,vocode=50,cshape=SAW,cnote=41,double=15,widen=55,drive=4,tone=-2,mix=80,intensity=75),
Q("Razor Mist", lowcut=400,drive=16,dtype=FOLD,grit=50,tone=6,reverb=45,decay=3.2,delay=70,fb=20,bits=11,mix=85,widen=70,intensity=80),
Q("Vault Whisper", gate=-50,lowcut=180,highcut=6000,reverb=55,decay=4.5,drive=2,widen=40,vocode=15,cshape=NOISE,mix=75,out=-2,intensity=65,**{'in':6}),
Q("Nine Bit Angel", bits=9,rate=20000,double=30,widen=80,reverb=65,decay=5,tone=4,highcut=16000,vocode=25,cshape=SAW,cnote=60,drive=3,mix=90,intensity=75),
Q("Chrome Teeth", gate=-42,drive=26,dtype=CLIP,grit=75,tone=7,lowcut=300,highcut=13000,bits=12,reverb=12,mix=100,out=-3,intensity=85),
Q("Void Doubler", double=55,widen=90,delay=38,fb=15,reverb=30,drive=6,lowcut=150,highcut=9000,mix=85,intensity=80),
Q("Splinter Pad", gate=-52,vocode=85,cshape=SAW,cnote=48,double=35,widen=85,reverb=60,decay=5,lowcut=180,highcut=9000,drive=6,mix=90,intensity=80),
Q("Basalt Bark", gate=-38,drive=28,dtype=FUZZ,grit=60,lowcut=100,highcut=5500,tone=-3,bits=10,reverb=10,mix=100,out=-4,intensity=85),
Q("Lunar Fuzz", drive=20,dtype=FUZZ,grit=40,tone=-1,highcut=8000,delay=330,fb=45,reverb=50,decay=4,double=18,widen=60,mix=85,intensity=80),
Q("Night Mine", lowcut=220,highcut=5000,drive=12,dtype=FOLD,grit=50,reverb=70,decay=7,delay=450,fb=50,bits=11,rate=15000,mix=70,intensity=80),
Q("Smoke Facets", reverb=45,decay=3.5,double=28,widen=65,drive=7,grit=25,tone=1,vocode=20,cshape=SAW,cnote=55,delay=150,fb=30,mix=85,intensity=75),
Q("Toxic Geode", drive=22,dtype=FOLD,grit=85,bits=8,rate=11000,tone=3,vocode=40,cshape=SQ,cnote=50,reverb=25,mix=90,intensity=85),
Q("Phantom Choke", gate=-35,bits=6,rate=8000,drive=15,dtype=CLIP,lowcut=400,highcut=4500,tone=2,reverb=20,decay=1,mix=100,out=-3,intensity=90),
Q("Corroded Siren", vocode=70,cshape=SAW,cnote=64,drive=12,dtype=FUZZ,grit=45,tone=5,highcut=12000,double=40,widen=70,reverb=40,decay=3,delay=100,fb=30,mix=90,out=-5,intensity=80),
Q("Opal Rot", bits=13,rate=18000,drive=8,grit=35,dtype=FOLD,double=22,widen=60,reverb=55,decay=4.5,tone=1,highcut=14000,mix=90,intensity=75),
Q("Slate Hymn", vocode=55,cshape=SAW,cnote=43,reverb=75,decay=7,double=20,widen=75,lowcut=200,highcut=7500,drive=3,mix=85,tone=-1,intensity=75),
Q("Monolith Moan", lowcut=60,highcut=4500,drive=18,dtype=FUZZ,grit=30,tone=-6,vocode=45,cshape=SAW,cnote=36,reverb=50,decay=5,bits=12,mix=90,out=-3,intensity=80),
Q("Cold Strobe", delay=125,fb=55,bits=10,rate=12000,drive=9,tone=4,highcut=15000,reverb=20,mix=85,gate=-34,intensity=80),
Q("Dust Prism", lowcut=250,highcut=16000,bits=12,rate=20000,reverb=40,decay=3,double=16,widen=55,tone=3,drive=4,vocode=15,cshape=NOISE,mix=90,intensity=70),
Q("Heavy Salt", gate=-44,drive=30,dtype=CLIP,grit=65,bits=9,tone=2,lowcut=200,highcut=9000,reverb=8,mix=100,out=-5,intensity=90),
Q("Silent Riot", gate=-36,drive=24,dtype=FOLD,grit=70,bits=6,rate=10000,vocode=30,cshape=SQ,cnote=53,double=30,widen=80,reverb=35,decay=2,mix=100,out=-4,intensity=90),
Q("Gutter Crystal", drive=14,dtype=FUZZ,grit=55,bits=8,rate=13000,lowcut=300,highcut=10000,reverb=30,delay=80,fb=25,tone=4,mix=95,intensity=80),
Q("Ember Static", vocode=50,cshape=NOISE,drive=16,dtype=FUZZ,grit=50,tone=-2,highcut=7000,reverb=35,decay=2.5,bits=9,rate=9000,mix=90,widen=45,intensity=80),
Q("Lo-Fi Reliquary", bits=8,rate=12000,tone=-5,highcut=5500,lowcut=200,reverb=60,decay=5,delay=240,fb=35,double=14,widen=50,drive=5,dtype=FUZZ,mix=90,intensity=75),
Q("Gated Ghost", gate=-40,reverb=70,decay=4,delay=280,fb=45,lowcut=180,highcut=8000,drive=6,widen=70,double=15,mix=85,intensity=75),
Q("Crush Cathedral", bits=5,rate=8000,reverb=90,decay=8.5,drive=10,dtype=CLIP,grit=40,lowcut=150,highcut=9000,tone=1,widen=80,mix=85,out=-3,intensity=85),
Q("Telephone Saint", lowcut=500,highcut=3400,drive=12,dtype=CLIP,grit=20,bits=10,rate=14000,reverb=40,decay=3,vocode=20,cshape=SAW,cnote=55,tone=3,mix=95,intensity=80,**{'in':3}),
Q("Granite Drift", lowcut=100,highcut=6000,drive=6,delay=520,fb=60,reverb=65,decay=6,double=20,widen=70,tone=-3,mix=75,intensity=70),
Q("Mono Nightmare", widen=0,double=40,drive=20,dtype=FUZZ,grit=60,bits=7,rate=9000,lowcut=220,highcut=6500,reverb=55,decay=4,vocode=35,cshape=NOISE,mix=95,gate=-42,intensity=85),
Q("Prism Panic", drive=22,dtype=FOLD,grit=90,bits=6,rate=7000,double=50,widen=100,delay=60,fb=55,reverb=45,decay=2.5,tone=5,mix=90,out=-4,intensity=100),
Q("Vapor Shard", rate=24000,bits=14,highcut=18000,lowcut=300,drive=5,dtype=FOLD,grit=30,double=24,widen=90,reverb=55,decay=4,delay=200,fb=35,tone=5,vocode=20,cshape=SAW,cnote=60,mix=90,intensity=75),
Q("Industrial Lullaby", vocode=65,cshape=SAW,cnote=45,drive=14,dtype=CLIP,grit=50,bits=10,rate=14000,lowcut=180,highcut=8500,reverb=50,decay=4.5,delay=330,fb=40,double=18,widen=60,mix=90,intensity=80),
Q("Crypt Chorus", double=48,widen=85,vocode=50,cshape=SAW,cnote=41,reverb=80,decay=7,lowcut=160,highcut=7000,drive=5,tone=-2,mix=85,intensity=75),
Q("Rust Halo", drive=18,dtype=FUZZ,grit=55,bits=9,rate=12000,tone=-1,highcut=9000,lowcut=200,reverb=50,decay=4,double=22,widen=65,delay=140,fb=30,mix=90,intensity=80),
]
T,QZ_,R=0,1,2
def A(name, **k):
    # Key/Scale default to "--" (no quantizing); presets that auto-tune must name a real key and scale.
    if k.get('mode', 0) in (1, 2): k.setdefault('key', 0); k.setdefault('scale', 0)
    return (name,k)
rob = lambda n,key,p,**k: A(n, mode=R,key=key,pitch=p,amix=100,**k)
AP = [
A("Init Transpose", mode=T),
A("Octave Down", pitch=-12),
A("Octave Up", pitch=12),
A("Fifth Up Double", pitch=7, amix=50),
A("Fourth Down Double", pitch=-5, amix=50),
A("Minor Third Harmony", pitch=3, amix=50),
A("Major Third Harmony", pitch=4, amix=50),
A("Soft Feminize", pitch=0, formant=3, gender=25, adrive=3.5, speed=12, amix=70),
A("Full Feminize", pitch=5, formant=4, gender=60),
A("Soft Masculinize", pitch=-2, formant=-2, gender=-25),
A("Deep Giant", pitch=-7, link=1, formant=-3, gender=-70, adrive=2, aout=-4),
A("Chipmunk", pitch=10, link=1, formant=2, gender=30),
A("Pitch Warp Down", pitch=-3.7, formant=2, adrive=1.5),
A("DJ Slowdown", pitch=-6, link=1, formant=-2, adrive=1),
A("DJ Speedup", pitch=6, link=1, formant=2),
A("Hard Tune", mode=QZ_, speed=1, scale=0),
A("Hard Tune Drive", mode=QZ_, speed=1, scale=0, adrive=4, aout=-4),
A("Pop Quantize", mode=QZ_, speed=22, scale=0),
A("Gentle Snap", mode=QZ_, speed=70, scale=1),
rob("Robot C",0,-12), rob("Robot D",2,-10), rob("Robot E",4,-8), rob("Robot F",5,-7), rob("Robot G",7,-5), rob("Robot A",9,-3), rob("Robot B",11,-1),
rob("Robot Drive",0,-12,adrive=5,aout=-5),
A("Vocoder Chord Hold", mode=R, key=9, scale=2, pitch=-3, amix=100, adrive=1),
A("Thick Background", pitch=0.2, formant=-1, amix=55),
A("Nasal Fix", formant=-2.5, gender=-10),
A("Breath Hollow", pitch=-1, formant=-5, amix=80),
A("Alien Choir", pitch=7, formant=-8, gender=50, amix=65),
A("Crushed Tuner", mode=QZ_, speed=1, key=0, scale=5, adrive=6, aout=-5),
A("Ghost Octave", pitch=12, link=1, amix=35),
A("Opposites", pitch=5, formant=-5),
A("Opposites Reverse", pitch=-5, formant=5),
A("Gritty Robot", mode=R, key=2, pitch=-10, adrive=7, speed=1, amix=100, aout=-5),
A("Gender Lift", gender=45),
A("Gender Drop", gender=-45),
A("Gender Blend", gender=25, amix=50),
]
assert len(QP)==48 and len(AP)==40, (len(QP),len(AP))
def emit(name, lst):
    o = f"inline const FactoryPreset {name}[{len(lst)}] = {{\n"
    for n,k in lst:
        kv = ", ".join("{%s, %sf}" % (ENUM[a], repr(float(b))) for a,b in k.items())
        o += f'    {{ "{n}", {len(k)}, {{ {kv} }} }},\n'
    return o + "};\n"
out = '''// GENERATED by tools/gen_presets.py - do not edit by hand.
#pragma once
#include "ParamTable.h"
namespace qz
{
struct PresetEntry { int index; float value; };
struct FactoryPreset { const char* name; int count; PresetEntry e[40]; };
'''
out += emit("kQuartzPresets", QP) + emit("kAlterPresets", AP) + "} // namespace qz\n"
open('../Source/FactoryPresets.h','w').write(out)
print("ok", len(QP), len(AP), max(len(k) for n,k in QP))
