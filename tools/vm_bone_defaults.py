# Blender (SourceIO): compare each bone's Blender rest-local transform with the MDL default local pose
# (bone.position / bone.quat), to find the import scale and the bones whose defaults differ.
import bpy, sys, addon_utils
from mathutils import Matrix, Quaternion, Vector
MDL = sys.argv[sys.argv.index("--") + 1]
addon_utils.enable("SourceIO", default_set=True)
from SourceIO.library.models.mdl.v49 import MdlV49
from SourceIO.library.utils import FileBuffer
arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
with FileBuffer(MDL) as f:
    mdl = MdlV49.from_buffer(f)
def rest_local(b):
    return (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local
ratios = []
for mb in mdl.bones:
    b = arm.data.bones.get(mb.name)
    if not b: continue
    p = Vector([float(v) for v in mb.position])
    t = rest_local(b).translation
    if p.length > 1e-3 and t.length > 1e-4: ratios.append(t.length / p.length)
ratios.sort(); scale = ratios[len(ratios) // 2]
print("SCALE median", round(scale, 6), "min", round(ratios[0], 6), "max", round(ratios[-1], 6))
for mb in mdl.bones:
    b = arm.data.bones.get(mb.name)
    if not b: continue
    q = [float(v) for v in mb.quat]
    D = Matrix.Translation(Vector([float(v) for v in mb.position]) * scale) @ Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4()
    L = rest_local(b)
    dt = (D.translation - L.translation).length * 1000
    da = (L.to_quaternion().rotation_difference(D.to_quaternion())).angle * 57.2958
    if dt > 0.5 or da > 0.5 or mb.name in ("j_slide", "j_ammo_17", "j_mag1"):
        print(f"DIFF {mb.name:24s} pos {dt:8.2f} mm  rot {da:7.2f} deg")
