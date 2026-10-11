# Blender: fit the Deagle into Henry's captured sword fist (tools/grip_pose.py output) and pose the fingers on it.
# The fingers keep Warhorse's captured curl shape; each finger only scales its curl (slerp from straight to captured)
# so it closes onto the grip. The index finger gets its own per-joint scales so its pad lands on the trigger face.
# Hand skin is linear-blend-skinned in numpy (checked against Blender's evaluated mesh), the gun is a BVH.
# Cost: skin penetration (dominant), finger/thumb/palm contact, high grip (web under the beavertail),
# bore in line with the forearm, index pad on the trigger.
# usage: blender -b <sword grip_pose.blend> -P sword_grip_fit.py -- <deagle gltf> <out json> <render dir> <name>
import bpy, bmesh, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector, Quaternion
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
GUN_GLTF, OUT_JSON, RENDER, NAME = argv[:4]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB, BONES = arm.pose.bones, arm.data.bones
bpy.context.view_layer.update()
S = "Right"
HAND = S + "Hand"

# ---------------- gun in model space (metres, muzzle +Y, up +Z, grip pivot at origin), as build_model.py
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
part = lambda pre: [o for o in gun_objs if o.name.startswith(pre)]
gmn, gmx = bbox(part("Grip_low")); bmn, bmx = bbox(part("Barrel_low"))
pivot = Vector(((gmn.x + gmx.x) / 2, (gmn.y + gmx.y) / 2, gmx.z - (gmx.z - gmn.z) / 3))
flip = Matrix.Identity(4) if (bmx.y - pivot.y) > (pivot.y - bmn.y) else Matrix.Rotation(math.pi, 4, "Z")
to_model = Matrix.Scale(0.01, 4) @ flip @ Matrix.Translation(-pivot)
for o in gun_objs:
    o.data.transform(o.matrix_world); o.matrix_world = Matrix.Identity(4); o.data.transform(to_model)
for o in [o for o in gun_objs if o.name.startswith(("Bullet_low", "BulletCase_low"))]:
    bpy.data.objects.remove(o)
gun_objs = [o for o in scene.objects if o.type == "MESH" and o.parent is None and o not in arm.children]
def vbbox(objs):
    pts = [v.co for o in objs for v in o.data.vertices]
    return (Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)]))
def bvh_of(objs):
    bm = bmesh.new()
    for o in objs:
        bm.from_mesh(o.data)
    t = BVHTree.FromBMesh(bm); bm.free(); return t
BVH = bvh_of(gun_objs)
TBVH = bvh_of(part("Trigger_low"))
g_mn, g_mx = vbbox(part("Grip_low"))
t_mn, t_mx = vbbox(part("Trigger_low"))
# trigger face: first hit on the trigger when coming from the muzzle side, 40% down the blade
tz = t_mx.z - 0.6 * (t_mx.z - t_mn.z)
hit = TBVH.ray_cast(Vector(((t_mn.x + t_mx.x) / 2, t_mx.y + 0.003, tz)), Vector((0, -1, 0)), 0.05)
TRIGGER = hit[0] if hit[0] is not None else Vector(((t_mn.x + t_mx.x) / 2, t_mx.y, tz))
# beavertail: top-back of the grip, where the web of the hand sits in a high grip
hit = BVH.ray_cast(Vector(((g_mn.x + g_mx.x) / 2, g_mn.y - 0.05, g_mx.z - 0.006)), Vector((0, 1, 0)), 0.2)
BEAVERTAIL = hit[0] if hit[0] is not None else Vector(((g_mn.x + g_mx.x) / 2, g_mn.y, g_mx.z))
GRIP_TOP = Vector(((g_mn.x + g_mx.x) / 2, (g_mn.y + g_mx.y) / 2, g_mx.z - 0.01))
GRIP_BOT = Vector(((g_mn.x + g_mx.x) / 2, (g_mn.y + g_mx.y) / 2 - 0.012, g_mn.z + 0.01))
print("GUN grip bbox mm", [round(v * 1000) for v in (g_mx - g_mn)], "trigger", [round(v * 1000, 1) for v in TRIGGER],
      "beavertail", [round(v * 1000, 1) for v in BEAVERTAIL])

# ---------------- hand skin (numpy LBS over the right-hand subtree; other bones held at their current pose)
hands = next(o for o in arm.children if o.type == "MESH" and o.name.startswith("hands"))
gi = {g.index: g.name for g in hands.vertex_groups}
HAND_SET = {HAND} | {b.name for b in BONES[HAND].children_recursive if not any("Slot" in a.name for a in [b] + b.parent_recursive)}
def in_hand(n): return n in HAND_SET
SUB = [HAND] + sorted(HAND_SET - {HAND})
vid, infl = [], []
for v in hands.data.vertices:
    gs = [(gi[g.group], g.weight) for g in v.groups if g.weight > 0 and gi[g.group] in BONES]
    tot = sum(w for _, w in gs)
    if tot <= 0 or sum(w for n, w in gs if in_hand(n)) / tot < 0.5:
        continue
    vid.append(v.index); infl.append([(n, w / tot) for n, w in gs])
mw = hands.matrix_world
rest_v = np.array([list(mw @ hands.data.vertices[i].co) for i in vid])
used = sorted({n for l in infl for n, _ in l})
arm_mw = np.array(arm.matrix_world)
rest_inv = {n: np.linalg.inv(arm_mw @ np.array(BONES[n].matrix_local)) for n in used}
W = np.zeros((len(vid), len(used)))
for k, l in enumerate(infl):
    for n, w in l:
        W[k, used.index(n)] = w
LOC = {n: (rest_inv[n] @ np.c_[rest_v, np.ones(len(vid))].T) for n in used}  # 4 x N per bone
print("HAND VERTS", len(vid), "bones", len(used))
dom = [max(l, key=lambda t: t[1])[0] for l in infl]
def idx_of(pred): return np.array([k for k, n in enumerate(dom) if pred(n)])

# FK for the subtree from basis quaternions
rest_rel = {n: (BONES[n].parent.matrix_local.inverted() @ BONES[n].matrix_local) for n in SUB if n != HAND}
order = sorted(SUB, key=lambda n: len(BONES[n].parent_recursive))
QC = {n: PB[n].rotation_quaternion.copy() for n in SUB}  # captured sword pose
def fk(basis):
    M = {HAND: PB[HAND].matrix.copy()}
    for n in order:
        if n == HAND:
            continue
        M[n] = M[BONES[n].parent.name] @ rest_rel[n] @ basis.get(n, QC[n]).to_matrix().to_4x4()
    return M
def skin(M):
    out = np.zeros((len(vid), 3))
    for j, n in enumerate(used):
        Mw = arm_mw @ (np.array(M[n]) if n in M else np.array(PB[n].matrix))
        out += W[:, j:j + 1] * (Mw @ LOC[n])[:3].T
    return out

# verify LBS against Blender's own deformation at the captured pose
ev = hands.evaluated_get(bpy.context.evaluated_depsgraph_get()); me = ev.to_mesh()
blv = np.array([list(mw @ me.vertices[i].co) for i in vid]); ev.to_mesh_clear()
print("LBS vs Blender max mm", round(float(np.abs(skin(fk({})) - blv).max()) * 1000, 3))

FINGERS = ["Middle", "Ring", "Pinky"]
def finger_bones(f): return [f"{HAND}{f}{i}" for i in (1, 2, 3)]
P_IDX = {f: idx_of(lambda n, f=f: n.startswith(HAND + f) and n[-1] in "23") for f in FINGERS + ["Thumb", "Index"]}
P_SEG = {f + s: idx_of(lambda n, f=f, s=s: n == f"{HAND}{f}{s}") for f in FINGERS for s in "23"}
P_PALM = idx_of(lambda n: n == HAND or n.startswith(S + "InHand"))
P_INDEX_TIP = idx_of(lambda n: n in (HAND + "Index3", HAND + "Index4"))

def pw(q, s):
    """q scaled to s times its rotation angle (s may exceed 1)"""
    q = q.copy()
    if q.w < 0:
        q.negate()
    ax, ang = q.to_axis_angle()
    return Quaternion(ax, ang * s)

NP = 6 + 2 * 4 + 3   # gun(6) + [j1, j23] x (Middle, Ring, Pinky, Thumb) + Index j1, j2, j3
def basis_from(x):
    b = {}
    for k, f in enumerate(FINGERS + ["Thumb"]):
        n1, n2, n3 = finger_bones(f)
        b[n1] = pw(QC[n1], x[6 + 2 * k])
        b[n2] = pw(QC[n2], x[7 + 2 * k]); b[n3] = pw(QC[n3], x[7 + 2 * k])
    for k, n in enumerate(finger_bones("Index")):
        b[n] = pw(QC[n], x[14 + k])
    return b

# ---------------- initial gun placement from the hand frame
# Captured sword: the handle centre sat ~54 mm toward the knuckles and ~20 mm palmar of the wrist joint, its axis
# along the knuckle line. A pistol grip goes the same way: grip axis = pinky->index knuckles, bore = wrist->knuckles.
M0 = fk({})
jw = lambda M, n: Vector((arm.matrix_world @ M[n]).translation)
Hw0 = (arm.matrix_world @ M0[HAND])
grip_axis = (jw(M0, HAND + "Index1") - jw(M0, HAND + "Pinky1")).normalized()
fwd_h = (jw(M0, HAND + "Middle1") - jw(M0, HAND)).normalized()
G_AX = (GRIP_TOP - GRIP_BOT).normalized()   # grip axis in gun space (raked)
def frame(a, b):
    a = a.normalized(); b = (b - a * b.dot(a)).normalized()
    return Matrix((a, b, a.cross(b))).transposed()
R0 = frame(grip_axis, fwd_h) @ frame(G_AX, Vector((0, 1, 0))).transposed()
palm_n = (jw(M0, HAND + "Middle3") - (jw(M0, HAND + "Middle1") + jw(M0, HAND)) / 2)
palm_n = (palm_n - fwd_h * palm_n.dot(fwd_h)); palm_n = (palm_n - grip_axis * palm_n.dot(grip_axis)).normalized()
print("INIT palm normal . gun -x =", round(-(R0 @ Vector((1, 0, 0))).dot(palm_n), 3), "(expect ~ +1)")
slot_c = jw(M0, HAND) + fwd_h * 0.054 + palm_n * 0.020
T0 = slot_c - R0 @ ((GRIP_TOP + GRIP_BOT) / 2)
fore = fwd_h

def gun_matrix(x):
    w = Vector(x[3:6]); ang = w.length
    dR = Quaternion(w.normalized(), ang).to_matrix() if ang > 1e-9 else Matrix.Identity(3)
    R = dR @ R0
    return Matrix.LocRotScale(T0 + Vector(x[:3]), R.to_quaternion(), None)

# ---------------- signed distance grid of the gun around the grip (2 mm), trilinear lookups in numpy
SD_STEP = 0.002
lo_c = np.array([g_mn.x - 0.05, min(g_mn.y, BEAVERTAIL.y) - 0.04, g_mn.z - 0.04])
hi_c = np.array([g_mx.x + 0.05, TRIGGER.y + 0.06, g_mx.z + 0.07])
dims = np.ceil((hi_c - lo_c) / SD_STEP).astype(int) + 1
SDF = np.empty(dims)
# inside = the gun surrounds the point along all 6 axis rays (parts are separate, not all closed, so face-normal
# sign tests lie; enclosure is reliable for a pistol around the grip: guard openings, gaps under the slide etc. escape)
RAYS = [Vector(v) for v in ((-1, 0, 0), (1, 0, 0), (0, 0, -1), (0, 0, 1), (0, -1, 0), (0, 1, 0))]
def inside(q):
    for r in RAYS:
        if BVH.ray_cast(q, r, 0.4)[0] is None:
            return False
    return True
for i in range(dims[0]):
    for j in range(dims[1]):
        for k in range(dims[2]):
            q = Vector(lo_c + np.array([i, j, k]) * SD_STEP)
            loc, nrm, _, d = BVH.find_nearest(q, 0.08)
            if loc is None:
                SDF[i, j, k] = 0.08
            else:
                SDF[i, j, k] = -d if (d < 0.03 and inside(q)) else d
print("SDF", tuple(dims), "cells")
def sdf(Q):
    g = (Q - lo_c) / SD_STEP
    out = np.full(len(Q), 0.08)
    ok = np.all((g >= 0) & (g < dims - 1), axis=1)
    g = g[ok]; i0 = np.floor(g).astype(int); f = g - i0
    acc = np.zeros(len(g))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                acc += w * SDF[i0[:, 0] + dx, i0[:, 1] + dy, i0[:, 2] + dz]
    out[ok] = acc
    return out

# web target: on the backstrap just under the beavertail
hit = BVH.ray_cast(Vector(((g_mn.x + g_mx.x) / 2, g_mn.y - 0.05, g_mx.z - 0.018)), Vector((0, 1, 0)), 0.2)
WEB_T = hit[0] if hit[0] is not None else BEAVERTAIL
print("WEB target", [round(v * 1000, 1) for v in WEB_T])

def measure(x, detail=False, parts=None):
    G = gun_matrix(x); Gi = np.array(G.inverted())
    M = fk(basis_from(x))
    P = skin(M)
    Q = (Gi[:3, :3] @ P.T).T + Gi[:3, 3]
    d = sdf(Q)
    pen = np.maximum(-d, 0); dist = np.maximum(d, 0)
    def mind(ix): return float(dist[ix].min()) if len(ix) else 1.0
    contact = {f + s: mind(P_SEG[f + s]) for f in FINGERS for s in "23"}
    contact["Thumb"] = mind(P_IDX["Thumb"])
    palm = mind(P_PALM)
    web = (jw(M, HAND + "Thumb2") + jw(M, HAND + "Index1")) / 2
    web_d = (web - G @ WEB_T).length
    tip = P[P_INDEX_TIP]
    trig_d = float(np.linalg.norm(tip - np.array(G @ TRIGGER), axis=1).min())
    bore = 1 - (G.to_3x3() @ Vector((0, 1, 0))).normalized().dot(fore)
    terms = {"pen": 300 * float((pen ** 1).sum()) + 3000 * float(pen.max()),
             "contact": 40 * sum(max(0, v - 0.001) for v in contact.values()),
             "palm": 30 * max(0, palm - 0.002), "web": 20 * max(0, web_d - 0.015),
             "trigger": 40 * max(0, trig_d - 0.002), "bore": 0.6 * bore}
    c = sum(terms[k] for k in (parts or terms))
    if detail:
        return {"pen_max_mm": round(float(pen.max()) * 1000, 2), "pen_verts": int((pen > 0.0005).sum()),
                "contact_mm": {k: round(v * 1000, 1) for k, v in contact.items()}, "palm_mm": round(palm * 1000, 1),
                "web_mm": round(web_d * 1000, 1), "trigger_mm": round(trig_d * 1000, 1),
                "bore_deg": round(math.degrees(math.acos(max(-1, min(1, 1 - bore)))), 1), "cost": round(c, 4)}
    return c

lo = [-1] * 6 + [-0.2] * (NP - 6); hi = [1] * 6 + [1.4] * (NP - 6)
def descend(x, idxs, steps, parts, iters=600, min_pos=0.0001, label=""):
    steps = dict(steps)
    best = measure(x, parts=parts)
    for it in range(iters):
        improved = False
        for i in idxs:
            for sgn in (1, -1):
                cand = x[:]; cand[i] = min(hi[i], max(lo[i], cand[i] + sgn * steps[i]))
                c = measure(cand, parts=parts)
                if c < best:
                    x, best, improved = cand, c, True
        if not improved:
            for i in idxs:
                steps[i] *= 0.7
            if max(steps[i] for i in idxs) < min_pos:
                break
    print(f"STAGE {label}: cost {best:.5f} iters {it}", json.dumps(measure(x, detail=True)), flush=True)
    return x

GUN = list(range(6))
gun_steps = {i: (0.004 if i < 3 else 0.06) for i in GUN}
x = [0.0] * 6 + [0.15, 0.15] * 3 + [0.5, 0.3] + [0.2, 0.2, 0.2]
# 1) gun into the open hand: high grip, palm on the panel, no clipping
x = descend(x, GUN, gun_steps, ("pen", "palm", "web", "bore"), label="1 gun")
# 2) close each finger until it touches (gun fixed)
for k, f in enumerate(FINGERS + ["Thumb"]):
    ids = [6 + 2 * k, 7 + 2 * k]
    x = descend(x, ids, {i: 0.08 for i in ids}, ("pen", "contact"), min_pos=0.002, label="2 " + f)
# 3) index pad onto the trigger
x = descend(x, [14, 15, 16], {i: 0.08 for i in (14, 15, 16)}, ("pen", "trigger"), min_pos=0.002, label="3 index")
# 4) everything together, small steps
allsteps = {i: (0.001 if i < 3 else 0.015 if i < 6 else 0.02) for i in range(NP)}
x = descend(x, list(range(NP)), allsteps, None, min_pos=0.00005, label="4 joint")
fit = measure(x, detail=True)
print("FIT", json.dumps(fit), "x", [round(v, 3) for v in x])

# ---------------- apply, save, report in RightHand space
b = basis_from(x)
for n, q in b.items():
    PB[n].rotation_quaternion = q
bpy.context.view_layer.update()
G = gun_matrix(x)
gun_root = bpy.data.objects.new("gun", None); scene.collection.objects.link(gun_root); gun_root.matrix_world = G
for o in gun_objs:
    o.parent = gun_root
bpy.context.view_layer.update()
# true deformed-mesh penetration check (Blender evaluated mesh, every hand vertex)
ev = hands.evaluated_get(bpy.context.evaluated_depsgraph_get()); me = ev.to_mesh()
Gi = G.inverted(); worst, nin = 0.0, 0
for i in vid:
    q = Gi @ (mw @ me.vertices[i].co)
    loc, nrm, _, d = BVH.find_nearest(q, 0.04)
    if loc is not None and inside(q):
        worst = max(worst, d); nin += d > 0.0005
ev.to_mesh_clear()
print("TRUE MESH penetration max mm", round(worst * 1000, 2), "verts >0.5mm", nin)
hand_w = arm.matrix_world @ PB[HAND].matrix
in_hand = hand_w.inverted() @ G
json.dump({"fit": fit, "true_pen_max_mm": round(worst * 1000, 2), "true_pen_verts": nin, "x": x,
           "gun_in_hand_blender": [list(r) for r in in_hand],
           "finger_basis": {n: list(q) for n, q in b.items()},
           "points_gun_space": {"trigger": list(TRIGGER), "beavertail": list(BEAVERTAIL)}}, open(OUT_JSON, "w"), indent=1)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(RENDER, f"{NAME}.blend"))

# ---------------- renders
skin_m = bpy.data.materials.new("skin"); skin_m.diffuse_color = (0.85, 0.66, 0.55, 1)
for o in arm.children:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(skin_m)
metal = bpy.data.materials.new("metal"); metal.diffuse_color = (0.18, 0.18, 0.2, 1)
for o in gun_objs:
    o.data.materials.clear(); o.data.materials.append(metal)
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 900, 700
world = bpy.data.worlds.new("w"); world.color = (0.55, 0.6, 0.65); scene.world = world
for r in ((0.7, 0.2, 0.5), (-0.6, 0.4, -2.2), (2.0, 0.0, 1.2)):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); s.data.energy = 2.5; s.rotation_euler = r; scene.collection.objects.link(s)
gx, gy, gz = [G.to_3x3().col[i].normalized() for i in range(3)]
centre = G @ Vector((0, 0.015, -0.01))
views = {"A right side": gx * 0.34 + gz * 0.03, "B left side": -gx * 0.34 + gz * 0.03,
         "C from below": -gz * 0.3 + gy * 0.08 + gx * 0.06, "D behind (shooter)": -gy * 0.38 + gz * 0.14 + gx * 0.06}
for label, off in views.items():
    c = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(c)
    c.location = centre + off
    zc = -(centre - c.location).normalized(); up = gy if "below" in label else gz
    xc = up.cross(zc).normalized(); yc = zc.cross(xc)
    c.rotation_euler = Matrix((xc, yc, zc)).transposed().to_euler()
    c.data.lens = 45; c.data.clip_start = 0.01; scene.camera = c
    scene.render.filepath = os.path.join(RENDER, f"{NAME} {label}.png")
    bpy.ops.render.render(write_still=True)
print("FIT RENDERS DONE")
