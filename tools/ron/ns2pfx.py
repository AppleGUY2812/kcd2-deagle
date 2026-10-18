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
    # P_Bullet_Ricochet (Cascade). M_Sparks_NoNormal_Master: lit, saturate(tex x colour x 0.945) baked per emitter
    # (mk_ron_fx_tex.py ron_sparks_lit), opacity tex.R x alpha x Opacity x Master Opacity (0.5 default; mapping of the
    # two cb constants to those names inferred). Its constant emissive (cb4[1], a preshader value) isn't readable.
    'MI_Sparks_02': dict(tex='imp_ron_ricflash03_diff', tiles=(2, 2), lit=True, back=1, soft=True, baked_color=True, alpha_mul=0.5),
    'MI_Sparks_03': dict(tex='imp_ron_ricflash02_diff', tiles=(2, 2), lit=True, back=1, soft=True, baked_color=True, alpha_mul=0.5),
    'MI_Powder_01': dict(tex='imp_ron_powder_diff', tiles=(5, 5), lit=True, back=1, soft=True, baked_color=True, alpha_mul=0.75),
    # MAT_BloodClip_Master (NS_VFX_Impact_Mud's splash): an eroding, noise-distorted mask driven by its dynamic
    # parameters, baked to 4 variants x 16 frames over life with the splash colour (tools/ron/mk_ron_clip_tex.py)
    'MIC_BloodClip_A': dict(tex='imp_ron_mudsplash_diff', tiles=(8, 8), lit=True, back=1, baked_color=True,
                            tiling='TilesX=8,TilesY=8,VariantCount=4,AnimFramesCount=16,AnimCycle=Once,AnimBlend=true,FlipChance=0.5'),
    # M_Sparks_NoNormal_Master again (cloth): feathers (colour 0.25 x Base Overlay 0.945, Opacity 1.5 x Master 1.0),
    # sparks (HDR colour baked; Opacity 5 x Master 0.5)
    'MI_Feathers_01': dict(tex='imp_ron_feather_diff', tiles=(5, 5), lit=True, back=1, soft=True, color_mul=0.945, alpha_mul=1.5),
    'MI_Sparks_05': dict(tex='imp_ron_sparks05_diff', tiles=(3, 3), lit=True, back=1, soft=True, baked_color=True, alpha_mul=2.5),
    # M_Glass_Chunks: dithered clip baked as a threshold (mk_ron_fx_tex.py); its normal map isn't carried over
    'M_Glass_Chunks': dict(tex='imp_ron_glasschunk_diff', tiles=(3, 3), lit=True, back=0, dither=True),
    # M_Particle_Additive with its default T_longSpark_02_8x2 (drawn 4x2), adds texture x colour; PivotOffset (0, 0.5)
    'M_Particle_Additive': dict(tex='imp_sparks_diff', tiles=(4, 2), additive=True, ei=1),
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
    over, enote = {}, ''
    if E.vf:   # a vector field pushes the particles around: KCD2 can't sample it; its random turbulence is the nearest
        st = ns_stats.measure(frames, b, rend)
        k = max(st.get('drag_per_s') or 0.0, 0.0); g = (st.get('gravity_g') or 0.0) * 980.665
        acc = []
        for f0, f1 in zip(frames, frames[1:]):
            if not (f0['n'] and f1['n']): continue
            c, i0, i1 = np.intersect1d(f0['UniqueID'][:, 0], f1['UniqueID'][:, 0], return_indices=True)
            v0 = f0['Velocity'][i0]; v1 = f1['Velocity'][i1]
            r = (v1 - v0) / (f1['t'] - f0['t']) + k * v0; r[:, 2] += g
            acc.append(np.linalg.norm(r, axis=1))
        if acc:
            a = float(np.median(np.concatenate(acc))) / 100
            over['Turbulence3DSpeed'] = f(a, 2)
            enote = (' Its vector field (%s, curl noise) pushes each particle along curving paths, %.2f m/s2 median: KCD2'
                     ' cannot sample that field, so Turbulence3DSpeed %s stands in (random turbulence, not the same paths)'
                     % (E.vf.get('name', 'TilingCurl32'), a, f(a, 2)))
    return build(frames, b, rend, M, mat, em, name, nmin, nmax, override=over, extra_note=enote)


def build(frames, b, rend, M, mat, em, name, nmin, nmax, axis=2, vkey='Velocity', head=None, children='', depth=3,
          override=None, hidden=False, extra_note=''):
    """one KCD2 <Particles> from simulated frames. axis: the effect's local axis that becomes the KCD2 emitter's y
    (2 = +Z, the hit normal; 0 = +X, the outgoing direction of a ricochet). vkey: the velocity to read the emission
    from ('OwnVelocity' for a child that inherits its parent's). head: params written first (Count, Continuous...).
    children: nested <Particles> (SpawnIndirection children)."""
    frames = [fr for fr in frames if fr['n']] or frames
    st = ns_stats.measure(frames, b, rend)
    f0 = frames[0]
    p = {}
    if head:
        p.update(head)
    else:
        p['Count'] = one_sided(nmin, nmax, 2)
        if b.get('time'): p['SpawnDelay'] = f(b['time'])
    p['ParticleLifeTime'] = one_sided(*pct(f0['Lifetime'][:, 0], 0, 100))
    # direction and speed
    v0 = f0[vkey] if vkey in f0 else f0['Velocity']; sp = np.linalg.norm(v0, axis=1) / 100
    if sp.max() > 0.05:
        ang = np.degrees(np.arccos(np.clip(v0[:, axis] / np.maximum(sp * 100, 1e-6), -1, 1)))
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
    if M.get('tiling'): p['TextureTiling'] = M['tiling']   # frames / variants baked by age (mk_ron_clip_tex.py)
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
    elif M.get('baked_color'):   # the material saturates texture x colour: baked into the texture
        p['Color'] = '(x=1,y=1,z=1)'
        p['DiffuseLighting'] = '1'; p['DiffuseBacklighting'] = f(M.get('back', 0)); p['EmissiveLighting'] = '0'
        clo = chi = 1.0; vec = np.ones_like(vec)
    else:
        rgb = base_rgb * chi * M.get('color_mul', 1.0)
        if rgb.max() > 1.001: raise SystemExit('%s: lit colour above 1 (%s): the material saturates it, bake it (--matover)' % (em, rgb))
        p['Color'] = '(x=%s,y=%s,z=%s)' % tuple(f(x) for x in rgb)
        p['DiffuseLighting'] = '1'; p['DiffuseBacklighting'] = f(M.get('back', 0)); p['EmissiveLighting'] = '0'
    if chi > 0 and 1 - clo / chi >= 0.02: p['Color'] += ',Random=' + f(1 - clo / chi, 2)
    if np.abs(vec - vec[:, :1]).max() < 0.03:          # the same curve on all three: write it once, as a grey
        t2, v2 = simplify(ts, vec.mean(1))
        if not np.all(np.abs(v2 - 1) < 0.02): p['Color'] += ',' + age_curve(t2, None, vec=[(v, v, v) for v in v2])
    else:
        p['Color'] += ',' + age_curve(ts, None, vec=[tuple(r) for r in vec])
    Ca = Ca * M.get('alpha_mul', 1.0)
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
    if M.get('dither'):   # threshold texture (mk_ron_fx_tex.py, glass chunks): SourceMin 1 -> 0 over the alpha
        p['AlphaClip'] = 'Scale=(Min=1,Max=1),SourceMin=(Min=1,Max=0),SourceWidth=(Min=0.167,Max=0.167)'
        if over: p['Alpha'] = one_sided(apk * alo, min(ahi_abs, 1.0))
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
    if M.get('pivot') is not None: p['PivotY'] = f(M['pivot'])
    p.update(override or {})
    if hidden:   # kept only so its children have a particle to ride on
        note += ' Drawn invisible (Alpha 0, RoN %s): it only carries its children' % p.get('Alpha')
        p['Alpha'] = '0'; p.pop('AlphaClip', None)
    note += extra_note
    attrs = ' '.join('%s="%s"' % kv for kv in p.items())
    t = '\t' * depth
    kids = ('%s\t<Childs>\n%s%s\t</Childs>\n' % (t, children, t)) if children else ''
    return ('%s<Particles Name="%s">\n%s\t<!-- %s -->\n%s\t<Params %s />\n%s%s</Particles>\n' % (t, name, t, note, t, attrs, kids, t))


def cascade_xml(sysname, samples, mats, axis=0, hide=(), skip='LOW_SETTINGS'):
    """a RoN Cascade system (tools/ron/cascade_sim.py) -> <Particles> children. Location-emitter chains become KCD2
    children with SpawnIndirection ParentStart (a child emitter per parent particle, at it, moving with it) and
    InheritVelocity; a rate-spawned emitter becomes Continuous over its EmitterDuration (KCD2's Count = particles alive
    at once = rate x particle life). hide: emitters kept only as carriers for their children (Alpha 0)."""
    import cascade_sim as cs
    ems = cs.load(sysname)
    lives = []
    for E in ems:
        if not E.enabled: continue
        fr = E.run(0.0, 2, dt=1 / 60)
        if fr and fr[0]['n']: lives.append(float(fr[0]['Lifetime'].min()))
    dt = float(np.clip(min(lives or [1.0]) / 30, 1 / 1200, 1 / 60))
    order = sorted(range(len(ems)), key=lambda i: cs.depth(ems, i))
    by, runs = {}, {}
    for i in order:
        E = ems[i]
        if not E.enabled or (skip and re.search(skip, E.name or '')): continue
        fr = E.run(0.0, 2, dt=dt, seed=i + 1, sources=by)
        lmax = float(fr[0]['Lifetime'].max()) if fr and fr[0]['n'] else 3.0
        fr = E.run(min(lmax, 3.0) + (E.req.get('EmitterDuration') or 0) + 4 * dt, samples, dt=dt, seed=i + 1, sources=by)
        by[E.name] = fr; runs[E.name] = (i, E, fr)

    def per_burst(fr):
        ids = {}
        for x in fr:
            if x['n']:
                for b, u in zip(x['Burst'][:, 0].tolist(), x['UniqueID'][:, 0].tolist()): ids.setdefault(b, set()).add(u)
        return np.mean([len(v) for v in ids.values()]) if ids else 0.0

    def one(name, depth):
        i, E, fr = runs[name]
        rend = E.renderer(); mat = (rend.get('Material') or '').split('.')[-1]
        if mat not in mats: raise SystemExit('%s: material %s is not in MATS; decode it first' % (name, mat))
        M = dict(mats[mat]); src = cs.source_of(E)
        if E.pivot: M['pivot'] = -2 * float(E.pivot[1])   # UE pivot offset (0, 0.5) = half a sprite back: KCD2 PivotY -1 (sign unverified)
        bl = E.spawn.get('BurstList') or []
        lo = sum((x.get('CountLow') if (x.get('CountLow') or -1) >= 0 else x.get('Count', 0)) for x in bl)
        hi = sum(x.get('Count', 0) for x in bl)
        head = {}
        rate = (cs.table(E.spawn.get('Rate')) or {}).get('Values') or [0]
        nparent = per_burst(runs[src][2]) if src in runs else 1.0
        if hi:
            head['Count'] = one_sided(max(lo, 1), hi, 2)
            t0 = min((x.get('Time') or 0) for x in bl) * (E.req.get('EmitterDuration') or 1.0)
            if t0: head['SpawnDelay'] = f(t0)
        elif max(rate) > 0:
            life = float(np.mean([x['Lifetime'].mean() for x in fr if x['n']] or [0.1]))
            rlo, rhi = min(rate), max(rate)
            # RoN spreads one rate over all its source's particles; KCD2 runs one child emitter per parent particle
            head['Continuous'] = 'true'
            head['Count'] = one_sided(rlo * life / nparent, rhi * life / nparent, 2)
            head['EmitterLifeTime'] = f(E.req.get('EmitterDuration') or 1.0)
        if src:
            head['SpawnIndirection'] = 'ParentStart'
            locem = next(m for m in E.mods if m['cls'] == 'ParticleModuleLocationEmitter')
            if locem.get('InheritSourceVelocity'): head['InheritVelocity'] = f(locem.get('InheritSourceVelocityScale') or 1.0)
        kids = ''.join(one(c, depth + 2) for c in runs if cs.source_of(runs[c][1]) == name)
        n = (E.name or 'emitter').lower()
        # Cascade has no built-in gravity: its Acceleration module (world space) is it. A uniform random range gives
        # each particle its own constant acceleration; KCD2's is one vector, so the mean goes in (GravityScale) and the
        # spread can't (noted)
        over, enote = {}, ''
        accm = next((m for m in E.mods if m['cls'] == 'ParticleModuleAcceleration'), None)
        if accm:
            tab = cs.table(accm['Acceleration']); v = np.array(tab['Values'], float)
            lo3, hi3 = (v[:3], v[3:6]) if tab.get('Op') == 2 else (v[:3], v[:3])
            mean = (lo3 + hi3) / 2
            over['GravityScale'] = f(-mean[2] / 980.665, 2)
            if np.any(np.abs(mean[:2]) > 1): over['Acceleration'] = 'x=%s,y=%s' % (f(mean[0] / 100), f(mean[1] / 100))
            if np.any(hi3 - lo3 > 1):
                enote = ' Its random acceleration (+-%s m/s2 per particle) is not reproduced: KCD2 has one vector' % f(float(np.max(hi3 - lo3)) / 200, 1)
        elif not any(m['cls'] == 'ParticleModuleAccelerationOverLifetime' for m in E.mods):
            over['GravityScale'] = '0'
        if name in hide:   # an invisible carrier only has to outlive its children's emission + their particles
            need = 0.0
            for c in runs:
                if cs.source_of(runs[c][1]) == name:
                    cf = runs[c][2]
                    need = max(need, (runs[c][1].req.get('EmitterDuration') or 0) + max((float(x['Lifetime'].max()) for x in cf if x['n']), default=0))
            if need:
                over['ParticleLifeTime'] = f(need)
                enote += ' Lives %s s here (RoN %.2f s): its children are done by then' % (f(need), float(fr[0]['Lifetime'].max()) if fr[0]['n'] else 0)
        return build(fr, {'min': lo, 'max': hi}, rend, M, mat, E.name, n, max(lo, 1), max(hi, 1), axis=axis,
                     vkey='OwnVelocity', head=head, children=kids, depth=depth, override=over, hidden=name in hide,
                     extra_note=enote)
    return ''.join(one(nm, 3) for nm in runs if cs.source_of(runs[nm][1]) is None)


def flashref_frames(kinds, shots, dt, seed=7):
    """frames (ns_stats layout, t = particle age) of the given flash_ref.py particle kinds: spawned by its spawn_shot
    and stepped by its rules (docs/ron/cascade_semantics.md: drag c redrawn every step, the per-particle random
    acceleration, size by speed then the life curve, sheet frames redrawn every step), at dt."""
    import flash_ref as fr
    rng = np.random.default_rng(seed)
    ps = []
    for k in range(shots):
        for q in fr.spawn_shot(rng, pop=False):
            if q.kind in kinds:
                q.shot = k; q.uid = len(ps); q.vel0 = q.vel.copy()
                q.sub0 = q.sub[1] if q.sub[0] == 'fixed' else int(rng.uniform(q.sub[1], q.sub[2]))
                ps.append(q)
    frames, t = [], 0.0
    life = max(q.life for q in ps)
    while t <= life + dt:
        live = [q for q in ps if q.age < q.life]
        if not live: break
        rows = []
        for q in live:
            r = q.age / q.life
            size = np.array(q.size, float)
            if q.sbs and t > 0: size[1] = abs(size[1]) * min(max(0.05 * np.linalg.norm(q.vel), 1), 100)
            size = size * fr.lerp_curve(q.mult, r)
            colour = q.colour if q.colour is not None else (q.colour_rng[0] + q.colour_rng[1]) / 2
            rows.append((q, size, colour, fr.lerp_curve(q.alpha, r)))
        frames.append({'t': t, 'n': len(rows),
                       'Position': np.array([q.pos for q, *_ in rows]), 'Velocity': np.array([q.vel for q, *_ in rows]),
                       'OwnVelocity': np.array([q.vel0 for q, *_ in rows]),
                       'SpriteSize': np.array([sz[:2] for _, sz, _, _ in rows]),
                       'Color': np.array([np.r_[c, a] for _, _, c, a in rows]),
                       'Lifetime': np.array([[q.life] for q, *_ in rows]), 'Age': np.array([[q.age] for q, *_ in rows]),
                       'NormalizedAge': np.array([[q.age / q.life] for q, *_ in rows]),
                       'SpriteRotation': np.array([[q.rot] for q, *_ in rows]), 'SubImageIndex': np.array([[q.sub0] for q, *_ in rows], float),
                       'UniqueID': np.array([[q.uid] for q, *_ in rows]), 'Burst': np.array([[q.shot] for q, *_ in rows])})
        # one step, flash_ref.step's rules
        for q in live:
            if q.drag: q.vel = q.vel * (1 - rng.uniform(2.5, 15) * dt)
            q.vel = q.vel + q.acc * dt
            q.pos = q.pos + q.vel * dt
            q.age += dt
        t += dt
    return frames


def flash_sparks_xml(samples, mats, kinds=('spark',)):
    """WeaponFlash_Pistol_V3's Sparks (and, on request, LongSpark / CoreSpark) -> <Particles> children. Claude 1's
    flash plan: the pops / core / flame and the 0.03 s LongSpark / CoreSpark go on the frame-exact cards; only the
    Sparks are particles spawned on the shot. RoN's +X = the barrel = the KCD2 emitter y. Born at 0.01 s. Sparks
    simulate in world space; LongSpark and CoreSpark in the gun's (local)."""
    M = dict(mats['M_Particle_Additive'])
    out = []
    spec = [  # kind, name, RoN emitter, burst (min, max), alignment, overrides
        ('spark', 'sparks', 'Sparks', (0, 10), 'VelocityAligned',
         {'TextureTiling': 'TilesX=4,TilesY=2,VariantCount=8', 'MoveRelativeEmitter': 'false', 'PivotY': '-0.3',
          'RandomOffset': 'x=0.005,y=0.005,z=0.005', 'GravityScale': '0'},
         ' Count: Cascade burst CountLow 0 / Count 10 = CountLow + round(SRand x 10), 0-10 a shot (flash_ref.py spawns'
         ' 10). Sheet frame re-drawn every frame in RoN: one random frame per particle here. Drag c redrawn every frame'
         ' U(2.5, 15): its mean. Random acceleration +-30 m/s2 per axis per particle: KCD2 has one vector, mean 0.'
         ' Length = base x clamp(0.05 |v|, 1, 100) x the life curve: the mean over particles here. Pivot (0, 0.15):'
         ' centre 0.15 of the length behind the particle (stock UE shader maths), PivotY sign unverified. Orbit +-0.5 cm.'
         ' Inherits 5x the gun velocity in RoN (KCD2 free emitter: none).'),
        ('longspark', 'longspark', 'LongSpark', (1, 1), 'VelocityAligned',
         {'TextureTiling': 'TilesX=4,TilesY=2,VariantCount=3', 'MoveRelativeEmitter': 'true', 'PivotY': '-1',
          'GravityScale': '0', 'Speed': '0.05', 'EmitAngle': '180'},
         ' Velocity -5 cm/s (back along the barrel), so it lies along the barrel; pivot (0, 0.5) puts it ahead of the'
         ' muzzle (PivotY sign unverified). Frames 0-2 re-drawn every frame in RoN: one per particle here.'),
        ('corespark', 'corespark', 'CoreSpark', (1, 1), None,
         {'TextureTiling': 'TilesX=4,TilesY=2,FirstTile=7,VariantCount=1', 'MoveRelativeEmitter': 'true',
          'PositionOffset': 'y=0.1', 'PivotY': '-0.18', 'GravityScale': '0'},
         ' PSA_Rectangle: faces the camera, width x, height y. 10 cm down the barrel; pivot (0, 0.09).'),
    ]
    for kind, name, em, (lo, hi), align, over, enote in spec:
        if kind not in kinds: continue
        dt = 1 / 2000.0
        frames = flashref_frames({kind}, samples, dt)
        rend = {'Alignment': align, 'Material': 'M_Particle_Additive', 'bSubImageBlend': False}
        head = {'Count': one_sided(max(lo, 1), hi, 2) if lo else str(hi), 'SpawnDelay': '0.01'}
        if lo == 0: head['Count'] = '%d,Random=1' % hi
        if kind == 'spark': over = dict(over, AirResistance='8.75,WindScale=0')
        out.append(build(frames, {'min': lo, 'max': hi}, rend, M, 'M_Particle_Additive', em, name, max(lo, 1), hi,
                         axis=0, vkey='OwnVelocity', head=head, override=over,
                         extra_note=enote + ' Brightness: RoN HDR / 1000 (the ns2pfx scale); on it the flash cards\' pop core is 38.25.'))
    return ''.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir'); ap.add_argument('effect')
    ap.add_argument('--off', default='', help='old layer names to list switched off (fxreload keeps missing ones)')
    ap.add_argument('--samples', type=int, default=48)
    ap.add_argument('--into', help='library XML: replace the effect\'s top-level block there (or add it), old layer names off')
    ap.add_argument('--note', default='', help='a line for the comment above the effect (which surfaces use it)')
    ap.add_argument('--matover', default='{}', help='JSON {material: {key: value}} merged into MATS for this effect '
                    '(a system whose particle colour makes a saturating material need its own baked texture)')
    ap.add_argument('--flash-sparks', action='store_true', help='WeaponFlash_Pistol_V3\'s Sparks / LongSpark / CoreSpark from '
                    'tools/ron/flash_ref.py (dir: ignored, give -)')
    ap.add_argument('--cascade', action='store_true', help='dir is a Cascade system name (tools/ron/cascade_sim.py), not an extract dir')
    ap.add_argument('--axis', default='z', choices='xz', help='the RoN local axis that becomes the KCD2 emitter y: z = the hit normal, x = a ricochet\'s outgoing direction')
    ap.add_argument('--hide', default='', help='emitters drawn invisible, kept as carriers for their children (comma list)')
    ap.add_argument('--skip', default='LOW_SETTINGS',
                    help='emitters left out (regex): RoN draws either its HIGH or its LOW debris, by quality setting')
    a = ap.parse_args()
    for k, v in json.loads(a.matover).items(): MATS[k] = {**MATS.get(k, {}), **v}
    out = ['\t<Particles Name="%s">\n\t\t<Params Enabled="false" Count="1" ParticleLifeTime="0.05" />\n\t\t<Childs>\n' % a.effect]
    names = []
    if a.flash_sparks:
        body = flash_sparks_xml(a.samples, MATS)
        out.append(body); names = re.findall(r'<Particles Name="([^"]+)">', body)
        sysd = {'emitters': {}}
    elif a.cascade:
        body = cascade_xml(a.dir, a.samples, MATS, axis={'x': 0, 'z': 2}[a.axis], hide=[x for x in a.hide.split(',') if x], skip=a.skip)
        out.append(body); names = re.findall(r'<Particles Name="([^"]+)">', body)
        sysd = {'emitters': {}}
    else:
        sysd = json.load(open(os.path.join(a.dir, 'system.json')))
    for em in sysd['emitters']:
        if not os.path.exists(os.path.join(a.dir, em + '.asm')): continue
        if a.skip and re.search(a.skip, em): continue
        if 'Mesh' in str(sysd['emitters'][em].get('renderer', {}).get('cls')):
            out.append('\t\t\t<!-- %s: a mesh emitter (RoN static meshes), not converted -->\n' % em); continue
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
    src = ("WeaponFlash_Pistol_V3's Sparks, LongSpark and CoreSpark, simulated with the rules read from its exe "
           "(tools/ron/flash_ref.py, docs/ron/cascade_semantics.md), converted by tools/ron/ns2pfx.py in its flash-sparks mode"
           if a.flash_sparks else '%s, measured by running its own shaders (tools/ron/ns2pfx.py)' % os.path.basename(os.path.normpath(a.dir)))
    head = '\t<!-- %s: Ready or Not\'s %s%s -->\n' % (a.effect, src, ('. ' + a.note) if a.note else '')
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
