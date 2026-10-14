#!/usr/bin/env python3
"""Ready or Not's pistol water impact (P_BulletImpact_Water, docs/ron/ron_impact_fx.md "RON_Water") as KCD2 textures.
RoN's materials combine textures in their shaders; KCD2's particles take one texture with alpha (plus a normal map for
refraction), so each material is baked into what it draws:
  imp_ron_spray_diff   M_DropletMicro / M_DustPuff: T_FineParticle_DustAndDoplet, white, its alpha (the spray blob)
                       with the blue-channel specks (droplets) on top
  imp_ron_sheet_diff   M_AnimatedCaustics: T_Smoke01's shape (alpha) x a caustic pattern (T_Caustics_A red), white
  imp_ron_mist_diff    M_DustFine: T_GradientRound_A x T_Noise_RVB red, white
  imp_ron_ring_diff    M_WaterWave's ring mask: |T_Ring_HeightMap - 0.5| x 2, white
  ron_ring_ddn         M_WaterWave's T_Ring02_N (refraction ring), green flipped (UE -> CryEngine)
  ron_drop_ddn         M_RoundDistortion's T_SpericalNormal (refracting droplets), green flipped
Private use only (Ready or Not assets). Source PNGs: E:/Tools/deagle-work/ron_fx/particles (tools that exported them:
docs/ron/ron_impact_fx.md). Output: E:/Tools/deagle-work/fx_tif_impacts, compiled by tools/fx_impact_textures.ps1.
usage: mk_water_tex.py [src_dir] [out_dir]"""
import sys
import numpy as np
from PIL import Image

src = sys.argv[1] if len(sys.argv) > 1 else "/mnt/e/Tools/deagle-work/ron_fx/particles"
out = sys.argv[2] if len(sys.argv) > 2 else "/mnt/e/Tools/deagle-work/fx_tif_impacts"


def load(name, size=None):
    im = Image.open(f"{src}/{name}").convert("RGBA")
    if size and im.size != (size, size):
        im = im.resize((size, size), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def save_alpha(name, alpha, rgb=(1.0, 1.0, 1.0)):
    a = np.clip(alpha, 0, 1)
    h, w = a.shape
    px = np.empty((h, w, 4), np.float32)
    px[..., 0], px[..., 1], px[..., 2], px[..., 3] = rgb[0], rgb[1], rgb[2], a
    Image.fromarray((px * 255 + 0.5).astype(np.uint8), "RGBA").save(f"{out}/{name}.tif", compression="tiff_lzw")
    print(f"{name}.tif  {w}x{h}  alpha mean {a.mean():.3f} max {a.max():.3f}")


def save_normal(name, nrm):
    px = nrm.copy()
    px[..., 1] = 1.0 - px[..., 1]   # UE normal maps are green-down, CryEngine green-up
    px[..., 3] = 1.0
    Image.fromarray((px * 255 + 0.5).astype(np.uint8), "RGBA").convert("RGB").save(f"{out}/{name}.tif", compression="tiff_lzw")
    print(f"{name}.tif  {px.shape[1]}x{px.shape[0]}  normal map")


spray = load("T_FineParticle_DustAndDoplet.png")
save_alpha("imp_ron_spray_diff", np.maximum(spray[..., 3], spray[..., 2] * spray[..., 3] * 4))

smoke = load("T_Smoke01.png", 512)
caustic = load("T_Caustics_A.png", 512)
save_alpha("imp_ron_sheet_diff", smoke[..., 3] * (0.35 + 0.65 * caustic[..., 0]), rgb=(0.92, 0.96, 1.0))

grad = load("T_GradientRound_A.png")
noise = load("T_Noise_RVB.png")
save_alpha("imp_ron_mist_diff", grad[..., 0] * (0.4 + 0.6 * noise[..., 0]))

height = load("T_Ring_HeightMap.png")
save_alpha("imp_ron_ring_diff", np.abs(height[..., 0] - 0.5) * 2)

save_normal("ron_ring_ddn", load("T_Ring02_N.png"))
save_normal("ron_drop_ddn", load("T_SpericalNormal.png"))
