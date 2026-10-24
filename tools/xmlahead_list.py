#!/usr/bin/env python3
"""Make deagle_xmlahead_list.txt (native/src/xmlahead.cpp: the XML files to parse ahead, in the order the load asks).

usage: tools/xmlahead_list.py <deagle_xmlahead_trace.csv> <out list>
   or: tools/xmlahead_list.py --from-paks <deagle_pakahead_trace.csv> <out list> [--from-ms N] [--until-ms N]

The first form keeps the files the game loaded through LoadXmlFromFile (ok=1, first load only, time order). The second
guesses from a pak recording: whole reads of .xml entries, the game path being the pak's mount folder + the entry
name (Data/*.pak and Mods/*/Data/*.pak mount at the game root, Data/Levels/<level>/*.pak at levels/<level>/; other paks
are left out). A wrong guess costs a parse nobody uses, never a wrong file: the plugin serves by exact path only.
"""
import csv, os, struct, sys


def wsl(p):
    return '/mnt/' + p[0].lower() + p[2:].replace('\\', '/') if len(p) > 2 and p[1] == ':' else p


def entries(path):
    """data offset -> entry name"""
    with open(path, 'rb') as f:
        f.seek(0, 2); n = f.tell(); t = min(n, 65557); f.seek(n - t); b = f.read(t)
        e = b.rfind(b'PK\x05\x06')
        cnt, cs, co = struct.unpack_from('<HII', b, e + 10)
        f.seek(co); cd = f.read(cs); out = {}; p = 0
        for _ in range(cnt):
            if cd[p:p + 4] != b'PK\x01\x02':
                break
            nl, xl, cl = struct.unpack_from('<HHH', cd, p + 28)
            lho, = struct.unpack_from('<I', cd, p + 42)
            name = cd[p + 46:p + 46 + nl].decode('latin1')
            p += 46 + nl + xl + cl
            f.seek(lho); lh = f.read(30)
            a, bb = struct.unpack_from('<HH', lh, 26)
            out[lho + 30 + a + bb] = name
        return out


def mount(pak):
    q = pak.replace('\\', '/').lower()
    i = q.find('/kingdomcomedeliverance2/')
    rel = q[i + len('/kingdomcomedeliverance2/'):] if i >= 0 else None
    if rel is None:
        return '' if '/workshop/content/' in q and '/data/' in q else None   # workshop mods: <id>/Data/x.pak
    parts = rel.split('/')
    if parts[0] == 'data' and len(parts) == 2:
        return ''
    if parts[0] == 'data' and parts[1] == 'levels' and len(parts) == 4:
        return 'levels/' + parts[2] + '/'
    if parts[0] == 'mods' and len(parts) == 4 and parts[2] == 'data':
        return ''
    return None


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return 1
    files = []
    if a[0] != '--from-paks':
        with open(a[0], newline='') as f:
            for r in csv.DictReader(f):
                if r['ok'] == '1':
                    files.append((int(r['t_ms']), r['file']))
        out = a[1]
    else:
        src, out = a[1], a[2]
        lo = int(a[a.index('--from-ms') + 1]) if '--from-ms' in a else 0
        hi = int(a[a.index('--until-ms') + 1]) if '--until-ms' in a else 1 << 40
        paks, body = {}, []
        for l in open(src).read().splitlines():
            if l.startswith('#'):
                if ',' in l:
                    i, p = l[1:].split(',', 1)
                    if i.isdigit():
                        paks[int(i)] = p
            else:
                body.append(l)
        dirs = {}
        for r in csv.DictReader(body):
            t = int(r['t_ms'])
            if t < lo or t > hi or r['part'] != '0':
                continue
            pak = paks.get(int(r['pak']), '?')
            root = mount(pak) if not pak.startswith('mem') else None
            if root is None:
                continue
            if pak not in dirs:
                try:
                    dirs[pak] = entries(wsl(pak))
                except OSError:
                    dirs[pak] = {}
            name = dirs[pak].get(int(r['data_off']))
            if name and name.lower().endswith('.xml'):
                files.append((t, root + name))
    seen, keep = set(), []
    for t, f in sorted(files):
        k = f.replace('\\', '/').lower()
        if k in seen or k.startswith('%user%'):
            continue
        seen.add(k); keep.append(k)
    with open(out, 'w', newline='\n') as f:
        f.write('# made by tools/xmlahead_list.py\n')
        f.write('\n'.join(keep) + '\n')
    print(f'{len(keep)} files -> {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
