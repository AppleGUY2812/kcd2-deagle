#!/usr/bin/env python3
"""Find Asura property tables by hash pattern. SEV2 looks a property up by h = seed*31 + c per character of the lowercased
name ('\\' -> '/'), seed = the owning object's hash. Without knowing the seed, two names a, b of one object satisfy
k_b = (k_a - H0(a)) * 31^(len(b) - len(a)) + H0(b)  (H0 = the hash from seed 0), so look for u32 pairs that do.
usage: propfind.py <file.asr> <name_a> <name_b> [window_words=24]   prints seed, offsets and the float after each key"""
import sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from asr_unpack import load

M = 0xFFFFFFFF
def h0(s):
    h = 0
    for c in s.lower().replace("\\", "/").encode():
        h = (h * 31 + c) & M
    return h
def inv31():
    return pow(31, -1, 1 << 32)

def main():
    blob = load(sys.argv[1])
    a, b = sys.argv[2], sys.argv[3]
    win = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    la, lb = len(a), len(b)
    mul = pow(31, lb - la, 1 << 32) if lb >= la else pow(inv31(), la - lb, 1 << 32)
    ha, hb = h0(a), h0(b)
    out = []
    for al in range(4):
        n = (len(blob) - al) // 4
        arr = np.frombuffer(blob, dtype="<u4", count=n, offset=al)
        exp = ((arr - np.uint32(ha)) * np.uint32(mul) + np.uint32(hb)).astype(np.uint32)
        for d in range(-win, win + 1):
            if d == 0: continue
            lo, hi = max(0, -d), min(n, n - d)
            idx = np.nonzero(arr[lo + d: hi + d] == exp[lo:hi])[0]
            for i in idx[:50]:
                p = lo + int(i)
                ka = int(arr[p])
                seed = ((ka - ha) & M) * pow(inv31(), la, 1 << 32) & M
                out.append((al + 4 * p, d, seed))
    print(f"{len(out)} candidate pairs")
    for off, d, seed in out[:20]:
        fa = np.frombuffer(blob, dtype="<f4", count=1, offset=off + 4)[0] if off + 8 <= len(blob) else None
        fb = np.frombuffer(blob, dtype="<f4", count=1, offset=off + 4 * d + 4)[0] if off + 4 * d + 8 <= len(blob) else None
        print(f"  at 0x{off:08x} (b {d:+d} words)  seed 0x{seed:08x}  next float a={fa}  b={fb}")

if __name__ == "__main__":
    main()
