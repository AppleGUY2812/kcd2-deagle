# Ready or Not first-person clips -> full local poses (plain Python + numpy; tools/ron/ron_import.py builds the Blender
# rig from them). RoN's FP arm clips are additive (AAT_LocalSpaceBase) over the gun's idle pose; its gun clips are full
# local poses of the gun skeleton (J_Gun root). Unreal's local-space additive (FTransform::BlendFromIdentityAndAccumulate):
#   rotation = additive.R * base.R, translation = base.T + additive.T, scale = base.S * (1 + additive.S).
# Bones a clip doesn't key take the base (arms: the idle pose; idle's missing bones: the skeleton's reference pose).
# Input: anim_dump.py's arms_clips.json / weapon_clips.json. Units: cm, Unreal axes; ron_import.py converts.
import json, math
import numpy as np


def qmul(a, b):   # Hamilton product, (x, y, z, w)
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz])


def qrot(q, v):   # rotate vector v by unit quaternion q
    u = np.array(q[:3]); w = q[3]
    return 2 * np.dot(u, v) * u + (w * w - np.dot(u, u)) * np.array(v) + 2 * w * np.cross(u, v)


class Skel:
    def __init__(self, D):
        self.bones = [b[0] for b in D['skeleton']['bones']]
        self.parent = [b[1] for b in D['skeleton']['bones']]
        self.ref = [(np.array(p[0], float), np.array(p[1], float), np.array(p[2], float)) for p in D['skeleton']['pose']]
        self.index = {n.lower(): i for i, n in enumerate(self.bones)}
        self.clips = D['clips']

    def frames(self, name):
        return self.clips[name]['frames']

    def local(self, clip, f, base=None, additive=None):
        """the local pose (list of (q, t, s) per bone) of a clip at frame f; additive clips over base (a local pose)"""
        C = self.clips[clip]['tracks']
        add = additive if additive is not None else (base is not None)
        pose = [tuple(x.copy() for x in b) for b in (base or self.ref)]
        for bone, tr in C.items():
            i = self.index.get(bone.lower())
            if i is None: continue
            k = min(f, len(tr['R']) - 1)
            r, t, s = np.array(tr['R'][k]), np.array(tr['T'][k]), np.array(tr['S'][k])
            if add:
                q0, t0, s0 = pose[i]
                q = qmul(r, q0); q /= np.linalg.norm(q)
                pose[i] = (q, t0 + t, s0 * (1 + s))
            else:
                pose[i] = (r / np.linalg.norm(r), t, s)
        return pose

    def world(self, pose):
        """component-space (q, t) per bone from a local pose (scale ignored: RoN's FP bones don't scale)"""
        out = [None] * len(pose)
        for i, (q, t, s) in enumerate(pose):
            p = self.parent[i]
            if p < 0: out[i] = (q, t)
            else:
                pq, pt = out[p]
                out[i] = (qmul(pq, q), pt + qrot(pq, t))
        return out


def load(path):
    return Skel(json.load(open(path)))


if __name__ == '__main__':
    import sys
    O = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/ron/grizzly'
    A, W = load(O + '/arms_clips.json'), load(O + '/weapon_clips.json')
    idle = A.local('fp_grizzly_idle_pose_b', 0)
    wa = A.world(idle)
    g = W.world(W.local('fp_grizzly_idle_pose_b_weapon', 0))
    for n in ('hand_RI', 'hand_LE', 'weapon_right', 'RightWeaponSocket', 'ik_hand_weapon_root', 'fp_camera'):
        i = A.index.get(n.lower())
        if i is not None: print(f'{n:22s} world {np.round(wa[i][1], 1)}')
    print('gun J_Gun in its idle clip', np.round(g[0][1], 2), 'grip safety', np.round(g[W.index["tag_grip_safety"]][1], 2))
