# Blender: one slider per real hand motion (degrees), on the Henry object (Object properties > Custom Properties).
# Finger joint 1: curl (local Z) + splay (local Y); joints 2+3: one "curl tip" slider (joint 3 = 0.75 x joint 2).
# Thumb base (Thumb1): swing toward the pinky (Z), lift off the palm (Y), twist (X); thumb joints 2+3: one curl slider.
# Axes are the exact joint frames (twist = toward the child, curl = captured hinge axis). Every slider's sign is
# self-tested so "+" moves the fingertip the way its name says. Starts from the current pose.
# usage: blender -b <kit.blend (gun sliders on Henry, no IK)> -P kit_fk_sliders.py -- <out.blend>
import bpy, sys, math
from mathutils import Vector
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
PB = arm.pose.bones; H = "RightHand"; D2R = math.pi / 180
bpy.context.view_layer.update()
def jw(n): return arm.matrix_world @ PB[n].head

def add_slider(name, val, lo, hi, desc):
    arm[name] = round(float(val), 1)
    arm.id_properties_ui(name).update(min=lo, max=hi, soft_min=lo, soft_max=hi, description=desc, step=100, precision=1)
def drive(pb, comp, name, k):
    fc = pb.driver_add("rotation_euler", comp)
    d = fc.driver; d.type = "SCRIPTED"
    v = d.variables.new(); v.name = "v"; v.type = "SINGLE_PROP"
    v.targets[0].id_type = "OBJECT"; v.targets[0].id = arm; v.targets[0].data_path = f'["{name}"]'
    d.expression = f"v*{k:.7f}"
    return d

def euler_of(n):
    q = PB[n].matrix_basis.to_quaternion()
    return q.to_euler("XYZ")

CONTROLS = []   # (slider, [(bone, comp, factor)], test-direction function or None)
thumb_side = (jw(H + "Index1") - jw(H + "Pinky1")).normalized()
fwd = (jw(H + "Middle1") - jw(H)).normalized()
palm_n = jw(H + "Middle3") - (jw(H) + jw(H + "Middle1")) / 2
palm_n = (palm_n - fwd * palm_n.dot(fwd)); palm_n = (palm_n - thumb_side * palm_n.dot(thumb_side)).normalized()

order = [("Index", 1), ("Middle", 2), ("Ring", 3), ("Pinky", 4)]
for f, k in order:
    n1, n2, n3 = (f"{H}{f}{i}" for i in (1, 2, 3))
    e1, e2 = euler_of(n1), euler_of(n2)
    CONTROLS.append((f"{k}1 {f} curl base", -math.degrees(e1.z), -40, 110, [(n1, 2, -1.0)], "curl"))
    CONTROLS.append((f"{k}2 {f} curl tip", -math.degrees(e2.z), -15, 120, [(n2, 2, -1.0), (n3, 2, -0.75)], "curl"))
    CONTROLS.append((f"{k}3 {f} splay (+ toward thumb)", math.degrees(e1.y), -35, 35, [(n1, 1, 1.0)], ("dir", f, thumb_side)))
t1, t2, t3 = (f"{H}Thumb{i}" for i in (1, 2, 3))
e1, e2 = euler_of(t1), euler_of(t2)
CONTROLS.append(("51 Thumb base swing toward pinky", math.degrees(e1.z), -90, 90, [(t1, 2, 1.0)], ("toward", "Thumb", H + "Pinky2")))
CONTROLS.append(("52 Thumb base spread (opens the U)", math.degrees(e1.y), -90, 90, [(t1, 1, 1.0)], ("away", "Thumb", H + "Index1")))
CONTROLS.append(("53 Thumb base twist", math.degrees(e1.x), -90, 90, [(t1, 0, 1.0)], None))
CONTROLS.append(("54 Thumb curl", -math.degrees(e2.z), -30, 100, [(t2, 2, -1.0), (t3, 2, -0.8)], "curl"))

for n in {b for c in CONTROLS for b, _, _ in c[4]}:
    PB[n].rotation_mode = "XYZ"
    PB[n].rotation_euler = (0, 0, 0)
drivers = {}
for name, val, lo, hi, targets, test in CONTROLS:
    add_slider(name, val, lo, hi, name)
    drivers[name] = [(drive(PB[b], comp, name, f * D2R), f) for b, comp, f in targets]
bpy.context.view_layer.update()

# self-test signs: +8 deg must move the finger the named way. Curl: the bend angle grows (sign comes from the
# captured fist, so this only verifies). Directional sliders flip their driver sign if the test disagrees.
def seg(f, a, b): return (jw(f"{H}{f}{b}") - jw(f"{H}{f}{a}")).normalized()
def measure(name, test):
    f = name.split()[1]
    if test == "curl":
        if "base" in name or f == "Thumb":
            ref = fwd if "base" in name else seg(f, 1, 2)
            return math.acos(max(-1, min(1, ref.dot(seg(f, 1, 2) if "base" in name else seg(f, 3, 4)))))
        return math.acos(max(-1, min(1, seg(f, 1, 2).dot(seg(f, 3, 4)))))
    kind, _, ref = test
    tip = jw(f"{H}{f}4")
    if kind == "dir":
        return tip.dot(ref)
    d = (jw(ref) - tip).length
    return -d if kind == "toward" else d
def bump(name, dv):
    arm[name] = arm[name] + dv; arm.update_tag(); bpy.context.view_layer.update()
flipped, bad = [], []
for name, val, lo, hi, targets, test in CONTROLS:
    if test is None:
        continue
    a = measure(name, test); bump(name, 8); b = measure(name, test); bump(name, -8)
    if b > a:
        continue
    if test == "curl":
        bad.append(name); continue
    for d, f in drivers[name]:
        d.expression = f"v*{-f * D2R:.7f}"
    arm[name] = -arm[name]; arm.update_tag(); bpy.context.view_layer.update(); flipped.append(name)
    a = measure(name, test); bump(name, 8); b = measure(name, test); bump(name, -8)
    if b <= a:
        bad.append(name)
print("SIGN FLIPPED", flipped)
print("SIGN CHECK bad", bad)
for b in arm.data.bones:
    b.hide = True
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("FK SLIDERS SAVED", OUT, len(CONTROLS), "finger sliders;", {c[0]: arm[c[0]] for c in CONTROLS})
