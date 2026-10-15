#!/usr/bin/env python3
"""List / extract the resources inside a SEV2 archive blob (docs/se2_killcam.md). Container: 'RSCF', u32 size (end = at + 4 +
size), 4 x u32 (type fields), u32 payload size, NUL-terminated name, then the original file's bytes at the end.
usage: rscf.py <file.asr|.pc> --list [substring]
       rscf.py <file.asr|.pc> --extract <outdir> <substring> [...]   (outdir outside the repo: private game assets)"""
import os, re, struct, sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from asr_unpack import load

def containers(b):
    for m in re.finditer(b"RSCF", b):
        o = m.start()
        if o + 32 > len(b): continue
        size, = struct.unpack_from("<I", b, o + 4)
        t0, t1, fl, psize = struct.unpack_from("<4I", b, o + 12)
        end = o + 4 + size
        z = b.find(b"\0", o + 28, o + 28 + 300)
        if z < 0 or end > len(b) or psize > size: continue
        name = b[o + 28: z].decode("ascii", "replace")
        start = end - psize
        if b[start - 4: start] == b"DDS ":   # the payload size doesn't count the DDS signature
            start -= 4
        yield {"at": o, "type": (t0, t1), "name": name, "start": start, "end": end, "size": end - start}

if __name__ == "__main__":
    b = load(sys.argv[1])
    mode = sys.argv[2]
    subs = [s.lower() for s in sys.argv[(3 if mode == "--list" else 4):]]
    sel = [c for c in containers(b) if not subs or any(s in c["name"].lower() for s in subs)]
    if mode == "--list":
        for c in sel: print(f"{c['at']:#010x} {c['type']} {c['size']:>9d}  {c['name']}")
        print(len(sel), "resources")
    else:
        out = sys.argv[3]
        for c in sel:
            p = os.path.join(out, c["name"].lstrip("\\/").replace("\\", "/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(b[c["start"]: c["end"]])
        print(len(sel), "extracted to", out)
