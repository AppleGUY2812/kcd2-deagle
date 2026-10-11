# Blender: remove clipping from a user-posed grip with the smallest changes. Bakes the evaluated pose (IK/drivers)
# into plain rotations, then nudges finger joints (curl/splay deltas, degrees, on the exact joint axes) and the gun
# (mm / deg) to minimise: skin inside the gun (6-ray enclosure SDF) + drift from the user's pose, while keeping each
# finger touching and the index pad on the trigger point the user chose. Writes a polished .blend + metrics.
# usage: blender -b <user grip.blend> -P grip_polish.py -- <out .blend> <out json>
import bpy, bmesh, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector, Euler, Quaternion
from mathutils.bvhtree import BVHTree
_a = sys.argv[sys.argv.index("--") + 1:]
OUT_BLEND, OUT_JSON = _a[:2]
SOFT = float(_a[2]) if len(_a) > 2 else 0.0          # mm of skin compression allowed (real skin presses on a grip)
DEV_W = float(_a[3]) if len(_a) > 3 else 0.002       # cost per deg^2 of finger drift from the user's pose
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB, BN = arm.pose.bones, arm.data.bones
H = "RightHand"
F4 = ["Index", "Middle", "Ring", "Pinky"]
bpy.context.view_layer.update()

# ---- bake the evaluated pose into FK, drop IK/shrinkwrap
def eff_basis(pb):
    rest_rel = pb.parent.bone.matrix_local.inverted() @ pb.bone.matrix_local
    return (rest_rel.inverted() @ pb.parent.matrix.inverted() @ pb.matrix).to_quaternion()
HAND_SET = {b.name for b in BN[H].children_recursive if "Slot" not in b.name and not b.name.startswith("TIP")
            and not any("Slot" in a.name for a in b.parent_recursive)}
baked = {n: eff_basis(PB[n]) for n in HAND_SET}
for n in HAND_SET | {f"TIP {f}" for f in F4 + ["Thumb"]}:
    if n in PB:
        for c in list(PB[n].constraints):
            PB[n].constraints.remove(c)
if arm.animation_data:
    for fc in [fc for fc in arm.animation_data.drivers if any(f'"{n}"' in fc.data_path for n in HAND_SET)]:
        arm.animation_data.drivers.remove(fc)
for n, q in baked.items():
    PB[n].rotation_mode = "QUATERNION"; PB[n].rotation_quaternion = q
bpy.context.view_layer.update()

# ---- gun in its own space
adj = bpy.data.objects["GunAdjust"]; base = bpy.data.objects["GunBase"]
G0 = adj.matrix_world.copy()
pieces = [o for o in scene.objects if o.type == "MESH" and o.parent is adj and o.name != "GunShell"]
bm = bmesh.new()
for o in pieces:
    t = bmesh.new(); t.from_mesh(o.data); t.transform(o.matrix_basis); m = bpy.data.meshes.new("t"); t.to_mesh(m); bm.from_mesh(m); t.free()
BVH = BVHTree.FromBMesh(bm)
RAYS = [Vector(v) for v in ((-1, 0, 0), (1, 0, 0), (0, 0, -1), (0, 0, 1), (0, -1, 0), (0, 1, 0))]
def inside(q): return all(BVH.ray_cast(q, r, 0.4)[0] is not None for r in RAYS)

# ---- hand skin LBS (numpy) over the right-hand subtree
hands = next(o for o in arm.children if o.type == "MESH" and o.name.startswith("hands"))
gi = {g.index: g.name for g in hands.vertex_groups}
SET = HAND_SET | {H}
vid, infl = [], []
for v in hands.data.vertices:
    gs = [(gi[g.group], g.weight) for g in v.groups if g.weight > 0 and gi[g.group] in BN]
    tot = sum(w for _, w in gs)
    if tot <= 0 or sum(w for n, w in gs if n in SET) / tot < 0.5:
        continue
    vid.append(v.index); infl.append([(n, w / tot) for n, w in gs])
mw = hands.matrix_world; arm_mw = np.array(arm.matrix_world)
rest_v = np.array([list(mw @ hands.data.vertices[i].co) for i in vid])
used = sorted({n for l in infl for n, _ in l})
W = np.zeros((len(vid), len(used)))
for k, l in enumerate(infl):
    for n, w in l:
        W[k, used.index(n)] = w
LOC = {n: np.linalg.inv(arm_mw @ np.array(BN[n].matrix_local)) @ np.c_[rest_v, np.ones(len(vid))].T for n in used}
dom = [max(l, key=lambda t: t[1])[0] for l in infl]
def ix(pred): return np.array([k for k, n in enumerate(dom) if pred(n)], dtype=int)
order = sorted(HAND_SET, key=lambda n: len(BN[n].parent_recursive))
rest_rel = {n: BN[n].parent.matrix_local.inverted() @ BN[n].matrix_local for n in HAND_SET}
HAND_M = PB[H].matrix.copy()
def fk(basis):
    M = {H: HAND_M}
    for n in order:
        M[n] = M[BN[n].parent.name] @ rest_rel[n] @ basis[n].to_matrix().to_4x4()
    return M
def skin(M):
    out = np.zeros((len(vid), 3))
    for j, n in enumerate(used):
        Mw = arm_mw @ np.array(M[n] if n in M else PB[n].matrix)
        out += W[:, j:j + 1] * (Mw @ LOC[n])[:3].T
    return out
ev = hands.evaluated_get(bpy.context.evaluated_depsgraph_get()); me = ev.to_mesh()
blv = np.array([list(mw @ me.vertices[i].co) for i in vid]); ev.to_mesh_clear()
print("LBS vs Blender max mm", round(float(np.abs(skin(fk(baked)) - blv).max()) * 1000, 3))

# ---- SDF in gun space over the hand's region
Gi0 = np.array(G0.inverted())
Q0 = (Gi0[:3, :3] @ blv.T).T + Gi0[:3, 3]
STEP = 0.0015
lo_c = Q0.min(0) - 0.02; hi_c = Q0.max(0) + 0.02
dims = np.ceil((hi_c - lo_c) / STEP).astype(int) + 1
SDF = np.empty(dims)
for i in range(dims[0]):
    for j in range(dims[1]):
        for k in range(dims[2]):
            q = Vector(lo_c + np.array([i, j, k]) * STEP)
            loc, nrm, _, d = BVH.find_nearest(q, 0.06)
            SDF[i, j, k] = 0.06 if loc is None else (-d if (d < 0.025 and inside(q)) else d)
print("SDF", tuple(dims))
def sdf(Q):
    g = (Q - lo_c) / STEP; out = np.full(len(Q), 0.06)
    ok = np.all((g >= 0) & (g < dims - 1), axis=1); g = g[ok]; i0 = np.floor(g).astype(int); f = g - i0
    acc = np.zeros(len(g))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                acc += w * SDF[i0[:, 0] + dx, i0[:, 1] + dy, i0[:, 2] + dz]
    out[ok] = acc; return out

# ---- parameters: finger joint deltas (deg) + gun delta (mm, deg) in gun space
JOINTS = []   # (bone, axis index 0=x twist 1=y splay 2=z curl)
for f in F4:
    JOINTS += [(f"{H}{f}1", 2), (f"{H}{f}1", 1), (f"{H}{f}2", 2), (f"{H}{f}3", 2)]
JOINTS += [(f"{H}Thumb1", 0), (f"{H}Thumb1", 1), (f"{H}Thumb1", 2), (f"{H}Thumb2", 2), (f"{H}Thumb3", 2)]
NJ = len(JOINTS); NP = NJ + 6
def basis_from(x):
    b = dict(baked)
    eul = {}
    for k, (n, a) in enumerate(JOINTS):
        e = eul.setdefault(n, [0.0, 0.0, 0.0]); e[a] += math.radians(x[k])
    for n, e in eul.items():
        b[n] = baked[n] @ Euler(e, "XYZ").to_quaternion()
    return b
def gun_from(x):
    t = Vector(x[NJ:NJ + 3]) * 0.001
    r = Euler([math.radians(v) for v in x[NJ + 3:NJ + 6]], "XYZ").to_matrix().to_4x4()
    return G0 @ Matrix.Translation(t) @ r

SEG = {f: ix(lambda n, f=f: n.startswith(f"{H}{f}") and n[-1] in "23") for f in F4 + ["Thumb"]}
IDX_TIP = ix(lambda n: n in (f"{H}Index3", f"{H}Index4"))
P0 = skin(fk(baked)); Q = (Gi0[:3, :3] @ P0.T).T + Gi0[:3, 3]
k_pad = IDX_TIP[np.argmin(np.abs(sdf(Q[IDX_TIP])))]
pad_q = Q[k_pad]; loc, _, _, _ = BVH.find_nearest(Vector(pad_q)); TRIG = np.array(loc)   # the user's trigger contact
def evaluate(x, detail=False):
    P = skin(fk(basis_from(x))); Gi = np.array(gun_from(x).inverted())
    Qx = (Gi[:3, :3] @ P.T).T + Gi[:3, 3]; d = sdf(Qx)
    pen = np.maximum(-d - SOFT * 0.001, 0)
    gaps = {f: float(np.maximum(d[SEG[f]], 0).min()) for f in SEG}
    trig = float(np.linalg.norm(Qx[IDX_TIP] - TRIG, axis=1).min())
    dev = sum(v * v for v in x[:NJ]) * DEV_W + sum(v * v for v in x[NJ:NJ + 3]) * 0.004 + sum(v * v for v in x[NJ + 3:]) * 0.05
    c = 300 * float(pen.sum()) + 3000 * float(pen.max()) + dev + 40 * sum(max(0, g - 0.0015) for g in gaps.values()) + 40 * max(0, trig - 0.002)
    if detail:
        parts = {}
        for name, sel in [("Palm", ix(lambda n: n == H or "InHand" in n))] + [(f, ix(lambda n, f=f: f in n)) for f in F4 + ["Thumb"]]:
            parts[name] = round(float(pen[sel].max()) * 1000, 1) if len(sel) else 0
        return {"pen_max_mm_by_part": parts, "gap_mm": {k: round(v * 1000, 1) for k, v in gaps.items()},
                "trigger_mm": round(trig * 1000, 1), "cost": round(c, 4)}
    return c

x = [0.0] * NP
print("BEFORE", json.dumps(evaluate(x, True)), flush=True)
steps = [2.0] * NJ + [0.8] * 3 + [1.0] * 3
best = evaluate(x)
for it in range(600):
    improved = False
    for i in range(NP):
        for sgn in (1, -1):
            cand = x[:]; cand[i] += sgn * steps[i]
            c = evaluate(cand)
            if c < best:
                x, best, improved = cand, c, True
    if not improved:
        steps = [s * 0.6 for s in steps]
        if max(steps) < 0.05:
            break
after = evaluate(x, True)
print("AFTER", json.dumps(after), "iters", it)
print("CHANGES deg", {f"{n[9:]}.{'xyz'[a]}": round(x[k], 1) for k, (n, a) in enumerate(JOINTS) if abs(x[k]) >= 0.5},
      "gun mm", [round(v, 1) for v in x[NJ:NJ + 3]], "gun deg", [round(v, 1) for v in x[NJ + 3:]])

# ---- apply: finger rotations, gun via GunBase (keeps the user's slider values meaningful)
for n, q in basis_from(x).items():
    PB[n].rotation_quaternion = q
Gn = gun_from(x)
base.matrix_world = Gn @ adj.matrix_basis.inverted()
bpy.context.view_layer.update()
# true-mesh check
dgr = bpy.context.evaluated_depsgraph_get(); ev = hands.evaluated_get(dgr); me = ev.to_mesh()
Gi = adj.matrix_world.inverted(); worst = 0.0; nin = 0
for i in vid:
    q = Gi @ (mw @ me.vertices[i].co); loc, _, _, d = BVH.find_nearest(q, 0.06)
    if loc is not None and d < 0.025 and inside(q):
        worst = max(worst, d); nin += d > 0.0005
ev.to_mesh_clear()
print("TRUE MESH after: max mm", round(worst * 1000, 2), "verts >0.5mm", nin)
for b in BN:
    b.hide = not b.name.startswith("TIP ") if False else True
bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
json.dump({"before": evaluate([0.0] * NP, True), "after": after, "true_max_mm": round(worst * 1000, 2), "true_verts": nin,
           "x": x, "joints": JOINTS}, open(OUT_JSON, "w"), indent=1)
print("POLISH SAVED", OUT_BLEND)
