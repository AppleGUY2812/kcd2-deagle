import sys,os,json,re,subprocess
sys.path.insert(0,os.path.dirname(__file__)); from usmap import U; from dump import dump,OUT,SCR,PAKS,USMAP,gamepath,class_schema; from uasset import Pkg,R
from niagara import read_system
from tex import extract_many
u=U(USMAP)
M=json.load(open(SCR+'/impact_map.json'))
GROUPS={'plaster_drywall':['RON_Plaster','RON_Drywall','RON_Wallpaper'],'concrete':['RON_ConcreteSoft','RON_ConcreteStrong'],'brick':['RON_Brick'],
 'wood':['RON_Pine','RON_Plywood','RON_Mahogany','RON_Treewood'],'metal':['RON_Steel','RON_Aluminium','RON_Galvanized','RON_Tin'],
 'glass':['RON_Glass_Plate','RON_Glass_Windshield','RON_Bulletproof_Glass'],'flesh':['RON_Flesh'],'cloth':['RON_Fabric_Thin','RON_Fabric_Carpet','RON_Fabric_Stuffing'],
 'dirt':['RON_Dirt','RON_Soil','RON_Mud'],'water':['RON_Water']}
BPS=['AllImpactEffects','ImpactEffects_Pistol','ShotgunImpactEffects','ExitImpactEffects','RicochetImpactEffects']
table={}; assets={'fx':set(),'mat':set()}
def full(bp,obj): return M[bp]['paths'].get(obj) if isinstance(obj,str) else None
for g,surfs in GROUPS.items():
    for s in surfs:
        for bp in BPS:
            fx=M[bp]['props'].get(s)
            if not isinstance(fx,dict): continue
            row={k:(full(bp,v) or v) for k,v in fx.items()}
            table.setdefault(g,{}).setdefault(s,{})[bp]=row
            if row.get('ParticleFx'): assets['fx'].add(row['ParticleFx'].split('.')[0])
            if row.get('Decal'): assets['mat'].add(row['Decal'].split('.')[0])
extract_many(sorted(assets['fx']|assets['mat']))
def mic_info(gp,depth=0):
    base=gamepath(gp)
    if not base or not os.path.exists(base+'.uasset'): extract_many([gp])
    if not base or not os.path.exists(base+'.uasset'): return {'missing':gp}
    p=Pkg(base,u); e=[x for x in p.exports if 'Material' in str(p.ref(x['cls']))][0]
    paths={n:(p.imports[-o-1][1]+'.'+n if o<0 else n) for c,n,o in p.imports}
    info={'class':p.ref(e['cls']),'textures':sorted(set(p.imports[-o-1][1] for c,n,o in p.imports if c=='Texture2D' and o<0))}
    try:
        n,ex=class_schema(p,e['cls'],u); d=p.props(R(p.data(e)),n,ex)
        info['parent']=paths.get(d.get('Parent'),d.get('Parent'))
        for key,vk in [('ScalarParameterValues','ParameterValue'),('VectorParameterValues','ParameterValue'),('TextureParameterValues','ParameterValue')]:
            vals={}
            for it in d.get(key,[]) or []:
                nm=(it.get('ParameterInfo') or {}).get('Name'); v=it.get(vk); vals[nm]=paths.get(v,v) if key.startswith('Texture') else v
            if vals: info[key.replace('ParameterValues','')]=vals
        bp=d.get('BasePropertyOverrides') 
        if bp: info['BasePropertyOverrides']=bp
    except Exception as x: info['error']=repr(x)
    return info
mats={m:mic_info(m) for m in sorted(assets['mat'])}
fx={}
for f in sorted(assets['fx']):
    base=gamepath(f)
    if not os.path.exists(base+'.uasset'): fx[f]={'missing':True}; continue
    p=Pkg(base,u); cls=p.ref(p.exports[[i for i,e in enumerate(p.exports) if e['name']==os.path.basename(f)][0]]['cls'])
    if cls=='NiagaraSystem':
        r=read_system(base); sysd=r.get('system',{})
        em={}
        for k,v in r['emitters'].items():
            vd=(v.get('VersionData') or [{}])
            vd=vd[0] if isinstance(vd,list) and vd else (vd if isinstance(vd,dict) else {})
            em[k]={x:vd.get(x) for x in ['SimTarget','bLocalSpace','bDeterminism','CalculateBoundsMode','FixedBounds','MaxGPUParticlesSpawnPerFrame','AllocationMode','PreAllocationCount'] if x in vd}
        rend={k:{x:v.get(x) for x in ['cls','Material','Alignment','FacingMode','SubImageSize','bSubImageBlend','SortMode','bRemoveHMDRollInVR','MinFacingCameraBlendDistance','PivotInUVSpace','Meshes','bEnableLightRendering','RadiusScale','ColorAdd','bAffectsTranslucency','bOverrideRenderingEnabled'] if x in v} for k,v in r['renderers'].items()}
        curves={}
        for k,v in r['data_interfaces'].items():
            if 'Curve' in v['cls']:
                curves[k]={'cls':v['cls'],**{x:y for x,y in v.items() if x in('ShaderLUT','LUTMinTime','LUTMaxTime','LUTInvTimeRange','LUTNumSamplesMinusOne')}}
        user=(sysd.get('SystemCompiledData') or {}).get('InstanceParamStore')
        from niagara import decode_store
        fx[f]={'type':'Niagara','emitters':em,'renderers':rend,'curves':curves,'user_params':decode_store(user,p) if user else {},'materials':r['materials'],'meshes':r['meshes'],'EffectType':sysd.get('EffectType'),'note':'spawn count / lifetime / velocity are compiled into Niagara VM bytecode (not readable)'}
    else:
        p.fullrefs=True; mods={}
        for i,e in enumerate(p.exports):
            c=p.ref(e['cls'])
            if not (str(c).startswith('ParticleModule') or c in('ParticleSpriteEmitter','ParticleSystem','ParticleLODLevel')): continue
            try:
                n,ex=class_schema(p,e['cls'],u); d=p.props(R(p.data(e)),n,ex)
            except Exception as x: d={'__error__':repr(x)}
            mods[p.ref(i+1)]={'cls':c,**{a:b for a,b in d.items() if a!='LODValidity'}}
        fx[f]={'type':'Cascade','modules':mods,'materials':sorted(set(p.imports[-o-1][1] for c,n,o in p.imports if c in('Material','MaterialInstanceConstant') and o<0))}
pmats=sorted(set(m for v in fx.values() for m in v.get('materials',[]) or []))
pm={m:mic_info(m) for m in pmats}
json.dump({'surface_groups':table,'decal_materials':mats,'particle_systems':fx,'particle_materials':pm},open(sys.argv[1],'w'),indent=1,default=str)
print('fx',len(fx),'decal mats',len(mats),'particle mats',len(pm)); print([k for k,v in fx.items() if v.get('missing')])
