# Blender (4.3 + KCD2 toolkit): a Ready or Not shell mesh (.pskx) -> a CGF for our casings (gun.caseModel), compiled
# by rc.exe. Ready or Not's own mesh, axes and origin, 1:1:
#   - psk.read gives Unreal axes (left-handed, cm); KCD2 is right-handed, metres: (x, y, z) -> (-x, y, z) x 0.01, the
#     same mirror the gun importer makes (ron_import.py: Unreal +Y = the gun's forward stays CryEngine's +Y); front
#     faces are then (w0, w2, w1): the file's order agreed with Ready or Not's stored normals on 0 of 382 faces (an
#     inside-out casing; the gun had the same, the user saw its inside in game 2026-10-18). Checked here against those
#     normals (refused under 95 %), and they are the mesh's own custom normals, as ron_import.py does;
#   - the origin stays where Ready or Not has it (the 50AE_Casing's base): its BulletShell is put at ShellSpawn by its
#     actor origin (docs/ron/gun_smoke_shells.md 2.3 step 5) and spins about its centre of mass, as KCD2's physics does;
#   - UVs as the importer makes them (u, 1 - v), so the green-up normal map stays right;
#   - one material, physDefault: the mesh is its own collision shape, like our deagle_case.
# usage: blender -b --factory-startup -P mk_casing.py -- <mesh.pskx> <out dir> <rc.exe> <cgf name> <mtl name>
import bpy, sys, os, addon_utils

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT, RC, NAME, MTL = argv[:5]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psk

addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)

m = psk.read(SRC)
pts = [(-x * 0.01, y * 0.01, z * 0.01) for x, y, z in m["points"]]
W = m["wedges"]
faces = [(W[a][0], W[c][0], W[b][0]) for a, b, c, _, _ in m["faces"]]
me = bpy.data.meshes.new(NAME)
me.from_pydata(pts, [], faces)
uv = me.uv_layers.new(name="UVMap")
li = 0
for a, b, c, _, _ in m["faces"]:
    for wi in (a, c, b):
        uv.data[li].uv = (W[wi][1], 1 - W[wi][2]); li += 1
mats = m["materials"]
for i, mn in enumerate(mats):
    me.materials.append(bpy.data.materials.new(f"{MTL}__{i + 1:02d}__{mn}__physDefault"))
for poly, (_, _, _, mat, _) in zip(me.polygons, m["faces"]):
    poly.material_index = min(mat, len(mats) - 1)
    poly.use_smooth = True
me.update()
from mathutils import Vector
nrm = [Vector((-x, y, z)).normalized() for x, y, z in m["normals"]]   # the same mirror as the points
agree = sum(1 for poly in me.polygons if poly.normal.dot(sum((nrm[v] for v in poly.vertices), Vector())) > 0)
print(f"CASING FACING {agree}/{len(me.polygons)} faces agree with Ready or Not's stored normals")
if agree < 0.95 * len(me.polygons):
    raise SystemExit(f"CASING faces point inward ({agree}/{len(me.polygons)} agree): fix the winding")
me.normals_split_custom_set_from_vertices(nrm)
ob = bpy.data.objects.new(NAME, me)
bpy.context.scene.collection.objects.link(ob)
co = [v.co for v in me.vertices]
print("CASING", NAME, len(co), "verts", len(me.polygons), "tris", "min", tuple(round(min(v[i] for v in co), 4) for i in range(3)),
      "max", tuple(round(max(v[i] for v in co), 4) for i in range(3)), "mats", [s.name for s in me.materials])
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=NAME)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=f"{OUT}/{NAME}.dae", apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE")
