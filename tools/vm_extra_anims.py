# Blender: extra viewmodel clips on top of the (gun-bone-filled) MWII actions:
#   @idle_empty  idle with the slide locked back (pose of j_slide/j_slidestop from the last frame of @shoot_empty)
#   @holster     @draw reversed
#   @ads         aim-down-sights pose: the gun's sight line moved onto the camera axis (Source view space: eye at the
#                origin looking +X, up +Z), both arms re-solved with analytic 2-bone IK (shoulders fixed, elbows keep
#                their bend plane), wrists take the moved hip orientation so the hands keep their grip.
# usage: blender -b mwii_vm_filled.blend -P vm_extra_anims.py -- <out.blend> <render dir> [eye_relief_m]
import bpy, sys, os, math
from mathutils import Matrix, Vector, Quaternion
argv = sys.argv[sys.argv.index("--") + 1:]
OUT, RENDER = argv[0], argv[1]
RELIEF = float(argv[2]) if len(argv) > 2 else 0.24     # eye to rear sight (m)
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB = arm.pose.bones
A = bpy.data.actions

def bake_pose(name, f0, f1):
    act = A.new(name); act.use_fake_user = True
    for pb in PB:
        pb.rotation_mode = "QUATERNION"
        for i in range(3):
            fc = act.fcurves.new(f'pose.bones["{pb.name}"].location', index=i, action_group=pb.name)
            fc.keyframe_points.insert(f0, pb.location[i]); fc.keyframe_points.insert(f1, pb.location[i])
        for i in range(4):
            fc = act.fcurves.new(f'pose.bones["{pb.name}"].rotation_quaternion', index=i, action_group=pb.name)
            fc.keyframe_points.insert(f0, pb.rotation_quaternion[i]); fc.keyframe_points.insert(f1, pb.rotation_quaternion[i])
    return act

def pose_at(act, f):
    arm.animation_data.action = act; scene.frame_set(f); bpy.context.view_layer.update()

# ---- @idle_empty
pose_at(A["@shoot_empty"], int(A["@shoot_empty"].frame_range[1]))
locked = {n: (PB[n].location.copy(), PB[n].rotation_quaternion.copy()) for n in ("j_slide", "j_slidestop")}
pose_at(A["@idle1"], 1)
for n, (l, r) in locked.items():
    PB[n].location, PB[n].rotation_quaternion = l, r
bake_pose("@idle_empty", 1, 2)
print("MADE @idle_empty")

# ---- @holster: draw reversed
d = A["@draw"]; h = d.copy(); h.name = "@holster"; h.use_fake_user = True
f0, f1 = d.frame_range
for fc in h.fcurves:
    for kp in fc.keyframe_points:
        kp.co.x = f0 + f1 - kp.co.x
        kp.handle_left.x = f0 + f1 - kp.handle_left.x; kp.handle_right.x = f0 + f1 - kp.handle_right.x
    fc.keyframe_points.sort() if hasattr(fc.keyframe_points, "sort") else None
    fc.update()
print("MADE @holster", tuple(h.frame_range))

# ---- @ads
pose_at(A["@idle1"], 1)
dg = bpy.context.evaluated_depsgraph_get()
gun = next(o for o in scene.objects if o.type == "MESH" and o.name.startswith("deagle"))
gi = {g.index: g.name for g in gun.vertex_groups}
me = gun.evaluated_get(dg).to_mesh()
slide, body = [], []
for v in gun.data.vertices:
    names = [gi[g.group] for g in v.groups if g.weight > 0.5]
    w = gun.matrix_world @ me.vertices[v.index].co
    if "j_slide" in names: slide.append(w)
    if any(n in ("j_slide", "j_gun", "tag_pistol_offset") for n in names): body.append(w)   # slide + barrel/frame
gun.evaluated_get(dg).to_mesh_clear()
cen = sum(slide, Vector()) / len(slide)
# principal axis of the slide = bore direction, pointing toward the muzzle (tag_flash)
cov = Matrix(((0,) * 3,) * 3)
for p in slide:
    d = p - cen
    for i in range(3):
        for j in range(3):
            cov[i][j] += d[i] * d[j]
ax = Vector((1, 0, 0))
for _ in range(50):
    ax = (cov @ ax).normalized()
flash = arm.matrix_world @ PB["tag_flash"].head
if (flash - cen).dot(ax) < 0:
    ax = -ax
up0 = Vector((0, 0, 1)); up0 = (up0 - ax * up0.dot(ax)).normalized()
ts = [(p - cen).dot(ax) for p in slide]
tmin, tmax = min(ts), max(ts)
# sights on the slide's centreline: lateral = midpoint of the slide's left/right extent (the rear sight has two ears
# with the notch between them), height = top of the region, position along the bore = middle of the region
side = up0.cross(ax).normalized()
def sight(region, src=None):
    src = src or slide
    pts = [p for p in src if region((p - cen).dot(ax))]
    top = max(p.dot(up0) for p in pts)
    upper = [p for p in pts if p.dot(up0) > top - 0.004]            # the sight blade / ears
    lat = (min(p.dot(side) for p in pts) + max(p.dot(side) for p in pts)) / 2
    along = sum(p.dot(ax) for p in upper) / len(upper)
    return ax * along + side * lat + up0 * top
rear = sight(lambda t: t < tmin + 0.03)
# the Deagle's front sight is on the barrel (not the slide): search the whole gun body's front 2.5 cm
tb = [(p - cen).dot(ax) for p in body]
front = sight(lambda t: t > max(tb) - 0.025, body)
print("BODY verts", len(body), "front of gun at", round(max(tb), 3), "front of slide at", round(tmax, 3))
sight = (front - rear).normalized()
print("SIGHTS rear", [round(v, 3) for v in rear], "front", [round(v, 3) for v in front], "slide len", round(tmax - tmin, 3))
# rigid transform: sight line -> +X through the eye, gun upright, rear sight at RELIEF
gup = (up0 - sight * up0.dot(sight)).normalized()
Rsrc = Matrix((sight, gup.cross(sight), gup)).transposed()    # columns: fwd, left (= up x fwd), up
Rdst = Matrix((Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))).transposed()
R = Rdst @ Rsrc.transposed()
T = Matrix.Translation(Vector((RELIEF, 0, 0)) - R @ rear) @ R.to_4x4()
print("ADS move", [round(v, 3) for v in T.translation], "turn deg", round(math.degrees(R.to_quaternion().angle), 1), "det", round(R.determinant(), 3))

def solve_arm(side):
    S_, E_, W_ = (f"j_shoulder_{side}", f"j_elbow_{side}", f"j_wrist_{side}")
    mw = arm.matrix_world
    wm = {n: mw @ PB[n].matrix for n in (S_, E_, W_)}
    S, E, W = wm[S_].translation, wm[E_].translation, wm[W_].translation
    Wt_m = T @ wm[W_]
    Wt = Wt_m.translation
    L1, L2 = (E - S).length, (W - E).length
    d = min((Wt - S).length, L1 + L2 - 1e-4)
    axis = (Wt - S).normalized()
    pole = (E - S) - axis * (E - S).dot(axis)
    pole = pole.normalized() if pole.length > 1e-6 else Vector((0, 0, -1))
    a = (L1 * L1 - L2 * L2 + d * d) / (2 * d)
    h = math.sqrt(max(0.0, L1 * L1 - a * a))
    Et = S + axis * a + pole * h
    inv = mw.inverted()
    # shoulder: rotate about S so the upper arm points at Et
    q1 = (E - S).normalized().rotation_difference((Et - S).normalized())
    M = Matrix.Translation(S) @ q1.to_matrix().to_4x4() @ Matrix.Translation(-S) @ wm[S_]
    PB[S_].matrix = inv @ M; bpy.context.view_layer.update()
    # elbow: now at Et; rotate so the forearm points at Wt
    Ecur = (mw @ PB[E_].matrix).translation; Wcur = (mw @ PB[W_].matrix).translation
    q2 = (Wcur - Ecur).normalized().rotation_difference((Wt - Ecur).normalized())
    M = Matrix.Translation(Ecur) @ q2.to_matrix().to_4x4() @ Matrix.Translation(-Ecur) @ (mw @ PB[E_].matrix)
    PB[E_].matrix = inv @ M; bpy.context.view_layer.update()
    # wrist: the moved hip orientation (keeps the grip)
    PB[W_].matrix = inv @ Wt_m; bpy.context.view_layer.update()
    err = ((mw @ PB[W_].matrix).translation - Wt).length * 1000
    print(f"IK {side}: reach {d:.3f}/{L1 + L2:.3f} m, wrist error {err:.2f} mm, elbow moved {(Et - E).length * 1000:.0f} mm")

for pb in PB:
    pb.rotation_mode = "QUATERNION"
# ADS = the whole upper body (j_mainroot: torso, arms, gun) moved rigidly by T. The hip pose's arms are already ~98%
# straight, so per-arm IK can only stretch them; the rigid move keeps the arm shapes and puts the sights on centre.
root = PB["j_mainroot"]
root.matrix = arm.matrix_world.inverted() @ T @ (arm.matrix_world @ root.matrix)
bpy.context.view_layer.update()
mw = arm.matrix_world
rs = (mw @ PB["tag_flash"].head)
for nm, p in (("rear", T @ rear), ("front", T @ front)):
    ang_r = math.degrees(math.atan2(-p.y, p.x)); ang_u = math.degrees(math.atan2(p.z, p.x))
    print(f"ADS {nm} sight: {p.x:.3f} m ahead, {ang_r:+.3f} deg right, {ang_u:+.3f} deg up "
          f"(= {ang_r / math.degrees(1.1011) * 1080:+.1f} px, {ang_u / math.degrees(1.1011) * 1080:+.1f} px at 1080p)")
bake_pose("@ads", 1, 2)
print("MADE @ads")

# ---- @ads_empty: the ADS pose with the slide locked back (rest pose while aiming with an empty mag)
for n, (l, r) in locked.items():
    PB[n].location, PB[n].rotation_quaternion = l, r
bake_pose("@ads_empty", 1, 2)
print("MADE @ads_empty")
bpy.ops.wm.save_as_mainfile(filepath=OUT)

# ---- previews from the eye with KCD2's FOV (1.1011 rad vertical, 16:9)
for o in scene.objects:
    if o.type == "MESH":
        m = bpy.data.materials.new("c"); m.diffuse_color = (0.25, 0.3, 0.22, 1) if "western" in o.name else (0.12, 0.12, 0.13, 1) if "oakley" in o.name else (0.32, 0.32, 0.35, 1)
        o.data.materials.clear(); o.data.materials.append(m)
scene.render.engine = "BLENDER_WORKBENCH"; scene.display.shading.color_type = "MATERIAL"; scene.display.shading.light = "STUDIO"
scene.render.resolution_x, scene.render.resolution_y = 960, 540
world = bpy.data.worlds.new("w"); world.color = (0.62, 0.7, 0.78); scene.world = world
cam = bpy.data.objects.new("eye", bpy.data.cameras.new("eye")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.sensor_fit = "VERTICAL"; cam.data.angle = 1.1011; cam.data.clip_start = 0.01
cam.matrix_world = Matrix(((0, 0, -1, 0), (-1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1))).transposed().inverted().transposed() if False else Matrix.Identity(4)
cam.rotation_euler = Quaternion(Vector((0, 0, 1)), -math.pi / 2).to_euler()   # camera -Z -> +X
cam.rotation_euler = (Matrix.Rotation(-math.pi / 2, 4, "Z") @ Matrix.Rotation(math.pi / 2, 4, "X")).to_euler()
# crosshair
ch = bpy.data.objects.new("ch", None); ch.empty_display_type = "SPHERE"; ch.empty_display_size = 0.001
for act, f, label in (("@idle1", 1, "hip"), ("@ads", 1, "ads"), ("@idle_empty", 1, "idle_empty"), ("@holster", 18, "holster_mid"), ("@ads_empty", 1, "ads_empty")):
    pose_at(A[act], f)
    scene.render.filepath = os.path.join(RENDER, f"vm {label}.png")
    bpy.ops.render.render(write_still=True)
print("EXTRA DONE")
