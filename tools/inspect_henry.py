# Blender: import Henry's body glb, print the armature bone hierarchy (arm chain) and render a preview.
import bpy, sys, math, mathutils
argv = sys.argv[sys.argv.index("--") + 1:]
files, outdir = argv[:-1], argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
for f in files:
    bpy.ops.import_scene.gltf(filepath=f)
arms = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
print("ARMATURES", [(a.name, len(a.data.bones)) for a in arms])
arm = arms[0]
def chain(b, depth=0):
    if depth < 40 and any(k in b.name.lower() for k in ("hip", "spine", "neck", "head", "arm", "shoulder", "clav", "hand", "root", "pelvis", "bip", "index", "thumb")):
        print("BONE", "  " * min(depth, 12) + b.name, tuple(round(v, 3) for v in (arm.matrix_world @ b.head_local)))
    for c in b.children:
        chain(c, depth + 1)
for b in arm.data.bones:
    if b.parent is None:
        chain(b)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
pts = [o.matrix_world @ mathutils.Vector(c) for o in meshes for c in o.bound_box]
print("MESH BBOX", tuple(round(min(p[i] for p in pts), 3) for i in range(3)), tuple(round(max(p[i] for p in pts), 3) for i in range(3)))
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 600, 800
world = bpy.data.worlds.new("w"); world.color = (0.3, 0.3, 0.32); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
sun.rotation_euler = (math.radians(40), 0, math.radians(30)); scene.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
cam.location = (2.2, -3.2, 1.3)
cam.rotation_euler = (mathutils.Vector((0, 0, 1.0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scene.render.filepath = f"{outdir}/henry_bindpose.png"
bpy.ops.render.render(write_still=True)
