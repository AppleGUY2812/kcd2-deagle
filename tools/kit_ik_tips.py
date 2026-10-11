# Blender: add fingertip handles (IK) to the posing kit. One sphere bone per finger at its tip joint, parented to
# RightHand; an IK constraint on <finger>4 (shortened to 1 mm, locked; its tail = the tip joint) pulls joints
# 1-3 toward it. Joints 2/3 are hinges (only their curl axis free, limited to natural range); joint 1 has no twist,
# limited curl and splay. Hinge/twist axes come from the captured sword pose and the bind child offsets.
# usage: blender -b <kit.blend> -P kit_ik_tips.py -- <out.blend>
import bpy, sys, math
from mathutils import Vector, Matrix
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
H = "RightHand"
F = ["Thumb", "Index", "Middle", "Ring", "Pinky"]
bpy.context.view_layer.objects.active = arm
if bpy.context.mode != "POSE":
    bpy.ops.object.mode_set(mode="POSE")
bpy.context.view_layer.update()
PB, BN = arm.pose.bones, arm.data.bones

def dominant(v):
    i = max(range(3), key=lambda k: abs(v[k])); return i, (1 if v[i] > 0 else -1)

# axes per joint (bone-local, Blender): twist = toward the child; curl = hinge axis of joint 2 of that finger
axes = {}
for f in F:
    n = [f"{H}{f}{i}" for i in (1, 2, 3, 4)]
    twist = [dominant((BN[n[k]].matrix_local.inverted() @ BN[n[k + 1]].head_local))[0] for k in range(3)]
    q = PB[n[1]].matrix_basis.to_quaternion()
    if q.w < 0:
        q.negate()
    ax, ang = q.to_axis_angle()
    curl_i, curl_s = dominant(ax) if ang > math.radians(5) else (2, 1)
    axes[f] = (twist, curl_i, curl_s)
    print("AXES", f, "twist", twist, "curl", curl_i, curl_s, "j2 angle", round(math.degrees(ang)))

# tip positions in RightHand space at the current pose
tips = {f: (arm.matrix_world @ PB[H].matrix).inverted() @ (arm.matrix_world @ PB[f"{H}{f}4"].head) for f in F}
bpy.ops.object.mode_set(mode="EDIT")
eb = arm.data.edit_bones
hand_rest = BN[H].matrix_local
for f in F:   # tip bones: 1 mm long (same direction/roll -> same joint frame) so their tail is the fingertip joint
    t4 = eb[f"{H}{f}4"]; t4.tail = t4.head + (t4.tail - t4.head).normalized() * 0.001
for f in F:
    b = eb.new(f"TIP {f}")
    b.head = hand_rest @ tips[f]
    b.tail = b.head + (hand_rest.to_3x3() @ Vector((0, 0, 0.006)))
    b.parent = eb[H]; b.use_deform = False
bpy.ops.object.mode_set(mode="POSE")

shape = bpy.data.objects.get("TipShape") or bpy.data.objects.new("TipShape", None)
shape.empty_display_type = "SPHERE"; shape.empty_display_size = 1.0
LIM = {"j1_curl": (-25, 100), "j1_splay": (-30, 30), "hinge": (-5, 115)}
for f in F:
    twist, ci, cs = axes[f]
    t = PB[f"TIP {f}"]
    t.custom_shape = shape; t.custom_shape_scale_xyz = (0.25, 0.25, 0.25); t.use_custom_shape_bone_size = True
    c = PB[f"{H}{f}4"].constraints.new("IK")
    c.target = arm; c.subtarget = f"TIP {f}"; c.use_tail = True; c.chain_count = 4; c.use_stretch = False
    p4 = PB[f"{H}{f}4"]
    p4.lock_ik_x = p4.lock_ik_y = p4.lock_ik_z = True
    for k, j in enumerate((1, 2, 3)):
        pb = PB[f"{H}{f}{j}"]
        names = "xyz"
        for a in range(3):
            setattr(pb, f"lock_ik_{names[a]}", False); setattr(pb, f"use_ik_limit_{names[a]}", False)
        tw = twist[k]
        setattr(pb, f"lock_ik_{names[tw]}", True)
        lo, hi = LIM["j1_curl"] if j == 1 else LIM["hinge"]
        if cs < 0:
            lo, hi = -hi, -lo
        setattr(pb, f"use_ik_limit_{names[ci]}", True)
        setattr(pb, f"ik_min_{names[ci]}", math.radians(lo)); setattr(pb, f"ik_max_{names[ci]}", math.radians(hi))
        other = ({0, 1, 2} - {tw, ci}).pop() if ci != tw else None
        if other is not None:
            if j == 1:
                setattr(pb, f"use_ik_limit_{names[other]}", True)
                setattr(pb, f"ik_min_{names[other]}", math.radians(LIM["j1_splay"][0])); setattr(pb, f"ik_max_{names[other]}", math.radians(LIM["j1_splay"][1]))
            else:
                setattr(pb, f"lock_ik_{names[other]}", True)
# fingertips stick to the gun: a joined copy of the gun (follows the gun sliders) is the Shrinkwrap surface; each
# handle sits a skin's thickness off it, so a dragged fingertip slides along the metal instead of sinking in
import bmesh
adj = bpy.data.objects["GunAdjust"]
pieces = [o for o in scene.objects if o.type == "MESH" and o.parent is adj]
bm = bmesh.new()
for o in pieces:
    bm.from_mesh(o.data)
me = bpy.data.meshes.new("GunShell"); bm.to_mesh(me); bm.free()
shell = bpy.data.objects.new("GunShell", me); scene.collection.objects.link(shell)
shell.parent = adj; shell.hide_select = True; shell.hide_render = True; shell.display_type = "WIRE"
shell.hide_set(True)
for f in F:
    sw = PB[f"TIP {f}"].constraints.new("SHRINKWRAP")
    sw.name = "Stick to gun"; sw.target = shell; sw.shrinkwrap_type = "NEAREST_SURFACE"
    sw.wrap_mode = "OUTSIDE_SURFACE"; sw.distance = 0.007
# only the tip handles are shown (finger joints stay clickable via Alt+H if wanted)
for b in BN:
    b.hide = not b.name.startswith("TIP ")
    b.select = False
scene.transform_orientation_slots[0].type = "GLOBAL"
scene.tool_settings.transform_pivot_point = "MEDIAN_POINT"
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("IK TIPS SAVED", OUT)
