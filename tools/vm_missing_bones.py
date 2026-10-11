# Blender (SourceIO): which bones each MWII action leaves unkeyed, and how their Blender rest (bind) pose differs from
# the MDL's per-bone default local pose (what Source uses for bones a sequence doesn't animate).
# usage: blender -b mwii_vm.blend -P vm_missing_bones.py -- <v_pist_deagle.mdl>
import bpy, sys, addon_utils
from mathutils import Matrix, Quaternion, Vector, Euler
MDL = sys.argv[sys.argv.index("--") + 1]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.models.mdl.v49 import MdlV49
from SourceIO.library.utils import FileBuffer
arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
with FileBuffer(MDL) as f:
    mdl = MdlV49.from_buffer(f)
b0 = mdl.bones[0]
print("BONE ATTRS", [a for a in dir(b0) if not a.startswith("_")][:40])
all_bones = [b.name for b in arm.data.bones]
for act in bpy.data.actions:
    keyed = {fc.data_path.split('"')[1] for fc in act.fcurves if fc.data_path.startswith("pose.bones")}
    missing = [n for n in all_bones if n not in keyed]
    print("ACTION", act.name, "unkeyed", len(missing), missing)
