#!/usr/bin/env python3
"""Ready or Not's impact effects ported 1:1, their textures for KCD2: the pistol water hit (P_BulletImpact_Water,
docs/ron/ron_impact_fx.md "RON_Water") and the metal hit (P_Bullet_Impact_Metal_Big, "RON_Aluminium").
RoN's materials combine textures in their shaders; KCD2's particles take one texture with alpha (plus a normal map for
refraction), so each material is baked into what it draws:
  imp_ron_spray_diff   M_DropletMicro / M_DustPuff: T_FineParticle_DustAndDoplet, white, its alpha (the spray blob)
                       with the blue-channel specks (droplets) on top
  imp_ron_sheet_diff   M_AnimatedCaustics: T_Smoke01's shape (alpha) x a caustic pattern (T_Caustics_A red), white
  imp_ron_mist_diff    M_DustFine: T_GradientRound_A x T_Noise_RVB red, white
  imp_ron_ring_diff    M_WaterWave's ring mask: |T_Ring_HeightMap - 0.5| x 2, white
  ron_ring_ddn         M_WaterWave's T_Ring02_N (refraction ring), green flipped (UE -> CryEngine)
  ron_drop_ddn         M_RoundDistortion's T_SpericalNormal (refracting droplets), green flipped
  imp_ron_sparks01_diff  MI_Sparks_01: T_VFX_Sparks_01_B, 4x4 spark bursts, additive (opaque, black = nothing)
  imp_ron_sparks03_diff  MI_Sparks_02: T_VFX_Sparks_03_B, 2x2 spark bursts, additive
  imp_ron_puff_diff      MI_Muzzle_Puff_Front: T_muzzlePuff_Front_01, 8x4 dust puffs, alpha
  imp_ron_shards_diff    M_MetalShards: T_WoodShard_OAH (R occlusion, G alpha, B height), 2x2, grey x occlusion
Private use only (Ready or Not assets). Source PNGs: E:/Tools/deagle-work/ron_fx/particles (tools that exported them:
docs/ron/ron_impact_fx.md). Output: E:/Tools/deagle-work/fx_tif_impacts, compiled by tools/fx_impact_textures.ps1.
usage: mk_ron_fx_tex.py [src_dir] [out_dir]"""
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


def save_rgba(name, px):
    Image.fromarray((np.clip(px, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(f"{out}/{name}.tif", compression="tiff_lzw")
    print(f"{name}.tif  {px.shape[1]}x{px.shape[0]}")


def additive(name, png):   # DXT1 sprite sheets on black: colour as is, opaque (additive blending ignores alpha)
    px = load(png)
    px[..., 3] = 1.0
    save_rgba(name, px)


additive("imp_ron_sparks01_diff", "T_VFX_Sparks_01_B.png")
additive("imp_ron_sparks03_diff", "T_VFX_Sparks_03_B.png")
save_rgba("imp_ron_puff_diff", load("T_muzzlePuff_Front_01.png"))
shard = load("T_WoodShard_OAH.png")
px = np.empty_like(shard)
px[..., 0] = px[..., 1] = px[..., 2] = 0.35 + 0.65 * shard[..., 0]
px[..., 3] = shard[..., 1]
save_rgba("imp_ron_shards_diff", px)


# Ready or Not's Niagara impacts (tools/ron/ns2pfx.py), masked materials: RoN clips where texture alpha x particle
# alpha < 0.3333; alpha here is that clip's threshold (clip()); colour as each pixel shader computes it (tools/ron/ron_mat.py)
def clip(a):
    """RoN's masked clip, kept as a threshold KCD2's AlphaClip can move with particle alpha (tools/ron/ns2pfx.py):
    0.004 + 0.996 (1 - 1/(3a)) where a >= 1/3, else 0"""
    s = np.where(a >= 1 / 3, 0.004 + 0.996 * (1 - 1 / (3 * np.maximum(a, 1e-6))), 0.0)
    return s.astype(np.float32)


#   imp_ron_grain_diff  M_SandSplashSmall: colour T_Cloud_ABC.R, clip T_Bullet_ConcreteImpact_BaseHRA.A. Which slot is
#                       which texture is inferred (the other way round draws square grains), not read
cloud = load("T_Cloud_ABC.png", 256)
hra = load("T_Bullet_ConcreteImpact_BaseHRA.png", 256)
px = np.empty_like(hra)
px[..., 0] = px[..., 1] = px[..., 2] = cloud[..., 0]
px[..., 3] = clip(hra[..., 3])
save_rgba("imp_ron_grain_diff", px)
#   imp_ron_grass_diff  M_GrassBlade: colour T_FX_GrassBlade.rgb, clip its alpha (2x2 blades)
g = load("T_FX_GrassBlade.png", 512)
g[..., 3] = clip(g[..., 3])
save_rgba("imp_ron_grass_diff", g)
#   imp_ron_woodshard_diff  Shards_Mat: colour Shards.R (grey), clip Shards.A (2x2)
sh = load("Shards.png", 256)
px = np.empty_like(sh)
px[..., 0] = px[..., 1] = px[..., 2] = sh[..., 0]
px[..., 3] = clip(sh[..., 3])
save_rgba("imp_ron_woodshard_diff", px)
#   imp_ron_sandblocks_diff  M_SandSplash: T_SandBlocks.rgb, clip its alpha (2x2)
sb = load("T_SandBlocks.png", 512)
sb[..., 3] = clip(sb[..., 3])
save_rgba("imp_ron_sandblocks_diff", sb)
#   imp_ron_glowdot_diff  M_ParticleEmberSimple: lit, colour T_Glow_A.rgb x particle colour, opacity T_Glow_A.R
gl = load("T_Glow_A.png", 256)
gl[..., 3] = gl[..., 0]
save_rgba("imp_ron_glowdot_diff", gl)
#   imp_ron_sprayadd_diff  MI_SparkSpray (M_Particle_Additive): adds T_debrisSpray_4x1.rgb x colour; onto black
sp = load("T_debrisSpray_4x1.png")
sp[..., :3] *= sp[..., 3:4]
sp[..., 3] = 1.0
save_rgba("imp_ron_sprayadd_diff", sp)
# P_Bullet_Ricochet (RoN's pistol ricochet, Cascade) through M_Sparks_NoNormal_Master: lit, colour =
# saturate(tex.rgb x particle colour x Base Overlay 0.945), opacity = tex.R x alpha (x Opacity x Master Opacity, in the
# effect's Alpha). The emitters' colours are constant over life, so each is baked into its own texture.
def ron_sparks_lit(name, png, colour):
    t = load(png)
    px = np.empty_like(t)
    px[..., :3] = np.clip(t[..., :3] * np.array(colour) * 0.945, 0, 1)
    px[..., 3] = t[..., 0]
    save_rgba(name, px)


ron_sparks_lit("imp_ron_ricflash03_diff", "T_VFX_Sparks_03_B.png", (5151.597, 920.969, 236.775))   # Sparks_02
ron_sparks_lit("imp_ron_ricflash02_diff", "T_VFX_Sparks_02_B.png", (2555.0, 255.0, 25.0))          # Sparks_03
ron_sparks_lit("imp_ron_powder_diff", "T_VFX_Powder_01_B.png", (1.0, 1.0, 1.0))                    # SpallCircle


# Cloth (NS_Bullet_Impact_Fabric_Big) and glass (NS_BulletImpact_Glass): materials that saturate texture x a particle
# colour above 1, baked with that colour (the converter then writes Color 1), plus glass's dithered clip
def sat_bake(name, png, colour, alpha_from="a", size=None):
    t = load(png, size)
    px = np.empty_like(t)
    px[..., :3] = np.clip(t[..., :3] * np.array(colour), 0, 1)
    px[..., 3] = t[..., 3] if alpha_from == "a" else t[..., 0]
    save_rgba(name, px)


sat_bake("imp_ron_spray_x100_diff", "T_debrisSpray_4x1.png", (100, 100, 100))      # cloth spatter (MI_DebrisSpray x User 100)
sat_bake("imp_dust_x2_diff", "T_thinSmoke_02_8x4.png", (2, 2, 2))                # glass base dust (MI_ThinSmoke_01 x User 2)
sat_bake("imp_ron_sparks05_diff", "T_VFX_Sparks_05_B.png", (5000 * 0.945, 500 * 0.945, 250 * 0.945), "r")   # cloth sparks
fe = load("T_VFX_Feathers_01_B.png", 1024)                                         # feathers: colour 0.25 x 0.945 in Color
fe[..., 3] = fe[..., 0]
save_rgba("imp_ron_feather_diff", fe)
# M_Glass_Chunks: masked with a temporal dither, clip where A x alpha < 0.667 + 0.167 x noise, i.e. on average
# opacity = saturate((A x alpha - 0.667) / 0.167). Alpha holds s = 1 - 0.667 / A (0 below): with SourceMin 1 -> 0
# over the particle alpha and SourceWidth 0.167, KCD2 gives saturate((s - (1 - alpha)) / 0.167), the same ramp to
# within the ramp width's change with A. Colour = A (Debris Color 1).
gc = load("T_Glass_Chunks_3x3_Mask.png")
px = np.empty_like(gc)
A = gc[..., 3]
px[..., 0] = px[..., 1] = px[..., 2] = A
px[..., 3] = np.where(A > 0.667, 1 - 0.667 / np.maximum(A, 1e-6), 0.0)
save_rgba("imp_ron_glasschunk_diff", px)
