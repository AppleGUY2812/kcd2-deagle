"""Bakes Ready or Not's pistol muzzle flash into flash cards for the "ron" flash look (flashMesh.look "ron").

Ready or Not's flash lives 0.035-0.04 s (sparks to 0.095 s); KCD2 draws a particle that short late or not at all, so
the flash is shown on cards on the muzzle bone instead, frame-exact. Ready or Not's own Cascade system
(flash_collect.py: WeaponFlash_Pistol_V3 for the Deagle, V2 for the Glock) runs in tools/ron/cascade_sim.py, plus what
that skips (VelocityCone and SizeScaleBySpeed for the sparks, the Pop's event-spawned side flame, PivotOffset). Every
particle alive at a stage's middle becomes a quad: camera-facing sprites face back down the barrel, velocity-aligned
ones lie along their velocity in the plane that best faces the eye (above and behind the muzzle). Sizes, positions,
rotations, flipbook tiles, colours and fades are Ready or Not's. A card = one variant at one stage; Lua shows the
stage the flash's game time is in.

    python3 tools/ron/flash_bake.py <repo> [gain]
    -> E:/Tools/deagle-work/flash_ron/cards.json (for tools/flash_mesh_ron.py), tif/*.tif (for tools/fx_textures.ps1),
       src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash_ron{,_dim,_hot}.mtl

Coordinates: Ready or Not's emitter has +X down the barrel, +Y right, +Z up (cm); the muzzle bone has +Y down the
barrel, X and Z flipped against the viewmodel's right and up (MODLOG 2026-10-14), so bone = (-Y, X, -Z) / 100.
HDR colour -> Illum emittance: Claude 2's particle mapping (EmissiveLighting = nits / 1000, docs/ron/niagara_gpu.md)
and Illum's x 0.1 (Illum.cfx) give intensity = nits / 100, x gain. Brightness is quantised to powers of two (one
sub-material each), the hue per family.
"""
import json, math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import cascade_sim as cs

REPO = sys.argv[1]
GAIN = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
cs.FX = '/mnt/e/Tools/deagle-work/ron_fx/_data/flash_collect.json'
PNG = '/mnt/e/Tools/deagle-work/ron_fx/flash'
OUT = '/mnt/e/Tools/deagle-work/flash_ron'
VARIANTS, STAGES, T = 4, 10, 0.095
EYE = np.array([0.057, -0.407, -0.042])   # the eye from the muzzle, bone axes, m (MuzzleLocal: lp 0.057 0.407 -0.042)
SETS = {'deagle_vm_flash_ron': 'WeaponFlash_Pistol_V3', 'deagle_vm_flash_ron_s': 'WeaponFlash_Pistol_V2'}
SHEETS = {   # material -> texture, tiles across, down (Ready or Not's materials, flash_collect.json)
    'MI_MuzzleFlash_SMG': ('Rifle_MuzzleFlash_Macro_Rifle_-_Copy__2_', 4, 4),
    'MI_MuzzleFlash_Rifle': ('MuzzleFlash_Macro_M14', 4, 4),
    'MI_MuzzleFlash_Pistol': ('MuzzleFlash_Macro_M2011', 4, 4),
    'MI_MuzzleFlash_Side': ('T_muzzle_flashes', 2, 2),
    'M_Particle_Additive': ('T_longSpark_02_8x2', 4, 2),
}
TEX = {m: 'ron_flash_' + t.split('_-_')[0].lower() for m, (t, _, _) in SHEETS.items()}
HUES = {'pop': (1.0, 0.0118, 0.0052), 'spark': (1.0, 0.118, 0.0052)}
D = json.load(open(cs.FX))


def modules(system):
    k = [x for x in D['particle_systems'] if x.split('/')[-1] == system][0]
    return D['particle_systems'][k]['modules']


def module(E, cls):
    return next((m for m in E.mods if m['cls'] == 'ParticleModule' + cls), None)


def pivot(E):
    m = module(E, 'PivotOffset')
    return float(m['PivotOffset'][1]) if m and m.get('PivotOffset') else 0.0


def run(E, seed, force=None):
    if force is not None:   # the Pop's burst is 0-1 particles; its side flame is spawned by the Pop's event
        E.spawn = dict(E.spawn, BurstList=[{'Count': force, 'CountLow': -1, 'Time': 0.0}])
    return E.run(T + 0.01, 1, dt=0.0005, seed=seed)


def sparks(E, frames, rng):
    """VelocityCone (+X, angle 0-0.075 x 180 deg, 500-5000 cm/s), drag and SizeScaleBySpeed (y x clamp(speed x 0.05,
    1, 100)), which cascade_sim skips: positions integrated here from each spark's birth"""
    vc = module(E, 'VelocityCone'); n = max((f['n'] for f in frames), default=0)
    ang = np.radians(rng.uniform(0, 0.075, n) * 180); az = rng.uniform(0, 2 * np.pi, n); spd = rng.uniform(500, 5000, n)
    d = np.stack([np.cos(ang), np.sin(ang) * np.cos(az), np.sin(ang) * np.sin(az)], 1)
    drag = rng.uniform(2.5, 15, n)
    out = []
    for f in frames:
        if not f['n']: out.append(f); continue
        g = dict(f); age = f['Age'][:, 0]; k = drag[:f['n']]
        v0 = d[:f['n']] * spd[:f['n'], None]
        g['Position'] = v0 / k[:, None] * (1 - np.exp(-k * age))[:, None]
        g['Velocity'] = v0 * np.exp(-k * age)[:, None]
        sp = np.linalg.norm(g['Velocity'], axis=1)
        sz = f['SpriteSize'].copy(); sz[:, 1] *= np.clip(sp * 0.05, 1, 100); g['SpriteSize'] = sz
        out.append(g)
    return out


def at(frames, age):
    return min(frames, key=lambda f: abs(f['t'] - age))


def tile_uv(sheet, idx):
    _, nx, ny = SHEETS[sheet]
    i = int(idx) % (nx * ny); c, r = i % nx, i // nx; e = 1.0 / 512
    u0, u1 = c / nx + e, (c + 1) / nx - e
    v0, v1 = 1 - (r + 1) / ny + e, 1 - r / ny - e   # Blender V is up
    return [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]   # corners: bottom-left, bottom-right, top-right, top-left


def to_bone(p):
    return np.array([-p[1], p[0], -p[2]]) / 100.0


def quad(sheet, align, piv, f, j):
    pos = to_bone(f['Position'][j]); sx, sy = f['SpriteSize'][j][:2] / 100.0
    col = f['Color'][j]; lum = float(col[0] * col[3])
    if lum <= 0.5 or sx <= 0 or sy <= 0: return None
    hue = 'spark' if col[1] / max(col[0], 1e-6) > 0.05 else 'pop'
    if align == 'VelocityAligned':
        v = to_bone(f['Velocity'][j]); vn = np.linalg.norm(v)
        up = v / vn if vn > 1e-6 else np.array([0.0, 1.0, 0.0])          # the texture's top leads
        cam = EYE - pos; nrm = cam - cam.dot(up) * up; nrm /= max(np.linalg.norm(nrm), 1e-6)
        right = np.cross(up, nrm); right /= max(np.linalg.norm(right), 1e-6)
        c = pos + up * sy * (-piv / 2)
    else:   # facing the eye (back down the barrel), the sprite's own rotation about that axis
        a = math.radians(float(f['SpriteRotation'][j][0]))
        up0, right0 = np.array([0.0, 0.0, -1.0]), np.array([-1.0, 0.0, 0.0])
        up = up0 * math.cos(a) + right0 * math.sin(a); right = right0 * math.cos(a) - up0 * math.sin(a)
        c = pos
    hx, hy = right * sx / 2, up * sy / 2
    corners = [c - hx - hy, c + hx - hy, c + hx + hy, c - hx + hy]
    level = int(round(math.log2(lum)))
    return {'mtl': f'{TEX[sheet][10:]}_{hue}_{level:02d}', 'tex': TEX[sheet], 'hue': hue, 'level': level,
            'p': [list(map(float, q)) for q in corners], 'uv': tile_uv(sheet, f['SubImageIndex'][j][0])}


def bake(system, name):
    mods = modules(system)
    ems = cs.load(system)
    cards = []
    for v in range(VARIANTS):
        rng = np.random.default_rng(100 + v)
        pop = 1 if v % 2 == 0 else 0      # Ready or Not's Pop burst is 0-1: the big fireball on half the shots
        runs = []
        for i, E in enumerate(ems):
            if not E.enabled: continue
            sheet = E.req.get('Material')
            al = E.renderer()['Alignment']
            event = any(m['cls'] == 'ParticleModuleEventReceiverSpawn' for m in E.mods)
            gen = any(m['cls'] == 'ParticleModuleEventGenerator' for m in E.mods)
            force = pop if (event or gen) else None
            fr = run(E, 1000 * v + i + 1, force)
            if E.name == 'Sparks': fr = sparks(E, fr, rng)
            runs.append((sheet, al, pivot(E), fr))
        stages = []
        for s in range(STAGES):
            age = (s + 0.5) * T / STAGES
            qs = []
            for sheet, al, piv, fr in runs:
                f = at(fr, age)
                for j in range(f['n']):
                    q = quad(sheet, al, piv, f, j)
                    if q: qs.append(q)
            if not qs:   # the flash is over for this variant: a 1 mm quad at the dimmest level inside the bore (every card is a model)
                qs.append({'mtl': 't_longspark_02_8x2_pop_00', 'tex': TEX['M_Particle_Additive'], 'hue': 'pop', 'level': 0,
                           'p': [[-0.0005, -0.01, -0.0005], [0.0005, -0.01, -0.0005], [0.0005, -0.01, 0.0005], [-0.0005, -0.01, 0.0005]],
                           'uv': tile_uv('M_Particle_Additive', 0)})
            stages.append(qs)
        cards.append(stages)
    return cards


os.makedirs(OUT + '/tif', exist_ok=True)
sets = {name: bake(system, name) for name, system in SETS.items()}
mats = sorted({(q['mtl'], q['tex'], q['hue'], q['level']) for c in sets.values() for v in c for s in v for q in s})
json.dump({'sets': sets, 'materials': [m[0] for m in mats], 'stages': STAGES, 'variants': VARIANTS, 'time': T},
          open(OUT + '/cards.json', 'w'))
from PIL import Image
for m, (t, _, _) in SHEETS.items():
    Image.open(f'{PNG}/{t}.png').convert('RGBA').save(f'{OUT}/tif/{TEX[m]}_diff.tif', compression='tiff_lzw')
for suffix, g in (('', 1.0), ('_dim', 1 / 3), ('_hot', 3.0)):
    subs = []
    for mtl, tex, hue, level in mats:
        r, gg, b = HUES[hue]
        inten = 2 ** level / 100.0 * GAIN * g
        subs.append(f'    <Material Name="{mtl}" MtlFlags="524454" Shader="Illum" GenMask="20000000000000" StringGenMask="%SUBSURFACE_SCATTERING" '
                    f'SurfaceType="" MatTemplate="" Diffuse="0,0,0" Specular="0,0,0" Opacity="0.99" Emittance="{r},{gg},{b},{inten:.4g}" '
                    f'Shininess="10" vertModifType="0" LayerAct="1">\n      <Textures>\n'
                    f'        <Texture Map="Diffuse" File="textures/particles/deagle/ron_flash/{tex}_diff.tif" />\n'
                    f'        <Texture Map="Emittance" File="textures/particles/deagle/ron_flash/{tex}_diff.tif" />\n      </Textures>\n'
                    f'      <PublicParams EmittanceMapGamma="1" SSSIndex="0" />\n    </Material>')
    xml = ('<!-- Ready or Not\'s pistol muzzle flash on cards (tools/ron/flash_bake.py; private use, never published): one\n'
           f'     sub-material per texture, hue and brightness (powers of two of Ready or Not\'s HDR nits, x {GAIN * g:.3g}) -->\n'
           '<Material MtlFlags="524544" vertModifType="0">\n  <SubMaterials>\n' + '\n'.join(subs) + '\n  </SubMaterials>\n</Material>\n')
    open(f'{REPO}/src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash_ron{suffix}.mtl', 'w').write(xml)
for name, c in sets.items():
    print(name, 'quads per stage, variant 1..4:', [[len(s) for s in v] for v in c])
print('materials', len(mats))
