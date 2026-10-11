# Blender: import the source glTF, report objects/dimensions, render 3 preview angles.
import bpy, sys, math, mathutils
argv = sys.argv[sys.argv.index("--") + 1:]
src, outdir = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
for o in bpy.context.scene.objects:
    mats = [s.material.name for s in o.material_slots if s.material] if o.type == "MESH" else []
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons) if o.type == "MESH" else 0
    print("OBJ", o.type, repr(o.name), "parent", o.parent.name if o.parent else None, "dims", tuple(round(d, 4) for d in o.dimensions), "tris", tris, "mats", mats)
bb = [o.matrix_world @ mathutils.Vector(c) for o in meshes for c in o.bound_box]
mn = mathutils.Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
mx = mathutils.Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
print("BBOX min", tuple(round(x, 4) for x in mn), "max", tuple(round(x, 4) for x in mx), "size", tuple(round(x, 4) for x in (mx - mn)))
center, size = (mn + mx) / 2, max(mx - mn)
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 900, 600
world = bpy.data.worlds.new("w"); world.color = (0.25, 0.25, 0.27); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(30)); scene.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
for name, d in {"side": (0, -1, 0.15), "top": (0.01, -0.01, 1), "persp": (0.8, -0.9, 0.5)}.items():
    dv = mathutils.Vector(d).normalized()
    cam.location = center + dv * size * 2.2
    cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = f"{outdir}/model_{name}.png"
    bpy.ops.render.render(write_still=True)
print("DONE")
