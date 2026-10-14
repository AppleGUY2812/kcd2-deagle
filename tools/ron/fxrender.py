"""Draws particle frames (Ready or Not's, from tools/ron/niagara_gpu.py sim) the way Unreal's sprite renderer would:
camera-facing / velocity-aligned quads, SubUV flipbooks, additive (emissive x opacity) or translucent blending, HDR
then exposure and a filmic curve. A grey wall at x = 0 faces the camera (Unreal axes, cm, Z up); particles behind it
are hidden like the depth test would.

    python3 tools/ron/fxrender.py <sim dir> <out.png> [exposure] [normal: wall|floor]
    -> a contact sheet: one burst at 0, 17, 33, 50, 83, 133, 200, 300, 500, 800 ms

Material shading is a stand-in (M_ParticleGeneric is compiled): emissive = texture x colour x Emissive Intensity, x
opacity for additive; translucent dust gets an ambient light; the metal shards a flat key light.
"""
import json, os, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
TEX = '/mnt/e/Tools/deagle-work/ron_fx/particles'
FX = '/mnt/e/Tools/deagle-work/ron_fx/_data/fx_collect.json'
W, H = 480, 320
_tex = {}


def texture(name):
    if name not in _tex:
        p = os.path.join(TEX, name + '.png')
        im = Image.open(p).convert('RGBA') if os.path.exists(p) else Image.new('RGBA', (8, 8), (255, 255, 255, 255))
        _tex[name] = np.asarray(im, np.float32) / 255.0
    return _tex[name]


def material_info(mat):
    d = json.load(open(FX))['particle_materials']
    m = next((v for k, v in d.items() if k.split('/')[-1] == mat), None) or {}
    tex = (m.get('Texture') or {}).get('Main Texture') or (m.get('Texture') or {}).get('Base') or (m.get('textures') or [''])[0]
    blend = (m.get('BasePropertyOverrides') or {}).get('BlendMode', 'BLEND_Translucent')
    ei = (m.get('Scalar') or {}).get('Emissive Intensity', 1.0)
    lit = mat in ('M_MetalShards', 'Shards_Mat', 'DebrisMaster', 'M_Glass_Chunks', 'PaperBits', 'M_WoodShards')
    return {'tex': tex.split('.')[-1] if tex else '', 'blend': blend, 'ei': ei, 'lit': lit}


class Camera:
    def __init__(self, pos, target, fov=70.0):
        self.pos = np.array(pos, float); f = np.array(target, float) - self.pos; self.fwd = f / np.linalg.norm(f)
        self.right = np.cross(self.fwd, [0, 0, 1.0]); self.right /= np.linalg.norm(self.right)
        self.up = np.cross(self.right, self.fwd)
        self.focal = (W / 2) / np.tan(np.radians(fov / 2))

    def project(self, p):
        d = p - self.pos; z = d @ self.fwd
        return np.stack([W / 2 + (d @ self.right) * self.focal / z, H / 2 - (d @ self.up) * self.focal / z], -1), z


def draw_sprite(img, cam, center, right, up, uv0, uv1, tex, rgb, a, blend, mask_g=False):
    """quad center +- right +- up; uv rectangle uv0..uv1 of tex; rgb (HDR) and opacity a."""
    corners = np.array([center - right - up, center + right - up, center + right + up, center - right + up])
    sc, z = cam.project(corners)
    if (z <= 1).any(): return
    x0, y0 = np.floor(sc.min(0)).astype(int); x1, y1 = np.ceil(sc.max(0)).astype(int)
    x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W - 1), min(y1, H - 1)
    if x1 < x0 or y1 < y0: return
    # inverse affine map from screen to the quad's (s, t) in [0,1]^2 (affine is fine for small sprites)
    o = sc[0]; ex = sc[1] - sc[0]; ey = sc[3] - sc[0]
    M = np.array([ex, ey]).T
    if abs(np.linalg.det(M)) < 1e-6: return
    Mi = np.linalg.inv(M)
    ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
    pts = np.stack([xs + 0.5 - o[0], ys + 0.5 - o[1]], -1) @ Mi.T
    s, t = pts[..., 0], pts[..., 1]
    inside = (s >= 0) & (s <= 1) & (t >= 0) & (t <= 1)
    if not inside.any(): return
    th, tw = tex.shape[:2]
    u = uv0[0] + s * (uv1[0] - uv0[0]); v = uv0[1] + (1 - t) * (uv1[1] - uv0[1])
    ti = np.clip((v * th).astype(int), 0, th - 1); tj = np.clip((u * tw).astype(int), 0, tw - 1)
    texel = tex[ti, tj]
    alpha = (texel[..., 1] if mask_g else texel[..., 3]) * a * inside
    region = img[y0:y1 + 1, x0:x1 + 1]
    col = (texel[..., 2:3] * 1.5 if mask_g else texel[..., :3]) * rgb  # shards: B = height as a shade
    if blend == 'BLEND_Additive':
        region += col * alpha[..., None]
    else:
        region[:] = region * (1 - alpha[..., None]) + col * alpha[..., None]


def tonemap(hdr, exposure):
    x = hdr * exposure
    y = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)  # ACES fit (Narkowicz)
    y = np.clip(y, 0, 1)
    return np.where(y <= 0.0031308, 12.92 * y, 1.055 * np.power(y, 1 / 2.4) - 0.055)


def render_frame(sysd, frames_by_em, ti, cam, exposure, pick, wall_lum):
    img = np.zeros((H, W, 3), np.float32)
    # the wall (x = 0 plane facing +X): lit grey with a little falloff
    ys, xs = np.mgrid[0:H, 0:W]
    img[:] = wall_lum / exposure * (0.9 + 0.1 * (1 - ys / H))[..., None]
    sprites = []
    for em, frames in frames_by_em.items():
        if ti >= len(frames): continue
        fr = frames[ti]
        if not fr['n']: continue
        E = sysd['emitters'][em]; rp = E.get('renderer', {})
        mi = material_info(rp.get('Material', ''))
        tex = texture(mi['tex']) if mi['tex'] else texture('')
        cols, rows = (rp.get('SubImageSize') or [1, 1])
        cols, rows = int(cols), int(rows)
        ids = fr['UniqueID'][:, 0] if 'UniqueID' in fr else np.arange(fr['n'])
        sel = pick[em](ids)
        for k in np.nonzero(sel)[0]:
            p = fr['Position'][k]
            if p[0] < 0: continue  # behind the wall
            size = fr['SpriteSize'][k] if 'SpriteSize' in fr else np.array([10, 10.])
            c = fr['Color'][k] if 'Color' in fr else np.array([1, 1, 1, 1.])
            rgb = c[:3] * mi['ei']; a = float(np.clip(c[3], 0, 1))
            if mi['lit']: rgb = c[:3] * 1.6 * wall_lum / exposure / 0.18; a = 1.0
            elif mi['blend'] != 'BLEND_Additive': rgb = c[:3] * wall_lum / exposure / 0.18 * 0.9
            align = rp.get('Alignment'); facing = cam.pos - p; facing /= np.linalg.norm(facing)
            if align == 'VelocityAligned' and 'Velocity' in fr and np.linalg.norm(fr['Velocity'][k]) > 1e-3:
                vel = fr['Velocity'][k]; up = vel - facing * (vel @ facing)
                if np.linalg.norm(up) < 1e-6: up = cam.up
                up = up / np.linalg.norm(up); right = np.cross(facing, up); right /= np.linalg.norm(right)
            else:
                rot = np.radians(fr['SpriteRotation'][k][0]) if 'SpriteRotation' in fr else 0.0
                right = cam.right * np.cos(rot) + cam.up * np.sin(rot); up = np.cross(facing, right) * -1
                up = -np.cross(right, facing) if True else up
            idx = fr['SubImageIndex'][k][0] if 'SubImageIndex' in fr else 0.0
            fi = int(np.clip(np.floor(idx), 0, cols * rows - 1))
            u0 = (fi % cols) / cols; v0 = (fi // cols) / rows
            depth = (p - cam.pos) @ cam.fwd
            sprites.append((depth, p, right * size[0] / 2, up * size[1] / 2, (u0, v0), (u0 + 1 / cols, v0 + 1 / rows), tex, rgb, a, mi['blend'] if not mi['lit'] else 'BLEND_Translucent', mi['lit']))
    sprites.sort(key=lambda s: -s[0])
    for s in sprites:
        draw_sprite(img, cam, s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10])
    return tonemap(img, exposure)


def burst_picker(sysd, sims, burst=0, seed=1):
    rng = np.random.default_rng(seed); pick = {}
    for em in sims:
        b = sysd.get('bursts', {}).get(em.rsplit('_', 1)[0]) or {'min': 1, 'max': 1}
        lo, hi = max(b['min'] or 1, 1), max(b['max'] or 1, 1)
        n = int(rng.integers(lo, hi + 1)); per = sims[em][0]['n'] // max(1, sims[em][0]['n'] // max(hi, 1)) if sims[em][0]['n'] else hi
        start = burst * hi
        pick[em] = (lambda ids, s=start, n=n: (ids >= s) & (ids < s + n))
    return pick


def sheet(sim_dir, out, exposure=1 / 300.0, times=(0, 1, 2, 3, 5, 8, 12, 18, 30), title=None, skip=('DEBRIS_LOW',), bursts=(0, 1, 2)):
    sysd = json.load(open(os.path.join(sim_dir, 'system.json')))
    sims = np.load(os.path.join(sim_dir, 'sim.npy'), allow_pickle=True)[0]
    sims = {k: v for k, v in sims.items() if not any(s in k for s in skip)}
    cam = Camera([150, -50, 15], [0, 0, 5], 70)
    rows = []
    for b in bursts:
        pick = burst_picker(sysd, sims, b, seed=b + 1)
        row = []
        for ti in times:
            im = (render_frame(sysd, sims, ti, cam, exposure, pick, 0.18) * 255).astype(np.uint8)
            im = Image.fromarray(im); dr = ImageDraw.Draw(im)
            dr.text((6, 4), '%d ms' % round(ti * 1000 / 60), fill=(255, 255, 0))
            row.append(im)
        rows.append(row)
    cw, ch = W * 2 // 3, H * 2 // 3
    S = Image.new('RGB', (cw * len(times), ch * len(rows) + 24), (20, 20, 20))
    for r, row in enumerate(rows):
        for c, im in enumerate(row): S.paste(im.resize((cw, ch), Image.LANCZOS), (c * cw, 24 + r * ch))
    ImageDraw.Draw(S).text((6, 6), title or sysd['name'], fill=(255, 255, 255))
    S.save(out)
    return out


if __name__ == '__main__':
    sheet(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 1 / 300.0)
