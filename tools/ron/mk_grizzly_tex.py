#!/usr/bin/env python3
"""Ready or Not's Grizzly MK V materials -> KCD2 Illum: TIFs for rc and the .mtl files. Private use (Ready or Not
assets), never published.

    python3 -I tools/ron/mk_grizzly_tex.py [rip root] [tif out dir]

What Ready or Not draws, read from its compiled base-pass pixel shaders (tools/ron/ron_mat.py; Weapon_1P_MASTER
ps_023, M_SWAT_Officer_Firstperson ps_020, MM_Shell_Casing ps_026):
- base colour = B (sRGB), saturated; normal = N (UE green-down, z rebuilt); roughness = ORM.G; metallic = ORM.B,
  capped at 0.9 on the gun master (Weapon_1P_MASTER: body, silver body, mag and rounds), uncapped on the arms and the
  casing; specular 0.5 (dielectric F0 0.08 x 0.5 = 0.04). ORM.R (occlusion) is never sampled.
- the rain layer (drops normal, wet roughness) is off: its "Blend" is 1 (pure N) and IsGunWet? 0.
- emissive (the sight dots, one 72 x 46 texel island) = Emissive x EmissiveColour x Emissive Intensity (0.18), divided
  by the eye adaptation: the same brightness on screen at every exposure. The 0.18 product (cb2[7].xyz) and a lerp
  to cb2[8].xyz by cb2[7].w taken as 0 are inferred from the parameter list, not read from the preshader.

KCD2 (Shaders/HWScripts/CryFX shadeLib.cfi:421, Illum.cfx:953, rc.ini presets by suffix):
- roughness = (1 - smoothness)^2 like UE's roughness^2, so smoothness = 1 - ORM.G exactly (ddna alpha).
- diffuse = base x (1 - metallic), spec = lerp(0.04, base, metallic), both in linear light, stored sRGB (_diff
  Albedo and _spec Reflectance are sRGB presets).
- normal: green flipped (KCD2 is green-up; every Grizzly map has 2x less curl as green-down, the casing's is flat
  enough to be ambiguous).
- emittance = Emittance.rgb x intensity x 0.1 (HDR, 1 = 10000 cd/m2) x the emittance map. Ready or Not's dot shows
  tonemap(EmissiveColour x 0.18) on screen whatever the exposure; KCD2's emittance is absolute, so the intensity is
  solved through KCD2's post chain per exposure step (kcd2_tonemap.EV_SCENES, the flash's ladder): <name>.mtl by day,
  <name>_ev9 / _ev8 / _ev7.mtl for dusk and night (Deagle:FlashEvSuffix picks the step).
"""
import os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kcd2_tonemap as kt  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RIP = sys.argv[1] if len(sys.argv) > 1 else '/mnt/e/Tools/deagle-work/ron/grizzly/ReadyOrNot'
OUT = sys.argv[2] if len(sys.argv) > 2 else '/mnt/e/Tools/deagle-work/ron/grizzly/tif'
MTL_DIR = os.path.join(REPO, 'src/Objects/manmade/weapons/ron_grizzly')
GAME_DIR = 'objects/manmade/weapons/ron_grizzly'
GRIZ = 'Plugins/GameFeatures/ReadyOrNotDLC3/Content/Weapons/Grizzly/Textures/'
SWAT = 'Content/ReadyOrNot/Character/SWAT/SWAT_Officer/'
SHELL = 'Content/ReadyOrNot/Assets/Weapons/Shell/357_Magnum/Texture/'
EMISSIVE = np.array([7.939507, 10.0, 6.614584]) * 0.18   # MI_PIS_GRIZZLY_Body(_Silver) EmissiveColour x Emissive Intensity

# out name: (UE material slot, base colour, normal, ORM, emissive or None, metallic cap, KCD2 surface)
MATS = {
    'body': ('MI_PIS_GRIZZLY_Body', GRIZ + 'GrizzlyMKV_Main_BaseColor', GRIZ + 'GrizzlyMKV_Main_Normal',
             GRIZ + 'GrizzlyMKV_Main_OcclusionRoughnessMetallic', GRIZ + 'GrizzlyMKV_Main_Emissive', 0.9, 'mat_metal'),
    'silver': ('MI_PIS_GRIZZLY_Body_Silver', GRIZ + 'Silver/GrizzlyMKV_Silver_BaseColor', GRIZ + 'Silver/GrizzlyMKV_Silver_Normal',
               GRIZ + 'Silver/GrizzlyMKV_Silver_OcclusionRoughnessMetallic', GRIZ + 'Silver/GrizzlyMKV_Silver_Emissive', 0.9, 'mat_metal'),
    'mag': ('MI_PIS_GRIZZLY_Mag', GRIZ + 'GrizMag_Main_BaseColor', GRIZ + 'GrizMag_Main_Normal',
            GRIZ + 'GrizMag_Main_OcclusionRoughnessMetallic', None, 0.9, 'mat_metal'),
    'arms': ('M_SWAT_Officer_Firstperson', SWAT + 'FP_Arms_01_B', SWAT + 'FP_Arms_01_N', SWAT + 'FP_Arms_01_ORM', None, 1.0, 'mat_cloth'),
    'casing': ('MI_Shell_Casing_45ACP', SHELL + 'Painter_357_Magnum_B', SHELL + 'Painter_357_Magnum_N', SHELL + 'Painter_357_Magnum_ORM',
               None, 1.0, 'mat_metal'),
}
# .mtl files: name -> sub-materials in id order (the psk material slots; the rounds use the mag's)
MTLS = {'ron_grizzly': ('body', 'mag', 'arms', 'casing'), 'ron_grizzly_silver': ('silver', 'mag', 'arms', 'casing'),
        'ron_grizzly_casing': ('casing',)}


def load(rel):
    return np.asarray(Image.open(os.path.join(RIP, rel + '.png')).convert('RGB')).astype(np.float64) / 255.0


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def save(a, name, mode):
    Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8), mode).save(os.path.join(OUT, name + '.tif'),
                                                                                 compression='tiff_deflate')


def convert(key):
    slot, b, n, orm, em, cap, _ = MATS[key]
    base = lin(np.clip(load(b), 0, 1)); o = load(orm)
    m = np.minimum(o[..., 2], cap)[..., None]
    save(srgb(base * (1 - m)), f'ron_grizzly_{key}_diff', 'RGB')
    save(srgb(0.04 * (1 - m) + base * m), f'ron_grizzly_{key}_spec', 'RGB')
    nm = load(n)
    nm[..., 1] = 1 - nm[..., 1]                                    # UE green-down -> CryEngine green-up
    save(np.dstack([nm, 1 - o[..., 1]]), f'ron_grizzly_{key}_ddna', 'RGBA')
    if em:
        save(load(em), f'ron_grizzly_{key}_emit_diff', 'RGB')
    print('TEX', key, slot, base.shape[1], 'x', base.shape[0], 'metallic mean %.3f' % m.mean(),
          'smoothness mean %.3f' % (1 - o[..., 1]).mean(), '+ emissive' if em else '')


def emittance():
    """per exposure step: (rgb with max 1, intensity) KCD2 shows as Ready or Not's dot (its filmic curve, the ACES fit
    flash_bake.py uses, on the exposure-free EmissiveColour x 0.18)"""
    x = EMISSIVE
    ron = np.clip(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)
    out = {}
    for suffix, kw in kt.EV_SCENES.items():
        s = kt.Scene(**kw); hdr = s.solve(kt.srgb(ron))
        out[suffix] = (hdr / hdr.max(), hdr.max() / 0.1, s.name)
    return out


def sub_xml(key, emit):
    slot, *_, surface = MATS[key]
    p = f'{GAME_DIR}/ron_grizzly_{key}'
    em, emtex = '', ''
    if MATS[key][4] and emit:
        rgb, inten, _ = emit
        em = ' Emittance="%s,%.4g"' % (','.join('%.3g' % c for c in rgb), inten)
        emtex = f'\n        <Texture Map="Emittance" File="{p}_emit_diff.tif" />'
    return f'''    <Material Name="{slot}" MtlFlags="524416" Shader="Illum" GenMask="24000000002000" StringGenMask="%NORMAL_MAP%SPECULAR_MAP" SurfaceType="{surface}" MatTemplate="" Diffuse="1,1,1" Specular="1,1,1" Opacity="1" Shininess="255"{em} vertModifType="0" LayerAct="1">
      <Textures>
        <Texture Map="Diffuse" File="{p}_diff.tif" />
        <Texture Map="Bumpmap" File="{p}_ddna.tif" />
        <Texture Map="Specular" File="{p}_spec.tif" />{emtex}
      </Textures>
      <PublicParams EmittanceMapGamma="1" SSSIndex="0" />
    </Material>'''


def write_mtls(ems):
    os.makedirs(MTL_DIR, exist_ok=True)
    for name, keys in MTLS.items():
        steps = ems.items() if any(MATS[k][4] for k in keys) else [('', None)]
        for suffix, emit in steps:
            note = (f'emittance solved through KCD2\'s post chain ({emit[2]}) to show Ready or Not\'s sight dots'
                    if emit else 'no emissive')
            subs = '\n'.join(sub_xml(k, emit) for k in keys)
            f = os.path.join(MTL_DIR, name + suffix + '.mtl')
            open(f, 'w').write(f'''<!-- Ready or Not's Grizzly MK V ({', '.join(MATS[k][0] for k in keys)}) for KCD2 (tools/ron/mk_grizzly_tex.py;
     private use, never published): {note} -->
<Material MtlFlags="524544" vertModifType="0">
  <SubMaterials>
{subs}
  </SubMaterials>
</Material>
''')
            print('MTL', os.path.relpath(f, REPO), 'emittance %s x %.4g' % (np.round(emit[0], 3), emit[1]) if emit else '')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for k in MATS:
        convert(k)
    write_mtls(emittance())
