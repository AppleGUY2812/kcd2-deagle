#!/usr/bin/env python3
"""glTF metal/roughness textures -> KCD2 spec/gloss TIFs (rc picks presets by suffix):
  <name>_diff.tif  albedo = base * (1 - metal)
  <name>_spec.tif  reflectance = lerp(0.04, base, metal)
  <name>_ddna.tif  normal (green flipped: glTF is OpenGL, CryEngine is DirectX) + smoothness (1 - roughness) in alpha
"""
import sys
from pathlib import Path
import numpy as np
from PIL import Image

src, dst, size = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 2048
dst.mkdir(parents=True, exist_ok=True)

def load(path, n):
    im = Image.open(path).convert("RGB")
    if im.size[0] > n:
        im = im.resize((n, n), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0

def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def lin_to_srgb(c):
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.clip(c, 0, 1), 1 / 2.4) - 0.055)

def save(arr, path, mode):
    Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), mode).save(path, compression="tiff_deflate")
    print("wrote", path)

for base_path in sorted(src.glob("*_baseColor.png")):
    name = base_path.name.replace("_baseColor.png", "")
    out = name.lower()
    n = min(size, Image.open(base_path).size[0])
    base = srgb_to_lin(load(base_path, n))
    mr = load(src / f"{name}_metallicRoughness.png", n)
    rough, metal = mr[..., 1:2], mr[..., 2:3]
    save(lin_to_srgb(base * (1 - metal)), dst / f"deagle_{out}_diff.tif", "RGB")
    save(lin_to_srgb(0.04 * (1 - metal) + base * metal), dst / f"deagle_{out}_spec.tif", "RGB")
    nrm = load(src / f"{name}_normal.png", n)
    nrm[..., 1] = 1 - nrm[..., 1]
    save(np.concatenate([nrm, 1 - rough], axis=-1), dst / f"deagle_{out}_ddna.tif", "RGBA")
