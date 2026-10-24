#!/usr/bin/env python3
"""Make fastload_pakahead_list.txt (the plugin's unpack-ahead list, native/src/pakahead.cpp) from a recording.

usage: tools/pakahead_list.py <fastload_pakahead_trace.csv> <out list> [--from-ms N] [--until-ms N]
(t_ms counts from the plugin's start; the level load is the window to keep, boot reads come before the menu anyway)

Keeps each entry the game read whole and unpacked (or stored), first read only, in time order. Every entry is checked
against its pak's own zip directory: an entry whose data offset, sizes and crc32 don't match one directory entry is
left out and counted, so a wrong recording can't put wrong bytes in the list.
"""
import csv, struct, sys, os, collections


def winpath_to_wsl(p):
    if len(p) > 2 and p[1] == ':':
        return '/mnt/' + p[0].lower() + p[2:].replace('\\', '/')
    return p


def zip_dir(path):
    """data offset -> (crc, packed, size, method) for every entry of a zip/pak."""
    with open(path, 'rb') as f:
        f.seek(0, 2)
        n = f.tell()
        tail = min(n, 65557)
        f.seek(n - tail)
        t = f.read(tail)
        e = t.rfind(b'PK\x05\x06')
        if e < 0:
            raise ValueError('no zip directory')
        count, cd_size, cd_off = struct.unpack_from('<HII', t, e + 10)
        f.seek(cd_off)
        cd = f.read(cd_size)
        out = {}
        p = 0
        for _ in range(count):
            if cd[p:p + 4] != b'PK\x01\x02':
                break
            method, = struct.unpack_from('<H', cd, p + 10)
            crc, cs, us = struct.unpack_from('<III', cd, p + 16)
            nl, xl, cl = struct.unpack_from('<HHH', cd, p + 28)
            lho, = struct.unpack_from('<I', cd, p + 42)
            p += 46 + nl + xl + cl
            f.seek(lho)
            lh = f.read(30)
            if lh[:4] != b'PK\x03\x04':
                continue
            lnl, lxl = struct.unpack_from('<HH', lh, 26)
            out[lho + 30 + lnl + lxl] = (crc, cs, us, method)
        return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    until = None
    start = 0
    if '--from-ms' in sys.argv:
        start = int(sys.argv[sys.argv.index('--from-ms') + 1])
    if '--until-ms' in sys.argv:
        until = int(sys.argv[sys.argv.index('--until-ms') + 1])
    paks = {}
    rows = []
    with open(sys.argv[1], newline='') as f:
        for line in f:
            if line.startswith('#'):
                if ',' in line:
                    i, path = line[1:].rstrip('\r\n').split(',', 1)
                    if i.isdigit():
                        paks[int(i)] = path
                continue
            break
        rd = csv.DictReader(f, fieldnames=line.strip().split(','))
        rows = list(rd)
    seen = set()
    dirs = {}
    kept = []
    why = collections.Counter()
    for r in sorted(rows, key=lambda r: int(r['t_ms'])):
        if until is not None and int(r['t_ms']) > until:
            break
        if int(r['t_ms']) < start:
            continue
        pak = paks.get(int(r['pak']), '?')
        key = (pak.lower(), int(r['data_off']))
        if key in seen:
            why['read again'] += 1
            continue
        seen.add(key)
        if r['decompress'] == '2':
            why['was served'] += 1
        if pak.startswith('mem:') or pak == '?':
            why['pak in memory / unnamed'] += 1
            continue
        if r['part'] != '0':
            why['part of an entry'] += 1
            continue
        method = int(r['method'])
        if r['decompress'] == '0' and method != 0:
            why['packed bytes wanted'] += 1
            continue
        if int(r['crc'], 16) == 0:
            why['crc already cleared'] += 1
            continue
        if pak not in dirs:
            try:
                dirs[pak] = zip_dir(winpath_to_wsl(pak))
            except Exception as ex:
                print(f'cannot read {pak}: {ex}')
                dirs[pak] = {}
        d = dirs[pak].get(int(r['data_off']))
        want = (int(r['crc'], 16), int(r['csize']), int(r['usize']))
        if not d or d[:3] != want:
            why['no match in the pak directory'] += 1
            continue
        if (method == 0) != (d[1] == d[2] and d[3] == 0):
            why['method mismatch'] += 1
            continue
        kept.append((pak, int(r['data_off']), want[1], want[2], want[0]))
    with open(sys.argv[2], 'w', newline='\n') as f:
        f.write('# made by tools/pakahead_list.py from %s\n' % os.path.basename(sys.argv[1]))
        for k in kept:
            f.write('%s\t%u\t%u\t%u\t%08x\n' % k)
    mb = sum(k[3] for k in kept) / 1e6
    print(f'{len(rows)} reads -> {len(kept)} entries, {mb:.0f} MB unpacked, from {len(set(k[0] for k in kept))} paks')
    for w, n in why.most_common():
        print(f'  left out / noted: {w}: {n}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
