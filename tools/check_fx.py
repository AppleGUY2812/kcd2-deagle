#!/usr/bin/env python3
"""Every part of the muzzle flash effects must live long enough to be drawn. A new emitter is first drawn a frame or
two after it's made, and KCD2 runs at ~26 fps indoors (38 ms a frame): parts living 35-50 ms were never drawn
(in game 2026-10-12, twice: commit 762f3b0, then again after a7598b2 shortened them), 0.08 s parts were.
usage: check_fx.py <repo>"""
import os, sys, xml.etree.ElementTree as ET

MIN_LIFE = 0.08
# The rule behind it (Claude 4, 2026-10-18, flashlag 23/23): a new emitter is first drawn one frame late with its
# particles ~2 frames old, so a part shows at F fps only if it lives over 2/F (60 fps 33 ms, 30 fps 66 ms, 26 fps 77 ms).
# Ready or Not's own parts keep RoN's lifetimes (user: copy RoN): checked against 2 frames at 60 fps, and the frame rate
# each one stops showing at is printed (RoN draws them at frame 0 at any rate; the low-fps loss is KCD2's lag).
RON_EXACT = {"muzzle_sparks_ron"}
RON_MIN = 2 / 60
repo = sys.argv[1]
lib = ET.parse(os.path.join(repo, "src", "Libs", "Particles", "deagle.xml")).getroot()
bad, n = [], 0
for eff in lib.findall("Particles"):
    if not eff.get("Name", "").startswith("muzzle") or eff.get("Name") == "muzzle_smoke":
        continue
    if eff.get("Name", "").endswith("_live"):   # continuous: always alive, shown by the plugin moving it (no spawn delay)
        continue
    for part in eff.iter("Particles"):
        prm = part.find("Params")
        if part is eff or prm is None or prm.get("Enabled") == "false":
            continue
        lt = prm.get("ParticleLifeTime", "0").split(",")
        life = float(lt[0])
        n += 1
        if eff.get("Name") in RON_EXACT:
            shortest = life * (1 - (float(lt[1]) if len(lt) > 1 else 0))   # base, random share
            print(f"FX RON {eff.get('Name')}.{part.get('Name')}: {shortest * 1000:.0f}-{life * 1000:.0f} ms, all drawn above "
                  f"{2 / shortest:.0f} fps, none below {2 / life:.0f} fps")
            if shortest <= RON_MIN:
                bad.append(f"{eff.get('Name')}.{part.get('Name')} {shortest:.3f} (Ready or Not's, under 2 frames at 60 fps)")
            continue
        if life < MIN_LIFE:
            bad.append(f"{eff.get('Name')}.{part.get('Name')} {life}")
if bad:
    print(f"FX TOO SHORT (< {MIN_LIFE} s, never drawn):", ", ".join(bad))
    sys.exit(1)
print(f"FX OK: {n} flash parts live >= {MIN_LIFE} s")
