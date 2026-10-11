# Blender: fit Henry's hands to the Deagle's actual geometry (no clipping), on the exact-frame rig.
#   right hand: palm against the back strap, middle/ring/pinky curled around the front strap, index on the
#               trigger, thumb along the left of the frame; left hand: palm on the right hand's fingers,
#               fingers wrapped around, thumb forward along the frame.
# Fingers curl joint by joint until a capsule around each segment touches the gun (BVH), so they cannot clip.
# Writes grip.json (wrist transforms relative to the gun + local rotations of every hand bone) and close-ups.
# usage: blender -b --factory-startup -P grip_fit.py -- <body_dir> <deagle gltf> <out json> <render dir>
import bpy, bmesh, sys, os, json, math
from mathutils import Matrix, Vector, Quaternion
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
BODY, GUN_GLTF, OUT_JSON, RENDER = argv[:4]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import henry_rig

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
arm = henry_rig.import_henry([os.path.join(BODY, f) for f in ("male_body.glb", "male_body_arms.glb", "male_body_hands.glb")])
for o in [o for o in scene.objects if o.type == "MESH" and o.name.startswith("Icosphere")]:
    bpy.data.objects.remove(o)

# ---------------- gun, in its model space (metres, muzzle +Y, up +Z, grip pivot at origin) ----------------
before = set(scene.objects)
bpy.ops.import_scene.gltf(filepath=GUN_GLTF)
gun_objs = [o for o in scene.objects if o not in before and o.type == "MESH"]
for o in gun_objs:
    mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
for o in [o for o in scene.objects if o not in before and o.type == "EMPTY"]:
    bpy.data.objects.remove(o)
def bbox(objs):
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    return (Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)]))
part = lambda prefix: [o for o in gun_objs if o.name.startswith(prefix)]
gmn, gmx = bbox(part("Grip_low")); bmn, bmx = bbox(part("Barrel_low"))
pivot = Vector(((gmn.x + gmx.x) / 2, (gmn.y + gmx.y) / 2, gmx.z - (gmx.z - gmn.z) / 3))
flip = Matrix.Identity(4) if (bmx.y - pivot.y) > (pivot.y - bmn.y) else Matrix.Rotation(math.pi, 4, "Z")
to_model = Matrix.Scale(0.01, 4) @ flip @ Matrix.Translation(-pivot)
for o in gun_objs:
    o.data.transform(o.matrix_world); o.matrix_world = Matrix.Identity(4); o.data.transform(to_model)
for o in [o for o in gun_objs if o.name.startswith(("Bullet_low", "BulletCase_low"))]:
    bpy.data.objects.remove(o)
gun_objs = [o for o in scene.objects if o.type == "MESH" and o not in arm.children]
gun_root = bpy.data.objects.new("gun", None); scene.collection.objects.link(gun_root)
for o in gun_objs:
    o.parent = gun_root

# grip / trigger landmarks in gun space
def mesh_bbox(objs):  # from vertex data (bound_box is stale after data.transform)
    pts = [v.co for o in objs for v in o.data.vertices]
    return (Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)]))
g_mn, g_mx = mesh_bbox([o for o in gun_objs if o.name.startswith("Grip_low")])
t_mn, t_mx = mesh_bbox([o for o in gun_objs if o.name.startswith("Trigger_low")])
trigger = Vector(((t_mn.x + t_mx.x) / 2, t_mn.y + (t_mx.y - t_mn.y) * 0.35, (t_mn.z + t_mx.z) / 2))
print("GRIP bbox", tuple(round(v, 3) for v in g_mn), tuple(round(v, 3) for v in g_mx), "trigger", tuple(round(v, 3) for v in trigger))

# place the gun in front of Henry's right shoulder, muzzle forward (Henry faces -Y on this rig)
fwd = Vector((0, -1, 0)); up = Vector((0, 0, 1)); right = fwd.cross(up)  # Henry's right
gun_rot = Matrix((right, fwd, up)).transposed()  # gun x->right, y->forward, z->up
shoulder = (arm.matrix_world @ arm.data.bones["RightArm"].matrix_local).translation
gun_root.matrix_world = Matrix.LocRotScale(shoulder + fwd * 0.42 + up * -0.12 + right * -0.12, gun_rot.to_quaternion(), None)
bpy.context.view_layer.update()
G = gun_root.matrix_world

def gun_bvh():
    bm = bmesh.new()
    for o in gun_objs:
        me = o.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
        tmp = bmesh.new(); tmp.from_mesh(me); tmp.transform(o.matrix_world)
        m2 = bpy.data.meshes.new("t"); tmp.to_mesh(m2); bm.from_mesh(m2); tmp.free()
    return BVHTree.FromBMesh(bm)
BVH = gun_bvh()

# ---------------- hand placement (wrist from palm frame) ----------------
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="POSE")
for pb in arm.pose.bones:
    pb.rotation_mode = "QUATERNION"
PB = arm.pose.bones
def jw(name):
    return arm.matrix_world @ PB[name].head
def bw(name):
    return arm.matrix_world @ PB[name].matrix
def set_world_rot(pb, R):
    bpy.context.view_layer.update()
    head = pb.matrix.translation.copy()
    pb.matrix = Matrix.LocRotScale(head, (arm.matrix_world.to_3x3().inverted() @ R).to_quaternion(), None)
    bpy.context.view_layer.update()

def palm_frame(side):
    """right-handed hand frame: y = along the palm (wrist->middle knuckle), z = palm normal (the side fingers
    curl to), x = y cross z. (index->pinky is +x for one hand and -x for the other, so it is not used as an axis.)"""
    w, m1 = jw(f"{side}Hand"), jw(f"{side}HandMiddle1")
    i1, p1 = jw(f"{side}HandIndex1"), jw(f"{side}HandPinky1")
    y = (m1 - w).normalized()
    across = (p1 - i1); across = (across - y * across.dot(y)).normalized()
    z = across.cross(y)
    tip = jw(f"{side}HandMiddle3")
    if (tip - m1).dot(z) < 0:
        z = -z
    return Matrix((y.cross(z), y, z)).transposed()

def frame_yz(y, z):
    y = y.normalized(); z = (z - y * z.dot(y)).normalized()
    return Matrix((y.cross(z), y, z)).transposed()

def solve_arm_to(side, wrist_pos, wrist_rot):
    """2-bone IK with elbow down/out, then hand rotation"""
    up_b, fore_b, hand_b = f"{side}Arm", f"{side}ForeArm", f"{side}Hand"
    S, E, W = jw(up_b), jw(fore_b), jw(hand_b)
    a, b = (E - S).length, (W - E).length
    d_vec = wrist_pos - S; d = min(d_vec.length, a + b - 1e-3); n = d_vec.normalized()
    cos_a = max(-1, min(1, (a * a + d * d - b * b) / (2 * a * d)))
    out = right if side == "Right" else -right
    pole = (out * 0.6 + Vector((0, 0, -1))).normalized()
    m = (pole - n * pole.dot(n)).normalized()
    E2 = S + a * (cos_a * n + math.sqrt(max(0, 1 - cos_a * cos_a)) * m); W2 = S + n * d
    rS, rE, rW = [(arm.matrix_world @ arm.data.bones[x].matrix_local).translation for x in (up_b, fore_b, hand_b)]
    hinge_rest = (rE - rS).cross(rW - rE); hinge_new = (E2 - S).cross(W2 - E2)
    def hf(dv, h):
        x = dv.normalized(); z = (h - x * h.dot(x)).normalized(); return Matrix((x, z.cross(x), z)).transposed()
    for bone, r0, r1, p0, p1 in ((up_b, rS, rE, S, E2), (fore_b, rE, rW, E2, W2)):
        rest = (arm.matrix_world @ arm.data.bones[bone].matrix_local).to_3x3().normalized()
        set_world_rot(PB[bone], hf(p1 - p0, hinge_new) @ hf(r1 - r0, hinge_rest).inverted() @ rest)
    set_world_rot(PB[hand_b], wrist_rot)

def place_hand(side, palm_target_rot, palm_point, palm_offset):
    """rotate the hand so its palm frame matches palm_target_rot, wrist positioned so that the palm centre
    (wrist + palm_offset in palm frame) lands on palm_point"""
    hand_now = bw(f"{side}Hand").to_3x3().normalized()
    pf = palm_frame(side)
    hand_rot = palm_target_rot @ pf.inverted() @ hand_now
    wrist = palm_point - palm_target_rot @ palm_offset
    for _ in range(3):
        solve_arm_to(side, wrist, hand_rot)
    return wrist, hand_rot

gx, gy, gz = G.to_3x3().col[0].normalized(), G.to_3x3().col[1].normalized(), G.to_3x3().col[2].normalized()
grip_axis = (gz * 1.0 - gy * 0.28).normalized()  # Deagle grip rakes back ~15 deg
# right palm: knuckle line along the grip axis (downwards), fingers point to the gun's left, palm faces left
hand_fwd = (gy - grip_axis * gy.dot(grip_axis)).normalized()     # perpendicular to the grip, towards the muzzle
R_palm = frame_yz(hand_fwd * 0.75 - gx * 0.25, -gx)               # fingers forward (slightly across), palm faces left
back_strap = G @ Vector((g_mx.x * 0.6, g_mn.y + 0.012, g_mx.z - 0.03))  # right side of the grip, near the back strap
pf0 = palm_frame("Right")
palm_len = (jw("RightHandMiddle1") - jw("RightHand")).length
wristR, rotR = place_hand("Right", R_palm, back_strap + gx * 0.012, Vector((0, palm_len * 0.45, 0.016)))
print("RIGHT wrist", tuple(round(v, 3) for v in wristR))
bpy.context.view_layer.update()

# ---------------- finger curl with collision ----------------
RADIUS = {"Thumb": 0.0105, "Index": 0.0095, "Middle": 0.0098, "Ring": 0.0092, "Pinky": 0.0082}
def seg_clear(a, b, r):
    for t in (0.0, 0.25, 0.5, 0.75, 1.0):
        p = a.lerp(b, t)
        hit = BVH.find_nearest(p)
        if hit[0] is not None and hit[3] < r:
            return False
    return True

def curl_finger(side, finger, hinge_world, max_deg=(95, 105, 85), stop_target=None):
    """rotate joints 1..3 about hinge_world in 2-degree steps until the segment would touch the gun"""
    r = RADIUS[finger]
    for j in (1, 2, 3):
        bone = f"{side}Hand{finger}{j}"; child = f"{side}Hand{finger}{j + 1}"
        step = math.radians(2)
        for _ in range(int(max_deg[j - 1] / 2)):
            cur = bw(bone).to_3x3().normalized()
            nxt = Matrix.Rotation(step, 3, hinge_world) @ cur
            set_world_rot(PB[bone], nxt)
            a, b = jw(bone), jw(child)
            if not seg_clear(a, b, r) or (stop_target is not None and (b - stop_target).length < 0.006):
                set_world_rot(PB[bone], cur)
                break

hands_mesh = next(o for o in arm.children if o.type == "MESH" and o.name.startswith("hands"))
def penetration(side):
    """deepest hand-skin vertex inside the gun (metres), using the evaluated (skinned) mesh"""
    deps = bpy.context.evaluated_depsgraph_get()
    ev = hands_mesh.evaluated_get(deps); me = ev.to_mesh()
    gi = hands_mesh.vertex_groups
    side_groups = {g.index for g in gi if g.name.startswith(side)}
    worst = 0.0
    for v in me.vertices:
        if not any(g.group in side_groups and g.weight > 0.3 for g in hands_mesh.data.vertices[v.index].groups):
            continue
        p = hands_mesh.matrix_world @ v.co
        loc, nrm, idx, dist = BVH.find_nearest(p)
        if loc is not None and (p - loc).dot(nrm) < 0:  # behind the surface = inside
            worst = max(worst, dist)
    ev.to_mesh_clear()
    return worst

def push_out(side, direction, wrist, rot):
    for i in range(30):
        pen = penetration(side)
        if pen < 0.0015:
            break
        wrist = wrist + direction * max(0.002, pen * 0.6)
        solve_arm_to(side, wrist, rot)
    print("PUSH", side, "residual", round(penetration(side), 4), "steps", i)
    return wrist

pfR = palm_frame("Right")
hingeR = pfR.col[0]  # flexion: rotation about x turns y (finger) towards z (palm)
for f in ("Middle", "Ring", "Pinky"):
    curl_finger("Right", f, hingeR)
curl_finger("Right", "Index", hingeR, max_deg=(70, 80, 60), stop_target=G @ trigger)
curl_finger("Right", "Thumb", (pfR.col[1] + pfR.col[2] * 0.3).normalized(), max_deg=(40, 50, 40))
wristR = push_out("Right", gx, wristR, bw("RightHand").to_3x3().normalized())

# ---------------- left hand: palm on the right fingers, thumb forward ----------------
right_fingers = (jw("RightHandMiddle2") + jw("RightHandRing2")) / 2
L_palm = frame_yz(hand_fwd * 0.8 + gx * 0.2 - grip_axis * 0.3, gx)  # fingers forward/under, palm faces the gun's right
lp_len = (jw("LeftHandMiddle1") - jw("LeftHand")).length
wristL, rotL = place_hand("Left", L_palm, right_fingers - gx * 0.035, Vector((0, lp_len * 0.45, 0.016)))
bpy.context.view_layer.update()
BVH = gun_bvh()  # (left fingers also must not enter the gun)
pfL = palm_frame("Left")
for f in ("Index", "Middle", "Ring", "Pinky"):
    curl_finger("Left", f, pfL.col[0])
curl_finger("Left", "Thumb", (pfL.col[1] + pfL.col[2] * 0.3).normalized(), max_deg=(30, 30, 30))
wristL = push_out("Left", -gx, wristL, bw("LeftHand").to_3x3().normalized())
bpy.context.view_layer.update()

# ---------------- save: wrists relative to the gun + local rotations of hand bones ----------------
hand_bones = [b.name for b in arm.data.bones if b.name.startswith(("RightHand", "LeftHand", "RightInHand", "LeftInHand"))]
Gi = G.inverted()
data = {"right_wrist_in_gun": [list(r) for r in (Gi @ bw("RightHand"))], "left_wrist_in_gun": [list(r) for r in (Gi @ bw("LeftHand"))],
        "slot_in_gun": [list(r) for r in (Gi @ bw("RightHandSlot"))],
        "local_rot": {n: list(PB[n].rotation_quaternion) for n in hand_bones}}
json.dump(data, open(OUT_JSON, "w"), indent=1)
print("GRIP SAVED", OUT_JSON)

# ---------------- close-up renders ----------------
bpy.ops.object.mode_set(mode="OBJECT")
skin = bpy.data.materials.new("skin"); skin.diffuse_color = (0.85, 0.66, 0.55, 1)
metal = bpy.data.materials.new("metal"); metal.diffuse_color = (0.25, 0.27, 0.3, 1); metal.metallic = 0.6
for o in arm.children:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(skin)
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 900, 700
world = bpy.data.worlds.new("w"); world.color = (0.55, 0.6, 0.65); scene.world = world
for rot in ((0.7, 0.2, 0.5), (-0.6, 0.4, -2.2)):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); s.data.energy = 3; s.rotation_euler = rot; scene.collection.objects.link(s)
centre = G @ Vector((0, 0.02, -0.01))
views = {"A right side": gx * 0.32 + gz * 0.04, "B left side": -gx * 0.32 + gz * 0.04, "C from below": -gz * 0.3 + gy * 0.05 + gx * 0.05,
         "D shooter view": -gy * 0.35 + gz * 0.12 + gx * 0.04}
for label, off in views.items():
    c = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(c)
    c.location = centre + off; c.rotation_euler = (centre - c.location).to_track_quat("-Z", "Z" if "below" in label else "Y").to_euler()
    c.data.lens = 45; scene.camera = c
    scene.render.filepath = os.path.join(RENDER, f"grip {label}.png")
    bpy.ops.render.render(write_still=True)
print("GRIP RENDERS DONE")
