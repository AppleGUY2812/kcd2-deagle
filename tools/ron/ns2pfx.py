#!/usr/bin/env python3
"""A Ready or Not Niagara impact (tools/ron/niagara_gpu.py extract dir) -> a KCD2 particle effect (pfx1 XML), every
number measured by running RoN's own compiled particle shaders (tools/ron/sm5emu.py), none typed in by hand.

    python3 -I tools/ron/ns2pfx.py <extract dir> <effect name> [--off old,layer,names] [--samples 48]

Prints one <Particles Name="<effect>"> block for src/Libs/Particles/deagle_impacts.xml (library deagle_impacts).

Mapping (docs/ron/niagara_gpu.md; KCD2 facts in MODLOG):
- the effect's local +Z is the hit normal = the KCD2 emitter's y; gravity is world -Z in both games.
- one-sided Random: Base = max, Random = 1 - min / max (CryEngine draws Base x (1 - Random x rand)).
- Count / SpawnDelay = the spawn burst (system script); counts under 1 spawn 1 (the compiled max(n, 1)).
- Size = half the sprite width in m; Aspect = height / width. Velocity-aligned sprites (Facing CameraX +
  OrientToVelocity) take Size = half the length along the flight and Aspect = width / length (MODLOG 2026-10-14).
- curves over particle age are per-particle ratios to the particle's spawn value (tracked by UniqueID), keyed every
  10 % of life; CryEngine curves are unit floats, so a curve is divided by its peak and the peak goes into the base.
- colour: lit materials -> Color = the spawn colour (DiffuseLighting 1); additive / emissive -> Color = the hue,
  EmissiveLighting = HDR nits x the material's emissive intensity / 1000 (the metal port's scale).
- the material table (MATS) says which KCD2 texture each RoN material became and how it is lit; a material missing
  from it stops the run (no guessing).
"""
import argparse, json, os, re, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from niagara_gpu import Emitter  # noqa: E402
import ns_stats  # noqa: E402

TEX = 'textures/particles/deagle/impacts/'
# RoN material -> KCD2 texture and lighting. lit: DiffuseLighting 1; back: DiffuseBacklighting (1 = RoN's translucent
# 'volumetric non-directional' lighting, lit the same from both sides; 0 = a lit card facing the camera);
# ei: emissive intensity of additive materials; soft: RoN depth fade -> SoftParticle.
MATS = {
    # masked + lit: base colour = particle colour x T_Cloud_ABC.R, clipped where T_Bullet_ConcreteImpact_BaseHRA.A
    # x particle alpha < 0.3333 (its pixel shader); baked into imp_ron_grain_diff by tools/ron/mk_ron_fx_tex.py
    'M_SandSplashSmall': dict(tex='imp_ron_grain_diff', tiles=(1, 1), lit=True, back=0, masked=True),
    # M_ParticleGeneric: translucent, lit (translucency volume), colour = texture x particle colour, opacity = texture
    # alpha x particle alpha x depth fade; T_thinSmoke_02_8x4 (Depth Fade Distance 50 cm)
    'MI_ParticleGeneric_ThinSmoke': dict(tex='imp_dust_diff', tiles=(8, 4), lit=True, back=1, soft=True),
    # M_ParticleGeneric's default Main Texture T_debrisSpray_4x1
    'MI_ParticleGeneric_DebrisSpray': dict(tex='imp_spray_diff', tiles=(4, 1), lit=True, back=1, soft=True),
    # M_Particle_LitTransparent: the same lighting and colour x texture as M_ParticleGeneric (no emissive)
    'MI_ThinSmoke_01': dict(tex='imp_dust_diff', tiles=(8, 4), lit=True, back=1, soft=True),
    'MI_DustImpact': dict(tex='imp_dustjet_diff', tiles=(16, 2), lit=True, back=1, soft=True),
    'MI_ParticleGeneric_DustImpact': dict(tex='imp_dustjet_diff', tiles=(16, 2), lit=True, back=1, soft=True),
    'MI_Muzzle_Puff_Front': dict(tex='imp_ron_puff_diff', tiles=(8, 4), lit=True, back=1, soft=True),
    'MI_DebrisSpray': dict(tex='imp_spray_diff', tiles=(4, 1), lit=True, back=1, soft=True),
    # DebrisMaster: lit translucent, Stones2x2.rgb x colour (defaults Darkness 1, Desat 0), opacity Stones.a x alpha
    'DebrisMaster': dict(tex='imp_stones_diff', tiles=(2, 2), lit=True, back=0),
    # masked + lit (clip at alpha 0.3333, baked hard into the texture by mk_ron_fx_tex.py)
    'M_SandSplash': dict(tex='imp_ron_sandblocks_diff', tiles=(2, 2), lit=True, back=0, masked=True),
    'M_GrassBlade': dict(tex='imp_ron_grass_diff', tiles=(2, 2), lit=True, back=0, masked=True),
    'Shards_Mat': dict(tex='imp_ron_woodshard_diff', tiles=(2, 2), lit=True, back=0, masked=True),
    # M_ParticleEmberSimple: lit translucent, T_Glow_A x colour, opacity T_Glow_A.R x alpha
    'M_ParticleEmberSimple': dict(tex='imp_ron_glowdot_diff', tiles=(1, 1), lit=True, back=1, soft=True),
    # M_Particle_Additive: adds texture x colour (HDR), no emissive-intensity parameter (1)
    'MI_SparkSpray': dict(tex='imp_ron_sprayadd_diff', tiles=(4, 1), additive=True, ei=1),
}


def f(x, n=3):
    s = ('%.' + str(n) + 'f') % x
    s = s.rstrip('0').rstrip('.') if '.' in s else s
    return '0' if s in ('-0', '') else s


def pct(a, lo=2, hi=98):
    return float(np.percentile(a, lo)), float(np.percentile(a, hi))


def one_sided(lo, hi, n=3):
    """CryEngine 'Base,Random=R' covering [lo, hi]"""
    if hi <= 1e-9: return '0'
    r = 1 - lo / hi
    return f(hi, n) + (',Random=' + f(r, 2) if r >= 0.02 else '')


def observe(frames, key, comp=None):
    """(normalised age, value, particle id) of every live particle in every frame; comp None = mean of the components"""
    xs, ys, ids = [], [], []
    for fr in frames:
        if not fr['n'] or key not in fr: continue
        v = fr[key] if comp is None else fr[key][:, comp]
        if v.ndim > 1: v = v.mean(1)
        xs.append(fr['NormalizedAge'][:, 0]); ys.append(v); ids.append(fr['UniqueID'][:, 0])
    return np.concatenate(xs), np.concatenate(ys), np.concatenate(ids)


def model(frames, key, comp=None, keys=11):
    """value(particle i, age t) = factor_i x C(t): C = the mean over particles at ages 0, 0.1 .. 1 (each from the
    particles within +-0.05 of it), factor_i = the particle's median of value / C (mean 1) -> (ts, C, f_lo, f_hi)"""
    x, y, ids = observe(frames, key, comp)
    ts = np.linspace(0, 1, keys); C = []
    for t in ts:
        m = np.abs(x - t) <= 0.5 / (keys - 1)
        C.append(float(y[m].mean()) if m.any() else (C[-1] if C else 0.0))
    C = np.array(C)
    ci = np.interp(x, ts, C)
    ok = np.abs(ci) > 1e-9
    r = y[ok] / ci[ok]; pid = ids[ok]
    order = np.argsort(pid, kind='stable'); pid = pid[order]; r = r[order]
    cut = np.flatnonzero(np.diff(pid)) + 1
    fac = np.array([np.median(g) for g in np.split(r, cut)]) if len(r) else np.ones(1)
    fac = fac / max(fac.mean(), 1e-9)
    lo, hi = pct(fac)
    return ts, C, lo, hi


def simplify(ts, vs, tol=0.03):
    """drop keys within tol of the line through their neighbours (Douglas-Peucker over the key list)"""
    keep = [0, len(ts) - 1]
    def rec(a, b):
        if b - a < 2: return
        seg = vs[a] + (vs[b] - vs[a]) * (ts[a + 1:b] - ts[a]) / (ts[b] - ts[a])
        err = np.abs(vs[a + 1:b] - seg); k = int(np.argmax(err))
        if err[k] > tol: keep.append(a + 1 + k); rec(a, a + 1 + k); rec(a + 1 + k, b)
    rec(0, len(ts) - 1)
    keep = sorted(set(keep))
    return ts[keep], vs[keep]


def age_curve(ts, vs, vec=None):
    """CryEngine ParticleAge=(...) from unit-peak keys; None if flat. vec: per-key (x, y, z) tuples instead"""
    if vec is None:
        ts2, vs2 = simplify(ts, vs)
        if np.all(np.abs(vs2 - 1) < 0.02): return None
        keys = [('v=' + f(v)) if t == 0 else ('t=%s,v=%s' % (f(t, 2), f(v))) for t, v in zip(ts2, vs2)]
    else:
        keys = [(('v=' if t == 0 else 't=%s,v=' % f(t, 2)) + '(x=%s,y=%s,z=%s)' % tuple(f(c) for c in v)) for t, v in zip(ts, vec)]
    return 'ParticleAge=(' + ';'.join(keys) + ')'


def rotation_rate(frames):
    """deg/s per particle from the first frames (sprite rotation in degrees)"""
    if 'SpriteRotation' not in frames[0] or len(frames) < 4: return 0.0, 0.0
    a, b = frames[0], frames[3]
    common, i0, i1 = np.intersect1d(a['UniqueID'][:, 0], b['UniqueID'][:, 0], return_indices=True)
    if len(common) < 2: return 0.0, 0.0
    r = (b['SpriteRotation'][i1, 0] - a['SpriteRotation'][i0, 0]) / (b['t'] - a['t'])
    return float(np.median(r)), float(np.percentile(np.abs(r), 98))


def emitter_xml(sysd, d, em, samples, mats, name):
    E = Emitter(sysd, em, d)
    b = sysd.get('bursts', {}).get(em.rsplit('_', 1)[0], {}) or {}
    nmax = max(b.get('max') or 1, 1); nmin = max(b.get('min') or 1, 1)
    rend = sysd['emitters'][em].get('renderer', {})
    mat = rend.get('Material')
    if mat not in mats: raise SystemExit('%s: material %s is not in MATS; decode it first' % (em, mat))
    M = mats[mat]
    # RoN's update scripts can draw a new random number every frame (alpha does in NS_BulletImpact_Dirt's Dust):
    # with one seed for all frames each particle keeps its draw, so the per-particle spread and the mean curve over
    # age come out clean. Whether RoN shows that draw as a flicker in game is not known (unverified).
    run = E.run
    E.run = lambda ne, ns, fi, ii, dt, seed, rot=None, cap=None: run(ne, ns, fi, ii, dt, 12345, rot=rot, cap=cap)
    # time step: 1/60 s, finer for short-lived particles so 10 % of a life spans a few steps
    probe = E.simulate(0.0, 4, count=nmax)
    lmin = float(probe[0]['Lifetime'][:, 0].min()) if probe[0]['n'] else 1.0
    dt = float(np.clip(lmin / 30, 1 / 600, 1 / 60))
    lmax = float(probe[0]['Lifetime'][:, 0].max()) if probe[0]['n'] else 3.0
    frames = E.simulate(lmax + 2 * dt, samples, dt=dt, count=nmax)
    st = ns_stats.measure(frames, b, rend)
    f0 = frames[0]
    p = {}
    p['Count'] = one_sided(nmin, nmax, 2)
    if b.get('time'): p['SpawnDelay'] = f(b['time'])
    p['ParticleLifeTime'] = one_sided(*pct(f0['Lifetime'][:, 0], 0, 100))
    # direction and speed
    v0 = f0['Velocity']; sp = np.linalg.norm(v0, axis=1) / 100
    if sp.max() > 0.05:
        ang = np.degrees(np.arccos(np.clip(v0[:, 2] / np.maximum(sp * 100, 1e-6), -1, 1)))
        a_lo, a_hi = pct(ang, 0, 100)
        p['EmitAngle'] = one_sided(a_lo, a_hi, 1) if a_hi > 1 else '0'
        p['Speed'] = one_sided(*pct(sp))
    else:
        p['Speed'] = '0'
    # renderer
    tx, ty = M['tiles']
    tiling = ['TilesX=%d' % tx] + (['TilesY=%d' % ty] if ty > 1 else [])
    if tx * ty > 1 and 'SubImageIndex' in f0:
        si = st.get('subimage_over_age') or []
        s0 = pct(f0['SubImageIndex'][:, 0], 0, 100)
        if si and si[-1] is not None and si[-1] - (si[0] or 0) > 1:   # played over the particle's life
            tiling += ['AnimFramesCount=%d' % (tx * ty), 'AnimCycle=Once'] + (['AnimBlend=true'] if rend.get('bSubImageBlend') else [])
        elif s0[1] - s0[0] < 0.5:                                    # one fixed frame
            if int(round(s0[0])): tiling += ['FirstTile=%d' % int(round(s0[0]))]
            tiling += ['VariantCount=1']
        else:
            tiling += ['VariantCount=%d' % (tx * ty)]
    velal = rend.get('Alignment') == 'VelocityAligned'
    if velal: p['Facing'] = 'CameraX'; p['OrientToVelocity'] = 'true'
    if M.get('additive'): p['BlendType'] = 'Additive'
    p['Texture'] = TEX + M['tex'] + '.tif'
    p['TextureTiling'] = ','.join(tiling)
    if M.get('soft'): p['SoftParticle'] = 'true'
    # colour, alpha, size: value = per-particle factor x the mean curve over age (model())
    ts, Ca, alo, ahi = model(frames, 'Color', 3)
    ch = [model(frames, 'Color', k) for k in range(3)]
    base_rgb = np.array([max(float(c[1].max()), 0.0) for c in ch])
    vec = np.stack([c[1] / max(float(c[1].max()), 1e-9) for c in ch], 1)    # keys x 3, each channel peaks at 1
    k = int(np.argmax(base_rgb)); clo, chi = ch[k][2], ch[k][3]
    if M.get('additive'):
        hdr = float(base_rgb.max()) * chi
        p['Color'] = '(x=%s,y=%s,z=%s)' % tuple(f(x) for x in base_rgb / max(base_rgb.max(), 1e-9))
        p['EmissiveLighting'] = f(hdr * M.get('ei', 1) / 1000, 3)
    else:
        rgb = base_rgb * chi
        if rgb.max() > 1.001: raise SystemExit('%s: lit colour above 1 (%s): check the material' % (em, rgb))
        p['Color'] = '(x=%s,y=%s,z=%s)' % tuple(f(x) for x in rgb)
        p['DiffuseLighting'] = '1'; p['DiffuseBacklighting'] = f(M.get('back', 0)); p['EmissiveLighting'] = '0'
    if chi > 0 and 1 - clo / chi >= 0.02: p['Color'] += ',Random=' + f(1 - clo / chi, 2)
    if np.abs(vec - vec[:, :1]).max() < 0.03:          # the same curve on all three: write it once, as a grey
        t2, v2 = simplify(ts, vec.mean(1))
        if not np.all(np.abs(v2 - 1) < 0.02): p['Color'] += ',' + age_curve(t2, None, vec=[(v, v, v) for v in v2])
    else:
        p['Color'] += ',' + age_curve(ts, None, vec=[tuple(r) for r in vec])
    apk = max(float(Ca.max()), 1e-9)
    # KCD2 (Engine/Shaders.pak, Particles.cfi): particle alpha only reaches the pixel through AlphaClip,
    # clip = alphaTestMin + alphaTestMax x alpha (Min at alpha 0, Max at alpha 1), out = saturate(min(tex.a - SourceMin,
    # SourceWidth) x Scale / SourceWidth), discarded at <= 0.004; the default (Scale 0..1, SourceMin 0, Width 1) is
    # tex.a x alpha. Vertex colour is 8-bit, so RoN's alpha above 1 (saturate(tex.a x alpha)) goes into Scale instead.
    ahi_abs = apk * ahi
    over = ahi_abs > 1.001
    p['Alpha'] = one_sided(apk * alo / (ahi_abs if over else 1), 1.0 if over else ahi_abs)
    if over and not M.get('masked'): p['AlphaClip'] = 'Scale=(Max=%s)' % f(ahi_abs)
    acv = age_curve(ts, Ca / apk)
    if acv: p['Alpha'] += ',' + acv
    masknote = ''
    if M.get('masked'):
        # RoN draws where tex.a x alpha >= 1/3, opaque. The texture's alpha holds s = 0.004 + 0.996 (1 - 1/(3 tex.a))
        # (0 where tex.a < 1/3; tools/ron/mk_ron_fx_tex.py), and SourceMin runs 1 -> 0.004 with the particle alpha:
        # s - SourceMin > 0.004 exactly when alpha > 1/(3 tex.a). Scale stays 1: a masked pixel is opaque.
        p['AlphaClip'] = 'Scale=(Min=1,Max=1),SourceMin=(Min=1,Max=0.004),SourceWidth=(Min=0.005,Max=0.005)'
        if over: p['Alpha'] = one_sided(apk * alo, min(ahi_abs, 1.0))   # above 1 changes nothing on a clip
        if acv or alo < 0.98 * ahi or apk < 0.98:
            masknote = ' dissolves with alpha %.2f-%.2f%s (RoN clip tex.a x alpha < 1/3)' % (apk * alo, ahi_abs, ' over age' if acv else '')
    # size (UE full width / height in cm -> half size in m)
    _, Cx, xlo, xhi = model(frames, 'SpriteSize', 0)
    _, Cy, ylo, yhi = model(frames, 'SpriteSize', 1)
    if velal:   # along the flight = sprite y, across = x
        L, W, llo, lhi = Cy, Cx, ylo, yhi
    else:
        L, W, llo, lhi = Cx, Cy, xlo, xhi
    lpk = max(float(L.max()), 1e-9)
    p['Size'] = one_sided(lpk * llo / 200, lpk * lhi / 200, 4)
    scv = age_curve(ts, L / lpk)
    if scv: p['Size'] += ',' + scv
    ac = W / np.maximum(L, 1e-9)
    gone = L < 0.02 * lpk                            # shrunk to nothing: hold the last real aspect
    for i in np.flatnonzero(gone): ac[i] = ac[i - 1] if i else 1.0
    if velal or np.abs(ac - 1).max() > 0.03:
        apk2 = max(float(ac.max()), 1e-9)
        p['Aspect'] = f(apk2)
        acc = age_curve(ts, ac / apk2)
        if acc: p['Aspect'] += ',' + acc
    s0 = f0['SpriteSize']
    # motion
    if st.get('drag_per_s') is not None:
        p['AirResistance'] = f(max(st['drag_per_s'], 0), 2) + ',WindScale=0'
        p['GravityScale'] = f(st['gravity_g'], 2)
    else:
        p['AirResistance'] = 'WindScale=0'; p['GravityScale'] = '0'
    if 'SpriteRotation' in f0:
        r_lo, r_hi = pct(f0['SpriteRotation'][:, 0], 0, 100)
        span = r_hi - r_lo
        if span > 1: p['RandomAngles'] = 'y=%s' % (180 if span > 300 else f(span / 2, 1))
        rmed, rmax = rotation_rate(frames)
        if rmax > 5:
            if abs(rmed) > 0.8 * rmax: p['RotationRate'] = 'y=%s' % f(rmed, 0)
            else: p['RandomRotationRate'] = 'y=%s' % f(rmax, 0)
    # comment: what RoN does, in its own units
    note = ('%s (%s%s): %d-%d%s, %.2f-%.2f s, %.1f-%.1f m/s%s, %.1f-%.1f x %.1f-%.1f cm, drag %s/s, %s g' % (
        em, mat, ', velocity-aligned' if velal else '', nmin, nmax, (' at %.2f s' % b['time']) if b.get('time') else '',
        *pct(f0['Lifetime'][:, 0], 0, 100), sp.min(), sp.max(),
        (' at %.0f-%.0f deg off the normal' % tuple(pct(ang, 0, 100))) if sp.max() > 0.05 else '',
        *pct(s0[:, 0]), *pct(s0[:, 1]), st.get('drag_per_s'), st.get('gravity_g'))) + masknote
    attrs = ' '.join('%s="%s"' % kv for kv in p.items())
    return ('\t\t\t<Particles Name="%s">\n\t\t\t\t<!-- %s -->\n\t\t\t\t<Params %s />\n\t\t\t</Particles>\n' % (name, note, attrs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir'); ap.add_argument('effect')
    ap.add_argument('--off', default='', help='old layer names to list switched off (fxreload keeps missing ones)')
    ap.add_argument('--samples', type=int, default=48)
    ap.add_argument('--into', help='library XML: replace the effect\'s top-level block there (or add it), old layer names off')
    ap.add_argument('--note', default='', help='a line for the comment above the effect (which surfaces use it)')
    ap.add_argument('--skip', default='LOW_SETTINGS',
                    help='emitters left out (regex): RoN draws either its HIGH or its LOW debris, by quality setting')
    a = ap.parse_args()
    sysd = json.load(open(os.path.join(a.dir, 'system.json')))
    out = ['\t<Particles Name="%s">\n\t\t<Params Enabled="false" Count="1" ParticleLifeTime="0.05" />\n\t\t<Childs>\n' % a.effect]
    names = []
    for em in sysd['emitters']:
        if not os.path.exists(os.path.join(a.dir, em + '.asm')): continue
        if a.skip and re.search(a.skip, em): continue
        if 'Mesh' in str(sysd['emitters'][em].get('renderer', {}).get('cls')):
            out.append('\t\t\t<!-- %s: a mesh emitter (RoN static meshes), not converted -->\n' % em); continue
        unsup = [x for x in sysd['emitters'][em].get('di_info', []) if 'VectorField' in x]
        if unsup:   # the emulator has no vector fields: their constants read as 0 and the motion blows up
            out.append('\t\t\t<!-- %s: not converted, needs %s (not emulated yet) -->\n' % (em, ', '.join(unsup))); continue
        n = em.rsplit('_', 1)[0].lower()
        names.append(n)
        out.append(emitter_xml(sysd, a.dir, em, a.samples, MATS, n))
    old_block = None
    if a.into:
        lib = open(a.into, encoding='utf-8').read()
        m = re.search(r'((?:\t<!--(?:(?!-->).)*-->\n)*)\t<Particles Name="%s">\n.*?\n\t</Particles>\n' % re.escape(a.effect), lib, re.S)
        if m:
            old_block = m
            prev = re.findall(r'^\t\t\t<Particles Name="([^"]+)">', m.group(0), re.M)
            a.off = ','.join([x for x in a.off.split(',') if x] + prev)
    off = [x for x in dict.fromkeys(a.off.split(',')) if x and x not in names]
    if off:
        out.append('\t\t\t<!-- layers of the old effect, switched off for deagle_fxreload -->\n')
        out += ['\t\t\t<Particles Name="%s"><Params Enabled="false" /></Particles>\n' % x for x in off]
    out.append('\t\t</Childs>\n\t</Particles>\n')
    head = '\t<!-- %s: Ready or Not\'s %s, measured by running its own shaders (tools/ron/ns2pfx.py)%s -->\n' % (
        a.effect, os.path.basename(os.path.normpath(a.dir)), ('. ' + a.note) if a.note else '')
    block = head + ''.join(out)
    if not a.into:
        sys.stdout.write(block); return
    if old_block:
        lib = lib[:old_block.start()] + block + lib[old_block.end():]
    else:
        k = lib.rindex('</ParticleLibrary>')
        lib = lib[:k] + block + lib[k:]
    open(a.into, 'w', encoding='utf-8', newline='').write(lib)
    print('%s: %s in %s (%d layers, %d old ones off)' % (a.effect, 'replaced' if old_block else 'added', a.into, len(names), len(off)))


if __name__ == '__main__':
    main()
