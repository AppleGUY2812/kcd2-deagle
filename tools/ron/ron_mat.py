#!/usr/bin/env python3
"""What a Ready or Not particle material draws, read from the package and its compiled pixel shaders.

    python3 -I tools/ron/ron_mat.py <material name> [...]    (MI_*, M_*: found in the pak file list)

For each material: the parent chain (material instance -> ... -> material), blend mode and overrides, the texture /
scalar / vector parameters set on the way, the base material's referenced textures in shader-slot order, and the
pixel shader of a Niagara-sprite permutation (the one that reads the particle colour as an interpolant), cut to the
lines that sample the material's textures and use the colour. Shaders: the material's shader map in
ShaderArchive-ReadyOrNot_Chunk0 (Oodle), disassembled by tools/ron/dxbc_dis.ps1 into
E:/Tools/deagle-work/ron/work/ngpu/mat_<base material>/ps_NNN.asm. Private use (Ready or Not assets).
"""
import ctypes, json, os, re, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ronpaths  # noqa: E402
from niagara_gpu import Archive, pkg_open, props, dis  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = os.path.join(os.path.dirname(ronpaths.OUT), 'ron_files.txt')
NGPU = os.path.join(os.path.dirname(ronpaths.OUT), 'ngpu')
ARCH = os.path.join(ronpaths.OUT, 'ReadyOrNot/Content/ShaderArchive-ReadyOrNot_Chunk0-PCD3D_SM5-PCD3D_SM5.ushaderbytecode')


def find(name):
    for line in open(FILES):
        line = line.strip()
        if line.endswith('/' + name + '.uasset'):
            return line.split('ReadyOrNot/Content/', 1)[1][:-len('.uasset')]
    raise SystemExit('not in the pak list: ' + name)


def open_mat(rel):
    base = os.path.join(ronpaths.OUT, 'ReadyOrNot/Content', rel)
    if not os.path.exists(base + '.uasset'):
        rx = re.escape(rel.replace('ReadyOrNot/', '', 1) if rel.startswith('ReadyOrNot/') else rel) + r'\.(uasset|uexp|ubulk)$'
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'ronx.py'), ronpaths.PAKS, ronpaths.OUT, ronpaths.OOZ, rx],
                       check=True, capture_output=True)
    return pkg_open(base)


def ref_textures(p):
    """the material's ReferencedTextures (shader slot order): int32 count, then that many import refs to textures"""
    blob = p.data(p.exports[0])
    tex = {-(k + 1): n for k, (c, n, o) in enumerate(p.imports) if c in ('Texture2D', 'TextureCube', 'Texture2DArray', 'VolumeTexture')}
    for o in range(0, min(len(blob) - 8, 4096)):
        n, = struct.unpack_from('<i', blob, o)
        if not 1 <= n <= 32: continue
        refs = struct.unpack_from('<%di' % n, blob, o + 4) if o + 4 + 4 * n <= len(blob) else ()
        if refs and all(r in tex for r in refs):
            return [tex[r] for r in refs]
    return []


def defaults(p, u):
    """a material's parameter defaults: UE 5.3 keeps them, unnamed, in FMaterialCachedExpressionData right after the
    material's properties (a bool, then the unversioned struct): RuntimeEntries[k] names, *Values the defaults"""
    from dump import class_schema
    from uasset import R
    e = p.exports[0]; nat, ex = class_schema(p, e['cls'], u); r = R(p.data(e)); p.props(r, nat, ex)
    c = p.props(R(p.data(e), r.o + 8), 'MaterialCachedExpressionData', None)
    names = [[x['Name'] for x in c.get('RuntimeEntries[%d]' % k, {}).get('ParameterInfoSet', [])] for k in range(8)]
    out = {}
    for k, key in ((0, 'ScalarValues'), (1, 'VectorValues'), (3, 'TextureValues')):
        out.update({n: (v.split('.')[-1] if isinstance(v, str) else v) for n, v in zip(names[k], c.get(key, []))})
    return out


def shaders(p, name):
    out = os.path.join(NGPU, 'mat_' + name)
    if os.path.isdir(out) and any(f.endswith('.asm') for f in os.listdir(out)):
        return out
    os.makedirs(out, exist_ok=True)
    a = Archive(ARCH)
    blob = p.data(p.exports[0]); m = None
    for k in range(len(blob) - 19):
        m = a.mapidx.get(blob[k:k + 20])
        if m is not None: break
    if m is None: raise SystemExit(name + ': no shader map found in the chunk 0 archive')
    off, num, _, _ = a.mapent[m]; files = []
    for n, j in enumerate(a.idx[off:off + num]):
        o, sz, usz, fr = a.ent[j]
        if fr != 3: continue   # pixel shaders
        a.f.seek(a.base + o); cb = a.f.read(sz)
        if sz < usz:
            buf = ctypes.create_string_buffer(usz + 64); a.ooz.ooz_decompress(cb, sz, buf, usz); cb = buf.raw[:usz]
        i = cb.find(b'DXBC'); L, = struct.unpack_from('<I', cb, i + 24)
        f = os.path.join(out, 'ps_%03d.dxbc' % n); open(f, 'wb').write(cb[i:i + L]); files.append(f)
    dis(files)
    return out


def sprite_ps(d, ntex):
    """the permutation for Niagara sprites: the most material-texture samples, a 4-wide colour interpolant used
    as a multiplier right after a sample; ties -> the shortest"""
    best = None
    for fn in sorted(os.listdir(d)):
        if not fn.endswith('.asm'): continue
        t = open(os.path.join(d, fn), errors='replace').read().replace('\r', '')
        ins = re.findall(r'dcl_input_ps\S* \S+ ?\S* (v\d+)\.xyzw', t)
        uses = [v for v in ins if re.search(r'(mul|mad)\S* r\d+\.\w+, r\d+\.\w+, %s\.\w+' % v, t)]
        s2 = len(re.findall(r'sample\S*\(texture2d\)', t))
        key = (bool(uses), s2, -len(t))
        if best is None or key > best[0]: best = (key, fn, t)
    return best


def main():
    for name in sys.argv[1:]:
        chain, params = [], {'tex': {}, 'scalar': {}, 'vector': {}}
        rel = find(name)
        while True:
            p, u = open_mat(rel)
            e = p.exports[0]; cls = p.ref(e['cls']); d = props(p, u, e)
            chain.append('%s (%s%s%s)' % (rel.split('/')[-1], cls, ', ' + str(d.get('BlendMode')) if d.get('BlendMode') else '',
                                         ', overrides ' + json.dumps(d['BasePropertyOverrides']) if d.get('BasePropertyOverrides') else ''))
            for k, kk in (('TextureParameterValues', 'tex'), ('ScalarParameterValues', 'scalar'), ('VectorParameterValues', 'vector')):
                for v in d.get(k, []) or []:
                    params[kk].setdefault(v['ParameterInfo']['Name'], v['ParameterValue'])
            if cls == 'Material': break
            parent = d.get('Parent')
            if not parent: break
            par_path = next((n for c, n, o in p.imports if n.endswith('/' + parent)), None)
            rel = par_path.split('/Game/', 1)[1] if par_path else find(parent)   # /Game/ = ReadyOrNot/Content/
        print('=' * 100)
        print(name, '->', ' -> '.join(chain))
        for kk, v in params.items():
            if v: print('  %s params: %s' % (kk, json.dumps(v, default=str)))
        if cls != 'Material': continue
        base = rel.split('/')[-1]
        rt = ref_textures(p)
        try: print('  base material defaults: %s' % json.dumps(defaults(p, u), default=str))
        except Exception as x: print('  base material defaults: unreadable (%r)' % x)
        print('  base material %s, shading %s, referenced textures (slot order): %s' % (base, d.get('ShadingModel', 'default lit'), rt))
        sd = shaders(p, base)
        best = sprite_ps(sd, len(rt))
        if not best: continue
        (has_col, ns, _), fn, t = best
        print('  sprite permutation %s (colour interpolant used: %s, %d texture samples)' % (fn, has_col, ns))
        L = t.split('\n')
        idx = [i for i, l in enumerate(L) if re.search(r'sample\S*\(texture2d\)|discard|^\s*\w+\S* o0\.', l)]
        if idx:
            a, b = max(idx[0] - 4, 0), min(idx[-1] + 2, len(L))
            seg = [l for l in L[a:b] if l.strip()]
            if len(seg) > 90:   # keep the samples, the colour use and the output
                seg = [l for l in seg if re.search(r'sample|discard| o0|v\d+\.|cb[1-9]', l)]
            print('\n'.join('    ' + l.strip() for l in seg[:120]))


if __name__ == '__main__':
    main()
