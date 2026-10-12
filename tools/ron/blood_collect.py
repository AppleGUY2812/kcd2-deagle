import sys,os,json
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import dump,OUT,SCR,USMAP,gamepath,class_schema; from uasset import Pkg,R
from tex import extract_many,export
def chain_textures(gp,seen=None):
    seen=seen if seen is not None else set()
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
u=U(USMAP)
FX=['/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Blood_Smoke_Impact_Red','/Game/ReadyOrNot/Assets/Particles/Weapon_VFX/Particles/P_VFX_Impact_Flesh_Exit',
    '/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Blood_Burst_Impact','/Game/ReadyOrNot/Assets/Particles/Gore/Blood_Pack_1/FXVillesBloodVFXPack/Particles/PS_Blood_Burst_Directional',
    '/Game/ReadyOrNot/Assets/Particles/Gore/Blood_Pack_1/FXVillesBloodVFXPack/Particles/P_Blood_GoreAndPuff','/Game/ReadyOrNot/Assets/Particles/Impacts/Blood/Blood_Artery_Med',
    '/Game/ReadyOrNot/VFX/VFXImpacts/Edit/P_Bullet_Impact_Fabric_Big']
DEC=['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/Blood_0%d_Inst'%i for i in range(1,8)]+['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/HEADSHOT/MI_Blood_Headshot_0%d'%i for i in (1,2,3)]+\
    ['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/Blood_Drop_0%d_Inst'%i for i in (1,2,3,5)]+['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/Blood_Drop_04_Inst1']+\
    ['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/M_Decal_BloodPool_0%d'%i for i in (1,2,3,4)]+['/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/Blood_Angle_01_Inst','/Game/ReadyOrNot/Assets/Decals/DAMAGE/BLOOD/Splatter_Angle_02_Inst2',
    '/Game/ReadyOrNot/Assets/Decals/Blood/Decal_Mats/Decals_Blood_Splatter_D_Inst_1','/Game/ReadyOrNot/Assets/Decals/Blood/Decal_Mats/Decals_Blood_Splatter_D_Inst_2','/Game/Blueprints/Actors/BP_RoNBloodPool']
extract_many(FX+DEC)
out={'fx':{},'decals':{}}
def props_of(gp,pred):
    base=gamepath(gp); p=Pkg(base,u); p.fullrefs=True; paths={n:p.imports[-o-1][1]+'.'+n for c,n,o in p.imports if o<0}; res={}
    for i,e in enumerate(p.exports):
        c=p.ref(e['cls'])
        if not pred(e['name'],str(c)): continue
        try: n,ex=class_schema(p,e['cls'],u); d=p.props(R(p.data(e)),n,ex)
        except Exception as x: d={'__error__':repr(x)}
        res[p.ref(i+1)]=(str(c),d)
    return res,paths,p
for f in FX:
    if not os.path.exists(gamepath(f)+'.uasset'): out['fx'][f]='missing'; continue
    mods,paths,p=props_of(f,lambda n,c: c.startswith('ParticleModule') or c in('ParticleSpriteEmitter','ParticleLODLevel','ParticleSystem'))
    out['fx'][f]={'modules':{k:{'cls':c,**d} for k,(c,d) in mods.items()},'materials':sorted(set(p.imports[-o-1][1] for c,n,o in p.imports if c in('Material','MaterialInstanceConstant') and o<0))}
for d in DEC:
    base=gamepath(d)
    if not os.path.exists(base+'.uasset'): out['decals'][d]='missing'; continue
    mods,paths,p=props_of(d,lambda n,c: 'Material' in c or n.startswith('Default__') or 'Decal' in c)
    info={}
    for k,(c,v) in mods.items():
        info[k]={'cls':c,**{a:b for a,b in v.items() if a in('Parent','ScalarParameterValues','VectorParameterValues','TextureParameterValues','BasePropertyOverrides','DecalSize','FadeStartDelay','FadeDuration','SortOrder')}}
    out['decals'][d]={'objects':info,'textures':sorted(chain_textures(d))}
json.dump(out,open(sys.argv[1],'w'),indent=1,default=str)
print({k:sum(1 for v in out[k].values() if v!='missing') for k in out}, [k for k in list(out['fx'])+list(out['decals']) if (out['fx'].get(k) or out['decals'].get(k))=='missing'])
