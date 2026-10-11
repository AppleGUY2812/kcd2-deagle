# Blender: retarget a Mixamo clip's arms + fingers onto Henry (KCD2 male skeleton), relative to the chest,
# preview it from Henry's eyes with the Deagle in hand, and export an upper-body i_caf DAE.
# usage: blender -b --factory-startup -P retarget.py -- <henry male_body.skin> <mixamo.fbx> <anim name>
#        <out dir for dae> <rc.exe> <deagle_frame source gltf> <preview dir> [first_frame last_frame]
import bpy, sys, math, mathutils, addon_utils
from mathutils import Matrix, Vector, Quaternion

argv = sys.argv[sys.argv.index("--") + 1:]
SKIN, FBX, NAME, OUTDIR, RC, GUN_GLTF, PREVIEW = argv[:7]
RANGE = None
OPTS = {}
for a in argv[7:]:
    if "=" in a:
        k, v = a.split("=", 1); OPTS[k] = float(v)
    elif RANGE is None:
        RANGE = ()
if OPTS.get("first") is not None:
    RANGE = (int(OPTS["first"]), int(OPTS["last"]))
# FPS framing: right wrist target relative to the Head bone, in Henry's character frame (metres)
FRAME_FWD, FRAME_RIGHT, FRAME_UP = OPTS.get("fwd"), OPTS.get("right", 0.06), OPTS.get("up", -0.30)
# Pitch variant: the game's chest does not follow view pitch, so each variant tilts the grip target (given in
# camera space as slot_fwd/right/up, relative to the camera at Head + 0.148 m) and the hands by the view pitch.
PITCH = math.radians(OPTS.get("pitch", 0.0))
SLOT_TARGET = (OPTS["slot_fwd"], OPTS.get("slot_right", 0.05), OPTS.get("slot_up", -0.11)) if "slot_fwd" in OPTS else None

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

# per-bone rest correction: rotate Henry's rest bone so both its limb direction AND its twist (measured against the
# across-the-palm vector index->pinky knuckles) match the aligned Mixamo rest bone. Swing-only left fingers/forearms rolled.
def palm_ref(obj, side, world_fn):
    return (world_fn(obj, f"{side}HandIndex1").translation - world_fn(obj, f"{side}HandPinky1").translation).normalized()

def frame_of(direction, ref):
    x = direction.normalized()
    y = (ref - x * ref.dot(x)).normalized()
    return Matrix((x, y, x.cross(y))).transposed()

corrected_rest = {}
for name in ARM_BONES:
    side = "Left" if name.startswith("Left") else "Right"
    t_rest = rot(rest_world(tgt, name))
    f_t = frame_of(limb_dir(tgt, name, rest_world), palm_ref(tgt, side, rest_world))
    f_s = frame_of(A @ limb_dir(src, name, rest_world), A @ palm_ref(src, side, rest_world))
    corrected_rest[name] = f_s @ f_t.inverted() @ t_rest

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
# ---- FPS framing: move both wrists by one offset (keeps the two-hand grip), re-solve elbows with 2-bone IK,
# keep the hands' world rotation so the gun still points where the mocap pointed it.
def set_world_rot(pb, world_rot):
    bpy.context.view_layer.update()
    head = pb.matrix.translation.copy()
    pb.matrix = Matrix.LocRotScale(head, (inv_tgt_world @ world_rot).to_quaternion(), None)
    bpy.context.view_layer.update()

def joint(name):
    return tgt.matrix_world @ tgt.pose.bones[name].head

def rest_joint(name):
    return (tgt.matrix_world @ tgt.data.bones[name].matrix_local).translation

def hinge_frame(direction, hinge):
    x = direction.normalized()
    z = (hinge - x * hinge.dot(x)).normalized()
    return Matrix((x, z.cross(x), z)).transposed()

def solve_arm(side, wrist_target, hand_turn=None):
    """2-bone IK that also fixes each segment's roll: the elbow hinge axis (taken from Henry's rest pose, where the
    elbow is slightly bent) is aligned with the normal of the new shoulder-elbow-wrist plane, so the elbow creases
    the right way instead of folding sideways."""
    up_b, fore_b, hand_b = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
    S, E, W = joint(up_b), joint(fore_b), joint(hand_b)
    a, b = (E - S).length, (W - E).length
    d_vec = wrist_target - S
    d = min(d_vec.length, a + b - 1e-3)
    n = d_vec.normalized()
    cos_a = max(-1.0, min(1.0, (a * a + d * d - b * b) / (2 * a * d)))
    pole = (E - S) - n * (E - S).dot(n)          # keep the elbow on the side the mocap had it, biased down
    pole = (pole.normalized() + Vector((0, 0, -0.6))).normalized()
    m = (pole - n * pole.dot(n)).normalized()
    E2 = S + a * (cos_a * n + math.sqrt(max(0.0, 1 - cos_a * cos_a)) * m)
    W2 = S + n * d
    hand_world = rot(tgt.matrix_world @ tgt.pose.bones[hand_b].matrix)
    if hand_turn is not None:
        hand_world = hand_turn @ hand_world
    # rest hinge: normal of the rest arm plane
    rS, rE, rW = rest_joint(up_b), rest_joint(fore_b), rest_joint(hand_b)
    hinge_rest = (rE - rS).cross(rW - rE)
    hinge_new = (E2 - S).cross(W2 - E2)
    if hinge_new.length < 1e-6:  # straight arm: keep the previous hinge
        hinge_new = (E - S).cross(W - E)
    for bone, r0, r1, p0, p1 in ((up_b, rS, rE, S, E2), (fore_b, rE, rW, E2, W2)):
        rest_rot = rot(rest_world(tgt, bone))
        new_rot = hinge_frame(p1 - p0, hinge_new) @ hinge_frame(r1 - r0, hinge_rest).inverted() @ rest_rot
        set_world_rot(tgt.pose.bones[bone], new_rot)
    # hand keeps its (pitched) world orientation so the gun keeps pointing where the camera looks
    set_world_rot(tgt.pose.bones[hand_b], hand_world)

if FRAME_FWD is not None or SLOT_TARGET is not None:
    Bt_rest = basis_from(tgt, rest_world)
    fwd_v, right_v, up_v = Bt_rest.col[1], Bt_rest.col[0], Bt_rest.col[2]
    head_pos = (tgt.matrix_world @ tgt.pose.bones["Head"].matrix).translation
    scene.frame_set(0)
    turn = Matrix.Rotation(-PITCH, 3, right_v)  # positive pitch = looking up (rotate forward towards up)
    if SLOT_TARGET is not None:
        cam_pos = head_pos + fwd_v * 0.148
        sf, sr, su = SLOT_TARGET
        target_slot = cam_pos + turn @ (fwd_v * sf + right_v * sr + up_v * su)
        offset = target_slot - (tgt.matrix_world @ tgt.pose.bones["RightHandSlot"].head)
    else:
        target_rw = head_pos + fwd_v * FRAME_FWD + right_v * FRAME_RIGHT + up_v * FRAME_UP
        offset = target_rw - (tgt.matrix_world @ tgt.pose.bones["RightHand"].head)
    print("FRAMING offset", tuple(round(v, 3) for v in offset))
    for out_frame in range(len(frames)):
        scene.frame_set(out_frame)
        for side in ("Right", "Left"):
            W = tgt.matrix_world @ tgt.pose.bones[f"{side}Hand"].head
            solve_arm(side, W + offset, turn if PITCH else None)
        for name in (f"{s}{b}" for s in ("Left", "Right") for b in ("Arm", "ForeArm", "Hand")):
            tgt.pose.bones[name].keyframe_insert("rotation_quaternion", frame=out_frame)

# Twist bones: upper-arm rolls follow the upper arm; forearm rolls blend from the forearm towards the hand
# (1/3, 2/3) so wrist twist spreads along the forearm; palm/slot/glove helpers follow their parent.
TWIST = {"ArmRoll": ("Arm", "Arm", 0.0), "ArmRoll1": ("Arm", "ForeArm", 0.5),
         "ForeArmRoll": ("ForeArm", "Hand", 0.0), "ForeArmRoll1": ("ForeArm", "Hand", 0.33), "ForeArmRoll2": ("ForeArm", "Hand", 0.66)}
HELPERS = ["Hand_Glove", "InHandPinky", "InHandRing", "HandSlot"]
TWIST_BONES = [f"{s}{b}" for s in ("Left", "Right") for b in list(TWIST) + HELPERS]

def carried(bone, driver):
    """world rotation the bone would have if rigidly carried by driver from rest"""
    return rot(tgt.matrix_world @ tgt.pose.bones[driver].matrix) @ rot(rest_world(tgt, driver)).inverted() @ rot(rest_world(tgt, bone))

for out_frame in range(len(frames)):
    scene.frame_set(out_frame)
    for side in ("Left", "Right"):
        for b, (d0, d1, k) in TWIST.items():
            name = f"{side}{b}"
            if name not in tgt.pose.bones:
                continue
            q0, q1 = carried(name, f"{side}{d0}").to_quaternion(), carried(name, f"{side}{d1}").to_quaternion()
            set_world_rot(tgt.pose.bones[name], q0.slerp(q1, k).to_matrix())
        for h in HELPERS:
            name = f"{side}{h}"
            if name in tgt.pose.bones:
                tgt.pose.bones[name].rotation_quaternion = (1, 0, 0, 0)
    for name in TWIST_BONES:
        if name in tgt.pose.bones:
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
cam.data.sensor_fit = "VERTICAL"; cam.data.angle = 1.1011  # game camera fov (rad, measured)
cam.location = head_m.translation + fwd * 0.148  # measured in game: camera 0.148 m ahead of the Head bone
cam.rotation_euler = (fwd).to_track_quat("-Z", "Y").to_euler()
RENDER = PREVIEW != "none"
clay = bpy.data.materials.new("clay"); clay.diffuse_color = (0.75, 0.62, 0.52, 1)
for o in scene.objects:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(clay)
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 960, 540
world = bpy.data.worlds.new("w"); world.color = (0.35, 0.38, 0.42); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
sun.rotation_euler = (math.radians(40), 0, math.radians(30)); scene.collection.objects.link(sun)
# Deagle in the preview: grip pivot (same as build_model.py) placed on RightHandSlot, muzzle along the camera's
# forward, upright. The slot-relative transform is what the Lua attachment offset (handPos/handAng) must be.
if GUN_GLTF and GUN_GLTF != "none":
    before = set(scene.objects)
    bpy.ops.import_scene.gltf(filepath=GUN_GLTF)
    gun_meshes = [o for o in scene.objects if o not in before and o.type == "MESH"]
    for o in gun_meshes:
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    for o in [o for o in scene.objects if o not in before and o.type == "EMPTY"]:
        bpy.data.objects.remove(o)
    def bb(objs):
        pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
        return (Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts))),
                Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts))))
    grip = [o for o in gun_meshes if o.name.startswith("Grip_low")]
    barrel = [o for o in gun_meshes if o.name.startswith("Barrel_low")]
    gmn, gmx = bb(grip); bmn, bmx = bb(barrel)
    pivot = Vector(((gmn.x + gmx.x) / 2, (gmn.y + gmx.y) / 2, gmx.z - (gmx.z - gmn.z) / 3))
    muzzle_sign = 1 if (bmx.y - pivot.y) > (pivot.y - bmn.y) else -1
    to_model = Matrix.Scale(0.01, 4) @ (Matrix.Identity(4) if muzzle_sign > 0 else Matrix.Rotation(math.pi, 4, "Z")) @ Matrix.Translation(-pivot)
    for o in gun_meshes:
        o.data.transform(o.matrix_world); o.matrix_world = Matrix.Identity(4)
        o.data.transform(to_model)
    gun = bpy.data.objects.new("deagle_preview", None); scene.collection.objects.link(gun)
    for o in gun_meshes:
        o.parent = gun
    scene.frame_set(0)
    slot = tgt.matrix_world @ tgt.pose.bones["RightHandSlot"].matrix
    gun_rot = Matrix((basis_from(tgt, rest_world).col[0], fwd, Vector((0, 0, 1)))).transposed()  # x right, y fwd, z up
    grip_adjust = Vector((0, 0, OPTS.get("gun_up", 0.022))) + fwd * OPTS.get("gun_fwd", 0.005)  # fist on the grip, not the frame
    gun.matrix_world = Matrix.LocRotScale(slot.translation + grip_adjust, gun_rot.to_quaternion(), None)
    rel = slot.inverted() @ gun.matrix_world
    e = rel.to_euler("XYZ")
    print("HANDOFFSET pos", tuple(round(v, 4) for v in rel.translation), "ang_deg", tuple(round(math.degrees(a), 1) for a in e))
for label, frame in (("fp_start", 0), ("fp_mid", n_frames // 2)):
    scene.frame_set(frame)
    scene.render.filepath = f"{PREVIEW}/{NAME}_{label}.png"
    RENDER and bpy.ops.render.render(write_still=True)
# close-ups of the grip from the right and from above-left
scene.frame_set(0)
slot_pos = (tgt.matrix_world @ tgt.pose.bones["RightHandSlot"].matrix).translation
right_v = basis_from(tgt, rest_world).col[0]
for label, offset in (("grip_right", right_v * 0.45 + Vector((0, 0, 0.05))), ("grip_left", -right_v * 0.4 + Vector((0, 0, 0.25)) + fwd * 0.1)):
    c = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(c)
    c.location = slot_pos + offset
    c.rotation_euler = (slot_pos - c.location).to_track_quat("-Z", "Y").to_euler()
    c.data.lens = 50
    scene.camera = c
    scene.render.filepath = f"{PREVIEW}/{NAME}_{label}.png"
    RENDER and bpy.ops.render.render(write_still=True)
# elbow close-ups (outside of each arm, slightly below)
for side_name, sgn in (("Right", 1), ("Left", -1)):
    el = (tgt.matrix_world @ tgt.pose.bones[f"{side_name}ForeArm"].matrix).translation
    c = bpy.data.objects.new(f"elbow{side_name}", bpy.data.cameras.new("e")); scene.collection.objects.link(c)
    c.location = el + right_v * (0.5 * sgn) + Vector((0, 0, -0.15)) - fwd * 0.1
    c.rotation_euler = (el - c.location).to_track_quat("-Z", "Y").to_euler(); c.data.lens = 40
    scene.camera = c
    scene.render.filepath = f"{PREVIEW}/{NAME}_elbow_{side_name.lower()}.png"
    RENDER and bpy.ops.render.render(write_still=True)
side_cam = bpy.data.objects.new("side", bpy.data.cameras.new("side")); scene.collection.objects.link(side_cam)
right = basis_from(tgt, rest_world).col[0]
side_cam.location = head_m.translation + right * 2.4 + fwd * 0.4 + Vector((0, 0, -0.2))
side_cam.rotation_euler = (head_m.translation + Vector((0, 0, -0.4)) - side_cam.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = side_cam
scene.frame_set(0)
scene.render.filepath = f"{PREVIEW}/{NAME}_side.png"
RENDER and bpy.ops.render.render(write_still=True)
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
