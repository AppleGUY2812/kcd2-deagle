# Blender: build a test i_caf on Henry's skeleton (right arm raised straight forward), export via the KCD2 toolkit.
# usage: blender -b --factory-startup -P anim_test.py -- <male_body.skin> <out .dae path> <rc.exe> [preview_dir]
import bpy, sys, math, mathutils, addon_utils

argv = sys.argv[sys.argv.index("--") + 1:]
SKIN, OUT_DAE, RC = argv[0], argv[1], argv[2]
PREVIEW = argv[3] if len(argv) > 3 else None

bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC

bpy.ops.import_scene.kcd2_skin(filepath=SKIN)
arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
print("ARMATURE", arm.name, len(arm.data.bones), "bones; RightArm" , "RightArm" in arm.data.bones)
for name in ("RightArm", "RightForeArm", "RightHand", "Hips"):
    b = arm.data.bones[name]
    print("REST", name, "head", tuple(round(v, 3) for v in b.head_local), "y-axis", tuple(round(v, 3) for v in b.matrix_local.to_3x3().col[1]))

bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="POSE")
scene = bpy.context.scene
scene.frame_start, scene.frame_end = 0, 30
scene.render.fps = 30

def aim_bone(pb, world_dir):
    """Rotate a pose bone so its Y axis points along world_dir (armature space), keeping it in pose space."""
    rest = pb.bone.matrix_local.to_3x3()
    cur_y = rest.col[1].normalized()
    q = cur_y.rotation_difference(mathutils.Vector(world_dir).normalized())
    parent_rest = pb.parent.bone.matrix_local.to_3x3() if pb.parent else mathutils.Matrix.Identity(3)
    pb.rotation_mode = "QUATERNION"
    pb.rotation_quaternion = (rest.inverted() @ q.to_matrix() @ rest).to_quaternion()

fwd = mathutils.Vector((0, -1, 0))  # Henry faces -Y in this rig
for frame in (0, 30):
    scene.frame_set(frame)
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.keyframe_insert("rotation_quaternion", frame=frame)
    aim_bone(arm.pose.bones["RightArm"], fwd + mathutils.Vector((0, 0, 0.05)))
    arm.pose.bones["RightArm"].keyframe_insert("rotation_quaternion", frame=frame)
bpy.ops.object.mode_set(mode="OBJECT")

if PREVIEW:
    scene.render.engine = "BLENDER_EEVEE_NEXT"
    scene.render.resolution_x, scene.render.resolution_y = 600, 800
    world = bpy.data.worlds.new("w"); world.color = (0.3, 0.3, 0.32); scene.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
    sun.rotation_euler = (math.radians(40), 0, math.radians(30)); scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    cam.location = (2.2, -3.2, 1.3)
    cam.rotation_euler = (mathutils.Vector((0, 0, 1.0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.frame_set(15)
    scene.render.filepath = f"{PREVIEW}/anim_test_pose.png"
    bpy.ops.render.render(write_still=True)

bpy.ops.object.select_all(action="DESELECT")
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name="deagle_test", mode="Manual",
                                    range_type="Values", node_start=0, node_end=30)
bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=OUT_DAE)
print("ANIM EXPORT DONE")
