# Blender 4.3 + KCD2 toolkit: Sniper Elite V2 organ OBJs (tools/se2/organ.py) -> one CGF each, compiled by rc.exe.
# usage: blender -b --factory-startup -P build_organs.py -- <obj_dir> <out_dir> <rc.exe> <preview.png>
import bpy, sys, math, mathutils, addon_utils, os

OBJ_DIR, OUT, RC, PREVIEW = sys.argv[sys.argv.index("--") + 1:][:4]
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)

# material ids come from "<mtl>__<NN>__<name>__<physics>" (se2_organs.mtl lists them in this order)
MATS = {"heartlungs": "se2_organs__01__HeartLungs__physDefault", "guts": "se2_organs__02__GutsLiverKidneys__physDefault",
        "eyeball": "se2_organs__03__Eyeball__physDefault", "testes": "se2_organs__04__Testes__physDefault"}
WHICH = {"Heart_Solid": "heartlungs", "Lung_Left_Collapse": "heartlungs", "Lung_Right_Coll": "heartlungs",
         "Windpipe_Upper_Section": "heartlungs", "Windpipe_Lower_Section": "heartlungs", "Kidney_left": "guts",
         "Kidney_right": "guts", "Liver_Upper": "guts", "Liver_lower": "guts", "BCV_Eyeball": "eyeball", "Testicle_L": "testes"}
mats = {k: bpy.data.materials.new(v) for k, v in MATS.items()}
objs = []
for i, (name, mk) in enumerate(WHICH.items()):
    path = os.path.join(OBJ_DIR, name + ".obj")
    if not os.path.exists(path):
        print("missing", path); continue
    before = set(bpy.context.scene.objects)
    bpy.ops.wm.obj_import(filepath=path)        # Y-up OBJ -> Blender Z-up (the import's default axes)
    new = [o for o in bpy.context.scene.objects if o not in before and o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in new: o.select_set(True)
    bpy.context.view_layer.objects.active = new[0]
    if len(new) > 1: bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = "se2_" + name.lower()
    ob.data.materials.clear(); ob.data.materials.append(mats[mk])
    # origin at the middle of the bounding box so the model spawns where it's put
    c = sum((mathutils.Vector(b) for b in ob.bound_box), mathutils.Vector()) / 8
    ob.data.transform(mathutils.Matrix.Translation(-c))
    ob.location = (0.0, 0.0, 0.0)
    objs.append(ob)
    print("PART", ob.name, "verts", len(ob.data.vertices), "tris", sum(len(p.vertices) - 2 for p in ob.data.polygons),
          "size", tuple(round(v, 3) for v in ob.dimensions))
for ob in objs:
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=ob.name)

# preview: all of them in a row (a check on the shapes and the winding)
scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x, scene.render.resolution_y = 1400, 500
world = bpy.data.worlds.new("w"); world.color = (0.30, 0.30, 0.32); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3
sun.rotation_euler = (math.radians(50), math.radians(10), math.radians(30)); scene.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
x = 0.0
for ob in objs:
    ob.location = (x, 0, 0); x += max(ob.dimensions) * 1.2 + 0.02
cam.location = (x / 2, -0.9, 0.1)
cam.rotation_euler = (mathutils.Vector((x / 2, 0, 0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scene.render.filepath = PREVIEW
bpy.ops.render.render(write_still=True)
for ob in objs: ob.location = (0, 0, 0)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=f"{OUT}/se2_organs.dae", apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE")
