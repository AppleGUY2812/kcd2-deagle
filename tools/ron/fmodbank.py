"""Read the structure of an FMOD Studio .bank (RIFF 'FEV '): chunk tree with offsets, and the strings bank
(GUID -> path). Read-only. The bank format is undocumented; chunk meanings here are inferred (see docs/ron_fmod_events.md)."""
import struct, uuid

class Chunk:
    def __init__(s, cid, off, size, data, kids=None): s.id, s.off, s.size, s.data, s.kids = cid, off, size, data, kids or []
    def find(s, path):
        """all chunks under this one matching a path like 'EVTS/EVNT/EVTB' (list ids or chunk ids)"""
        parts = path.split('/'); out = [s]
        for p in parts: out = [k for c in out for k in c.kids if k.id == p]
        return out
    def __repr__(s): return f"<{s.id} @{s.off} {s.size}b {len(s.kids)} kids>"

def parse(path):
    d = open(path, 'rb').read()
    def walk(o, end):
        out = []
        while o + 8 <= end:
            cid = d[o:o+4].decode('latin1'); sz, = struct.unpack_from('<I', d, o+4)
            if cid in ('RIFF', 'LIST'):
                t = d[o+8:o+12].decode('latin1')
                kids = walk(o + 12, o + 8 + sz) if t != 'SND ' else []
                out.append(Chunk(t, o, sz, d[o+12:o+8+sz] if t == 'SND ' else b'', kids))
            else:
                out.append(Chunk(cid, o, sz, d[o+8:o+8+sz]))
            o += 8 + sz + (sz & 1)
        return out
    return Chunk('ROOT', 0, len(d), b'', walk(0, len(d))), d

def guid(b, o=0): return str(uuid.UUID(bytes_le=bytes(b[o:o+16])))
