# Blender: render first-person frames of the MWII Deagle viewmodel animations from its own camera (tag_view),
# solid colours (its textures aren't mounted yet). usage: blender -b mwii_vm.blend -P mwii_preview.py -- <out dir>
import bpy, sys, os, math
from mathutils import Matrix, Vector, Euler
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
cols = {"deagle": (0.16, 0.16, 0.18, 1), "mag": (0.2, 0.2, 0.22, 1), "mp_western": (0.25, 0.3, 0.22, 1), "oakley": (0.12, 0.12, 0.12, 1)}
for o in scene.objects:
    if o.type == "MESH":
        m = bpy.data.materials.new(o.name + "_c"); m.diffuse_color = next((c for k, c in cols.items() if o.name.startswith(k)), (0.7, 0.7, 0.7, 1))
        o.data.materials.clear(); o.data.materials.append(m)
scene.render.engine = "BLENDER_WORKBENCH"; scene.display.shading.color_type = "MATERIAL"; scene.display.shading.light = "STUDIO"
scene.render.resolution_x, scene.render.resolution_y = 640, 360
cam = bpy.data.objects.new("vmcam", bpy.data.cameras.new("vmcam")); scene.collection.objects.link(cam)
cam.data.sensor_fit = "HORIZONTAL"; cam.data.angle = math.radians(80); cam.data.clip_start = 0.005
scene.camera = cam
arm.animation_data_create()
shots = [("@draw", 18), ("@idle1", 1), ("@shoot1", 3), ("@reload", 15), ("@reload", 35), ("@reload_empty", 50), ("@inspect", 30), ("@inspect", 80), ("@inspect", 120)]
for act, f in shots:
    arm.animation_data.action = bpy.data.actions[act]
    scene.frame_set(f)
    tv = arm.matrix_world @ arm.pose.bones["tag_view"].matrix
    # Source/CoD view tag: X forward, Z up -> Blender camera looks down -Z with +Y up
    cam.matrix_world = Matrix.LocRotScale(tv.translation, (tv.to_3x3() @ Matrix(((0, 0, -1), (-1, 0, 0), (0, 1, 0)))).to_quaternion(), None)
    scene.render.filepath = os.path.join(OUT, f"mwii {act[1:]} f{f:03d}.png")
    bpy.ops.render.render(write_still=True)
print("PREVIEW DONE", bpy.context.scene.render.fps)
