#!/usr/bin/env python3
"""Sniper Elite V2 models (HSKN chunks of an .asr / .pc, docs/se2_killcam.md) -> OBJ. Layout read from
SEV2_Remastered.exe: the HSKN loader 0x8fc50 and the sub-mesh parser 0x1d1cc0.
chunk:   'HSKN', u32 size, u32 version, u32 flags, u32 nA, u32 nB (nodes), model name\\0, [nA*72 if nA and !(flags&0x40)],
         nB*4, nB*28, [u8 if version >= 10], then if flags&1, per node: name\\0, [u8 if version >= 10], u32 size, mesh (size B).
mesh:    u32 ver (10-13), u32 nverts, u32 nfaces, u32 f1, f2, f3, u16, skip 2, u32 colour, 6 f32 (box), u32,
         nverts x 3 f32 (positions), nfaces x 4 u16 (v0 v1 v2 + 1), then [f1: nfaces u16], [f2: skip nfaces u16],
         [f3: nfaces u32], u32, u32 g, [g: nfaces u32], then (ver 13) the attribute block (normals, UVs, weights: not read yet).
usage: hskn.py <archive> --list [substring]
       hskn.py <archive> --obj <outdir> <substring> [...]   (outdir outside the repo: private game assets)"""
import os, struct, sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from asr_unpack import load

def chunks(b):
    pos = 8
    while pos + 8 <= len(b):
        cc, size = b[pos:pos + 4], struct.unpack_from("<I", b, pos + 4)[0]
        if size < 8 or pos + size > len(b): break
        yield cc, pos, size
        pos += size

def cstr(b, p):
    """The game reads a string 4 bytes at a time until a word holds a NUL (0x9ddb0): it takes whole words."""
    q = p
    while b"\0" not in b[q:q + 4]:
        q += 4
        if q - p > 1024: raise ValueError("unterminated string")
    return b[p:b.index(b"\0", p)].decode("ascii", "replace"), q + 4

def parse_mesh(b, p, size):
    ver, nv, nf, f1, f2, f3 = struct.unpack_from("<6I", b, p)
    if not (10 <= ver <= 13 and nv < 200000 and nf < 400000 and 24 + 4 + 4 + 28 + 12 * nv + 8 * nf <= size + 8):
        raise ValueError(f"not a sane mesh (ver {ver}, {nv} verts, {nf} faces, {size} bytes)")
    q = p + 24 + 2 + 2
    colour, = struct.unpack_from("<I", b, q); q += 4
    box = struct.unpack_from("<6f", b, q); q += 24 + 4
    verts = [struct.unpack_from("<3f", b, q + 12 * i) for i in range(nv)]; q += 12 * nv
    faces = [struct.unpack_from("<4H", b, q + 8 * i) for i in range(nf)]; q += 8 * nf
    return {"ver": ver, "nv": nv, "nf": nf, "flags": (f1, f2, f3), "colour": colour, "box": box, "verts": verts,
            "faces": faces, "size": size, "attr_at": q}

def models(b):
    for cc, at, size in chunks(b):
        if cc != b"HSKN": continue
        ver, flags, na, nb = struct.unpack_from("<4I", b, at + 8)
        name, p = cstr(b, at + 0x18)
        if na and not flags & 0x40: p += na * 72
        p += nb * 4 + nb * 28
        if ver >= 10: p += 1
        meshes = []
        if flags & 1:
            for _ in range(nb):
                if p >= at + size: break
                mname, p = cstr(b, p)
                if ver >= 10: p += 1
                msize, = struct.unpack_from("<I", b, p); p += 4
                if msize:
                    try: meshes.append((mname, parse_mesh(b, p, msize)))
                    except Exception as e: meshes.append((mname, {"error": str(e)}))
                p += msize
        yield {"name": name, "at": at, "size": size, "ver": ver, "flags": flags, "nodes": nb, "meshes": meshes}

def write_obj(path, model):
    n = 0
    with open(path, "w") as f:
        f.write(f"# {model['name']} (Sniper Elite V2, private)\n")
        for mname, m in model["meshes"]:
            if "error" in m: continue
            f.write(f"o {mname}\n")
            for v in m["verts"]: f.write("v %.6f %.6f %.6f\n" % v)
            for a, b_, c, _ in m["faces"]: f.write(f"f {a + 1 + n} {b_ + 1 + n} {c + 1 + n}\n")
            n += m["nv"]

if __name__ == "__main__":
    blob = load(sys.argv[1])
    mode = sys.argv[2]
    subs = [s.lower() for s in sys.argv[(3 if mode == "--list" else 4):]]
    sel = [m for m in models(blob) if not subs or any(s in m["name"].lower() for s in subs)]
    for m in sel:
        if mode == "--list":
            nv = sum(x["nv"] for _, x in m["meshes"] if "nv" in x); nf = sum(x["nf"] for _, x in m["meshes"] if "nf" in x)
            bad = sum(1 for _, x in m["meshes"] if "error" in x)
            print(f"{m['name']:44s} v{m['ver']} {len(m['meshes'])} meshes {nv:6d} verts {nf:6d} faces{' ERR %d' % bad if bad else ''}")
        else:
            os.makedirs(sys.argv[3], exist_ok=True)
            write_obj(os.path.join(sys.argv[3], m["name"] + ".obj"), m)
    print(len(sel), "models")
