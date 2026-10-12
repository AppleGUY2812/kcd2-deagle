# The mesh muzzle flash (docs/flash_mesh_plan.md, from johnpork-9f's prototype): 4 variants of flash cards on the muzzle
# bone (+Y down the barrel), exported as <name>_1..4.cgf with the KCD2 toolkit + rc. Material library deagle_vm_flash
# (sub-materials front, side, core: src/Objects/manmade/weapons/deagle/vm/deagle_vm_flash.mtl).
# blender -b --factory-startup -P flash_mesh.py -- <out_dir> <rc.exe> <tex_dir> <scale> <name> [preview.png]
#   Deagle: scale 1, name deagle_vm_flash; Glock (blast 0.48x the Deagle's): scale 0.48, name deagle_vm_flash_s
import bpy, sys, math, addon_utils, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
OUT, RC, TEX, SCALE, NAME = argv[0], argv[1], argv[2], float(argv[3]), argv[4]
PREVIEW = argv[5] if len(argv) > 5 else None
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC
Configuration.texture_rc_path = RC
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)

MTL = "deagle_vm_flash"
SHEETS = {"front": (2, 2, "deagle_flash_front_diff"), "side": (4, 2, "deagle_flash_side_diff"), "core": (2, 2, "deagle_flash_ring_diff")}
SUB = {"front": 1, "side": 2, "core": 3}
mats = {}
for k, n in SUB.items():
    m = bpy.data.materials.new(f"{MTL}__{n:02d}__{k}__physNone")
    m.use_nodes = True
    nt = m.node_tree; nt.nodes.clear()
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = bpy.data.images.load(f"{TEX}/{SHEETS[k][2]}.tif")
    em = nt.nodes.new("ShaderNodeEmission"); em.inputs[1].default_value = {"front": 3, "side": 3, "core": 8}[k]
    tint = nt.nodes.new("ShaderNodeMixRGB"); tint.blend_type = "MULTIPLY"; tint.inputs[0].default_value = 1
    tint.inputs[2].default_value = {"front": (1, .55, .25, 1), "side": (1, .55, .25, 1), "core": (1, .92, .8, 1)}[k]
    mul = nt.nodes.new("ShaderNodeMixRGB"); mul.blend_type = "MULTIPLY"; mul.inputs[0].default_value = 1
    nt.links.new(tex.outputs["Color"], tint.inputs[1]); nt.links.new(tint.outputs[0], mul.inputs[1])
    nt.links.new(tex.outputs["Alpha"], mul.inputs[2]); nt.links.new(mul.outputs[0], em.inputs[0])
    tr = nt.nodes.new("ShaderNodeBsdfTransparent"); add = nt.nodes.new("ShaderNodeAddShader")
    nt.links.new(em.outputs[0], add.inputs[0]); nt.links.new(tr.outputs[0], add.inputs[1])
    out = nt.nodes.new("ShaderNodeOutputMaterial"); nt.links.new(add.outputs[0], out.inputs[0])
    m.blend_method = "BLEND"
    mats[k] = m

def tile_uv(kind, t):
    tx, ty, _ = SHEETS[kind]
    c, r = t % tx, t // tx                      # tile 0 = top-left, row-major (as the particle sheets)
    e = 2 / 512                                 # inset so mips don't bleed the next tile in
    u0, u1 = c / tx + e, (c + 1) / tx - e
    v0, v1 = 1 - (r + 1) / ty + e, 1 - r / ty - e   # Blender V is up
    return [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]

def quad(name, verts, kind, tile):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], [(0, 1, 2, 3)])
    uv = me.uv_layers.new(name="UVMap")
    for li, co in zip(me.polygons[0].loop_indices, tile_uv(kind, tile)):
        uv.data[li].uv = co
    me.materials.append(mats[kind])
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    return ob

def front(name, w, y, rot, kind, tile):            # faces down the barrel (+Y), centred on the bore
    h = w * SCALE / 2; a = math.radians(rot)
    R = lambda x, z: (x * math.cos(a) - z * math.sin(a), y * SCALE, x * math.sin(a) + z * math.cos(a))
    return quad(name, [R(-h, -h), R(h, -h), R(h, h), R(-h, h)], kind, tile)

def tongue(name, L, theta, tile):                  # a plane through the bore: tile bottom (hot base) at the muzzle
    L *= SCALE; w = L / 4; a = math.radians(theta)
    r = (math.cos(a), 0, math.sin(a)); y0 = -0.005 * SCALE
    P = lambda s, y: (r[0] * s, y, r[2] * s)
    return quad(name, [P(-w, y0), P(w, y0), P(w, y0 + L), P(-w, y0 + L)], "side", tile)

VARIANTS = [   # front (tile, width m, roll deg), extra front layer, core width, tongues (tile, length m, plane angle deg)
    dict(front=[(0, 0.36, 0)], core=0.11, tongues=[(0, 0.32, 0), (3, 0.28, 60), (7, 0.30, 120)]),
    dict(front=[(1, 0.40, 30)], core=0.12, tongues=[(1, 0.26, 20), (4, 0.34, 80), (5, 0.24, 140)]),
    dict(front=[(0, 0.42, 75), (1, 0.30, 20)], core=0.10, tongues=[(2, 0.30, 40), (3, 0.22, 100), (0, 0.36, 160)]),
    dict(front=[(1, 0.34, 50)], core=0.13, tongues=[(7, 0.36, 10), (1, 0.30, 70), (4, 0.28, 130)]),
]
nodes = []
for i, v in enumerate(VARIANTS, 1):
    parts = [front(f"f{i}_{j}", w, 0.03 + 0.01 * j, rot, "front", t) for j, (t, w, rot) in enumerate(v["front"])]
    parts.append(front(f"c{i}", v["core"], 0.012, 37 * i, "core", 1))       # ring sheet tile 1 = the solid hot blob
    parts += [tongue(f"t{i}_{j}", L, th, t) for j, (t, L, th) in enumerate(v["tongues"])]
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts: p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = ob.data.name = f"{NAME}_{i}"
    co = [x.co for x in ob.data.vertices]
    print("VARIANT", i, "tris", sum(len(p.vertices) - 2 for p in ob.data.polygons),
          "min", tuple(round(min(c[k] for c in co), 3) for k in range(3)), "max", tuple(round(max(c[k] for c in co), 3) for k in range(3)),
          "mats", [s.material.name for s in ob.material_slots])
    nodes.append(ob)

if PREVIEW:
    sc = bpy.context.scene; sc.render.engine = "BLENDER_EEVEE_NEXT"; sc.render.resolution_x, sc.render.resolution_y = 1200, 600
    w = bpy.data.worlds.new("w"); w.color = (0.02, 0.02, 0.025); sc.world = w
    for i, ob in enumerate(nodes):
        ob.location = (-0.9 + i * 0.6, 0, 0)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.type = "ORTHO"; cam.data.ortho_scale = 2.6
    cam.location = (0, -2.0, 0.55); cam.rotation_euler = (mathutils.Vector((0, 0.15, 0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = PREVIEW; bpy.ops.render.render(write_still=True)
    for i, ob in enumerate(nodes): ob.location = (0, 0, 0.75 - i * 0.5)
    cam.data.ortho_scale = 2.1; cam.location = (2.0, -0.5, 0.0)
    cam.rotation_euler = (mathutils.Vector((0, 0.15, 0)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.resolution_x, sc.render.resolution_y = 700, 1100
    sc.render.filepath = PREVIEW.replace('.png', '_side.png'); bpy.ops.render.render(write_still=True)
    for ob in nodes: ob.location = (0, 0, 0)

for ob in nodes:
    bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.bcry.add_cry_export_node(node_type="cgf", node_name=ob.name)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=f"{OUT}/{NAME}.dae", apply_modifiers=True,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("EXPORT DONE")
