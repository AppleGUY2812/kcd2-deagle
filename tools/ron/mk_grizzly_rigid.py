# Blender (4.3 + KCD2 toolkit): the Grizzly MK V's body as 10 RIGID pieces, one CGF per gun bone, for Silhouette POM.
# Why (2026-10-23): KCD2's SilPOM (CommonZPass.cfi) builds its prisms from the mesh's own TANGENT stream and the
# un-animated position (mul(InstMatrix, Position)); a skinned mesh has neither, so every ray missed and the whole gun
# was clipped away (invisible, no log error). Every vertex of the gun body rides exactly ONE bone (SK_PIS_GRIZZLY.psk:
# 21939 points, none with two influences), so each bone's piece is a static mesh in that bone's own frame: attached
# with CreateBoneAttachment(0, bone, name, false) + SetAttachmentCGF it moves exactly as the skinned body did
# (world = joint x inverse bind x bind-pose vertex), and SilPOM sees a static mesh like the walls that ship with it.
# Input: <npz dir> from the extraction below (part_<bone>.npz: per triangle corner p (bone space, m), n, uv), written
# from build/model/ron_grizzly_vm.dae (the export the game's .chr/.skin were compiled from: its inverse bind matrices).
# usage: blender -b --factory-startup -P mk_grizzly_rigid.py -- <npz dir> <out dir> <rc.exe>
import bpy, sys, os, json, addon_utils
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT, RC = argv[0], argv[1], argv[2]
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import bcry_normals; bcry_normals.install()   # the meshes' own normals (Ready or Not's), not the toolkit's recompute

MTL = "ron_grizzly_rigid"                     # -> ron_grizzly_rigid.mtl next to the CGFs (sub-material 1: the body)
mat = bpy.data.materials.new(f"{MTL}__01__Body__physNone")
parts = json.load(open(os.path.join(SRC, "parts.json")))
objs = []
for bone in parts:
    d = np.load(os.path.join(SRC, f"part_{bone}.npz"))
    p, n, uv = d["p"], d["n"], d["uv"]
    tris = len(p) // 3
    me = bpy.data.meshes.new(f"{MTL}_{bone}")
    me.from_pydata([tuple(v) for v in p], [], [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(tris)])
    me.materials.append(mat)
    uvl = me.uv_layers.new(name="UVMap")
    uvl.data.foreach_set("uv", uv.astype(np.float32).ravel())   # as the toolkit wrote them from Blender (COLLADA and Blender share v-up)
    me.validate(clean_customdata=False)
    me.update()
    me.normals_split_custom_set([tuple(v) for v in n])
    ob = bpy.data.objects.new(f"{MTL}_{bone}", me)
    bpy.context.scene.collection.objects.link(ob)
    objs.append((bone, ob))
    print("PART", bone, "tris", tris)
for bone, ob in objs:
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=f"{MTL}_{bone}")
os.makedirs(OUT, exist_ok=True)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=os.path.join(OUT, f"{MTL}.dae"), apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE", sorted(os.listdir(OUT)))
