# Blender: dumps a converted gun's clip tracks (gun / hands / elbows / slide / magazines) for the transition audit.
# usage: blender -b <gun dir>/<slug>_vm_roles.blend -P gb_tracks_dump.py -- <out.json> <profile.json>
import bpy, sys
scene = bpy.context.scene
arm = max([o for o in scene.objects if o.type == "ARMATURE"], key=lambda a: len(a.data.bones))
OUTJ, PROF = sys.argv[sys.argv.index("--") + 1:][:2]

# ---- generic dump for a converted gun: <slug>_vm_roles.blend (clips named by role, all fixes applied) -> tracks keyed
# by the names tools/vm_transition_audit.py expects (idle1, shoot1, j_gun, j_slide, j_mag1...)
import json
GUN_PROF = json.load(open(PROF))
B = {r: v["bone"] for r, v in GUN_PROF["bones"].items() if v.get("bone")}
BONES = {"j_gun": B.get("gun"), "j_wrist_ri": "j_wrist_ri", "j_wrist_le": "j_wrist_le", "j_elbow_ri": "j_elbow_ri", "j_elbow_le": "j_elbow_le",
         "j_slide": B.get("slide"), "j_mag1": B.get("mag"), "j_mag2": B.get("mag2")}
BONES = {k: v for k, v in BONES.items() if v and v in arm.pose.bones}
NAMES = {"idle": "idle1", "shoot": "shoot1"}
mw = arm.matrix_world
out = {}
for act in sorted(a for a in bpy.data.actions.keys() if a.startswith("@")):
    A_ = bpy.data.actions[act]; arm.animation_data.action = A_
    f0, f1 = (int(v) for v in A_.frame_range)
    frames = []
    for f in range(f0, f1 + 1):
        scene.frame_set(f); bpy.context.view_layer.update()
        fr = {}
        for k, b in BONES.items():
            m = mw @ arm.pose.bones[b].matrix
            q = m.to_quaternion()
            fr[k] = [round(v * 1000, 3) for v in m.translation] + [q.w, q.x, q.y, q.z]
        frames.append(fr)
    role = act[1:]
    out[NAMES.get(role, role)] = frames
    if role == "shoot" and "@shoot2" not in bpy.data.actions:
        out["shoot2"] = frames
    print("DUMP", act, len(frames), "@", A_.get("fps"))
out["__fps__"] = {k: float(bpy.data.actions["@" + ({v: k2 for k2, v in NAMES.items()}.get(k, k))].get("fps", 30.0))
                  for k in list(out) if ("@" + ({v: k2 for k2, v in NAMES.items()}.get(k, k))) in bpy.data.actions}
out["__fps__"].setdefault("shoot2", out["__fps__"].get("shoot1", 30.0))
json.dump(out, open(OUTJ, "w"))
print("DUMP DONE")
