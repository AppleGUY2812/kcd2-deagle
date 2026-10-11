# Blender: pose the exact Henry rig from a live capture (tools/hand_frames.py JSON) and render hand close-ups.
# Basis of a bone = conv @ (Brel^T @ Rp^T @ Rc) @ conv^T, where R* are the captured joint frames in glTF-local axes,
# Brel the bind rotation parent->child (glTF) and conv the glTF->Blender axis change used by henry_rig.
# usage: blender -b -P grip_pose.py -- <body glb dir> <frames.json> <out dir> <name> [Right|Left]
import bpy, sys, os, json, math
from mathutils import Matrix, Vector
argv = sys.argv[sys.argv.index("--") + 1:]
BODY_DIR, FRAMES, OUT, NAME = argv[:4]
SIDE = argv[4] if len(argv) > 4 else "Right"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import henry_rig
bpy.ops.wm.read_factory_settings(use_empty=True)
glbs = [os.path.join(BODY_DIR, f) for f in ("male_body.glb", "male_body_arms.glb", "male_body_hands.glb")]
arm = henry_rig.import_henry(glbs)
for o in [o for o in bpy.context.scene.objects if o.type == "MESH" and o.name.startswith("Icosphere")]:
    bpy.data.objects.remove(o)
scene = bpy.context.scene
binds, parents = {}, {}
for g in glbs:
    b, p = henry_rig.joint_binds(g)
    for k, v in b.items():
        binds.setdefault(k, v); parents.setdefault(k, p.get(k))
conv = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))
fr = json.load(open(FRAMES))["bones"]
R = {k: Matrix(v["R"]) for k, v in fr.items() if v["R"] is not None}

def basis(name):
    p = parents.get(name)
    if name not in R or p not in R:
        return None
    brel = (binds[p].inverted() @ binds[name]).to_3x3().normalized()
    q = brel.transposed() @ R[p].transposed() @ R[name]
    return (conv @ q @ conv.transposed()).to_quaternion()

posed = []
for pb in arm.pose.bones:
    pb.rotation_mode = "QUATERNION"
    if not (pb.name.startswith(("Right", "Left")) and ("Hand" in pb.name or "ForeArm" in pb.name)):
        continue
    if "Slot" in pb.name:
        continue
    q = basis(pb.name)
    if q is not None:
        pb.rotation_quaternion = q; posed.append(pb.name)
bpy.context.view_layer.update()
print("POSED", len(posed), sorted(posed))

# self-check: captured child offsets (in the parent's captured frame) vs the posed rig, hand-relative
def jw(n): return arm.matrix_world @ arm.pose.bones[n].head
H = arm.pose.bones[SIDE + "Hand"]
hw = (arm.matrix_world @ H.matrix).to_3x3()
Rh = R[SIDE + "Hand"]
worst = 0.0
for n in [k for k in fr if k.startswith(SIDE) and "Slot" not in k and k != SIDE + "Hand" and "Arm" not in k]:
    cap_local = Rh.transposed() @ (Vector(fr[n]["pos"]) - Vector(fr[SIDE + "Hand"]["pos"]))  # glTF-local of hand
    rig_local = conv.transposed() @ (hw.transposed() @ (jw(n) - jw(SIDE + "Hand")))            # Blender-local -> glTF-local
    worst = max(worst, (cap_local - rig_local).length)
print("HAND-RELATIVE JOINT ERROR max mm", round(worst * 1000, 3))

os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, f"{NAME}_pose.blend"))

# renders around the hand
skin = bpy.data.materials.new("skin"); skin.diffuse_color = (0.85, 0.66, 0.55, 1)
for o in arm.children:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(skin)
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 900, 700
world = bpy.data.worlds.new("w"); world.color = (0.55, 0.6, 0.65); scene.world = world
for r in ((0.7, 0.2, 0.5), (-0.6, 0.4, -2.2)):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); s.data.energy = 3; s.rotation_euler = r; scene.collection.objects.link(s)
P = SIDE + "Hand"
c = (jw(P + "Index1") + jw(P + "Pinky1") + jw(P) + jw(P + "Middle2")) / 4
f = (jw(P + "Middle1") - jw(P)).normalized()
s = (jw(P + "Index1") - jw(P + "Pinky1")); s = (s - f * s.dot(f)).normalized()
n = f.cross(s)
if (jw(P + "Middle3") - c).dot(n) < 0:
    n = -n
views = {"palm side": n, "back of hand": -n, "thumb side": s, "pinky side": -s}
for label, d in views.items():
    cam = bpy.data.objects.new(label, bpy.data.cameras.new(label)); scene.collection.objects.link(cam)
    cam.location = c + d * 0.32 + f * 0.04
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam.data.lens = 50; cam.data.clip_start = 0.01; scene.camera = cam
    scene.render.filepath = os.path.join(OUT, f"{NAME} {label}.png")
    bpy.ops.render.render(write_still=True)
print("GRIP RENDERS DONE")
