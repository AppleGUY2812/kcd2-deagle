#!/usr/bin/env python3
"""Sniper Elite V2 Remastered .asr archives (Rebellion "AsuraZlb"): 8-byte magic, u32 0, u32 compressed size,
u32 uncompressed size, then one zlib stream. Private-use reference for the kill-cam work (docs/se2_killcam.md).
usage: asr_unpack.py <file.asr> <out.bin>            unpack to a flat blob
       asr_unpack.py <file.asr> --find <text> [n]    offsets and a short ascii window of each match (n <= 10)"""
import struct, sys, zlib

def load(path):
    d = open(path, "rb").read()
    if d[:8] != b"AsuraZlb":
        raise SystemExit(f"{path}: not an AsuraZlb archive")
    comp, uncomp = struct.unpack_from("<II", d, 12)
    out = zlib.decompress(d[20:])
    if len(out) != uncomp:
        raise SystemExit(f"{path}: size {len(out)} != header {uncomp}")
    return out

def window(b, at, before=96, after=160):
    s = b[max(0, at - before): at + after]
    return "".join(chr(c) if 32 <= c < 127 else "." for c in s)

if __name__ == "__main__":
    blob = load(sys.argv[1])
    if sys.argv[2] == "--find":
        needle, n = sys.argv[3].encode(), int(sys.argv[4]) if len(sys.argv) > 4 else 3
        at, shown = blob.find(needle), 0
        while at >= 0 and shown < n:
            print(f"0x{at:08x}  {window(blob, at)}")
            at, shown = blob.find(needle, at + 1), shown + 1
    else:
        open(sys.argv[2], "wb").write(blob)
