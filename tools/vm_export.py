# Blender: MWII Deagle viewmodel (SourceIO import, mwii_vm.blend) -> KCD2 character: deagle_vm.chr (skeleton),
# deagle_vm_skin.skin (arms, gloves, gun, mag, rounds) and one DAE per animation for rc.
# Orientation: the view camera (tag_view, never animated) at the origin looking +Y, up +Z (CryEngine forward).
# usage: blender -b --factory-startup -P vm_export.py -- <mwii_vm.blend> <model out dir> <anim dae dir> <rc.exe>
import bpy, sys, os, math, addon_utils
from mathutils import Matrix
BLEND, OUT, ANIM, RC = sys.argv[sys.argv.index("--") + 1:][:4]
bpy.ops.wm.open_mainfile(filepath=BLEND)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC; Configuration.texture_rc_path = RC
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
meshes = [o for o in scene.objects if o.type == "MESH"]
for o in [o for o in scene.objects if o.type not in ("ARMATURE", "MESH")]:
    bpy.data.objects.remove(o)
if bpy.context.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")

# ---- orientation: Source/CoD view looks +X, Z up -> CryEngine +Y forward
R = Matrix.Rotation(math.radians(90), 4, "Z")
for o in [arm] + [m for m in meshes if m.parent is None]:
    o.matrix_world = R @ o.matrix_world
bpy.ops.object.select_all(action="DESELECT")
for o in [arm] + meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)

# ---- names CryEngine is happy with
for b in arm.data.bones:
    if "." in b.name:
        b.name = b.name.replace(".", "_")
MATS = {"MI_1P_DEagle": "deagle_vm__01__Gun__physNone", "MI_1P_Ammunition": "deagle_vm__02__Ammo__physNone",
        "base_male_arm_skintone_01": "deagle_vm__03__Skin__physNone", "mp_western_dday_shirt_a": "deagle_vm__04__Sleeve__physNone",
        "knuckled_col_bale": "deagle_vm__05__Glove__physNone"}
for m in bpy.data.materials:
    if m.name in MATS:
        m.name = MATS[m.name]
print("MATERIALS", sorted({s.material.name for o in meshes for s in o.material_slots if s.material}))

# ---- one skin with every mesh, joined (keeps vertex groups + armature modifier)
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
skin = bpy.context.view_layer.objects.active
skin.name = "deagle_vm_skin"
if skin.parent is not arm:
    mw = skin.matrix_world.copy(); skin.parent = arm; skin.matrix_world = mw
for md in skin.modifiers:
    if md.type == "ARMATURE":
        md.object = arm
nw = max((len(v.groups) for v in skin.data.vertices), default=0)
print("SKIN", len(skin.data.vertices), "verts, max weights/vertex", nw, "bones", len(arm.data.bones))

# ---- export nodes: chr (skeleton) + skin (mesh)
# the toolkit wants a mesh in the chr node: a 1 mm triangle at the camera origin skinned to the root
pm = bpy.data.meshes.new("deagle_vm_proxy"); pm.from_pydata([(0, 0, 0), (0.001, 0, 0), (0, 0, 0.001)], [], [(0, 1, 2)])
pm.materials.append(bpy.data.materials["deagle_vm__03__Skin__physNone"])
proxy = bpy.data.objects.new("deagle_vm_proxy", pm); scene.collection.objects.link(proxy); proxy.parent = arm
vg = proxy.vertex_groups.new(name="tag_origin"); vg.add([0, 1, 2], 1.0, "REPLACE")
md = proxy.modifiers.new("Armature", "ARMATURE"); md.object = arm
bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); proxy.select_set(True); bpy.context.view_layer.objects.active = proxy
bpy.ops.bcry.add_cry_export_node("EXEC_DEFAULT", node_type="chr", node_name="deagle_vm")
bpy.ops.object.select_all(action="DESELECT"); skin.select_set(True); bpy.context.view_layer.objects.active = skin
bpy.ops.bcry.add_cry_export_node("EXEC_DEFAULT", node_type="skin", node_name="deagle_vm_skin")
print("NODES", [c.name for c in bpy.data.collections if c.name.endswith((".chr", ".skin"))])
os.makedirs(OUT, exist_ok=True)
arm.animation_data.action = bpy.data.actions["@idle1"]; scene.frame_set(int(bpy.data.actions["@idle1"].frame_range[0]))
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=os.path.join(OUT, "deagle_vm.dae"), apply_modifiers=False,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("MODEL EXPORTED", sorted(os.listdir(OUT)))

# ---- animations: one i_caf node per action, exported one at a time
os.makedirs(ANIM, exist_ok=True)
for c in [c for c in bpy.data.collections if c.name.endswith((".chr", ".skin")) or "chr" in c.name or "skin" in c.name]:
    pass
for act in [a for a in bpy.data.actions if a.name.startswith("@")]:
    name = "deagle_vm_" + act.name[1:]
    for c in [c for c in bpy.data.collections if c.name.endswith(".i_caf")]:
        bpy.data.collections.remove(c)
    arm.animation_data.action = act
    f0, f1 = (int(v) for v in act.frame_range)
    scene.frame_start, scene.frame_end = f0, f1
    bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name=name, mode="Manual",
                                        range_type="Values", node_start=f0, node_end=f1)
    bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=os.path.join(ANIM, f"{name}.dae"), disable_rc=True, save_dae=True)
    print("ANIM", name, f0, f1)
print("COLLECTIONS", [c.name for c in bpy.data.collections])
print("VM EXPORT DONE")
