# Blender: contact-sheet frames of a CONVERTED gun (<slug>_vm_roles.blend: clips by role after every fix and generated
# clip), from the eye like tools/gb/gb_preview.py. Frames -> <gun dir>/roles_preview/<role>_<n>.png (gb_sheet.py).
# usage: blender -b <gun dir>/<slug>_vm_roles.blend -P gb_roles_preview.py -- <gun dir>
import bpy, sys, os, math
from mathutils import Matrix
GUN = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = max([o for o in scene.objects if o.type == "ARMATURE"], key=lambda a: len(a.data.bones))
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE"
scene.render.resolution_x, scene.render.resolution_y = 480, 270
world = bpy.data.worlds.new("w"); world.color = (0.62, 0.7, 0.78); scene.world = world
cam = bpy.data.objects.new("eye", bpy.data.cameras.new("eye")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.sensor_fit = "VERTICAL"; cam.data.angle = 1.1011; cam.data.clip_start = 0.005
cam.rotation_euler = (Matrix.Rotation(-math.pi / 2, 4, "Z") @ Matrix.Rotation(math.pi / 2, 4, "X")).to_euler()
out = os.path.join(GUN, "roles_preview")
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    os.remove(os.path.join(out, f))
ORDER = ["idle", "idle_empty", "shoot", "shoot_empty", "reload", "reload_empty", "inspect", "inspect_empty", "draw", "draw_empty", "holster"]
for role in ORDER:
    act = bpy.data.actions.get("@" + role)
    if not act:
        continue
    arm.animation_data.action = act
    f0, f1 = (int(v) for v in act.frame_range)
    for i, k in enumerate((0.0, 0.33, 0.66, 1.0)):
        scene.frame_set(int(round(f0 + (f1 - f0) * k)))
        scene.render.filepath = os.path.join(out, f"{role}_{i}.png")
        bpy.ops.render.render(write_still=True)
    print("RENDERED", role)
print("ROLES PREVIEW DONE")
