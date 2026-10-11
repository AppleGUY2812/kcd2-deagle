# Blender (SourceIO): decode every .vtf in a folder to RGBA uint8 .npy (top-down). usage: -- <in dir> <out dir>
import sys, os, addon_utils
import numpy as np
IN, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.utils.pylib.vtf import load_vtf_texture
os.makedirs(OUT, exist_ok=True)
for f in sorted(os.listdir(IN)):
    if not f.lower().endswith(".vtf"): continue
    px, w, h, is_float = load_vtf_texture(open(os.path.join(IN, f), "rb").read())
    a = np.frombuffer(px, dtype=np.float32 if is_float else np.uint8).reshape(h, w, 4)
    if is_float: a = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    np.save(os.path.join(OUT, f[:-4] + ".npy"), a)
    print("VTF", f, w, h, "alpha", int(a[..., 3].min()), int(a[..., 3].max()), "rgbmax", int(a[..., :3].max()))
