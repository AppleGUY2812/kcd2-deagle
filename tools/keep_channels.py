# Shared: nudge every keyed rotation 0.2 degrees off the bind pose. rc drops controllers equal to the default
# pose, and a dropped controller lets the game's own animation (e.g. relaxed fingers) show through our layer.
import math
from mathutils import Quaternion

NUDGE = Quaternion((1, 0, 0), math.radians(0.2))

def nudge_identity(pose_bones, names, frame):
    for name in names:
        pb = pose_bones.get(name)
        if pb is None:
            continue
        q = pb.rotation_quaternion
        if abs(abs(q.w) - 1.0) < 1e-4:   # at (or within ~1.6 deg of) bind: make it distinct
            pb.rotation_quaternion = q @ NUDGE
            pb.keyframe_insert("rotation_quaternion", frame=frame)
