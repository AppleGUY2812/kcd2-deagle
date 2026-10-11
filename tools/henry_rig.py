# Blender helper: import Henry with an armature whose bone frames equal KCD2's joint frames exactly.
# The KCD2 toolkit's skin import goes through glTF with Blender's bone-direction heuristic plus a roll "hack",
# so Blender bone axes differ per bone from the game's joints; animations exported from that rig rotate small
# bones (fingers, forearm twist) around the wrong axes in game. Here each bone is rebuilt from the glTF skin's
# inverse bind matrices (the game's bind pose) so bone.matrix_local == joint bind matrix.
import json, struct, bpy, mathutils
from mathutils import Matrix, Vector


def _read_glb(path):
    data = open(path, "rb").read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67, "not a glb"
    off, gltf, binary = 12, None, None
    while off < length:
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif ctype == 0x004E4942:
            binary = chunk
        off += 8 + clen
    return gltf, binary


def joint_binds(glb_path):
    """{joint name: world bind matrix in glTF space}, and {joint name: parent name}"""
    gltf, binary = _read_glb(glb_path)
    skin = gltf["skins"][0]
    acc = gltf["accessors"][skin["inverseBindMatrices"]]
    view = gltf["bufferViews"][acc["bufferView"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    nodes = gltf["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    binds, parents = {}, {}
    for k, j in enumerate(skin["joints"]):
        vals = struct.unpack_from("<16f", binary, base + k * 64)
        ibm = Matrix([vals[0:4], vals[4:8], vals[8:12], vals[12:16]]).transposed()  # glTF is column-major
        name = nodes[j]["name"]
        binds[name] = ibm.inverted()
        p = parent.get(j)
        parents[name] = nodes[p]["name"] if p is not None and p in skin["joints"] else None
    return binds, parents


def import_henry(glb_paths, toolkit_rotation=False):
    """Import Henry's meshes (glTF) and replace the armature with one built from the exact joint binds.
    Returns the armature object. Meshes are re-bound to it by vertex-group names."""
    before = set(bpy.context.scene.objects)
    for p in glb_paths:
        bpy.ops.import_scene.gltf(filepath=p)
    new = [o for o in bpy.context.scene.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    old_arms = [o for o in new if o.type == "ARMATURE"]

    binds, parents = {}, {}
    for p in glb_paths:
        b, pa = joint_binds(p)
        for k, v in b.items():
            binds.setdefault(k, v)
            parents.setdefault(k, pa.get(k))

    # glTF (Y up) -> Blender (Z up), as the glTF importer converts positions; then the toolkit's 180 deg yaw
    conv = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    yaw = Matrix.Rotation(3.141592653589793, 4, "Z") if toolkit_rotation else Matrix.Identity(4)
    world = {n: yaw @ conv @ m @ conv.inverted() @ yaw.inverted() for n, m in binds.items()}

    arm_data = bpy.data.armatures.new("HenryExact")
    arm = bpy.data.objects.new("Henry", arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = {}
    for name, m in world.items():
        b = arm_data.edit_bones.new(name)
        head = m.translation
        y = m.col[1].xyz.normalized()
        b.head = head
        b.tail = head + y * 0.03
        b.align_roll(m.col[2].xyz)  # bone Z axis -> joint Z axis; with Y fixed this sets X too
        eb[name] = b
    for name, b in eb.items():
        p = parents.get(name)
        if p in eb:
            b.parent = eb[p]
    bpy.ops.object.mode_set(mode="OBJECT")

    # verify: bone frames equal joint binds (rotation only; Blender bones carry no scale)
    worst = 0.0
    for name, m in world.items():
        bm = arm_data.bones[name].matrix_local
        worst = max(worst, max(abs(bm.to_3x3()[i][j] - m.to_3x3().normalized()[i][j]) for i in range(3) for j in range(3)))
    print("HENRY RIG bones", len(world), "max frame error", round(worst, 6))

    for o in meshes:
        for mod in [m for m in o.modifiers if m.type == "ARMATURE"]:
            mod.object = arm
        o.parent = arm
    for o in old_arms:
        bpy.data.objects.remove(o)
    return arm
