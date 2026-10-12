# Blender: render a .psk (ActorX) with a base-colour texture from 3 sides. usage: blender -b -P psk_render.py -- <psk> <tex.png> <out_prefix> [scale_cm]
import bpy, sys, struct, math, os
from mathutils import Vector
psk, tex, outp = sys.argv[sys.argv.index("--") + 1:][:3]
d = open(psk, "rb").read(); o = 0; ch = {}
while o + 32 <= len(d):
    cid = d[o:o + 20].split(b"\0")[0].decode(); _, size, cnt = struct.unpack_from("<iii", d, o + 20); o += 32
    ch[cid] = (o, size, cnt); o += size * cnt
po, ps, pc = ch["PNTS0000"]; pts = [struct.unpack_from("<fff", d, po + 12 * i) for i in range(pc)]
wo, ws, wc = ch["VTXW0000"]; wed = []
for i in range(wc):
    b = wo + ws * i
    pi = struct.unpack_from("<I", d, b)[0] if pc > 65536 else struct.unpack_from("<H", d, b)[0]
    u, v = struct.unpack_from("<ff", d, b + 4); wed.append((pi, u, v))
fk = "FACE3200" if "FACE3200" in ch else "FACE0000"; fo, fs, fc = ch[fk]; faces = []
for i in range(fc):
    b = fo + fs * i
    w = struct.unpack_from("<III", d, b) if fk == "FACE3200" else struct.unpack_from("<HHH", d, b)
    faces.append(w)
bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new("m"); me.from_pydata([(p[0] * 0.01, -p[1] * 0.01, p[2] * 0.01) for p in pts], [], [(wed[a][0], wed[c][0], wed[b][0]) for a, b, c in faces])
uv = me.uv_layers.new(); li = 0
for (a, b, c) in faces:
    for wi in (a, c, b): uv.data[li].uv = (wed[wi][1], 1 - wed[wi][2]); li += 1
me.update(); ob = bpy.data.objects.new("psk", me); bpy.context.scene.collection.objects.link(ob)
for p in me.polygons: p.use_smooth = True
mat = bpy.data.materials.new("t"); mat.use_nodes = True; bs = mat.node_tree.nodes["Principled BSDF"]
img = mat.node_tree.nodes.new("ShaderNodeTexImage"); img.image = bpy.data.images.load(tex); mat.node_tree.links.new(img.outputs["Color"], bs.inputs["Base Color"])
bs.inputs["Roughness"].default_value = 0.5; ob.data.materials.append(mat)
bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]; ctr = sum(bb, Vector()) / 8; rad = max((v - ctr).length for v in bb)
sc = bpy.context.scene; sc.render.engine = "BLENDER_EEVEE_NEXT"; sc.render.resolution_x = 800; sc.render.resolution_y = 600
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.35, 0.37, 1); w.node_tree.nodes["Background"].inputs[1].default_value = 1.5
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); sun.data.energy = 4; sun.rotation_euler = (0.7, 0.2, 0.8); sc.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
for name, dirv in (("side", Vector((1, 0, 0.15))), ("front", Vector((0.3, -1, 0.25))), ("top", Vector((0.2, 0.1, 1)))):
    cam.location = ctr + dirv.normalized() * rad * 2.6
    cam.rotation_euler = (ctr - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = f"{outp}_{name}.png"; bpy.ops.render.render(write_still=True)
print("PSK", len(pts), "points", len(faces), "faces", "radius cm", round(rad * 100, 1))
