import numpy as np, json, sys
from sklearn.linear_model import LogisticRegression
sys.argv=['x']; from train_head import feats, G
import soundfile as sf
d=np.load('train.npz'); X,Y=d['X'],d['Y']
ALLOW={'bass':[9,10,7],'lead':list(range(9)),'comp':list(range(9))}
meta=json.load(open('solo.json')); T=[(feats(sf.read(m['file'],dtype='float32')[0]),G.index(m['group']),m) for m in meta]
for C in [0.003,0.01,0.03,0.1]:
    clf=LogisticRegression(C=C,max_iter=3000,class_weight='balanced').fit(X,Y)
    ok=0
    for f,g,m in T:
        p=clf.predict_proba(f).mean(0); al=ALLOW[m['kind']]; ok+= al[int(np.argmax(p[al]))]==g
    print(C, 'train acc %.3f'%clf.score(X,Y), 'heldout clips %d/%d'%(ok,len(T)),flush=True)
    np.savez(f'head_{C}.npz',W=clf.coef_,b=clf.intercept_)
