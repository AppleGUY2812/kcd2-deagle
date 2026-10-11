#!/usr/bin/env python3
"""MWII viewmodel textures (decoded VTF .npy, top-down RGBA) -> CryEngine Illum maps: <name>_diff.tif,
<name>_ddna.tif (normal + gloss in alpha) and <name>_spec.tif. Source has no metalness: the phong exponent map's red
channel becomes gloss, the normal/base alpha phong or env mask scales the specular colour.
usage: vm_textures.py <npy dir> <out dir>"""
import sys, os
import numpy as np
from PIL import Image
SRC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
L = lambda n: np.load(os.path.join(SRC, n + ".npy"), allow_pickle=False).astype(np.float32) / 255.0
def save(name, a, mode):
    a = np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8)
    Image.fromarray(a, mode).save(os.path.join(OUT, name + ".tif"))
def fit(a, shape):
    if a.shape[:2] == shape:
        return a
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(im.resize((shape[1], shape[0]), Image.BICUBIC)).astype(np.float32) / 255.0
def build(out, col, nrm, gloss, spec_rgb):
    h, w = col.shape[:2]
    save(out + "_diff", col[..., :3], "RGB")
    n = fit(nrm, (h, w))
    save(out + "_ddna", np.dstack([n[..., :3], fit(gloss[..., None].repeat(3, 2), (h, w))[..., 0]]), "RGBA")
    save(out + "_spec", fit(spec_rgb, (h, w)), "RGB")
    print("TEX", out, w, h)

# gun: phong exponent map R -> gloss; normal alpha = env/spec mask; no albedo tint (dark steel)
c, n, e = L("MI_1P_DEAGLE_rgb"), L("MI_1P_DEAGLE_n"), L("MI_1P_DEAGLE_e")
mask = n[..., 3]
build("deagle_vm_gun", c, n, np.clip(0.25 + 0.7 * np.sqrt(e[..., 0]), 0, 1), np.repeat((0.06 + 0.5 * mask)[..., None], 3, 2))
# rounds: brass, albedo-tinted spec
c, n, e = L("t_1p_ammunition_col"), L("t_1p_ammunition_nrm"), L("t_1p_ammunition_exp")
build("deagle_vm_ammo", c, n, np.clip(0.3 + 0.6 * np.sqrt(e[..., 0]), 0, 1), np.clip(c[..., :3] * 0.9 + 0.05, 0, 1))
# skin: dielectric, normal alpha = phong mask
c, n, m = L("skin_col_a"), L("skin_col_n"), L("skin_col_m")
build("deagle_vm_skin", c, n, np.clip(0.2 + 0.45 * m[..., 0], 0, 1), np.repeat((0.03 + 0.05 * n[..., 3])[..., None], 3, 2))
# sleeve: cloth
c, n = L("mp_western_dday_shirt_a_c"), L("hero_alex_shirt_a_n")
build("deagle_vm_sleeve", c, n, np.full(c.shape[:2], 0.22, np.float32), np.full(c.shape[:2] + (3,), 0.03, np.float32))
# gloves: leather/synthetic, normal alpha = phong mask
c, n, m = L("knuckled_col_bale"), L("viewmodel_hero_alex_gloves_legal_a_n"), L("viewmodel_hero_alex_gloves_legal_a_m")
build("deagle_vm_glove", c, n, np.clip(0.25 + 0.5 * fit(m, c.shape[:2])[..., 0], 0, 1), np.repeat((0.04 + 0.12 * fit(n, c.shape[:2])[..., 3])[..., None], 3, 2))
print("TEXTURES DONE")
