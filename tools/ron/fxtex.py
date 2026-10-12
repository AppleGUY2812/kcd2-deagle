import sys,os,json
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import OUT,SCR,USMAP,gamepath; from uasset import Pkg
from tex import extract_many,export
u=U(USMAP)
D=json.load(open(SCR+'/fx_collect.json'))
def chain_textures(gp,seen=None):
    seen=seen or set()
    if gp in seen or not gp or not gp.startswith('/Game'): return set()
    seen.add(gp); base=gamepath(gp)
    if not os.path.exists(base+'.uasset'): extract_many([gp])
    if not os.path.exists(base+'.uasset'): return set()
    p=Pkg(base,u); t=set(); parents=[]
    for c,n,o in p.imports:
        if o<0 and c=='Texture2D': t.add(p.imports[-o-1][1])
        if o<0 and c in('Material','MaterialInstanceConstant'): parents.append(p.imports[-o-1][1])
    for q in parents: t|=chain_textures(q,seen)
    return t
mat_tex={}
for m in list(D['particle_materials'])+['/Game/ReadyOrNot/VFX/Materials/M_Ribbon']:
    mat_tex[m]=sorted(chain_textures(m))
alltex=sorted(set(x for v in mat_tex.values() for x in v)-set(json.load(open(SCR+'/fxout/decals/_index.json'))))
print('particle textures',len(alltex))
extract_many(alltex); idx={}
for gp in alltex:
    cat='tracer' if any(gp in mat_tex.get(m,[]) for m in ['/Game/ReadyOrNot/VFX/Materials/M_Ribbon']) else 'particles'
    try: idx[gp]=export(gp,f'{SCR}/fxout/{cat}/{os.path.basename(gp)}.png')
    except Exception as x: idx[gp]={'error':repr(x)}
json.dump({'material_textures':mat_tex,'exports':idx},open(SCR+'/fxout/particles_index.json','w'),indent=1,default=str)
print('ok',sum('png' in v for v in idx.values()),'errors',[ (k,v['error']) for k,v in idx.items() if 'error' in v][:10])
