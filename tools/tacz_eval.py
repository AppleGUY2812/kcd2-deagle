#!/usr/bin/env python3
"""Evaluate a TACZ (Bedrock geometry + animation JSON) clip into per-frame world matrices of chosen bones,
in TACZ model space (pixels, Y up). Bedrock bones: rotate about their pivot (Z, then Y, then X: M = Rz*Ry*Rx,
with X and Y negated as in Bedrock/GeckoLib), then translate by the animated position. Keyframes are linear
(TACZ exports dense frames); "pre"/"post" and catmullrom keyframes are reduced to their values.
usage: tacz_eval.py <geo.json> <anim.json> <clip> <fps> <out.json> [bones...]"""
import json, math, sys
import numpy as np


def rot_xyz(deg):
    x, y, z = (math.radians(a) for a in deg)
    x, y = -x, -y
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return rz @ ry @ rx


def mat(r=np.eye(3), t=(0, 0, 0)):
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = t
    return m


def channel_value(ch, t):
    """ch: list/dict of keyframes; returns 3-vector at time t (linear)."""
    if isinstance(ch, (int, float, str)):
        return np.full(3, float(ch) if not isinstance(ch, str) else 1.0)
    if isinstance(ch, list) or (isinstance(ch, dict) and "vector" in ch):
        v = ch if isinstance(ch, list) else ch["vector"]
        return np.array([float(x) if not isinstance(x, str) else 0.0 for x in v])
    keys = []
    for k, v in ch.items():
        if isinstance(v, dict):
            v = v.get("post", v.get("pre", v.get("vector")))
            if isinstance(v, dict):
                v = v.get("vector")
        if isinstance(v, (int, float)):
            v = [v, v, v]
        keys.append((float(k), np.array([float(x) if not isinstance(x, str) else 0.0 for x in v])))
    keys.sort(key=lambda kv: kv[0])
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t0 <= t <= t1:
            a = 0 if t1 == t0 else (t - t0) / (t1 - t0)
            return v0 + (v1 - v0) * a
    return keys[-1][1]


def evaluate(geo_path, anim_path, clip, fps, bones_wanted):
    geo = json.load(open(geo_path))["minecraft:geometry"][0]
    bones = {b["name"]: b for b in geo["bones"]}
    anim = json.load(open(anim_path))["animations"][clip]
    length = float(anim.get("animation_length", 0.0))
    tracks = anim.get("bones", {})
    n = max(1, int(round(length * fps)) + 1)
    frames = []
    for i in range(n):
        t = i / fps
        cache = {}

        def world(name):
            if name in cache:
                return cache[name]
            b = bones[name]
            piv = np.array(b.get("pivot", [0, 0, 0]), dtype=float)
            r = rot_xyz(b.get("rotation", [0, 0, 0]))
            tr = tracks.get(name, {})
            if "rotation" in tr:
                r = r @ rot_xyz(channel_value(tr["rotation"], t))
            pos = channel_value(tr["position"], t) if "position" in tr else np.zeros(3)
            pos = pos * np.array([-1, 1, 1])  # Bedrock animation X position is mirrored vs geometry space
            scl = channel_value(tr["scale"], t) if "scale" in tr else np.ones(3)
            local = mat(t=pos) @ mat(t=piv) @ mat(r @ np.diag(scl)) @ mat(t=-piv)
            p = b.get("parent")
            m = (world(p) if p else np.eye(4)) @ local
            cache[name] = m
            return m

        frames.append({name: world(name).tolist() for name in bones_wanted})
    return {"clip": clip, "fps": fps, "length": length, "frames": frames,
            "rest_pivots": {name: bones[name].get("pivot", [0, 0, 0]) for name in bones},
            "cubes": {name: bones[name].get("cubes", []) for name in bones_wanted}}


if __name__ == "__main__":
    geo, anim, clip, fps, out = sys.argv[1:6]
    wanted = sys.argv[6:] or ["Deagle", "righthand_pos", "lefthand_pos", "slide2", "hammer", "magazine", "mag_and_lefthand"]
    res = evaluate(geo, anim, clip, float(fps), wanted)
    json.dump(res, open(out, "w"))
    print(f"{clip}: {len(res['frames'])} frames, {res['length']}s")
