#!/usr/bin/env python3
"""Ready or Not's pistol water hit (P_BulletImpact_Water), its 3 switched-on layers, baked from the materials' own
pixel shaders (decoded from RoN's SM5 shader archive, scratchpad ngpu/matps.py):
  imp_ron_wdrop_diff   M_DropletMicro: opacity = saturate(2 x R x A) of T_FineParticle_DustAndDoplet, colour white
  imp_ron_wpuff_diff   M_DustPuff: opacity = A^3 of the same texture (x a 30 cm depth fade = SoftParticle), white
  imp_ron_wcaus_diff   M_AnimatedCaustics, 4x4 flipbook over the particle's life (RoN animates it by world time;
                       0.625 s = its mean life spread over the 16 frames):
                         c = Caustics.R(uv + T(-0.25, 1)) + Caustics.G(uv + T(0, 0.5)) + Caustics.B(uv + T(0.2, 0.5))
                         opacity = saturate(4 (Cloud.R(rot(uv, T/2) + T(0.5, 1)) + Cloud.G(uv + T(1.5, 0.5)))
                                   x Smoke01.A(uv) x c x Smoke01.A(c, c))
                         colour = lerp(c, <material colour>, k): the material constants aren't in the export, so c
                         (clamped to 1) is used
All samplers wrap, bilinear. Output: PNG previews + TIF (LZW) like tools/ron/mk_ron_fx_tex.py, for
tools/fx_impact_textures.ps1 (imp_*_diff -> AlbedoWithGenericAlpha).
usage: mk_water2_tex.py <out_dir>"""
import os, sys
import numpy as np
from PIL import Image

SRC = '/mnt/e/Tools/deagle-work/ron_fx/particles'
out = sys.argv[1] if len(sys.argv) > 1 else '.'


def load(name):
    return np.asarray(Image.open(os.path.join(SRC, name + '.png')).convert('RGBA')).astype(np.float32) / 255.0


def sample(tex, u, v):
    """bilinear, wrap; u, v arrays in texture uv (0..1 = one tile), v down like UE"""
    h, w = tex.shape[:2]
    x = u * w - 0.5; y = v * h - 0.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    x0 %= w; y0 %= h; x1 = (x0 + 1) % w; y1 = (y0 + 1) % h
    return (tex[y0, x0] * (1 - fx) * (1 - fy) + tex[y0, x1] * fx * (1 - fy) + tex[y1, x0] * (1 - fx) * fy + tex[y1, x1] * fx * fy)


def save(name, rgba):
    px = (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8)
    Image.fromarray(px, 'RGBA').save(os.path.join(out, name + '.png'))
    Image.fromarray(px, 'RGBA').save(os.path.join(out, name + '.tif'), compression='tiff_lzw')
    a = rgba[..., 3]
    print('%s  %dx%d  alpha mean %.3f max %.3f' % (name, px.shape[1], px.shape[0], a.mean(), a.max()))


def white(alpha):
    px = np.ones(alpha.shape + (4,), np.float32); px[..., 3] = alpha
    return px


dd = load('T_FineParticle_DustAndDoplet')
save('imp_ron_wdrop_diff', white(np.clip(2 * dd[..., 0] * dd[..., 3], 0, 1)))
save('imp_ron_wpuff_diff', white(dd[..., 3] ** 3))

caus = load('T_Caustics_A'); cloud = load('T_Cloud_ABC'); smoke = load('T_Smoke01')
N = 256; LIFE = 0.625
v, u = (np.mgrid[0:N, 0:N] + 0.5) / N
sheet = np.zeros((4 * N, 4 * N, 4), np.float32)
for f in range(16):
    T = f / 15.0 * LIFE
    c = (sample(caus, u - 0.25 * T, v + 1.0 * T)[..., 0] + sample(caus, u, v + 0.5 * T)[..., 1]
         + sample(caus, u + 0.2 * T, v + 0.5 * T)[..., 2])
    ang = 0.5 * T; du, dv = u - 0.5, v - 0.5
    ru = np.cos(ang) * du - np.sin(ang) * dv + 0.5 * T + 0.5
    rv = np.sin(ang) * du + np.cos(ang) * dv + 1.0 * T + 0.5
    cl = sample(cloud, ru, rv)[..., 0] + sample(cloud, u + 1.5 * T, v + 0.5 * T)[..., 1]
    op = np.clip(4 * cl * sample(smoke, u, v)[..., 3] * c * sample(smoke, c, c)[..., 3], 0, 1)
    px = np.empty((N, N, 4), np.float32); px[..., :3] = np.clip(c, 0, 1)[..., None]; px[..., 3] = op
    r, k = divmod(f, 4)
    sheet[r * N:(r + 1) * N, k * N:(k + 1) * N] = px
save('imp_ron_wcaus_diff', sheet)
