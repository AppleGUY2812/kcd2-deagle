#!/usr/bin/env python3
"""Every .chr and .skin under src/Objects must have case-insensitively unique joint names, and every gun skin's
joints must exist in its gun's .chr. CryEngine hashes joint names case-insensitively: 'Head' and 'head' in one file
is a duplicated CRC32 at load; a skin carrying a joint the .chr lacks is also a load error.

usage: python3 tests/check_joint_names.py
"""
import os, struct, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(REPO, "src", "Objects")
BONE = 0x2000


def chunks(path):
    d = open(path, "rb").read()
    _, num, toff = struct.unpack_from("<III", d, 4)
    out = []
    for i in range(num):
        t, _, _, size, off = struct.unpack_from("<HHIII", d, toff + i * 16)
        out.append((t, size, off))
    return d, out


def joints(path):
    d, cs = chunks(path)
    c = next(d[off:off + size] for t, size, off in cs if t == BONE)
    return [c[32 + i * 584 + 312: 32 + i * 584 + 568].split(b"\0")[0].decode()
            for i in range((len(c) - 32) // 584)]


def gun_chr(skin):
    d = os.path.dirname(os.path.abspath(skin))
    root = os.path.abspath(REPO)
    while os.path.abspath(d).startswith(root):
        hits = [f for f in os.listdir(d) if f.lower().endswith(".chr")]
        if hits:
            return os.path.join(d, sorted(hits)[0])
        d = os.path.dirname(d)
    return None


def main():
    bad, checked = [], 0
    for base, _, files in os.walk(ROOT):
        for f in sorted(files):
            if not f.lower().endswith((".chr", ".skin")):
                continue
            p = os.path.join(base, f)
            checked += 1
            try:
                names = joints(p)
            except Exception as e:
                bad.append("%s: cannot read joints (%s)" % (p, e))
                continue
            fold = {}
            for n in names:
                k = n.casefold()
                if k in fold:
                    bad.append("%s: duplicate joint CRC32: %r and %r" % (p, fold[k], n))
                fold[k] = n
            if f.lower().endswith(".skin"):
                chr_path = gun_chr(p)
                if not chr_path:
                    continue            # a character clothing skin, not a gun skin
                try:
                    ref = set(n.casefold() for n in joints(chr_path))
                except Exception as e:
                    bad.append("%s: cannot read its .chr (%s)" % (chr_path, e))
                    continue
                extra = sorted(fold[k] for k in fold if k not in ref)
                if extra:
                    bad.append("%s: joints not in %s: %s" % (p, os.path.basename(chr_path), ", ".join(extra[:6])))
    for line in bad:
        print("FAIL " + line)
    print("joint names: %d files checked, %d problems" % (checked, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
