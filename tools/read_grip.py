# Blender: measure a posed grip file (the user's v3 slider kit) and render close-ups.
# Reports per finger: deepest skin-inside-gun (6-ray enclosure test, every right-hand vertex of the evaluated mesh),
# gap to the gun, index pad to trigger, web to the back of the grip; writes gun-in-RightHand + finger rotations.
# usage: blender -b <grip.blend> -P read_grip.py -- <out json> <render dir> <name>
import bpy, bmesh, sys, os, json, math
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
OUT_JSON, RENDER, NAME = sys.argv[sys.argv.index("--") + 1:][:3]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB, BN = arm.pose.bones, arm.data.bones
H = "RightHand"
dg = bpy.context.evaluated_depsgraph_get(); bpy.context.view_layer.update()
adj = bpy.data.objects["GunAdjust"]
pieces = [o for o in scene.objects if o.type == "MESH" and o.parent is adj and o.name != "GunShell"]
bm = bmesh.new()
for o in pieces:
    t = bmesh.new(); t.from_mesh(o.data); t.transform(o.matrix_world); bm_me = bpy.data.meshes.new("t"); t.to_mesh(bm_me); bm.from_mesh(bm_me); t.free()
BVH = BVHTree.FromBMesh(bm)
trig = [o for o in pieces if o.name.startswith("Trigger")]
tb = bmesh.new()
for o in trig:
    t = bmesh.new(); t.from_mesh(o.data); t.transform(o.matrix_world); m = bpy.data.meshes.new("t"); t.to_mesh(m); tb.from_mesh(m); t.free()
TBVH = BVHTree.FromBMesh(tb)
RAYS = [Vector(v) for v in ((-1, 0, 0), (1, 0, 0), (0, 0, -1), (0, 0, 1), (0, -1, 0), (0, 1, 0))]
G = adj.matrix_world.copy(); R3 = G.to_3x3().normalized()
RAYS = [R3 @ r for r in RAYS]
def inside(q):
    return all(BVH.ray_cast(q, r, 0.4)[0] is not None for r in RAYS)

hands = next(o for o in arm.children if o.type == "MESH" and o.name.startswith("hands"))
gi = {g.index: g.name for g in hands.vertex_groups}
hand_set = {H} | {b.name for b in BN[H].children_recursive if "Slot" not in b.name and not b.name.startswith("TIP")}
ev = hands.evaluated_get(dg); me = ev.to_mesh()
per = {}
for v in hands.data.vertices:
    gs = [(gi[g.group], g.weight) for g in v.groups if g.weight > 0]
    tot = sum(w for _, w in gs)
    if tot <= 0 or sum(w for n, w in gs if n in hand_set) / tot < 0.5:
        continue
    dom = max(gs, key=lambda t: t[1])[0]
    part = next((f for f in ("Thumb", "Index", "Middle", "Ring", "Pinky") if f in dom), "Palm")
    p = hands.matrix_world @ me.vertices[v.index].co
    loc, nrm, _, d = BVH.find_nearest(p, 0.08)
    if loc is None:
        d, pen = 0.08, 0.0
    else:
        pen = d if (d < 0.03 and inside(p)) else 0.0
    r = per.setdefault(part, {"pen_max_mm": 0.0, "pen_verts": 0, "gap_mm": 999.0, "_pts": []})
    r["pen_max_mm"] = max(r["pen_max_mm"], pen * 1000); r["pen_verts"] += pen > 0.0005
    r["gap_mm"] = min(r["gap_mm"], 0.0 if pen > 0 else d * 1000)
    if part == "Index" and dom.endswith(("3", "4")):
        r["_pts"].append(p)
ev.to_mesh_clear()
idx = per.get("Index", {}).pop("_pts", [])
for r in per.values():
    r.pop("_pts", None); r["pen_max_mm"] = round(r["pen_max_mm"], 1); r["gap_mm"] = round(r["gap_mm"], 1)
trig_d = min((TBVH.find_nearest(p)[3] for p in idx), default=1.0) * 1000
jw = lambda n: arm.matrix_world @ PB[n].head
def eff_basis(pb):
    """local rotation as evaluated (drivers, IK and all): rest-relative, parent-relative"""
    if pb.parent is None:
        return pb.matrix.to_quaternion()
    rest_rel = pb.parent.bone.matrix_local.inverted() @ pb.bone.matrix_local
    return (rest_rel.inverted() @ pb.parent.matrix.inverted() @ pb.matrix).to_quaternion()
hand_w = arm.matrix_world @ PB[H].matrix
in_hand = hand_w.inverted() @ G
rep = {"parts": per, "index_pad_to_trigger_mm": round(trig_d, 1),
       "sliders": {k: round(arm[k], 1) for k in arm.keys() if k[:1].isdigit()}}
print("GRIP", json.dumps(rep))
json.dump({**rep, "gun_in_hand_blender": [list(r) for r in in_hand],
           "finger_basis": {b.name: list(eff_basis(b)) for b in PB if b.name in hand_set}},
          open(OUT_JSON, "w"), indent=1)

# renders (EEVEE): only the hand/forearm meshes and the gun
for o in arm.children:
    if o.type == "MESH" and not o.name.startswith(("hands", "arms")):
        o.hide_render = True
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 900, 700
world = bpy.data.worlds.new("w"); world.color = (0.5, 0.53, 0.57); scene.world = world
for r in ((0.7, 0.2, 0.5), (-0.6, 0.4, -2.2), (2.0, 0.0, 1.2)):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); s.data.energy = 2.5; s.rotation_euler = r; scene.collection.objects.link(s)
gx, gy, gz = [R3.col[i] for i in range(3)]
centre = G @ Vector((0, 0.01, -0.02))
views = {"1 left side (fingertips)": (-gx, gz), "2 right side (back of hand)": (gx, gz),
         "3 from below": (-gz + gy * 0.25 - gx * 0.2, gy), "4 shooter view": (-gy * 1.0 + gz * 0.35 + gx * 0.15, gz)}
for label, (d, up) in views.items():
    c = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(c)
    c.location = centre + d.normalized() * 0.33
    zc = (c.location - centre).normalized(); xc = up.cross(zc).normalized(); yc = zc.cross(xc)
    c.rotation_euler = Matrix((xc, yc, zc)).transposed().to_euler()
    c.data.lens = 45; c.data.clip_start = 0.005; scene.camera = c
    scene.render.filepath = os.path.join(RENDER, f"{NAME} {label}.png")
    bpy.ops.render.render(write_still=True)
print("GRIP RENDERS DONE")
