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

# ---- bones a clip leaves unkeyed would sit at the import's bind pose (slide out of place in idle, a loose round
# floating in reload). Each gets constant keys copied from a clip that does animate it, at a neutral moment.
DONORS = [("@shoot1", "first"), ("@draw", "last"), ("@reload_empty", "last"), ("@inspect", "first")]
GUN_BONES = ("j_slide", "j_mag1", "j_mag2", "j_ammo_01", "j_ammo_17", "j_safetylock", "j_slidestop", "j_hammer", "j_trigger")
def keyed(act):
    return {fc.data_path.split('"')[1] for fc in act.fcurves if fc.data_path.startswith("pose.bones")}
def fill_unkeyed():
    if not arm.animation_data:
        arm.animation_data_create()
    keyed_by = {a.name: keyed(a) for a in bpy.data.actions if a.name.startswith("@")}
    donor_pose = {}
    for bn in GUN_BONES:
        for dname, which in DONORS:
            if dname in keyed_by and bn in keyed_by[dname]:
                act = bpy.data.actions[dname]
                f = int(act.frame_range[0] if which == "first" else act.frame_range[1])
                arm.animation_data.action = act
                bpy.context.scene.frame_set(f)
                pb = arm.pose.bones[bn]
                donor_pose[bn] = (pb.location.copy(), pb.rotation_quaternion.copy(), dname, f)
                break
    for act in [a for a in bpy.data.actions if a.name.startswith("@")]:
        f0, f1 = (int(v) for v in act.frame_range)
        added = []
        for bn, (loc, rot, dname, df) in donor_pose.items():
            if bn in keyed_by[act.name]:
                continue
            for i in range(3):
                fc = act.fcurves.new(f'pose.bones["{bn}"].location', index=i, action_group=bn)
                fc.keyframe_points.insert(f0, loc[i]); fc.keyframe_points.insert(f1, loc[i])
            for i in range(4):
                fc = act.fcurves.new(f'pose.bones["{bn}"].rotation_quaternion', index=i, action_group=bn)
                fc.keyframe_points.insert(f0, rot[i]); fc.keyframe_points.insert(f1, rot[i])
            added.append(f"{bn}<-{dname}:{df}")
        print("FILLED", act.name, added)
# ---- Source stores long sequences in 30-frame sections. Where a section doesn't animate a bone, Source uses the bone's
# default (= our rest pose), but SourceIO writes a zero parent-relative transform. In @inspect that pulled the slide
# back, stuck the spare round out of the bottom of the grip and left the mag hanging. Those frames go back to rest.
def fix_section_zeros():
    rest = {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local for b in arm.data.bones}
    cand = [n for n, m in rest.items() if m.translation.length > 1e-3]
    def zero_frames(act):
        arm.animation_data.action = act
        f0, f1 = (int(v) for v in act.frame_range)
        bad = {}
        for f in range(f0, f1 + 1):
            scene.frame_set(f)
            for n in cand:
                pb = arm.pose.bones[n]
                if (pb.parent.matrix.inverted() @ pb.matrix).translation.length < 1e-5:
                    bad.setdefault(n, set()).add(f)
        return bad
    for act in [a for a in bpy.data.actions if a.name.startswith("@")]:
        bad = zero_frames(act)
        for n, frames in bad.items():
            for fc in act.fcurves:
                if not fc.data_path.startswith(f'pose.bones["{n}"].'):
                    continue
                v = 1.0 if fc.data_path.endswith("rotation_quaternion") and fc.array_index == 0 else 0.0
                for kp in fc.keyframe_points:
                    if int(round(kp.co.x)) in frames:
                        kp.co.y = kp.handle_left.y = kp.handle_right.y = v
                fc.update()
        if bad:
            left = zero_frames(act)
            print("SECTION ZEROS", act.name, {n: len(f) for n, f in bad.items()}, "left after fix", {n: len(f) for n, f in left.items()})
if not arm.animation_data:
    arm.animation_data_create()
fix_section_zeros()
fill_unkeyed()
if not BLEND.endswith('_extra.blend'):
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(os.path.dirname(BLEND), 'mwii_vm_filled.blend'), copy=True)

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

# ---- the rounds in the gun's magazine (bone j_ammo_01, rides j_mag1) become their own skin attachment, so the
# script can hide them while that magazine is empty. The spare magazine (j_mag2) always shows its rounds.
ai = skin.vertex_groups["j_ammo_01"].index
in_mag = {v.index for v in skin.data.vertices if any(g.group == ai and g.weight > 0.5 for g in v.groups)}
for v in skin.data.vertices:
    v.select = False
for e in skin.data.edges:
    e.select = False
n_sel = 0
for p in skin.data.polygons:
    p.select = all(i in in_mag for i in p.vertices)
    if p.select:
        n_sel += 1
        for i in p.vertices:
            skin.data.vertices[i].select = True
bpy.ops.object.select_all(action="DESELECT"); skin.select_set(True); bpy.context.view_layer.objects.active = skin
bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_mode(type="FACE"); bpy.ops.mesh.separate(type="SELECTED")
bpy.ops.object.mode_set(mode="OBJECT")
rounds = next(o for o in bpy.context.selected_objects if o is not skin)
rounds.name = "deagle_vm_rounds"
print("ROUNDS", n_sel, "faces ->", rounds.name, len(rounds.data.vertices), "verts; skin left", len(skin.data.vertices))
# one skin per round (deagle_vm_round_1 = the top one): the script shows as many as the magazine holds
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from split_rounds import split_rounds
round_objs = split_rounds(rounds, arm.matrix_world @ arm.data.bones["j_ammo_17"].head_local, "deagle_vm_round_{}")
# both magazines are their own skins too: a reload cancelled mid-swap hides them while the hands blend back
from split_rounds import split_off
mag_objs = [m for m in (split_off(skin, {b} | {c.name for c in arm.data.bones[b].children_recursive}, f"deagle_vm_{b[2:]}")
                        for b in ("j_mag1", "j_mag2")) if m]

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
for ro in round_objs + mag_objs:
    bpy.ops.object.select_all(action="DESELECT"); ro.select_set(True); bpy.context.view_layer.objects.active = ro
    bpy.ops.bcry.add_cry_export_node("EXEC_DEFAULT", node_type="skin", node_name=ro.name)
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
    if act.name in ("@shoot1", "@shoot2", "@shoot_empty"):   # additive copy (recoil on top of hip or ADS)
        for c in [c for c in bpy.data.collections if c.name.endswith(".i_caf")]:
            bpy.data.collections.remove(c)
        bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
        bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name=name + "_add", mode="Manual",
                                            range_type="Values", node_start=f0, node_end=f1)
        bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=os.path.join(ANIM, f"{name}_add.dae"), disable_rc=True, save_dae=True)
        print("ANIM", name + "_add", f0, f1)
print("COLLECTIONS", [c.name for c in bpy.data.collections])
print("VM EXPORT DONE")
