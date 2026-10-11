# Blender: fit the Deagle INTO Henry's captured two-hand pistol grip (Mixamo pistol idle, exact-frame rig).
# Fingers are left as captured; the gun's 6-DOF placement is searched to minimise:
#   hand skin inside the gun (clipping), grip not inside the right fist, index pad away from the trigger,
#   and tilt away from "muzzle forward, upright". Writes gun-in-RightHand transform + close-ups.
# usage: blender -b <pose.blend> -P gun_fit.py -- <deagle gltf> <out json> <render dir>
import bpy, bmesh, sys, os, json, math, random
from mathutils import Matrix, Vector, Euler
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:]
GUN_GLTF, OUT_JSON, RENDER = argv[:3]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB = arm.pose.bones
scene.frame_set(0)
bpy.context.view_layer.update()
jw = lambda n: arm.matrix_world @ PB[n].head
bw = lambda n: arm.matrix_world @ PB[n].matrix

# ---- gun in model space (metres, muzzle +Y, up +Z, grip pivot at origin), as build_model.py
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
g_mn, g_mx = vbbox([o for o in gun_objs if o.name.startswith("Grip_low")])
t_mn, t_mx = vbbox([o for o in gun_objs if o.name.startswith("Trigger_low")])
TRIGGER = Vector(((t_mn.x + t_mx.x) / 2, t_mn.y + (t_mx.y - t_mn.y) * 0.4, (t_mn.z + t_mx.z) / 2 - 0.004))
GRIP_TOP = Vector(((g_mn.x + g_mx.x) / 2, (g_mn.y + g_mx.y) / 2, g_mx.z - 0.01))
GRIP_BOT = Vector(((g_mn.x + g_mx.x) / 2, (g_mn.y + g_mx.y) / 2 - 0.01, g_mn.z + 0.01))
bm = bmesh.new()
for o in gun_objs:
    tmp = bpy.data.meshes.new("t"); tmp_bm = bmesh.new(); tmp_bm.from_mesh(o.data); tmp_bm.to_mesh(tmp); bm.from_mesh(tmp); tmp_bm.free()
BVH = BVHTree.FromBMesh(bm)  # in gun space

# ---- hand skin points (world), right / left
hands = next(o for o in arm.children if o.type == "MESH" and o.name.startswith("hands"))
ev = hands.evaluated_get(bpy.context.evaluated_depsgraph_get()); me = ev.to_mesh()
groups = {g.index: g.name for g in hands.vertex_groups}
pts = {"Right": [], "Left": []}
for v in me.vertices:
    gs = [(groups[g.group], g.weight) for g in hands.data.vertices[v.index].groups if g.weight > 0.25]
    side = "Right" if any(n.startswith("Right") for n, _ in gs) else "Left" if any(n.startswith("Left") for n, _ in gs) else None
    if side:
        pts[side].append(hands.matrix_world @ v.co)
ev.to_mesh_clear()
pts = {k: v[::2] for k, v in pts.items()}
print("HAND POINTS", {k: len(v) for k, v in pts.items()})

fist = (jw("RightHandMiddle1") + jw("RightHandRing1") + jw("RightHandMiddle2") + jw("RightHand") * 0.5) / 3.5
index_pad = jw("RightHandIndex3")
left_palm = (jw("LeftHandMiddle1") + jw("LeftHand")) / 2
# desired orientation: muzzle along the forearm-to-fist "pointing" direction, up = world up
point_dir = ((jw("RightHandMiddle1") - jw("RightForeArm")).normalized())
world_up = Vector((0, 0, 1))

def gun_matrix(x):
    return Matrix.LocRotScale(Vector(x[:3]), Euler(x[3:]).to_quaternion(), None)

def cost(x, detail=False):
    G = gun_matrix(x); Gi = G.inverted()
    pen = 0.0
    for side, w in (("Right", 1.0), ("Left", 0.7)):
        for p in pts[side]:
            q = Gi @ p
            loc, nrm, idx, d = BVH.find_nearest(q, 0.03)
            if loc is not None and (q - loc).dot(nrm) < 0:
                pen += w * d
    # grip axis through the fist
    a, b = G @ GRIP_TOP, G @ GRIP_BOT
    ab = b - a; t = max(0, min(1, (fist - a).dot(ab) / ab.length_squared))
    grip_d = (a + ab * t - fist).length
    trig_d = (G @ TRIGGER - index_pad).length
    left_d = max(0.0, (G @ ((GRIP_TOP + GRIP_BOT) / 2 - Vector((0.03, 0, 0))) - left_palm).length - 0.03)
    muzzle = G.to_3x3() @ Vector((0, 1, 0)); up = G.to_3x3() @ Vector((0, 0, 1))
    ang = (1 - muzzle.normalized().dot(point_dir)) + 0.5 * (1 - up.normalized().dot(world_up))
    c = 40 * pen + 6 * grip_d + 4 * trig_d + 2 * left_d + 0.3 * ang
    if detail:
        return {"pen_sum": round(pen, 4), "grip_d": round(grip_d, 4), "trigger_d": round(trig_d, 4), "left_d": round(left_d, 4), "ang": round(ang, 3), "cost": round(c, 4)}
    return c

# initial: grip pivot at the fist, muzzle along point_dir, upright
y = point_dir; z = (world_up - y * world_up.dot(y)).normalized(); xax = y.cross(z)
R0 = Matrix((xax, y, z)).transposed()
best = list(fist) + list(R0.to_euler())
best_c = cost(best)
steps = [0.006, 0.006, 0.006, 0.06, 0.06, 0.06]
random.seed(1)
for it in range(500):
    improved = False
    for i in range(6):
        for sgn in (1, -1):
            cand = best[:]; cand[i] += sgn * steps[i]
            c = cost(cand)
            if c < best_c:
                best, best_c, improved = cand, c, True
    if not improved:
        steps = [s * 0.6 for s in steps]
        if max(steps[:3]) < 0.0004:
            break
print("FIT", cost(best, True), "iters", it)

G = gun_matrix(best)
gun_root = bpy.data.objects.new("gun", None); scene.collection.objects.link(gun_root); gun_root.matrix_world = G
for o in gun_objs:
    o.parent = gun_root
bpy.context.view_layer.update()
in_hand = bw("RightHand").inverted() @ G
in_slot = bw("RightHandSlot").inverted() @ G
json.dump({"gun_in_RightHand": [list(r) for r in in_hand], "gun_in_RightHandSlot": [list(r) for r in in_slot], "fit": cost(best, True)},
          open(OUT_JSON, "w"), indent=1)
e = in_hand.to_euler("XYZ")
print("GUN IN RightHand pos", tuple(round(v, 4) for v in in_hand.translation), "ang_deg", tuple(round(math.degrees(a), 2) for a in e))

# ---- renders
skin = bpy.data.materials.new("skin"); skin.diffuse_color = (0.85, 0.66, 0.55, 1)
for o in arm.children:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(skin)
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 900, 700
world = bpy.data.worlds.new("w"); world.color = (0.55, 0.6, 0.65); scene.world = world
for r in ((0.7, 0.2, 0.5), (-0.6, 0.4, -2.2)):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); s.data.energy = 3; s.rotation_euler = r; scene.collection.objects.link(s)
gx, gy, gz = [G.to_3x3().col[i].normalized() for i in range(3)]
centre = G @ Vector((0, 0.01, -0.02))
views = {"A right side": gx * 0.33 + gz * 0.03, "B left side": -gx * 0.33 + gz * 0.03, "C from below": -gz * 0.3 + gy * 0.06 + gx * 0.08,
         "D behind (shooter)": -gy * 0.36 + gz * 0.14 + gx * 0.05}
for label, off in views.items():
    c = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(c)
    c.location = centre + off; c.rotation_euler = (centre - c.location).to_track_quat("-Z", "Z" if "below" in label else "Y").to_euler()
    c.data.lens = 42; scene.camera = c
    scene.render.filepath = os.path.join(RENDER, f"fit {label}.png")
    bpy.ops.render.render(write_still=True)
print("FIT RENDERS DONE")
