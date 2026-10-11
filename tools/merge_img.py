#!/usr/bin/env python3
"""Merge CAF header chunks from rc-built Animations.img files into the game's Animations.img.
CryEngine chunk file 0x746: 'CrCh', version, chunk count, table offset; table entries are
(type, id, size, offset); each CAF header is one 0x09713007 chunk whose payload starts with flags then a
256-byte file path. Entries with the same path are replaced, new ones appended."""
import struct, sys

def read(path):
    d = open(path, "rb").read()
    sig, ver, n, tab = struct.unpack_from("<4sIII", d, 0)
    assert sig == b"CrCh" and ver == 0x746, (path, sig, hex(ver))
    chunks = []
    for i in range(n):
        typ, cid, size, off = struct.unpack_from("<IIII", d, tab + i * 16)
        chunks.append((typ, d[off:off + size]))
    return chunks

def path_of(payload):
    return payload[4:4 + 256].split(b"\0")[0].lower()

base, out, extras = sys.argv[1], sys.argv[2], sys.argv[3:]
chunks = read(base)
index = {path_of(p): i for i, (t, p) in enumerate(chunks)}
for extra in extras:
    for typ, payload in read(extra):
        key = path_of(payload)
        if key in index:
            chunks[index[key]] = (typ, payload)
            print("replaced", key.decode())
        else:
            index[key] = len(chunks)
            chunks.append((typ, payload))
            print("added", key.decode())

n = len(chunks)
table_off = 16
data_off = table_off + n * 16
table, data = bytearray(), bytearray()
for i, (typ, payload) in enumerate(chunks):
    table += struct.pack("<IIII", typ, i + 1, len(payload), data_off + len(data))
    data += payload
with open(out, "wb") as f:
    f.write(struct.pack("<4sIII", b"CrCh", 0x746, n, table_off) + table + data)
print("wrote", out, n, "chunks")
