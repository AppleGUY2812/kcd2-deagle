"""Ready or Not's Niagara impact effects, run offline through their own compiled GPU particle shaders.

Niagara's particle scripts are compiled into compute shaders (spawn counts, speeds, lifetimes, sizes, drag... are
literals in the shader). This pulls them out and runs them on the CPU with tools/ron/sm5emu.py:

  niagara_gpu.py extract <game path of the NS system, e.g. ReadyOrNot/VFX/VFXImpacts/Niagara/NS_BulletImpact_Brick> <dir>
      the package (ronx.py), each emitter's compute shader from the chunk shader archive (ShaderArchive-*-PCD3D_SM5,
      Oodle), disassembled with hex literals (dxbc_dis.ps1, Windows d3dcompiler_47), the particle layout, curves,
      renderers, user colours and the spawn-burst counts (system update script, VectorVM bytecode) -> <dir>/system.json
  niagara_gpu.py sim <dir> [seconds] [samples]
      spawns `samples` bursts of every emitter and steps them at 60 Hz -> <dir>/sim.npz + a summary on stdout

Units are Unreal's (cm, cm/s); the effect's local frame is identity (RoN rotates it to the hit; see the summary for
which local axis the sparks leave along). See docs/ron/niagara_gpu.md.
"""
import ctypes, json, os, re, struct, subprocess, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ronpaths  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
U32 = np.uint32
WALL = [[0, 0, 1], [0, -1, 0], [1, 0, 0]]  # rows = the effect's local X, Y, Z in world: local Z (normal) -> +X


# ---------------------------------------------------------------- extraction
def pkg_open(base):
    from usmap import U
    from dump import USMAP
    from uasset import Pkg
    u = U(USMAP)
    return Pkg(base, u), u


def fullname(p, i):
    e = p.exports[i]; n = e['name']; o = e['outer']
    while o > 0:
        n = p.exports[o - 1]['name'] + '/' + n; o = p.exports[o - 1]['outer']
    return n


def props(p, u, e):
    from dump import class_schema
    from uasset import R
    nat, ex = class_schema(p, e['cls'], u)
    return p.props(R(p.data(e)), nat, ex)


class Archive:
    """FSerializedShaderArchive (UE 5.3, version 2): shader map hashes -> shader entries -> Oodle-compressed code."""

    def __init__(self, path):
        self.f = open(path, 'rb'); b = self.f.read(64 << 20); o = 4
        def arr(o, size, fmt=None):
            n, = struct.unpack_from('<i', b, o); o += 4
            if fmt is None: return [b[o + size * i:o + size * i + size] for i in range(n)], o + size * n
            return [struct.unpack_from(fmt, b, o + size * i) for i in range(n)], o + size * n
        self.maps, o = arr(o, 20); self.shaders, o = arr(o, 20)
        self.mapent, o = arr(o, 16, '<4I'); self.ent, o = arr(o, 17, '<QIIB'); _, o = arr(o, 16, '<qq')
        n, = struct.unpack_from('<i', b, o); o += 4
        self.idx = list(struct.unpack_from('<%dI' % n, b, o)); o += 4 * n
        self.base = o
        self.mapidx = {h: i for i, h in enumerate(self.maps)}
        self.ooz = ctypes.CDLL(ronpaths.OOZ)

    def find_map(self, blob):
        for k in range(len(blob) - 19):
            m = self.mapidx.get(blob[k:k + 20])
            if m is not None: return m
        return None

    def code(self, m):
        off, num, _, _ = self.mapent[m]; out = []
        for j in self.idx[off:off + num]:
            o, sz, usz, freq = self.ent[j]
            self.f.seek(self.base + o); cb = self.f.read(sz)
            if sz < usz:
                buf = ctypes.create_string_buffer(usz + 64); self.ooz.ooz_decompress(cb, sz, buf, usz); cb = buf.raw[:usz]
            i = cb.find(b'DXBC'); n, = struct.unpack_from('<I', cb, i + 24)
            out.append((freq, cb[i:i + n]))
        return out


VM_OPS = ("done add sub mul div mad lerp rcp rsq sqrt neg abs exp exp2 log log2 sin cos tan asin acos atan atan2 ceil "
          "floor fmod frac trunc clamp min max pow round sign step random noise cmplt cmple cmpgt cmpge cmpeq cmpneq select "
          "addi subi muli divi clampi mini maxi absi negi signi randomi cmplti cmplei cmpgti cmpgei cmpeqi cmpneqi bit_and "
          "bit_or bit_xor bit_not bit_lshift bit_rshift logic_and logic_or logic_xor logic_not f2i i2f f2b b2f i2b b2i "
          "inputdata_float inputdata_int32 inputdata_half inputdata_noadvance_float inputdata_noadvance_int32 "
          "inputdata_noadvance_half outputdata_float outputdata_int32 outputdata_half acquireindex external_func_call "
          "exec_index noise2D noise3D enter_stat_scope exit_stat_scope update_id acquire_id half_to_float fasi iasf").split()
VM_ONE = set("rcp rsq sqrt neg abs exp exp2 log log2 sin cos tan asin acos atan ceil floor frac trunc round sign f2i i2f "
             "f2b b2f i2b b2i absi negi signi bit_not logic_not random randomi noise half_to_float fasi iasf".split())
VM_THREE = set("mad lerp clamp select clampi".split())


def vm_listing(vm):
    """Legacy VectorVM bytecode -> [(scope path, op, srcs, dst)]. Constant operands are byte offsets into
    [parameter store][ScriptLiterals]; the literals start at the first offset past the store (found below)."""
    bc = bytes.fromhex(vm['ByteCode']['Data']); lit = bytes.fromhex(vm.get('ScriptLiterals', '') or '')
    scopes = [s['FriendlyName'] for s in vm.get('StatScopes', [])]
    ops, o, path = [], 0, []
    def u16():
        nonlocal o
        v, = struct.unpack_from('<H', bc, o); o += 2; return v
    raw = []
    while o < len(bc):
        op = VM_OPS[bc[o]]; o += 1
        if op == 'done': break
        if op == 'enter_stat_scope': raw.append(('enter', u16())); continue
        if op == 'exit_stat_scope': raw.append(('exit',)); continue
        if op.startswith('inputdata'): raw.append((op, None, [u16(), u16()], u16())); continue
        if op.startswith('outputdata'):
            t = bc[o]; o += 1; raw.append((op, t, [u16(), u16(), u16()], u16())); continue
        if op == 'acquireindex':
            t = bc[o]; o += 1; raw.append((op, t, [u16(), u16()], u16())); continue
        if op == 'exec_index': raw.append((op, 0, [], u16())); continue
        if op in ('update_id', 'acquire_id'): raw.append((op, 0, [u16(), u16()], u16())); continue
        if op == 'external_func_call': raise NotImplementedError('external function call in a system script')
        n = 1 if op in VM_ONE else 3 if op in VM_THREE else 2
        t = bc[o]; o += 1; raw.append((op, t, [u16() for _ in range(n)], u16()))
    # literal base: enter_stat_scope operands are constants holding the scope index; the first one is 'Main'-ish
    enters = [r[1] for r in raw if r[0] == 'enter']
    base = min(enters) if enters else 0
    def const(off):
        k = off - base
        if 0 <= k <= len(lit) - 4:
            return struct.unpack_from('<i', lit, k)[0], struct.unpack_from('<f', lit, k)[0]
        return None
    for r in raw:
        if r[0] == 'enter':
            c = const(r[1]); path.append(scopes[c[0]] if c and 0 <= c[0] < len(scopes) else '?'); continue
        if r[0] == 'exit':
            path.pop(); continue
        op, t, srcs, dst = r
        s = []
        for k, v in enumerate(srcs):
            if op.startswith('input') or op.startswith('output') and k != 2 or op in ('acquireindex',) and k == 0:
                s.append(('n', v))
            elif t is not None and t >> k & 1:
                c = const(v); s.append(('c', c) if c else ('p', v))
            else:
                s.append(('r', v))
        ops.append(('/'.join(path), op, s, dst))
    return ops


def burst_counts(ops):
    """count = base + int(rand * k) right before each SpawnBurst_Instantaneous_<emitter> scope (Niagara's
    'random range int' as compiled); constant counts show as a literal."""
    out = {}
    prev = None
    for i, (scope, op, s, dst) in enumerate(ops):
        m = re.search(r'SpawnBurst_Instantaneous_([A-Za-z0-9_]+)$', scope)
        if not m or (prev and prev == scope): prev = scope if m else prev; continue
        prev = scope; em = m.group(1)
        # the scope starts by reading the count register: i2f(rCount) then mul by SpawnCountScale
        reg = next((x[2][0][1] for x in ops[i:i + 12] if x[1] == 'i2f' and x[0] == scope), None)
        lo, span = None, 0
        for j in range(i - 1, max(i - 60, 0), -1):
            sc, o2, s2, d2 = ops[j]
            if d2 == reg and o2 == 'addi':
                lits = [v[1][0] for v in s2 if v[0] == 'c']; lo = lits[0] if lits else lo
                rr = [v[1] for v in s2 if v[0] == 'r']
                if rr:  # the random part: f2i(mul(rand, k))
                    for jj in range(j - 1, max(j - 8, 0), -1):
                        if ops[jj][1] == 'mul' and any(v[0] == 'c' for v in ops[jj][2]):
                            span = [v[1][1] for v in ops[jj][2] if v[0] == 'c'][0]; break
                break
            if d2 == reg and o2 == 'f2i':
                lo = 0
                for jj in range(j - 1, max(j - 4, 0), -1):
                    if ops[jj][1] == 'mul' and any(v[0] == 'c' for v in ops[jj][2]):
                        span = [v[1][1] for v in ops[jj][2] if v[0] == 'c'][0]; break
                break
        if lo is None:  # a literal straight into the scope
            lits = [v[1][0] for x in ops[i:i + 12] if x[1] == 'i2f' for v in x[2] if v[0] == 'c']
            lo = lits[0] if lits else None
        if lo is None and reg is None and any(x[1] == 'f2i' and x[0] == scope for x in ops[i:i + 12]):
            lo = 1  # count 1: the compiler folds i2f(1) x SpawnCountScale into f2i(SpawnCountScale)
        # spawn time: a time t > 0 compiles to sub(t, emitter age) in the scope's first ops; t = 0 to a neg
        t0 = next((v[1][1] for x in ops[i:i + 4] if x[1] == 'sub' and x[0] == scope
                   for v in x[2][:1] if v[0] == 'c'), 0.0)
        out[em] = {'min': lo, 'max': (lo + int(span) - 1) if span else lo, 'time': round(t0, 4),
                   'note': 'count = min + int(rand * (max-min+1)), x SpawnCountScale, at least 1; time = s after the hit'}
    return out


def dis(dxbcs):
    ps = os.path.join(HERE, 'dxbc_dis.ps1')
    def win(p): return subprocess.run(['wslpath', '-w', p], capture_output=True, text=True).stdout.strip()
    subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
                    "& '%s' -Files %s" % (win(ps), ','.join("'%s'" % win(p) for p in dxbcs))], check=True, capture_output=True)


def extract(game_path, outdir):
    os.makedirs(outdir, exist_ok=True)
    name = game_path.rstrip('/').split('/')[-1]
    base = os.path.join(ronpaths.OUT, 'ReadyOrNot/Content', game_path.split('ReadyOrNot/', 1)[-1] if not game_path.startswith('ReadyOrNot/Content') else game_path[len('ReadyOrNot/Content/'):])
    if not os.path.exists(base + '.uasset'):
        rx = re.escape(game_path.split('Content/')[-1].replace('ReadyOrNot/VFX', 'VFX')) + r'\.(uasset|uexp|ubulk)$'
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'ronx.py'), ronpaths.PAKS, ronpaths.OUT, ronpaths.OOZ, rx], check=True)
    p, u = pkg_open(base)
    sysd = {'name': name, 'emitters': {}, 'user': {}}
    # user parameters (colours), the curves, renderers
    dis_by_owner = {}
    for i, e in enumerate(p.exports):
        cls = p.ref(e['cls']); fn = fullname(p, i)
        if cls == 'NiagaraSystem':
            d = props(p, u, e); st = d['SystemCompiledData']['InstanceParamStore']
            data = bytes.fromhex(st['ParameterData'])
            for v in st['SortedParameterOffsets']:
                if v['Type'] in ('LinearColor', 'Vector4f'):
                    sysd['user'][v['Name']] = list(struct.unpack_from('<4f', data, v['Offset']))
                elif v['Type'] in ('NiagaraFloat',):
                    sysd['user'][v['Name']] = struct.unpack_from('<f', data, v['Offset'])[0]
            sysd['effect_type'] = d.get('EffectType')
        elif 'DataInterface' in str(cls) and '/GPUComputeScript/' in fn:
            try: d = props(p, u, e)
            except Exception: d = {}
            em = fn.split('/')[1]
            dis_by_owner.setdefault(em, []).append({'cls': cls, 'obj': e['name'], **{k: v for k, v in d.items()}})
        elif 'RendererProperties' in str(cls):
            try: d = props(p, u, e)
            except Exception as x: d = {'error': repr(x)}
            em = fn.split('/')[1]
            sysd['emitters'].setdefault(em, {})['renderer'] = {'cls': cls, **{k: v for k, v in d.items() if not isinstance(v, (dict, list)) or k in ('SubImageSize', 'PivotInUVSpace')}}
        elif cls == 'NiagaraScript' and fn.endswith('SystemUpdateScript'):
            vm = props(p, u, e)['CachedScriptVM']
            sysd['bursts'] = burst_counts(vm_listing(vm))
    # shaders
    arch = None
    for chunk in [0] + list(range(1, 25)):
        path = os.path.join(ronpaths.OUT, 'ReadyOrNot/Content/ShaderArchive-ReadyOrNot_Chunk%d-PCD3D_SM5-PCD3D_SM5.ushaderbytecode' % chunk)
        if not os.path.exists(path):
            subprocess.run([sys.executable, '-I', os.path.join(HERE, 'ronx.py'), ronpaths.PAKS, ronpaths.OUT, ronpaths.OOZ,
                            r'^ReadyOrNot/Content/+ShaderArchive-ReadyOrNot_Chunk%d-PCD3D_SM5-PCD3D_SM5\.ushaderbytecode$' % chunk], check=True)
        a = Archive(path)
        gpu = [(i, e) for i, e in enumerate(p.exports) if e['name'] == 'GPUComputeScript']
        if gpu and a.find_map(p.data(gpu[0][1])) is not None:
            arch = a; sysd['shader_archive'] = os.path.basename(path); break
    dx = []
    for i, e in enumerate(p.exports):
        fn = fullname(p, i)
        if e['name'] == 'GPUComputeScript':
            em = fn.split('/')[1]; blob = p.data(e)
            m = arch.find_map(blob); code = arch.code(m)
            assert len(code) == 1 and code[0][0] == 5, (em, [c[0] for c in code])
            f = os.path.join(outdir, em + '.dxbc'); open(f, 'wb').write(code[0][1]); dx.append(f)
            # external constants (cb1 = current, cb2 = previous): the user parameters this script names, in order
            names = p.names; ext = []
            for k in range(0, len(blob) - 8):
                ni, num = struct.unpack_from('<ii', blob, k)
                if num == 0 and 0 <= ni < len(names) and names[ni].startswith('User.') and names[ni] not in ext and k < 400:
                    ext.append(names[ni])
            E = sysd['emitters'].setdefault(em, {})
            E['external'] = ext; E['data_interfaces'] = dis_by_owner.get(em, [])
        elif e['name'] == 'SpawnScript':
            em = fn.split('/')[1]; sp = props(p, u, e); vm = sp.get('CachedScriptVM', {})
            E = sysd['emitters'].setdefault(em, {})
            E['attributes'] = [(a['Name'], a['Type']) for a in vm.get('Attributes', [])]
            E['di_info'] = [d['Name'] for d in vm.get('DataInterfaceInfo', [])]
            # which data-interface object each compiled name is (the GPU script's DI exports carry the same names)
            E['di_map'] = {d['CompileName']: d['ResolvedDataInterface'] for d in sp.get('ResolvedDataInterfaces', []) or []}
    dis(dx)
    json.dump(sysd, open(os.path.join(outdir, 'system.json'), 'w'), indent=1, default=str)
    return sysd


# ---------------------------------------------------------------- simulation
SIZES = {'NiagaraFloat': 1, 'NiagaraInt32': 1, 'NiagaraBool': 1, 'Vector2f': 2, 'Vector3f': 3, 'NiagaraPosition': 3,
         'Vector4f': 4, 'LinearColor': 4, 'Quat4f': 4, 'NiagaraID': 2, 'ENiagaraExecutionState': 1}
INTS = {'NiagaraInt32', 'NiagaraBool', 'NiagaraID', 'ENiagaraExecutionState'}


def layout(attrs):
    fl, il = {}, {}
    nf = ni = 0
    for n, t in attrs:
        k = SIZES.get(t, 1)
        if t in INTS: il[n] = (ni, k); ni += k
        else: fl[n] = (nf, k); nf += k
    return fl, nf, il, ni


def find_base(asm):
    m = re.search(r'imad r\d+\.[xyzw], vThreadID\.y, cb0\[(\d+)\]\.y, vThreadID\.x', asm)
    return int(m.group(1))


def curve_slots(asm):
    """Curve DI lookups: add x, -cb0[A] (min time); mul_sat x, cb0[B] (inv range); utof cb0[C] (samples-1); then the
    LUT offset cb0[D] with a stride of 1 / 2 / 3 / 4 (float / 2D / vector / colour curve)."""
    L = [ln.strip() for ln in asm.replace('\r', '').split('\n')]
    out = []
    for i, ln in enumerate(L):
        m = re.match(r'utof r\d+\.\w+, cb0\[(\d+)\]\.([xyzw])$', ln)
        if not m: continue
        a = re.search(r'-cb0\[(\d+)\]\.([xyzw])', L[i - 2]); b = re.search(r'cb0\[(\d+)\]\.([xyzw])', L[i - 1])
        if not (a and b and L[i - 1].startswith('mul_sat')): continue
        stride, off = 1, None
        for ln2 in L[i + 1:i + 24]:
            if 'ishl' in ln2 and re.search(r'ishl .*l\((0x[0-9a-f]+, )*0x00000001', ln2): stride = 2   # index x 2
            if 'bfi' in ln2 and 'l(0x0000001f), l(0x00000001)' in ln2: stride = 2                    # index x 2 + 1
            mm = re.search(r'imad r\S+, r\S+, l\(0x0000000(\d)\), cb0\[(\d+)\]\.([xyzw])', ln2)
            if mm: stride = int(mm.group(1)); off = off or (int(mm.group(2)), mm.group(3)); break
            mm = re.search(r'iadd r\S+, r\S+, cb0\[(\d+)\]\.([xyzw])', ln2)
            if mm and 'umin' not in ln2 and off is None: off = (int(mm.group(1)), mm.group(2))
            if off is not None and re.match(r'(add|mul_sat|utof) .*cb0\[\d+\]', ln2): break   # the next curve
        out.append({'min': (int(a.group(1)), a.group(2)), 'inv': (int(b.group(1)), b.group(2)), 'n': (int(m.group(1)), m.group(2)),
                    'off': off, 'stride': stride})
    # one entry per DI (spawn and update both sample)
    uniq = {}
    for c in out: uniq[c['off']] = c
    return list(uniq.values())


CURVE_CLS = {1: 'NiagaraDataInterfaceCurve', 2: 'NiagaraDataInterfaceVector2DCurve', 3: 'NiagaraDataInterfaceVectorCurve',
             4: 'NiagaraDataInterfaceColorCurve'}


def curve_dis(curves, E):
    """each curve slot's data interface. The DIs' shader parameters sit in cb0 one row each, in the order of the
    script's DataInterfaceInfo (unused ones keep their row), so row - first row = the DI's index there; di_map turns
    its compiled name into the DI object. Returns {off slot: DI dict}; raises if the classes don't line up."""
    dis_ = E['data_interfaces']; info = E.get('di_info', []); dmap = E.get('di_map', {})
    by_obj = {d.get('obj'): d for d in dis_}
    if not curves: return {}
    if not dmap or not all(d.get('obj') for d in dis_):
        raise ValueError('system.json has no DI names: extract it again (niagara_gpu.py extract)')
    # the DI parameters are packed in DataInterfaceInfo order (a curve: MinTime, InvTimeRange, NumSamplesMinusOne,
    # LUTOffset = 4 components; unused DIs keep their space; where the first one starts depends on the emitter).
    # Assign slots to DIs in order, matching the curve type; two slots 4 components apart are two DIs in a row.
    comp = lambda c: c['min'][0] * 4 + 'xyzw'.index(c['min'][1])
    cs = sorted(curves, key=comp)
    cls = [(by_obj.get(dmap.get(n)) or {}).get('cls') for n in info]
    def solve(i, j):
        if i == len(cs): return []
        for k in range(j, len(info)):
            if cls[k] != CURVE_CLS[cs[i]['stride']]: continue
            if i and comp(cs[i]) - comp(cs[i - 1]) == 4 and k != j: break    # back to back: must be the next DI
            if i and comp(cs[i]) - comp(cs[i - 1]) > 4 and k == j: continue  # a gap: at least one DI between
            rest = solve(i + 1, k + 1)
            if rest is not None: return [k] + rest
        return None
    ks = solve(0, 0)
    if ks is None:
        raise ValueError('curve slots %s do not line up with the DIs %s' % ([(c['min'], c['stride']) for c in cs], info))
    return {c['off']: by_obj[dmap[info[k]]] for c, k in zip(cs, ks)}


def build_static(curves, E):
    """t2 = every curve's LUT back to back; returns (words, {off slot: (start, samples-1, min, inv)})"""
    data, info = [], {}
    which = curve_dis(curves, E)
    for c in curves:
        d = which[c['off']]
        lut = d.get('ShaderLUT') or [1.0] * c['stride']
        nm1 = int(d.get('LUTNumSamplesMinusOne', len(lut) // c['stride'] - 1))
        mn = float(d.get('LUTMinTime', 0.0)); mx = float(d.get('LUTMaxTime', 1.0))
        inv = float(d.get('LUTInvTimeRange', 1.0 / (mx - mn) if mx != mn else 1.0))
        info[c['off']] = (len(data), nm1, mn, inv); data += lut
    return np.array(data, np.float32).view(U32), info


def vector_field(name):
    """a VectorFieldStatic (Niagara's engine ones: Engine/Plugins/FX/Niagara/Content/VectorFields/Assets/<name>) ->
    (volume [z, y, x, 4] float32, bounds min, bounds max). Its SourceData is FFloat16Color per voxel, x fastest,
    at the end of the export; Bounds a Box of doubles."""
    rel = 'Engine/Plugins/FX/Niagara/Content/VectorFields/Assets/' + name
    base = os.path.join(ronpaths.OUT, rel)
    if not os.path.exists(base + '.uasset'):
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'ronx.py'), ronpaths.PAKS, ronpaths.OUT, ronpaths.OOZ,
                        r'VectorFields/Assets/%s\.(uasset|uexp|ubulk)$' % re.escape(name)], check=True, capture_output=True)
    p, u = pkg_open(base)
    e = p.exports[0]; d = props(p, u, e); blob = p.data(e)
    nx, ny, nz = d['SizeX'], d['SizeY'], d['SizeZ']
    raw = blob[-(nx * ny * nz * 8) - 4:-4] if len(blob) >= nx * ny * nz * 8 + 4 else blob[-(nx * ny * nz * 8):]
    vol = np.frombuffer(raw, np.float16).astype(np.float32).reshape(nz, ny, nx, 4)
    b = bytes.fromhex(d['Bounds']); mn = struct.unpack_from('<3d', b, 0); mx = struct.unpack_from('<3d', b, 24)
    return vol, np.array(mn, np.float32), np.array(mx, np.float32)


class Emitter:
    def __init__(self, sysd, name, outdir):
        from sm5emu import Program
        self.name = name; E = sysd['emitters'][name]; self.E = E
        self.asm = open(os.path.join(outdir, name + '.asm')).read()
        self.prog = Program(self.asm)
        self.fl, self.nf, self.il, self.ni = layout(E['attributes'])
        self.base = find_base(self.asm)
        self.curves = curve_slots(self.asm)
        self.static, self.cinfo = build_static(self.curves, E)
        ncb = int(re.search(r'dcl_constantbuffer CB0\[(\d+)\]', self.asm).group(1))
        self.cb0 = np.zeros((ncb, 4), U32)
        ext = E.get('external', [])
        # buffers in declaration order: particle floats, particle ints, curve LUTs; textures (distance field) read as empty
        self.bufsrv = [int(x) for x in re.findall(r'dcl_resource_buffer \([^)]*\) t(\d+)', self.asm)]
        self.texsrv = [int(x) for x in re.findall(r'dcl_resource_texture\w+ \([^)]*\) t(\d+)', self.asm)]
        self.cbdecl = {int(a): int(b) for a, b in re.findall(r'dcl_constantbuffer CB(\d+)\[(\d+)\]', self.asm)}
        # the user parameters: current and previous frame, lerped by the spawn time (add rX, cbA[0], -cbB[0])
        m = re.search(r'add r\d+\.\w+, cb(\d+)\[0\]\.\w+, -cb(\d+)\[0\]', self.asm)
        self.extcb = [int(m.group(1)), int(m.group(2))] if m else []
        ncb1 = int((re.search(r'dcl_constantbuffer CB1\[(\d+)\]', self.asm) or [0, 1])[1]) if 'CB1' in self.asm else 1
        cb1 = np.zeros((max(ncb1, len(ext)), 4), np.float32)
        for k, n in enumerate(ext): cb1[k] = sysd['user'].get(n, [1, 1, 1, 1])
        self.cb1 = cb1.view(U32)
        rp = E.get('renderer', {})
        self.blend = bool(rp.get('bSubImageBlend'))
        # a vector field DI: its 3D texture goes to the shader's texture3d slot; its bounds to the two constant rows
        # the shader subtracts (uvw = (p x scale - min) / (max - min)); owner scale 1 and large-world tile 0 for it
        self.vf = None
        vfd = next((d for d in E.get('data_interfaces', []) if d.get('cls') == 'NiagaraDataInterfaceVectorField'), None)
        if vfd:
            vol, mn, mx = vector_field(vfd['Field'])
            t3 = re.search(r'sample_l_indexable\(texture3d\)\S* r\d+\.\w+, r\d+\.\w+, t(\d+)', self.asm)
            bm = re.search(r'add r\d+\.\w+, -cb0\[(\d+)\]\.(\w+), cb0\[(\d+)\]\.(\w+)', self.asm[t3.start() - 800:t3.start()]) if t3 else None
            if not (t3 and bm): raise ValueError('vector field: texture or bounds constants not found in ' + name)
            self.vf = {'slot': int(t3.group(1)), 'vol': vol, 'min': mn, 'max': mx,
                       'rmin': (int(bm.group(1)), bm.group(2)[1:4]), 'rmax': (int(bm.group(3)), bm.group(4)[1:4])}   # dest .yzw: source lanes 1-3

    def setcb(self, slot, val, kind='f'):
        r, c = slot; c = 'xyzw'.index(c) if isinstance(c, str) else c
        if r >= len(self.cb0): return   # a constant this shader doesn't declare (no data interfaces, ...)
        self.cb0[r, c] = struct.unpack('<I', struct.pack('<f' if kind == 'f' else '<i' if kind == 'i' else '<I', val))[0]

    def run(self, n_existing, n_spawn, fin, iin, dt, seed, rot=None, cap=None):
        B = self.base; cap = cap or max(n_existing + n_spawn, 1)
        self.cb0[:] = 0
        self.setcb((B, 'y'), 0, 'u'); self.setcb((B, 'w'), seed, 'u')
        for c in 'xyzw':
            self.setcb((B + 1, c), 0x7fffffff, 'i'); self.setcb((B + 2, c), 0x7fffffff, 'i')
        self.setcb((B + 3, 'x'), 0.0); self.setcb((B + 3, 'y'), 0.0); self.setcb((B + 3, 'w'), 0, 'i')
        self.setcb((B + 11, 'x'), n_spawn, 'u'); self.setcb((B + 11, 'y'), cap, 'u'); self.setcb((B + 11, 'z'), cap, 'u')
        self.setcb((B + 11, 'w'), 0, 'u'); self.setcb((B + 12, 'x'), 1, 'u')
        self.setcb((B + 12, 'z'), dt); self.setcb((B + 12, 'w'), 1.0 / dt)
        R = np.eye(3) if rot is None else rot
        for k in range(3):
            for c in range(3): self.setcb((B + 25 + k, c), float(R[k][c]))
        self.setcb((B + 49, 'x'), 0, 'i'); self.setcb((B + 49, 'w'), seed * 7 + 1, 'u')
        if self.vf:
            for c in 'xyz': self.setcb((B + 39, c), 1.0)              # Engine.Owner.Scale
            for c in 'xyz': self.setcb((B + 40, c), 0.0)              # large-world tile 0 (the effect at the origin)
            self.setcb((B + 40, 'w'), 2097152.0)                       # UE5's LWC tile size
            (rn, cn), (rx, cx) = self.vf['rmin'], self.vf['rmax']
            for k in range(3):
                self.setcb((rn, cn[k]), float(self.vf['min'][k])); self.setcb((rx, cx[k]), float(self.vf['max'][k]))
        for off, (start, nm1, mn, inv) in self.cinfo.items():
            c = [x for x in self.curves if x['off'] == off][0]
            self.setcb(c['min'], mn); self.setcb(c['inv'], inv); self.setcb(c['n'], nm1, 'u'); self.setcb(off, start, 'u')
        for m in re.finditer(r'movc r\d+\.\w, cb0\[(\d+)\]\.([xyzw])', self.asm):
            r = int(m.group(1))
            if r >= B + 49: self.setcb((r, m.group(2)), 0xffffffff if self.blend else 0, 'u')
        threads = ((n_existing + n_spawn + 63) // 64) * 64 or 64
        u2 = np.array([n_existing, 0], U32)
        uf = np.zeros(self.nf * cap, U32); ui = np.zeros(max(self.ni, 1) * cap, U32)
        cbs = {k: np.zeros((max(n, 1), 4), U32) for k, n in self.cbdecl.items()}
        cbs[0] = self.cb0
        for k in self.extcb: cbs[k] = self.cb1
        S = self.prog.run(threads, cb=cbs,
                          srv={**{k: np.zeros(0, U32) for k in self.texsrv},
                               **({self.vf['slot']: ('tex3d', self.vf['vol'])} if self.vf else {}),
                               self.bufsrv[0]: fin, self.bufsrv[1]: iin, self.bufsrv[2]: self.static},
                          uav={0: ui, 1: uf, 2: u2},
                          gsm={'g0': 64, 'g1': 16, 'g2': 4, 'g3': 1})
        return int(u2[1]), uf, ui

    def simulate(self, seconds, samples, dt=1 / 60.0, count=None, rot=None):
        """samples independent bursts of `count` particles (default: the burst's max) -> list of frames, each a dict
        attribute -> array [alive, k] (+ 'burst' index)."""
        frames = []
        cap = samples * count
        fin = np.zeros(self.nf * cap, U32); iin = np.zeros(max(self.ni, 1) * cap, U32)
        n, fo, io = self.run(0, cap, fin, iin, dt, seed=12345, cap=cap, rot=rot)
        t = 0.0
        while True:
            frames.append(self.unpack(fo, io, n, cap, t))
            t += dt
            if t > seconds or n == 0: break
            n, fo, io = self.run(n, 0, fo, io, dt, seed=12345 + int(t * 1000), cap=cap, rot=rot)
        return frames

    def unpack(self, fo, io, n, cap, t):
        f = fo.view(np.float32).reshape(self.nf, cap)[:, :n]
        out = {'t': t, 'n': n}
        for name, (o, k) in self.fl.items(): out[name] = f[o:o + k].T.copy()
        ii = io.view(np.int32).reshape(max(self.ni, 1), cap)[:, :n]
        for name, (o, k) in self.il.items(): out[name] = ii[o:o + k].T.copy()
        return out


def summary(sysd, sims):
    for em, frames in sims.items():
        f0 = frames[0]
        if not f0['n']: print(em, 'no particles'); continue
        print('##', em, 'renderer', {k: sysd['emitters'][em]['renderer'].get(k) for k in ('Material', 'Alignment', 'FacingMode', 'SubImageSize', 'bSubImageBlend')},
              'burst', sysd.get('bursts', {}).get(em.rsplit('_', 1)[0]))
        for k in ('Lifetime', 'SpriteSize', 'Velocity', 'Position', 'Color', 'Mass', 'SpriteRotation'):
            if k in f0:
                v = f0[k]; sp = np.linalg.norm(v, axis=1) if k == 'Velocity' else None
                print('   %-14s min %s max %s mean %s%s' % (k, np.round(v.min(0), 3), np.round(v.max(0), 3), np.round(v.mean(0), 3),
                                                       ('  speed %.0f-%.0f' % (sp.min(), sp.max())) if sp is not None else ''))
        life = [(fr['t'], fr['n']) for fr in frames]
        print('   alive', life[::6][:12])


if __name__ == '__main__':
    if sys.argv[1] == 'extract':
        s = extract(sys.argv[2], sys.argv[3]); print(json.dumps(s.get('bursts'), indent=1))
    elif sys.argv[1] == 'sim':
        d = sys.argv[2]; sysd = json.load(open(os.path.join(d, 'system.json')))
        secs = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0; samples = int(sys.argv[4]) if len(sys.argv) > 4 else 8
        sims = {}
        for em in sysd['emitters']:
            if not os.path.exists(os.path.join(d, em + '.asm')): continue
            E = Emitter(sysd, em, d)
            b = sysd.get('bursts', {}).get(em.rsplit('_', 1)[0], {}) or {}
            # 'wall': the effect's local Z (the hit normal) along world +X, like a hit on a wall facing the camera
            rot = WALL if (len(sys.argv) > 5 and sys.argv[5] == 'wall') else None
            sims[em] = E.simulate(secs, samples, count=max(b.get('max') or 4, 1), rot=rot)
        summary(sysd, sims)
        np.save(os.path.join(d, 'sim.npy'), np.array([sims], dtype=object), allow_pickle=True)
