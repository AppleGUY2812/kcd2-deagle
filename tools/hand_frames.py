#!/usr/bin/env python3
"""Turn a live hand capture (tools/capture_hand.lua) into full joint frames.

Facts (verified on a capture: child joints predicted to 0.08 mm mean / 0.15 mm max):
  - GetBonePos is world space; GetBoneLocal(bone, t) = (a.t, X.t, c.t) for an orthonormal world frame [a X c],
    X = GetBoneDir. In glTF-local axes of Henry's skin (the bind frames read from the glb inverse bind matrices)
    the joint's world frame is [-a, c, X] (columns = local x, y, z).
  - Old captures with only GetBoneDir (= local z) still give full frames for every joint that has a captured child:
    local z plus the bind offset to the child (perpendicular to z) pin the rotation (checked: <= 0.17 deg).
Writes {"bones": {name: {"pos": [...], "R": 3x3 or null}}} with R = glTF-local axes in game world (columns).
usage: hand_frames.py <capture.txt> <tag> <out.json> <glb>..."""
import json, struct, sys
import numpy as np


def read_binds(path):
    data = open(path, "rb").read()
    off, gltf, binary = 12, None, None
    while off < len(data):
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif ctype == 0x004E4942:
            binary = chunk
        off += 8 + clen
    skin = gltf["skins"][0]
    acc = gltf["accessors"][skin["inverseBindMatrices"]]
    view = gltf["bufferViews"][acc["bufferView"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    nodes = gltf["nodes"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    binds, parents = {}, {}
    for k, j in enumerate(skin["joints"]):
        ibm = np.array(struct.unpack_from("<16f", binary, base + k * 64)).reshape(4, 4).T
        binds[nodes[j]["name"]] = np.linalg.inv(ibm)
        p = parent.get(j)
        parents[nodes[j]["name"]] = nodes[p]["name"] if p is not None and p in skin["joints"] else None
    return binds, parents


def load_skeleton(glbs):
    binds, parents = {}, {}
    for g in glbs:
        b, p = read_binds(g)
        for k in b:
            binds.setdefault(k, b[k])
            parents.setdefault(k, p[k])
    return binds, parents


def read_capture(path, tag):
    cap = {}
    for line in open(path, encoding="utf-8", errors="ignore"):
        f = line.split()
        if len(f) < 10 or f[1] != "CAP" or f[2] != tag:
            continue
        v = np.array([float(x) for x in f[4:]])
        if len(v) >= 12:
            cap[f[3]] = {"pos": v[0:3], "a": v[3:6], "X": v[6:9], "c": v[9:12]}
        else:
            cap[f[3]] = {"pos": v[0:3], "X": v[3:6]}
    return cap


def kabsch(U, D):
    H = np.asarray(U).T @ np.asarray(D)
    u, _, vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(vt.T @ u.T))
    return vt.T @ np.diag([1, 1, d]) @ u.T


def frames(cap, binds, parents):
    kids = {}
    for b in cap:
        if parents.get(b) in cap and "Slot" not in b:
            kids.setdefault(parents[b], []).append(b)
    out = {}
    for b, d in cap.items():
        R = None
        if "a" in d:
            R = np.column_stack([-d["a"], d["c"], d["X"]])
        elif b in kids:
            U, D = [np.array([0.0, 0.0, 1.0])], [d["X"]]
            for c in kids[b]:
                l = (np.linalg.inv(binds[b]) @ binds[c])[:3, 3]
                U.append(l / np.linalg.norm(l))
                D.append((cap[c]["pos"] - d["pos"]) / np.linalg.norm(cap[c]["pos"] - d["pos"]))
            R = kabsch(U, D)
        out[b] = {"pos": d["pos"].tolist(), "R": None if R is None else R.tolist(), "z": d["X"].tolist()}
    return out


def check(cap, fr, binds, parents):
    errs = []
    for b in cap:
        p = parents.get(b)
        if p in fr and fr[p]["R"] is not None and "Slot" not in b and b in binds:
            l = (np.linalg.inv(binds[p]) @ binds[b])[:3, 3]
            errs.append(np.linalg.norm(np.array(fr[p]["pos"]) + np.array(fr[p]["R"]) @ l - cap[b]["pos"]) * 1000)
    return float(np.mean(errs)), float(np.max(errs)), len(errs)


if __name__ == "__main__":
    cap_path, tag, out_json, glbs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    binds, parents = load_skeleton(glbs)
    cap = read_capture(cap_path, tag)
    fr = frames(cap, binds, parents)
    mean, mx, n = check(cap, fr, binds, parents)
    print(f"{tag}: {len(cap)} bones, {sum(v['R'] is not None for v in fr.values())} full frames; "
          f"child-joint check mean {mean:.3f} mm max {mx:.3f} mm (n={n})")
    json.dump({"tag": tag, "bones": fr}, open(out_json, "w"), indent=1)
