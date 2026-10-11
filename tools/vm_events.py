# Blender (SourceIO): dump animation events (sound cues) per sequence of the MWII viewmodel MDL.
import bpy, sys, json, addon_utils
MDL, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.models.mdl.v49 import MdlV49
from SourceIO.library.utils import FileBuffer
with FileBuffer(MDL) as f:
    mdl = MdlV49.from_buffer(f)
s0 = mdl.sequences[0]
print("SEQ ATTRS", [a for a in dir(s0) if not a.startswith("_")])
res = {}
for s in mdl.sequences:
    evs = []
    for e in getattr(s, "events", []) or []:
        d = {a: getattr(e, a) for a in dir(e) if not a.startswith("_") and not callable(getattr(e, a))}
        evs.append({k: (v if isinstance(v, (int, float, str)) else str(v)) for k, v in d.items()})
    res[s.name] = {"events": evs, "fps": getattr(s, "fps", None)}
    print("SEQ", s.name, len(evs), evs[:6])
json.dump(res, open(OUT, "w"), indent=1)
