#!/usr/bin/env python3
"""Decoded Source maps (tools/gb/gb_vtf.py) -> CryEngine Illum maps <slug>_vm_<sub>_{diff,ddna,spec}.tif per material
and the gun's .mtl. Source has no metalness: the phong exponent map's red channel becomes gloss and the phong mask
(normal-map alpha) scales the specular colour; the recipe per kind (metal / brass / skin / cloth / glove) is the
one tuned on the Deagle (tools/vm_textures.py).
usage: gb_tex.py <npy dir> <tex.json> <out tif dir> <slug> <out .mtl>"""
import sys, os, json, re, math
import numpy as np
from PIL import Image
NPY, TEXJ, OUT, SLUG, MTL = sys.argv[1:6]
os.makedirs(OUT, exist_ok=True)
tex = json.load(open(TEXJ))
L = lambda f: np.load(os.path.join(NPY, f), allow_pickle=False).astype(np.float32) / 255.0

def save(name, a, mode):
    Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), mode).save(os.path.join(OUT, name + ".tif"))
def fit(a, shape):
    if a.shape[:2] == shape:
        return a
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8))
    return np.asarray(im.resize((shape[1], shape[0]), Image.BICUBIC)).astype(np.float32) / 255.0
def kind(name):
    n = name.lower()
    if re.search(r"ammo|ammun|bullet|round|shell|brass", n): return "brass"
    if re.search(r"skin|skintone|hand(?!guard)|arm_", n): return "skin"
    if re.search(r"shirt|sleeve|cloth|jacket|fabric", n): return "cloth"
    if re.search(r"glove|knuckle", n): return "glove"
    return "metal"
SURFACE = {"metal": "mat_metal", "brass": "mat_metal", "skin": "mat_flesh", "cloth": "mat_cloth", "glove": "mat_cloth"}

subs = []
for name, t in sorted(tex.items(), key=lambda kv: kv[1]["id"]):
    k, sub, maps, prm = kind(name), t["sub"], t["maps"], t["params"]
    base = f"{SLUG}_vm_{sub.lower()}"
    if "base" not in maps:
        print("NO BASE MAP", name, "-> grey"); c = np.full((64, 64, 4), 0.5, np.float32)
    else:
        c = L(maps["base"])
    h, w = c.shape[:2]
    n = fit(L(maps["bump"]), (h, w)) if "bump" in maps else np.dstack([np.full((h, w), 0.5), np.full((h, w), 0.5), np.ones((h, w)), np.ones((h, w))]).astype(np.float32)
    e = fit(L(maps["exp"]), (h, w))[..., 0] if "exp" in maps else None
    if e is None:
        pe = float(re.sub(r"[^0-9.]", "", str(prm.get("$phongexponent", "20"))) or 20)
        e = np.full((h, w), min(1.0, math.log2(max(pe, 1.0)) / 8.0), np.float32)
    mask = n[..., 3]
    if k == "metal":
        gloss, spec = np.clip(0.25 + 0.7 * np.sqrt(e), 0, 1), np.repeat((0.06 + 0.5 * mask)[..., None], 3, 2)
    elif k == "brass":
        gloss, spec = np.clip(0.3 + 0.6 * np.sqrt(e), 0, 1), np.clip(c[..., :3] * 0.9 + 0.05, 0, 1)
    elif k == "skin":
        gloss, spec = np.clip(0.2 + 0.45 * e, 0, 1), np.repeat((0.03 + 0.05 * mask)[..., None], 3, 2)
    elif k == "cloth":
        gloss, spec = np.full((h, w), 0.22, np.float32), np.full((h, w, 3), 0.03, np.float32)
    else:
        gloss, spec = np.clip(0.25 + 0.5 * e, 0, 1), np.repeat((0.04 + 0.12 * mask)[..., None], 3, 2)
    save(base + "_diff", c[..., :3], "RGB")
    save(base + "_ddna", np.dstack([n[..., :3], gloss]), "RGBA")
    save(base + "_spec", spec, "RGB")
    subs.append((sub, k, base))
    print("TEX", name, k, w, h, sorted(maps))

d = f"objects/manmade/weapons/guns/{SLUG}"
mats = "\n".join(f'''    <Material Name="{sub}" MtlFlags="524416" Shader="Illum" GenMask="24000000002000" StringGenMask="%NORMAL_MAP%SPECULAR_MAP" SurfaceType="{SURFACE[k]}" MatTemplate="" Diffuse="1,1,1" Specular="1,1,1" Opacity="1" Shininess="255" vertModifType="0" LayerAct="1">
      <Textures>
        <Texture Map="Diffuse" File="{d}/{b}_diff.tif" />
        <Texture Map="Bumpmap" File="{d}/{b}_ddna.tif" />
        <Texture Map="Specular" File="{d}/{b}_spec.tif" />
      </Textures>
      <PublicParams EmittanceMapGamma="1" SSSIndex="0" />
    </Material>''' for sub, k, b in subs)
open(MTL, "w").write(f'''<Material MtlFlags="524544" vertModifType="0">
  <SubMaterials>
{mats}
  </SubMaterials>
</Material>
''')
print("MTL", MTL, len(subs), "submaterials")
