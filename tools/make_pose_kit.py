# Blender: build a beginner-friendly posing file. Henry's hand starts in the game's captured sword fist, the Deagle in the
# best automated placement; everything is driven by sliders on a "CONTROLS" object (Object properties > Custom Properties).
# Finger joints are axis-angle rotations whose angle = captured angle * slider (simple driver expressions: no script
# auto-run needed). The gun hangs off the RightHand bone through GunBase (fixed) -> GunAdjust (slider-driven).
# usage: blender -b <sword grip_pose.blend> -P make_pose_kit.py -- <deagle gltf> <fit json> <out .blend>
import bpy, sys, os, json, math
from mathutils import Matrix, Vector, Quaternion
argv = sys.argv[sys.argv.index("--") + 1:]
GUN_GLTF, FIT_JSON, OUT = argv[:3]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB = arm.pose.bones
H = "RightHand"
fit = json.load(open(FIT_JSON))
x = fit["x"]

# ---- gun (model space as build_model.py)
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

# ---- controls
ctl = bpy.data.objects.new("CONTROLS", None); scene.collection.objects.link(ctl)
ctl.empty_display_size = 0.01
SLIDERS = []  # (name, default, min, max, description)
def slider(name, default, lo, hi, desc):
    ctl[name] = float(default)
    ctl.id_properties_ui(name).update(min=lo, max=hi, soft_min=lo, soft_max=hi, description=desc, step=1 if hi > 5 else 0.5,
                                      precision=1 if hi > 5 else 2)
    SLIDERS.append(name)
GUN_SL = [("01 Gun up-down (mm)", "location", 2, 0.001), ("02 Gun forward-back (mm)", "location", 1, 0.001),
          ("03 Gun left-right (mm)", "location", 0, 0.001), ("04 Gun tilt muzzle up-down (deg)", "rotation_euler", 0, math.pi / 180),
          ("05 Gun turn muzzle left-right (deg)", "rotation_euler", 2, math.pi / 180), ("06 Gun roll (deg)", "rotation_euler", 1, math.pi / 180)]
for name, _, _, _ in GUN_SL:
    slider(name, 0.0, -60.0, 60.0, "Moves the gun relative to the hand")
FING = [("Middle", 0), ("Ring", 1), ("Pinky", 2), ("Thumb", 3)]
FING_SL = {}
for f, k in FING:
    a = f"1{k}a {f} base curl"; b = f"1{k}b {f} tip curl"
    slider(a, x[6 + 2 * k], -0.3, 1.6, f"{f}: knuckle joint. 0 = straight, 1 = the game's sword fist")
    slider(b, x[7 + 2 * k], -0.3, 1.6, f"{f}: middle + tip joints. 0 = straight, 1 = the game's sword fist")
    FING_SL[f"{H}{f}1"] = a; FING_SL[f"{H}{f}2"] = b; FING_SL[f"{H}{f}3"] = b
for i in (1, 2, 3):
    n = f"2{i} Index joint {i} curl ({['knuckle', 'middle', 'tip'][i - 1]})"
    slider(n, x[13 + i], -0.3, 1.6, "Index (trigger) finger. Put the pad of the finger on the red trigger")
    FING_SL[f"{H}Index{i}"] = n

def add_driver(owner, path, index, prop, expr):
    fc = owner.driver_add(path, index)
    d = fc.driver; d.type = "SCRIPTED"
    v = d.variables.new(); v.name = "v"; v.type = "SINGLE_PROP"
    v.targets[0].id_type = "OBJECT"; v.targets[0].id = ctl; v.targets[0].data_path = f'["{prop}"]'
    d.expression = expr
    return fc

# ---- fingers: axis-angle = captured sword rotation scaled by the slider
for bone, prop in FING_SL.items():
    pb = PB[bone]
    q = pb.rotation_quaternion.copy()
    if q.w < 0:
        q.negate()
    ax, ang = q.to_axis_angle()
    pb.rotation_mode = "AXIS_ANGLE"
    pb.rotation_axis_angle = (ang, ax.x, ax.y, ax.z)
    add_driver(pb, "rotation_axis_angle", 0, prop, f"v*{ang:.6f}")

# ---- gun: GunBase on the RightHand bone at the fit placement, GunAdjust driven by sliders
bpy.context.view_layer.update()
hand_w = arm.matrix_world @ PB[H].matrix
G = hand_w @ Matrix(fit["gun_in_hand_blender"])
base = bpy.data.objects.new("GunBase", None); scene.collection.objects.link(base)
base.parent = arm; base.parent_type = "BONE"; base.parent_bone = H
bpy.context.view_layer.update()
base.matrix_world = G
adj = bpy.data.objects.new("GunAdjust", None); scene.collection.objects.link(adj)
adj.parent = base; adj.empty_display_size = 0.02
for name, path, idx, k in GUN_SL:
    add_driver(adj, path, idx, name, f"v*{k:.8f}")
for o in gun_objs:
    o.parent = adj; o.matrix_parent_inverse = Matrix.Identity(4); o.matrix_basis = Matrix.Identity(4)

# ---- level the world on the gun: gun up = world Z, muzzle = world +Y, grip centre at the origin
# (Blender's turntable orbit spins around world Z; a tilted gun makes navigation feel broken)
centre_w = G @ Vector((0, 0.01, -0.02))
Rfix = G.to_3x3().normalized().transposed().to_4x4()
arm.matrix_world = Matrix.Translation(-(Rfix @ centre_w)) @ Rfix @ arm.matrix_world
bpy.context.view_layer.update()
G = base.matrix_world.copy()

# ---- looks: skin, dark metal, red trigger; hide the rest of Henry and the bones
skin = bpy.data.materials.new("skin"); skin.diffuse_color = (0.86, 0.66, 0.55, 1)
metal = bpy.data.materials.new("metal"); metal.diffuse_color = (0.22, 0.23, 0.26, 1)
red = bpy.data.materials.new("trigger"); red.diffuse_color = (0.9, 0.05, 0.05, 1)
for o in arm.children:
    if o.type == "MESH":
        o.data.materials.clear(); o.data.materials.append(skin)
        o.hide_set(not o.name.startswith(("hands", "arms")))
        o.hide_select = True
for o in gun_objs:
    o.data.materials.clear(); o.data.materials.append(red if o.name.startswith("Trigger") else metal)
    o.hide_select = True
arm.hide_set(True); base.hide_set(True); adj.hide_set(True)
for o in scene.objects:
    o.select_set(False)
ctl.select_set(True); bpy.context.view_layer.objects.active = ctl

# ---- viewport: solid + material colours, framed on the grip from the gun's left side (fingertips visible)
bpy.context.view_layer.update()
centre = G @ Vector((0, 0.01, -0.02))
gx, gy, gz = [G.to_3x3().col[i].normalized() for i in range(3)]
for scr in bpy.data.screens:
    for area in scr.areas:
        if area.type == "VIEW_3D":
            sp = area.spaces[0]
            sp.shading.type = "SOLID"; sp.shading.color_type = "MATERIAL"; sp.shading.light = "STUDIO"
            sp.clip_start = 0.001; sp.overlay.show_floor = False; sp.overlay.show_axis_x = sp.overlay.show_axis_y = False
            r3 = sp.region_3d
            r3.view_location = centre; r3.view_distance = 0.35; r3.view_perspective = "PERSP"
            zc = -gx; xc = -gy; yc = zc.cross(xc)   # look at the gun's left side, gun upright
            r3.view_rotation = Matrix((xc, yc, zc)).transposed().to_quaternion()
        if area.type == "PROPERTIES":
            try:
                area.spaces[0].context = "OBJECT"
            except Exception:
                pass
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("KIT SAVED", OUT, "sliders", len(SLIDERS))
