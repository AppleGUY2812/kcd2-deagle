#!/usr/bin/env python3
# Compare two quest graph dumps written by native/src/questdump.cpp (one block per project: "project ..." to "end ... #hash").
# usage: python3 tools/questdump_diff.py A.txt B.txt [--lines 40] [--verify]
# Prints EQUAL or DIFFERENT, projects only in one file, whether the project order differs, every differing project,
# and for the first one the first --lines differing lines with their object paths. Projects are matched by path (and
# by occurrence, when one file holds several builds). Exit code: 0 equal, 1 different, 2 a file has no projects.
# Block hashes are Merkle FNV-1a 64: a block hashes its own lines, a nested block counts as its opening line and its
# closing "} #hash" line. Equal hashes mean equal text. --verify recomputes every hash (slow on big files).
import argparse
import difflib
import sys

MASK = (1 << 64) - 1


def quoted(line, at):
    """The quoted string starting at line[at] (escapes kept)."""
    if line[at:at + 1] != b'"':
        return b'?'
    i = at + 1
    while i < len(line):
        if line[i:i + 1] == b'\\':
            i += 2
            continue
        if line[i:i + 1] == b'"':
            return line[at + 1:i]
        i += 1
    return line[at + 1:]


def scan(path):
    """Projects in file order: name, key (name, occurrence), header, end line, byte range, hash."""
    projects, cur, seen, off = [], None, {}, 0
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.rstrip(b'\r\n')
            if line.startswith(b'project '):
                cur = {'name': quoted(line, 8), 'header': line, 'start': off}
            elif line.startswith(b'end ') and cur is not None:
                cur.update(end=line, stop=off + len(raw), hash=line.rsplit(b'#', 1)[-1])
                n = seen.get(cur['name'], 0)
                seen[cur['name']] = n + 1
                cur['key'] = (cur['name'], n)
                projects.append(cur)
                cur = None
            off += len(raw)
    if cur is not None:   # cut off (the game stopped mid-dump)
        cur.update(end=b'(no end line)', stop=off, hash=b'truncated')
        n = seen.get(cur['name'], 0)
        cur['key'] = (cur['name'], n)
        projects.append(cur)
    return projects


class Tree:
    """One project's lines with their block structure (by indentation)."""

    def __init__(self, path, p):
        with open(path, 'rb') as f:
            f.seek(p['start'])
            data = f.read(p['stop'] - p['start'])
        lines = data.split(b'\n')
        if lines and lines[-1] == b'':
            lines.pop()
        self.text = [l.lstrip(b' ') for l in lines]
        self.depth = [len(l) - len(t) for l, t in zip(lines, self.text)]
        self.kids, self.closer, self.top = {}, {}, []
        stack = []
        for i, (d, t) in enumerate(zip(self.depth, self.text)):
            closing = t.startswith(b'} #') or t.startswith(b'end ')
            consumed = False
            while stack and d <= self.depth[stack[-1]]:
                o = stack.pop()
                if d == self.depth[o] and closing:
                    self.closer[o] = i
                    consumed = True
                    break
            if consumed:
                continue
            (self.kids[stack[-1]] if stack else self.top).append(i)
            if t.endswith(b' {') or t.startswith(b'project '):
                stack.append(i)
                self.kids[i] = []

    def is_block(self, i):
        return i in self.kids

    def close_text(self, i):
        c = self.closer.get(i)
        return self.text[c] if c is not None else b'(unclosed)'

    def label(self, i):
        t = self.text[i]
        if t.startswith(b'["'):
            j = 2
            while j < len(t):
                if t[j:j + 1] == b'\\':
                    j += 2
                    continue
                if t[j:j + 2] == b'"]':
                    return t[:j + 2]
                j += 1
            return t
        k = t.find(b' = ')
        return t if k < 0 else t[:k]

    def value(self, i):
        t = self.text[i]
        return t[len(self.label(i)):]

    def signature(self, i):
        return self.value(i) + (b'|' + self.close_text(i) if self.is_block(i) else b'')

    def objpath(self, i):
        """The registered path in an opening line ("... @= path {"), else None."""
        t = self.text[i]
        k = t.find(b' @= ')
        return t[k + 4:-2] if k >= 0 and t.endswith(b' {') else None


def is_index(lab):
    return lab.startswith(b'[') and lab[1:-1].isdigit()


def compare(A, B, ia, ib, where, out, limit):
    ka, kb = A.kids.get(ia, []), B.kids.get(ib, [])
    la, lb = [A.label(j) for j in ka], [B.label(j) for j in kb]
    pairs = []
    if la and lb and all(is_index(x) for x in la + lb):   # a sequence: align by content, so an insert shows as one
        sm = difflib.SequenceMatcher(None, [A.signature(j) for j in ka], [B.signature(j) for j in kb], autojunk=False)
        for op, a0, a1, b0, b1 in sm.get_opcodes():
            if op == 'equal':
                continue
            for k in range(max(a1 - a0, b1 - b0)):
                pairs.append((ka[a0 + k] if a0 + k < a1 else None, kb[b0 + k] if b0 + k < b1 else None))
    else:
        pos = {x: j for x, j in zip(lb, kb)}
        used = set()
        for x, j in zip(la, ka):
            m = pos.get(x)
            if m is not None:
                used.add(m)
            pairs.append((j, m))
        pairs += [(None, j) for j in kb if j not in used]
        sa = set(la)
        if [x for x in la if x in pos] != [x for x in lb if x in sa]:
            out.append((where, b'(order of entries differs)', b'(order of entries differs)'))
    for ja, jb in pairs:
        if len(out) >= limit:
            return
        lab = A.label(ja) if ja is not None else B.label(jb)
        sub = where + (lab if lab.startswith(b'[') or not where else b'.' + lab)
        if ja is None or jb is None:
            out.append((sub, A.text[ja] if ja is not None else None, B.text[jb] if jb is not None else None))
            continue
        ba, bb = A.is_block(ja), B.is_block(jb)
        if ba and bb:
            if A.text[ja] == B.text[jb] and A.close_text(ja) == B.close_text(jb):
                continue
            if A.text[ja] != B.text[jb]:
                out.append((sub, A.text[ja], B.text[jb]))
            pa = A.objpath(ja)
            compare(A, B, ja, jb, pa if pa is not None and pa == B.objpath(jb) else sub, out, limit)
        elif A.text[ja] != B.text[jb] or ba != bb:
            out.append((sub, A.text[ja], B.text[jb]))


def fnv(h, s):
    for c in s:
        h = ((h ^ c) * 0x100000001b3) & MASK
    return ((h ^ 10) * 0x100000001b3) & MASK


def verify(T):
    """Recompute every block hash; returns the number of mismatches."""
    bad = 0

    def block(i):
        nonlocal bad
        h = fnv(0xcbf29ce484222325, T.text[i]) if T.text[i].startswith(b'project ') else 0xcbf29ce484222325
        for j in T.kids[i]:
            h = fnv(h, T.text[j])
            if T.is_block(j):
                hj = block(j)
                c = T.closer.get(j)
                if c is not None:
                    if T.text[c] != b'} #%016x' % hj:
                        bad += 1
                    h = fnv(h, T.text[c])
        return h

    for i in T.top:
        if T.text[i].startswith(b'project '):
            h = block(i)
            c = T.closer.get(i)
            if c is None or not T.text[c].endswith(b'#%016x' % h):
                bad += 1
    return bad


def show(b):
    return b.decode('utf-8', 'replace') if b is not None else '(missing)'


def main():
    ap = argparse.ArgumentParser(description='Compare two quest graph dumps (questdump.cpp).')
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--lines', type=int, default=40, help='differing lines to print for the first differing project')
    ap.add_argument('--verify', action='store_true', help='recompute every block hash (slow)')
    args = ap.parse_args()
    pa, pb = scan(args.a), scan(args.b)
    print('A: %s: %d projects' % (args.a, len(pa)))
    print('B: %s: %d projects' % (args.b, len(pb)))
    if not pa or not pb:
        print('no projects in %s' % (args.a if not pa else args.b))
        return 2
    if args.verify:
        for name, path, ps in (('A', args.a, pa), ('B', args.b, pb)):
            bad = sum(verify(Tree(path, p)) for p in ps)
            print('%s: hash check: %s' % (name, 'all hashes match' if not bad else '%d MISMATCHES (file damaged or capped)' % bad))
    ma, mb = {p['key']: p for p in pa}, {p['key']: p for p in pb}
    only_a = [p['key'] for p in pa if p['key'] not in mb]
    only_b = [p['key'] for p in pb if p['key'] not in ma]
    common = [p['key'] for p in pa if p['key'] in mb]
    order_same = common == [p['key'] for p in pb if p['key'] in ma]
    differ = [k for k in common if ma[k]['hash'] != mb[k]['hash'] or ma[k]['header'] != mb[k]['header']]
    for k in only_a:
        print('only in A: %s (occurrence %d)' % (show(k[0]), k[1]))
    for k in only_b:
        print('only in B: %s (occurrence %d)' % (show(k[0]), k[1]))
    if not order_same:
        print('project order differs')
    for k in differ:
        print('differs: %s (occurrence %d): A #%s, B #%s' % (show(k[0]), k[1], show(ma[k]['hash']), show(mb[k]['hash'])))
    if not only_a and not only_b and order_same and not differ:
        print('EQUAL: %d projects' % len(common))
        return 0
    print('DIFFERENT: %d of %d common projects differ, %d only in A, %d only in B%s' % (
        len(differ), len(common), len(only_a), len(only_b), '' if order_same else ', order differs'))
    if differ:
        k = differ[0]
        A, B = Tree(args.a, ma[k]), Tree(args.b, mb[k])
        print('\nfirst differing project: %s' % show(k[0]))
        if ma[k]['header'] != mb[k]['header']:
            print('  - %s\n  + %s' % (show(ma[k]['header']), show(mb[k]['header'])))
        out = []
        ra = [i for i in A.top if A.text[i].startswith(b'project ')]
        rb = [i for i in B.top if B.text[i].startswith(b'project ')]
        if ra and rb:
            compare(A, B, ra[0], rb[0], b'', out, args.lines)
        for where, la, lb in out:
            print('@ %s\n  - %s\n  + %s' % (show(where or b'(project)'), show(la), show(lb)))
        if len(out) >= args.lines:
            print('(first %d differences only: --lines N for more)' % args.lines)
    return 1


if __name__ == '__main__':
    sys.exit(main())
