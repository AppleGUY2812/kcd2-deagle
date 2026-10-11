import bpy, sys
argv = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=argv[0])
arm = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
act = arm.animation_data.action if arm.animation_data else None
print("FBX", arm.name, len(arm.data.bones), "bones", "action", act.name if act else None, "range", tuple(act.frame_range) if act else None, "scale", tuple(arm.scale), "rot", tuple(round(r,3) for r in arm.rotation_euler))
print("NAMES", [b.name for b in arm.data.bones][:70])
