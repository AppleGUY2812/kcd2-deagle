#!/usr/bin/env python3
"""KCD2 save files (.whs), read and written (found 2026-10-19 from WHGame.dll's loader, FUN_1837065e0):
  u32 0xFFFFFFFF, u32 n, n bytes of C_SaveGameDescription XML (the save menu's text, the mods used)
  zlib chunks: u32 compressed size, u32 raw size (32768 but the last), the zlib stream     (CryEngine XMLCPB);
    compressed size 0xFFFFFFFF = the chunk is stored as is (it didn't compress)
  64-byte footer: 4 bytes "0XBP", the MD5 (16), zeros
  MD5 = md5(file[:-64] + footer with its MD5 field zeroed); the loader refuses a save whose MD5 is off.
The raw data (5.8 MB for a mid-game save): u32, then one tree of tagged records (u16 tag, u32 length, value; a value
is either data or more records), then one byte. A record's tag names its field; the names aren't in the save.
  kcd2_save.py info <save.whs>                      sizes, chunks, MD5 check
  kcd2_save.py roundtrip <save.whs> <out.whs>       decode + encode with nothing changed (tests the writer)
  kcd2_save.py find <save.whs> <hex | guid>         where bytes / a GUID (as KCD2 stores it) sit, with their records
Private use: saves are the user's own; edit copies, never the only save."""
import hashlib
import struct
import sys
import uuid
import zlib

CHUNK = 32768


def read(path):
    d = open(path, "rb").read()
    magic, n = struct.unpack_from("<iI", d, 0)
    assert magic == -1, "not a KCD2 save"
    desc = d[8:8 + n]
    o, raw = 8 + n, bytearray()
    chunks = 0
    while o < len(d) - 64:
        c, u = struct.unpack_from("<II", d, o)
        if c == 0xFFFFFFFF:      # stored: the chunk didn't compress (autosave277, chunk 142)
            blk, c = d[o + 8:o + 8 + u], u
        else:
            blk = zlib.decompress(d[o + 8:o + 8 + c])
        assert len(blk) == u, (o, len(blk), u)
        raw += blk
        o += 8 + c
        chunks += 1
    assert o == len(d) - 64, ("footer not where expected", o, len(d))
    footer = d[-64:]
    return {"desc": desc, "raw": bytes(raw), "footer": footer, "chunks": chunks, "file": d}


def md5_ok(d):
    f = bytearray(d[-64:]); want = bytes(f[4:20]); f[4:20] = bytes(16)
    return hashlib.md5(d[:-64] + bytes(f)).digest() == want


def write(path, desc, raw, footer):
    out = bytearray(struct.pack("<iI", -1, len(desc)) + desc)
    for i in range(0, len(raw), CHUNK):
        blk = raw[i:i + CHUNK]
        z = zlib.compress(blk, 9)
        out += struct.pack("<II", len(z), len(blk)) + z
    f = bytearray(footer); f[4:20] = bytes(16)
    f[4:20] = hashlib.md5(bytes(out) + bytes(f)).digest()
    out += f
    open(path, "wb").write(out)
    return bytes(out)


def records(raw, start, end):
    """the records directly in raw[start:end], or None if it isn't exactly a run of records"""
    out, o = [], start
    while o < end:
        if o + 6 > end:
            return None
        tag, ln = struct.unpack_from("<HI", raw, o)
        if o + 6 + ln > end:
            return None
        out.append((o, tag, ln))
        o += 6 + ln
    return out


def path_to(raw, pos):
    """the chain of records (offset, tag, length) from the top down to the deepest one holding byte pos"""
    chain, start, end = [], 4, len(raw) - 1
    while True:
        recs = records(raw, start, end)
        if not recs:
            return chain
        hit = next(((o, t, l) for o, t, l in recs if o <= pos < o + 6 + l), None)
        if hit is None:
            return chain
        chain.append(hit)
        o, t, l = hit
        if pos < o + 6:
            return chain
        start, end = o + 6, o + 6 + l


if __name__ == "__main__":
    cmd, path = sys.argv[1], sys.argv[2]
    s = read(path)
    if cmd == "info":
        print(f"{len(s['file'])} bytes, description {len(s['desc'])} B, {s['chunks']} zlib chunks -> {len(s['raw'])} B raw, "
              f"footer {s['footer'][:4]!r}, MD5 {'ok' if md5_ok(s['file']) else 'BAD'}")
        top = records(s["raw"], 4, len(s["raw"]) - 1)
        print("top-level records:", [(o, hex(t), l) for o, t, l in top] if top else "none")
    elif cmd == "roundtrip":
        out = write(sys.argv[3], s["desc"], s["raw"], s["footer"])
        back = read(sys.argv[3])
        print("raw identical:", back["raw"] == s["raw"], "| MD5 ok:", md5_ok(out), "| size", len(s["file"]), "->", len(out))
    elif cmd == "find":
        q = sys.argv[3]
        b = uuid.UUID(q).bytes_le if "-" in q else bytes.fromhex(q)
        raw, i = s["raw"], s["raw"].find(b)
        while i >= 0:
            print(i, " > ".join(f"{t:#06x}[{l}]@{o}" for o, t, l in path_to(raw, i)))
            i = raw.find(b, i + 1)
