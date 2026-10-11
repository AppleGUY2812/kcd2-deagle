# Blender: import the GameBanana MWII Deagle viewmodel (Source 1 .mdl) with SourceIO and list what's inside:
# meshes, bones, animations (actions + frame ranges). Saves a .blend for later steps.
# usage: blender -b --factory-startup -P mwii_inspect.py -- <v_pist_deagle.mdl> <out.blend>
import bpy, sys, os, addon_utils
MDL, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.wm.read_factory_settings(use_empty=True)
addon_utils.enable("SourceIO", default_set=True)
d = os.path.dirname(MDL)
r = bpy.ops.sourceio.mdl(filepath=MDL, directory=d, files=[{"name": os.path.basename(MDL)}], import_animations=True, import_include_animations=True,
                         import_textures=True, discover_resources=True)
print("IMPORT", r)
for o in bpy.data.objects:
    if o.type == "MESH":
        print("MESH", o.name, len(o.data.vertices), "mats", [m.name for m in o.data.materials if m])
arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
for a in arms:
    print("ARMATURE", a.name, len(a.data.bones))
    print("BONES", [b.name for b in a.data.bones])
for act in bpy.data.actions:
    print("ACTION", act.name, tuple(round(v) for v in act.frame_range), "fcurves", len(act.fcurves))
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("SAVED", OUT)
