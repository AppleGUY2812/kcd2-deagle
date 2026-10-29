#!/usr/bin/env python3
"""Compare a built gun's compiled skeleton with the game's own objects/characters/humans/male/skeleton/male.chr.

Both files are read as compiled .chr bytes (CompiledBones chunk 0x2000: name at 312, DefaultB2W 3x4 at 264, stride
584). This is the skeleton the game actually loads; the glb binds are not enough (they lack the toolkit's 180 deg yaw).

usage: python3 tools/armswap/check_chr.py [our .chr] [--quiet]
  default: src/Objects/manmade/weapons/guns/ron_grizzly_henry/ron_grizzly_henry_vm.chr
Missing game male.chr or our .chr = SKIP, exit 0.
"""
import os, struct, sys
import numpy as np
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT = os.path.join(REPO, 'src', 'Objects', 'manmade', 'weapons', 'guns', 'ron_grizzly_henry',
                       'ron_grizzly_henry_vm.chr')
GAME_DIRS = ['/mnt/d/steam_bruh/steamapps/common/KingdomComeDeliverance2/Data',
             '/mnt/e/SteamLibrary/steamapps/common/KingdomComeDeliverance2/Data']
ENTRY = 'objects/characters/humans/male/skeleton/male.chr'
POS_TOL, ROT_TOL = 0.5, 0.1


def chr_bones(data):
    _, num, toff = struct.unpack_from('<III', data, 4)
    c = next(data[off:off + size] for t, _, _, size, off in
             (struct.unpack_from('<HHIII', data, toff + i * 16) for i in range(num)) if t == 0x2000)
    out = {}
    for i in range((len(c) - 32) // 584):
        b = c[32 + i * 584: 32 + (i + 1) * 584]
        name = b[312:568].split(b'\0')[0].decode()
        r = struct.unpack_from('<12f', b, 264)
        out[name] = np.array([r[0:4], r[4:8], r[8:12], [0, 0, 0, 1]], float)
    return out


def game_chr():
    for d in GAME_DIRS:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith('.pak'):
                continue
            try:
                with zipfile.ZipFile(os.path.join(d, f)) as z:
                    hit = next((n for n in z.namelist() if n.replace('\\', '/').lower() == ENTRY), None)
                    if hit:
                        return chr_bones(z.open(hit).read())
            except Exception:
                pass
    return None


def main():
    ours = next((a for a in sys.argv[1:] if not a.startswith('-')), DEFAULT)
    quiet = '--quiet' in sys.argv
    if not os.path.exists(ours):
        print('SKIP check_chr: no built .chr at', ours)
        return 0
    G = game_chr()
    if G is None:
        print('SKIP check_chr: game male.chr not found in', GAME_DIRS)
        return 0
    D = chr_bones(open(ours, 'rb').read())
    rows, missing = [], []
    for n, g in G.items():
        if n not in D:
            missing.append(n)
            continue
        err = D[n] @ np.linalg.inv(g)
        pos = float(np.linalg.norm(D[n][:3, 3] - g[:3, 3])) * 1000.0
        r = err[:3, :3]
        det = float(np.linalg.det(r))
        if det > 0:
            r = r / (det ** (1.0 / 3.0))
        rot = float(np.degrees(np.arccos(np.clip((np.trace(r) - 1) / 2, -1, 1))))
        rows.append((pos, rot, n))
    rows.sort(key=lambda r: r[2].lower())
    if not quiet:
        for pos, rot, n in rows:
            print('%-24s pos %9.3f mm   rot %8.3f deg' % (n, pos, rot))
    max_pos = max(((r[0], r[2]) for r in rows), default=(0, ''))
    max_rot = max(((r[1], r[2]) for r in rows), default=(0, ''))
    print('check_chr %s: %d shared joints, max pos %.3f mm (%s), max rot %.3f deg (%s), missing %d' % (
        os.path.basename(ours), len(rows), max_pos[0], max_pos[1], max_rot[0], max_rot[1], len(missing)))
    if missing:
        print('game bones not in our .chr (fine: gun skeleton is a subset): %d, e.g. %s' % (
            len(missing), ', '.join(sorted(missing)[:6])))
    return 0 if max_pos[0] <= POS_TOL and max_rot[0] <= ROT_TOL else 1


if __name__ == '__main__':
    sys.exit(main())
