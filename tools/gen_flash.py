#!/usr/bin/env python3
"""Writes the muzzle flash effects into src/Libs/Particles/deagle.xml (between the GENERATED FLASH markers):
- muzzle / muzzle_large: spawned per shot (flashMode "gun"). Full size from the first frame, no growth, no drift (the
  tongues keep a crawl of speed only so they stay pointed outward), held at full brightness then cut, 0.08 s (KCD2
  draws a new emitter a frame or two late and pre-ages it a frame: shorter parts were never drawn).
- muzzle_live / muzzle_large_live: one emitter per drawn gun that never stops (flashMode "live"). Its particles are
  always alive with a random tile and turn each; the plugin keeps it far below the world and puts it on the muzzle
  for exactly the frames a shot is seen, so the flash is there the frame it's fired and gone the next (Ready or Not:
  every flash part lives 0.035-0.04 s, full size at once; docs/muzzle_flash_research.md). The light rides the bloom.
Light radius from the blast (7 m x G^1.5, see the library comment). usage: gen_flash.py <repo>"""
import os, re, sys

repo = sys.argv[1]
path = os.path.join(repo, "src", "Libs", "Particles", "deagle.xml")
TEX = "textures/particles/deagle/"
HOT = "(x=1,y=0.88,z=0.72)"          # white-yellow, the hot core of a flash

def parts(life, light_radius, light_int, live, em=1.0, sz=1.0):
    cont = ' Continuous="true"' if live else ""
    plife = 0.04 if live else life
    cut = "1,ParticleAge=(v=1;t=0.85,v=1;t=1)"                     # full, then gone
    def E(v): return f'EmissiveLighting="{v * em:g}"'
    def S(v, rest=""): return f'Size="{v * sz:g}{rest}"'
    return [
        ("bloom", f'Count="1"{cont} ParticleLifeTime="{plife}" PositionOffset="y=0.01" BlendType="Additive" '
                  f'Texture="{TEX}deagle_flash_ring_diff.tif" TextureTiling="TilesX=2,TilesY=2,VariantCount=4" SoftParticle="Softness=0.2" '
                  f'Alpha="{cut}" Color="{HOT}" DiffuseBacklighting="1" {E(5)} '
                  + ("" if live else f'LightSource="Radius={light_radius},Intensity={light_int}" ') +
                  f'MoveRelativeEmitter="true" {S(0.13, ",Random=0.25")} '
                  f'AirResistance="WindScale=0" RandomAngles="y=180" SortOffset="3"'),
        ("front", f'Count="1"{cont} ParticleLifeTime="{plife}" PositionOffset="y=0.03" BlendType="Additive" '
                  f'Texture="{TEX}deagle_flash_front_diff.tif" TextureTiling="TilesX=2,TilesY=2,VariantCount=4" SoftParticle="Softness=0.2" '
                  f'Alpha="{cut}" Color="(x=1,y=0.82,z=0.64)" DiffuseBacklighting="1" {E(4)} MoveRelativeEmitter="true" '
                  f'{S(0.2, ",Random=0.3")} AirResistance="WindScale=0" RandomAngles="y=180" SortOffset="4"'),
        ("tongues", f'Count="2,Random=0.5"{cont} ParticleLifeTime="{plife}" EmitAngle="9,Random=0.5" Facing="CameraX" OrientToVelocity="true" '
                    f'BlendType="Additive" Texture="{TEX}deagle_flash_side_diff.tif" TextureTiling="TilesX=4,TilesY=2,VariantCount=8" '
                    f'SoftParticle="Softness=0.2" Alpha="{cut}" Color="(x=1,y=0.84,z=0.64)" DiffuseBacklighting="1" {E(4)} '
                    f'MoveRelativeEmitter="true" {S(0.11, ",Random=0.3")} Aspect="0.5" PivotY="-0.8" Speed="0.01" AirResistance="WindScale=0" SortOffset="5"'),
        ("core", f'Count="1"{cont} ParticleLifeTime="{plife}" PositionOffset="y=0.005" BlendType="Additive" '
                 f'Texture="{TEX}deagle_flash_ring_diff.tif" TextureTiling="TilesX=2,TilesY=2,VariantCount=4" Alpha="{cut}" '
                 f'Color="(x=1,y=0.94,z=0.84)" {E(14)} MoveRelativeEmitter="true" {S(0.05, ",Random=0.2")} '
                 f'AirResistance="WindScale=0" RandomAngles="y=180" SortOffset="6"'),
        ("sparks", f'Count="2"{cont} ParticleLifeTime="{0.06 if live else 0.08}" EmitAngle="12,Random=0.5" Facing="CameraX" OrientToVelocity="true" '
                   f'BlendType="Additive" Texture="{TEX}deagle_sparks_diff.tif" TextureTiling="TilesX=4,VariantCount=4" '
                   f'Alpha="1.2,ParticleAge=(v=1;t=0.7,v=0.8;t=1)" Color="(x=1,y=0.85,z=0.65)" {E(25)} MoveRelativeEmitter="true" '
                   f'{S(0.07, ",Random=0.3")} Aspect="0.6" PivotY="-0.8" Speed="2,Random=0.3" AirResistance="WindScale=0"'),
    ]

def effect(name, life, radius, intensity, live, em=1.0, sz=1.0):
    out = [f'\t<Particles Name="{name}">', '\t\t<Params Enabled="false" Count="1" ParticleLifeTime="0.05" />', '\t\t<Childs>']
    for pn, prm in parts(life, radius, intensity, live, em, sz):
        out += [f'\t\t\t<Particles Name="{pn}">', f'\t\t\t\t<Params {prm} />', '\t\t\t</Particles>']
    out += ['\t\t</Childs>', '\t</Particles>']
    return "\n".join(out)

# The mesh flash's light (and the live flash's): warm like Ready or Not's flash, not white (its recorded body
# (254, 205, 145) and rim (238, 138, 97)), and LIGHT_AHEAD m in front of the muzzle: on the muzzle, a few cm from the
# slide and gloves, inverse-square burnt them to flat white and lit a night forest ~5x (user's night clip, 2026-10-14:
# "very hard"; Ready or Not's light is weak, +2-8 % next to the gun, docs/muzzle_flash_research.md section 7)
LIGHT_COLOR = "(x=1,y=0.7,z=0.45)"
LIGHT_AHEAD = 0.5

def light(name, radius, intensity):
    # one dot-sized particle carrying the light, spawned per shot (a particle light's radius doesn't follow the
    # emitter's size, so the live emitter can't hide its light); the light's colour is the particle's
    return "\n".join([f'\t<Particles Name="{name}">',
        f'\t\t<Params Count="1" ParticleLifeTime="0.06" BlendType="Additive" Texture="{TEX}deagle_flash_ring_diff.tif" '
        f'TextureTiling="TilesX=2,TilesY=2,VariantCount=4" Alpha="1,ParticleAge=(v=1;t=0.85,v=1;t=1)" Color="{LIGHT_COLOR}" '
        f'EmissiveLighting="1" LightSource="Radius={radius},Intensity={intensity}" PositionOffset="y={LIGHT_AHEAD}" '
        f'MoveRelativeEmitter="true" Size="0.001" AirResistance="WindScale=0" />', '\t</Particles>'])

blocks = [
    effect("muzzle", 0.08, 7, 0.6, False),
    effect("muzzle_large", 0.08, 21, 1.25, False),
    effect("muzzle_live", 0.04, 7, 0.6, True),
    effect("muzzle_large_live", 0.04, 21, 1.25, True),
    light("muzzle_light", 5, 0.15),          # Glock-size blasts
    light("muzzle_large_light", 10, 0.3),    # the Deagle
]
# look variants to compare live against Ready or Not (deagle_set muzzleFx deagle.muzzle_b4 + muzzleFxLarge
# deagle.muzzle_large_b4): brighter (emissive x), bigger (size x), stronger light (x3 on the bigger ones)
for tag, em, sz, lk in (("b4", 4, 1, 1), ("b4s15", 4, 1.5, 3), ("b10s2", 10, 2, 3)):
    blocks.append(effect(f"muzzle_{tag}", 0.08, 7, 0.6 * lk, False, em, sz))
    blocks.append(effect(f"muzzle_large_{tag}", 0.08, 21, 1.25 * lk, False, em, sz))
xml = open(path, encoding="ascii").read()
gen = "\t<!-- GENERATED FLASH (tools/gen_flash.py) -->\n" + "\n".join(blocks) + "\n\t<!-- /GENERATED FLASH -->"
if "<!-- GENERATED FLASH" in xml:
    xml = re.sub(r"\t<!-- GENERATED FLASH.*?<!-- /GENERATED FLASH -->", lambda m: gen, xml, flags=re.S)
else:   # first run: replace the hand-written muzzle and muzzle_large
    start = xml.index('\t<Particles Name="muzzle">')
    end = xml.index('\t<Particles Name="muzzle_smoke">')
    xml = xml[:start] + gen + "\n" + xml[end:]
open(path, "w", encoding="ascii", newline="\n").write(xml)
print("FLASH effects:", ", ".join(b.split('"')[1] for b in blocks))
