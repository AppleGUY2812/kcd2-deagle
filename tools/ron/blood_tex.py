# blood_tex.py: export every texture behind the blood effects and decals listed in WORK/blood_collect.json
# (made by blood_collect.py) to WORK/fxout/blood/<name>.png, index in WORK/fxout/_data/blood_textures.json.
# Input of mk_blood_tif.py. Read-only on the paks.
import sys,os,json
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from usmap import U; from uasset import Pkg; from dump import WORK,USMAP,gamepath
from tex import extract_many,export
u=U(USMAP)
def chain(gp,seen=None):   # textures of a material and all its parents (same as blood_collect.chain_textures)
    seen=seen if seen is not None else set()
    if gp in seen or not gp or not gp.startswith('/Game'): return set()
    seen.add(gp); base=gamepath(gp)
    if not os.path.exists(base+'.uasset'): extract_many([gp])
    if not os.path.exists(base+'.uasset'): return set()
    p=Pkg(base,u); t=set(); parents=[]
    for c,n,o in p.imports:
        if o<0 and c=='Texture2D': t.add(p.imports[-o-1][1])
        if o<0 and c in('Material','MaterialInstanceConstant'): parents.append(p.imports[-o-1][1])
    for q in parents: t|=chain(q,seen)
    return t
D=json.load(open(os.path.join(WORK,'blood_collect.json')))
tex=set()
for f,v in D['fx'].items():
    if isinstance(v,dict):
        for m in v['materials']: tex|=chain(m)
for d,v in D['decals'].items():
    if isinstance(v,dict): tex|=set(v['textures'])
tex=sorted(t for t in tex if t.startswith('/Game'))
extract_many(tex); os.makedirs(WORK+'/fxout/blood',exist_ok=True); os.makedirs(WORK+'/fxout/_data',exist_ok=True); idx={}
for t in tex:
    try: idx[t]=export(t,WORK+'/fxout/blood/'+os.path.basename(t)+'.png')
    except Exception as x: idx[t]={'error':repr(x)}
json.dump(idx,open(WORK+'/fxout/_data/blood_textures.json','w'),indent=1,default=str)
print(len(tex),'textures, ok',sum('png' in v for v in idx.values())); [print(k,v['error']) for k,v in idx.items() if 'error' in v]
