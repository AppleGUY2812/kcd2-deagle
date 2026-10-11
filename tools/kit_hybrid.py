# Blender: hybrid posing kit. Fingertip spheres (IK + stick-to-gun) drive the two outer joints of every finger and the
# thumb; the knuckle joint (curl + splay) and the thumb base (swing toward pinky, spread, twist) are sliders on Henry
# (Blender's IK overwrites anything set by hand on bones inside its chain, so those joints stay outside it).
# Starts from the file's evaluated pose; reports how far any joint moved.
# usage: blender -b <any kit .blend> -P kit_hybrid.py -- <out.blend>
import bpy, bmesh, sys, math
from mathutils import Vector
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB, BN = arm.pose.bones, arm.data.bones
H = "RightHand"; D2R = math.pi / 180
F = ["Thumb", "Index", "Middle", "Ring", "Pinky"]
FB = [f"{H}{f}{i}" for f in F for i in (1, 2, 3, 4)]
bpy.context.view_layer.objects.active = arm
if bpy.context.mode != "POSE":
    bpy.ops.object.mode_set(mode="POSE")
bpy.context.view_layer.update()
def jw(n): return arm.matrix_world @ PB[n].head

# ---- bake the evaluated pose, clear old controls
def eff_basis(pb):
    rr = pb.parent.bone.matrix_local.inverted() @ pb.bone.matrix_local
    return (rr.inverted() @ pb.parent.matrix.inverted() @ pb.matrix).to_quaternion()
baked = {n: eff_basis(PB[n]) for n in FB}
before = {n: jw(n).copy() for n in FB}
tips_hand = {f: (arm.matrix_world @ PB[H].matrix).inverted() @ jw(f"{H}{f}4") for f in F}
for pb in PB:
    if pb.name in FB or pb.name.startswith("TIP "):
        for c in list(pb.constraints):
            pb.constraints.remove(c)
if arm.animation_data:
    for fc in [fc for fc in arm.animation_data.drivers if any(f'"{n}"' in fc.data_path for n in FB)]:
        arm.animation_data.drivers.remove(fc)
for k in [k for k in arm.keys() if k[:1].isdigit() and not k.startswith("0")]:
    del arm[k]
for n, q in baked.items():
    PB[n].rotation_mode = "QUATERNION"; PB[n].rotation_quaternion = q

# ---- stick-to-gun surface (follows the gun sliders)
adj = bpy.data.objects["GunAdjust"]
shell = bpy.data.objects.get("GunShell")
if shell is None:
    bm = bmesh.new()
    for o in [o for o in scene.objects if o.type == "MESH" and o.parent is adj]:
        bm.from_mesh(o.data)
    me = bpy.data.meshes.new("GunShell"); bm.to_mesh(me); bm.free()
    shell = bpy.data.objects.new("GunShell", me); scene.collection.objects.link(shell)
    shell.parent = adj; shell.hide_select = True; shell.hide_render = True; shell.display_type = "WIRE"
shell.hide_set(True)

# ---- tip bones at the current fingertips; fingertip joint bones 1 mm long (same frame) so their tail is the tip
bpy.ops.object.mode_set(mode="EDIT")
eb = arm.data.edit_bones; hand_rest = BN[H].matrix_local
for f in F:
    t4 = eb[f"{H}{f}4"]; t4.tail = t4.head + (t4.tail - t4.head).normalized() * 0.001
    b = eb.get(f"TIP {f}") or eb.new(f"TIP {f}")
    b.head = hand_rest @ tips_hand[f]; b.tail = b.head + hand_rest.to_3x3() @ Vector((0, 0, 0.006))
    b.parent = eb[H]; b.use_deform = False
bpy.ops.object.mode_set(mode="POSE")
shape = bpy.data.objects.get("TipShape") or bpy.data.objects.new("TipShape", None)
shape.empty_display_type = "SPHERE"; shape.empty_display_size = 1.0
for f in F:
    t = PB[f"TIP {f}"]
    t.location = (0, 0, 0); t.rotation_mode = "QUATERNION"; t.rotation_quaternion = (1, 0, 0, 0)
    t.custom_shape = shape; t.custom_shape_scale_xyz = (0.25, 0.25, 0.25)
    sw = t.constraints.new("SHRINKWRAP"); sw.name = "Stick to gun"; sw.target = shell
    sw.shrinkwrap_type = "NEAREST_SURFACE"; sw.wrap_mode = "OUTSIDE_SURFACE"; sw.distance = 0.007
    p4 = PB[f"{H}{f}4"]; p4.lock_ik_x = p4.lock_ik_y = p4.lock_ik_z = True
    ik = p4.constraints.new("IK"); ik.target = arm; ik.subtarget = f"TIP {f}"
    ik.use_tail = True; ik.chain_count = 3; ik.use_stretch = False        # bones 4 (locked), 3, 2
    for j in (2, 3):   # hinge: curl (local Z) only, natural range
        pb = PB[f"{H}{f}{j}"]
        pb.lock_ik_x = pb.lock_ik_y = True; pb.lock_ik_z = False
        pb.use_ik_limit_z = True; pb.ik_min_z = -115 * D2R; pb.ik_max_z = 5 * D2R

# ---- sliders for the knuckles and the thumb base (exact start values, sign self-tested)
def add_slider(name, val, lo, hi):
    arm[name] = round(float(val), 1)
    arm.id_properties_ui(name).update(min=lo, max=hi, soft_min=lo, soft_max=hi, description=name, step=100, precision=1)
def drive(pb, comp, name, k):
    fc = pb.driver_add("rotation_euler", comp); d = fc.driver; d.type = "SCRIPTED"
    v = d.variables.new(); v.name = "v"; v.type = "SINGLE_PROP"
    v.targets[0].id_type = "OBJECT"; v.targets[0].id = arm; v.targets[0].data_path = f'["{name}"]'
    d.expression = f"v*{k:.7f}"; return d
thumb_side = (jw(H + "Index1") - jw(H + "Pinky1")).normalized()
fwd = (jw(H + "Middle1") - jw(H)).normalized()
CTL = []
for f, k in (("Index", 1), ("Middle", 2), ("Ring", 3), ("Pinky", 4)):
    CTL += [(f"{k}1 {f} knuckle curl", f"{H}{f}1", 2, -1.0, ("angle", f)), (f"{k}2 {f} splay (+ toward thumb)", f"{H}{f}1", 1, 1.0, ("dir", f, thumb_side))]
CTL += [("51 Thumb base swing toward pinky", f"{H}Thumb1", 2, 1.0, ("toward", "Thumb", H + "Pinky2")),
        ("52 Thumb base spread (opens the U)", f"{H}Thumb1", 1, 1.0, ("away", "Thumb", H + "Index1")),
        ("53 Thumb base twist", f"{H}Thumb1", 0, 1.0, None)]
for n in {c[1] for c in CTL}:
    e = PB[n].rotation_quaternion.to_euler("XYZ"); PB[n].rotation_mode = "XYZ"; PB[n].rotation_euler = e
drivers = {}
for name, bone, comp, sgn, test in CTL:
    add_slider(name, math.degrees(PB[bone].rotation_euler[comp]) * sgn, -100, 120 if "curl" in name else 100)
    drivers[name] = (drive(PB[bone], comp, name, sgn * D2R), sgn)
bpy.context.view_layer.update()
def seg(f, a, b): return (jw(f"{H}{f}{b}") - jw(f"{H}{f}{a}")).normalized()
def measure(test):
    kind, f = test[0], test[1]
    if kind == "angle":
        return math.acos(max(-1, min(1, fwd.dot(seg(f, 1, 2)))))
    tip = jw(f"{H}{f}4")
    if kind == "dir":
        return tip.dot(test[2])
    d = (jw(test[2]) - tip).length
    return -d if kind == "toward" else d
def bump(name, dv):
    arm[name] = arm[name] + dv; arm.update_tag(); bpy.context.view_layer.update()
flipped = []
for name, bone, comp, sgn, test in CTL:
    if test is None:
        continue
    a = measure(test); bump(name, 6); b = measure(test); bump(name, -6)
    if b < a:
        d, s = drivers[name]; arm[name] = -arm[name]; d.expression = f"v*{-s * D2R:.7f}"; flipped.append(name)
        arm.update_tag(); bpy.context.view_layer.update()
print("SIGN FLIPPED", flipped)

# per-finger switch: sphere on (IK) / off (rotate joint pins 2-3 by hand; they hold the pose from build time)
for k, f in enumerate(F):
    name = f"6{k + 1} {f} sphere (1 on, 0 = rotate joints by hand)"
    arm[name] = 1.0
    arm.id_properties_ui(name).update(min=0.0, max=1.0, soft_min=0.0, soft_max=1.0, description=name, step=100, precision=0)
    fc = PB[f"{H}{f}4"].constraints["IK"].driver_add("influence"); d = fc.driver; d.type = "SCRIPTED"
    v = d.variables.new(); v.name = "v"; v.type = "SINGLE_PROP"
    v.targets[0].id_type = "OBJECT"; v.targets[0].id = arm; v.targets[0].data_path = f'["{name}"]'; d.expression = "v"
SHOW = {f"TIP {f}" for f in F} | {f"{H}{f}{j}" for f in F for j in (2, 3)}
bpy.ops.object.mode_set(mode="EDIT")
for f in F:
    for j in (2, 3):   # short pins, same direction/roll -> same joint frame
        e = arm.data.edit_bones[f"{H}{f}{j}"]; e.tail = e.head + (e.tail - e.head).normalized() * 0.008
bpy.ops.object.mode_set(mode="POSE")
arm.show_in_front = True; arm.data.display_type = "OCTAHEDRAL"
for b in BN:
    b.hide = b.name not in SHOW
    b.select = False
scene.transform_orientation_slots[0].type = "LOCAL"; scene.tool_settings.transform_pivot_point = "INDIVIDUAL_ORIGINS"
bpy.context.view_layer.update()
shift = {n: round((jw(n) - before[n]).length * 1000, 1) for n in FB}
print("POSE SHIFT max mm", max(shift.values()), "worst", sorted(shift.items(), key=lambda t: -t[1])[:4])
print("TIP REACH mm", {f: round((jw(f"{H}{f}4") - jw(f"TIP {f}")).length * 1000, 1) for f in F})
bpy.ops.wm.save_as_mainfile(filepath=OUT)
# switch test: index sphere off -> joints hold the build-time pose and the pins rotate by hand
arm["62 Index sphere (1 on, 0 = rotate joints by hand)"] = 0.0; arm.update_tag(); bpy.context.view_layer.update()
a = jw(f"{H}Index4").copy(); PB[f"{H}Index2"].rotation_quaternion = PB[f"{H}Index2"].rotation_quaternion @ Vector((0, 0, 1)).rotation_difference(Vector((0, 0.1, 1)).normalized()) if False else PB[f"{H}Index2"].rotation_quaternion
from mathutils import Quaternion
q0 = PB[f"{H}Index2"].rotation_quaternion.copy()
PB[f"{H}Index2"].rotation_quaternion = q0 @ Quaternion(Vector((0, 0, 1)), -0.3); bpy.context.view_layer.update()
print("SWITCH TEST index tip moved by hand-rotation mm", round((jw(f"{H}Index4") - a).length * 1000, 1))
PB[f"{H}Index2"].rotation_quaternion = q0
arm["62 Index sphere (1 on, 0 = rotate joints by hand)"] = 1.0; arm.update_tag(); bpy.context.view_layer.update()
print("HYBRID SAVED", OUT, {k: arm[k] for k in arm.keys() if k[:1].isdigit()})
