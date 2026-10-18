"""Ready or Not's .50 pistol muzzle flash (WeaponFlash_Pistol_V3), simulated the way RoN's own code runs it and drawn
frame by frame from the first-person eye: the reference our flash is checked against (Claude 1, 2026-10-18).

Every rule is from docs/ron/cascade_semantics.md (read from RoN's exe) and the raw module tables
(/mnt/e/Tools/deagle-work/ron_fx/_data/flash_collect.json, docs/ron/ron_flash_pistol_v3.md):
  - one update per game frame (dt = 1/60 s here), alive while age < lifetime, drawn at the end of the frame;
  - Op-2 ("uniform") values in an Update are drawn again every frame: sheet frames, the core's colour, the drag;
  - VelocityCone: pitch = trunc(U(0, 0.075) x 182.0378) whole degrees, azimuth random; speed U(500, 5000) cm/s;
  - Acceleration: one U(-3000, 3000) cm/s^2 per axis drawn at spawn; AccelerationDrag: v *= 1 - c dt, c = U(2.5, 15)
    drawn every frame; SizeScaleBySpeed: size.y = base.y x clamp(0.05 |v|, 1, 100), then SizeMultiplyLife on top;
  - StartSize is axis-locked (LockFlag 4) in every emitter: one draw for x, y and z; the core's colour is not
    locked (three independent draws), so a rare low red draw shows as a green or pink speck, as RoN's would;
  - the Pop's burst is 0-1 particles (CountLow 0, Count 1) and each one sends FlashCore: one side flame at its place;
  - square sprites face the eye and use size.x; velocity-aligned ones lie along the velocity, width x, length y.
Unverified (shader code, not read): what sheet frames 16-23 show on a 4x4 sheet (taken as frame mod 16 here), the
exact pivot maths (flash_bake's convention: centre moved -pivot/2 x length along the long axis), and RoN's bloom.

    python3 tools/ron/flash_ref.py <out dir> [shots] [seed]
    -> <out>/ref_shotNN_fK.png (frames K = 0..5 after the shot) and <out>/ref_sheet.png (shots x frames)
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw

PNG = '/mnt/e/Tools/deagle-work/ron_fx/flash'
DT = 1 / 60
W, H, VFOV = 640, 360, 55.0          # camera: first person, vertical field of view in degrees
EYE = np.array([-40.7, -5.7, 4.2])   # the eye from the muzzle in RoN's emitter axes (cm): +X down the barrel, +Y right,
                                     # +Z up (flash_bake's EYE, bone axes -> RoN axes)
EXPOSURE = 0.135                     # RoN's exposure, fitted to the user's clip (flash_bake.RON_EXPOSURE)
SHEETS = {'SMG': ('Rifle_MuzzleFlash_Macro_Rifle_-_Copy__2_', 4, 4), 'Pistol': ('MuzzleFlash_Macro_M2011', 4, 4),
          'Side': ('T_muzzle_flashes', 2, 2), 'Add': ('T_longSpark_02_8x2', 4, 2)}
_tex = {}


def tex(name):
    if name not in _tex:
        f, nx, ny = SHEETS[name]
        _tex[name] = (np.asarray(Image.open(f'{PNG}/{f}.png').convert('RGB')).astype(np.float32) / 255.0, nx, ny)
    return _tex[name]


def lerp_curve(vals, t):
    """a curve of n samples over life 0..1 (TimeScale = n - 1), linear, clamped"""
    vals = np.asarray(vals, float); n = len(vals)
    if n == 1: return vals[0]
    x = min(max(t, 0.0), 1.0) * (n - 1); i = min(int(x), n - 2); f = x - i
    return vals[i] * (1 - f) + vals[i + 1] * f


SPARK_LIFE_MULT = np.array([[1, .5, 0], [1, .56667, 0], [1, .63333, 0], [1, .7, 0], [1, .76667, 0], [1, .83333, 0],
                            [1, .9, 0], [1, .96667, 0], [1, .96667, 0], [1, .9, 0], [1, .83333, 0], [1, .76667, 0],
                            [1, .7, 0], [1, .63333, 0], [1, .56667, 0], [1, .5, 0]])


class P:   # one particle
    pass


def spawn_shot(rng, pop=None):
    """one shot's particles; pop forces the Pop's 0-1 burst (None = RoN's own coin flip)"""
    ps = []
    def add(**k):
        p = P(); p.__dict__.update(dict(age=0.0, birth=0.0, pos=np.zeros(3), vel=np.zeros(3), acc=np.zeros(3),
                                        rot=0.0, piv=0.0, drag=False, sbs=False, colour_rng=None, sub=None), **k)
        ps.append(p)
    # Pop, front (SMG sheet): 0-1, sends FlashCore
    if (rng.integers(0, 2) == 1) if pop is None else pop:
        s = rng.uniform(25, 35, 3)
        add(kind='pop', sheet='SMG', align='square', life=0.04, pos=np.array([25.0, 0, 0]), vel=np.array([250.0, 0, 0]),
            size=s, mult=[[1, 1, 1], [1.5, 1.5, 2]], colour=np.array([38250, 450, 200.0]), alpha=[1, 0],
            rot=rng.uniform(0, 25) * 360, sub=('rand', 0, 23))
        # Pop, side, from the event: at the pop's place, no inherited velocity; its own StartVelocity 5 cm/s
        add(kind='pop_side', sheet='Side', align='vel', life=0.04, pos=np.array([25.0, 0, 0]), vel=np.array([5.0, 0, 0]),
            size=np.full(3, rng.uniform(20, 30)), mult=[[0.12, 0.5, 1], [1, 2.5, 2]], colour=np.array([38250, 450, 200.0]),
            alpha=[1, 0], piv=-0.35, sub=('fixed', int(rng.integers(0, 4))))
    # Pop, pistol core: always 1
    add(kind='core', sheet='Pistol', align='square', life=0.035, pos=np.array([12.0, 0, 0]), vel=np.array([100.0, 0, 0]),
        size=np.full(3, rng.uniform(8, 12)), mult=[[1, 1, 1], [1.5, 1.5, 1.2]], colour=None,
        colour_rng=(np.array([0.85417, 0.07355, 0.0178]), np.array([38250, 450, 200.0])), alpha=[1, 0],
        rot=rng.uniform(0, 25) * 360, sub=('rand', 0, 11))
    # SmallFlame (Side sheet), velocity-aligned, base at the muzzle
    add(kind='flame', sheet='Side', align='vel', life=0.035, vel=np.array([25.0, 0, 0]), size=np.full(3, rng.uniform(15, 20)),
        mult=[[0.25, 0.5, 1], [0.75, 1.5, 2]], colour=np.array([382.5, 4.5, 2.0]), alpha=[1, 0], piv=-1.0,
        sub=('fixed', int(rng.integers(0, 4))))
    # born at 0.01 s: 10 Sparks, 1 LongSpark, 1 CoreSpark
    for _ in range(10):
        pitch = math.radians(int(rng.uniform(0, 0.075) * 182.0378)); az = math.radians(int(rng.uniform(0, 1) * 65533.625))
        d = np.array([math.cos(pitch), math.sin(pitch) * math.cos(az), math.sin(pitch) * math.sin(az)])
        v = d * rng.uniform(500, 5000) + np.array([rng.uniform(-50, 50), rng.uniform(-50, 50), 0.0])
        add(kind='spark', sheet='Add', align='vel', birth=0.01, life=rng.uniform(0.05, 0.085), vel=v,
            acc=rng.uniform(-3000, 3000, 3), size=np.full(3, rng.uniform(0.25, 0.75)), mult=SPARK_LIFE_MULT,
            colour=np.array([25, 0.27332, 0.1936]), alpha=[1.5, 0], drag=True, sbs=True, piv=0.15,
            pos=rng.uniform(-0.5, 0.5, 3), sub=('fixed', int(rng.integers(0, 8))))
    add(kind='longspark', sheet='Add', align='vel', birth=0.01, life=0.03, vel=np.array([-5.0, 0, 0]),
        size=np.full(3, rng.uniform(25, 50)), mult=[[0.1, 0.5, 1], [0.05, 1.25, 2]], colour=np.array([1528, 180, 8.0]),
        alpha=[1, 0], piv=0.5, sub=('rand', 0, 3))
    add(kind='corespark', sheet='Add', align='square', birth=0.01, life=0.03, pos=np.array([10.0, 0, 0]),
        size=np.full(3, rng.uniform(6, 12)), mult=[[0.5, 1.5, 1], [0.6, 1.8, 2]], colour=np.array([1528, 180, 8.0]),
        alpha=[1, 0], rot=rng.uniform(0, 1) * 360, piv=0.09, sub=('fixed', 7))
    return ps


def step(ps, t_end, rng):
    """one game frame ending at t_end: age, move, the every-frame modules; returns the particles alive to draw"""
    out = []
    for p in ps:
        if t_end <= p.birth: continue
        dt = min(DT, t_end - p.birth)
        p.age += dt
        if p.age >= p.life: continue
        if p.drag: p.vel = p.vel * (1 - rng.uniform(2.5, 15) * dt)
        p.vel = p.vel + p.acc * dt
        p.pos = p.pos + p.vel * dt
        r = p.age / p.life
        size = np.array(p.size, float)
        if p.sbs: size[1] = abs(size[1]) * min(max(0.05 * np.linalg.norm(p.vel), 1), 100)
        size = size * lerp_curve(p.mult, r)
        colour = p.colour if p.colour is not None else rng.uniform(p.colour_rng[0], p.colour_rng[1])
        a = lerp_curve(p.alpha, r)
        sub = p.sub[1] if p.sub[0] == 'fixed' else int(rng.uniform(p.sub[1], p.sub[2]))
        out.append(dict(kind=p.kind, sheet=p.sheet, align=p.align, pos=p.pos.copy(), vel=p.vel.copy(), size=size,
                        rgb=colour * a, rot=p.rot, piv=p.piv, sub=sub))
    return out


def camera():
    fwd = np.array([1.0, 0, 0]); f = (H / 2) / math.tan(math.radians(VFOV) / 2)
    return fwd, f


def frame_of(s):
    """a sprite's plane, facing EYE: centre, right, up (unit), width, height (cm, RoN axes), normal. The cards in the
    game are these same quads (tools/ron/flash_bake.py), so both renders share this geometry."""
    c = s['pos']; sx, sy = s['size'][0], s['size'][1]
    to_eye = EYE - c; to_eye /= np.linalg.norm(to_eye)
    if s['align'] == 'vel':
        up = s['vel'] / max(np.linalg.norm(s['vel']), 1e-6)
        nrm = to_eye - to_eye.dot(up) * up; nrm /= max(np.linalg.norm(nrm), 1e-9)
        right = np.cross(up, nrm); w, h = sx, sy
        c = c + up * sy * (-s['piv'] / 2)
    else:
        nrm = to_eye; a = math.radians(s['rot'])
        up0 = np.array([0, 0, 1.0]); up0 = up0 - up0.dot(nrm) * nrm; up0 /= np.linalg.norm(up0)
        r0 = np.cross(up0, nrm)
        up = up0 * math.cos(a) + r0 * math.sin(a); right = r0 * math.cos(a) - up0 * math.sin(a); w = h = sx
    return c, right, up, w, h, nrm


def draw(sprites, img):
    """additive sprites into an HDR image (linear RoN units), seen from EYE looking down +X"""
    fwd, f = camera()
    yy, xx = np.mgrid[0:H, 0:W]
    dirs = np.stack([np.full(xx.shape, f), (xx - W / 2), -(yy - H / 2)], -1).astype(np.float64)   # +Y right, +Z up
    for s in sprites:
        c, right, up, w, h, nrm = frame_of(s)
        # pixel rays hitting the sprite plane
        corners = [c + sgx * right * w / 2 + sgy * up * h / 2 for sgx in (-1, 1) for sgy in (-1, 1)]
        pts = [k - EYE for k in corners]
        if any(q[0] <= 1 for q in pts): continue
        us = [W / 2 + q[1] / q[0] * f for q in pts]; vs = [H / 2 - q[2] / q[0] * f for q in pts]
        x0, x1 = max(int(min(us)) - 1, 0), min(int(max(us)) + 2, W); y0, y1 = max(int(min(vs)) - 1, 0), min(int(max(vs)) + 2, H)
        if x0 >= x1 or y0 >= y1: continue
        d = dirs[y0:y1, x0:x1]
        denom = d @ nrm
        tt = ((c - EYE) @ nrm) / np.where(np.abs(denom) < 1e-9, 1e-9, denom)
        hit = EYE + d * tt[..., None] - c
        u = hit @ right / w + 0.5; v = 0.5 - hit @ up / h
        m = (u >= 0) & (u < 1) & (v >= 0) & (v < 1) & (tt > 0)
        if not m.any(): continue
        t_img, nx, ny = tex(s['sheet'])
        i = s['sub'] % (nx * ny); cx, cy = i % nx, i // nx
        th, tw = t_img.shape[0] // ny, t_img.shape[1] // nx
        px = np.clip((cx * tw + u * tw).astype(int), 0, t_img.shape[1] - 1)
        py = np.clip((cy * th + v * th).astype(int), 0, t_img.shape[0] - 1)
        texel = t_img[py, px]
        img[y0:y1, x0:x1][m] += texel[m] * s['rgb'][None, :]


def tonemap(img):
    x = img * EXPOSURE
    y = np.clip(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)
    return (np.power(y, 1 / 2.2) * 255).astype(np.uint8)


def run(out, shots=6, seed=1, frames=6):
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(seed)
    tiles = []
    for k in range(shots):
        ps = spawn_shot(rng); row = []
        for fr in range(frames):
            img = np.zeros((H, W, 3))
            draw(step(ps, (fr + 1) * DT, rng), img)
            im = Image.fromarray(tonemap(img)); im.save(f'{out}/ref_shot{k:02d}_f{fr}.png'); row.append(im)
        tiles.append(row)
    sheet = Image.new('RGB', (W * frames // 2, H * shots // 2))
    for r, row in enumerate(tiles):
        for c, im in enumerate(row):
            sheet.paste(im.resize((W // 2, H // 2)), (c * W // 2, r * H // 2))
    d = ImageDraw.Draw(sheet)
    for r in range(shots):
        for c in range(frames):
            d.text((c * W // 2 + 6, r * H // 2 + 4), f'shot {r + 1}  frame {c}  ({(c + 1) * 1000 * DT:.0f} ms)', fill=(200, 200, 120))
    for c in range(1, frames): d.line([(c * W // 2, 0), (c * W // 2, sheet.height)], fill=(60, 60, 60))
    for r in range(1, shots): d.line([(0, r * H // 2), (sheet.width, r * H // 2)], fill=(60, 60, 60))
    sheet.save(f'{out}/ref_sheet.png')
    return sheet


if __name__ == '__main__':
    run(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 6, int(sys.argv[3]) if len(sys.argv) > 3 else 1)
