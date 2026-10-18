# anim_dump.py <skeleton package> <out.json> <anim package>...: decode Ready or Not AnimSequences: ACL ones (the DLC3
# Grizzly's) with acl_dump.exe (tools/ron/acl_dump, the ACL 2.1 library), the others with animdec.py
# (AnimCompress_PerTrackCompression). CUE4Parse 1.2.2 can't read the DLC3 Grizzly's clips: "Unsupported
# compressed data type", and its weapon clips' skeleton doesn't resolve) against a skeleton's bones, into one JSON:
#   {"skeleton": {"bones": [[name, parent]], "pose": [[q xyzw, t cm, s]]},
#    "clips": {name: {"length", "fps", "frames", "additive", "tracks": {bone: {"T": [...], "R": [...], "S": [...]}}}}}
# Packages are paths under dump.OUT (extract them first with ronx.py). Read-only. Private use: the output stays in
# E:/Tools/deagle-work, never in the repo.
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from usmap import U
from uasset import Pkg, R
from dump import OUT, USMAP, ensure
import animdec
import struct, subprocess, tempfile

u = U(USMAP)
ACL_DUMP = os.environ.get('ACL_DUMP', '/mnt/e/Tools/deagle-work/acl_src/build/acl_dump.exe')   # tools/ron/acl_dump
ACL_TAG = bytes.fromhex('11ac11ac')   # acl::compressed_tracks' tag (after the raw buffer header: size, hash)


def decode_acl(base, bones):
    """an ACL-compressed sequence (Unreal's ACL plugin): its compressed_tracks buffer through acl_dump.exe (ACL 2.1),
    track i -> bone through CompressedTrackToSkeletonMapTable (read as animdec.decode does)"""
    p = Pkg(base, u); e = p.exports[0]; d = p.data(e); r = R(d)
    try:
        pr = p.props(r, 'AnimSequence'); start = r.o
    except IndexError:   # the fire clips' properties hold an object our reader can't resolve: the length and rate
        pr, start = {'unreadable': True}, None   # then come from the ACL header, the map table from the search below
    i = d.find(ACL_TAG)
    size, = struct.unpack_from('<I', d, i - 8)
    nacl, = struct.unpack_from('<I', d, i + 8)   # tracks_header after the tag: version, algorithm, track type, num tracks
    ntr = -1
    if start is not None:
        o = start + 4 + 16 + 2
        _, _, ntr = struct.unpack_from('<iii', d, o); o += 12
    if ntr != nacl:   # clips with curves lay the header out differently: find the map table itself, an int32 = the
        # track count followed by that many bone indices, before the ACL buffer
        o = next(k + 4 for k in range(i - 4 * nacl - 4, -1, -1)   # the nearest one before the buffer
                 if struct.unpack_from('<i', d, k)[0] == nacl
                 and all(0 <= x < len(bones) for x in struct.unpack_from('<%di' % nacl, d, k + 4)))
        ntr = nacl
    tmap = struct.unpack_from('<%di' % ntr, d, o)
    with tempfile.TemporaryDirectory(dir='/mnt/e/Tools/deagle-work/ron/grizzly') as tmp:
        src, out = os.path.join(tmp, 'c.acl'), os.path.join(tmp, 'c.bin')
        open(src, 'wb').write(d[i - 8:i - 8 + size])
        win = lambda x: subprocess.run(['wslpath', '-w', x], capture_output=True, text=True).stdout.strip()
        subprocess.run([ACL_DUMP, win(src), win(out)], check=True, capture_output=True, cwd='/mnt/e')
        b = open(out, 'rb').read()
    nt, ns, rate, dur = struct.unpack_from('<IIff', b, 0)
    assert nt == ntr, f'{nt} ACL tracks, {ntr} in the map table'
    f = struct.unpack_from('<%df' % (nt * ns * 10), b, 16)
    res = {}
    for t in range(nt):
        at = lambda k: [f[(k * nt + t) * 10 + c] for c in range(10)]
        res[bones[tmap[t]][0]] = {'R': [at(k)[0:4] for k in range(ns)], 'T': [at(k)[4:7] for k in range(ns)],
                                  'S': [at(k)[7:10] for k in range(ns)]}
    fps = (pr.get('TargetFrameRate') or {}).get('Numerator', rate)
    add = pr.get('AdditiveAnimType', 'unreadable' if pr.get('unreadable') else None)
    length = pr.get('SequenceLength')
    if not isinstance(length, (int, float)) or length != length: length = dur   # missing or NaN: the ACL duration
    return res, length, fps, ns, add


def skeleton(pkg):
    base = OUT + '/' + pkg
    assert ensure(base), 'cannot extract ' + base
    p = Pkg(base, u)
    e = [x for x in p.exports if p.ref(x['cls']) == 'Skeleton'][0]
    r = R(p.data(e)); p.props(r, 'Skeleton'); r.u('i')
    bones = [(p.fn(r), r.u('i')) for _ in range(r.u('i'))]   # (name, parent index)
    pose = []
    for _ in range(r.u('i')):
        q = r.u('dddd'); t = r.u('ddd'); s = r.u('ddd'); pose.append((q, t, s))   # rotation xyzw, translation cm, scale
    return bones, pose


def main():
    skel_pkg, out, anims = sys.argv[1], sys.argv[2], sys.argv[3:]
    bones, pose = skeleton(skel_pkg)
    clips, failed = {}, {}
    for a in anims:
        name = a.split('/')[-1]
        try:
            base = OUT + '/' + a
            acl = ACL_TAG in open(base + '.uexp', 'rb').read()
            res, length, fps, nframes, add = (decode_acl if acl else lambda b, bn: animdec.decode(b, u, bn))(base, bones)
            clips[name] = {'length': length, 'fps': fps, 'frames': nframes, 'additive': add, 'codec': 'acl' if acl else 'ue', 'tracks': res}
        except Exception as x:
            failed[name] = repr(x)
    json.dump({'skeleton': {'bones': bones, 'pose': pose}, 'clips': clips, 'failed': failed}, open(out, 'w'))
    print(f'{len(bones)} bones; {len(clips)} clips decoded, {len(failed)} failed -> {out}')
    for k, v in failed.items():
        print('  FAILED', k, v[:160])


if __name__ == '__main__':
    main()
