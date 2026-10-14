"""Plays a Ready or Not Cascade particle system (decoded in fx_collect.json) the way Unreal's Cascade does, so it can
be drawn (tools/ron/fxrender.py) and measured in KCD2 terms (tools/ron/ns_stats.py) like the Niagara ones.

    python3 tools/ron/cascade_sim.py <P_ name> <outdir> [seconds] [bursts]
    -> <outdir>/system.json + sim.npy (same layout as tools/ron/niagara_gpu.py)

Only enabled emitters run: Cascade's emitter checkbox is the Required module's bEnabled (false = switched off in the
editor; RoN ships many effects with layers switched off). Distributions are Unreal's lookup tables: Op 1 = a curve
over the table (TimeScale entries per unit time), Op 2 = uniform between the two sub-entries. Local frame: +Z = the
hit normal (EmitterOrigin offsets along it). Modules not implemented are listed in system.json ("skipped").
"""
import json, os, sys
import numpy as np

FX = '/mnt/e/Tools/deagle-work/ron_fx/_data/fx_collect.json'


def table(dist):
    if not isinstance(dist, dict): return None
    return dist.get('Table')


def lookup(tab, t, rnd=None):
    """value(s) of a distribution table at time t (array [n]); rnd [n] for Op 2. Returns [n, stride_out]."""
    vals = np.asarray(tab['Values'], float); op = tab.get('Op', 1); cnt = max(tab.get('EntryCount', 1), 1)
    stride = tab.get('EntryStride', 1); sub = tab.get('SubEntryStride', 0) or stride
    ts = tab.get('TimeScale', 0) or 0; tb = tab.get('TimeBias', 0) or 0
    t = np.atleast_1d(np.asarray(t, float))
    entries = vals.reshape(cnt, stride)
    if cnt == 1 or ts == 0:
        e = np.repeat(entries[:1], len(t), 0)
    else:
        x = np.clip((t - tb) * ts, 0, cnt - 1); i0 = np.floor(x).astype(int); i1 = np.minimum(i0 + 1, cnt - 1); f = (x - i0)[:, None]
        e = entries[i0] * (1 - f) + entries[i1] * f
    if op == 2:  # uniform between [min..] and [max..]
        lo, hi = e[:, :sub], e[:, sub:2 * sub]
        r = np.random.random((len(t), 1)) if rnd is None else np.asarray(rnd).reshape(len(t), -1)
        return lo + (hi - lo) * r
    return e


def lock_xyz(tab):
    return tab is not None and tab.get('LockFlag', 0) not in (0, None)


class Emitter:
    def __init__(self, mods, ek):
        self.ev = mods[ek]; self.name = self.ev.get('EmitterName') or ek.split('/')[-1]
        L = mods[self.ev['LODLevels'][0]]
        self.req = mods[L['RequiredModule']]; self.spawn = mods.get(L.get('SpawnModule'), {})
        self.mods = [mods[m] for m in L.get('Modules', []) if m in mods and mods[m].get('bEnabled') is not False]
        self.enabled = self.req.get('bEnabled') is not False
        td = mods.get(L.get('TypeDataModule'), {})
        self.typedata = td.get('cls', '').replace('ParticleModuleTypeData', '') or None
        self.skipped = []

    def renderer(self):
        r = self.req
        return {'Material': r.get('Material'), 'Alignment': {'PSA_Velocity': 'VelocityAligned'}.get(r.get('ScreenAlignment'), r.get('ScreenAlignment')),
                'SubImageSize': [r.get('SubImages_Horizontal') or 1, r.get('SubImages_Vertical') or 1],
                'bSubImageBlend': r.get('InterpolationMethod') in ('PSUVIM_Linear_Blend', 'PSUVIM_Random_Blend'),
                'Interpolation': r.get('InterpolationMethod'), 'AxisLock': None, 'TypeData': self.typedata}

    def run(self, seconds, bursts, dt=1 / 60.0, seed=1):
        np.random.seed(seed)
        R = self.req; S = self.spawn
        dur = R.get('EmitterDuration', 1.0) or 1.0; delay = R.get('EmitterDelay', 0.0) or 0.0
        loops = R.get('EmitterLoops', 0)
        origin = np.array(R.get('EmitterOrigin') or [0, 0, 0], float)
        rate = lookup(table(S.get('Rate')), [0])[0, 0] if table(S.get('Rate')) else 0.0
        rscale = lookup(table(S.get('RateScale')), [0])[0, 0] if table(S.get('RateScale')) else 1.0
        bl = S.get('BurstList') or []
        P = {k: [] for k in ('Position', 'Velocity', 'Age', 'Lifetime', 'SizeBase', 'Rot', 'RotRate', 'Accel', 'Drag', 'Burst', 'UniqueID', 'SubImageIndex', 'ColorBase', 'Spawned')}
        uid = 0; frames = []
        alive = None
        # spawn times per burst sample (independent bursts all at t=0, ids per burst)
        def spawn(n, t_emit, burst):
            nonlocal uid
            st = np.full(n, t_emit)
            pos = np.tile(origin, (n, 1)).astype(float); vel = np.zeros((n, 3)); size = np.ones((n, 3)); life = np.ones(n)
            rot = np.zeros(n); rrate = np.zeros(n); acc = np.zeros((n, 3)); drag = np.zeros(n); color = np.ones((n, 4)); sub = np.zeros(n)
            t_norm = np.clip(st / dur, 0, 1)
            for m in self.mods:
                c = m['cls'].replace('ParticleModule', '')
                if c == 'Lifetime': life = lookup(table(m['Lifetime']), t_norm)[:, 0]
                elif c == 'Size':
                    tab = table(m['StartSize'])
                    if tab and tab.get('Op') == 2 and lock_xyz(tab): r = np.random.random((n, 1)); size = lookup(tab, t_norm, np.repeat(r, 3, 1))
                    elif tab and tab.get('Op') == 2:
                        # Unreal's vector uniform: one random per axis unless locked; equal min/max per axis -> treat X=Y
                        r = np.random.random((n, 1)); size = lookup(tab, t_norm, np.repeat(r, 3, 1)) if np.allclose(tab['Values'][0], tab['Values'][1]) else lookup(tab, t_norm, np.random.random((n, 3)))
                    elif tab: size = lookup(tab, t_norm)
                elif c == 'Velocity':
                    tab = table(m['StartVelocity'])
                    if tab: vel += lookup(tab, t_norm, np.random.random((n, 3)) if tab.get('Op') == 2 else None)
                    trad = table(m.get('StartVelocityRadial'))
                    if trad:
                        rv = lookup(trad, t_norm)[:, 0]
                        d = pos - origin; ln = np.linalg.norm(d, axis=1, keepdims=True); d = np.where(ln > 1e-6, d / np.maximum(ln, 1e-6), 0)
                        vel += d * rv[:, None]
                elif c == 'Acceleration':
                    tab = table(m['Acceleration']); acc += lookup(tab, t_norm, np.random.random((n, 3)) if tab.get('Op') == 2 else None)
                elif c == 'AccelerationDrag':
                    tab = table(m['DragCoefficientRaw']); drag += lookup(tab, t_norm)[:, 0]
                elif c == 'Rotation':
                    tab = table(m['StartRotation']); rot = lookup(tab, t_norm)[:, 0] * 360.0
                elif c == 'RotationRate':
                    tab = table(m['StartRotationRate']); rrate = lookup(tab, t_norm)[:, 0] * 360.0
                elif c in ('Color', 'ColorOverLife', 'SizeMultiplyLife', 'OrientationAxisLock', 'ParameterDynamic', 'SubUV'):
                    if c == 'Color':
                        tc = table(m.get('StartColor')); ta = table(m.get('StartAlpha'))
                        if tc: color[:, :3] = lookup(tc, t_norm)
                        if ta: color[:, 3] = lookup(ta, t_norm)[:, 0]
                elif c == 'Location':
                    tab = table(m['StartLocation']); pos += lookup(tab, t_norm, np.random.random((n, 3)) if tab.get('Op') == 2 else None)
                elif c == 'LocationPrimitiveSphere':
                    rad = lookup(table(m['StartRadius']), t_norm)[:, 0] if table(m.get('StartRadius')) else np.full(n, 50.0)
                    d = np.random.normal(size=(n, 3)); d /= np.linalg.norm(d, axis=1, keepdims=True)
                    if not m.get('Surface_Only'): d *= np.random.random((n, 1)) ** (1 / 3)
                    pos += d * rad[:, None]
                    if m.get('Velocity'): vel += d * (lookup(table(m['VelocityScale']), t_norm)[:, :1] if table(m.get('VelocityScale')) else 1)
                else:
                    if c not in self.skipped: self.skipped.append(c)
            ids = np.arange(uid, uid + n); uid += n
            for k, v in (('Position', pos), ('Velocity', vel), ('Age', np.zeros(n)), ('Lifetime', life), ('SizeBase', size), ('Rot', rot),
                         ('RotRate', rrate), ('Accel', acc), ('Drag', drag), ('Burst', np.full(n, burst)), ('UniqueID', ids),
                         ('SubImageIndex', sub), ('ColorBase', color), ('Spawned', st)):
                P[k].append(v)

        t = 0.0; rate_acc = np.zeros(bursts)
        state = None
        def cat():
            return {k: (np.concatenate(v) if v else np.zeros(0)) for k, v in P.items()}
        # bursts at their times; rate over the emitter duration (first loop)
        burst_done = [set() for _ in range(bursts)]
        cur = None
        while t <= seconds + 1e-9:
            et = t - delay
            if 0 <= et <= dur and (loops == 1 or loops == 0):
                for b in range(bursts):
                    for bi, br in enumerate(bl):
                        if bi in burst_done[b]: continue
                        if et >= (br.get('Time') or 0) * dur:
                            lo = br.get('CountLow', -1); hi = br.get('Count', 0)
                            n = hi if lo is None or lo < 0 else np.random.randint(lo, hi + 1)
                            if n > 0: spawn(int(n), et, b)
                            burst_done[b].add(bi)
                    if rate > 0:
                        rate_acc[b] += rate * rscale * dt
                        k = int(rate_acc[b]); rate_acc[b] -= k
                        if k: spawn(k, et, b)
            cur = cat() if cur is None or any(len(v) for v in P.values()) else cur
            if cur is not None and any(len(v) for v in P.values()):
                # merge new into state
                if state is None: state = cur
                else: state = {k: np.concatenate([state[k], cur[k]]) for k in state}
                for v in P.values(): v.clear()
                cur = None
            if state is not None and len(state['Age']):
                keep = state['Age'] <= state['Lifetime']
                state = {k: v[keep] for k, v in state.items()}
                frames.append(self.frame(state, t))
                # update (Cascade: velocity += accel dt, drag, position += velocity dt, age += dt)
                acc = state['Accel'] - state['Velocity'] * state['Drag'][:, None]
                state['Velocity'] = state['Velocity'] + acc * dt
                state['Position'] = state['Position'] + state['Velocity'] * dt
                state['Rot'] = state['Rot'] + state['RotRate'] * dt
                state['Age'] = state['Age'] + dt
            else:
                frames.append({'t': t, 'n': 0})
            t += dt
        return frames

    def frame(self, s, t):
        n = len(s['Age']); na = np.clip(s['Age'] / np.maximum(s['Lifetime'], 1e-6), 0, 1)
        size = s['SizeBase'].copy(); color = s['ColorBase'].copy(); sub = s['SubImageIndex'].copy()
        for m in self.mods:
            c = m['cls'].replace('ParticleModule', '')
            if c == 'SizeMultiplyLife':
                f = lookup(table(m['LifeMultiplier']), na)
                if f.shape[1] == 1: f = np.repeat(f, 3, 1)
                size = size * f
            elif c == 'ColorOverLife':
                tc = table(m.get('ColorOverLife')); ta = table(m.get('AlphaOverLife'))
                if tc: color[:, :3] = color[:, :3] * lookup(tc, na)
                if ta: color[:, 3] = color[:, 3] * lookup(ta, na)[:, 0]
            elif c == 'SubImageIndex' or c == 'SubUV':
                tab = table(m.get('SubImageIndex'))
                if tab: sub = lookup(tab, na)[:, 0]
        cols, rows = self.renderer()['SubImageSize']
        if self.req.get('InterpolationMethod') in ('PSUVIM_Linear', 'PSUVIM_Linear_Blend') and not any(m['cls'].endswith('SubUV') for m in self.mods):
            sub = na * (cols * rows - 1)
        return {'t': t, 'n': n, 'Position': s['Position'].copy(), 'Velocity': s['Velocity'].copy(),
                'SpriteSize': size[:, :2].copy(), 'Initial.SpriteSize': s['SizeBase'][:, :2].copy(), 'Color': color,
                'Initial.Color': s['ColorBase'].copy(), 'Lifetime': s['Lifetime'][:, None], 'Age': s['Age'][:, None],
                'NormalizedAge': na[:, None], 'SpriteRotation': s['Rot'][:, None], 'SubImageIndex': sub[:, None],
                'UniqueID': s['UniqueID'][:, None].astype(np.int64), 'Burst': s['Burst'][:, None]}


def load(name):
    d = json.load(open(FX))['particle_systems']
    k = [x for x in d if x.split('/')[-1] == name][0]
    mods = d[k]['modules']; root = mods[name]
    return [Emitter(mods, ek) for ek in root['Emitters']]


def main(name, out, seconds=2.0, bursts=8):
    os.makedirs(out, exist_ok=True)
    ems = load(name)
    sysd = {'name': name, 'type': 'Cascade', 'emitters': {}, 'bursts': {}, 'user': {}, 'disabled': []}
    sims = {}
    used = {}
    for i, E in enumerate(ems):
        key = '%s_%d' % ((E.name or 'None').replace(' ', '_'), i)
        if not E.enabled:
            sysd['disabled'].append(key); continue
        frames = E.run(seconds, bursts, seed=i + 1)
        bl = E.spawn.get('BurstList') or []
        sysd['emitters'][key] = {'renderer': E.renderer(), 'skipped': E.skipped,
                                 'modules': [m['cls'].replace('ParticleModule', '') for m in E.mods]}
        lo = sum((b.get('CountLow') if (b.get('CountLow') or -1) >= 0 else b.get('Count', 0)) for b in bl)
        hi = sum(b.get('Count', 0) for b in bl)
        sysd['bursts'][key.rsplit('_', 1)[0]] = {'min': lo, 'max': hi}
        sims[key] = frames
    json.dump(sysd, open(os.path.join(out, 'system.json'), 'w'), indent=1, default=str)
    np.save(os.path.join(out, 'sim.npy'), np.array([sims], dtype=object), allow_pickle=True)
    return sysd, sims


if __name__ == '__main__':
    s, _ = main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 2.0, int(sys.argv[4]) if len(sys.argv) > 4 else 8)
    print(json.dumps({k: v for k, v in s.items() if k != 'emitters'}, indent=1))
    for k, v in s['emitters'].items(): print(k, v['renderer']['Material'], v['modules'], 'skipped', v['skipped'])
