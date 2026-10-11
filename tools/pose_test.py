# Blender: a deliberately awkward arm/finger pose on the exact rig, exported as i_caf, plus the joint-to-joint
# distances it should produce in game (rigid within the chest frame, so comparable regardless of body posture).
import bpy, sys, os, math, addon_utils
from mathutils import Euler
argv = sys.argv[sys.argv.index("--") + 1:]
BODY_DIR, OUT_DAE, RC = argv[0], argv[1], argv[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import henry_rig
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
arm = henry_rig.import_henry([os.path.join(BODY_DIR, f) for f in ("male_body.glb", "male_body_arms.glb", "male_body_hands.glb")])
scene = bpy.context.scene
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="POSE")
POSE = {"RightArm": (40, -20, 30), "RightForeArm": (0, 0, 70), "RightHand": (25, 40, -15),
        "RightHandIndex1": (0, 0, 60), "RightHandIndex2": (0, 0, 50), "RightHandThumb1": (30, 10, 0),
        "RightHandMiddle1": (0, 0, 80), "LeftArm": (-30, 10, -20), "LeftForeArm": (0, 0, -40)}
for f in (0, 10):
    scene.frame_set(f)
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = Euler([math.radians(a) for a in POSE[pb.name]]).to_quaternion() if pb.name in POSE else (1, 0, 0, 0)
        pb.keyframe_insert("rotation_quaternion", frame=f)
    import keep_channels
    keep_channels.nudge_identity(arm.pose.bones, [b.name for b in arm.pose.bones], f)
bpy.context.view_layer.update()
P = ["RightArm", "RightHand", "RightHandIndex4", "RightHandMiddle4", "RightHandThumb4", "LeftHand", "LeftArm"]
pos = {n: arm.matrix_world @ arm.pose.bones[n].head for n in P}
pairs = [("RightArm", "RightHand"), ("RightArm", "RightHandIndex4"), ("RightHandIndex4", "RightHandThumb4"),
         ("RightHandMiddle4", "RightHandThumb4"), ("RightHand", "LeftHand"), ("RightHandIndex4", "LeftArm"), ("RightHandThumb4", "LeftHand")]
print("EXPECT " + " ".join(f"{a}-{b}={(pos[a] - pos[b]).length:.4f}" for a, b in pairs))
bpy.ops.object.mode_set(mode="OBJECT")
scene.frame_start, scene.frame_end = 0, 10
for o in [o for o in scene.objects if o.type == "MESH"]:
    bpy.data.objects.remove(o)
bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
bpy.ops.bcry.add_cry_animation_node("EXEC_DEFAULT", node_type="i_caf", node_name="deagle_posetest", mode="Manual", range_type="Values", node_start=0, node_end=10)
bpy.ops.bcry.export_animations("EXEC_DEFAULT", filepath=OUT_DAE, disable_rc=True, save_dae=True)
print("POSETEST EXPORTED")
