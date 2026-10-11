# Blender: close-up renders of the viewmodel gun at given clip frames (side view from the gun's right), to check
# the slide / magazine / loose round. usage: blender -b <blend> -P vm_gun_closeup.py -- <out dir> <tag>
import bpy, sys, os, math
from mathutils import Matrix, Vector
OUT, TAG = sys.argv[sys.argv.index("--") + 1:][:2]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
for o in scene.objects:
    if o.type == "MESH":
        m = bpy.data.materials.new("c"); m.diffuse_color = (0.75, 0.7, 0.6, 1) if any(k in o.name for k in ("arms", "oakley", "mp_western", "skin")) else (0.25, 0.25, 0.28, 1)
        o.data.materials.clear(); o.data.materials.append(m)
scene.render.engine = "BLENDER_WORKBENCH"; scene.display.shading.color_type = "MATERIAL"; scene.display.shading.light = "STUDIO"
scene.render.resolution_x, scene.render.resolution_y = 640, 420
cam = bpy.data.objects.new("c", bpy.data.cameras.new("c")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.lens = 50; cam.data.clip_start = 0.005
if not arm.animation_data: arm.animation_data_create()
for act, f in (("@idle1", 1), ("@reload", 20), ("@reload", 40), ("@shoot1", 1), ("@draw", 37), ("@shoot1", 6)):
    arm.animation_data.action = bpy.data.actions[act]; scene.frame_set(f)
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in scene.objects:
        if o.type == "MESH" and o.name.startswith("deagle"):
            me = o.evaluated_get(dg).to_mesh()
            pts += [o.matrix_world @ v.co for v in list(me.vertices)[::7]]
            o.evaluated_get(dg).to_mesh_clear()
    mn = Vector([min(p[i] for p in pts) for i in range(3)]); mx = Vector([max(p[i] for p in pts) for i in range(3)])
    c = (mn + mx) / 2; size = (mx - mn).length
    # side view from the gun's right (Source view space: forward +X, right -Y, up +Z)
    cam.location = c + Vector((0.0, -1.0, 0.15)).normalized() * size * 1.6
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Z").to_euler()
    scene.render.filepath = os.path.join(OUT, f"{TAG} {act[1:]} f{f:02d}.png")
    bpy.ops.render.render(write_still=True)
print("CLOSEUPS DONE")
