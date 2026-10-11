# Blender: turn the slider posing kit into a direct-posing file, keeping the user's current pose.
# Finger drivers are baked into plain rotations and removed; right-hand finger bones are shown (short, in front),
# everything else hidden; Pose mode + Local orientation so the Rotate tool's rings curl/splay a finger joint.
# Gun sliders stay. usage: blender -b <kit.blend> -P kit_free_pose.py -- <out.blend>
import bpy, sys
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
bpy.context.view_layer.update()
H = "RightHand"
FINGER = [f"{H}{f}{i}" for f in ("Thumb", "Index", "Middle", "Ring", "Pinky") for i in (1, 2, 3)]
baked = 0
for n in FINGER:
    pb = arm.pose.bones[n]
    q = pb.matrix_basis.to_quaternion()   # evaluated (drivers applied)
    if arm.animation_data:
        for fc in [fc for fc in arm.animation_data.drivers if fc.data_path.startswith(f'pose.bones["{n}"]')]:
            arm.animation_data.drivers.remove(fc)
    pb.rotation_mode = "QUATERNION"; pb.rotation_quaternion = q
    baked += 1
ctl = bpy.data.objects["CONTROLS"]
for k in [k for k in ctl.keys() if not k.startswith("0")]:
    del ctl[k]
# armature visible, only the right-hand finger joints, short bones drawn in front of the skin
arm.hide_set(False); arm.hide_select = False
arm.show_in_front = True
arm.data.display_type = "OCTAHEDRAL"
bpy.context.view_layer.objects.active = arm
for o in scene.objects:
    o.select_set(o is arm)
bpy.ops.object.mode_set(mode="EDIT")
for eb in arm.data.edit_bones:
    if eb.name in FINGER:
        eb.tail = eb.head + (eb.tail - eb.head).normalized() * 0.008   # same direction/roll -> same joint frame
bpy.ops.object.mode_set(mode="POSE")
for b in arm.data.bones:
    b.hide = b.name not in FINGER
    b.select = False
for c in arm.data.collections_all:
    c.is_visible = True
scene.transform_orientation_slots[0].type = "LOCAL"
scene.tool_settings.transform_pivot_point = "INDIVIDUAL_ORIGINS"
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("FREE POSE SAVED", OUT, "baked", baked)
