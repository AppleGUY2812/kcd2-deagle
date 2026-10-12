# Blender (SourceIO): import a gun's viewmodel with all its sequences (as actions "@<sequence>"), save it as the
# gun's source .blend, and render 4 frames of every profiled clip from the eye (Source viewmodels: the model origin is
# the eye, +X forward, +Z up). Frames go to <out>/preview/<role>_<n>.png for tools/gb/gb_sheet.py.
# usage: blender -b --factory-startup -P gb_preview.py -- <extracted mod dir> <out dir>   (reads <out>/profile.json)
import bpy, sys, os, json, math, addon_utils
from mathutils import Matrix
SRC, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
prof = json.load(open(os.path.join(OUT, "profile.json")))
mdl = os.path.join(SRC, prof["mdl"].replace("/", os.sep))
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("SourceIO", default_set=True)
r = bpy.ops.sourceio.mdl(filepath=mdl, directory=os.path.dirname(mdl), files=[{"name": os.path.basename(mdl)}],
                         import_animations=True, import_include_animations=True, import_textures=True, discover_resources=True)
print("IMPORT", r)
scene = bpy.context.scene
arms = [o for o in scene.objects if o.type == "ARMATURE"]
if not arms:
    print("NO ARMATURE"); sys.exit(1)
arm = max(arms, key=lambda a: len(a.data.bones))
actions = {a.name.lstrip("@"): a for a in bpy.data.actions}
for a in bpy.data.actions:
    a.use_fake_user = True
print("ACTIONS", len(actions), sorted(actions)[:30])
slug = prof.get("slug") or "gun"
blend = os.path.join(OUT, f"{slug}_vm.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
print("SAVED", blend)

# ---- preview renders from the eye, KCD2's FOV (1.1011 rad vertical, 16:9)
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE"
scene.render.resolution_x, scene.render.resolution_y = 480, 270
world = bpy.data.worlds.new("w"); world.color = (0.62, 0.7, 0.78); scene.world = world
cam = bpy.data.objects.new("eye", bpy.data.cameras.new("eye")); scene.collection.objects.link(cam); scene.camera = cam
cam.data.sensor_fit = "VERTICAL"; cam.data.angle = 1.1011; cam.data.clip_start = 0.005
cam.rotation_euler = (Matrix.Rotation(-math.pi / 2, 4, "Z") @ Matrix.Rotation(math.pi / 2, 4, "X")).to_euler()
cam.location = (0, 0, 0)
if not arm.animation_data:
    arm.animation_data_create()
pdir = os.path.join(OUT, "preview")
os.makedirs(pdir, exist_ok=True)
for f in os.listdir(pdir):
    os.remove(os.path.join(pdir, f))
order = ["draw", "idle", "shoot", "shoot_empty", "reload", "reload_empty", "inspect", "idle_empty", "holster", "sprint"]
roles = sorted(prof["clips"], key=lambda r: order.index(r) if r in order else 99)
for role in roles:
    seq = prof["clips"][role]["sequence"]
    act = actions.get(seq)
    if not act:
        print("NO ACTION FOR", role, seq); continue
    arm.animation_data.action = act
    f0, f1 = (int(v) for v in act.frame_range)
    for i, k in enumerate((0.0, 0.33, 0.66, 1.0)):
        scene.frame_set(int(round(f0 + (f1 - f0) * k)))
        scene.render.filepath = os.path.join(pdir, f"{role}_{i}.png")
        bpy.ops.render.render(write_still=True)
    print("RENDERED", role, seq, f0, f1)
print("PREVIEW DONE")
