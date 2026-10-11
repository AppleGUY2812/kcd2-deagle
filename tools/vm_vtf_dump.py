# Blender (SourceIO enabled): decode the viewmodel's VTF textures to raw RGBA .npy (top-down rows, uint8).
# usage: blender -b --factory-startup -P vm_vtf_dump.py -- <materials root> <out dir>
import sys, os, addon_utils
import numpy as np
MAT, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.utils.pylib.vtf import load_vtf_texture
NAMES = ["yabrak/deagle/MI_1P_DEAGLE_rgb", "yabrak/deagle/MI_1P_DEAGLE_n", "yabrak/deagle/MI_1P_DEAGLE_e",
         "bullets/t_1p_ammunition_col", "bullets/t_1p_ammunition_nrm", "bullets/t_1p_ammunition_exp",
         "viper/mw/rigs/skin/skin_col_a", "viper/mw/rigs/skin/skin_col_n", "viper/mw/rigs/skin/skin_col_m",
         "viper/mw/arms/dday/mp_western_dday_shirt_a_c", "viper/mw/arms/dday/hero_alex_shirt_a_n",
         "viper/mw/rigs/gloves/knuckled/knuckled_col_bale", "viper/mw/rigs/gloves/knuckled/viewmodel_hero_alex_gloves_legal_a_n",
         "viper/mw/rigs/gloves/knuckled/viewmodel_hero_alex_gloves_legal_a_m"]
os.makedirs(OUT, exist_ok=True)
for n in NAMES:
    data = open(os.path.join(MAT, n.replace("/", os.sep) + ".vtf"), "rb").read()
    px, w, h, is_float = load_vtf_texture(data)
    a = np.frombuffer(px, dtype=np.float32 if is_float else np.uint8).reshape(h, w, 4)
    if is_float:
        a = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    np.save(os.path.join(OUT, os.path.basename(n) + ".npy"), a)
    print("VTF", n, w, h, "float" if is_float else "u8", "alpha range", int(a[..., 3].min()), int(a[..., 3].max()))
print("DUMP DONE")
