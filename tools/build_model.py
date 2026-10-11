# Blender (4.3 + KCD2 toolkit): source glTF -> 4 CGFs (frame, slide, mag, case) compiled by rc.exe.
# usage: blender -b --factory-startup -P build_model.py -- <scene.gltf> <out_dir> <rc.exe> [preview_dir]
import bpy, sys, math, mathutils, addon_utils

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT, RC = argv[0], argv[1], argv[2]
PREVIEW = argv[3] if len(argv) > 3 else None

addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC

bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
bpy.ops.import_scene.gltf(filepath=SRC)

# Part membership by source object name prefix (see assets/source inspection).
SLIDE = ("Slide_low", "Safety_low", "BoltMain_low", "BoltAdd_low", "RearSight_low", "SlidePart_low", "BoltButton_low", "BoltBack_low", "FrontSight_low")
MAG = ("Magazine_low", "MagazineBase_low", "BaseInside_low")
DROP = ("Bullet_low",)  # loose display bullet next to the case; not used
CASE = ("BulletCase_low",)
# CGF material ids come from the "<mtl>__<NN>__<name>__<physics>" naming; deagle.mtl lists them in this order.
MATS = {"MainBody": "deagle__01__MainBody__physNone", "Slide": "deagle__02__Slide__physNone",
        "Magazine": "deagle__03__Magazine__physDefault", "Bullet": "deagle__04__Bullet__physDefault"}

def part_of(obj):
    owner = obj.parent.name if obj.parent else obj.name
    for names, part in ((SLIDE, "slide"), (MAG, "mag"), (CASE, "case")):
        if owner in names:
            return part
    return "frame"

for o in [o for o in bpy.context.scene.objects if o.type == "MESH" and o.parent and o.parent.name in DROP]:
    bpy.data.objects.remove(o)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
membership = {o.name: part_of(o) for o in meshes}  # before the parent empties are removed
# bake world transforms (cm, Y-up import chain) into the meshes, then drop the empties
for o in meshes:
    mw = o.matrix_world.copy()
    o.parent = None
    o.matrix_world = mw
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in list(bpy.context.scene.objects):
    if o.type == "EMPTY":
        bpy.data.objects.remove(o)

for mat in bpy.data.materials:
    if mat.name in MATS:
        mat.name = MATS[mat.name]

def bbox(objs):
    pts = [o.matrix_world @ mathutils.Vector(c) for o in objs for c in o.bound_box]
    mn = mathutils.Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = mathutils.Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return mn, mx

grip = [o for o in meshes if o.name.startswith("Grip_low")]
barrel = [o for o in meshes if o.name.startswith("Barrel_low")]
gmn, gmx = bbox(grip)
bmn, bmx = bbox(barrel)
# pivot = palm position: grip centre, a third of the way down from its top
pivot = mathutils.Vector(((gmn.x + gmx.x) / 2, (gmn.y + gmx.y) / 2, gmx.z - (gmx.z - gmn.z) / 3))
muzzle_sign = 1 if (bmx.y - pivot.y) > (pivot.y - bmn.y) else -1
print("PIVOT", tuple(round(v, 3) for v in pivot), "muzzle towards", "+Y" if muzzle_sign > 0 else "-Y")

# metres, muzzle along +Y (CryEngine forward), Z up, pivot at origin
rot = mathutils.Matrix.Identity(4) if muzzle_sign > 0 else mathutils.Matrix.Rotation(math.pi, 4, "Z")
xf = mathutils.Matrix.Scale(0.01, 4) @ rot @ mathutils.Matrix.Translation(-pivot)
for o in meshes:
    o.data.transform(xf)
    o.matrix_world = mathutils.Matrix.Identity(4)

# join each part into one mesh
parts = {}
for o in meshes:
    parts.setdefault(membership[o.name], []).append(o)
joined = {}
for part, objs in parts.items():
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = f"deagle_{part}"
    joined[part] = ob

# the slide and magazine keep their assembled position relative to the frame pivot;
# the case is re-centred so it spins about its own middle when ejected
case = joined["case"]
vs = [v.co for v in case.data.vertices]
centre = mathutils.Vector((sum(v.x for v in vs), sum(v.y for v in vs), sum(v.z for v in vs))) / len(vs)
case.data.transform(mathutils.Matrix.Translation(-centre))
case.data.update()

for part, ob in joined.items():
    co = [v.co for v in ob.data.vertices]
    mn = mathutils.Vector((min(v.x for v in co), min(v.y for v in co), min(v.z for v in co)))
    mx = mathutils.Vector((max(v.x for v in co), max(v.y for v in co), max(v.z for v in co)))
    tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    print("PART", part, "tris", tris, "min", tuple(round(v, 3) for v in mn), "max", tuple(round(v, 3) for v in mx),
          "mats", [s.material.name for s in ob.material_slots])

for part, ob in joined.items():
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=f"deagle_{part}")

if PREVIEW:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE_NEXT"
    scene.render.resolution_x, scene.render.resolution_y = 900, 600
    world = bpy.data.worlds.new("w"); world.color = (0.25, 0.25, 0.27); scene.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4
    sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(30)); scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    # exploded view so each part is visible
    offsets = {"frame": (0, 0, 0), "slide": (0, 0, 0.06), "mag": (0, 0, -0.12), "case": (0.08, 0, 0.02)}
    for part, ob in joined.items():
        ob.location = offsets[part]
    cam.location = (0.45, -0.25, 0.12)
    cam.rotation_euler = (mathutils.Vector((0, 0.05, -0.02)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = f"{PREVIEW}/parts_exploded.png"
    bpy.ops.render.render(write_still=True)
    for ob in joined.values():
        ob.location = (0, 0, 0)

bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=f"{OUT}/deagle.dae", apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE")
