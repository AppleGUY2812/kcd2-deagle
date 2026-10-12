import sys,json; sys.path.insert(0,__import__('os').path.dirname(__file__)); from usmap import U; from dump import OUT,USMAP,class_schema; from uasset import Pkg,R
u=U(USMAP)
def tb(v,k,n=14):
    t=(v.get(k) or {}).get('Table') if isinstance(v.get(k),dict) else None
    if t and t.get('Values'): return '/'.join(f'{x:g}' for x in t['Values'][:n])+(f' (TimeScale {t.get("TimeScale"):g})' if t.get('TimeScale') else '')
    return None
b=OUT+'/'+sys.argv[1]; p=Pkg(b,u); p.fullrefs=True; paths={n:p.imports[-o-1][1].split('/')[-1] for c,n,o in p.imports if o<0}
mods={}
for i,e in enumerate(p.exports):
    try: n,ex=class_schema(p,e['cls'],u); d=p.props(R(p.data(e)),n,ex)
    except Exception as x: d={'err':repr(x)}
    mods[p.ref(i+1)]=(p.ref(e['cls']),d)
for k,(c,v) in mods.items():
    if c=='ParticleSystem': print('SYSTEM',{a:x for a,x in v.items() if not isinstance(x,(dict,list))})
for k,(c,v) in mods.items():
    if c!='ParticleSpriteEmitter': continue
    print('== emitter',v.get('EmitterName'),{a:x for a,x in v.items() if a not in('LODLevels','EmitterName') and not isinstance(x,(dict,list))})
    L=mods.get((v.get('LODLevels') or [None])[0],(None,{}))[1]
    for m in [L.get('RequiredModule'),L.get('SpawnModule')]+(L.get('Modules') or [])+[L.get('TypeDataModule')]:
        if m not in mods: continue
        mc,mv=mods[m]; s=mc.replace('ParticleModule','')
        if s=='Required': print('   Required mat',paths.get(mv.get('Material'),mv.get('Material')),'subUV',mv.get('SubImages_Horizontal'),mv.get('SubImages_Vertical'),'dur',mv.get('EmitterDuration'),'loops',mv.get('EmitterLoops'),'local',mv.get('bUseLocalSpace'),'interp',mv.get('InterpolationMethod'),'screenAlign',mv.get('ScreenAlignment'),'delay',mv.get('EmitterDelay'))
        elif s=='Spawn': print('   Spawn rate',tb(mv,'Rate'),'bursts',[(b.get('CountLow'),b.get('Count'),b.get('Time')) for b in mv.get('BurstList') or []])
        else:
            ks={x:tb(mv,x) for x in mv if tb(mv,x)}
            other={x:(paths.get(y,y) if isinstance(y,str) else y) for x,y in mv.items() if not isinstance(y,(dict,list)) and x not in('LODValidity',)}
            print('  ',s,ks,other if other else '')
