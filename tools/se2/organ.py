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

def organs(b):
    cs = list(chunks(b))
    rs = {c["name"]: c for c in containers(b)}
    for i, (cc, at, size) in enumerate(cs):
        if cc != b"HSKN": continue
        ver, flags, na, nb = struct.unpack_from("<4I", b, at + 8)
        name, _ = cstr(b, at + 0x18)
        r = rs.get(name.lower())
        if not r or not flags & 0x40: continue
        box = None
        for j in range(i + 1, min(i + 4, len(cs))):
            if cs[j][0] == b"HSBB": box = read_box(b, cs[j][1], cs[j][2]); break
        raw = b[r["start"]:r["end"]]
        nv, nidx, ntri = struct.unpack_from("<3I", raw, 0)
        if nidx != ntri * 3 or nv < 4: continue
        buf = np.frombuffer(raw, dtype=np.uint8)
        if 28 + 36 * (nv - 1) + 8 > len(raw): continue
        pos = np.lib.stride_tricks.as_strided(buf[28:], shape=(nv, 8), strides=(36, 1)).copy()
        pos = pos.view("<f2").astype(np.float32).reshape(nv, 4)[:, :3]   # metres
        if not np.isfinite(pos).all(): continue
        # the index block follows the vertices after a few bytes whose count varies: take the offset whose triangles are
        # in range and shortest (a wrong offset or a wrong vertex phase makes long, random edges)
        best = None
        for q in range(16 + 36 * (nv - 2), len(raw) - nidx * 2 + 1, 2):
            a = np.frombuffer(raw, dtype="<u2", count=nidx, offset=q)
            if a.max() >= nv or a.max() < nv - 3: continue
            t = a.astype(np.int64).reshape(-1, 3)
            e = float(np.linalg.norm(pos[t[:, 0]] - pos[t[:, 1]], axis=1).mean())
            if best is None or e < best[0]: best = (e, q, t)
        if best is None: continue
        tri = best[2]
        yield {"name": name, "nv": nv, "ntri": ntri, "raw_pos": pos, "tri": tri, "box": box, "nA": na, "edge": best[0], "idx_at": best[1]}

def write_obj(path, m, scale=None):
    p = m["raw_pos"]
    if m["box"]:
        lo, hi = np.array(m["box"][1][:3]), np.array(m["box"][1][3:])
        p = p if scale is None else p * scale
    with open(path, "w") as f:
        f.write(f"# {m['name']} (Sniper Elite V2, private)\n")
        for v in p: f.write("v %.6f %.6f %.6f\n" % tuple(v))
        for a, b_, c in m["tri"]: f.write(f"f {a + 1} {b_ + 1} {c + 1}\n")

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
