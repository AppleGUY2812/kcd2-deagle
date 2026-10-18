#!/usr/bin/env python3
"""Ready or Not Cascade gun smoke -> KCD2 particle effects (pfx1 XML), every number read from RoN's assets
(docs/ron/gun_smoke_shells.md), none typed in by hand.

    python3 -I tools/ron/casc2pfx.py [--print-spec]

Prints the <Particles> blocks for src/Libs/Particles/deagle.xml (library deagle):
  muzzle_smoke_ron  P_WeaponSmoke_M2011_Muzzle, its 4 running emitters (ScreenPuff, BigPuff, TinyPuff, Puff Left):
                    spawned on the muzzle every shot, its y = the barrel
  shell_puff_ron    P_WeaponSmoke_M2011_Ejection (Puff Left): spawned on the ejection port at every casing, y = the
                    casing's throw direction
  barrel_smoke_ron  P_BarrelSmokeTrail's LingerSmoke (the Grizzly's HeatSmoke, run 5x fast: CustomTimeDilation 5), the
                    only one of its emitters both flags switch on: kept on the muzzle by the plugin (smokefx), y = the
                    barrel; its particles stay in the world (see linger() for what that changes)

Mapping (KCD2 facts in MODLOG, conventions shared with tools/ron/ns2pfx.py):
- frame: RoN's component +X is the barrel (resp. the throw), after the emitter's EmitterRotation; KCD2's emitter y is
  the direction it is spawned with. Both effects are world space in RoN (no bUseLocalSpace), as KCD2's free emitters.
- one-sided Random: Base = max, Random = 1 - min / max (CryEngine draws Base x (1 - Random x rand)).
- Size = half the sprite width in m (Cascade sizes are full widths in cm). Camera-facing (PSA_Square) sprites use X only.
  Velocity-aligned (PSA_Velocity) sprites: Facing CameraX + OrientToVelocity, Size = half the length along the flight
  (Cascade Y) and Aspect = width / length (MODLOG 2026-10-14).
- curves over particle age (Cascade lookup tables, sample i at t = i / TimeScale) are keyed and thinned to within 3 %
  of the peak (the same Douglas-Peucker as ns2pfx); CryEngine curves are unit floats, so a curve is divided by its
  peak and the peak goes into the base.
- flipbook: SubImageIndex over life -> TextureTiling AnimCurve (frame / (frames - 1)) with AnimFramerate 0 (one
  cycle a life), AnimBlend (Cascade Linear_Blend). Random U flip -> FlipChance 0.5 (KCD2 has no V flip).
- lit translucent materials (M_Particle_LitTransparent): Color = the particle colour, DiffuseLighting 1,
  DiffuseBacklighting 1 (the translucent 'volumetric non-directional' lighting, lit alike from both sides), and the
  material's depth fade -> SoftParticle.
- AccelerationDrag DragCoefficientRaw -> AirResistance (both: velocity loses that share a second).
Not carried over (KCD2 can't, or not known), each listed in the output's comment:
- VelocityInheritParent (x0.2 / x0.5 of the gun's velocity): a KCD2 free emitter has no velocity.
- alpha threshold (the material's 0.025 / 0.01 cut): KCD2's AlphaClip works on the texture, not the particle alpha.
- the spawn module's rate of 1 /s on top of the burst: whether it adds a second puff depends on Cascade's default
  emitter duration, not read (docs/ron/gun_smoke_shells.md "Not found").
"""
import argparse, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from usmap import U  # noqa: E402
from dump import OUT, USMAP, class_schema  # noqa: E402
from uasset import Pkg, R  # noqa: E402

TEX = 'textures/particles/deagle/'
# RoN material -> KCD2 texture (tools/ron/mk_gun_smoke_tex.py), its flipbook, how it's lit
MATS = {
    'MI_Muzzle_Puff_Front': dict(tex='gun_ron_puff_front_diff', tiles=(8, 4)),
    'MI_Muzzle_puff_Side': dict(tex='gun_ron_puff_side_diff', tiles=(8, 4)),
    'MI_Muzzle_Puff_Side': dict(tex='gun_ron_puff_side_diff', tiles=(8, 4)),
    'MI_WispySmoke': dict(tex='gun_ron_wispy_diff', tiles=(8, 4)),
}
BARREL = ('ReadyOrNot/Content/ReadyOrNot/VFX/VFXBarrel/P_BarrelSmokeTrail', 5.0)   # the Grizzly's HeatSmoke CustomTimeDilation
SYSTEMS = {
    'muzzle_smoke_ron': ('ReadyOrNot/Content/ReadyOrNot/Assets/Particles/WeaponFlash/Particle/Gunsmoke/fx/P_WeaponSmoke_M2011_Muzzle',
                         ['ScreenPuff', 'BigPuff', 'TinyPuff', 'Puff Left']),
    'shell_puff_ron': ('ReadyOrNot/Content/ReadyOrNot/Assets/Particles/WeaponFlash/Particle/Gunsmoke/fx/P_WeaponSmoke_M2011_Ejection',
                       ['Puff Left']),
}


def f(x, n=3):
    s = ('%.' + str(n) + 'f') % x
    s = s.rstrip('0').rstrip('.') if '.' in s else s
    return '0' if s in ('-0', '') else s


def one_sided(lo, hi, n=3):
    """CryEngine 'Base,Random=R' covering [lo, hi]"""
    if hi <= 1e-9: return '0'
    r = 1 - lo / hi
    return f(hi, n) + (',Random=' + f(r, 2) if r >= 0.02 else '')


def simplify(ts, vs, tol=0.03):
    keep = [0, len(ts) - 1]
    def rec(a, b):
        if b - a < 2: return
        seg = vs[a] + (vs[b] - vs[a]) * (ts[a + 1:b] - ts[a]) / (ts[b] - ts[a])
        err = np.abs(vs[a + 1:b] - seg); k = int(np.argmax(err))
        if err[k] > tol: keep.append(a + 1 + k); rec(a, a + 1 + k); rec(a + 1 + k, b)
    rec(0, len(ts) - 1)
    keep = sorted(set(keep))
    return ts[keep], vs[keep]


def age_curve(ts, vs):
    """ParticleAge=(...) from unit-peak keys; None if flat"""
    ts2, vs2 = simplify(np.asarray(ts, float), np.asarray(vs, float))
    if np.all(np.abs(vs2 - 1) < 0.02): return None
    return '(' + ';'.join(('v=' + f(v)) if t == 0 else ('t=%s,v=%s' % (f(t, 2), f(v))) for t, v in zip(ts2, vs2)) + ')'


def rot(pyr, v):
    """Unreal FRotator (pitch, yaw, roll) degrees applied to a vector (left-handed: X fwd, Y right, Z up)"""
    p, y, r = np.radians(pyr)
    cp, sp, cy, sy, cr, sr = np.cos(p), np.sin(p), np.cos(y), np.sin(y), np.cos(r), np.sin(r)
    # FRotationMatrix rows: X axis, Y axis, Z axis of the rotated frame
    X = np.array([cp * cy, cp * sy, sp])
    Y = np.array([sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp])
    Z = np.array([-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp])
    return v[0] * X + v[1] * Y + v[2] * Z


def table(d):
    """a Cascade raw distribution -> (times, samples [n, stride]) from its baked lookup table"""
    t = d.get('Table') or {}
    vals = np.asarray(t.get('Values') or [], float)
    stride = int(t.get('EntryStride') or 1)
    n = int(t.get('EntryCount') or (len(vals) // max(stride, 1)))
    vals = vals[:n * stride].reshape(n, stride)
    ts = t.get('TimeScale') or 0
    times = np.arange(n) / ts if ts else np.zeros(n)
    return times + float(t.get('TimeBias') or 0), vals


def minmax(d, dims):
    """a uniform (or constant) start value -> (min, max) arrays, from the baked table (it holds locked axes as baked:
    a size locked to X has X in all three), else from Min/MaxValue(Vec)"""
    _, v = table(d)
    if len(v) and v.shape[1] == 2 * dims: return v[0, :dims].copy(), v[0, dims:].copy()
    if len(v) and v.shape[1] == dims: return v[0].copy(), v[0].copy()
    if dims == 1: return np.array([d.get('MinValue', 0.0)]), np.array([d.get('MaxValue', 0.0)])
    return np.asarray(d.get('MinValueVec'), float), np.asarray(d.get('MaxValueVec'), float)


def load(path):
    u = U(USMAP)
    p = Pkg(OUT + '/' + path, u); p.fullrefs = True
    mods = {}
    for i, e in enumerate(p.exports):
        n, ex = class_schema(p, e['cls'], u)
        mods[p.ref(i + 1)] = (p.ref(e['cls']), p.props(R(p.data(e)), n, ex))
    paths = {n: p.imports[-o - 1][1].split('/')[-1] for c, n, o in p.imports if o < 0}
    return mods, paths


def emitter(mods, paths, name):
    for k, (c, v) in mods.items():
        if c != 'ParticleSpriteEmitter' or v.get('EmitterName') != name: continue
        L = mods.get((v.get('LODLevels') or [None])[0], (None, {}))[1]
        req = mods.get(L.get('RequiredModule'), (None, {}))[1]
        if req.get('bEnabled') is False or L.get('bEnabled') is False: continue   # a switched-off twin of the same name
        spawn = mods.get(L.get('SpawnModule'), (None, {}))[1]
        ms = {}
        for m in L.get('Modules') or []:
            mc, mv = mods.get(m, (None, {}))
            if mc and mv.get('bEnabled', True) is not False: ms[mc.replace('ParticleModule', '')] = mv
        mat = req.get('Material'); mat = paths.get(mat, mat)
        return dict(req=req, spawn=spawn, ms=ms, mat=mat)
    raise SystemExit('no running emitter named %r' % name)


def convert(E, label):
    req, ms, sp = E['req'], E['ms'], E['spawn']
    if E['mat'] not in MATS: raise SystemExit('%s: material %s not in MATS' % (label, E['mat']))
    M = MATS[E['mat']]
    erot = req.get('EmitterRotation') or (0.0, 0.0, 0.0)
    fwd = lambda v: rot(erot, np.asarray(v, float))   # emitter local -> component (X = the barrel / throw)
    vel_aligned = req.get('ScreenAlignment') == 'PSA_Velocity'
    p, notes = {}, []
    bursts = sp.get('BurstList') or []
    p['Count'] = str(int(max(b.get('Count') or 1 for b in bursts))) if bursts else '1'
    lo, hi = minmax(ms['Lifetime']['Lifetime'], 1)
    p['ParticleLifeTime'] = one_sided(lo[0], hi[0])
    if 'Location' in ms:
        a, b = minmax(ms['Location']['StartLocation'], 3)
        c = fwd((a + b) / 2) / 100
        p['PositionOffset'] = 'x=%s,y=%s,z=%s' % (f(c[1]), f(c[0]), f(c[2]))
    v = np.zeros(3)
    if 'Velocity' in ms:
        a, b = minmax(ms['Velocity']['StartVelocity'], 3)
        v = fwd((a + b) / 2) / 100
        cone = 0.0
    if 'VelocityCone' in ms:
        vc = ms['VelocityCone']
        clo, chi = minmax(vc['Angle'], 1); slo, shi = minmax(vc['Velocity'], 1)
        d = fwd(np.asarray(vc.get('Direction') or (1, 0, 0), float))
        v = d / np.linalg.norm(d) * shi[0] / 100
        cone = chi[0] * 180   # Cascade cone: 1 = 180 deg
        p['Speed'] = one_sided(slo[0] / 100, shi[0] / 100)
        p['EmitAngle'] = one_sided(clo[0] * 180, chi[0] * 180, 1) if chi[0] > 0 else '0'
    elif np.linalg.norm(v) > 1e-6:
        if abs(v[1]) > 1e-6 or abs(v[2]) > 1e-6: raise SystemExit('%s: start velocity off the barrel axis: %s' % (label, v))
        p['Speed'] = f(abs(v[0]))
        if v[0] < 0: raise SystemExit('%s: start velocity points back up the barrel' % label)
    if 'VelocityInheritParent' in ms: notes.append('inherits %s of the gun velocity in RoN (KCD2: none)' % f(minmax(ms['VelocityInheritParent']['Scale'], 3)[1][0]))
    if 'AccelerationDrag' in ms:
        dr = ms['AccelerationDrag'].get('DragCoefficientRaw')
        p['AirResistance'] = f(minmax(dr, 1)[1][0]) if isinstance(dr, dict) else f(dr)
    # size: Cascade full widths in cm; velocity-aligned: X across, Y along the flight
    a, b = minmax(ms['Size']['StartSize'], 3)
    mul_t, mul = np.array([0.0]), np.ones((1, 3))
    if 'SizeMultiplyLife' in ms:
        mul_t, mv = table(ms['SizeMultiplyLife']['LifeMultiplier'])
        mul = mv[:, :3]
    if vel_aligned:
        length = mul[:, 1] * 1.0; width = mul[:, 0] * 1.0
        Lpk = length.max(); Wpk = width.max()
        p['Facing'] = 'CameraX'; p['OrientToVelocity'] = 'true'
        size_curve = age_curve(mul_t, length / Lpk)
        p['Size'] = one_sided(a[1] * Lpk / 200, b[1] * Lpk / 200) + (',ParticleAge=' + size_curve if size_curve else '')
        asp = (width * (a[0] + b[0]) / 2) / (length * (a[1] + b[1]) / 2)
        acur = age_curve(mul_t, asp / asp.max())
        p['Aspect'] = f(asp.max()) + (',ParticleAge=' + acur if acur else '')
    else:
        pk = mul[:, 0].max()
        size_curve = age_curve(mul_t, mul[:, 0] / pk)
        p['Size'] = one_sided(a[0] * pk / 200, b[0] * pk / 200) + (',ParticleAge=' + size_curve if size_curve else '')
    # colour and alpha over life (lit: the particle colour)
    col = ms.get('ColorOverLife', {})
    _, cv = table(col.get('ColorOverLife', {}))
    c0 = cv[0, :3] if len(cv) else np.ones(3)
    p['Color'] = '(x=%s,y=%s,z=%s)' % tuple(f(x) for x in c0)
    at, av = table(col.get('AlphaOverLife', {}))
    av = av[:, 0]
    apk = av.max()
    acur = age_curve(at, av / apk) if len(av) > 1 else None
    p['Alpha'] = f(apk) + (',ParticleAge=' + acur if acur else '')
    # flipbook
    tx, ty = M['tiles']
    frames = tx * ty
    tile = 'TilesX=%d,TilesY=%d,AnimFramesCount=%d,AnimBlend=true' % (tx, ty, frames)
    if 'SubUV' in ms:
        st, sv = table(ms['SubUV']['SubImageIndex'])
        sv = sv[:, 0]
        if len(sv) > 1:
            tt = st / st[-1]
            cur = age_curve(tt, np.clip(sv / (frames - 1), 0, 1))
            if cur: tile += ',AnimCurve=' + cur
            first = sv[0] / (frames - 1)
            if first > 0.01: notes.append('flipbook starts at frame %d' % round(sv[0]))
    tile += ',FlipChance=0.5'
    p['Texture'] = TEX + M['tex'] + '.tif'
    p['TextureTiling'] = tile
    if 'Rotation' in ms:
        rlo, rhi = minmax(ms['Rotation']['StartRotation'], 1)
        if abs(rhi[0] - rlo[0]) < 1e-6:
            p['InitAngles'] = 'y=%s' % f(rlo[0] * 360)
        else:
            p['RandomAngles'] = 'y=%s' % f(min(180, (rhi[0] - rlo[0]) * 180))
            mid = (rhi[0] + rlo[0]) / 2 * 360
            if abs(mid) > 0.5 and abs(abs(mid) - 180) > 0.5: p['InitAngles'] = 'y=%s' % f(mid)
    if 'PivotOffset' in ms:
        # Cascade: the emitter's pivot starts at (-0.5, -0.5) (the centre: vertex = (UV + pivot) x size, UV 0..1) and
        # the module adds its offset (UE source, UParticleEmitter::CacheEmitterModuleInfo; not read from RoN's exe), so
        # +-0.5 is an edge; KCD2's PivotX / PivotY run -1..1 edge to edge: x2. The sign of KCD2's PivotY against
        # Cascade's V is not checked yet (in game, on the velocity-aligned puffs where it decides which end the puff
        # grows from).
        pu, pv = ms['PivotOffset'].get('PivotOffset') or (0, 0)
        if abs(pu) > 1e-6: p['PivotX'] = f(2 * pu)
        if abs(pv) > 1e-6: p['PivotY'] = f(2 * pv)
        notes.append('pivot %s x2, PivotY sign unchecked in game' % ((pu, pv),))
    p['SoftParticle'] = 'true'
    p['DiffuseLighting'] = '1'; p['DiffuseBacklighting'] = '1'; p['EmissiveLighting'] = '0'
    p['CameraMinDistance'] = '0.05'
    return p, notes


def linger(mods, paths, dil):
    """P_BarrelSmokeTrail's LingerSmoke at RoN's time dilation dil (asset seconds / dil = real seconds; speeds x dil,
    accelerations x dil^2, drag and rates x dil). Modelled, each in the output's comment: SizeScaleBySpeed (size x
    clamp(0.1 x speed in asset cm/s, 1, 2)) becomes part of the size curve for a mean particle (life 6.5 asset s, up
    acceleration 1.25 cm/s^2, the rms of the random sideways one); SourceMovement (particles follow the gun x1 -> 0.15)
    has no KCD2 match: the particles stay where they are born (MoveRelativeEmitter false) while the emitter rides the
    muzzle; StartAlpha by emitter time -> Alpha's EmitterStrength curve (KCD2 bakes it at a particle's birth, as Cascade
    does; that a free emitter's strength is its age is CryEngine's rule, unchecked in KCD2)."""
    E = emitter(mods, paths, 'LingerSmoke')
    req, ms, sp = E['req'], E['ms'], E['spawn']
    M = MATS[E['mat']]
    p, notes = {}, []
    dur = float(req.get('EmitterDuration')) / dil
    rate = float(minmax(sp['Rate'], 1)[1][0]) * dil
    lo, hi = minmax(ms['Lifetime']['Lifetime'], 1)
    lo, hi = lo[0] / dil, hi[0] / dil
    p['Continuous'] = 'true'
    p['Count'] = f(rate * hi, 1)                                   # KCD2: continuous rate = Count / ParticleLifeTime
    p['EmitterLifeTime'] = f(dur)
    if req.get('EmitterDelay'): p['SpawnDelay'] = f(float(req['EmitterDelay']) / dil)
    p['ParticleLifeTime'] = one_sided(lo, hi)
    p['MoveRelativeEmitter'] = 'false'
    # speed: the local start velocity, turned by EmitterRotation, is down the barrel. HeatSmoke sits on ItemMesh with
    # no rotation of its own (unlike MuzzleSmoke, yawed 90), so the barrel is the component's +Y here
    loc = [m for k, m in ms.items() if k == 'Velocity']
    a, b = minmax(loc[0]['StartVelocity'], 3)
    v = rot(req.get('EmitterRotation') or (0, 0, 0), (a + b) / 2)
    if abs(v[0]) > 1e-3 or abs(v[2]) > 1e-3 or v[1] <= 0: raise SystemExit('LingerSmoke: start velocity not down the barrel: %s' % v)
    p['Speed'] = f(v[1] * dil / 100)
    notes.append('a world 1 cm/s (5 real) start rise, the random sideways acceleration (+-0.25 m/s^2) and 0.15 of the gun velocity are not carried over')
    # up acceleration -> negative gravity; drag
    alo, ahi = minmax(ms['Acceleration']['Acceleration'], 3)
    up_lo, up_hi = alo[2] * dil * dil / 100, ahi[2] * dil * dil / 100
    p['GravityScale'] = '-' + one_sided(up_lo / 9.81, up_hi / 9.81)
    drag = float(minmax(ms['AccelerationDrag']['DragCoefficientRaw'], 1)[1][0]) if isinstance(ms['AccelerationDrag']['DragCoefficientRaw'], dict) else float(ms['AccelerationDrag']['DragCoefficientRaw'])
    p['AirResistance'] = f(drag * dil)
    # size: X only (camera-facing), x the speed scale of a mean particle
    sa, sb = minmax(ms['Size']['StartSize'], 3)
    mt, mv = table(ms['SizeMultiplyLife']['LifeMultiplier'])
    life = 6.5; aup = 1.25; alat = 1 / np.sqrt(3); k = drag; v0 = float(np.linalg.norm((a + b) / 2))
    age = mt * life
    along = v0 * np.exp(-k * age)
    vup = aup / k + (1 - aup / k) * np.exp(-k * age)
    vlat = alat / k * (1 - np.exp(-k * age))
    speed = np.sqrt(along ** 2 + vup ** 2 + 2 * vlat ** 2)
    sss = ms['SizeScaleBySpeed']
    sc = np.clip((sss.get('SpeedScale') or (0.1, 0.1))[0] * speed, 1, (sss.get('MaxScale') or (2, 2))[0])
    curve = mv[:, 0] * sc
    pk = curve.max()
    cur = age_curve(mt, curve / pk)
    p['Size'] = one_sided(sa[0] * pk / 200, sb[0] * pk / 200) + (',ParticleAge=' + cur if cur else '')
    notes.append('size by speed for a mean particle: x%s at birth, x%s at the end' % (f(sc[0], 2), f(sc[-1], 2)))
    # colour, alpha: start alpha by emitter time x alpha over life
    _, cv = table(ms['Color']['StartColor'])
    p['Color'] = '(x=%s,y=%s,z=%s)' % tuple(f(x) for x in cv[0, :3])
    et, ev = table(ms['Color']['StartAlpha'])
    lt, lv = table(ms['ColorScaleOverLife']['AlphaScaleOverLife'])
    epk, lpk = ev[:, 0].max(), lv[:, 0].max()
    p['Alpha'] = f(epk * lpk, 4) + ',EmitterStrength=' + age_curve(et / et[-1], ev[:, 0] / epk) + ',ParticleAge=' + age_curve(lt, lv[:, 0] / lpk)
    tx, ty = M['tiles']
    p['Texture'] = TEX + M['tex'] + '.tif'
    p['TextureTiling'] = 'TilesX=%d,TilesY=%d,AnimFramesCount=%d,AnimBlend=true,FlipChance=0.5' % (tx, ty, tx * ty)
    rlo, rhi = minmax(ms['Rotation']['StartRotation'], 1)
    p['RandomAngles'] = 'y=%s' % f(min(180, (rhi[0] - rlo[0]) * 180))
    qlo, qhi = minmax(ms['RotationRate']['StartRotationRate'], 1)
    p['RandomRotationRate'] = 'y=%s' % f(max(abs(qlo[0]), abs(qhi[0])) * 360 * dil)
    pu, pv = ms['PivotOffset'].get('PivotOffset') or (0, 0)
    if abs(pv) > 1e-6: p['PivotY'] = f(2 * pv)
    if abs(pu) > 1e-6: p['PivotX'] = f(2 * pu)
    notes.append('its material fades it within 50 cm of the camera (CamFadeDistance); KCD2 has no such fade')
    p['SoftParticle'] = 'true'
    p['DiffuseLighting'] = '1'; p['DiffuseBacklighting'] = '1'; p['EmissiveLighting'] = '0'
    return p, notes


ORDER = ['Continuous', 'EmitterLifeTime', 'SpawnDelay', 'MoveRelativeEmitter', 'Count', 'ParticleLifeTime', 'PositionOffset', 'EmitAngle', 'Facing', 'OrientToVelocity', 'Texture', 'TextureTiling',
         'SoftParticle', 'Alpha', 'Color', 'DiffuseLighting', 'DiffuseBacklighting', 'EmissiveLighting', 'Size', 'Aspect', 'Speed',
         'AirResistance', 'GravityScale', 'InitAngles', 'RandomAngles', 'RandomRotationRate', 'PivotX', 'PivotY', 'CameraMinDistance']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--print-spec', action='store_true')
    a = ap.parse_args()
    out = []
    for sysname, (path, names) in SYSTEMS.items():
        mods, paths = load(path)
        out.append('\t<!-- %s: Ready or Not %s, by tools/ron/casc2pfx.py (do not edit by hand) -->' % (sysname, path.rsplit('/', 1)[1]))
        out.append('\t<Particles Name="%s">' % sysname)
        out.append('\t\t<Params Enabled="false" Count="1" ParticleLifeTime="0.05" />')
        out.append('\t\t<Childs>')
        for nm in names:
            E = emitter(mods, paths, nm)
            prm, notes = convert(E, nm)
            if a.print_spec: print(sysname, nm, E['mat'], prm, notes, file=sys.stderr)
            child = nm.lower().replace(' ', '_')
            out.append('\t\t\t<Particles Name="%s">' % child)
            if notes: out.append('\t\t\t\t<!-- ' + '; '.join(notes) + ' -->')
            out.append('\t\t\t\t<Params ' + ' '.join('%s="%s"' % (k, prm[k]) for k in ORDER if k in prm) + ' />')
            out.append('\t\t\t</Particles>')
        out.append('\t\t</Childs>')
        out.append('\t</Particles>')
    mods, paths = load(BARREL[0])
    prm, notes = linger(mods, paths, BARREL[1])
    if a.print_spec: print('barrel_smoke_ron LingerSmoke', prm, notes, file=sys.stderr)
    out.append('\t<!-- barrel_smoke_ron: Ready or Not P_BarrelSmokeTrail (LingerSmoke at its HeatSmoke\'s x%s time), by tools/ron/casc2pfx.py (do not edit by hand) -->' % f(BARREL[1]))
    out.append('\t<Particles Name="barrel_smoke_ron">')
    out.append('\t\t<!-- ' + '; '.join(notes) + ' -->')
    out.append('\t\t<Params ' + ' '.join('%s="%s"' % (k, prm[k]) for k in ORDER if k in prm) + ' />')
    out.append('\t</Particles>')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
