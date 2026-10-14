"""Ready or Not's muzzle flashes for the "ron" flash look: the Grizzly (MK V .50 AE, DLC3) BP_Secondary_LARGrizzly's
MuzzleFlash component uses WeaponFlash_Pistol_V3 (Cascade), 13.6 cm ahead of the gun, 2.5 cm up (the Deagle); the
G19 uses WeaponFlash_Pistol_V2 (docs/muzzle_flash_research.md; the Glock).
Collects the system the way fxcollect.py does (same JSON layout, so cascade_sim.py / fxrender.py can play it), its
materials and every texture under them as PNG.

    python3 -I tools/ron/flash_collect.py <out.json> [png dir]   (run from E:/Tools/deagle-work/ron/work, like fxcollect)
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from usmap import U
from dump import SCR, USMAP, gamepath, class_schema
from uasset import Pkg, R
from tex import extract_many, export
u = U(USMAP)
SYSTEMS = ['/Game/ReadyOrNot/Assets/Particles/WeaponFlash/WeaponFlash_Pistol_V3', '/Game/ReadyOrNot/Assets/Particles/WeaponFlash/WeaponFlash_Pistol_V2']
out = sys.argv[1]
png = sys.argv[2] if len(sys.argv) > 2 else '/mnt/e/Tools/deagle-work/ron_fx/flash'


def mic_info(gp):   # a material (instance): its parent, parameters and the textures it imports (as in fxcollect.py)
    base = gamepath(gp)
    if not base or not os.path.exists(base + '.uasset'): extract_many([gp])
    if not base or not os.path.exists(base + '.uasset'): return {'missing': gp}
    p = Pkg(base, u); e = [x for x in p.exports if 'Material' in str(p.ref(x['cls']))][0]
    paths = {n: (p.imports[-o - 1][1] + '.' + n if o < 0 else n) for c, n, o in p.imports}
    info = {'class': p.ref(e['cls']), 'textures': sorted(set(p.imports[-o - 1][1] for c, n, o in p.imports if c == 'Texture2D' and o < 0))}
    try:
        n, ex = class_schema(p, e['cls'], u); d = p.props(R(p.data(e)), n, ex)
        info['parent'] = paths.get(d.get('Parent'), d.get('Parent'))
        for key in ('ScalarParameterValues', 'VectorParameterValues', 'TextureParameterValues'):
            vals = {}
            for it in d.get(key, []) or []:
                nm = (it.get('ParameterInfo') or {}).get('Name'); v = it.get('ParameterValue')
                vals[nm] = paths.get(v, v) if key.startswith('Texture') else v
            if vals: info[key.replace('ParameterValues', '')] = vals
        if d.get('BasePropertyOverrides'): info['BasePropertyOverrides'] = d['BasePropertyOverrides']
    except Exception as x: info['error'] = repr(x)
    return info


def chain_textures(gp, seen):   # textures of a material and of its parents
    if gp in seen or not gp or not gp.startswith('/Game'): return set()
    seen.add(gp); base = gamepath(gp)
    if not os.path.exists(base + '.uasset'): extract_many([gp])
    if not os.path.exists(base + '.uasset'): return set()
    p = Pkg(base, u); t = set(); parents = []
    for c, n, o in p.imports:
        if o < 0 and c == 'Texture2D': t.add(p.imports[-o - 1][1])
        if o < 0 and c in ('Material', 'MaterialInstanceConstant'): parents.append(p.imports[-o - 1][1])
    for q in parents: t |= chain_textures(q, seen)
    return t


extract_many(SYSTEMS)
fx = {}
for f in SYSTEMS:
    base = gamepath(f)
    p = Pkg(base, u); p.fullrefs = True; mods = {}
    for i, e in enumerate(p.exports):
        c = p.ref(e['cls'])
        if not (str(c).startswith('ParticleModule') or c in ('ParticleSpriteEmitter', 'ParticleSystem', 'ParticleLODLevel')): continue
        try:
            n, ex = class_schema(p, e['cls'], u); d = p.props(R(p.data(e)), n, ex)
        except Exception as x: d = {'__error__': repr(x)}
        mods[p.ref(i + 1)] = {'cls': c, **{a: b for a, b in d.items() if a != 'LODValidity'}}
    fx[f] = {'type': 'Cascade', 'modules': mods,
             'materials': sorted(set(p.imports[-o - 1][1] for c, n, o in p.imports if c in ('Material', 'MaterialInstanceConstant') and o < 0))}
pmats = sorted(set(m for v in fx.values() for m in v['materials']))
pm = {m: mic_info(m) for m in pmats}
mat_tex = {m: sorted(chain_textures(m, set())) for m in pmats}
alltex = sorted(set(x for v in mat_tex.values() for x in v))
extract_many(alltex)
os.makedirs(png, exist_ok=True); idx = {}
for gp in alltex:
    try: idx[gp] = export(gp, f'{png}/{os.path.basename(gp)}.png')
    except Exception as x: idx[gp] = {'error': repr(x)}
json.dump({'particle_systems': fx, 'particle_materials': pm, 'material_textures': mat_tex, 'exports': idx},
          open(out, 'w'), indent=1, default=str)
print('systems', len(fx), 'materials', len(pm), 'textures', len(alltex), 'png', sum('png' in str(v) for v in idx.values()))
for m, t in mat_tex.items(): print(m.split('/')[-1], '->', [x.split('/')[-1] for x in t])
