"""Checks the baked flash cards (cards.json from tools/ron/flash_bake.py) against the reference they came from
(tools/ron/flash_ref.py): every variant and stage is drawn twice from the same eye, once as RoN's sprites shown the
way Ready or Not shows them (its exposure and filmic curve) and once as the cards shown the way KCD2 shows them: the
textures and emittance the game loads (the baked tifs and the .mtl), Illum's emittance and alpha blend, then KCD2's
post chain in kcd2_tonemap.FLASH_SCENE. A wrong axis, UV flip or colour shows here before the game sees it.
Owner: Claude 1.

    python3 tools/ron/flash_check.py [cards.json] [out.png] [flash .mtl] [set: deagle_vm_flash_ron (V3) | deagle_vm_flash_ron_s (V2)]
"""
import json, math, re, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, __import__('os').path.dirname(__file__))
import flash_ref as fr
import kcd2_tonemap as kt

class fb:   # flash_bake's texture names (importing flash_bake runs the bake)
    SHEETS = {'MI_MuzzleFlash_SMG': 'Rifle_MuzzleFlash_Macro_Rifle_-_Copy__2_', 'MI_MuzzleFlash_Rifle': 'MuzzleFlash_Macro_M14', 'MI_MuzzleFlash_Pistol': 'MuzzleFlash_Macro_M2011',
              'MI_MuzzleFlash_Side': 'T_muzzle_flashes', 'M_Particle_Additive': 'T_longSpark_02_8x2'}
    TEX = {m: 'ron_flash_' + t.split('_-_')[0].lower() for m, t in SHEETS.items()}

CARDS = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/flash_ron/cards.json'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/mnt/e/Tools/deagle-work/flash_ron/check.png'
MTL = sys.argv[3] if len(sys.argv) > 3 else __import__('os').path.join(__import__('os').path.dirname(__file__), '../../src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash_ron.mtl')
SET = sys.argv[4] if len(sys.argv) > 4 else 'deagle_vm_flash_ron'   # deagle_vm_flash_ron: V3 (the Grizzly's), _s: V2 (the G19's)
SYSTEM = 'V2' if SET.endswith('_s') else 'V3'
TIF = '/mnt/e/Tools/deagle-work/flash_ron/tif'
SCENE = kt.Scene(**kt.FLASH_SCENE)
OPACITY = 0.99   # the .mtl's Opacity: MatDifColor.w in GatherAlpha (Illum.cfx:858)


def emittance():   # sub-material -> HDR colour per unit texel (Emittance rgb x intensity x 0.1, Illum.cfx:953)
    out = {}
    for m in re.finditer(r'<Material Name="([^"]+)"[^>]*Emittance="([^"]+)"', open(MTL).read()):
        r, g, b, w = (float(x) for x in m.group(2).split(','))
        out[m.group(1)] = np.array([r, g, b]) * w * 0.1
    return out


_tifs = {}
def tif(name):   # the baked texture as KCD2 samples it: sRGB rgb linearised, alpha as is
    if name not in _tifs:
        from PIL import Image
        t = np.asarray(Image.open(f'{TIF}/{name}_diff.tif').convert('RGBA')).astype(np.float64) / 255.0
        _tifs[name] = (kt.srgb_inv(t[..., :3]), t[..., 3])
    return _tifs[name]


def from_bone(b):   # flash_bake.to_bone inverted: bone (m) -> RoN axes (cm)
    return np.array([b[1], -b[0], -b[2]]) * 100.0


def draw_cards(quads, img, emit):
    fwd, f = fr.camera()
    yy, xx = np.mgrid[0:fr.H, 0:fr.W]
    dirs = np.stack([np.full(xx.shape, f), (xx - fr.W / 2), -(yy - fr.H / 2)], -1).astype(np.float64)
    for q in quads:
        P = [from_bone(p) for p in q['p']]           # corners: bottom-left, bottom-right, top-right, top-left
        o, ex, ey = P[0], P[1] - P[0], P[3] - P[0]
        nrm = np.cross(ex, ey); nrm /= np.linalg.norm(nrm)
        pts = [k - fr.EYE for k in P]
        if any(t[0] <= 1 for t in pts): continue
        us = [fr.W / 2 + t[1] / t[0] * f for t in pts]; vs = [fr.H / 2 - t[2] / t[0] * f for t in pts]
        x0, x1 = max(int(min(us)) - 1, 0), min(int(max(us)) + 2, fr.W); y0, y1 = max(int(min(vs)) - 1, 0), min(int(max(vs)) + 2, fr.H)
        if x0 >= x1 or y0 >= y1: continue
        d = dirs[y0:y1, x0:x1]
        den = d @ nrm
        tt = ((o - fr.EYE) @ nrm) / np.where(np.abs(den) < 1e-9, 1e-9, den)
        hit = fr.EYE + d * tt[..., None] - o
        a = hit @ ex / ex.dot(ex); b = hit @ ey / ey.dot(ey)
        m = (a >= 0) & (a < 1) & (b >= 0) & (b < 1)
        if not m.any(): continue
        uv = np.array(q['uv'])
        u = uv[0, 0] + a * (uv[1, 0] - uv[0, 0]); v = uv[0, 1] + b * (uv[3, 1] - uv[0, 1])   # Blender V is up
        R, A = tif(q['tex'])
        px = np.clip((u * R.shape[1]).astype(int), 0, R.shape[1] - 1)
        py = np.clip(((1 - v) * R.shape[0]).astype(int), 0, R.shape[0] - 1)
        # Illum (Illum.cfx:953-1018): emittance x the emittance map's rgb x its alpha (:894), blended by alpha =
        # Opacity x the diffuse map's alpha (:858-860): dst x (1 - a) + src x a, in the cards' order
        a = (A[py, px] * OPACITY)[m][:, None]
        src = emit[q['mtl']][None, :] * R[py, px][m] * A[py, px][m][:, None]
        tile = img[y0:y1, x0:x1]
        tile[m] = tile[m] * (1 - a) + src * a


def main():
    D = json.load(open(CARDS))
    emit = emittance()
    cards = D['sets'][SET]
    tiles = []
    worst = 0.0
    for v, stages in enumerate(cards):
        rng = np.random.default_rng(100 + v)
        ps = fr.spawn_shot(rng, pop=(v % 2 == 0), system=SYSTEM)
        for k, quads in enumerate(stages):
            ref = np.zeros((fr.H, fr.W, 3)); ours = np.zeros_like(ref)
            sprites = [x for x in fr.step(ps, (k + 1) * fr.DT, rng) if x['kind'] in ('pop', 'pop_side', 'core', 'flame', 'longspark', 'corespark')]
            fr.draw(sprites, ref); draw_cards(quads, ours, emit)
            ours_img = np.clip(np.rint(SCENE.display(ours) * 255), 0, 255).astype(np.uint8)
            a, b = fr.tonemap(ref).astype(float), ours_img.astype(float)
            mask = (a.max(2) > 30) | (b.max(2) > 30)
            iou = ((a.max(2) > 30) & (b.max(2) > 30)).sum() / max(mask.sum(), 1)
            diff = np.abs(a - b)[mask].mean() if mask.any() else 0.0
            worst = max(worst, diff)
            print(f'variant {v + 1:2d} stage {k + 1}: shape overlap {iou:.3f}, mean colour difference {diff:5.1f} / 255')
            tiles.append((Image.fromarray(fr.tonemap(ref)), Image.fromarray(ours_img), v, k, iou, diff))
    w, h = fr.W // 2, fr.H // 2
    sheet = Image.new('RGB', (w * 4, h * len(tiles) // 2))
    dr = ImageDraw.Draw(sheet)
    for i, (r, o, v, k, iou, diff) in enumerate(tiles):
        x, y = (i % 2) * 2 * w, (i // 2) * h
        sheet.paste(r.resize((w, h)), (x, y)); sheet.paste(o.resize((w, h)), (x + w, y))
        dr.text((x + 4, y + 4), f'RoN v{v + 1} frame {k}', fill=(200, 200, 120))
        dr.text((x + w + 4, y + 4), f'cards  overlap {iou:.2f}  diff {diff:.0f}', fill=(120, 200, 200))
    sheet.save(OUT)
    print(f'{SET} ({SYSTEM}), KCD2 scene: {SCENE.name}')
    print('worst mean colour difference', round(worst, 1), '->', OUT)


if __name__ == '__main__':
    main()
