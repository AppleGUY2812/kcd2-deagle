#!/usr/bin/env python3
"""Sniper Elite V2 rigged models whose geometry is in the RSCF named after them (heart, lungs, ...; docs/se2_killcam.md).
Per model the stream is HSKN (bones) + HSBB (box) + RSCF '<name>' (lower case). RSCF payload: u32 nverts, u32 nidx, u32
ntris, u32 0; vertices 36 bytes each from payload offset 16 (+ 44 B first): position = 3 half floats (a 4th, w) at payload offset 28 + 36 * i, in metres; u16 triangle indices at 60 + 36 * nverts, nidx of them.
usage: organ.py <archive> --list | --obj <outdir> [substring ...]"""
import os, struct, sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from asr_unpack import load
from hskn import chunks, cstr
from rscf import containers

def read_box(b, at, size):
    """HSBB: 'HSBB', size, version, 0, the model's name (4-byte words), u32 n, 6 sentinel floats, then n records of 7 floats
    (0, xmin, xmax, ymin, ymax, zmin, zmax), one per bone. Returns the union (min xyz, max xyz) or None."""
    try:
        _, p = cstr(b, at + 16)
        n, = struct.unpack_from("<I", b, p); p += 4 + 24
        lo = [1e9] * 3; hi = [-1e9] * 3
        for k in range(n):
            r = struct.unpack_from("<7f", b, p + 28 * k)
            for a in range(3):
                lo[a] = min(lo[a], r[1 + 2 * a]); hi[a] = max(hi[a], r[2 + 2 * a])
        return (4, tuple(lo) + tuple(hi)) if n and all(abs(x) < 50 for x in lo + hi) else None
    except Exception:
        return None


def find_layout(raw, nv, nidx):
    """The vertex block starts at a different byte in different models (Heart_Solid: position at 28, the eyeball at 88):
    try every even offset for a half-float triple (+ w) at stride 36, with the index run that has its max at nv-1 (nv-3 at
    least), and take the pair with the shortest triangle edges relative to the model size. Returns (pos offset, triangles,
    edge / size, index offset) or None."""
    buf = np.frombuffer(raw, dtype=np.uint8)
    cands = []
    for q in range(16 + 36 * (nv - 2), len(raw) - nidx * 2 + 1, 2):
        a = np.frombuffer(raw, dtype="<u2", count=nidx, offset=q)
        if nv - 3 <= a.max() < nv: cands.append((q, a.astype(np.int64).reshape(-1, 3)))
    if not cands: return None
    best = None
    # the usual phase first (position at 28 + 36 k: Heart, lungs, kidneys, liver, windpipe upper), then any
    for offs in (range(28, 28 + 36 * 4, 36), range(0, 160, 2)):
        best = _scan(buf, raw, nv, cands, offs)
        if best is not None and best[2] < 0.15: return best
    return best


def _scan(buf, raw, nv, cands, offs):
    best = None
    for off in offs:
        if off + 36 * (nv - 1) + 8 > len(raw): break
        pos = np.lib.stride_tricks.as_strided(buf[off:], shape=(nv, 8), strides=(36, 1)).copy().view("<f2").astype(np.float32).reshape(nv, 4)[:, :3]
        if not np.isfinite(pos).all(): continue
        ext = pos.max(0) - pos.min(0)
        if ext.max() > 0.5 or ext.max() < 0.01 or ext.min() < 0.1 * ext.max(): continue   # an organ: 1 cm to 50 cm, 3-D
        diag = float(np.linalg.norm(ext))
        for q, t in cands:
            e = float(np.linalg.norm(pos[t[:, 0]] - pos[t[:, 1]], axis=1).mean()) / diag
            if best is None or e < best[2]: best = (off, t, e, q)
    return best

def organs(b):
    cs = list(chunks(b))
    rs = {c["name"]: c for c in containers(b)}
    for i, (cc, at, size) in enumerate(cs):
        if cc != b"HSKN": continue
        ver, flags, na, nb = struct.unpack_from("<4I", b, at + 8)
        name, _ = cstr(b, at + 0x18)
        r = rs.get(name.lower())
        if not r: continue
        box = None
        for j in range(i + 1, min(i + 4, len(cs))):
            if cs[j][0] == b"HSBB": box = read_box(b, cs[j][1], cs[j][2]); break
        raw = b[r["start"]:r["end"]]
        nv, nidx, ntri = struct.unpack_from("<3I", raw, 0)
        if nidx != ntri * 3 or nv < 4: continue
        lay = find_layout(raw, nv, nidx)
        if lay is None: continue
        pos_off, tri, edge, q = lay
        pos = np.lib.stride_tricks.as_strided(np.frombuffer(raw, dtype=np.uint8)[pos_off:], shape=(nv, 8), strides=(36, 1)).copy()
        pos = pos.view("<f2").astype(np.float32).reshape(nv, 4)[:, :3]   # metres
        uv = np.lib.stride_tricks.as_strided(np.frombuffer(raw, dtype=np.uint8)[pos_off - 16:], shape=(nv, 4), strides=(36, 1)).copy()
        uv = uv.view("<f2").astype(np.float32).reshape(nv, 2)   # the u32 at the record start (twice): half2 UVs
        yield {"name": name, "nv": nv, "ntri": ntri, "raw_pos": pos, "tri": tri, "box": box, "nA": na, "edge": edge, "idx_at": q, "flags": flags, "pos_off": pos_off, "uv": uv}

def write_obj(path, m, scale=None):
    p = m["raw_pos"]
    if m["box"]:
        lo, hi = np.array(m["box"][1][:3]), np.array(m["box"][1][3:])
        p = p if scale is None else p * scale
    with open(path, "w") as f:
        f.write(f"# {m['name']} (Sniper Elite V2, private)\n")
        for v in p: f.write("v %.6f %.6f %.6f\n" % tuple(v))
        for t in m["uv"]: f.write("vt %.5f %.5f\n" % (t[0], 1.0 - t[1]))
        for a, b_, c in m["tri"]: f.write(f"f {a + 1}/{a + 1} {b_ + 1}/{b_ + 1} {c + 1}/{c + 1}\n")

if __name__ == "__main__":
    blob = load(sys.argv[1])
    sel = [m for m in organs(blob) if len(sys.argv) < 4 or any(s.lower() in m["name"].lower() for s in sys.argv[(3 if sys.argv[2] == "--list" else 4):])]
    for m in sel:
        if sys.argv[2] == "--list":
            print(f"{m['name']:34s} {m['nv']:5d} verts {m['ntri']:5d} tris  edge {m['edge']:.4f} (of the model size)  idx at {m['idx_at']}")
        else:
            os.makedirs(sys.argv[3], exist_ok=True)
            write_obj(os.path.join(sys.argv[3], m["name"] + ".obj"), m)
    print(len(sel), "organ models")
