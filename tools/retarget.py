# Blender: retarget a Mixamo clip's arms + fingers onto Henry (KCD2 male skeleton), relative to the chest,
# preview it from Henry's eyes with the Deagle in hand, and export an upper-body i_caf DAE.
# usage: blender -b --factory-startup -P retarget.py -- <henry male_body.skin> <mixamo.fbx> <anim name>
#        <out dir for dae> <rc.exe> <deagle_frame source gltf> <preview dir> [first_frame last_frame]
import bpy, sys, math, mathutils, addon_utils
from mathutils import Matrix, Vector, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
SKIN, FBX, NAME, OUTDIR, RC, GUN_GLTF, PREVIEW = argv[:7]
RANGE = (int(argv[7]), int(argv[8])) if len(argv) > 8 else None

ARM_BONES = []
for side in ("Left", "Right"):
    ARM_BONES += [f"{side}Shoulder", f"{side}Arm", f"{side}ForeArm", f"{side}Hand"]
    for finger in ("Thumb", "Index", "Middle", "Ring", "Pinky"):
        ARM_BONES += [f"{side}Hand{finger}{i}" for i in (1, 2, 3)]
SRC_CHEST, TGT_CHEST = "Spine2", "Spine4"

bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
scene = bpy.context.scene

bpy.ops.import_scene.kcd2_skin(filepath=SKIN)
tgt = next(o for o in scene.objects if o.type == "ARMATURE")
tgt.name = "Henry"
bpy.ops.import_scene.fbx(filepath=FBX)
src = next(o for o in scene.objects if o.type == "ARMATURE" and o is not tgt)
for b in src.pose.bones:
    b.name = b.name.replace("mixamorig:", "")
act = src.animation_data.action
first, last = RANGE or (int(act.frame_range[0]), int(act.frame_range[1]))

def rest_world(obj, name):
    return obj.matrix_world @ obj.data.bones[name].matrix_local

def pose_world(obj, name):
    return obj.matrix_world @ obj.pose.bones[name].matrix

def basis_from(obj, world_fn):
    """Character frame: x = towards the character's left->right shoulders, z = up, y = x cross z (forward-ish)."""
    l, r = world_fn(obj, "LeftArm").translation, world_fn(obj, "RightArm").translation
    x = (r - l).normalized()
    z = Vector((0, 0, 1))
    y = z.cross(x).normalized()
    z = x.cross(y)
    return Matrix((x, y, z)).transposed()

# world alignment source -> target (rotation only), from both rest skeletons
scene.frame_set(first)
A = basis_from(tgt, rest_world) @ basis_from(src, rest_world).inverted()

def rot(m):
    return m.to_3x3().normalized()

# Bone axes differ between rigs (KCD2 bones point away from their child), so limb direction is measured
# joint-to-joint: from a bone's head to the head of the next bone in the chain.
def child_of(name):
    for side in ("Left", "Right"):
        chain = [f"{side}Shoulder", f"{side}Arm", f"{side}ForeArm", f"{side}Hand", f"{side}HandMiddle1"]
        if name in chain[:-1]:
            return chain[chain.index(name) + 1]
    if name[-1].isdigit():
        return name[:-1] + str(int(name[-1]) + 1)
    return None

def limb_dir(obj, name, world_fn):
    return (world_fn(obj, child_of(name)).translation - world_fn(obj, name).translation).normalized()

# per-bone rest correction: swing Henry's rest bone so its limb direction matches the aligned Mixamo rest direction
corrected_rest = {}
for name in ARM_BONES:
    t_rest = rot(rest_world(tgt, name))
    swing = limb_dir(tgt, name, rest_world).rotation_difference(A @ limb_dir(src, name, rest_world)).to_matrix()
    corrected_rest[name] = swing @ t_rest

# chest-relative transfer: target chest stays at rest (in game the native animation drives it)
Cs_rest = rot(rest_world(src, SRC_CHEST))
Ct_rest = rot(rest_world(tgt, TGT_CHEST))
M = Ct_rest.inverted() @ A @ Cs_rest  # source chest axes -> target chest axes

bpy.context.view_layer.objects.active = tgt
bpy.ops.object.mode_set(mode="POSE")
for pb in tgt.pose.bones:
    pb.rotation_mode = "QUATERNION"
inv_tgt_world = rot(tgt.matrix_world).inverted()

frames = list(range(first, last + 1))
for out_frame, f in enumerate(frames):
    scene.frame_set(f)
    Cs_pose = rot(pose_world(src, SRC_CHEST))
    for name in ARM_BONES:  # parents before children
        s_pose = rot(pose_world(src, name))
        s_rest = rot(rest_world(src, name))
        delta_c = Cs_pose.inverted() @ s_pose @ s_rest.inverted() @ Cs_rest   # chest-local delta
        delta_t = Ct_rest @ M @ delta_c @ M.inverted() @ Ct_rest.inverted()   # same delta around Henry's chest
        world_rot = delta_t @ corrected_rest[name]
        pb = tgt.pose.bones[name]
        bpy.context.view_layer.update()
        head = pb.matrix.translation.copy()
        pb.matrix = Matrix.LocRotScale(head, (inv_tgt_world @ world_rot).to_quaternion(), None)
        bpy.context.view_layer.update()
        pb.location = (0, 0, 0)
    for name in ARM_BONES:
        tgt.pose.bones[name].keyframe_insert("rotation_quaternion", frame=out_frame)
bpy.ops.object.mode_set(mode="OBJECT")
n_frames = len(frames)
# debug: hand/forearm positions relative to the chest, in each rig's character frame, at the first frame
scene.frame_set(first)
Bs, Bt = basis_from(src, pose_world), basis_from(tgt, pose_world)
for nm in ("RightArm", "RightForeArm", "RightHand", "LeftHand"):
    ps = Bs.inverted() @ (pose_world(src, nm).translation - pose_world(src, "RightArm").translation)
    print("DBG src", nm, tuple(round(v, 2) for v in ps))
scene.frame_set(0)
for nm in ("RightArm", "RightForeArm", "RightHand", "LeftHand"):
    pt = Bt.inverted() @ (pose_world(tgt, nm).translation - pose_world(tgt, "RightArm").translation)
    print("DBG tgt", nm, tuple(round(v, 2) for v in pt))
src.animation_data_clear()
print("RETARGETED", NAME, n_frames, "frames from", FBX)

# preview: Deagle frame on RightHandSlot, camera at Henry's eyes looking forward
scene.frame_set(0)
eye_bone = tgt.pose.bones["Head"]
head_m = tgt.matrix_world @ eye_bone.matrix
fwd = basis_from(tgt, rest_world).col[1]
cam = bpy.data.objects.new("eye", bpy.data.cameras.new("eye")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.lens = 18
cam.location = head_m.translation + Vector((0, 0, 0.08)) + fwd * 0.1
cam.rotation_euler = (fwd).to_track_quat("-Z", "Y").to_euler()
clay = bpy.data.materials.new("clay"); clay.diffuse_color = (0.75, 0.62, 0.52, 1)
for o in scene.objects:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(clay)
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 960, 540
world = bpy.data.worlds.new("w"); world.color = (0.35, 0.38, 0.42); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
sun.rotation_euler = (math.radians(40), 0, math.radians(30)); scene.collection.objects.link(sun)
for label, frame in (("fp_start", 0), ("fp_mid", n_frames // 2)):
    scene.frame_set(frame)
    scene.render.filepath = f"{PREVIEW}/{NAME}_{label}.png"
    bpy.ops.render.render(write_still=True)
side_cam = bpy.data.objects.new("side", bpy.data.cameras.new("side")); scene.collection.objects.link(side_cam)
right = basis_from(tgt, rest_world).col[0]
side_cam.location = head_m.translation + right * 2.4 + fwd * 0.4 + Vector((0, 0, -0.2))
side_cam.rotation_euler = (head_m.translation + Vector((0, 0, -0.4)) - side_cam.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = side_cam
scene.frame_set(0)
scene.render.filepath = f"{PREVIEW}/{NAME}_side.png"
bpy.ops.render.render(write_still=True)
print("PREVIEW DONE")

# export: i_caf node over the retargeted range; rc is run separately after filtering the DAE to arm bones only
for o in list(scene.objects):
    if o.type == "MESH" or o.type == "CAMERA" or o.type == "LIGHT" or o is src:
        bpy.data.objects.remove(o)
scene.frame_start, scene.frame_end = 0, n_frames - 1
bpy.ops.object.select_all(action="DESELECT")
tgt.select_set(True)
bpy.context.view_layer.objects.active = tgt
bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name=NAME, mode="Manual",
                                    range_type="Values", node_start=0, node_end=n_frames - 1)
bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=f"{OUTDIR}/{NAME}.dae", disable_rc=True, save_dae=True)
print("DAE EXPORTED", f"{OUTDIR}/{NAME}.dae")
