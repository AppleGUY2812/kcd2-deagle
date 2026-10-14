"""Runs a D3D11 compute shader (cs_5_0) from its disassembly, on the CPU, for every thread at once (numpy lanes).

Made to run Ready or Not's compiled Niagara GPU particle scripts offline (tools/ron/niagara_gpu.py): the disassembly
must have hex literals (D3DDisassemble flag 0x80, tools/ron/dxbc_dis.ps1). Registers hold raw 32-bit words; each op
reinterprets them as float / int / uint like the hardware. Control flow uses lane masks (if / else / loop / break),
so all lanes run in lockstep and sync_g_t is a no-op. Only the instructions Niagara's scripts use are implemented: an
unknown one raises.

    prog = Program(open('x.asm').read())
    st = prog.run(nthreads, cb={0: words}, srv={0: words}, uav={1: words}, group=64)
"""
import re
import numpy as np

U32 = np.uint32
SW = {'x': 0, 'y': 1, 'z': 2, 'w': 3}


def f2u(a):
    return np.asarray(a, np.float32).view(U32)


def u2f(a):
    return np.asarray(a, U32).view(np.float32)


class Op:
    __slots__ = ('name', 'sat', 'args', 'mods', 'line')

    def __init__(self, name, sat, args, line):
        self.name, self.sat, self.args, self.line = name, sat, args, line


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([': depth += 1
        if ch in ')]': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


OPERAND = re.compile(r'^(?P<neg>-)?(?P<abs>\|)?(?P<body>.+?)\|?$')


class Operand:
    """r3.xyzw / -|r1.x| / cb0[r2.w + 3].y / l(0x..,..) / vThreadID.xy / t0.yzwx / g0.x / null"""
    __slots__ = ('kind', 'idx', 'rel', 'off', 'swz', 'neg', 'abs', 'lit', 'mask')

    def __init__(self, s):
        self.neg = s.startswith('-')
        if self.neg: s = s[1:]
        self.abs = s.startswith('|')
        if self.abs: s = s.strip('|')
        self.rel = None; self.off = 0; self.lit = None; self.swz = [0, 1, 2, 3]; self.mask = [0, 1, 2, 3]
        if s == 'null':
            self.kind = 'null'; return
        if s.startswith('l('):
            vals = [int(v.strip(), 16) & 0xffffffff for v in s[2:-1].split(',')]
            vals = vals + [vals[-1]] * (4 - len(vals)) if len(vals) < 4 else vals
            if len(vals) == 1: vals = vals * 4
            self.kind = 'l'; self.lit = np.array(vals, U32); return
        m = re.match(r'^(cb\d+|icb)\[(.+)\](?:\.([xyzw]+))?$', s)
        if m:
            self.kind = m.group(1); inner = m.group(2)
            mm = re.match(r'^(r\d+)\.([xyzw])(?:\s*\+\s*(\d+))?$', inner)
            if mm:
                self.rel = (mm.group(1), SW[mm.group(2)]); self.off = int(mm.group(3) or 0)
            else:
                self.off = int(inner)
            self._swz(m.group(3)); return
        m = re.match(r'^([a-zA-Z]+\d*)(?:\.([xyzw]+))?$', s)
        if not m: raise ValueError('operand ' + s)
        self.kind = m.group(1); self._swz(m.group(2))

    def _swz(self, s):
        if not s: return
        c = [SW[ch] for ch in s]
        self.mask = c
        self.swz = c * 4 if len(c) == 1 else (c + [c[-1]] * (4 - len(c)))


def parse(text):
    ops = []
    for ln in text.replace('\r', '').split('\n'):
        ln = ln.strip()
        if not ln or ln.startswith('//') or ln.startswith('dcl_') or ln.startswith('cs_'): continue
        head, _, rest = ln.partition(' ')
        name = head; sat = False
        if name.endswith('_sat'): name = name[:-4]; sat = True
        # strip type annotations: ld_indexable(buffer)(float,float,float,float) -> ld
        base = re.sub(r'\(.*?\)', '', name).replace('_indexable', '')
        rest = rest.strip()
        while rest.startswith('('):  # (float,float,...) after a space
            rest = rest[rest.index(')') + 1:].strip()
        args = [Operand(a) for a in split_args(rest)] if rest else []
        ops.append(Op(base, sat, args, ln))
    return ops


class State:
    pass


class Program:
    def __init__(self, text):
        self.ops = parse(text)
        # match control flow
        self.jump = {}
        stack = []
        for i, op in enumerate(self.ops):
            if op.name in ('if_nz', 'if_z', 'loop'): stack.append([i])
            elif op.name == 'else': stack[-1].append(i)
            elif op.name in ('endif', 'endloop'):
                fr = stack.pop(); fr.append(i)
                for j in fr: self.jump[j] = fr

    def run(self, n, cb, srv, uav, group=64, gsm=None, trace=None):
        S = State(); S.n = n
        S.r = {}
        S.cb = {k: np.asarray(v, U32).reshape(-1, 4) for k, v in cb.items()}
        S.srv = {k: np.asarray(v, U32) for k, v in srv.items()}
        S.uav = {k: np.asarray(v, U32) for k, v in uav.items()}
        tid = np.arange(n, dtype=U32)
        S.vtid = np.stack([tid, np.zeros(n, U32), np.zeros(n, U32), np.zeros(n, U32)], 1)
        S.vtig = np.stack([tid % group, np.zeros(n, U32), np.zeros(n, U32), np.zeros(n, U32)], 1)
        S.gid = tid // group
        ng = (n + group - 1) // group
        S.g = {}
        for k, size in (gsm or {}).items(): S.g[k] = np.zeros((ng, size), U32)
        active = np.ones(n, bool)
        stack = []  # frames: ['if', prev, cond] / ['loop', entry, broken, start]
        pc = 0
        ops = self.ops
        while pc < len(ops):
            op = ops[pc]; nm = op.name
            if nm in ('if_nz', 'if_z'):
                c = self.read(S, op.args[0])[:, 0] != 0
                if nm == 'if_z': c = ~c
                stack.append(['if', active, c])
                active = active & c
                if not active.any():
                    fr = self.jump[pc]
                    pc = fr[1] if len(fr) == 3 else fr[-1]
                    continue
            elif nm == 'else':
                fr = stack[-1]
                active = fr[1] & ~fr[2] & ~self._broken(stack)
                if not active.any():
                    pc = self.jump[pc][-1]; continue
            elif nm == 'endif':
                fr = stack.pop()
                active = fr[1] & ~self._broken(stack)
            elif nm == 'loop':
                stack.append(['loop', active.copy(), np.zeros(n, bool), pc])
            elif nm in ('break', 'breakc_nz', 'breakc_z', 'continue', 'continuec_nz'):
                if nm == 'break' or nm == 'continue': hit = active.copy()
                else:
                    c = self.read(S, op.args[0])[:, 0] != 0
                    hit = active & (c if nm.endswith('_nz') else ~c)
                for fr in reversed(stack):
                    if fr[0] == 'loop':
                        if nm.startswith('break'): fr[2] |= hit
                        else: fr.append(hit) if len(fr) == 4 else fr.__setitem__(4, fr[4] | hit)
                        break
                active = active & ~hit
            elif nm == 'endloop':
                fr = [f for f in stack if f[0] == 'loop'][-1]
                go = fr[1] & ~fr[2]
                if len(fr) > 4: fr.pop()  # continued lanes rejoin
                if go.any():
                    active = go; pc = fr[3] + 1; continue
                stack.pop(); active = fr[1] & ~self._broken(stack)
            elif nm == 'ret':
                if not stack: break
                active = np.zeros(n, bool)
            elif nm == 'sync_g_t':
                pass
            else:
                if active.any(): self.exec(S, op, active)
            if trace: trace(pc, op, S, active)
            pc += 1
        return S

    @staticmethod
    def _broken(stack):
        for fr in reversed(stack):
            if fr[0] == 'loop':
                b = fr[2]
                return b | fr[4] if len(fr) > 4 else b
        return np.zeros(1, bool)

    # ---- operands
    def reg(self, S, name):
        r = S.r.get(name)
        if r is None:
            r = S.r[name] = np.zeros((S.n, 4), U32)
        return r

    def read(self, S, o, ftype=None):
        k = o.kind
        if k == 'l':
            v = np.broadcast_to(o.lit, (S.n, 4))
        elif k.startswith('r') and k[1:].isdigit():
            v = self.reg(S, k)
        elif k.startswith('cb'):
            buf = S.cb[int(k[2:])]
            if o.rel:
                idx = self.reg(S, o.rel[0])[:, o.rel[1]].astype(np.int64) + o.off
                v = buf[np.clip(idx, 0, len(buf) - 1)]
            else:
                v = np.broadcast_to(buf[o.off], (S.n, 4))
        elif k == 'vThreadID': v = S.vtid
        elif k == 'vThreadIDInGroup': v = S.vtig
        elif k == 'vThreadGroupID':
            v = np.stack([S.gid, S.gid * 0, S.gid * 0, S.gid * 0], 1).astype(U32)
        else:
            raise ValueError('read ' + k)
        v = v[:, o.swz]
        if o.abs or o.neg:
            if ftype == 'i':
                vi = v.view(np.int32)
                if o.abs: vi = np.abs(vi)
                if o.neg: vi = -vi
                v = vi.view(U32)
            else:
                vf = v.view(np.float32)
                if o.abs: vf = np.abs(vf)
                if o.neg: vf = -vf
                v = vf.view(U32)
        return v

    def write(self, S, o, val, active, sat=False):
        if o.kind == 'null': return
        val = np.asarray(val)
        if val.dtype != U32: val = val.astype(np.float32).view(U32) if val.dtype.kind == 'f' else val.astype(np.int64).astype(U32)
        if sat:
            f = val.view(np.float32); f = np.clip(np.nan_to_num(f, nan=0.0), 0, 1).astype(np.float32); val = f.view(U32)
        r = self.reg(S, o.kind)
        for c in o.mask:
            r[active, c] = val[active, c]

    def f(self, S, o): return self.read(S, o).view(np.float32)
    def i(self, S, o): return self.read(S, o, 'i').view(np.int32)
    def u(self, S, o): return self.read(S, o, 'i')

    # ---- instructions
    def exec(self, S, op, act):
        nm, a = op.name, op.args
        F, I, Uu = (lambda k: self.f(S, a[k])), (lambda k: self.i(S, a[k])), (lambda k: self.u(S, a[k]))
        W = lambda v, k=0, sat=op.sat: self.write(S, a[k], v, act, sat)
        with np.errstate(all='ignore'):
            if nm == 'mov': W(self.read(S, a[1]))
            elif nm == 'movc':
                c = self.read(S, a[1]) != 0; W(np.where(c, self.read(S, a[2]), self.read(S, a[3])))
            elif nm == 'add': W(F(1) + F(2))
            elif nm == 'mul': W(F(1) * F(2))
            elif nm == 'mad': W(F(1) * F(2) + F(3))
            elif nm == 'div': W(F(1) / F(2))
            elif nm == 'min': W(np.fmin(F(1), F(2)))
            elif nm == 'max': W(np.fmax(F(1), F(2)))
            elif nm == 'sqrt': W(np.sqrt(F(1)))
            elif nm == 'rsq': W(1 / np.sqrt(F(1)))
            elif nm == 'rcp': W(1 / F(1))
            elif nm == 'exp': W(np.exp2(F(1)))
            elif nm == 'log': W(np.log2(F(1)))
            elif nm == 'frc': x = F(1); W(x - np.floor(x))
            elif nm == 'round_ni': W(np.floor(F(1)))
            elif nm == 'round_pi': W(np.ceil(F(1)))
            elif nm == 'round_z': W(np.trunc(F(1)))
            elif nm == 'round_ne': W(np.rint(F(1)))
            elif nm in ('dp2', 'dp3', 'dp4'):
                k = int(nm[2]); s = (F(1)[:, :k] * F(2)[:, :k]).sum(1, dtype=np.float32)
                W(np.repeat(s[:, None], 4, 1))
            elif nm == 'sincos':
                x = F(2)
                if a[0].kind != 'null': self.write(S, a[0], np.sin(x), act, op.sat)
                if a[1].kind != 'null': self.write(S, a[1], np.cos(x), act, op.sat)
            elif nm in ('lt', 'ge', 'eq', 'ne'):
                x, y = F(1), F(2)
                c = {'lt': x < y, 'ge': x >= y, 'eq': x == y, 'ne': x != y}[nm]
                W(np.where(c, U32(0xffffffff), U32(0)))
            elif nm in ('ilt', 'ige', 'ieq', 'ine'):
                x, y = I(1), I(2)
                c = {'ilt': x < y, 'ige': x >= y, 'ieq': x == y, 'ine': x != y}[nm]
                W(np.where(c, U32(0xffffffff), U32(0)))
            elif nm in ('ult', 'uge'):
                x, y = Uu(1), Uu(2)
                W(np.where(x < y if nm == 'ult' else x >= y, U32(0xffffffff), U32(0)))
            elif nm == 'iadd': W((I(1).astype(np.int64) + I(2)).astype(np.int32).view(U32))
            elif nm == 'imad': W((I(1).astype(np.int64) * I(2) + I(3)).astype(np.int64).astype(np.uint64).astype(U32))
            elif nm == 'imul':
                lo = (I(2).astype(np.int64) * I(3)).astype(np.uint64).astype(U32)
                if a[0].kind != 'null': raise NotImplementedError('imul hi')
                self.write(S, a[1], lo, act)
            elif nm == 'umul':
                lo = (Uu(2).astype(np.uint64) * Uu(3)).astype(U32)
                self.write(S, a[1], lo, act)
            elif nm in ('udiv',):
                x, y = Uu(2), Uu(3); y1 = np.where(y == 0, 1, y)
                if a[0].kind != 'null': self.write(S, a[0], np.where(y == 0, U32(0xffffffff), x // y1), act)
                if a[1].kind != 'null': self.write(S, a[1], np.where(y == 0, U32(0xffffffff), x % y1), act)
            elif nm == 'ineg': W((-I(1)).view(U32))
            elif nm == 'imin': W(np.minimum(I(1), I(2)).view(U32))
            elif nm == 'imax': W(np.maximum(I(1), I(2)).view(U32))
            elif nm == 'umin': W(np.minimum(Uu(1), Uu(2)))
            elif nm == 'umax': W(np.maximum(Uu(1), Uu(2)))
            elif nm == 'and': W(Uu(1) & Uu(2))
            elif nm == 'or': W(Uu(1) | Uu(2))
            elif nm == 'xor': W(Uu(1) ^ Uu(2))
            elif nm == 'not': W(~Uu(1))
            elif nm == 'ishl': W((Uu(1) << (Uu(2) & 31)).astype(U32))
            elif nm == 'ishr': W((I(1) >> (Uu(2) & 31).astype(np.int32)).view(U32))
            elif nm == 'ushr': W((Uu(1) >> (Uu(2) & 31)).astype(U32))
            elif nm == 'ubfe':
                w, o, x = Uu(1) & 31, Uu(2) & 31, Uu(3)
                v = np.where(w == 0, U32(0), ((x.astype(np.uint64) >> o) & ((np.uint64(1) << w.astype(np.uint64)) - 1)).astype(U32))
                W(v)
            elif nm == 'bfi':
                w, o, ins, base = Uu(1) & 31, Uu(2) & 31, Uu(3), Uu(4)
                m = (((np.uint64(1) << w.astype(np.uint64)) - 1) << o.astype(np.uint64)).astype(U32)
                W(((ins << o) & m) | (base & ~m))
            elif nm == 'countbits': W(np.array([[bin(int(x)).count('1') for x in row] for row in Uu(1)], U32))
            elif nm == 'utof': W(Uu(1).astype(np.float32))
            elif nm == 'itof': W(I(1).astype(np.float32))
            elif nm == 'ftoi':
                x = np.nan_to_num(F(1)); W(np.clip(np.trunc(x), -2**31, 2**31 - 1).astype(np.int64).astype(np.int32).view(U32))
            elif nm == 'ftou':
                x = np.nan_to_num(F(1)); W(np.clip(np.trunc(x), 0, 2**32 - 1).astype(np.uint64).astype(U32))
            elif nm == 'f32tof16':
                W(F(1).astype(np.float16).view(np.uint16).astype(U32))
            elif nm == 'f16tof32':
                W((Uu(1) & 0xffff).astype(np.uint16).view(np.float16).astype(np.float32))
            elif nm == 'ld':
                addr = Uu(1)[:, 0]; res = a[2]
                buf = S.srv[int(res.kind[1:])]
                v = buf[np.clip(addr, 0, len(buf) - 1)] if len(buf) else np.zeros(S.n, U32)
                v = np.where(addr < len(buf), v, U32(0))
                vec = np.stack([v, np.zeros_like(v), np.zeros_like(v), np.full_like(v, 0x3f800000)], 1)
                W(vec[:, res.swz])
            elif nm == 'ld_uav_typed':
                addr = Uu(1)[:, 0]; res = a[2]; buf = S.uav[int(res.kind[1:])]
                v = np.where(addr < len(buf), buf[np.clip(addr, 0, len(buf) - 1)], U32(0))
                vec = np.stack([v, np.zeros_like(v), np.zeros_like(v), np.zeros_like(v)], 1)
                W(vec[:, res.swz])
            elif nm == 'store_uav_typed':
                buf = S.uav[int(a[0].kind[1:])]; addr = Uu(1)[:, 0]; val = self.read(S, a[2])[:, 0]
                ok = act & (addr < len(buf))
                buf[addr[ok]] = val[ok]
            elif nm == 'ld_structured':
                g = S.g[a[3].kind]; idx = Uu(1)[:, 0]; off = Uu(2)[:, 0] // 4
                v = g[S.gid, np.clip(idx + off, 0, g.shape[1] - 1)]
                vec = np.stack([v, v, v, v], 1); W(vec)
            elif nm == 'store_structured':
                g = S.g[a[0].kind]; idx = Uu(1)[:, 0]; off = Uu(2)[:, 0] // 4; val = self.read(S, a[3])[:, 0]
                g[S.gid[act], (idx + off)[act]] = val[act]
            elif nm == 'ld_raw':
                g = S.g[a[2].kind]; off = Uu(1)[:, 0] // 4
                v = g[S.gid, off]; W(np.stack([v, v, v, v], 1))
            elif nm == 'store_raw':
                g = S.g[a[0].kind]; off = Uu(1)[:, 0] // 4; val = self.read(S, a[2])[:, 0]
                g[S.gid[act], off[act]] = val[act]
            elif nm == 'imm_atomic_iadd':
                buf = S.uav[int(a[1].kind[1:])]; addr = Uu(2)[:, 0]; val = self.read(S, a[3])[:, 0]
                old = np.zeros(S.n, U32)
                for lane in np.nonzero(act)[0]:
                    old[lane] = buf[addr[lane]]; buf[addr[lane]] = U32((int(buf[addr[lane]]) + int(val[lane])) & 0xffffffff)
                self.write(S, a[0], np.repeat(old[:, None], 4, 1), act)
            elif nm == 'atomic_iadd':
                buf = S.uav[int(a[0].kind[1:])]; addr = Uu(1)[:, 0]; val = self.read(S, a[2])[:, 0]
                for lane in np.nonzero(act)[0]:
                    buf[addr[lane]] = U32((int(buf[addr[lane]]) + int(val[lane])) & 0xffffffff)
            elif nm == 'sample_l':
                # scene textures (depth / normals for collision): nothing in the way
                W(np.tile(f2u([1e9, 0, 0, 1]), (S.n, 1)))
            else:
                raise NotImplementedError(op.line)
