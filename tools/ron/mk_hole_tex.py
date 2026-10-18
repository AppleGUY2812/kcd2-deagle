#!/usr/bin/env python3
"""Our bullet holes (hole_<surface>_diff / _ddna, KCD2 decals) baked from Ready or Not's ENTRY decals: the Decal of
each surface's ImpactEffects_Pistol row (its hitscan code plays that table on both faces of a wallbang). Private use
only (Ready or Not assets). The decal box is the whole texture (u = image x, v = image y down), as RoN's decal box.

Two master materials, read from their deferred-decal (DBuffer) pixel shaders and their preshaders
(tools/ron/ron_mat.py; parameter -> shader slot from the uniform expression set, not guessed):

M_BulletImpact_Master (M_BulletImpact_*): slots t1 = Texture (height), t2 = MaterialType (surface), t3 = TextureAlpha
  base colour = lerp(Colour_A, Colour_B, f), f = min((MaterialType.R x Multi) ^ Power, 1) (0 where R x Multi <= 0)
  opacity     = ceil(TextureAlpha.R)   (every texel above 0 is opaque)
  normal      = normalize(-dh/du x NI, -dh/dv x NI, 1), dh = Texture.R(uv + 0.005) - Texture.R(uv) (NI = NormalIntensity)
  roughness   = saturate(MaterialType.G x Roughness_Multi);  metallic = Metalness
Decal_Impact_MasterMaterial (M_BulletImpact_Metal / _Wood_01, Decal_Impact_Inst_*): uv' = (uv - 0.5) / DecalSize + 0.5
  base colour = Base.rgb (Base clamped: the hole sits in the middle DecalSize of the box), opacity = saturate(Base.a x
  AlphaPower), normal = the Normal map (wrapped), roughness = Roughness, metallic = Metal.

UE textures marked sRGB are linearised before use as heights / weights (the shader reads linear values); the KCD2
diffuse is written sRGB. Normals: UE tangent space (x = +u, y = +v down), green flipped for CryEngine (green up).
_ddna alpha = smoothness = 1 - roughness. Output: E:/Tools/deagle-work/fx_tif_impacts, compiled by
tools/fx_impact_textures.ps1. usage: mk_hole_tex.py [out_dir]"""
import json, os, sys
import numpy as np
from PIL import Image

SRC = '/mnt/e/Tools/deagle-work/ron_fx/decals'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/fx_tif_impacts'
N = 512

# RoN materials (MI parameters over the master's defaults, read by tools/ron/ron_mat.py) and which textures are sRGB
MATS = {
    'M_BulletImpact_Brick_01': dict(master='bullet', MaterialType='Decal_Broken_FloorConcrete_B', Texture='Bullet3', TextureAlpha='Bullet3',
                                    Multi=3.34705, Power=0.90825, NI=-5000.0, RM=4.98011, Metal=0.0,
                                    A=(0.916667, 0.871573, 0.845052), B=(0.25, 0.242289, 0.226562)),
    'M_BulletImpact_Drywall_01': dict(master='bullet', MaterialType='t_airfield_concrete_a', Texture='Bullet2', TextureAlpha='Bullet2',
                                      Multi=7.02245, Power=1.0, NI=-1550.0, RM=25.0, Metal=0.0,
                                      A=(0.114583, 0.114583, 0.114583), B=(0.328125, 0.311256, 0.276855)),
    'M_BulletImpact_Dirt': dict(master='bullet', MaterialType='Training_Landscape_Dirt_Albedo', Texture='Bullet2', TextureAlpha='Plaster_1',
                                Multi=3.5, Power=1.0, NI=-250.0, RM=25.0, Metal=0.0,
                                A=(0.291667, 0.204617, 0.153435), B=(0.0625, 0.047161, 0.040039)),
    'M_BulletImpact_Ceramic_01': dict(master='bullet', MaterialType='DecalImpact_Tile', Texture='Bullet8', TextureAlpha='Bullet8',
                                      Multi=2.99273, Power=1.82044, NI=-3000.0, RM=25.0, Metal=0.0,
                                      A=(0.177083, 0.177083, 0.177083), B=(0.088542, 0.087912, 0.087619)),
    'M_BulletImpact_Metal': dict(master='impact', Base='Metal_Soft_BO', Normal='Metal_Soft_N', DecalSize=0.27927, AlphaPower=26.51269,
                                 Roughness=0.45, Metal=1.0),
    'M_BulletImpact_Wood_01': dict(master='impact', Base='Wood_3_H', Normal='Wood_1_N', DecalSize=0.38996, AlphaPower=1.0,
                                   Roughness=1.0, Metal=0.0),
    'Decal_Impact_Inst_Fabric': dict(master='impact', Base='Fabric_BO', Normal='Cardboard_N', DecalSize=0.26667, AlphaPower=1.0,
                                     Roughness=0.9, Metal=0.0),
}
LINEAR = {'Metal_Soft_N', 'Wood_1_N', 'Cardboard_N', 'Wood_3_H'}   # SRGB = false in their Texture2D; all others sRGB
# our hole -> (RoN surface row of ImpactEffects_Pistol, its Decal). glass: RON_Glass_Plate's row has no decal (a DecalMesh)
HOLES = {'metal': ('RON_Steel', 'M_BulletImpact_Metal'), 'stone': ('RON_Rock', 'M_BulletImpact_Brick_01'),
         'brick': ('RON_Brick', 'M_BulletImpact_Brick_01'), 'plaster': ('RON_Plaster', 'M_BulletImpact_Drywall_01'),
         'wood': ('RON_Pine', 'M_BulletImpact_Wood_01'), 'wood_soft': ('RON_Pine', 'M_BulletImpact_Wood_01'),
         'soil': ('RON_Dirt', 'M_BulletImpact_Dirt'), 'cloth': ('RON_Fabric_Thin', 'Decal_Impact_Inst_Fabric'),
         'ceramic': ('RON_Ceramic', 'M_BulletImpact_Ceramic_01')}


def load(name):
    a = np.asarray(Image.open(os.path.join(SRC, name + '.png')).convert('RGBA')).astype(np.float64) / 255.0
    if name not in LINEAR:
        a = a.copy(); a[..., :3] = srgb_to_lin(a[..., :3])
    return a


def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def sample(tex, u, v, wrap=True):
    """bilinear; u, v in texture uv (v down); wrap or clamp"""
    h, w = tex.shape[:2]
    x = u * w - 0.5; y = v * h - 0.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    if wrap:
        x0 %= w; y0 %= h; x1 = (x0 + 1) % w; y1 = (y0 + 1) % h
    else:
        x1 = np.clip(x0 + 1, 0, w - 1); y1 = np.clip(y0 + 1, 0, h - 1); x0 = np.clip(x0, 0, w - 1); y0 = np.clip(y0, 0, h - 1)
    return tex[y0, x0] * (1 - fx) * (1 - fy) + tex[y0, x1] * fx * (1 - fy) + tex[y1, x0] * (1 - fx) * fy + tex[y1, x1] * fx * fy


def bake_bullet(m, res):
    mt, tx, ta = load(m['MaterialType']), load(m['Texture']), load(m['TextureAlpha'])
    v, u = (np.mgrid[0:res, 0:res] + 0.5) / res
    w = m['Multi'] * sample(mt, u, v)[..., 0]
    f = np.where(w > 0, np.minimum(np.power(np.maximum(w, 1e-12), m['Power']), 1.0), 0.0)
    A, B = np.array(m['A']), np.array(m['B'])
    col = A + (B - A) * f[..., None]
    alpha = np.ceil(np.clip(sample(ta, u, v, wrap=False)[..., 0], 0, 1) - 1e-6)   # ceil(R): any texel above 0
    h = sample(tx, u, v)[..., 0]
    dx = (sample(tx, u + 0.005, v)[..., 0] - h) * m['NI']
    dy = (sample(tx, u, v + 0.005)[..., 0] - h) * m['NI']
    n = np.stack([-dx, -dy, np.ones_like(dx)], -1)
    rough = np.clip(sample(mt, u, v)[..., 1] * m['RM'], 0, 1)
    return col, np.clip(alpha, 0, 1), n, rough


def bake_impact(m, res):
    base, nrm = load(m['Base']), load(m['Normal'])
    v, u = (np.mgrid[0:res, 0:res] + 0.5) / res
    uu = (u - 0.5) / m['DecalSize'] + 0.5; vv = (v - 0.5) / m['DecalSize'] + 0.5
    b = sample(base, uu, vv, wrap=False)
    inside = ((uu >= 0) & (uu <= 1) & (vv >= 0) & (vv <= 1))[..., None]   # clamped edge texels carry the edge's alpha
    col = b[..., :3]
    alpha = np.clip(b[..., 3] * m['AlphaPower'], 0, 1) * inside[..., 0]
    t = sample(nrm, uu, vv)
    n = np.stack([t[..., 0] * 2 - 1, t[..., 1] * 2 - 1, np.zeros(t.shape[:2])], -1)
    n[..., 2] = np.sqrt(np.clip(1 - n[..., 0] ** 2 - n[..., 1] ** 2, 0, 1))
    rough = np.full(alpha.shape, m['Roughness'])
    return col, alpha, n, rough


def save(name, px):
    Image.fromarray((np.clip(px, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(OUT, name + '.tif'), compression='tiff_lzw')


def main():
    made = {}
    for hole, (row, mat) in HOLES.items():
        m = MATS[mat]
        # 1024 for the shader maths (the 0.005 uv step is ~5 texels of RoN's 1024 maps), then down to N
        col, alpha, n, rough = (bake_bullet if m['master'] == 'bullet' else bake_impact)(m, 2 * N)
        d = np.concatenate([lin_to_srgb(col), alpha[..., None]], -1)
        d = d.reshape(N, 2, N, 2, 4).mean((1, 3))
        nn = n.reshape(N, 2, N, 2, 3).mean((1, 3)); nn /= np.linalg.norm(nn, axis=-1, keepdims=True)
        ddna = np.empty((N, N, 4)); ddna[..., :3] = nn * 0.5 + 0.5
        ddna[..., 1] = 1 - ddna[..., 1]                         # UE green-down -> CryEngine green-up
        ddna[..., 3] = 1 - rough.reshape(N, 2, N, 2).mean((1, 3))   # smoothness
        save('hole_%s_diff' % hole, d); save('hole_%s_ddna' % hole, ddna)
        made[hole] = {'row': row, 'material': mat, **{k: v for k, v in m.items()}, 'opaque_fraction': round(float((d[..., 3] > 0.5).mean()), 3)}
        print('%-9s %-18s %-28s opaque %.0f %%  smoothness %.2f' % (hole, row, mat, 100 * made[hole]['opaque_fraction'], ddna[..., 3][d[..., 3] > 0.5].mean() if (d[..., 3] > 0.5).any() else 0))
    made['glass'] = {'row': 'RON_Glass_Plate', 'material': None, 'note': 'no entry decal (the row has a DecalMesh): the old exit-table hole is kept'}
    json.dump(made, open(os.path.join(OUT, '_holes_ron_entry.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
