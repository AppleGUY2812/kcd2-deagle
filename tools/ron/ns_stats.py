"""What each emitter of a Ready or Not Niagara effect does, measured from its own shaders (tools/ron/niagara_gpu.py),
in the terms KCD2's particle params use. Local frame: identity, so +Z = the hit normal.

    python3 tools/ron/ns_stats.py <dir from niagara_gpu.py extract> [bursts]

Per emitter: count, lifetime, sprite size (UE cm, full width x height), speed (m/s) and the angle off the normal, drag
(1/s, CryEngine AirResistance), gravity (g), the HDR colour at spawn and colour / alpha / size over normalised age,
whether the SubUV frame is animated over life or a random variant, and the spawn rotation spread.
"""
import json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from niagara_gpu import Emitter  # noqa: E402

BINS = np.array([0, .1, .2, .3, .4, .5, .6, .7, .8, .9, 1.0001])


def rng(a, lo=2, hi=98):
    return np.percentile(a, lo), np.percentile(a, hi)


def over_age(frames, key, ref=None, comp=None):
    """mean of key (/ ref) per normalised-age bin across all frames"""
    xs, ys = [], []
    for fr in frames:
        if not fr['n'] or key not in fr: continue
        v = fr[key] if comp is None else fr[key][:, comp:comp + 1]
        if ref:
            r = fr[ref] if comp is None else fr[ref][:, comp:comp + 1]
            v = v / np.where(np.abs(r) < 1e-9, 1e-9, r)
        xs.append(fr['NormalizedAge'][:, 0]); ys.append(v.mean(1) if v.ndim > 1 else v)
    if not xs: return None
    x = np.concatenate(xs); y = np.concatenate(ys)
    out = []
    for a, b in zip(BINS[:-1], BINS[1:]):
        m = (x >= a) & (x < b)
        out.append(round(float(y[m].mean()), 3) if m.any() else None)
    return out


def stats(d, bursts=64):
    sysd = json.load(open(os.path.join(d, 'system.json')))
    res = {}
    for em in sysd['emitters']:
        if not os.path.exists(os.path.join(d, em + '.asm')): continue
        E = Emitter(sysd, em, d)
        b = sysd.get('bursts', {}).get(em.rsplit('_', 1)[0], {}) or {}
        cnt = max(b.get('max') or 1, 1)
        frames = E.simulate(3.0, bursts, count=cnt)
        f0 = frames[0]
        if not f0['n']: continue
        r = {'renderer': {k: sysd['emitters'][em].get('renderer', {}).get(k) for k in ('Material', 'Alignment', 'FacingMode', 'SubImageSize', 'bSubImageBlend', 'SortOrderHint')},
             'count': [max(b.get('min') or 1, 1), cnt]}
        r['lifetime'] = rng(f0['Lifetime'][:, 0], 0, 100)
        age = f0['Age'][:, :1]
        v0 = f0['Velocity']; p0 = f0['Position'] - v0 * age
        sp = np.linalg.norm(v0, axis=1)
        r['size_cm'] = {'x': rng(f0['SpriteSize'][:, 0]), 'y': rng(f0['SpriteSize'][:, 1]),
                        'y_over_x_median': float(np.median(f0['SpriteSize'][:, 1] / np.maximum(f0['SpriteSize'][:, 0], 1e-6)))}
        r['speed_ms'] = [round(x / 100, 2) for x in rng(sp)]
        if sp.max() > 1:
            ang = np.degrees(np.arccos(np.clip(v0[:, 2] / np.maximum(sp, 1e-6), -1, 1)))
            r['angle_off_normal'] = [round(x, 1) for x in rng(ang, 0, 100)]
            r['speed_vs_sizey_corr'] = round(float(np.corrcoef(sp, f0['SpriteSize'][:, 1])[0, 1]), 2) if f0['n'] > 2 else None
        r['spawn_offset_cm'] = {'radius': [round(x, 2) for x in rng(np.linalg.norm(p0, axis=1), 0, 100)], 'mean': np.round(p0.mean(0), 2).tolist()}
        # drag and gravity from the first frames: dv/dt = -k v + a
        if sp.max() > 1 and len(frames) > 3:
            ids0 = f0['UniqueID'][:, 0]
            f2 = frames[3]; ids2 = f2['UniqueID'][:, 0]
            common, i0, i2 = np.intersect1d(ids0, ids2, return_indices=True)
            if len(common) > 2:
                dt = f2['t'] - f0['t']
                vh0 = np.linalg.norm(f0['Velocity'][i0, :2], axis=1); vh2 = np.linalg.norm(f2['Velocity'][i2, :2], axis=1)
                ok = vh0 > 5
                k = float(np.median(-np.log(np.maximum(vh2[ok], 1e-6) / vh0[ok]) / dt)) if ok.any() else 0.0
                vz0 = f0['Velocity'][i0, 2]; vz2 = f2['Velocity'][i2, 2]
                az = float(np.median((vz2 - vz0 * np.exp(-k * dt)) / ((1 - np.exp(-k * dt)) / k if k > 1e-3 else dt)))
                r['drag_per_s'] = round(k, 2); r['gravity_g'] = round(-az / 980.0, 2)
        c0 = f0['Color']
        r['color_spawn'] = {'rgb_max_chan': [round(x, 1) for x in rng(c0[:, :3].max(1))], 'rgb_mean_norm': np.round(c0[:, :3].mean(0) / c0[:, :3].mean(0).max(), 3).tolist(),
                            'alpha': [round(x, 3) for x in rng(c0[:, 3])]}
        if 'Initial.Color' in f0:
            r['color_over_age'] = [over_age(frames, 'Color', 'Initial.Color', c) for c in range(4)]
        r['color_abs_over_age'] = {'R': over_age(frames, 'Color', None, 0), 'A': over_age(frames, 'Color', None, 3)}
        if 'Initial.SpriteSize' in f0:
            r['size_over_age'] = [over_age(frames, 'SpriteSize', 'Initial.SpriteSize', c) for c in range(2)]
        if 'SubImageIndex' in f0:
            r['subimage_over_age'] = over_age(frames, 'SubImageIndex')
            r['subimage_spawn'] = [round(x, 2) for x in rng(f0['SubImageIndex'][:, 0], 0, 100)]
        if 'SpriteRotation' in f0:
            r['rotation_spawn'] = [round(x, 1) for x in rng(f0['SpriteRotation'][:, 0], 0, 100)]
            if len(frames) > 6 and frames[6]['n']:
                r['rotation_rate_hint'] = 'compare with frame 6'
        r['speed_over_age'] = over_age([{**fr, 'Speed': np.linalg.norm(fr['Velocity'], axis=1)[:, None]} for fr in frames if fr['n']], 'Speed')
        r['alive_ms'] = [(round(fr['t'] * 1000), fr['n']) for fr in frames[::6] if fr['n']][:15]
        res[em] = r
    return sysd, res


if __name__ == '__main__':
    sysd, res = stats(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 64)
    print(json.dumps({'bursts': sysd.get('bursts'), 'user': sysd.get('user'), 'emitters': res}, indent=1, default=float))
