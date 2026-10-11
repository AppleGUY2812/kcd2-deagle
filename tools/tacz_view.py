# Blender: rebuild TACZ's first-person viewmodel (every cube of the gun + both arm boxes) for a clip frame and
# render it from TACZ's idle_view camera, to validate tacz_eval's Bedrock conventions against the real mod.
import bpy, sys, os, json, math
import numpy as np
from mathutils import Matrix, Vector
argv = sys.argv[sys.argv.index("--") + 1:]
GEO, ANIM, CLIP, FRAME, OUT = argv[0], argv[1], argv[2], int(argv[3]), argv[4]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tacz_eval
geo = json.load(open(GEO))["minecraft:geometry"][0]
names = [b["name"] for b in geo["bones"]]
res = tacz_eval.evaluate(GEO, ANIM, CLIP, 30, names)
fr = res["frames"][min(FRAME, len(res["frames"]) - 1)]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
# TACZ model space (px, Y up, forward -Z) -> Blender (Z up, forward +Y): (x, y, z) -> (x, -z, y)
C = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
mats = {"gun": (0.25, 0.25, 0.28, 1), "right": (0.8, 0.4, 0.3, 1), "left": (0.3, 0.5, 0.8, 1)}
for k, col in list(mats.items()):
    m = bpy.data.materials.new(k); m.diffuse_color = col; mats[k] = m
for b in geo["bones"]:
    cubes = b.get("cubes", [])
    if not cubes or b["name"] in ("ground", "fixed", "thirdperson_hand"):
        continue
    W = C @ Matrix(fr[b["name"]])
    kind = "right" if "righthand" in b["name"] else "left" if "lefthand" in b["name"] else "gun"
    for c in cubes:
        o, s = Vector(c["origin"]), Vector(c["size"])
        verts = [o + Vector((s.x * i, s.y * j, s.z * k)) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
        if "rotation" in c:
            piv = Matrix.Translation(Vector(c.get("pivot", [0, 0, 0])))
            R = Matrix(tacz_eval.rot_xyz(c["rotation"]).tolist()).to_4x4()
            verts = [piv @ R @ piv.inverted() @ v for v in verts]
        faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        me = bpy.data.meshes.new("c"); me.from_pydata([W @ v for v in verts], [], faces); me.materials.append(mats[kind])
        scene.collection.objects.link(bpy.data.objects.new(b["name"], me))
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
eye = C @ Vector(fr.get("idle_view", None) and Matrix(fr["idle_view"]).translation or Vector((2, 8, 16.5)))
cam.location = C @ Vector((2, 8, 16.5)) if True else eye
cam.rotation_euler = Vector((0, 1, 0)).to_track_quat("-Z", "Z").to_euler()
cam.data.lens = 18
world = bpy.data.worlds.new("w"); world.color = (0.6, 0.7, 0.8); scene.world = world
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 3; sun.rotation_euler = (0.6, 0.2, 0.4); scene.collection.objects.link(sun)
scene.render.engine = "BLENDER_EEVEE_NEXT"; scene.render.resolution_x, scene.render.resolution_y = 960, 540
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("TACZVIEW", OUT)
