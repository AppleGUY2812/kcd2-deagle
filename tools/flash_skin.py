# The mesh muzzle flash cards (ours: tools/flash_mesh.py, Ready or Not's: tools/flash_mesh_ron.py) as skins of one
# viewmodel, one .skin per card with every vertex on the muzzle bone. As bone attachments (a CGF on the muzzle bone) every
# card drew the first card's model in KCD2, made at load or at run time (in game 2026-10-14, MODLOG); the viewmodel's
# skins (gun, magazines, rounds) each draw their own. A skin carries the whole skeleton as the toolkit exports it (the
# pose at export), so the gun's own export steps are replayed (tools/vm_export.py, tools/gb/gb_build.py: Source's +X view
# turned to CryEngine's +Y, bone names CryEngine takes, the muzzle bone rigid on its parent, the idle clip's first frame)
# and the cards are put on the muzzle bone by its bind matrix in the gun's .chr. tools/flash_cdf.py compares every
# skin's skeleton with the .chr's.
# blender -b --factory-startup -P flash_skin.py -- <gun.blend> <gun.chr> <idle action> <cards.json> <out_dir> <rc.exe> [muzzle bone]
import bpy, sys, os, re, math, struct, addon_utils
from mathutils import Matrix

ARGS = sys.argv[sys.argv.index("--") + 1:]
BLEND, CHR, IDLE, CARDS, OUT, RC = ARGS[:6]
MUZZLE = ARGS[6] if len(ARGS) > 6 else "tag_flash"   # Ready or Not guns: tag_muzzle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flash_mesh, flash_mesh_ron

def chr_bones(path):   # bone name -> bone-to-model matrix of the .chr's bind pose (CompiledBones 0x2000: m_DefaultB2W)
    d = open(path, "rb").read()
    _, num, toff = struct.unpack_from("<III", d, 4)
    c = next(d[off:off + size] for t, _, _, size, off in (struct.unpack_from("<HHIII", d, toff + i * 16) for i in range(num)) if t == 0x2000)
    out = {}
    for i in range((len(c) - 32) // 584):
        b = c[32 + i * 584: 32 + (i + 1) * 584]
        r = struct.unpack_from("<12f", b, 264)
        out[b[312:568].split(b"\0")[0].decode()] = Matrix((r[0:4], r[4:8], r[8:12], (0, 0, 0, 1)))
    return out

BONES = chr_bones(CHR)
bpy.ops.wm.open_mainfile(filepath=BLEND)
addon_utils.enable("io_KCD2_Blender_Toolkit", default_set=True)
from io_KCD2_Blender_Toolkit.bcry_exporter.configuration import Configuration
Configuration.rc_path = RC; Configuration.texture_rc_path = RC
import multiprocessing
multiprocessing.cpu_count = lambda: 3     # rc's /threads: keep the user's game smooth during a card rebuild
scene = bpy.context.scene
if bpy.context.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")
arm = next(o for o in scene.objects if o.type == "ARMATURE")
for o in [o for o in scene.objects if o is not arm]:
    bpy.data.objects.remove(o)
for c in [c for c in bpy.data.collections if c.name == "cry_export_nodes" or c.name.endswith((".chr", ".skin", ".cgf", ".i_caf"))]:
    bpy.data.collections.remove(c)

# the muzzle bone rigid on its parent in every clip
if MUZZLE not in arm.pose.bones:
    raise SystemExit(f"FLASH no {MUZZLE} in {BLEND}")
for act in bpy.data.actions:
    for fc in [fc for fc in act.fcurves if fc.data_path.startswith(f'pose.bones["{MUZZLE}"].')]:
        act.fcurves.remove(fc)
pb = arm.pose.bones[MUZZLE]
pb.location = (0, 0, 0); pb.rotation_quaternion = (1, 0, 0, 0); pb.scale = (1, 1, 1)
# Source's view looks +X -> CryEngine +Y
arm.matrix_world = Matrix.Rotation(math.radians(90), 4, "Z") @ arm.matrix_world
bpy.ops.object.select_all(action="DESELECT"); arm.select_set(True); bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
for b in arm.data.bones:
    if b.name not in BONES:
        b.name = re.sub(r"[^A-Za-z0-9_]", "_", b.name)
odd = [b.name for b in arm.data.bones if b.name not in BONES]
if odd or len(arm.data.bones) != len(BONES):
    raise SystemExit(f"FLASH skeleton differs from {CHR}: {len(arm.data.bones)} bones vs {len(BONES)}, not in the .chr: {odd[:8]}")
act = bpy.data.actions[IDLE]
arm.animation_data.action = act; scene.frame_set(int(act.frame_range[0])); bpy.context.view_layer.update()

# the toolkit's bone frame (utils.transform_bone_matrix): CryEngine X = the bone's Y, Y = its X, Z = -its Z
m = arm.matrix_world @ arm.pose.bones[MUZZLE].matrix
mine = Matrix.Identity(4)
for k, ax in enumerate((m.col[1].xyz.normalized(), m.col[0].xyz.normalized(), -m.col[2].xyz.normalized())):
    for r in range(3): mine[r][k] = ax[r]
mine.translation = m.translation
B2W = BONES[MUZZLE]
print("FLASH muzzle bind: .chr", [round(v, 4) for v in B2W.translation], "this pose", [round(v, 4) for v in mine.translation],
      "max diff", round(max(abs(B2W[r][c] - mine[r][c]) for r in range(3) for c in range(4)), 5))

nodes = bpy.data.collections.new("cry_export_nodes"); scene.collection.children.link(nodes)
cards = flash_mesh.build(None, 1.0, "deagle_vm_flash") + flash_mesh.build(None, 0.48, "deagle_vm_flash_s") + flash_mesh_ron.build(CARDS)
for ob in cards:
    ob.data.transform(B2W)                      # the muzzle bone's axes -> the model's, at the bind pose
    ob.parent = arm; ob.matrix_parent_inverse = Matrix.Identity(4)
    vg = ob.vertex_groups.new(name=MUZZLE); vg.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    md = ob.modifiers.new("Armature", "ARMATURE"); md.object = arm
    # the toolkit's export node (what bcry.add_cry_export_node makes): that operator takes a mesh of 4 vertices or fewer
    # on an armature for the skeleton's stand-in and files it as <armature>.chr, a one-quad card too
    node = bpy.data.collections.new(f"{ob.name}.skin"); nodes.children.link(node)
    for c in list(ob.users_collection):
        c.objects.unlink(ob)
    node.objects.link(ob)
print("FLASH", len(cards), "cards")
os.makedirs(OUT, exist_ok=True)
bpy.ops.bcry.export_to_game("EXEC_DEFAULT", filepath=os.path.join(OUT, "flash.dae"), apply_modifiers=False,
                            generate_materials=False, convert_textures=False, save_dae=True, disable_rc=False)
print("FLASH EXPORTED", len(cards), "skin nodes (rc keeps compiling after Blender quits: tools/flash_skin.sh waits)")
