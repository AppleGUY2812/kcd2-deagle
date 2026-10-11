# Blender (SourceIO): raw per-frame bone data from the MDL (mdl.animations[anim][frame, bone]) vs what the imported
# Blender action contains, for the bones the action left unkeyed.
import bpy, sys, addon_utils
import numpy as np
from mathutils import Matrix, Quaternion, Vector
MDL = sys.argv[sys.argv.index("--") + 1]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.models.mdl.v49 import MdlV49
from SourceIO.library.utils import FileBuffer
arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
with FileBuffer(MDL) as f:
    mdl = MdlV49.from_buffer(f)
print("ANIMS", [(i, a.name, a.frame_count) for i, a in enumerate(mdl.anim_descs)])
print("SEQS", [s.name for s in mdl.sequences][:20] if hasattr(mdl, "sequences") else "n/a")
S = 0.01905
def rest_local(b):
    return (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local
names = [b.name for b in mdl.bones]
try:
    anims = mdl.animations
    print("ANIM DATA", type(anims), len(anims))
except Exception as e:
    print("NO mdl.animations:", e); anims = None
if anims is not None:
    for ai, ad in enumerate(mdl.anim_descs):
        data = anims[ai]
        for bn in ("j_slide", "j_ammo_17", "j_mag1"):
            bi = names.index(bn)
            rec = data[0, bi]
            pos = Vector([float(v) for v in np.asarray(rec["pos"]).reshape(-1)[:3]]) * S
            x, y, z, w = [float(v) for v in np.asarray(rec["rot"]).reshape(-1)[:4]]
            L = rest_local(arm.data.bones[bn])
            dt = (pos - L.translation).length * 1000
            da = L.to_quaternion().rotation_difference(Quaternion((w, x, y, z))).angle * 57.2958
            print(f"RAW {ad.name:16s} {bn:10s} frame0 vs rest: pos {dt:7.2f} mm rot {da:6.2f} deg")
