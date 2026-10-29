# Build the Grizzly viewmodel on Henry's skeleton, offline in Blender.
#
# Opens ron_grizzly_vm.blend (read only), imports Henry's bare arms + hands, scales the viewmodel to Henry's size,
# fits Henry's pose to every source action frame by frame, merges Henry's armature into the gun armature, bakes one
# merged action per source action ("<source>_henry": gun curves + Henry curves), prints per-action fit numbers,
# saves ron_grizzly_henry_vm.blend and renders a Workbench contact-sheet source.
#
#   blender -b ron_grizzly_vm.blend -t 3 -P henry_vm.py [-- <out blend> <sheet dir>]
#
# Debug switches (environment): HENRYVM_ONLY=name1,name2 limits the actions, HENRYVM_NO_RENDER=1 skips rendering.
import bpy, sys, os, math, json
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import henry_rig
import retarget
import check_binds

def arg(i, default):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return argv[i] if len(argv) > i else default

OUT_BLEND = arg(0, r"E:\Tools\deagle-work\guns\ron_grizzly_henry\ron_grizzly_henry_vm.blend")
SHEET_DIR = arg(1, r"E:\Tools\deagle-work\guns\ron_grizzly_henry\sheet")
PROFILE = os.environ.get("HENRYVM_PROFILE") or os.path.join(os.path.dirname(OUT_BLEND), "profile.json")
BODY = r"E:\Tools\deagle-work\henry\Objects\characters\humans\male\body"
check_binds.GAME_BODY = BODY
HENRY_MESH_NAMES = {"arms", "hands"}
VERIFY_BONES = ["RightHand", "LeftHand", "RightForeArm", "LeftForeArm", "RightHandIndex3"]
FINGER_TIPS = {"HandThumb4", "HandIndex4", "HandMiddle4", "HandRing4", "HandPinky4"}
G = Matrix.Rotation(math.radians(90), 4, "Z")
INPLACE = os.environ.get("HENRYVM_INPLACE") == "1"
# CryEngine's 2BIK targets: one per hand, parented where RoN's own static weapon IK lives. The .chrparams points the
# solver at these bones; baking the hand's world matrix into them keeps the hand on the gun under the game's layers.
IK_TARGETS = {"henry_ik_hand_RI": "RightHand", "henry_ik_hand_LE": "LeftHand"}

scene = bpy.context.scene
ron = next(o for o in bpy.data.objects if o.type == "ARMATURE")
SOURCE_PATH = bpy.data.filepath
SOURCE_ACTIONS = sorted(bpy.data.actions, key=lambda a: a.name)
only = [x for x in os.environ.get("HENRYVM_ONLY", "").split(",") if x]
if only:
    SOURCE_ACTIONS = [a for a in SOURCE_ACTIONS if a.name in only]

print("HENRYVM source", SOURCE_PATH)
print("HENRYVM actions", len(SOURCE_ACTIONS), "of", len(bpy.data.actions))

# --- 2. Henry's bare arms + hands, built on the game's exact joint frames ---------
h = henry_rig.import_henry([os.path.join(BODY, f) for f in ("male_body_arms.glb", "male_body_hands.glb")])
for o in [o for o in bpy.data.objects if o.type == "MESH" and o.parent == h and o.name not in HENRY_MESH_NAMES]:
    print("HENRYVM drop mesh", o.name)
    bpy.data.objects.remove(o)
henry_meshes = [o for o in bpy.data.objects if o.type == "MESH" and o.parent == h and o.name in HENRY_MESH_NAMES]
if {o.name for o in henry_meshes} != HENRY_MESH_NAMES:
    raise RuntimeError("Henry arm meshes not found: %r" % [o.name for o in henry_meshes])
h.animation_data_clear()
HENRY_BONES = [pb.name for pb in h.pose.bones]
print("HENRYVM Henry bones", len(HENRY_BONES), "meshes", [(o.name, len(o.data.vertices)) for o in henry_meshes])

# --- 3. source scale, exactly as proof_anim.py --------------------------------
fit = retarget.Fit(bpy, ron, h, G)
SRC_SCALE = fit.source_scale()
ron.matrix_world = Matrix.Scale(1 / SRC_SCALE, 4) @ ron.matrix_world
bpy.context.view_layer.update()
print("HENRYVM source arm size vs Henry %.6f, viewmodel scaled x%.6f" % (SRC_SCALE, 1 / SRC_SCALE))
print("HENRYVM source profile", fit.profile)

# --- 4a. Fit every source frame; record Henry's world pose + PROOF_DIAG numbers ---
def to_np(m):
    return [m[r][c] for r in range(4) for c in range(4)]

records, diags, verify_pre = {}, {}, {}
reload_name = "@grizzly_reload"
for act in SOURCE_ACTIONS:
    ron.animation_data.action = act
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    frames = list(range(f0, f1 + 1))
    arr = __import__("numpy").empty((len(frames), len(HENRY_BONES), 16), dtype="float32")
    fore, elbow, tips = [], [], {}
    for i, f in enumerate(frames):
        scene.frame_set(f)
        fit.frame("full")
        mw = h.matrix_world
        for bi, pb in enumerate(h.pose.bones):
            arr[i, bi] = to_np(mw @ pb.matrix)
        d, off = fit.directions(), fit.offsets()
        fore.extend(v for k, v in d.items() if k[1] == "ForeArmDir")
        elbow.extend(v for k, v in off.items() if k[1] == "ForeArm")
        for k, v in off.items():
            if k[1] in FINGER_TIPS:
                tips.setdefault(k[1], []).append(v)
        if act.name == reload_name and f in (f0, (f0 + f1) // 2, f1):
            verify_pre[f] = {b: (mw @ h.pose.bones[b].matrix).translation.copy() for b in VERIFY_BONES}
    records[act.name] = arr
    tip_means = {j: sum(v) / len(v) for j, v in tips.items()}
    tip_max = max((max(v) for v in tips.values()), default=0.0)
    diags[act.name] = {
        "frames": (f0, f1),
        "fore_mean": sum(fore) / len(fore), "fore_max": max(fore),
        "elbow_mean": sum(elbow) / len(elbow), "elbow_max": max(elbow),
        "tip_mean": max(tip_means.values()), "tip_joint": max(tip_means, key=tip_means.get),
        "tip_max": tip_max,
    }
    print("HENRYVM fit %-28s f %d-%d forearm %.2f/%.2f deg elbow %.2f/%.2f cm worst fingertip %s %.2f/%.2f cm" % (
        act.name, f0, f1, diags[act.name]["fore_mean"], diags[act.name]["fore_max"],
        diags[act.name]["elbow_mean"], diags[act.name]["elbow_max"],
        diags[act.name]["tip_joint"], diags[act.name]["tip_mean"], diags[act.name]["tip_max"]), flush=True)

# --- 5. merge the armatures (fail loudly on a name collision) ----------------
collisions = sorted(set(ron.data.bones.keys()) & set(h.data.bones.keys()))
if collisions:
    raise RuntimeError("bone name collisions, stopping: %r" % collisions)
print("HENRYVM merge bone collisions: none")

ron_rest_before = {b.name: b.matrix_local.copy() for b in ron.data.bones}
henry_rest_before = {b.name: b.matrix_local.copy() for b in h.data.bones}
h.matrix_world = Matrix.Identity(4)
bpy.context.view_layer.update()
mesh_rest = {o.name: o.matrix_world.copy() for o in henry_meshes}

ron.animation_data.action = None
bpy.ops.object.select_all(action="DESELECT")
ron.select_set(True)
h.select_set(True)
bpy.context.view_layer.objects.active = ron
bpy.ops.object.join()
merged = ron
M = merged.matrix_world.copy()
print("HENRYVM merged bones", len(merged.data.bones), "matrix", [round(x, 6) for r in M for x in r])

# Henry's root under the gun's root, keeping its rest pose; add the two ADIK target bones RoN's chrparams expects
bpy.context.view_layer.objects.active = merged
bpy.ops.object.mode_set(mode="EDIT")
eb = merged.data.edit_bones
eb["Human_Male"].parent = eb["root"]
for tgt, hand in IK_TARGETS.items():
    if tgt in eb:
        continue
    donor = eb["ik_hand_weapon_static_" + ("RI" if hand == "RightHand" else "LE")]
    b = eb.new(tgt)
    b.head, b.tail, b.roll = donor.head.copy(), donor.tail.copy(), donor.roll
    b.parent = donor.parent
    b.use_connect = False
    b.use_deform = False
bpy.ops.object.mode_set(mode="OBJECT")
bpy.context.view_layer.update()

M_inv = M.inverted()
rest_err = 0.0
for name, m in henry_rest_before.items():
    rest_err = max(rest_err, (merged.data.bones[name].matrix_local.translation - (M_inv @ m).translation).length)
ron_err = max((merged.data.bones[n].matrix_local.translation - m.translation).length for n, m in ron_rest_before.items())
print("HENRYVM merge rest max error: Henry %.6f m, Ron %.6f m" % (rest_err, ron_err))

# Step 6 first: the source arm mesh goes away before the bone scan, so its vertex groups cannot keep RoN's arms.
for n in retarget.PROFILES[fit.profile][3]:
    o = bpy.data.objects.get(n)
    if o:
        print("HENRYVM remove source arm mesh", n)
        bpy.data.objects.remove(o, do_unlink=True)

# --- 1. drop RoN body bones the viewmodel no longer needs ---------------------
# Keep the profile's gun/camera bones, the whole gun hierarchy, IK/adik/camera bones, every bone a remaining mesh
# weights, and all their ancestors. Everything else (RoN arms, hands, fingers, legs, unused sockets) goes, and its
# curves go with it.
from collections import defaultdict
profile = json.load(open(PROFILE)) if os.path.exists(PROFILE) else {}
if INPLACE and not profile:
    raise RuntimeError("in-place build needs a profile next to the blend: %s" % PROFILE)
profile_bones = {v.get("bone") for v in profile.get("bones", {}).values() if v.get("bone") in merged.data.bones}
gun_root = (profile.get("bones", {}).get("gun") or {}).get("bone")
keep = set(HENRY_BONES) | set(IK_TARGETS) | profile_bones
if gun_root and gun_root in merged.data.bones:
    keep.add(gun_root)
    keep.update(b.name for b in merged.data.bones[gun_root].children_recursive)
for b in merged.data.bones:
    low = b.name.casefold()
    if low.startswith("ik_") or low.startswith("adik_") or "camera" in low:
        keep.add(b.name)
for o in [o for o in bpy.data.objects if o.type == "MESH" and o not in henry_meshes]:
    for vg in o.vertex_groups:
        if any(g.group == vg.index and g.weight > 0 for v in o.data.vertices for g in v.groups):
            keep.add(vg.name)
for n in list(keep):
    b = merged.data.bones.get(n)
    while b and b.parent:
        keep.add(b.parent.name); b = b.parent

ron_bones = [b.name for b in merged.data.bones if b.name not in henry_rest_before and b.name not in IK_TARGETS]
kept_ron = [n for n in ron_bones if n in keep]
deleted = [n for n in ron_bones if n not in keep]
print("HENRYVM kept RoN bones (%d): %s" % (len(kept_ron), ", ".join(sorted(kept_ron))), flush=True)
print("HENRYVM deleted RoN bones (%d): %s" % (len(deleted), ", ".join(sorted(deleted))), flush=True)
prefixes = tuple('pose.bones["%s"].' % n for n in deleted)
for act in bpy.data.actions:
    for fc in [fc for fc in act.fcurves if fc.data_path.startswith(prefixes)]:
        act.fcurves.remove(fc)
bpy.context.view_layer.objects.active = merged
bpy.ops.object.mode_set(mode="EDIT")
for n in deleted:
    if n in merged.data.edit_bones:
        merged.data.edit_bones.remove(merged.data.edit_bones[n])
bpy.ops.object.mode_set(mode="OBJECT")
bpy.context.view_layer.update()


def casefold_dupes(names):
    d = defaultdict(list)
    for n in names:
        d[n.casefold()].append(n)
    return {k: v for k, v in d.items() if len(v) > 1}


# A KEPT RoN bone that collides case-insensitively with Henry gets the ron_ prefix (and its curves/vertex groups move).
for key, names in sorted(casefold_dupes([b.name for b in merged.data.bones]).items()):
    henry_hit = [n for n in names if n in henry_rest_before]
    ron_hit = [n for n in names if n not in henry_rest_before and n not in IK_TARGETS]
    if not (henry_hit and ron_hit and len(names) == len(henry_hit) + len(ron_hit)):
        raise RuntimeError("duplicate joint CRC32 in SKEL, joint names: %r" % names)
    for old in ron_hit:
        new = "ron_" + old
        folded = {b.name.casefold() for b in merged.data.bones}
        while new.casefold() in folded:
            new = "ron_" + new
        bpy.ops.object.mode_set(mode="EDIT")
        merged.data.edit_bones[old].name = new
        bpy.ops.object.mode_set(mode="OBJECT")
        for act in bpy.data.actions:
            for fc in act.fcurves:
                if fc.data_path.startswith('pose.bones["%s"]' % old):
                    fc.data_path = fc.data_path.replace('pose.bones["%s"]' % old, 'pose.bones["%s"]' % new, 1)
        for o in bpy.data.objects:
            vg = o.vertex_groups.get(old)
            if vg:
                vg.name = new
        print("HENRYVM renamed kept RoN bone %r -> %r" % (old, new), flush=True)

dupes = casefold_dupes([b.name for b in merged.data.bones])
if dupes:
    raise RuntimeError("duplicate joint CRC32 in SKEL (case-insensitive), joint names: %r" % dupes)
print("HENRYVM case-insensitive bone names OK (%d bones)" % len(merged.data.bones), flush=True)

# --- 2. Henry's rest = the game's bind, pre-R and pre-scale -------------------
# gb_build rotates the armature by R = 90 deg Z and applies its scale S; the exporter then writes joint X = bone.y,
# joint Y = bone.x, joint Z = -bone.z (transform_bone_matrix). Solve backwards from the game's bind so the exported
# default pose is the game's male_body bind. Translation is native size: the merged rig already carries S, gb_build
# applies S, so the pre-transform translation is divided by S.
if INPLACE:
    R = Matrix.Rotation(math.radians(90), 4, "Z")
    Rinv = R.inverted()
    S = M.to_scale().x
    G = check_binds.game_binds()
    bpy.context.view_layer.objects.active = merged
    bpy.ops.object.mode_set(mode="EDIT")
    dets = []
    for name, g in G.items():
        eb = merged.data.edit_bones.get(name)
        if eb is None:
            continue
        # desired bone frame after R: x=joint Y, y=joint X, z=-joint Z; translation = game translation
        bpost = Matrix(((g[0][1], g[0][0], -g[0][2], g[0][3]),
                        (g[1][1], g[1][0], -g[1][2], g[1][3]),
                        (g[2][1], g[2][0], -g[2][2], g[2][3]),
                        (0, 0, 0, 1)))
        rot = Rinv.to_3x3() @ bpost.to_3x3()
        dets.append(rot.determinant())
        bpre = Matrix.Identity(4)
        for r in range(3):
            for c in range(3):
                bpre[r][c] = rot[r][c]
        bpre.translation = (Rinv @ bpost.translation) / S
        eb.use_connect = False
        eb.head = bpre.translation
        eb.tail = eb.head + bpre.col[1].xyz.normalized() * 0.03
        eb.align_roll(bpre.col[2].xyz)
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()
    print("HENRYVM rest set from game binds: %d Henry bones, frame det %.6f..%.6f (target +1)" % (
        len(dets), min(dets), max(dets)), flush=True)

# Henry's meshes: parent to the merged armature; keep vertices in Henry space, place them in the gun's scaled space
for o in henry_meshes:
    o.parent = merged
    o.matrix_parent_inverse = Matrix.Identity(4)
    o.matrix_world = M @ mesh_rest[o.name]
    for mod in [m for m in o.modifiers if m.type == "ARMATURE"]:
        mod.object = merged
        mod.use_bone_envelopes = False
        mod.use_vertex_groups = True

# --- 4b/5b. Bake "<source>_henry" actions: copied gun curves + Henry world poses ----
BAKE_BONES = HENRY_BONES + list(IK_TARGETS)
for pb in merged.pose.bones:
    if pb.name in BAKE_BONES:
        pb.rotation_mode = "QUATERNION"
bone_order = [b.name for b in merged.data.bones if b.name in BAKE_BONES]
# parents before children
def depth(name):
    d, b = 0, merged.data.bones[name]
    while b.parent:
        d, b = d + 1, b.parent
    return d
bone_order.sort(key=depth)
parent_name = {n: (merged.data.bones[n].parent.name if merged.data.bones[n].parent else None) for n in bone_order}
rest = {n: merged.data.bones[n].matrix_local.copy() for n in bone_order}
index = {n: i for i, n in enumerate(HENRY_BONES)}

merged.animation_data_create()
for act in SOURCE_ACTIONS:
    arr = records[act.name]
    merged_act = act.copy()
    merged_act.name = act.name + "_henry"
    merged_act.use_fake_user = True
    merged.animation_data.action = merged_act
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    for i, f in enumerate(range(f0, f1 + 1)):
        scene.frame_set(f)
        bpy.context.view_layer.update()
        desired = {}
        for name in HENRY_BONES:
            v = arr[i, index[name]]
            desired[name] = M_inv @ Matrix((v[0:4], v[4:8], v[8:12], v[12:16]))
        for tgt, hand in IK_TARGETS.items():
            desired[tgt] = desired[hand]
        for name in bone_order:
            pb = merged.pose.bones[name]
            p = parent_name[name]
            if p in desired:
                P, Rp = desired[p], rest[p]
            elif p:
                P, Rp = merged.pose.bones[p].matrix, merged.data.bones[p].matrix_local
            else:
                P, Rp = Matrix.Identity(4), Matrix.Identity(4)
            pb.matrix_basis = (P @ Rp.inverted() @ rest[name]).inverted() @ desired[name]
        for name in BAKE_BONES:
            pb = merged.pose.bones[name]
            pb.keyframe_insert("location", frame=f, group=name)
            pb.keyframe_insert("rotation_quaternion", frame=f, group=name)
    # linear keys: the fit is evaluated per frame, so no Bezier overshoot between them
    wanted = {'pose.bones["%s"]' % n for n in BAKE_BONES}
    for fc in merged_act.fcurves:
        if any(fc.data_path.startswith(w) for w in wanted):
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"
    print("HENRYVM baked", merged_act.name, "frames", f1 - f0 + 1, "fcurves", len(merged_act.fcurves), flush=True)

# --- 5c. verify five joints at three reload frames against the pre-merge fit ---
if reload_name in records:
    merged.animation_data.action = bpy.data.actions[reload_name + "_henry"]
    worst = 0.0
    worst_m = worst_deg = 0.0
    for f, want in sorted(verify_pre.items()):
        scene.frame_set(f)
        bpy.context.view_layer.update()
        for b in VERIFY_BONES:
            got = (merged.matrix_world @ merged.pose.bones[b].matrix).translation
            worst = max(worst, (got - want[b]).length)
        for tgt, hand in IK_TARGETS.items():
            mt = merged.matrix_world @ merged.pose.bones[tgt].matrix
            mh = merged.matrix_world @ merged.pose.bones[hand].matrix
            worst_m = max(worst_m, (mt.translation - mh.translation).length)
            worst_deg = max(worst_deg, math.degrees(mt.to_quaternion().rotation_difference(mh.to_quaternion()).angle))
    print("HENRYVM merge verify reload joints %s max error %.6f m (target <= 0.001)" % (",".join(VERIFY_BONES), worst))
    print("HENRYVM IK targets vs hands max %.6f m / %.6f deg (targets <= 0.0001 m / 0.1 deg)" % (worst_m, worst_deg))

# --- 6/8 report + save -------------------------------------------------------
misses = [a.name for a in SOURCE_ACTIONS if diags[a.name]["fore_mean"] > 3.0 or diags[a.name]["tip_max"] > 2.5]
print("HENRYVM targets: forearm mean <= 3 deg, fingertip max <= 2.5 cm; miss:", misses or "none")

# In-place: the baked actions REPLACE the source names for gb_build, and Henry's meshes are not exported.
if INPLACE:
    replacement = [(a.name, bpy.data.actions.get(a.name + "_henry")) for a in SOURCE_ACTIONS]
    for old_name, merged_act in replacement:
        old = bpy.data.actions.get(old_name)
        if old and merged_act:
            bpy.data.actions.remove(old)
            merged_act.name = old_name
    print("HENRYVM in-place: merged actions now use the original names")
    for o in henry_meshes:
        print("HENRYVM in-place: drop Henry mesh", o.name)
        bpy.data.objects.remove(o, do_unlink=True)

os.makedirs(os.path.dirname(OUT_BLEND), exist_ok=True)
os.makedirs(SHEET_DIR, exist_ok=True)
reload_act = reload_name if INPLACE else reload_name + "_henry"
merged.animation_data.action = bpy.data.actions.get(reload_act) or merged.animation_data.action

# The game camera: viewmodel origin, looking +X (same matrix as proof_anim.py).
cam_data = bpy.data.cameras.new("cam")
cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam)
cam_data.sensor_fit = "VERTICAL"
cam_data.angle = 1.101
cam_data.clip_start = 0.01
scene.camera = cam
cam.matrix_world = Matrix.Rotation(math.radians(-90), 4, "Z") @ Matrix.Rotation(math.radians(90), 4, "X")
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x, scene.render.resolution_y = 640, 360
scene.render.resolution_percentage = 100
sh = scene.display.shading
sh.light = "STUDIO"
sh.color_type = "OBJECT"
sh.show_shadows = True
sh.show_cavity = True
sh.background_type = "VIEWPORT"
sh.background_color = (0.035, 0.04, 0.05)
skin = (0.72, 0.48, 0.35, 1.0)
for o in bpy.data.objects:
    if o.type == "MESH":
        o.color = skin if o.name in HENRY_MESH_NAMES and not INPLACE else (0.20, 0.21, 0.23, 1.0)

bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
print("HENRYVM saved", OUT_BLEND)

# --- 8. render Workbench contact-sheet frames from the game camera -----------
if not INPLACE and not os.environ.get("HENRYVM_NO_RENDER"):
    jobs = []
    for action_name, label, count in (("@grizzly_reload_henry", "reload", 6), ("@grizzly_idle_henry", "idle", 2)):
        act = bpy.data.actions.get(action_name)
        if not act:
            continue
        merged.animation_data.action = act
        f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
        if count == 1 or f1 <= f0:
            frames = [f0]
        else:
            frames = sorted({int(round(f0 + (f1 - f0) * i / (count - 1))) for i in range(count)})
        for n, f in enumerate(frames, 1):
            scene.frame_set(f)
            bpy.context.view_layer.update()
            path = os.path.join(SHEET_DIR, "%s_%02d_f%03d.png" % (label, n, f))
            scene.render.filepath = path
            bpy.ops.render.render(write_still=True)
            jobs.append({"label": "%s %d (frame %d)" % (label, n, f), "path": path, "frame": f, "action": action_name})
            print("HENRYVM render", path, flush=True)
    with open(os.path.join(SHEET_DIR, "frames.json"), "w") as f:
        json.dump(jobs, f, indent=1)
    print("HENRYVM rendered", len(jobs), "frames to", SHEET_DIR)
print("HENRYVM done")
