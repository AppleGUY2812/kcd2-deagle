#!/usr/bin/env python3
"""Every part of the muzzle flash effects must live long enough to be drawn. A new emitter is first drawn a frame or
two after it's made, and KCD2 runs at ~26 fps indoors (38 ms a frame): parts living 35-50 ms were never drawn
(in game 2026-10-12, twice: commit 762f3b0, then again after a7598b2 shortened them), 0.08 s parts were.
usage: check_fx.py <repo>"""
import os, sys, xml.etree.ElementTree as ET

MIN_LIFE = 0.08
repo = sys.argv[1]
lib = ET.parse(os.path.join(repo, "src", "Libs", "Particles", "deagle.xml")).getroot()
bad, n = [], 0
for eff in lib.findall("Particles"):
    if not eff.get("Name", "").startswith("muzzle") or eff.get("Name") == "muzzle_smoke":
        continue
    for part in eff.iter("Particles"):
        prm = part.find("Params")
        if part is eff or prm is None or prm.get("Enabled") == "false":
            continue
        life = float(prm.get("ParticleLifeTime", "0").split(",")[0])
        n += 1
        if life < MIN_LIFE:
            bad.append(f"{eff.get('Name')}.{part.get('Name')} {life}")
if bad:
    print(f"FX TOO SHORT (< {MIN_LIFE} s, never drawn):", ", ".join(bad))
    sys.exit(1)
print(f"FX OK: {n} flash parts live >= {MIN_LIFE} s")
