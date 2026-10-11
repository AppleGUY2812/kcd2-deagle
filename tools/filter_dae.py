#!/usr/bin/env python3
"""Keep only the arm/finger bone animation channels in a toolkit-exported i_caf DAE, so the compiled CAF
overrides just the arms when layered on Henry (legs/spine keep the game's own animation)."""
import re, sys
import xml.etree.ElementTree as ET

src, dst = sys.argv[1], sys.argv[2]
keep = set()
for side in ("Left", "Right"):
    keep |= {f"{side}Shoulder", f"{side}Arm", f"{side}ForeArm", f"{side}Hand"}
    keep |= {f"{side}Hand{f}{i}" for f in ("Thumb", "Index", "Middle", "Ring", "Pinky") for i in (1, 2, 3)}

ns = "http://www.collada.org/2005/11/COLLADASchema"
ET.register_namespace("", ns)
tree = ET.parse(src)
root = tree.getroot()
lib = root.find(f"{{{ns}}}library_animations")
pattern = re.compile(r"-([A-Za-z0-9_]+?)_(location|rotation_euler|scale)_[XYZ]$")
dropped, kept = set(), 0
for anim in list(lib):
    m = pattern.search(anim.get("id", ""))
    bone = m.group(1) if m else None
    if bone in keep:
        kept += 1
    else:
        lib.remove(anim)
        dropped.add(anim.get("id"))
clips = root.find(f"{{{ns}}}library_animation_clips")
for clip in clips:
    for inst in list(clip):
        if inst.get("url", "").lstrip("#") in dropped:
            clip.remove(inst)
tree.write(dst, xml_declaration=True, encoding="utf-8")
print(f"kept {kept} channels, dropped {len(dropped)}")
