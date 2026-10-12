# Blender: dumps every viewmodel clip's gun / hand / elbow / slide / magazine transforms per frame (with the export's
# section-zero fix) for tools/vm_transition_audit.py. usage: blender -b mwii_vm_extra.blend -P vm_tracks_dump.py -- <out.json>
import bpy, sys
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == 'ARMATURE')
OUTJ = sys.argv[sys.argv.index('--') + 1]
def fix_section_zeros():
    rest = {b.name: (b.parent.matrix_local.inverted() @ b.matrix_local) if b.parent else b.matrix_local for b in arm.data.bones}
    cand = [n for n, m in rest.items() if m.translation.length > 1e-3]
    def zero_frames(act):
        arm.animation_data.action = act
        f0, f1 = (int(v) for v in act.frame_range)
        bad = {}
        for f in range(f0, f1 + 1):
            scene.frame_set(f)
            for n in cand:
                pb = arm.pose.bones[n]
                if (pb.parent.matrix.inverted() @ pb.matrix).translation.length < 1e-5:
                    bad.setdefault(n, set()).add(f)
        return bad
    for act in [a for a in bpy.data.actions if a.name.startswith("@")]:
        bad = zero_frames(act)
        for n, frames in bad.items():
            for fc in act.fcurves:
                if not fc.data_path.startswith(f'pose.bones["{n}"].'):
                    continue
                v = 1.0 if fc.data_path.endswith("rotation_quaternion") and fc.array_index == 0 else 0.0
                for kp in fc.keyframe_points:
                    if int(round(kp.co.x)) in frames:
                        kp.co.y = kp.handle_left.y = kp.handle_right.y = v
                fc.update()
        if bad:
            left = zero_frames(act)
            print("SECTION ZEROS", act.name, {n: len(f) for n, f in bad.items()}, "left after fix", {n: len(f) for n, f in left.items()})

import json
fix_section_zeros()
BONES = ["j_gun", "j_wrist_ri", "j_wrist_le", "j_elbow_ri", "j_elbow_le", "j_slide", "j_mag1", "j_mag2"]
mw = arm.matrix_world
out = {}
for act in sorted(a for a in bpy.data.actions.keys() if a.startswith("@")):
    A = bpy.data.actions[act]; arm.animation_data.action = A
    f0, f1 = (int(v) for v in A.frame_range)
    frames = []
    for f in range(f0, f1 + 1):
        scene.frame_set(f); bpy.context.view_layer.update()
        fr = {}
        for b in BONES:
            m = mw @ arm.pose.bones[b].matrix
            q = m.to_quaternion()
            fr[b] = [round(v * 1000, 3) for v in m.translation] + [q.w, q.x, q.y, q.z]
        frames.append(fr)
    out[act[1:]] = frames
    print("DUMP", act, len(frames))
json.dump(out, open(OUTJ, "w"))
print("DUMP DONE")
