# Ready or Not's muzzle flash as flash cards (the "ron" flash look): builds one CGF per set, variant and stage from the
# quads tools/ron/flash_bake.py baked (cards.json), in the muzzle bone's axes (+Y down the barrel), with the KCD2 toolkit
# + rc, like tools/flash_mesh.py. Sub-material ids follow the order of cards.json "materials" (= the .mtl's), and every
# model carries all of them in that order: a model listing only the ones it uses (ids up to 25 against 4 names) drew on
# an entity but was silently refused as a bone attachment (in game 2026-10-14: the old cards stayed on).
# blender -b --factory-startup -P flash_mesh_ron.py -- <cards.json> <out_dir> <rc.exe> [preview.png]
import bpy, sys, json, addon_utils, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
CARDS, OUT, RC = argv[0], argv[1], argv[2]
PREVIEW = argv[3] if len(argv) > 3 else None
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)

D = json.load(open(CARDS))
MTL = "deagle_vm_flash_ron"
mats = {}
for n, name in enumerate(D["materials"], 1):
    m = bpy.data.materials.new(f"{MTL}__{n:02d}__{name}__physNone")
    mats[name] = m

nodes = []
for setname, variants in D["sets"].items():
    for v, stages in enumerate(variants, 1):
        for s, quads in enumerate(stages, 1):
            name = f"{setname}_{v}_{s}"
            me = bpy.data.meshes.new(name)
            verts, faces, uvs, slots = [], [], [], []
            for q in quads:
                i = len(verts)
                verts += [tuple(p) for p in q["p"]]
                faces.append((i, i + 1, i + 2, i + 3))
                uvs.append(q["uv"]); slots.append(D["materials"].index(q["mtl"]))
            me.from_pydata(verts, [], faces)
            uv = me.uv_layers.new(name="UVMap")
            for poly, quv, sl in zip(me.polygons, uvs, slots):
                for li, co in zip(poly.loop_indices, quv):
                    uv.data[li].uv = co
                poly.material_index = sl
            for mname in D["materials"]:
                me.materials.append(mats[mname])
            ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
            nodes.append(ob)
print("CARDS", len(nodes), "objects,", len(D["materials"]), "materials")

for ob in nodes:
    bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=ob.name)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=f"{OUT}/{MTL}.dae", apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE")
