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
    python3 tools/ron/flash_bake.py <repo> --textures      only the textures (tif/*.tif)
    -> E:/Tools/deagle-work/flash_ron/cards.json (for tools/flash_mesh_ron.py), tif/*.tif (for tools/fx_textures.ps1),
       src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash_ron{,_dim,_hot,_x10,_x30}.mtl (by day) and {_ev9,_ev8,_ev7}.mtl (dusk to night,
       kcd2_tonemap.EV_SCENES)

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
import flash_ref as fr
import kcd2_tonemap as kt

ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
REPO = ARGS[0]
GAIN = float(ARGS[1]) if len(ARGS) > 1 else 1.0
SCENE = kt.Scene(**kt.FLASH_SCENE)   # the KCD2 scene the cards' colours are solved for (kcd2_tonemap.FLASH_SCENE)
cs.FX = '/mnt/e/Tools/deagle-work/ron_fx/_data/flash_collect.json'
PNG = '/mnt/e/Tools/deagle-work/ron_fx/flash'
OUT = '/mnt/e/Tools/deagle-work/flash_ron'
# One stage per 60 fps frame: stage k = RoN's flash at the end of frame k after the shot (flash_ref.step), so the
# card on screen is RoN's state for that frame. The short-lived sprites go on cards (front pop, its side flame, the
# pistol core, the small flame: 35-40 ms; the streak and star: 30 ms from 0.01 s; all dead after frame 1); only the
# Sparks (0-10, 0.05-0.085 s) are a particle effect, which KCD2 draws one frame after the shot.
VARIANTS, STAGES = 16, 2
T = STAGES / 60.0
CARD_KINDS = ('pop', 'pop_side', 'core', 'flame', 'longspark', 'corespark')   # 0.03 s parts too: KCD2 doesn't draw particles that short
REF_SHEET = {'SMG': 'MI_MuzzleFlash_SMG', 'Rifle': 'MI_MuzzleFlash_Rifle', 'Pistol': 'MI_MuzzleFlash_Pistol', 'Side': 'MI_MuzzleFlash_Side',
             'Add': 'M_Particle_Additive'}
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
# Ready or Not's raw hues are near pure red; its tonemapper turns its very bright ones peach-white and orange on screen,
# KCD2's kept them red with a white core and a red glow at every brightness (in game 2026-10-14, held cards x1-x30).
# So each sub-material takes the colour Ready or Not shows: Unreal's filmic curve (the ACES fit, Narkowicz 2016,
# per channel) at RON_EXPOSURE, which is fitted to the user's Ready or Not clip (frames 230-231: the fireball's body
# 248, 166, 131 on screen, G/R 0.40 in linear light) over the fireball's own levels (82-328). Kept: each one's
# brightness (KCD2's tonemapper whitens the hottest), only the hue moves: 328 -> 1 : 0.63 : 0.35, 164 -> 1 : 0.39 :
# 0.15, 82 -> 1 : 0.18 : 0.06, 20 and under stay red.
RON_EXPOSURE = 0.135


def aces(x):
    x = np.asarray(x, float)
    return np.clip(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)


def screen_hue(hue, inten):   # the colour a raw Ready or Not hue at this brightness shows on its screen, max channel 1
    d = aces(RON_EXPOSURE * np.array(HUES[hue]) * inten)
    return d / max(d.max(), 1e-9)


def write_textures():
    """Ready or Not's flash materials are all additive (M_Particle_Additive, BLEND_Additive): a texel adds its colour,
    black adds nothing, and its sheets are solid (alpha 255 everywhere). KCD2 draws the cards by alpha, so a solid sheet
    painted its dark texels over the flash behind it (dark squares, in game 2026-10-14). Each sheet gets alpha = its
    brightest channel and its colour divided by that: by alpha a texel then adds what Ready or Not's added (the sheets
    behind it dimmed by 1 - alpha), and the grey sheets become white with their shape in alpha."""
    from PIL import Image
    os.makedirs(OUT + '/tif', exist_ok=True)
    for m, (t, _, _) in SHEETS.items():
        # Ready or Not's sheets are sRGB (flash_collect.json): a texel adds L = srgb_to_linear(png). On the card KCD2
        # adds emittance x srgb_to_linear(R) x A (Illum.cfx:894, the emittance map's alpha) x A again (the blend:
        # GatherAlpha Illum.cfx:858-860), the tif compiled sRGB (rc.ini [AlbedoWithGenericAlpha] colorspace=sRGB).
        # So A = sqrt(max L) and R = linear_to_srgb(L / A^2) add exactly L
        c = np.asarray(Image.open(f'{PNG}/{t}.png').convert('RGB')).astype(np.float64) / 255.0
        L = kt.srgb_inv(c)
        A = np.sqrt(L.max(axis=2))
        rgb = np.where(A[..., None] > 0, kt.srgb(L / np.maximum(A, 1e-9)[..., None] ** 2), 1.0)
        out = np.dstack([np.clip(np.rint(rgb * 255), 0, 255), np.clip(np.rint(A * 255), 0, 255)]).astype(np.uint8)
        Image.fromarray(out, 'RGBA').save(f'{OUT}/tif/{TEX[m]}_diff.tif', compression='tiff_lzw')


if '--textures' in sys.argv:
    write_textures()
    print('textures', sorted(TEX.values()))
    sys.exit(0)
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


def screen_rgb(rgb):
    """the colour an HDR RoN colour shows on RoN's screen (its exposure and filmic curve), max channel 1, rounded to
    1/8 so cards can share sub-materials"""
    d = aces(RON_EXPOSURE * np.asarray(rgb, float))
    d = d / max(d.max(), 1e-9)
    return tuple(round(float(x) * 8) / 8 for x in d)


def card_quad(s):
    """one of flash_ref's sprites as a card: the reference's own plane (facing the eye), in bone axes"""
    c, right, up, w, h, nrm = fr.frame_of(s)
    corners = [c - right * w / 2 - up * h / 2, c + right * w / 2 - up * h / 2,
               c + right * w / 2 + up * h / 2, c - right * w / 2 + up * h / 2]
    lum = float(np.max(s['rgb']))
    if lum <= 0.5 or w <= 0 or h <= 0: return None
    sheet = REF_SHEET[s['sheet']]
    _, nx, ny = SHEETS[sheet]
    col = screen_rgb(s['rgb']); level = int(round(math.log2(lum)))
    hue = 'c%d%d%d' % tuple(int(x * 8) for x in col)
    return {'mtl': f'{TEX[sheet][10:]}_{hue}_{level:02d}', 'tex': TEX[sheet], 'hue': hue, 'col': col, 'level': level,
            'rgb': [float(x) for x in s['rgb']], 'sheet': sheet,
            'p': [list(map(float, to_bone(q))) for q in corners], 'uv': tile_uv(sheet, s['sub'] % (nx * ny))}


def ron_display(hdr):   # Ready or Not's screen colour (flash_ref.tonemap, float 0..1)
    return np.power(aces(RON_EXPOSURE * np.asarray(hdr, float)), 1 / 2.2)


_texels = {}
def texels(sheet, n=1500):   # a sample of the sheet's linear texels (L, what a texel adds in Ready or Not)
    if sheet not in _texels:
        t, _, _ = fr.tex({'MI_MuzzleFlash_SMG': 'SMG', 'MI_MuzzleFlash_Rifle': 'Rifle', 'MI_MuzzleFlash_Pistol': 'Pistol', 'MI_MuzzleFlash_Side': 'Side',
                          'M_Particle_Additive': 'Add'}[sheet])
        L = t.reshape(-1, 3).astype(np.float64)
        L = L[L.max(axis=1) > 0.01]
        _texels[sheet] = L[np.random.default_rng(0).choice(len(L), min(n, len(L)), replace=False)]
    return _texels[sheet]


def kcd2_emittance(rgb, sheet, scene=None):
    """the HDR colour k (per channel) a card needs so KCD2 (SCENE) shows each texel L of the sheet as Ready or Not
    shows it: least squares over the sheet's texels of KCD2 display(k * L) vs Ready or Not's display(rgb * L), in
    display sRGB. The two curves differ, so one k can't match every texel; this is the best single one"""
    scene = scene or SCENE
    L = texels(sheet)
    want = ron_display(L * np.asarray(rgb, float)[None, :])
    k = np.maximum(scene.solve(ron_display(np.asarray(rgb, float))), 1e-6)   # start: matched at texel 1
    err = lambda kk: float(((scene.display(L * kk[None, :]) - want) ** 2).sum())
    for _ in range(3):
        for ch in range(3):
            lo, hi = math.log2(k[ch]) - 4, math.log2(k[ch]) + 4
            for _ in range(30):   # golden section in log2
                a = hi - (hi - lo) * 0.618; b = lo + (hi - lo) * 0.618
                ka, kb = k.copy(), k.copy(); ka[ch], kb[ch] = 2 ** a, 2 ** b
                if err(ka) < err(kb): hi = b
                else: lo = a
            k[ch] = 2 ** ((lo + hi) / 2)
    return k


def bake_v2(system='WeaponFlash_Pistol_V3'):
    """RoN's V3 (or V2) run by flash_ref (the rules read from its exe), 16 shots: the front pop on half of them, as RoN's 0-1
    burst; each stage = that shot at the end of frame k, with the sheet frame, the core's colour and the drag drawn
    again every frame as RoN does"""
    cards = []
    for v in range(VARIANTS):
        rng = np.random.default_rng(100 + v)
        ps = fr.spawn_shot(rng, pop=(v % 2 == 0), system=system[-2:])
        stages = []
        for k in range(STAGES):
            sprites = [x for x in fr.step(ps, (k + 1) * fr.DT, rng) if x['kind'] in CARD_KINDS]
            qs = [q for q in (card_quad(x) for x in sprites) if q]
            if not qs:
                qs.append({'mtl': 't_longspark_02_8x2_c800_00', 'tex': TEX['M_Particle_Additive'], 'hue': 'c800',
                           'col': (1.0, 0.0, 0.0), 'level': 0, 'rgb': [1.0, 0.0, 0.0], 'sheet': 'M_Particle_Additive',
                           'p': [[-0.0005, -0.01, -0.0005], [0.0005, -0.01, -0.0005], [0.0005, -0.01, 0.0005], [-0.0005, -0.01, 0.0005]],
                           'uv': tile_uv('M_Particle_Additive', 0)})
            stages.append(qs)
        cards.append(stages)
    return cards


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
sets = {name: bake_v2(system) for name, system in SETS.items()}   # V3 (the Grizzly's) for big blasts (the Deagle), V2 (the G19's) for small ones (the Glock)
members = {}
for c in sets.values():
    for v in c:
        for s in v:
            for q in s: members.setdefault((q['mtl'], q['tex'], q['sheet']), []).append(q['rgb'])
# sub-material ids dim to bright: KCD2 draws a card's sub-materials in id order, each over the last by alpha, where
# Ready or Not adds them; brightest last keeps the white core white under the streak (named by level: <name>_<level>)
ORDER = sorted(members.items(), key=lambda kv: (int(kv[0][0].rsplit('_', 1)[1]), kv[0]))


def solve_mats(scene):   # per sub-material: (name, texture, colour with max 1, intensity) for one KCD2 scene
    mats = []
    for (mtl, tex, sheet), rgbs in ORDER:
        k = kcd2_emittance(np.mean(np.array(rgbs), axis=0), sheet, scene)
        w = float(k.max()) * 10.0                   # Illum: HDR = colour x intensity x 0.1 (Illum.cfx:953)
        mats.append((mtl, tex, tuple(round(float(x), 4) for x in k / k.max()), w))
    return mats


def write_mtl(suffix, mats, g, scene):
    subs = []
    for mtl, tex, col, w in mats:
        r, gg, b = col
        inten = w * GAIN * g
        subs.append(f'    <Material Name="{mtl}" MtlFlags="524454" Shader="Illum" GenMask="20000000000000" StringGenMask="%SUBSURFACE_SCATTERING" '
                    f'SurfaceType="" MatTemplate="" Diffuse="0,0,0" Specular="0,0,0" Opacity="0.99" Emittance="{r},{gg},{b},{inten:.4g}" '
                    f'Shininess="10" vertModifType="0" LayerAct="1">\n      <Textures>\n'
                    f'        <Texture Map="Diffuse" File="textures/particles/deagle/ron_flash/{tex}_diff.tif" />\n'
                    f'        <Texture Map="Emittance" File="textures/particles/deagle/ron_flash/{tex}_diff.tif" />\n      </Textures>\n'
                    f'      <PublicParams EmittanceMapGamma="1" SSSIndex="0" />\n    </Material>')
    xml = ('<!-- Ready or Not\'s pistol muzzle flash on cards (tools/ron/flash_bake.py; private use, never published): one\n'
           f'     sub-material per texture and Ready or Not colour; emittance solved through KCD2\'s post chain ({scene.name}), x {GAIN * g:.3g} -->\n'
           '<Material MtlFlags="524544" vertModifType="0">\n  <SubMaterials>\n' + '\n'.join(subs) + '\n  </SubMaterials>\n</Material>\n')
    open(f'{REPO}/src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash_ron{suffix}.mtl', 'w').write(xml)


mats = solve_mats(SCENE)
json.dump({'sets': sets, 'materials': [m[0] for m in mats], 'stages': STAGES, 'variants': VARIANTS, 'time': T},
          open(OUT + '/cards.json', 'w'))
write_textures()
for suffix, g in (('', 1.0), ('_dim', 1 / 3), ('_hot', 3.0), ('_x10', 10.0), ('_x30', 30.0)):   # brightness steps, to match Ready or Not's on-screen colour live
    write_mtl(suffix, mats, g, SCENE)
for suffix, kw in kt.EV_SCENES.items():   # the exposure ladder (kcd2_tonemap.EV_SCENES): the same look at dusk and night
    if suffix:
        sc = kt.Scene(**kw)
        write_mtl(suffix, solve_mats(sc), 1.0, sc)
        print(suffix, sc.name)
for name, c in sets.items():
    print(name, 'quads per stage, variant 1..4:', [[len(s) for s in v] for v in c])
print('materials', len(mats))
