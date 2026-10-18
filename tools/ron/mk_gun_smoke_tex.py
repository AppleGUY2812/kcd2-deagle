#!/usr/bin/env python3
"""Ready or Not's gun smoke textures -> TIFs for KCD2 (compiled by tools/fx_textures.ps1 into
src/Textures/particles/deagle, which git ignores: ripped assets stay private).
    python3 -I tools/ron/mk_gun_smoke_tex.py [<png dir>] [<out dir>]
The PNGs are RoN's DXT5 sRGB textures as decoded (docs/ron/gun_smoke_shells.md 1.4-1.5), kept as they are: its
materials (M_Particle_LitTransparent) take colour = texture x particle colour, opacity = texture alpha x particle
alpha, and KCD2's lit particles do the same with the colour in Color= and the flipbook in TextureTiling.
  gun_ron_puff_front_diff  T_muzzlePuff_Front_01 (MI_Muzzle_Puff_Front): 8 x 4 puffs of 128 x 128
  gun_ron_puff_side_diff   T_muzzlePuff_Side_01 (MI_Muzzle_puff_Side): 8 x 4 side puffs of 128 x 256
  gun_ron_wispy_diff       T_smokeWispy_01_8x4 (MI_WispySmoke, the barrel smoke): 8 x 4 wisps of 128 x 128
"""
import sys
import numpy as np
from PIL import Image

src = sys.argv[1] if len(sys.argv) > 1 else "/mnt/e/Tools/deagle-work/ron/work/smoke_tex"
out = sys.argv[2] if len(sys.argv) > 2 else "/mnt/e/Tools/deagle-work/fx_tif"
for name, png in (("gun_ron_puff_front_diff", "T_muzzlePuff_Front_01"), ("gun_ron_puff_side_diff", "T_muzzlePuff_Side_01"),
                  ("gun_ron_wispy_diff", "T_smokeWispy_01_8x4")):
    im = Image.open(f"{src}/{png}.png").convert("RGBA")
    a = np.asarray(im)
    im.save(f"{out}/{name}.tif", compression="tiff_lzw")
    print(f"{name}.tif  {im.size[0]}x{im.size[1]}  rgb mean {a[..., :3].mean() / 255:.3f}  alpha mean {a[..., 3].mean() / 255:.3f}")
