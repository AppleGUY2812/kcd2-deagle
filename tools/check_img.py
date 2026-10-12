#!/usr/bin/env python3
"""Every clip the mod ships (src/Animations/**/*.caf) must have its header in the merged src/Animations/Animations.img,
or the game can't start it ("viewmodel anim did not start ... false" for every clip of a gun; it stands in its bind
pose). A build that rewrote the img from a subset (Deagle only) dropped the Glock once.
usage: check_img.py <repo>"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from merge_img import read, path_of

repo = sys.argv[1]
anim = os.path.join(repo, "src", "Animations")
have = {path_of(p).decode() for _, p in read(os.path.join(anim, "Animations.img"))}
missing = []
for root, _, files in os.walk(anim):
    for f in files:
        if f.endswith(".caf"):
            rel = "animations/" + os.path.relpath(os.path.join(root, f), anim).replace(os.sep, "/").lower()
            if rel not in have:
                missing.append(rel)
total = sum(1 for _, _, fs in os.walk(anim) for f in fs if f.endswith(".caf"))
if missing:
    print(f"IMG MISSING {len(missing)} of {total} clips:", " ".join(sorted(missing)[:12]))
    sys.exit(1)
print(f"IMG OK: all {total} clips indexed")
