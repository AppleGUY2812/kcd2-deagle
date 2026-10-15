#!/usr/bin/env python3
"""Sniper Elite V2 kill-cam parameter sets from misc/Common.asr (docs/se2_killcam.md).
A property record is [hash u32][3][0][0][1][float]. hash = seed*31^n + H0(name), over the lowercased name; the seed
belongs to one camera. Seeds are found from records whose keys relate like two property names of one camera, then
every kill-cam property is looked up for each seed.
usage: killcams.py <Common.asr> [--csv]"""
import struct, sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from asr_unpack import load
from propfind import h0

M = 0xFFFFFFFF
PROPS = ["Camera.Offset.Initial", "Camera.Offset.Final", "Camera.Angle.Initial", "Camera.Angle.Final",
         "Camera.FieldOfView.Initial", "Camera.FieldOfView.Final", "Camera.LookAtBullet", "Camera.TrackBullet",
         "TimeScale.PreHit", "TimeScale.Internal", "TimeScale.PostExit", "Duration", "Duration.ImpactTime",
         "Duration.ExitTime"]
SEED_PAIRS = [("Camera.Offset.Initial", "Camera.Offset.Final"), ("Camera.FieldOfView.Initial", "Camera.FieldOfView.Final"),
              ("TimeScale.PreHit", "TimeScale.Internal"), ("Camera.Angle.Initial", "Camera.Angle.Final")]

def key(seed, name):
    return (seed * pow(31, len(name), 1 << 32) + h0(name)) & M

def seeds_from(blob, a, b, win=30):
    la, lb = len(a), len(b)
    mul = pow(31, lb - la, 1 << 32) if lb >= la else pow(pow(31, -1, 1 << 32), la - lb, 1 << 32)
    ha, hb = h0(a), h0(b)
    inv_la = pow(pow(31, la, 1 << 32), -1, 1 << 32)
    found = set()
    n = len(blob) // 4
    arr = np.frombuffer(blob, dtype="<u4", count=n)               # 4-byte aligned, as the records are
    exp = ((arr - np.uint32(ha)) * np.uint32(mul) + np.uint32(hb)).astype(np.uint32)
    for d in range(-win, win + 1):
        if d == 0: continue
        lo, hi = max(0, -d), min(n, n - d)
        for i in np.nonzero(arr[lo + d: hi + d] == exp[lo:hi])[0]:
            p = lo + int(i)
            if p + 6 <= n and arr[p + 1] == 3 and arr[p + 2] == 0 and arr[p + 3] == 0 and arr[p + 4] == 1:   # a float record
                found.add(((int(arr[p]) - ha) & M) * inv_la & M)
    return found

def value(blob, k):
    at = blob.find(struct.pack("<I4I", k, 3, 0, 0, 1))
    while at >= 0 and at % 4:
        at = blob.find(struct.pack("<I4I", k, 3, 0, 0, 1), at + 1)
    return struct.unpack_from("<f", blob, at + 20)[0] if at >= 0 else None

def main():
    blob = load(sys.argv[1])
    seeds = set()
    for a, b in SEED_PAIRS:
        seeds |= seeds_from(blob, a, b)
    print(f"{len(seeds)} candidate camera seeds", file=sys.stderr)
    rows = []
    for s in sorted(seeds):
        vals = [value(blob, key(s, n)) for n in PROPS]
        got = sum(v is not None for v in vals)
        if got >= 3:
            rows.append((got, s, vals))
    rows.sort(reverse=True)
    short = [n.replace("Camera.", "").replace("FieldOfView", "FOV").replace("TimeScale.", "TS.") for n in PROPS]
    print("seed      " + " ".join(f"{n[:11]:>11s}" for n in short))
    for got, s, vals in rows:
        print(f"{s:08x}  " + " ".join(f"{v:11.4g}" if v is not None else f"{'-':>11s}" for v in vals))

if __name__ == "__main__":
    main()
