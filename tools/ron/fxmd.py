import json,os,struct,sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from ronpaths import WORK
S=WORK   # fx_collect.json, impact_map.json, fxout/
D=json.load(open(S+'/fx_collect.json')); PI=json.load(open(S+'/fxout/particles_index.json')); DI=json.load(open(S+'/fxout/decals/_index.json'))
M=json.load(open(S+'/impact_map.json'))
E='E:\\Tools\\deagle-work\\ron_fx\\'
def short(p): return p.split('/')[-1].split('.')[0] if isinstance(p,str) else str(p)
def png(gp,cat):
    if gp in DI and 'png' in DI[gp]: return f"{E}decals\\{short(gp)}.png ({DI[gp]['size'][0]}²,{DI[gp]['format'][3:]})"
    v=PI['exports'].get(gp,{})
    if 'png' in v: c='tracer' if '/tracer/' in v['png'] else 'particles'; return f"{E}{c}\\{short(gp)}.png ({v['size'][0]}x{v['size'][1]},{v['format'][3:]})"
    return short(gp)+' (engine texture, not exported)'
def box(h):
    try: b=bytes.fromhex(h); v=struct.unpack('<6d',b[:48]); return f'{v[0]:g}..{v[3]:g} / {v[1]:g}..{v[4]:g} / {v[2]:g}..{v[5]:g}'
    except Exception: return '-'
def lut(c):
    l=c.get('ShaderLUT') or []
    if not l: return '-'
    if all(isinstance(x,(int,float)) for x in l): return f'{l[0]:g} → max {max(l):g} → {l[-1]:g} ({len(l)} samples)'
    return f'{len(l)} samples'
def tbl(d,key):
    t=(d.get(key) or {}).get('Table') if isinstance(d.get(key),dict) else None
    return '/'.join(f'{x:g}' for x in t['Values']) if t and t.get('Values') else '-'
def niagara(fx):
    o=[]
    for en,e in fx['emitters'].items():
        name=en.split('/')[-1]; rend=[v for k,v in fx['renderers'].items() if k.startswith(en+'/')]
        cv=[ (k.split('/')[-1],v) for k,v in fx['curves'].items() if k.startswith(en+'/SpawnScript/') or k.startswith(en+'/UpdateScript/')]
        rs='; '.join(f"{r.get('cls','').replace('Niagara','').replace('RendererProperties','')} mat `{r.get('Material')}` subUV {r.get('SubImageSize','-')} align {r.get('Alignment','-')}" for r in rend)
        cs='; '.join(f"{n.replace('NiagaraDataInterface','')}: {lut(v)}" for n,v in cv[:4])
        o.append(f"  - `{name}`: {e.get('SimTarget','CPUSim')}, localSpace {e.get('bLocalSpace',0)}, bounds {box(e.get('FixedBounds','')) if e.get('FixedBounds') else e.get('CalculateBoundsMode','dynamic')} | {rs} | curves: {cs or '-'}")
    if fx.get('user_params'): o.append('  - user params: '+', '.join(f'{k}={v}' for k,v in fx['user_params'].items()))
    return o
def cascade(fx):
    o=[]; mods=fx['modules']
    order=[]
    for k,v in mods.items():
        if v['cls']=='ParticleSpriteEmitter':
            order.append(k)
            for lod in (v.get('LODLevels') or [])[:1]:
                L=mods.get(lod,{}); order+= [x for x in [L.get('RequiredModule'),L.get('SpawnModule')]+(L.get('Modules') or [])+[L.get('TypeDataModule')] if x in mods]
    order+= [k for k in mods if k not in order]
    for k in order:
        v=mods[k]
        c=v['cls']
        if c=='ParticleModuleSpawn': o.append(f"    - Spawn: rate {tbl(v,'Rate')}, bursts "+(', '.join(f"{(str(b.get('CountLow'))+'-') if (b.get('CountLow') or -1)>=0 else ''}{b.get('Count',0)}@{b.get('Time',0)}s" for b in (v.get('BurstList') or [])) or '-'))
        elif c.startswith('ParticleModuleTypeData'): o.append(f"    - TypeData {c[22:]}: "+', '.join(f'{a}={b}' for a,b in v.items() if a!='cls' and not isinstance(b,(dict,list)))[:200])
        elif c=='ParticleModuleLifetime': o.append(f"    - Lifetime: {tbl(v,'Lifetime')} s")
        elif c in('ParticleModuleSize','ParticleModuleSizeMultiplyLife'): o.append(f"    - {c[14:]}: {tbl(v,'StartSize') if 'StartSize' in v else tbl(v,'LifeMultiplier')}")
        elif c=='ParticleModuleVelocity': o.append(f"    - Velocity: {tbl(v,'StartVelocity')} radial {tbl(v,'StartVelocityRadial')}")
        elif c in('ParticleModuleColorOverLife','ParticleModuleColor'): o.append(f"    - {c[14:]}: color {tbl(v,'ColorOverLife') if 'ColorOverLife' in v else tbl(v,'StartColor')} alpha {tbl(v,'AlphaOverLife') if 'AlphaOverLife' in v else tbl(v,'StartAlpha')}")
        elif c=='ParticleModuleAcceleration' or c=='ParticleModuleAccelerationConstant': o.append(f"    - Acceleration: {tbl(v,'Acceleration')}")
        elif c=='ParticleModuleRequired': o.append(f"    - Required: material `{v.get('Material')}`, subUV {v.get('SubImages_Horizontal','1')}x{v.get('SubImages_Vertical','1')}, localSpace {v.get('bUseLocalSpace',0)}, duration {v.get('EmitterDuration','1(def)')}, loops {v.get('EmitterLoops','0(def)')}")
        elif c=='ParticleSpriteEmitter': o.append(f"  - emitter `{v.get('EmitterName')}`")
    return o
o=['# Ready or Not bullet-impact & tracer visuals','',
'Read-only decode of the Steam install (UE5.3), private use. Textures are full-resolution mip 0 as PNG (BC5 normals get a rebuilt Z). `-` = not set (engine default); **name-only** = the property exists but holds no readable value.',
f'Exports: `{E}decals\\` (64 PNG), `{E}particles\\` (95 PNG), raw decode JSON in `{E}_data\\`.','',
'## How impacts are picked','',
'- Bullet hit → `ImpactEffect` BP chosen by weapon/ammo: **AllImpactEffects** (rifle/default), **ImpactEffects_Pistol**, **ShotgunImpactEffects**, **ExitImpactEffects** (exit wounds/holes), **RicochetImpactEffects**. Each has one `ImpactFx` per physical surface `RON_*`: ParticleFx (Niagara `NS_` or Cascade `P_`), Decal material, optional DecalMesh (glass) and PaintMaterialTexture (flesh/paper mesh-paint).',
'- Decal size is random between DecalMinSize and DecalMaxSize (UE units = cm): '+', '.join(f"{b} {M[b]['props'].get('DecalMinSize','-')}–{M[b]['props'].get('DecalMaxSize','-')}" for b in ['ImpactEffects_Pistol','AllImpactEffects','ShotgunImpactEffects','ExitImpactEffects'])+f". DecalMeshScaleMultiplier (glass meshes): All {M['AllImpactEffects']['props'].get('DecalMeshScaleMultiplier')}, Shotgun {M['ShotgunImpactEffects']['props'].get('DecalMeshScaleMultiplier')}. Ricochet ParticleScale {M['RicochetImpactEffects']['props'].get('ParticleScale')}.",
'- **Decal lifetime / fade / random rotation: name-only.** No DecalComponent template exists in the BPs; decals are spawned in C++ (ImpactEffect.DecalComponent, DecalScale) with code defaults.',
'- Niagara impacts are GPU sims in world space with fixed bounds. **Spawn counts, lifetime, velocity and gravity are compiled into Niagara VM bytecode and are not readable** (only curves as baked LUTs, renderer setup, user colours). Cascade (`P_`) systems are fully readable (module tables below).','']
done=set()
for g,surfs in D['surface_groups'].items():
    o.append(f'## {g.replace("_"," / ")}'); o.append('')
    for s,bps in surfs.items():
        o.append(f'### {s}'); o.append('')
        o.append('| impact BP | particle FX | decal material | decal textures |'); o.append('|---|---|---|---|')
        for bp,row in bps.items():
            dm=row.get('Decal'); mi=D['decal_materials'].get(dm.split('.')[0],{}) if isinstance(dm,str) else {}
            tex='<br>'.join(png(t,'decals') for t in mi.get('textures',[]) if not t.startswith('/Engine')) or '-'
            extra=[]
            if row.get('DecalMesh'): extra.append('DecalMesh `'+short(row['DecalMesh'])+'`')
            if row.get('PaintMaterialTexture'): extra.append('mesh-paint `'+short(row['PaintMaterialTexture'])+'`')
            o.append(f"| {bp} | `{short(row.get('ParticleFx'))}` | `{short(dm)}` {' '.join(extra)} | {tex} |")
        o.append('')
        for bp,row in bps.items():
            f=row.get('ParticleFx')
            if not isinstance(f,str): continue
            f=f.split('.')[0]
            if f not in D['particle_systems']: continue
            if f in done: o.append(f"- **{short(f)}**: see above"); continue
            done.add(f); fx=D['particle_systems'][f]
            mats=sorted(set(fx.get('materials',[])))
            o.append(f"- **{short(f)}** ({fx['type']}, `{f}`)")
            o+= niagara(fx) if fx['type']=='Niagara' else cascade(fx)
            for m in mats:
                ts=[t for t in PI['material_textures'].get(m,[]) if not t.startswith('/Engine')]
                o.append(f"  - material `{short(m)}` → "+(', '.join(png(t,'particles') for t in ts) or 'no textures (procedural)'))
        o.append('')
# tracer
o+=['## Tracers','',
'- Asset: `/Game/ReadyOrNot/VFX/VFXImpacts/P_BulletTracer` (Cascade). It is spawned by the native `BulletTracer` actor (TracerParticle, SmokeParticle components).',
'- Emitter `Tracer`, **ribbon** type (`ParticleModuleTypeDataRibbon`, MaxParticleInTrailCount 25), trail source = actor socket `TracerSource` (strength 100).',
'- Spawn: 1 particle per 25 cm travelled (SpawnPerUnit, UnitScalar 25), rate 0. Lifetime **0.1 s**. Start size 0.25–2 (random per axis, i.e. ribbon width 0.25–2 cm). Start velocity 0.',
'- Colour over life: **HDR (40, 7, 0) → (1, 0, 0)** (orange-red emissive ×40 fading to dim red; TimeScale 1.11). Alpha over life 1 → 0 (TimeScale 1.33).',
'- Acceleration: random −500..500 / −1500..1500 / −500..500 cm/s² in world space (wobble along the trail).',
'- Material `/Game/ReadyOrNot/VFX/Materials/M_Ribbon`: a base Material with **no texture references** (procedural gradient; expression graph not readable from cooked data). Nothing to export.',
'- Length vs camera distance: no field; length = trail of ≤25 segments × 25 cm (~6 m) that fades in 0.1 s. **Name-only:** BaseMagazineWeapon.BulletTracers (class array, not overridden).',
'- Which rounds: every gun has `bSpawnTracer = true` **and** `bNoSpawnTracerForFiringPlayer = true`, so you only see tracers fired by others, never your own. No per-ammo tracer flag in AmmoDataTable.','',
'## Muzzle-side bits','',
'- Shell casings: `BulletShell_C` (weapon `ShellClass`), `ShellParticle` component on the gun, `ShellBounceFMODAudio` sound. No dedicated ground-puff FX found; footstep-style puffs exist (`/Game/ReadyOrNot/VFX/VFXFootsteps/P_Walk_ConcreteDustPuff` etc.) and are used by Walk/RunImpactEffects (MaxLifespan 5 s, decals 6–12).',
'- Bullet whiz / crack: **sound only** (`BulletProjectile.BulletWizzSound`, MinimumDistanceForWizz 500 cm). No heat-distortion visual in the data.',
'- Gun heat haze: `HeatSmokeParticleComponent` on weapons (HeatPerShot 0.1, HeatMinimumTime 0.8 s on the G19); particle asset not decoded here.','',
'## Surface → effect table (rifle / AllImpactEffects, every surface)','','| surface | particle FX | decal |','|---|---|---|']
A=M['AllImpactEffects']
for k,v in A['props'].items():
    if isinstance(v,dict) and ('ParticleFx' in v or 'Decal' in v): o.append(f"| {k} | `{short(v.get('ParticleFx'))}` | `{short(v.get('Decal'))}` |")
open(sys.argv[1],'w').write('\n'.join(o)+'\n'); print(len(o),'lines')
