#!/usr/bin/env python3
"""Sniper Elite V2 organ textures (extracted DDS, tools/se2/rscf.py) -> KCD2 diffuse TIFs for rc (private assets).
usage: organ_textures.py <extracted graphics dir> <out dir>"""
import sys
from pathlib import Path
from PIL import Image

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
MAP = {"heartlungs_colspec": "se2_heartlungs_diff", "gutsliverkidneys_colspec": "se2_guts_diff", "eyeball_d": "se2_eyeball_diff",
       "testes_col": "se2_testes_diff", "bones_colspec": "se2_bones_diff"}
for name, out in MAP.items():
    p = next(src.rglob(name + ".dds"), None)
    if not p:
        print("missing", name); continue
    im = Image.open(p).convert("RGB")
    im.save(dst / (out + ".tif"))
    print(out, im.size)
