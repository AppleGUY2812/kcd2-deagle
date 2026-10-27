# Arm swap demo, animated: Henry's worn clothing (sleeves, arm plates, gauntlets) on a Ready or Not Grizzly clip,
# rendered from the in-game camera next to today's RoN arms. Blender only; nothing here reaches the game.
# Opens on ron_grizzly_vm.blend. Per frame: Henry's armature root follows RoN's shoulders; arm + finger bones copy the
# RoN pose (each Henry bone's rest direction swung onto RoN's, then RoN's world rotation delta); the wrists are pulled
# onto RoN's by a 2-bone IK that keeps the bend plane; hands and fingers take RoN's frames again after the IK.
# Clothing: KCD2-Convertor .dae (mesh turned 180 deg about Z vs tools/henry_rig.py, fixed here); faces in the
# "UnderGloves" hiding group (vertex colour alpha bit 3 cleared) are removed on arms items, as the game does under
# gauntlets; colours are tex_prep.py's stand-in for KCD2's clothing shader (approximate, see there).
#   blender -b ron_grizzly_vm.blend -P proof_anim.py -- <out dir> <tex dir> <action> [step] [res_x] [samples]
import bpy, sys, os, math
from mathutils import Matrix, Vector
argv = sys.argv[sys.argv.index("--") + 1:]
OUT, TEX, ACTION = argv[0], argv[1], argv[2]
STEP = int(argv[3]) if len(argv) > 3 else 1
RES_X = int(argv[4]) if len(argv) > 4 else 640
SAMPLES = int(argv[5]) if len(argv) > 5 else 16
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import henry_rig

scene = bpy.context.scene
ron = bpy.data.objects["ron_grizzly_vm"]
ron.animation_data_create()
act = bpy.data.actions[ACTION]
ron.animation_data.action = act

BODY = r"E:\Tools\deagle-work\henry\Objects\characters\humans\male\body"
h = henry_rig.import_henry([os.path.join(BODY, f) for f in ("male_body.glb", "male_body_arms.glb", "male_body_hands.glb")])
for o in [o for o in bpy.data.objects if o.type == "MESH" and o.parent == h]:
    bpy.data.objects.remove(o)

CLOTH = r"E:\Tools\deagle-work\henry\Objects\characters\humans\male\clothing"
ITEMS = [(r"hands\gauntlets\gauntlets01\gauntlets01.dae", "gauntlets01_m01_hands", False, False),
         (r"arms\arm_armour\arm_brigandine\brigandinearm05\brigandinearm05.dae", "brigandinearm05_m01_arms", True, True),
         (r"arms\arms_splited\gambesonlong03_arms\gambesonlong03_arms.dae", "gambesonlong03_m01_arms", True, False)]
HIDE_BIT = 3   # ClothingHidingGroup.xml: UnderGloves (arms), hidden by Gauntlets01's HidingGroups="UnderGloves"

def material(tex, alpha_test):
    m = bpy.data.materials.new(tex); m.use_nodes = True
    nt = m.node_tree; N = nt.nodes; L = nt.links
    bsdf = N["Principled BSDF"]
    def img(name, colour):
        n = N.new("ShaderNodeTexImage"); n.image = bpy.data.images.load(os.path.join(TEX, name))
        n.image.colorspace_settings.name = "sRGB" if colour else "Non-Color"; return n
    a = img(tex + "_albedo.png", True); L.new(a.outputs["Color"], bsdf.inputs["Base Color"])
    mr = img(tex + "_metal.png", False); sp = N.new("ShaderNodeSeparateColor"); L.new(mr.outputs["Color"], sp.inputs[0])
    L.new(sp.outputs[0], bsdf.inputs["Metallic"]); L.new(sp.outputs[1], bsdf.inputs["Roughness"])
    nm = img(tex + "_normal.png", False); nn = N.new("ShaderNodeNormalMap"); L.new(nm.outputs["Color"], nn.inputs["Color"])
    L.new(nn.outputs["Normal"], bsdf.inputs["Normal"])
    if alpha_test:   # CryEngine AlphaTest 0.4 on the diffuse alpha
        gt = N.new("ShaderNodeMath"); gt.operation = "GREATER_THAN"; gt.inputs[1].default_value = 0.4
        L.new(a.outputs["Alpha"], gt.inputs[0]); L.new(gt.outputs[0], bsdf.inputs["Alpha"])
    return m

cloth = []
for f, tex, arms_item, alpha_test in ITEMS:
    before = set(bpy.data.objects)
    bpy.ops.wm.collada_import(filepath=os.path.join(CLOTH, f))
    new = [o for o in bpy.data.objects if o not in before]
    for o in [o for o in new if o.type == "MESH"]:
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
        for m in [m for m in o.modifiers if m.type == "ARMATURE"]:
            m.object = h; m.use_bone_envelopes = False; m.use_vertex_groups = True
        o.parent = h; o.parent_type = "OBJECT"; o.matrix_parent_inverse = Matrix.Identity(4)
        me = o.data
        for v in me.vertices: v.co.x, v.co.y = -v.co.x, -v.co.y
        if arms_item and me.color_attributes:
            ca = me.color_attributes[0]
            if ca.domain == "CORNER":
                hid = [all(not (round(ca.data[li].color[3] * 255) >> HIDE_BIT) & 1 for li in p.loop_indices) for p in me.polygons]
            else:
                vh = [not (round(d.color[3] * 255) >> HIDE_BIT) & 1 for d in ca.data]
                hid = [all(vh[vi] for vi in p.vertices) for p in me.polygons]
            import bmesh
            bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
            gone = [bm.faces[i] for i, x in enumerate(hid) if x]
            bmesh.ops.delete(bm, geom=gone, context="FACES"); bm.to_mesh(me); bm.free()
            print("ANIM hidden under gloves", o.name, len(gone), "faces")
        me.update()
        me.materials.clear(); me.materials.append(material(tex, alpha_test))
        cloth.append(o)
    for o in [o for o in new if o.type == "ARMATURE"]:
        bpy.data.objects.remove(o)

G = Matrix.Rotation(math.radians(90), 4, "Z")
def ron_pose(b): return ron.matrix_world @ ron.pose.bones[b].matrix
def ron_rest(b): return ron.matrix_world @ ron.data.bones[b].matrix_local
def hen_rest(b): return h.matrix_world @ h.data.bones[b].matrix_local
def hen_pose(b): return h.matrix_world @ h.pose.bones[b].matrix

def side(H, R):
    m = [(H + "Shoulder", "clavicle_" + R, H + "Arm", "upperarm_" + R),
         (H + "Arm", "upperarm_" + R, H + "ForeArm", "lowerarm_" + R),
         (H + "ForeArm", "lowerarm_" + R, H + "Hand", "hand_" + R),
         (H + "Hand", "hand_" + R, H + "HandMiddle1", "middle_2_" + R)]
    m += [(H + "HandThumb%d" % i, "thumb_%d_%s" % (i, R), H + "HandThumb%d" % (i + 1), ("thumb_%d_%s" % (i + 1, R)) if i < 3 else "thumb_4_end_" + R) for i in (1, 2, 3)]
    for f in ("Index", "Middle"):
        r = f.lower()
        m += [(H + "Hand%s%d" % (f, i), "%s_%d_%s" % (r, i + 1, R), H + "Hand%s%d" % (f, i + 1), ("%s_%d_%s" % (r, i + 2, R)) if i < 3 else "%s_5_end_%s" % (r, R)) for i in (1, 2, 3)]
    for f in ("Ring", "Pinky"):
        r = f.lower()
        m += [(H + "InHand" + f, "%s_1_%s" % (r, R), H + "Hand%s1" % f, "%s_2_%s" % (r, R))]
        m += [(H + "Hand%s%d" % (f, i), "%s_%d_%s" % (r, i + 1, R), H + "Hand%s%d" % (f, i + 1), ("%s_%d_%s" % (r, i + 2, R)) if i < 3 else "%s_5_end_%s" % (r, R)) for i in (1, 2, 3)]
    return m
MAP = side("Right", "RI") + side("Left", "LE")

def set_world_rot(hb, q):
    loc = hen_pose(hb).translation
    h.pose.bones[hb].matrix = h.matrix_world.inverted() @ Matrix.LocRotScale(loc, q, None)
    bpy.context.view_layer.update()

def target_rot(hb, rb, hn, rn):
    dh = (hen_rest(hn).translation - hen_rest(hb).translation).normalized()
    dr = (ron_rest(rn).translation - ron_rest(rb).translation).normalized()
    return ron_pose(rb).to_quaternion() @ ron_rest(rb).to_quaternion().inverted() @ dh.rotation_difference(dr) @ hen_rest(hb).to_quaternion()

def swing(hb, frm, to):
    set_world_rot(hb, frm.normalized().rotation_difference(to.normalized()) @ hen_pose(hb).to_quaternion())

def pose_frame():
    for pb in h.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    h.matrix_world = G; bpy.context.view_layer.update()
    mid_h = (hen_rest("RightArm").translation + hen_rest("LeftArm").translation) / 2
    mid_r = (ron_pose("upperarm_RI").translation + ron_pose("upperarm_LE").translation) / 2
    h.matrix_world = Matrix.Translation(mid_r - mid_h) @ G; bpy.context.view_layer.update()
    miss = []
    for H, R in (("Right", "RI"), ("Left", "LE")):
        rows = [m for m in MAP if m[0].startswith(H)]
        for m in rows: set_world_rot(m[0], target_rot(*m))
        S = hen_pose(H + "Arm").translation; E = hen_pose(H + "ForeArm").translation; W = hen_pose(H + "Hand").translation
        T = ron_pose("hand_" + R).translation
        a, b = (E - S).length, (W - E).length
        d = min((T - S).length, a + b - 1e-4)
        u = (T - S).normalized(); v = (E - S) - u * (E - S).dot(u)
        v = v.normalized() if v.length > 1e-6 else Vector((0, 0, -1))
        x = (a * a - b * b + d * d) / (2 * d)
        swing(H + "Arm", E - S, (S + u * x + v * math.sqrt(max(a * a - x * x, 0))) - S)
        E = hen_pose(H + "ForeArm").translation; W = hen_pose(H + "Hand").translation
        swing(H + "ForeArm", W - E, T - E)
        for m in rows[3:]: set_world_rot(m[0], target_rot(*m))
        if not os.environ.get("PROOF_NO_AIM"):
            # Henry's hand is ~11% smaller and his thumb root sits ~2 cm nearer the wrist than RoN's: copied angles leave the
            # thumb out to the side. Aim each thumb bone at RoN's next thumb joint instead (roll kept from the copy).
            ends = ["thumb_2_" + R, "thumb_3_" + R, "thumb_4_end_" + R]
            for i, rn in enumerate(ends, 1):
                J = hen_pose(H + "HandThumb%d" % i).translation; N = hen_pose(H + "HandThumb%d" % (i + 1)).translation
                swing(H + "HandThumb%d" % i, N - J, ron_pose(rn).translation - J)
        miss.append((hen_pose(H + "Hand").translation - T).length * 1000)
    return miss

if os.environ.get("PROOF_DIAG"):   # thumb / finger check: where each Henry joint sits vs its RoN joint, in RoN hand space
    scene.frame_set(int(os.environ["PROOF_DIAG"])); pose_frame()
    for R, H in (("RI", "Right"), ("LE", "Left")):
        hand = ron_pose("hand_" + R).inverted()
        def p(M): return [round(x * 100, 1) for x in (hand @ M.translation)]
        print("DIAG", R, "hand rest-length ron %.1f cm" % ((ron_rest("middle_2_" + R).translation - ron_rest("hand_" + R).translation).length * 100),
              "henry %.1f cm" % ((hen_rest(H + "HandMiddle1").translation - hen_rest(H + "Hand").translation).length * 100))
        for hb, rb in ((H + "HandThumb1", "thumb_1_" + R), (H + "HandThumb2", "thumb_2_" + R), (H + "HandThumb3", "thumb_3_" + R),
                       (H + "HandThumb4", "thumb_4_end_" + R), (H + "HandIndex1", "index_2_" + R), (H + "HandIndex4", "index_5_end_" + R),
                       (H + "HandPinky1", "pinky_2_" + R)):
            print("DIAG  %-18s henry %s  ron %-14s %s  off %.1f cm" % (hb, p(hen_pose(hb)), rb, p(ron_pose(rb)),
                  (hen_pose(hb).translation - ron_pose(rb).translation).length * 100))
        print("DIAG  henry thumb bone parents", [h.data.bones[H + "HandThumb%d" % i].parent.name for i in (1, 2, 3, 4)])
        print("DIAG  ron thumb bone parents", [ron.data.bones["thumb_%d_%s" % (i, R)].parent.name for i in (1, 2, 3)])
    sys.exit(0)

cam_data = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cam_data); scene.collection.objects.link(cam)
cam_data.sensor_fit = "VERTICAL"; cam_data.angle = 1.101; cam_data.clip_start = 0.01
scene.camera = cam
scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.samples = SAMPLES; scene.cycles.use_denoising = True
scene.render.resolution_x, scene.render.resolution_y = RES_X, RES_X * 9 // 16
scene.render.resolution_percentage = 100
w = bpy.data.worlds.new("w"); w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.42, 0.45, 0.5, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 1.2; scene.world = w
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun)
sun.data.energy = 3.5; sun.data.angle = math.radians(8); sun.rotation_euler = (math.radians(55), math.radians(15), math.radians(-35))
ron_arms = bpy.data.objects["ron_arms"]
f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
scene.frame_set(f0)
aim = (ron_pose("hand_RI").translation + ron_pose("hand_LE").translation) / 2 + Vector((0, 0, -0.05))
close_eye = aim + Vector((0.42, -0.48, 0.16))
GAME = Matrix.Rotation(math.radians(-90), 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")   # the viewmodel origin, looking +X
CLOSE = Matrix.LocRotScale(close_eye, (aim - close_eye).to_track_quat("-Z", "Z"), None)          # fixed, front right
for d in ("ron_game", "henry_game", "ron_close", "henry_close"): os.makedirs(os.path.join(OUT, d), exist_ok=True)
print("ANIM", ACTION, "frames", f0, f1, "fps", scene.render.fps)
for i, f in enumerate(range(f0, f1 + 1, STEP)):
    scene.frame_set(f)
    miss = pose_frame()
    # the game's camera = the viewmodel origin (RoN's camera is fixed on the arms mesh, ron_import.py)
    for view, M in (("game", GAME), ("close", CLOSE)):
        cam.matrix_world = M
        for who, show_ron in (("ron", True), ("henry", False)):
            ron_arms.hide_render = not show_ron
            for o in cloth: o.hide_render = show_ron
            scene.render.filepath = os.path.join(OUT, who + "_" + view, "%04d.png" % i)
            bpy.ops.render.render(write_still=True)
    print("ANIM frame", f, "wrist miss mm %.1f %.1f" % tuple(miss), flush=True)
print("ANIM done")
