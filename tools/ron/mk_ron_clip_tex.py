#!/usr/bin/env python3
"""Ready or Not's eroding splash (MAT_BloodClip_Master, through MIC_BloodClip_A: NS_VFX_Impact_Mud's Splash emitter)
baked into one KCD2 flipbook: frames over the particle's life x variants of its random distortion.

The material (its Niagara-sprite pixel shader, mat_MAT_BloodClip_Master/ps_046.asm; constants named through the
material's preshader, which loads Clip, SmoothStepMin/Max, Darkness ... by index): per pixel, sprite uv in 0..1,
the particle's DynamicMaterialParameter (x DistortOffset, y DistortAmount, z ReflectionAmount, w ClipAmount):
    n    = 4 x T_Noise_N_02.rg(uv x 0.7 + x) - 3                     (normal unpack, then the graph's own x2-1)
    A    = 1 - |uv - 0.5| / 0.6;  R = 1 - exp(-(3A)^2) if A > 0 else 0   (RadialGradientExponential)
    uv'  = uv + n R y
    mask = T_Blood_Clip_A.g(uv')                                      (sRGB texture: linear in the shader)
    nrm  = T_Blood_Clip_N.rg(uv') x 2 - 1, z rebuilt; matcap uv = (nrm.x / 2 + 0.5, 0.5 - nrm.y / 2) in [0.02, 0.95]
    refl = (T_Blood_Reflection.g(matcap) x z) ^ ReflectionPower                       (0 where it is 0)
    E    = smoothstep(sat((mask + w - Clip) / (SmoothStepMax - SmoothStepMin)))
    base = sat(colour x lerp(Darkness, 1, mask) + refl x E)            lit, translucency volume (no normal used)
    alpha= E x [mask > 0] x particle alpha (OpacityMaskAdjust 1: mask / 0) x depth fade (2 cm)
MIC_BloodClip_A: Clip 1.11806, Darkness 0.1, SmoothStepMin 0.95, Max 1.0, ReflectionPower 2, Normal Strength 1
(base default), OffsetNormal None -> the base's T_Noise_N_02 (an instance's null texture falls back to the parent).
Constant emissive TestColor.rgb x ReflectionTest = (0.72, 0, 0.007) is left out: below KCD2's emissive scale
(nits / 1000, docs/ron/niagara_gpu.md).

The particle's own numbers come from running the emitter's shader (tools/ron/niagara_gpu.py, one seed for all frames):
w and z are functions of age alone; x and y are uniform per particle; colour is the system's User.Splash Color for
every particle, so it is baked in (KCD2 Color = white). Frames are taken at ages k / (F - 1): KCD2's AnimCycle=Once is
assumed to show frame (F - 1) x age (unverified). The matcap is baked for an unrotated sprite: in RoN the highlight
stays put on screen while the sprite turns; here it turns with the sprite.

usage: mk_ron_clip_tex.py [--frames 16] [--variants 4] [--tile 256] [--out DIR] [--sheet PNG]
Private use only (Ready or Not assets). Output: imp_ron_mudsplash_diff.tif for tools/fx_impact_textures.ps1.
"""
import argparse, json, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SRC = '/mnt/e/Tools/deagle-work/ron_fx/particles'
SYS = '/mnt/e/Tools/deagle-work/ron/work/ngpu/NS_VFX_Impact_Mud'
EMITTER = 'Splash_9'
CLIP, DARK, SS_MIN, SS_MAX, RPOW, NSTR = 1.11806, 0.1, 0.95, 1.0, 2.0, 1.0


def load(name):
    return np.asarray(Image.open(os.path.join(SRC, name + '.png')).convert('RGBA')).astype(np.float32) / 255.0


def srgb2lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin2srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def sample(tex, u, v, wrap=True):
    """bilinear; u, v in texture uv (0..1 = one tile)"""
    h, w = tex.shape[:2]
    x = u * w - 0.5; y = v * h - 0.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    if wrap:
        x0 %= w; y0 %= h; x1 = (x0 + 1) % w; y1 = (y0 + 1) % h
    else:
        x1 = np.clip(x0 + 1, 0, w - 1); y1 = np.clip(y0 + 1, 0, h - 1); x0 = np.clip(x0, 0, w - 1); y0 = np.clip(y0, 0, h - 1)
    return (tex[y0, x0] * (1 - fx) * (1 - fy) + tex[y0, x1] * fx * (1 - fy) + tex[y1, x0] * (1 - fx) * fy + tex[y1, x1] * fx * fy)


def measure(samples=64):
    """the emitter's own run: w(age) and z(age) tables, the x / y ranges, the colour"""
    import niagara_gpu as ng
    sysd = json.load(open(os.path.join(SYS, 'system.json')))
    E = ng.Emitter(sysd, EMITTER, SYS)
    run = E.run
    E.run = lambda ne, ns, fi, ii, dt, seed, rot=None, cap=None: run(ne, ns, fi, ii, dt, 12345, rot=rot, cap=cap)
    fr = E.simulate(0.6, samples, dt=1 / 240, count=3)
    age = np.concatenate([f['NormalizedAge'][:, 0] for f in fr if f['n']])
    dm = np.concatenate([f['DynamicMaterialParameter'] for f in fr if f['n']])
    col = fr[0]['Color'][:, :3]
    ts = np.linspace(0, 1, 41)
    def table(k):
        out = []
        for t in ts:
            m = np.abs(age - t) <= 0.0125
            out.append(dm[m, k].mean() if m.any() else np.interp(t, age, dm[:, k]))
        return np.array(out)
    x0 = fr[0]['DynamicMaterialParameter'][:, 0]; y0 = fr[0]['DynamicMaterialParameter'][:, 1]
    return {'ts': ts, 'w': table(3), 'z': table(2), 'x': (float(x0.min()), float(x0.max())), 'y': (float(y0.min()), float(y0.max())),
            'colour': col.mean(0), 'colour_spread': float(np.ptp(col, axis=0).max())}


class Clip:
    def __init__(self):
        self.mask = srgb2lin(load('T_Blood_Clip_A')[..., 1:2])[..., 0]
        self.nrm = load('T_Blood_Clip_N')[..., :2]
        self.noise = load('T_Noise_N_02')[..., :2]
        self.refl = srgb2lin(load('T_Blood_Reflection')[..., 1:2])[..., 0]

    def shade(self, u, v, x, y, z, w, colour):
        """the material at sprite uv (arrays) for one particle -> linear rgb, alpha (before particle alpha)"""
        n = 4 * sample(self.noise, u * 0.7 + x, v * 0.7 + x) - 3
        A = 1 - np.hypot(u - 0.5, v - 0.5) * (1 / 0.6)
        R = np.where((A >= 0) & (np.abs(A) > 1e-5), 1 - np.exp(-(3 * A) ** 2), 0.0)
        uu = u + n[..., 0] * R * y; vv = v + n[..., 1] * R * y
        mask = sample(self.mask[..., None], uu, vv)[..., 0]
        nr = sample(self.nrm, uu, vv) * 2 - 1
        mu = np.clip(nr[..., 0] * NSTR * 0.5 + 0.5, 0.02, 0.95); mv = np.clip(0.5 - nr[..., 1] * NSTR * 0.5, 0.02, 0.95)
        r = sample(self.refl[..., None], mu, mv)[..., 0] * z
        r = np.where(r > 0, np.maximum(r, 0) ** RPOW, 0.0)
        e = np.clip((mask + w - CLIP) / (SS_MAX - SS_MIN), 0, 1); e = e * e * (3 - 2 * e)
        base = np.clip(colour[None, None, :] * (DARK + (1 - DARK) * mask)[..., None] + (r * e)[..., None], 0, 1)
        alpha = e * (mask > 0)
        return base, alpha


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=16); ap.add_argument('--variants', type=int, default=4)
    ap.add_argument('--tile', type=int, default=256)
    ap.add_argument('--out', default='/mnt/e/Tools/deagle-work/fx_tif_impacts')
    ap.add_argument('--sheet', default='/mnt/c/Users/<user>/Desktop/Deagle Progress/mud_splash_ron_sheet.png')
    a = ap.parse_args()
    m = measure()
    print('w(age)', np.round(m['w'][::8], 3).tolist(), ' z(age)', np.round(m['z'][::8], 3).tolist())
    print('x %.3f..%.3f  y %.3f..%.3f  colour %s (spread %.4f)' % (*m['x'], *m['y'], np.round(m['colour'], 4).tolist(), m['colour_spread']))
    F, V, T = a.frames, a.variants, a.tile
    # KCD2 numbers tiles row by row and a variant is AnimFramesCount tiles in a row of that numbering, so the sheet
    # can be square (<= 2048 px: 4096 particle textures stream in late, MODLOG)
    per_row = int(2 ** np.ceil(np.log2(np.sqrt(V * F))))
    rows = int(np.ceil(V * F / per_row))
    if max(per_row, rows) * T > 2048: raise SystemExit('sheet over 2048 px: fewer frames / variants or a smaller tile')
    # variants: stratified x, Latin-square y (independent uniforms in RoN)
    q = (np.arange(V) + 0.5) / V
    xs = m['x'][0] + (m['x'][1] - m['x'][0]) * q
    ys = m['y'][0] + (m['y'][1] - m['y'][0]) * q[(np.arange(V) * 3 + 1) % V if V > 2 else np.arange(V)[::-1]]
    C = Clip()
    v, u = (np.mgrid[0:T, 0:T] + 0.5) / T
    sheet = np.zeros((rows * T, per_row * T, 4), np.float32)
    ages = np.linspace(0, 1, F)
    for vi in range(V):
        for k, age in enumerate(ages):
            w = float(np.interp(age, m['ts'], m['w'])); z = float(np.interp(age, m['ts'], m['z']))
            base, alpha = C.shade(u, v, xs[vi], ys[vi], z, w, m['colour'])
            idx = vi * F + k; r, c = divmod(idx, per_row)
            sheet[r * T:(r + 1) * T, c * T:(c + 1) * T, :3] = lin2srgb(base)
            sheet[r * T:(r + 1) * T, c * T:(c + 1) * T, 3] = alpha
    px = (np.clip(sheet, 0, 1) * 255 + 0.5).astype(np.uint8)
    os.makedirs(a.out, exist_ok=True)
    Image.fromarray(px, 'RGBA').save(os.path.join(a.out, 'imp_ron_mudsplash_diff.tif'), compression='tiff_lzw')
    print('imp_ron_mudsplash_diff.tif  %dx%d  TilesX=%d,TilesY=%d,VariantCount=%d,AnimFramesCount=%d  variants x %s y %s' % (
        px.shape[1], px.shape[0], per_row, rows, V, F, np.round(xs, 3).tolist(), np.round(ys, 3).tolist()))
    if a.sheet:
        contact(a.sheet, C, m, sheet, xs, ys, F, per_row, T)


def contact(path, C, m, sheet, xs, ys, F, per_row, T, ages=(0.0, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 1.0), S=160):
    """top: RoN's material run per pixel for 3 random particles (continuous x, y) at these ages; bottom: the flipbook's
    nearest variant, frames blended like AnimBlend; right: the whole sheet. Over mid grey, lighting = 1."""
    rng = np.random.default_rng(7)
    v, u = (np.mgrid[0:S, 0:S] + 0.5) / S
    bg = np.full((S, S, 3), 0.35, np.float32)
    def shrink(t):
        return np.asarray(Image.fromarray((np.clip(t, 0, 1) * 255).astype(np.uint8), 'RGBA').resize((S, S), Image.BILINEAR)).astype(np.float32) / 255
    def over(rgb_srgb, a):
        return rgb_srgb * a[..., None] + lin2srgb(bg) * (1 - a[..., None])
    rows = []
    for _ in range(3):
        x = rng.uniform(*m['x']); y = rng.uniform(*m['y'])
        vi = int(np.argmin((xs - x) ** 2 / (np.ptp(m['x']) ** 2) + (ys - y) ** 2 / (np.ptp(m['y']) ** 2)))
        top, bot = [], []
        for age in ages:
            w = float(np.interp(age, m['ts'], m['w'])); z = float(np.interp(age, m['ts'], m['z']))
            base, alpha = C.shade(u, v, x, y, z, w, m['colour'])
            top.append(over(lin2srgb(base), alpha))
            f = age * (F - 1); k0 = int(np.floor(f)); k1 = min(k0 + 1, F - 1); t = f - k0
            def tile(k):
                r, c = divmod(vi * F + k, per_row); return sheet[r * T:(r + 1) * T, c * T:(c + 1) * T]
            tl = shrink(tile(k0) * (1 - t) + tile(k1) * t)
            bot.append(over(tl[..., :3], tl[..., 3]))
        rows.append(np.concatenate(top, 1)); rows.append(np.concatenate(bot, 1))
        rows.append(np.full((8, S * len(ages), 3), 1.0, np.float32))
    left = np.concatenate(rows, 0)
    sh = sheet[..., :3] * sheet[..., 3:] + lin2srgb(np.full(3, 0.35)) * (1 - sheet[..., 3:])
    scale = left.shape[0] / sh.shape[0]
    shimg = Image.fromarray((np.clip(sh, 0, 1) * 255).astype(np.uint8)).resize((int(sh.shape[1] * scale), left.shape[0]))
    img = np.concatenate([(np.clip(left, 0, 1) * 255).astype(np.uint8), np.full((left.shape[0], 16, 3), 255, np.uint8), np.asarray(shimg)], 1)
    Image.fromarray(img).save(path)
    print('sheet', path, img.shape[1], 'x', img.shape[0], '(rows: RoN per pixel / flipbook, x3 particles; ages', ages, ')')


if __name__ == '__main__':
    main()
