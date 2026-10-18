"""Checks the baked flash cards (cards.json from tools/ron/flash_bake.py) against the reference they came from
(tools/ron/flash_ref.py): every variant and stage is drawn twice from the same eye, once as RoN's sprites and once as
the cards (their quads, UVs, colours and brightness levels), and compared. A wrong axis, UV flip or colour shows here
before the game sees it. Owner: Claude 1.

    python3 tools/ron/flash_check.py [cards.json] [out.png]
"""
import json, math, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, __import__('os').path.dirname(__file__))
import flash_ref as fr

class fb:   # flash_bake's texture names (importing flash_bake runs the bake)
    SHEETS = {'MI_MuzzleFlash_SMG': 'Rifle_MuzzleFlash_Macro_Rifle_-_Copy__2_', 'MI_MuzzleFlash_Pistol': 'MuzzleFlash_Macro_M2011',
              'MI_MuzzleFlash_Side': 'T_muzzle_flashes', 'M_Particle_Additive': 'T_longSpark_02_8x2'}
    TEX = {m: 'ron_flash_' + t.split('_-_')[0].lower() for m, t in SHEETS.items()}

CARDS = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/flash_ron/cards.json'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/mnt/e/Tools/deagle-work/flash_ron/check.png'


def from_bone(b):   # flash_bake.to_bone inverted: bone (m) -> RoN axes (cm)
    return np.array([b[1], -b[0], -b[2]]) * 100.0


def draw_cards(quads, img):
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
        sheet = [k for k, t in fb.TEX.items() if t == q['tex']][0]
        t_img, _, _ = fr.tex({'MI_MuzzleFlash_SMG': 'SMG', 'MI_MuzzleFlash_Pistol': 'Pistol', 'MI_MuzzleFlash_Side': 'Side',
                              'M_Particle_Additive': 'Add'}[sheet])
        px = np.clip((u * t_img.shape[1]).astype(int), 0, t_img.shape[1] - 1)
        py = np.clip(((1 - v) * t_img.shape[0]).astype(int), 0, t_img.shape[0] - 1)
        # the card's light: its hue at 2^level (the card's sub-material). The colour column is only indicative: in the
        # game KCD2's own tonemapper draws it, here RoN's curve does; the shape overlap is the real check
        col = np.array(q['col']); lum = 2.0 ** q['level']
        img[y0:y1, x0:x1][m] += t_img[py, px][m] * (col * lum)[None, :]


def main():
    D = json.load(open(CARDS))
    cards = D['sets']['deagle_vm_flash_ron']
    tiles = []
    worst = 0.0
    for v, stages in enumerate(cards):
        rng = np.random.default_rng(100 + v)
        ps = fr.spawn_shot(rng, pop=(v % 2 == 0))
        for k, quads in enumerate(stages):
            ref = np.zeros((fr.H, fr.W, 3)); ours = np.zeros_like(ref)
            sprites = [x for x in fr.step(ps, (k + 1) * fr.DT, rng) if x['kind'] in ('pop', 'pop_side', 'core', 'flame')]
            fr.draw(sprites, ref); draw_cards(quads, ours)
            a, b = fr.tonemap(ref).astype(float), fr.tonemap(ours).astype(float)
            mask = (a.max(2) > 30) | (b.max(2) > 30)
            iou = ((a.max(2) > 30) & (b.max(2) > 30)).sum() / max(mask.sum(), 1)
            diff = np.abs(a - b)[mask].mean() if mask.any() else 0.0
            worst = max(worst, diff)
            print(f'variant {v + 1:2d} stage {k + 1}: shape overlap {iou:.3f}, mean colour difference {diff:5.1f} / 255')
            tiles.append((Image.fromarray(fr.tonemap(ref)), Image.fromarray(fr.tonemap(ours)), v, k, iou, diff))
    w, h = fr.W // 2, fr.H // 2
    sheet = Image.new('RGB', (w * 4, h * len(tiles) // 2))
    dr = ImageDraw.Draw(sheet)
    for i, (r, o, v, k, iou, diff) in enumerate(tiles):
        x, y = (i % 2) * 2 * w, (i // 2) * h
        sheet.paste(r.resize((w, h)), (x, y)); sheet.paste(o.resize((w, h)), (x + w, y))
        dr.text((x + 4, y + 4), f'RoN v{v + 1} frame {k}', fill=(200, 200, 120))
        dr.text((x + w + 4, y + 4), f'cards  overlap {iou:.2f}  diff {diff:.0f}', fill=(120, 200, 200))
    sheet.save(OUT)
    print('worst mean colour difference', round(worst, 1), '->', OUT)


if __name__ == '__main__':
    main()
