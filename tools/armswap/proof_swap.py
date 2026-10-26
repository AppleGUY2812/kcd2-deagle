# Proof render: Henry's own clothing (sleeves, arm plates, gauntlets) on the Grizzly's arm pose, from the in-game camera.
# Opens on ron_grizzly_vm.blend. Henry's arm + finger bones copy the RoN arms' pose (direction-matched rest, then the RoN
# rotation delta), the wrists are pulled onto RoN's wrists by an analytic 2-bone IK, the hands take RoN's hand frame.
# Renders: A = RoN arms (today), B = Henry's clothing on the same pose, C = both overlaid (Henry's clothing in red wire).
#   blender -b ron_grizzly_vm.blend -P proof_swap.py -- <out dir> [action] [frame]
import bpy, sys, os, math
from mathutils import Matrix, Vector, Quaternion
argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[0]
ACTION = argv[1] if len(argv) > 1 else "@grizzly_idle"
FRAME = int(argv[2]) if len(argv) > 2 else 1
sys.path.insert(0, r"\\wsl.localhost\Ubuntu\home\johnpork\kcd2-deagle\tools")
import henry_rig

scene = bpy.context.scene
ron = bpy.data.objects["ron_grizzly_vm"]
ron.animation_data_create()
ron.animation_data.action = bpy.data.actions[ACTION]
scene.frame_set(FRAME)

BODY = r"E:\Tools\deagle-work\henry\Objects\characters\humans\male\body"
h = henry_rig.import_henry([os.path.join(BODY, f) for f in ("male_body.glb", "male_body_arms.glb", "male_body_hands.glb")])
for o in [o for o in bpy.data.objects if o.type == "MESH" and o.parent == h]:
    bpy.data.objects.remove(o)   # bare body: the clothing covers the arms

CLOTH = r"E:\Tools\deagle-work\henry\Objects\characters\humans\male\clothing"
cloth = []
for f in (r"hands\gauntlets\gauntlets01\gauntlets01.dae", r"arms\arm_armour\arm_brigandine\brigandinearm05\brigandinearm05.dae",
          r"arms\arms_splited\gambesonlong03_arms\gambesonlong03_arms.dae"):
    before = set(bpy.data.objects)
    bpy.ops.wm.collada_import(filepath=os.path.join(CLOTH, f))
    new = [o for o in bpy.data.objects if o not in before]
    for o in new:
        if o.type == "MESH":
            mw = o.matrix_world.copy()
            o.parent = None
            o.matrix_world = mw
            for m in [m for m in o.modifiers if m.type == "ARMATURE"]:
                print("PROOF modifier", o.name, "envelopes", m.use_bone_envelopes, "vgroups", m.use_vertex_groups, "multi", m.use_multi_modifier)
                m.object = h
                m.use_bone_envelopes = False
                m.use_vertex_groups = True
            print("PROOF parent_type was", o.name, o.parent_type, "basis", [round(x, 3) for x in mw.to_euler()], [round(x, 3) for x in mw.translation], "scale", [round(x, 3) for x in mw.to_scale()])
            o.parent = h
            o.parent_type = "OBJECT"
            o.matrix_parent_inverse = Matrix.Identity(4)
            for v in o.data.vertices: v.co.x, v.co.y = -v.co.x, -v.co.y   # the converter's mesh is turned 180 deg about Z (the toolkit yaw)
            o.data.update()
            cloth.append(o)
    for o in new:
        if o.type == "ARMATURE":
            bpy.data.objects.remove(o)

# --- frames -------------------------------------------------------------------------------------------------------
G = Matrix.Rotation(math.radians(90), 4, "Z")   # Henry faces -Y, right = -X; the RoN rig faces +X, right = -Y
def ron_pose(b): return ron.matrix_world @ ron.pose.bones[b].matrix
def ron_rest(b): return ron.matrix_world @ ron.data.bones[b].matrix_local
def hen_rest(b): return h.matrix_world @ h.data.bones[b].matrix_local
def hen_pose(b): return h.matrix_world @ h.pose.bones[b].matrix

# root: Henry's shoulder midpoint onto RoN's (posed) shoulder midpoint
h.matrix_world = G
bpy.context.view_layer.update()
mid_h = (hen_rest("RightArm").translation + hen_rest("LeftArm").translation) / 2
mid_r = (ron_pose("upperarm_RI").translation + ron_pose("upperarm_LE").translation) / 2
h.matrix_world = Matrix.Translation(mid_r - mid_h) @ G
bpy.context.view_layer.update()

if os.environ.get("PROOF_REST"):
    cam_data = bpy.data.cameras.new("dbgcam"); cam = bpy.data.objects.new("dbgcam", cam_data); scene.collection.objects.link(cam); scene.camera = cam
    cam_data.type = "ORTHO"; cam_data.ortho_scale = 2.0
    scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 800, 450
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun); sun.data.energy = 3
    w = bpy.data.worlds.new("w"); w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.37, 0.4, 1); scene.world = w
    rm = bpy.data.materials.new("r"); rm.use_nodes = True; rm.node_tree.nodes["Principled BSDF"].inputs[0].default_value = (0.8, 0.05, 0.05, 1)
    for o in cloth: o.data.materials.clear(); o.data.materials.append(rm)
    cam_data.ortho_scale = 3.0
    for nm, rot, loc in (("R_side.png", (math.radians(90), 0, math.radians(180)), (-0.2, 2.0, -0.4)), ("R_top.png", (0, 0, math.radians(-90)), (-0.2, 0.0, 2.0))):
        cam.matrix_world = Matrix.LocRotScale(Vector(loc), Matrix(Matrix.Rotation(rot[2], 3, "Z") @ Matrix.Rotation(rot[0], 3, "X")).to_quaternion(), None)
        scene.render.filepath = os.path.join(OUT, nm); bpy.ops.render.render(write_still=True)
    raise SystemExit
# (henry bone, ron bone, henry next joint, ron next joint)
def side(H, R):
    m = [(H + "Shoulder", "clavicle_" + R, H + "Arm", "upperarm_" + R),
         (H + "Arm", "upperarm_" + R, H + "ForeArm", "lowerarm_" + R),
         (H + "ForeArm", "lowerarm_" + R, H + "Hand", "hand_" + R),
         (H + "Hand", "hand_" + R, H + "HandMiddle1", "middle_2_" + R)]
    m += [(H + "HandThumb1", "thumb_1_" + R, H + "HandThumb2", "thumb_2_" + R),
          (H + "HandThumb2", "thumb_2_" + R, H + "HandThumb3", "thumb_3_" + R),
          (H + "HandThumb3", "thumb_3_" + R, H + "HandThumb4", "thumb_4_end_" + R)]
    for f in ("Index", "Middle"):
        r = f.lower()
        m += [(H + "Hand%s1" % f, "%s_2_%s" % (r, R), H + "Hand%s2" % f, "%s_3_%s" % (r, R)),
              (H + "Hand%s2" % f, "%s_3_%s" % (r, R), H + "Hand%s3" % f, "%s_4_%s" % (r, R)),
              (H + "Hand%s3" % f, "%s_4_%s" % (r, R), H + "Hand%s4" % f, "%s_5_end_%s" % (r, R))]
    for f in ("Ring", "Pinky"):
        r = f.lower()
        m += [(H + "InHand" + f, "%s_1_%s" % (r, R), H + "Hand%s1" % f, "%s_2_%s" % (r, R)),
              (H + "Hand%s1" % f, "%s_2_%s" % (r, R), H + "Hand%s2" % f, "%s_3_%s" % (r, R)),
              (H + "Hand%s2" % f, "%s_3_%s" % (r, R), H + "Hand%s3" % f, "%s_4_%s" % (r, R)),
              (H + "Hand%s3" % f, "%s_4_%s" % (r, R), H + "Hand%s4" % f, "%s_5_end_%s" % (r, R))]
    return m
MAP = side("Right", "RI") + side("Left", "LE")
missing = [x for m in MAP for x in (m[0], m[2]) if x not in h.data.bones] + [x for m in MAP for x in (m[1], m[3]) if x not in ron.data.bones]
print("PROOF missing bones", missing)
MAP = [m for m in MAP if not any(x in missing for x in m)]

def target_rot(hb, rb, hn, rn):
    dh = (hen_rest(hn).translation - hen_rest(hb).translation).normalized()
    dr = (ron_rest(rn).translation - ron_rest(rb).translation).normalized()
    S = dh.rotation_difference(dr)
    q_hr = S @ hen_rest(hb).to_quaternion()
    return ron_pose(rb).to_quaternion() @ ron_rest(rb).to_quaternion().inverted() @ q_hr

def set_world_rot(hb, q):
    pb = h.pose.bones[hb]
    loc = hen_pose(hb).translation
    pb.matrix = h.matrix_world.inverted() @ Matrix.LocRotScale(loc, q, None)
    bpy.context.view_layer.update()

def swing(hb, frm, to):
    q = frm.normalized().rotation_difference(to.normalized()) @ hen_pose(hb).to_quaternion()
    set_world_rot(hb, q)

report = []
for H, R in (("Right", "RI"), ("Left", "LE")):
    rows = [m for m in MAP if m[0].startswith(H)]
    for m in rows:
        set_world_rot(m[0], target_rot(*m))
    # 2-bone IK: wrist onto RoN's wrist, keeping the bend plane
    S = hen_pose(H + "Arm").translation; E = hen_pose(H + "ForeArm").translation; W = hen_pose(H + "Hand").translation
    T = ron_pose("hand_" + R).translation
    a, b = (E - S).length, (W - E).length
    d = min((T - S).length, a + b - 1e-4)
    u = (T - S).normalized()
    v = (E - S) - u * (E - S).dot(u)
    v = v.normalized() if v.length > 1e-6 else Vector((0, 0, -1))
    x = (a * a - b * b + d * d) / (2 * d)
    E2 = S + u * x + v * math.sqrt(max(a * a - x * x, 0))
    swing(H + "Arm", E - S, E2 - S)
    E = hen_pose(H + "ForeArm").translation; W = hen_pose(H + "Hand").translation
    swing(H + "ForeArm", W - E, T - E)
    for m in rows:   # hand and fingers again: their world frames are RoN's, whatever the IK did above them
        if m[0] in (H + "Shoulder", H + "Arm", H + "ForeArm"):
            continue
        set_world_rot(m[0], target_rot(*m))
    report.append("%s wrist miss %.1f mm (arm %.3f + %.3f m, needs %.3f m)" % (H, (hen_pose(H + "Hand").translation - T).length * 1000, a, b, (T - S).length))
print("PROOF", report)

# --- camera + render ----------------------------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_data); scene.collection.objects.link(cam)
cam_data.sensor_fit = "VERTICAL"; cam_data.angle = 1.101; cam_data.clip_start = 0.01
cpos = ron_pose("fp_camera").translation
cam.matrix_world = Matrix.Translation(cpos) @ Matrix.Rotation(math.radians(-90), 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")
scene.camera = cam
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 1600, 900
scene.render.resolution_percentage = 100
w = bpy.data.worlds.new("w"); w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.37, 0.4, 1); scene.world = w
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun)
sun.data.energy = 3; sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(-30))
ron_arms = bpy.data.objects["ron_arms"]
red = bpy.data.materials.new("cloth_clay"); red.diffuse_color = (0.55, 0.42, 0.33, 1)
for o in cloth:
    if not o.data.materials or not any(m and m.node_tree and any(n.type == "TEX_IMAGE" and n.image for n in m.node_tree.nodes) for m in o.data.materials if m):
        o.data.materials.clear(); o.data.materials.append(red)

def shot(name, show_ron, show_cloth):
    ron_arms.hide_render = not show_ron
    for o in cloth: o.hide_render = not show_cloth
    scene.render.filepath = os.path.join(OUT, name)
    bpy.ops.render.render(write_still=True)
dg = bpy.context.evaluated_depsgraph_get()
for o in cloth:
    e = o.evaluated_get(dg); pts = [e.matrix_world @ v.co for v in e.data.vertices]
    print("PROOF cloth", o.name, len(pts), "min", tuple(round(min(p[i] for p in pts),3) for i in range(3)), "max", tuple(round(max(p[i] for p in pts),3) for i in range(3)), "mw", tuple(round(v,3) for v in o.matrix_world.translation), "mods", [(m.type, m.object.name if m.object else None) for m in o.modifiers], "coll", [c.name for c in o.users_collection], "hide", o.hide_render, o.hide_get())
print("PROOF hand_RI", tuple(round(v,3) for v in ron_pose("hand_RI").translation), "RightHand", tuple(round(v,3) for v in hen_pose("RightHand").translation), "cam", tuple(round(v,3) for v in cpos))
shot("A_ron_arms.png", True, False)
shot("B_henry_clothing.png", False, True)
# pulled back: 35 cm behind, 10 cm up, a bit right, aimed at the gun, so the sleeves show
gunp = ron_pose("hand_RI").translation
eye2 = gunp + Vector((0.28, -0.32, 0.12))
fwd = (gunp - eye2).normalized()
cam.matrix_world = Matrix.LocRotScale(eye2, fwd.to_track_quat("-Z", "Z"), None)
shot("F_ron_back.png", True, False)
shot("G_henry_back.png", False, True)
red.use_nodes = True
red.node_tree.nodes["Principled BSDF"].inputs[0].default_value = (0.8, 0.05, 0.05, 1)
for o in cloth: o.data.materials.clear(); o.data.materials.append(red)
for nm, rot, loc in (("D_side.png", (math.radians(90), 0, math.radians(180)), (-0.2, 2.0, -0.4)), ("E_top.png", (0, 0, math.radians(-90)), (-0.2, 0.0, 2.0))):
    cam_data.type = "ORTHO"; cam_data.ortho_scale = 2.0
    cam.matrix_world = Matrix.LocRotScale(Vector(loc), Matrix(Matrix.Rotation(rot[2], 3, "Z") @ Matrix.Rotation(rot[0], 3, "X")).to_quaternion(), None)
    shot(nm, True, True)
print("PROOF done", OUT)
