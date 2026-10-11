# Blender: move the gun sliders from the CONTROLS empty onto the Henry armature object, so they show under
# Object properties > Custom Properties while posing fingers in Pose mode. Drivers are re-pointed; values kept.
# usage: blender -b <kit.blend> -P kit_sliders_on_henry.py -- <out.blend>
import bpy, sys
OUT = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
ctl = bpy.data.objects["CONTROLS"]
for k in [k for k in ctl.keys() if k[:1].isdigit()]:
    arm[k] = ctl[k]
    ui = ctl.id_properties_ui(k).as_dict()
    arm.id_properties_ui(k).update(**{a: ui[a] for a in ("min", "max", "soft_min", "soft_max", "description", "step", "precision") if a in ui})
n = 0
for o in scene.objects:
    if o.animation_data:
        for fc in o.animation_data.drivers:
            for v in fc.driver.variables:
                for t in v.targets:
                    if t.id == ctl:
                        t.id = arm; n += 1
for k in [k for k in arm.keys() if not k[:1].isdigit()]:
    del arm[k]          # glTF import extras etc.: keep the panel to the sliders only
bpy.data.objects.remove(ctl)
for area in [a for s in bpy.data.screens for a in s.areas if a.type == "PROPERTIES"]:
    try:
        area.spaces[0].context = "OBJECT"
    except Exception:
        pass
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("SLIDERS ON HENRY", n, "drivers re-pointed", sorted(k for k in arm.keys()))
