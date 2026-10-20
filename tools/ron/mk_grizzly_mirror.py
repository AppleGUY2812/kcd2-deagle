#!/usr/bin/env python3
"""The Grizzly MK V's "mirror" finish (deagle_skin mirror; the user 2026-10-20: "are we able to do like a silver mirror
finish"): Ready or Not's Silver body made polished. Private use (Ready or Not assets), never published.

    python3 -I tools/ron/mk_grizzly_mirror.py [rip root] [tif out dir]

Ready or Not's Silver (MI_PIS_GRIZZLY_Body_Silver, read by mk_grizzly_tex.py): 85 % of the texels are metal (ORM.B >= 0.5)
with base colour ~0.43 sRGB, i.e. a reflectance of ~0.14 at the 0.9 metallic cap, and roughness ~0.42: brushed and
dull. Mirror, on those metal texels only:
- reflectance = polished silver (F0 0.95, 0.93, 0.88 linear), times the base colour's own variation around its mean
  (clipped 0.6-1.1) so the engraving and edge wear still read;
- roughness 0.03 + 0.45 x (RoN's - 0.30), so its wear pattern stays (RoN 0.30-0.59 -> 0.03-0.16, median 0.07), plus a
  fine polish grain (blurred noise, +-0.015): a single flat roughness (v1: RoN's x 0.12, 0.036-0.071) mirrored the
  world as one smooth gradient and read as chrome-painted plastic (the user 2026-10-20: "less plastic looking");
- specular occlusion from Ready or Not's own baked AO (ORM.R, which RoN never samples): a mirror lights up its cavities
  and the inside of the slide with the sky otherwise, a plastic giveaway. Frostbite's approximation (Lagarde & de
  Rousiers, "Moving Frostbite to PBR" 2014: so = sat((NdotV + ao)^exp2(-16 r - 1) - 1 + ao)) baked at NdotV 0.5;
- the normal map unchanged (the engraving is in it);
- metallic 1 (no diffuse).
The black parts (sights, grip screws: ORM.B < 0.5) stay exactly as the Silver converts. The sight dots' emissive is the
Silver's (the .mtl points at ron_grizzly_silver_emit_diff). The .mtl files are the Silver's with the body textures and
its slot name swapped (MI_PIS_GRIZZLY_Body_Mirror: ron_vm_mtl.py maps MI_PIS_GRIZZLY_Body to it).
"""
import os, re, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk_grizzly_tex as g  # noqa: E402  (reads the same [rip root] [tif out dir] arguments)

F0 = np.array([0.95, 0.93, 0.88])   # polished silver, linear
ROUGH_BASE, ROUGH_GAIN, ROUGH_FROM = 0.03, 0.45, 0.30
GRAIN = 0.015          # polish grain amplitude in roughness
GRAIN_SIGMA = 1.5      # texels


def blur(a, sigma):
    """separable gaussian blur, wrapping (the noise has no edges to care about)"""
    r = int(3 * sigma + 0.5)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2); k /= k.sum()
    for axis in (0, 1):
        a = sum(w * np.roll(a, i - r, axis) for i, w in enumerate(k))
    return a


def convert():
    _, b, n, orm, _, cap, _ = g.MATS['silver']
    base = g.lin(np.clip(g.load(b), 0, 1)); o = g.load(orm)
    metal = o[..., 2] >= 0.5
    m = np.where(metal, 1.0, np.minimum(o[..., 2], cap))[..., None]
    lum = base.mean(-1)
    detail = np.clip(lum / lum[metal].mean(), 0.6, 1.1)[..., None]
    rng = np.random.default_rng(20261020)   # fixed: the same grain every build
    grain = blur(rng.standard_normal(metal.shape), GRAIN_SIGMA)
    grain *= GRAIN / max(grain.std() * 2, 1e-9)
    polished = np.clip(ROUGH_BASE + ROUGH_GAIN * (o[..., 1] - ROUGH_FROM) + grain, 0.02, 0.3)
    rough = np.where(metal, polished, o[..., 1])
    ao = o[..., 0]
    so = np.clip((0.5 + ao) ** np.exp2(-16 * polished - 1) - 1 + ao, 0, 1)[..., None]
    spec = np.where(metal[..., None], F0 * detail * so, 0.04 * (1 - m) + base * m)
    g.save(g.srgb(base * (1 - m)), 'ron_grizzly_mirror_diff', 'RGB')
    g.save(g.srgb(spec), 'ron_grizzly_mirror_spec', 'RGB')
    nm = g.load(n)
    nm[..., 1] = 1 - nm[..., 1]                                    # UE green-down -> CryEngine green-up
    g.save(np.dstack([nm, 1 - rough]), 'ron_grizzly_mirror_ddna', 'RGBA')
    print('TEX mirror: metal %.3f, reflectance mean %.3f (silver %.3f), smoothness mean on metal %.3f (silver %.3f), '
          'specular occlusion mean on metal %.3f (%.0f %% under 0.5)' % (
        metal.mean(), spec[metal].mean(), (0.04 * 0.1 + base * 0.9)[metal].mean(), 1 - rough[metal].mean(),
        1 - o[..., 1][metal].mean(), so[..., 0][metal].mean(), 100 * (so[..., 0][metal] < 0.5).mean()))


def write_mtls():
    for f in sorted(os.listdir(g.MTL_DIR)):
        mm = re.fullmatch(r'ron_grizzly_silver((_ev\d+)?)\.mtl', f)
        if not mm:
            continue
        s = open(os.path.join(g.MTL_DIR, f)).read()
        s = s.replace('MI_PIS_GRIZZLY_Body_Silver', 'MI_PIS_GRIZZLY_Body_Mirror')
        for t in ('diff', 'spec', 'ddna'):
            s = s.replace(f'ron_grizzly_silver_{t}.tif', f'ron_grizzly_mirror_{t}.tif')
        s = s.replace('(tools/ron/mk_grizzly_tex.py;', '(tools/ron/mk_grizzly_mirror.py, polished from the Silver;', 1)
        out = os.path.join(g.MTL_DIR, f'ron_grizzly_mirror{mm.group(1)}.mtl')
        open(out, 'w').write(s)
        print('MTL', os.path.relpath(out, g.REPO))


if __name__ == '__main__':
    os.makedirs(g.OUT, exist_ok=True)
    convert()
    write_mtls()
