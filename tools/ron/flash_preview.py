"""Preview of the baked Ready or Not flash cards (tools/ron/flash_bake.py) from the eye, behind and above the muzzle:
each card's quads drawn additively with their textures, hue and brightness, then a filmic curve. Shapes and placement,
not KCD2's exact exposure.

    python3 tools/ron/flash_preview.py <out.png> [set] [exposure]
"""
import json, sys
import numpy as np
from PIL import Image, ImageDraw

OUT = sys.argv[1]
SET = sys.argv[2] if len(sys.argv) > 2 else 'deagle_vm_flash_ron'
EXPO = float(sys.argv[3]) if len(sys.argv) > 3 else 0.004
D = json.load(open('/mnt/e/Tools/deagle-work/flash_ron/cards.json'))
TEXDIR = '/mnt/e/Tools/deagle-work/flash_ron/tif'
HUES = {'pop': (1.0, 0.0118, 0.0052), 'spark': (1.0, 0.118, 0.0052)}
W, H, F = 480, 320, 261.0            # px, focal length px: the game's vertical view, 1.101 rad
EYE = np.array([0.057, -0.407, -0.042])   # bone axes: +Y down the barrel, -Z up, -X right
tex = {}


def texture(name):
    if name not in tex:
        tex[name] = np.asarray(Image.open(f'{TEXDIR}/{name}_diff.tif').convert('RGB')).astype(np.float32) / 255.0
    return tex[name]


def project(p):   # camera looks down +Y from the eye; screen x = -X (right), screen y = +Z (down)
    d = np.asarray(p) - EYE
    return np.array([W / 2 + F * (-d[0]) / d[1], H / 2 + F * d[2] / d[1]])


def draw(img, q):
    P = [project(c) for c in q['p']]
    o, ax, ay = P[0], P[1] - P[0], P[3] - P[0]              # screen parallelogram (affine is enough at this size)
    M = np.array([ax, ay]).T
    if abs(np.linalg.det(M)) < 1e-6: return
    Mi = np.linalg.inv(M)
    xs, ys = [p[0] for p in P], [p[1] for p in P]
    x0, x1 = max(0, int(min(xs))), min(W, int(max(xs)) + 1); y0, y1 = max(0, int(min(ys))), min(H, int(max(ys)) + 1)
    if x0 >= x1 or y0 >= y1: return
    gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
    st = np.einsum('ij,jkl->ikl', Mi, np.stack([gx - o[0], gy - o[1]]))
    s, t = st[0], st[1]; inside = (s >= 0) & (s <= 1) & (t >= 0) & (t <= 1)
    uv = np.array(q['uv'])
    u = uv[0][0] + s * (uv[1][0] - uv[0][0]); v = uv[0][1] + t * (uv[3][1] - uv[0][1])
    T = texture(q['tex']); th, tw = T.shape[:2]
    px = np.clip((u * tw).astype(int), 0, tw - 1); py = np.clip(((1 - v) * th).astype(int), 0, th - 1)
    col = T[py, px] * np.array(HUES[q['hue']]) * (2.0 ** q['level'])
    img[y0:y1, x0:x1] += col * inside[..., None]


def render(quads):
    img = np.zeros((H, W, 3), np.float32)
    for q in quads: draw(img, q)
    return (1 - np.exp(-img * EXPO)) ** (1 / 2.2)


variants = D['sets'][SET]
rows = []
for v, stages in enumerate(variants[:2], 1):
    row = []
    for s in range(6):
        im = Image.fromarray((render(stages[s]) * 255).astype(np.uint8))
        ms = (s + 0.5) * D['time'] / D['stages'] * 1000
        ImageDraw.Draw(im).text((6, 6), f'variant {v}, {ms:.0f} ms', fill=(255, 255, 0))
        row.append(im)
    rows.append(row)
sheet = Image.new('RGB', (W * 6, H * len(rows)))
for r, row in enumerate(rows):
    for c, im in enumerate(row): sheet.paste(im, (c * W, r * H))
sheet.save(OUT); print(OUT)
