# Blender: a Ready or Not first-person gun -> <slug>_vm.blend for tools/gb/gb_convert.sh (the RoN front end of
# docs/ron_importer_design.md; first gun: the Grizzly MK V). One armature = RoN's FP arms skeleton (Skinning_Arms_03's
# bind pose) + the gun's bones under weapon_right (checked: in the idle pose the trigger lands 1.7 cm from the right
# index fingertip); skinned arms + gun meshes, the magazine and rounds as rigid parts on tag_mag_01/02(_bullets); one
# action "@<sequence>" per clip: the arms' additive clip baked over the idle pose + the gun clip (tools/ron/ron_bake.py).
# Axes: RoN component space (cm, +Y forward, Z up, left-handed) -> Blender metres with the view along +X, the way the
# Source front end hands it to gb_build.py (which turns +X to CryEngine's +Y): x' = y, y' = x, z' = z (a mirror, so
# triangle winding flips), x 0.01. Rotations: R' = M R M (M = the swap).
#   blender -b --factory-startup -P ron_import.py -- <config.json>
# config: {"slug", "out", "arms_psk", "gun_psk", "mag_psk", "rounds_psk", "arms_clips", "weapon_clips",
#          "sequences": {name: {"arms": clip, "gun": clip, "fps": n, "ref_first": bool (an additive: idle frame first)}}}
import bpy, sys, os, json, math
from mathutils import Matrix, Quaternion, Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import psk, ron_bake
import numpy as np

CFG = json.load(open(sys.argv[sys.argv.index("--") + 1]))
M3 = Matrix(((0, 1, 0), (1, 0, 0), (0, 0, 1)))


def conv(q, t):
    """RoN (q xyzw, t cm) -> Blender 4x4 (metres, view +X)"""
    R = Quaternion((q[3], q[0], q[1], q[2])).to_matrix()
    R = M3 @ R @ M3
    m = R.to_4x4()
    m.translation = Vector((t[1], t[0], t[2])) * 0.01
    return m


def p_conv(p):
    return Vector((p[1], p[0], p[2])) * 0.01


def fk(bones):   # psk bones (name, parent, q, t) -> RoN component-space (q, t)
    out = []
    for i, (n, p, q, t) in enumerate(bones):
        q = np.array(q, float); t = np.array(t, float)
        if p < 0: out.append((q, t))
        else:
            pq, pt = out[p]
            out.append((ron_bake.qmul(pq, q), pt + ron_bake.qrot(pq, t)))
    return out


bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
A_psk, G_psk = psk.read(CFG["arms_psk"]), psk.read(CFG["gun_psk"])
A_sk, W_sk = ron_bake.load(CFG["arms_clips"]), ron_bake.load(CFG["weapon_clips"])
MOUNT = "weapon_right"
# Ready or Not keeps both hands on the gun with two LegIK nodes after all its layers (ANIMBP_Player_FP_Master:
# ik_hand_weapon_static_RI -> hand_RI, _LE -> hand_LE, 2-bone limbs at alpha 1, the hand taking the target's rotation;
# fp_cdo.json); without them the additive layers (breathing, steps, jump, crouch) slide the gun 1-24 mm in the hands.
# KCD2 does the same with the chrparams' animation-driven IK (gb_stage.py writes LimbIK 2BIK + ADIKTarget): its blend
# is a weight joint's local translation x, clamped to 0..1 (WHGame 1.5.6 FUN_180a8466c). One weight joint per hand under
# root 2 m along CryEngine's x (RoN's -X here, gb_build's turn), keyed in every clip at that rest offset: the DAE puts
# root at the eye, so 1 m came out 0.94 (94 % IK); the blend is clamped, so 1.94 is full IK
ADIK = (("hand_RI", "upperarm_RI", "ik_hand_weapon_static_RI", "adik_weight_RI"),
        ("hand_LE", "upperarm_LE", "ik_hand_weapon_static_LE", "adik_weight_LE"))

# ---------------------------------------------------------------- armature (rest = the arms' bind pose)
arm_names = [b[0] for b in A_psk["bones"]]
arm_parent = [b[1] for b in A_psk["bones"]]
arm_rest_ue = fk(A_psk["bones"])
_root = arm_parent.index(-1)
for *_, w in ADIK:
    arm_names.append(w); arm_parent.append(_root)
    arm_rest_ue.append((arm_rest_ue[_root][0], arm_rest_ue[_root][1] + np.array([-200.0, 0.0, 0.0])))
mount_i = [n.lower() for n in arm_names].index(MOUNT.lower())
gun_names = [b[0] for b in G_psk["bones"]]
gun_rest_local = fk(G_psk["bones"])          # J_Gun space (J_Gun = identity in the ref pose)
mq, mt = arm_rest_ue[mount_i]
gun_rest_ue = [(ron_bake.qmul(mq, q), mt + ron_bake.qrot(mq, t)) for q, t in gun_rest_local]
NAMES = arm_names + gun_names
PARENT = arm_parent + [(mount_i if p < 0 else p + len(arm_names)) for p in [b[1] for b in G_psk["bones"]]]
REST = [conv(q, t) for q, t in arm_rest_ue + gun_rest_ue]
# KCD2 puts a viewmodel's origin at the eye (the Source guns' model origin is their eye); RoN's FP rig is full-body,
# ~1.7 m up. RoN's FirstPersonCamera sits fixed on CharacterMesh1P (it doesn't follow the fp_camera bone, whose clip
# keys are a 2-5 deg wobble: Claude 4's decompile), so the whole rig moves by one fixed offset: the idle pose's camera
# point becomes the origin, axes unchanged (Cry = (-x, y, z) of RoN's after gb_build's turn). An inference: BasePlayer
# attaches FirstPersonCamera to CharacterMesh1P at RelativeLocation 0 (foot level), so RoN's code places it at run time
# and that code wasn't found; the fp_camera idle point (-5.87, 21.42, 167.83) cm is the best number we have. RoN's
# camera FOV is 110 (x1.15 sprinting): compare against RoN footage at that FOV.
CAM = NAMES.index("fp_camera")
_idle = A_sk.world(A_sk.local(CFG.get("arms_base", "fp_grizzly_idle_pose_b"), 0))[A_sk.index["fp_camera"]]
CAM_POS = conv(*_idle).translation.copy()
_c = Matrix.Translation(CAM_POS).inverted()
REST = [_c @ m for m in REST]
print("RON CAMERA (RoN cm)", [round(float(x), 2) for x in _idle[1]])

ad = bpy.data.armatures.new(CFG["slug"] + "_vm")
arm = bpy.data.objects.new(CFG["slug"] + "_vm", ad)
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
ebs = []
for i, n in enumerate(NAMES):
    eb = ad.edit_bones.new(n)
    eb.head = (0, 0, 0); eb.tail = (0, 0.015, 0)
    eb.matrix = REST[i]
    ebs.append(eb)
for i, p in enumerate(PARENT):
    if p >= 0: ebs[i].parent = ebs[p]; ebs[i].use_connect = False
bpy.ops.object.mode_set(mode="OBJECT")
REST = [arm.data.bones[n].matrix_local.copy() for n in NAMES]   # what Blender kept (roll rounding)
print("RON ARMATURE", len(NAMES), "bones;", len(arm_names), "arms +", len(gun_names), "gun on", MOUNT)


# ---------------------------------------------------------------- meshes
def add_mesh(name, m, place, groups):
    """place: per-point Blender position from the psk point; groups: list of (vertex group name, [(point, weight)])"""
    pts = [place(p) for p in m["points"]]
    W = m["wedges"]
    # front faces are (w0, w2, w1) here: the file's order drew the inside of the gun in KCD2 (the user, 2026-10-18:
    # "im seeing the inside of the gun"; Blender's preview draws both sides, so only the game showed it)
    faces = [(W[a][0], W[c][0], W[b][0]) for a, b, c, _, _ in m["faces"]]
    me = bpy.data.meshes.new(name)
    me.from_pydata(pts, [], faces)
    uv = me.uv_layers.new(name="UVMap")
    li = 0
    for a, b, c, _, _ in m["faces"]:
        for wi in (a, c, b):
            uv.data[li].uv = (W[wi][1], 1 - W[wi][2]); li += 1
    mats = m["materials"] or [name]
    for mn in mats:
        me.materials.append(bpy.data.materials.get(mn) or bpy.data.materials.new(mn))
    for poly, (_, _, _, mat, _) in zip(me.polygons, m["faces"]):
        poly.material_index = min(mat, len(mats) - 1); poly.use_smooth = True
    me.update()
    if m.get("normals") and len(m["normals"]) == len(m["points"]):
        nrm = [Vector((n[1], n[0], n[2])).normalized() for n in m["normals"]]   # the same swap as the points
        agree = sum(1 for poly in me.polygons if poly.normal.dot(sum((nrm[v] for v in poly.vertices), Vector())) > 0)
        print(f"RON FACING {name}: {agree}/{len(me.polygons)} faces agree with RoN's stored normals")
        if agree < 0.95 * len(me.polygons):
            raise SystemExit(f"RON {name}: faces point inward ({agree}/{len(me.polygons)} agree): fix the winding")
        me.normals_split_custom_set_from_vertices(nrm)   # RoN's own normals: its shading, hard edges included
    ob = bpy.data.objects.new(name, me); scene.collection.objects.link(ob)
    for gname, pw in groups:
        vg = ob.vertex_groups.get(gname) or ob.vertex_groups.new(name=gname)
        for p, w in pw:
            vg.add([p], w, "ADD")
    ob.parent = arm
    md = ob.modifiers.new("Armature", "ARMATURE"); md.object = arm
    return ob


def skin_groups(m, names):
    by = {}
    for w, p, b in m["weights"]:
        if w > 0: by.setdefault(names[b], []).append((p, w))
    return list(by.items())


add_mesh("ron_arms", A_psk, lambda p: _c @ p_conv(p), skin_groups(A_psk, arm_names))   # moved with the rest pose
mw = REST[len(arm_names)]   # J_Gun's rest (armature space): the gun's points are in J_Gun space
add_mesh("ron_gun", G_psk, lambda p: mw @ p_conv(p), skin_groups(G_psk, gun_names))
for part, bone in (("mag_psk", "tag_mag_01"), ("mag_psk", "tag_mag_02"), ("rounds_psk", "tag_mag_01_bullets"), ("rounds_psk", "tag_mag_02_bullets")):
    if not CFG.get(part): continue
    pm = psk.read(CFG[part]); rest = REST[NAMES.index(bone)]
    add_mesh(f"ron_{bone}", pm, lambda p, r=rest: r @ p_conv(p), [(bone, [(i, 1.0) for i in range(len(pm["points"]))])])
print("RON MESHES", [o.name for o in scene.objects if o.type == "MESH"])

# ---------------------------------------------------------------- actions
arm.animation_data_create()
ORDER = list(range(len(NAMES)))   # parents come first in psk order
arm_idx_in_anim = [A_sk.index.get(n.lower()) for n in arm_names]
gun_idx_in_anim = [W_sk.index.get(n.lower()) for n in gun_names]
base_arms = A_sk.local(CFG.get("arms_base", "fp_grizzly_idle_pose_b"), 0)


def pose_world(arms_clip, gun_clip, f):
    """Blender armature-space matrices of every bone at frame f"""
    la = A_sk.local(arms_clip, f, base=base_arms) if arms_clip and arms_clip != CFG.get("arms_base") else base_arms
    wa = A_sk.world(la)
    wg = W_sk.world(W_sk.local(gun_clip, f)) if gun_clip else gun_rest_local
    out = [None] * len(NAMES)
    for i in ORDER:
        if i < len(arm_names):
            k = arm_idx_in_anim[i]
            if k is not None: out[i] = conv(*wa[k])
            else:   # not in the animation skeleton (sockets): follow the parent at its rest offset
                p = PARENT[i]
                out[i] = REST[i] if p < 0 else out[p] @ (REST[p].inverted() @ REST[i])
        else:
            g = i - len(arm_names); k = gun_idx_in_anim[g]
            q, t = wg[k] if k is not None else gun_rest_local[g]
            mw_ = out[mount_i]
            out[i] = mw_ @ conv(q, t)
    return [_c @ m for m in out]


for seq, c in CFG["sequences"].items():
    nf = max(A_sk.frames(c["arms"]) if c.get("arms") else 1, W_sk.frames(c["gun"]) if c.get("gun") else 1)
    act = bpy.data.actions.new("@" + seq); act.use_fake_user = True; act["fps"] = float(c.get("fps", 30))
    curves = {}
    for n in NAMES:
        for path, k in (("location", 3), ("rotation_quaternion", 4)):
            for j in range(k):
                curves[(n, path, j)] = act.fcurves.new(f'pose.bones["{n}"].{path}', index=j, action_group=n)
    vals = {key: [] for key in curves}
    # "ref_first": an additive clip for KCD2 (animsettings additive: rc takes the first frame as the reference pose):
    # frame 1 = the idle pose (arms base + the gun's idle), then the clip baked over it, so the CAF's delta is RoN's own
    # additive (local: R = add.R x base.R, T = base.T + add.T)
    ref = bool(c.get("ref_first"))
    for f in range(-1 if ref else 0, nf):
        W = pose_world(None, c.get("ref_gun", c.get("gun")), 0) if f < 0 else pose_world(c.get("arms"), c.get("gun"), f)
        f += 1 if ref else 0
        for i, n in enumerate(NAMES):
            p = PARENT[i]
            basis = (REST[i].inverted() @ W[i]) if p < 0 else ((W[p] @ REST[p].inverted() @ REST[i]).inverted() @ W[i])
            loc, rot, _ = basis.decompose()
            for j in range(3): vals[(n, "location", j)] += [f + 1, loc[j]]
            for j in range(4): vals[(n, "rotation_quaternion", j)] += [f + 1, rot[j]]
    for key, fc in curves.items():
        v = vals[key]; fc.keyframe_points.add(len(v) // 2); fc.keyframe_points.foreach_set("co", v)
        for kp in fc.keyframe_points: kp.interpolation = "LINEAR"
        fc.update()
    print("RON ACTION", act.name, nf, "frames at", act["fps"], "fps")
for pb in arm.pose.bones:
    pb.rotation_mode = "QUATERNION"
arm.animation_data.action = bpy.data.actions.get("@" + next(iter(CFG["sequences"])))
os.makedirs(os.path.dirname(CFG["out"]), exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=CFG["out"])
print("RON IMPORT DONE", CFG["out"])
