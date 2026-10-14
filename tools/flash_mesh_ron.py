# Ready or Not's muzzle flash as flash cards (the "ron" flash look): one card per set, variant and stage from the quads
# tools/ron/flash_bake.py baked (cards.json), in the muzzle bone's axes (+Y down the barrel). tools/flash_skin.py makes
# them skins on each viewmodel's skeleton: flash_mesh_ron.build(cards.json). Sub-material ids follow the order of
# cards.json "materials" (= the .mtl's), and every card carries all of them in that order.
import bpy, json

MTL = "deagle_vm_flash_ron"

def build(cards):   # -> the card objects <set>_<variant>_<stage>, one mesh each
    D = json.load(open(cards))
    mats = {name: bpy.data.materials.new(f"{MTL}__{n:02d}__{name}__physNone") for n, name in enumerate(D["materials"], 1)}
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
    return nodes
