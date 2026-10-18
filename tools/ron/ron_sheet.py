# Blender: contact sheet of an imported RoN viewmodel (ron_import.py's blend): chosen actions and frames, each from the
# first-person camera (at fp_camera, looking +X) and from the right side, solid shading, one PNG per shot; then
# tools/gb/gb_sheet.py-style tiling is done by the caller (PIL).
#   blender -b <vm.blend> -P ron_sheet.py -- <out dir> <action:frame> ...
import bpy, sys, os, math
from mathutils import Vector, Euler

args = sys.argv[sys.argv.index("--") + 1:]
out, shots = args[0], args[1:]
os.makedirs(out, exist_ok=True)
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"; scene.display.shading.color_type = "OBJECT"
scene.render.resolution_x, scene.render.resolution_y = 640, 400
cols = {"ron_arms": (0.35, 0.37, 0.4, 1), "ron_gun": (0.75, 0.55, 0.2, 1)}
for o in scene.objects:
    if o.type == "MESH": o.color = cols.get(o.name, (0.2, 0.6, 0.9, 1))
cam_data = bpy.data.cameras.new("c"); cam = bpy.data.objects.new("c", cam_data); scene.collection.objects.link(cam)
scene.camera = cam
for s in shots:
    act, f = s.split(":"); f = int(f)
    arm.animation_data.action = bpy.data.actions[act]
    scene.frame_set(f); bpy.context.view_layer.update()
    eye = arm.matrix_world @ arm.pose.bones["fp_camera"].head
    for view, (loc, rot, lens) in {
        "fp": (eye, Euler((math.radians(90), 0, math.radians(-90))), 22),                          # look +X
        "side": (eye + Vector((0.25, -0.75, -0.05)), Euler((math.radians(90), 0, 0)), 35),         # from the right, look +Y
    }.items():
        cam.location = loc; cam.rotation_euler = rot; cam_data.lens = lens; cam_data.clip_start = 0.01
        scene.render.filepath = os.path.join(out, f"{act.strip('@')}_{f:03d}_{view}.png")
        bpy.ops.render.render(write_still=True)
print("SHEET DONE", len(shots))
